// SPDX-License-Identifier: GPL-3.0-or-later
#include "dashboard.hpp"

#include "core/save_file.hpp"
#include "gfx/renderer.hpp"
#include "image_file.hpp"
#include "ui/components/component.hpp"

#include <GL/glcorearb.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace ps5sd {

namespace {

using hui::gfx::Align;
using hui::gfx::Color;
using hui::gfx::DrawList;
using hui::gfx::Rect;
using State = AppStatus::State;

// ---- the design language (after the kit's Pulse dashboard) --------------------

const Color kInk       = Color::rgb(0xe9eefc);
const Color kClear     = Color::rgb(0x000000, 0.0f);
const Color kBlack     = Color::rgb(0x000000);
const Color kPanelTop  = Color::rgb(0x171f33);
const Color kPanelBot  = Color::rgb(0x0e1424);
const Color kWell      = Color::rgb(0x070b16);
const Color kCyan      = Color::rgb(0x4fe0ff);
const Color kLime      = Color::rgb(0xb8f26b);
const Color kAmber     = Color::rgb(0xffc24a);
const Color kRose      = Color::rgb(0xff6f9c);
const Color kViolet    = Color::rgb(0xb48cff);
const Color kSteel     = Color::rgb(0x8d9ab8);

constexpr float kMuted = 0.62f;  // alpha of secondary text
constexpr float kFaint = 0.38f;  // alpha of tertiary text and empty states

// The grid, inside the safe area: a wide column of server, activity and recent
// images; a narrow one of the picture, the memory and the log.
constexpr Rect kServer{ 96, 152, 1164, 160 };
constexpr Rect kActivity{ 96, 332, 1164, 396 };
constexpr Rect kRecent{ 96, 748, 1164, 236 };
constexpr Rect kImage{ 1284, 152, 540, 576 };
constexpr Rect kMemory{ 1284, 748, 540, 92 };
constexpr Rect kLogTile{ 1284, 860, 540, 124 };
constexpr float kRadius = 28.0f;
constexpr float kPad    = 32.0f;
constexpr int   kThumbs = 6;

constexpr const char * kDot = "\xC2\xB7";  // middle dot

Color with_a(Color c, float a) {
    c.a = a;
    return c;
}

std::string seconds_text(double s) {
    char text[32];
    if (s < 0) {
        return "-";
    }
    if (s < 60) {
        std::snprintf(text, sizeof(text), s < 10 ? "%.1f s" : "%.0f s", s);
    } else {
        const int whole = (int) (s + 0.5);
        std::snprintf(text, sizeof(text), "%d:%02d", whole / 60, whole % 60);
    }
    return text;
}

// sd.cpp's log lines start "[INFO ] stable-diffusion.cpp:2340 - "; on a
// small tile the words matter more.
std::string log_text(const std::string & line) {
    if (!line.empty() && line[0] == '[') {
        const std::size_t sep = line.find(" - ");
        if (sep != std::string::npos && sep < 64) {
            return line.substr(sep + 3);
        }
    }
    return line;
}

// A rectangle of the aspect w:h fitted (contained) in `area`.
Rect contain(const Rect & area, int w, int h) {
    if (w <= 0 || h <= 0) {
        return area;
    }
    const float scale = std::min(area.w / (float) w, area.h / (float) h);
    const float fw    = (float) w * scale;
    const float fh    = (float) h * scale;
    return { area.cx() - fw * 0.5f, area.cy() - fh * 0.5f, fw, fh };
}

hui::ui::Theme make_theme() {
    hui::ui::Theme t = hui::ui::default_theme();
    t.id             = "ps5sd";
    t.name           = "PS5SD";
    t.family         = "Night tiles";
    t.summary        = "The kit's Pulse dashboard language";
    t.page           = Color::rgb(0x0a0e1a);
    t.surface        = Color::rgb(0x131a2c);
    t.surface_high   = Color::rgb(0x202a45);
    t.text           = kInk;
    t.text_muted     = with_a(kInk, kMuted);
    t.primary        = kCyan;
    t.on_primary     = Color::rgb(0x0a0e1a);
    t.secondary      = t.surface_high;
    t.on_secondary   = kInk;
    t.accent         = kCyan;
    t.outline        = with_a(kInk, 0.12f);
    t.focus          = kCyan;
    t.shadow         = with_a(kBlack, 0.5f);
    t.light          = with_a(kInk, 0.2f);
    t.danger         = kRose;
    t.success        = kLime;
    t.warning        = kAmber;
    t.style          = hui::ui::SurfaceStyle::flat;
    t.corner         = hui::ui::Corner::round;
    t.radius         = 12.0f;
    t.radius_card    = kRadius;
    t.border         = 1.5f;
    t.shadow_offset  = 0.0f;
    t.shadow_blur    = 0.0f;
    t.pill_chips     = true;
    t.heading        = hui::ui::FontRole::display;
    t.label          = hui::ui::FontRole::semibold;
    t.caps           = false;
    t.tracking       = 0.0f;
    t.omega          = 16.0f;
    t.damping        = 1.0f;
    t.dark           = true;
    return t;
}

}  // namespace

Dashboard::Dashboard() = default;

Dashboard::~Dashboard() {
    if (loaded_) {
        release(preview_);
        for (Picture & p : pictures_) {
            release(p.thumb);
            release(p.large);
        }
    }
}

bool Dashboard::load_font(const std::string & path, hui::gfx::Font & font, hui::ui::FontRef & ref) {
    std::string data;
    if (!hui::save::read_file(path, &data) || !font.load(data)) {
        std::printf("ps5sd: font %s failed\n", path.c_str());
        return false;
    }
    ref.font    = &font;
    ref.texture = renderer_->batch().create_font_texture(font);
    return true;
}

bool Dashboard::load(hui::gfx::Renderer & renderer, const std::string & font_path) {
    renderer_ = &renderer;
    const std::size_t slash = font_path.find_last_of('/');
    const std::string dir   = slash == std::string::npos ? std::string(".") : font_path.substr(0, slash);
    if (!load_font(dir + "/inter-regular.huifont", regular_font_, fonts_.regular) ||
        !load_font(dir + "/inter-semibold.huifont", semibold_font_, fonts_.semibold) ||
        !load_font(dir + "/montserrat-medium.huifont", display_font_, fonts_.display) ||
        !load_font(font_path, mono_font_, fonts_.mono)) {
        return false;
    }
    // The components look their fonts up by role; the two faces the dashboard
    // does not use stand in for a loaded one so no role is ever empty.
    fonts_.pixel = fonts_.regular;
    fonts_.hand  = fonts_.regular;

    theme_ = make_theme();
    backdrop_.mode      = hui::gfx::BackdropMode::dots;
    backdrop_.colors[0] = Color::rgb(0x05070f);
    backdrop_.colors[1] = Color::rgb(0x0b1122);
    backdrop_.colors[2] = Color::rgb(0x2b3d66);

    state_chip_.style.theme       = theme_;
    state_chip_.style.leading_dot = true;
    state_chip_.style.height      = 46;
    state_chip_.style.text_size   = 19;
    state_chip_.style.padding     = 22;
    state_chip_.style.align       = Align::right;
    state_chip_.set_bounds({ 1824 - 420, 70, 420, 46 });

    progress_.style.theme     = theme_;
    progress_.style.height    = 22;
    progress_.style.text_size = 20;
    progress_.style.text_gap  = 12;
    progress_.style.sheen     = true;

    // The memory: a bar of what is free, coloured by how much is left.
    memory_.style.theme        = theme_;
    memory_.style.height       = 14;
    memory_.style.percent      = false;
    memory_.style.placement    = hui::ui::LabelPlacement::none;
    memory_.style.finish_flash = false;

    spinner_.style.theme = theme_;
    spinner_.style.kind  = hui::ui::SpinnerKind::arc;

    empty_.style.theme          = theme_;
    empty_.style.icon_size      = 88;
    empty_.style.title_size     = 30;
    empty_.style.body_size      = 21;
    empty_.style.max_text_width = 380;
    empty_.style.max_lines      = 3;
    empty_.style.panel          = false;
    empty_.icon = [this](hui::ui::Canvas & canvas, const Rect & area) {
        const bool busy = snap_.state == State::starting || snap_.state == State::loading ||
                          snap_.state == State::generating;
        if (busy) {
            hui::ui::Spinner spinner = spinner_;
            spinner.style.color      = with_a(accent_.value(), 1.0f);
            spinner.style.status     = hui::ui::Status::accent;
            spinner.set_bounds(area);
            spinner.draw(canvas);
            return;
        }
        // A picture frame with a sun and two hills, from shapes: not every
        // face has an icon glyph.
        DrawList & list   = canvas.list;
        const Color line  = with_a(kInk, 0.34f);
        const Rect  frame = area.inset(area.w * 0.1f);
        list.bordered_rect(frame, 16, kClear, 4.0f, line);
        list.circle(frame.x + frame.w * 0.72f, frame.y + frame.h * 0.3f, frame.w * 0.075f, line);
        list.triangle({ frame.x + frame.w * 0.08f, frame.y + frame.h * 0.5f, frame.w * 0.42f, frame.h * 0.4f },
                      with_a(kInk, 0.22f));
        list.triangle({ frame.x + frame.w * 0.4f, frame.y + frame.h * 0.58f, frame.w * 0.5f, frame.h * 0.32f },
                      with_a(kInk, 0.16f));
    };

    accent_.snap(state_color(State::starting));
    accent_set_ = true;
    reveal_.snap(1.0f);
    loaded_ = true;
    return true;
}

// ---- state to look -------------------------------------------------------------

Color Dashboard::state_color(State state) const {
    switch (state) {
        case State::ready:      return kLime;
        case State::generating: return kCyan;
        case State::error:      return kRose;
        case State::stopped:    return kSteel;
        case State::starting:
        case State::loading:
        default:                return kAmber;
    }
}

// ---- textures ------------------------------------------------------------------

void Dashboard::upload(Texture & texture, int width, int height, const uint8_t * rgba) {
    release(texture);
    texture.id     = renderer_->batch().create_texture(width, height, rgba);
    texture.width  = width;
    texture.height = height;
}

void Dashboard::release(Texture & texture) {
    if (texture.id != 0) {
        const GLuint id = texture.id;
        glDeleteTextures(1, &id);
    }
    texture = Texture{};
}

void Dashboard::sync_preview(const AppStatus::Snapshot & s) {
    if (!s.preview) {
        release(preview_);
        preview_serial_ = 0;
        return;
    }
    if (s.preview_serial == preview_serial_ && preview_.id != 0) {
        return;
    }
    const AppStatus::Frame & f = *s.preview;
    const std::size_t        n = (std::size_t) f.width * (std::size_t) f.height;
    if (f.rgb.size() < n * 3) {
        return;
    }
    scratch_.resize(n * 4);
    for (std::size_t i = 0; i < n; i++) {
        scratch_[i * 4 + 0] = f.rgb[i * 3 + 0];
        scratch_[i * 4 + 1] = f.rgb[i * 3 + 1];
        scratch_[i * 4 + 2] = f.rgb[i * 3 + 2];
        scratch_[i * 4 + 3] = 255;
    }
    upload(preview_, f.width, f.height, scratch_.data());
    preview_serial_ = s.preview_serial;
}

// The finished images the snapshot lists, newest first: kept in step, and
// decoded one per frame (a 1024 px PNG takes a few tens of milliseconds on
// the console; one a frame keeps a burst of results from stalling it).
void Dashboard::sync_pictures(const AppStatus::Snapshot & s) {
    const std::size_t keep = std::min<std::size_t>(s.recent.size(), kThumbs);
    std::vector<Picture> next;
    for (std::size_t i = 0; i < keep; i++) {
        const AppStatus::Result & wanted = s.recent[i];
        auto it = std::find_if(pictures_.begin(), pictures_.end(), [&](const Picture & p) { return p.path == wanted.path; });
        if (it != pictures_.end()) {
            next.push_back(std::move(*it));
            pictures_.erase(it);
        } else {
            Picture p;
            p.path = wanted.path;
            next.push_back(std::move(p));
        }
        next.back().prompt = wanted.prompt;
    }
    for (Picture & gone : pictures_) {  // what fell out of the list
        release(gone.thumb);
        release(gone.large);
    }
    pictures_ = std::move(next);

    for (std::size_t i = 0; i < pictures_.size(); i++) {
        Picture & p = pictures_[i];
        if (i != 0) {
            release(p.large);  // only the newest is shown large
        }
        if (p.failed) {
            continue;
        }
        const bool need_large = i == 0 && p.large.id == 0;
        if (p.thumb.id != 0 && !need_large) {
            continue;
        }
        Rgba image;
        if (!load_image_rgba(p.path, need_large ? 1024 : 256, image)) {
            std::printf("ps5sd: dashboard: cannot read %s\n", p.path.c_str());
            p.failed = true;
            continue;
        }
        if (need_large) {
            upload(p.large, image.width, image.height, image.pixels.data());
        }
        if (p.thumb.id == 0) {
            const Rgba small = shrink_to(image, 256);
            upload(p.thumb, small.width, small.height, small.pixels.data());
        }
        break;  // one decode a frame
    }
}

// ---- the frame's state ------------------------------------------------------------

void Dashboard::update(const AppStatus::Snapshot & s, float dt) {
    if (!loaded_) {
        return;
    }
    dt    = std::clamp(dt, 0.0f, 0.1f);
    snap_ = s;
    clock_ += dt;
    age_ += dt;

    accent_.target(state_color(s.state));
    accent_.update(dt, 6.0f);
    sync_preview(s);
    sync_pictures(s);

    std::string key = "none";
    if (s.state == State::generating && preview_.id != 0) {
        key = "preview";
    } else if (!pictures_.empty() && pictures_[0].large.id != 0) {
        key = "pic:" + pictures_[0].path;
    }
    if (key != shown_key_) {
        shown_key_ = key;
        reveal_.snap(key == "preview" || key == "none" ? 1.0f : 0.0f);
    }
    reveal_.target = 1.0f;
    reveal_.update(dt, 7.0f);

    sync_components(dt);
}

void Dashboard::settle() {
    age_ = 10.0f;
    accent_.snap(state_color(snap_.state));
    reveal_.snap(1.0f);
}

void Dashboard::sync_components(float dt) {
    const AppStatus::Snapshot & s = snap_;
    const Color accent            = accent_.value();

    // The chip in the header.
    static const char * const kNames[] = { "STARTING", "LOADING", "READY", "GENERATING", "ERROR", "STOPPED" };
    state_chip_.label = kNames[(int) s.state];
    switch (s.state) {
        case State::ready:      state_chip_.style.dot = hui::ui::Status::success; break;
        case State::generating: state_chip_.style.dot = hui::ui::Status::accent;  break;
        case State::error:      state_chip_.style.dot = hui::ui::Status::danger;  break;
        case State::stopped:    state_chip_.style.dot = hui::ui::Status::neutral; break;
        default:                state_chip_.style.dot = hui::ui::Status::warning; break;
    }
    state_chip_.update(dt);

    // The bar: loading counts tensors, sampling counts steps.
    const bool sampling = s.state == State::generating;
    const bool loading  = s.state == State::loading || s.state == State::starting;
    const bool show     = sampling || (loading && s.state == State::loading);
    progress_.style.color = with_a(accent, 1.0f);
    if (show) {
        const Rect tile = kActivity;
        progress_.set_bounds({ tile.x + kPad, tile.y + tile.h - 126, tile.w - 2 * kPad, 62 });
        char label[64];
        if (sampling) {
            if (s.steps > 0 && s.step <= 0) {
                std::snprintf(label, sizeof(label), "Preparing the first step");
                progress_.style.mode = hui::ui::ProgressMode::indeterminate;
            } else if (s.steps > 0 && s.step >= s.steps) {
                std::snprintf(label, sizeof(label), "Decoding the image");
                progress_.style.mode = hui::ui::ProgressMode::determinate;
                progress_.set_value(1.0f, !progress_shown_);
            } else if (s.steps > 0) {
                std::snprintf(label, sizeof(label), "Step %d of %d", s.step, s.steps);
                progress_.style.mode = hui::ui::ProgressMode::determinate;
                progress_.set_value((float) s.step / (float) s.steps, !progress_shown_);
            } else {
                std::snprintf(label, sizeof(label), "Working");
                progress_.style.mode = hui::ui::ProgressMode::indeterminate;
            }
        } else if (s.steps > 0) {
            std::snprintf(label, sizeof(label), "Loading the model");
            progress_.style.mode = hui::ui::ProgressMode::determinate;
            progress_.set_value((float) s.step / (float) s.steps, !progress_shown_);
        } else {
            std::snprintf(label, sizeof(label), "Loading the model");
            progress_.style.mode = hui::ui::ProgressMode::indeterminate;
        }
        progress_.label = label;
    }
    progress_shown_ = show;
    progress_.update(dt);

    // Memory: a bar of what is free, turning amber and then red as it runs out.
    memory_.set_bounds({ kMemory.x + 24, kMemory.y + 46, kMemory.w - 48, 28 });
    if (s.free_gib >= 0 && s.pool_gib > 0) {
        const float free = (float) std::clamp(s.free_gib / s.pool_gib, 0.0, 1.0);
        memory_.set_value(free);
        memory_.style.color = free > 0.3f ? kLime : (free > 0.12f ? kAmber : kRose);
    } else {
        memory_.set_value(0.0f, true);
    }
    memory_.update(dt);

    spinner_.update(dt);

    // The empty picture area.
    switch (s.state) {
        case State::starting:
        case State::loading:
            empty_.title = "Getting ready";
            empty_.body  = "The model is loading. Your images will show up here.";
            break;
        case State::generating:
            empty_.title = "Generating";
            empty_.body  = "This request has no live preview. The image appears here when it is done.";
            break;
        case State::error:
            empty_.title = "Something went wrong";
            empty_.body  = "The log below and /data/PS5SD/app.log say what.";
            break;
        case State::stopped:
            empty_.title = "Server stopped";
            empty_.body.clear();
            break;
        default:
            empty_.title = "No images yet";
            empty_.body  = "Send a prompt from the web UI and the picture shows up here.";
            break;
    }
    empty_.set_bounds({ kImage.x + 20, kImage.y + 58, kImage.w - 40, kImage.w - 40 });
    empty_.update(dt);
}

// ---- drawing ---------------------------------------------------------------------

hui::ui::Canvas Dashboard::canvas(DrawList & list) const {
    return hui::ui::Canvas{ list, fonts_, 0, clock_ };
}

float Dashboard::caps(DrawList & list, const std::string & text, float x, float baseline, float size, Color color,
                      Align align) const {
    return hui::ui::text(list, fonts_.semibold, hui::ui::upper(text), x, baseline, size, color, align, 3.0f);
}

void Dashboard::draw_panel(DrawList & list, const Rect & r, float radius, Color accent, float lit) const {
    if (lit > 0.01f) {
        list.shadow({ r.x, r.y + 14, r.w, r.h }, radius, 36, with_a(kBlack, 0.45f * lit));
        list.glow(r, radius, 24, with_a(accent, 0.13f * lit));
    }
    list.gradient_rect(r, radius, hui::gfx::mix(kPanelTop, accent, 0.05f * (0.3f + lit)).with_alpha(0.92f),
                       hui::gfx::mix(kPanelBot, accent, 0.02f * (0.3f + lit)).with_alpha(0.94f));
    list.bordered_rect(r, radius, kClear, 1.5f, hui::gfx::mix(with_a(kInk, 0.09f), with_a(accent, 0.55f), lit));
}

void Dashboard::compose(hui::gfx::Renderer & renderer, const char * version) {
    scene_.clear();
    if (loaded_) {
        draw(scene_, version);
    }
    backdrop_.time = clock_;
    renderer.begin();
    renderer.backdrop(backdrop_);
    renderer.draw(scene_);
}

void Dashboard::draw(DrawList & list, const char * version) const {
    // The parts arrive one after the other, each rising a little.
    int         index = 0;
    const auto  begin = [&] {
        const float in = hui::tween::stagger(age_, index++, 0.07f, 0.5f);
        list.push_opacity(in);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, 24.0f * (1.0f - in));
    };
    const auto end = [&] {
        list.pop_transform();
        list.pop_opacity();
    };
    begin();
    draw_header(list, version);
    end();
    begin();
    draw_server(list, kServer);
    end();
    begin();
    draw_activity(list, kActivity);
    end();
    begin();
    draw_image(list, kImage);
    end();
    begin();
    draw_recent(list, kRecent);
    end();
    begin();
    draw_memory(list, kMemory);
    draw_log_tile(list, kLogTile);
    end();
}

void Dashboard::draw_header(DrawList & list, const char * version) const {
    const float w = hui::ui::text(list, fonts_.display, "PS5SD", 96, 112, 58, kInk);
    char sub[160];
    std::snprintf(sub, sizeof(sub), "v%s  %s  stable-diffusion.cpp on the console's GPU", version, kDot);
    hui::ui::text(list, fonts_.regular, sub, 96 + w + 28, 112, 24, with_a(kInk, kMuted));
    hui::ui::Canvas c = canvas(list);
    state_chip_.draw(c);
}

void Dashboard::draw_server(DrawList & list, const Rect & r) const {
    draw_panel(list, r, kRadius, kCyan, 0.0f);
    caps(list, "Open in a browser", r.x + kPad, r.y + 42, 17, kCyan);

    // http://<address>:<port>, the port in the accent: it is the part to read out.
    float x = r.x + kPad;
    x += hui::ui::text(list, fonts_.display, "http://", x, r.y + 106, 58, with_a(kInk, 0.42f));
    if (snap_.address.empty()) {
        x += hui::ui::text(list, fonts_.display, "<console ip>", x, r.y + 106, 58, with_a(kInk, 0.7f));
    } else {
        x += hui::ui::text(list, fonts_.display, snap_.address, x, r.y + 106, 58, kInk);
    }
    x += hui::ui::text(list, fonts_.display, ":", x, r.y + 106, 58, with_a(kInk, 0.42f));
    hui::ui::text(list, fonts_.display, std::to_string(snap_.port), x, r.y + 106, 58, kCyan);

    const std::string hint = "From a phone or a PC on your network. The web UI and the OpenAI and A1111 APIs share it.";
    hui::ui::text(list, fonts_.regular, fonts_.regular.font->fit(hint, 22, r.w - 2 * kPad), r.x + kPad, r.y + 140, 22,
                  with_a(kInk, kMuted));
}

void Dashboard::draw_stat(DrawList & list, const char * name, const std::string & value, float x,
                          float baseline) const {
    const float w = caps(list, name, x, baseline, 16, with_a(kInk, 0.5f));
    hui::ui::text(list, fonts_.mono, value, x + w + 14, baseline, 28, kInk);
}

void Dashboard::draw_activity(DrawList & list, const Rect & r) const {
    const AppStatus::Snapshot & s = snap_;
    const Color accent            = accent_.value();
    const bool  generating        = s.state == State::generating;
    const bool  busy              = generating || s.state == State::loading || s.state == State::starting;
    draw_panel(list, r, kRadius, accent, generating ? 1.0f : (s.state == State::error ? 0.6f : 0.25f));
    caps(list, "Status", r.x + kPad, r.y + 42, 17, accent);

    // The mark, then the state in words.
    const Rect mark{ r.x + kPad, r.y + 66, 52, 52 };
    if (busy) {
        hui::ui::Spinner spinner = spinner_;
        spinner.style.color      = with_a(accent, 1.0f);
        spinner.set_bounds(mark);
        hui::ui::Canvas c = canvas(list);
        spinner.draw(c);
    } else {
        list.glow({ mark.cx() - 13, mark.cy() - 13, 26, 26 }, 13, 14, with_a(accent, 0.35f));
        list.circle(mark.cx(), mark.cy(), 13, accent);
    }
    const char * title = "Starting up";
    switch (s.state) {
        case State::loading:    title = "Loading the model"; break;
        case State::ready:      title = "Ready for prompts"; break;
        case State::generating: title = "Generating"; break;
        case State::error:      title = s.phase.empty() ? "Something went wrong" : s.phase.c_str(); break;
        case State::stopped:    title = "Server stopped"; break;
        default:                break;
    }
    const float tx = r.x + kPad + 76;
    hui::ui::text(list, fonts_.semibold, fonts_.semibold.font->fit(title, 46, 560), tx, r.y + 108, 46, kInk);

    // One line under it.
    std::string line;
    if (s.state == State::ready) {
        line = s.images > 0 ? "Last image saved as " + AppStatus::display_name(s.result) + ".png"
                            : "Waiting for a prompt from the web UI or an API client";
    } else if (s.state == State::loading || s.state == State::starting) {
        line = s.detail.empty() ? "This takes a moment" : s.detail;
    }
    if (!line.empty()) {
        hui::ui::text(list, fonts_.regular, fonts_.regular.font->fit(line, 24, r.w - 2 * kPad - 76), tx, r.y + 146, 24,
                      with_a(kInk, kMuted));
    }

    // The model, at the right.
    const float rx = r.x + r.w - kPad;
    caps(list, "Model", rx, r.y + 42, 17, with_a(kInk, 0.5f), Align::right);
    const std::string model = s.model.empty() ? "not known yet" : s.model;
    hui::ui::text(list, fonts_.semibold, fonts_.semibold.font->fit(model, 30, 430), rx, r.y + 84, 30,
                  s.model.empty() ? with_a(kInk, kFaint) : kInk, Align::right);

    // The prompt of this job, the last one, or what went wrong.
    std::string label, body;
    if (generating && !s.prompt.empty()) {
        label = "Prompt";
        body  = s.prompt;
    } else if (s.state == State::error && !s.detail.empty()) {
        label = "What happened";
        body  = s.detail;
    } else if ((s.state == State::ready || s.state == State::stopped) && !s.prompt.empty() && s.images > 0) {
        label = "Last prompt";
        body  = s.prompt;
    }
    if (!body.empty()) {
        caps(list, label, r.x + kPad, r.y + 184, 15, with_a(kInk, 0.5f));
        hui::ui::paragraph(list, fonts_.regular, body, r.x + kPad, r.y + 222, 30, r.w - 2 * kPad, 38, kInk, 2);
    }

    if (progress_shown_) {
        hui::ui::Canvas c = canvas(list);
        progress_.draw(c);
    }

    // The figures along the bottom.
    const float base = r.y + r.h - 32;
    const float x    = r.x + kPad;
    if (generating && s.steps > 0 && s.step > 0) {
        draw_stat(list, "Speed", [&] { char t[32]; std::snprintf(t, sizeof(t), "%.2f s/it", s.step_s); return std::string(t); }(), x, base);
        draw_stat(list, "Elapsed", seconds_text(s.job_s), x + 360, base);
        const double left = s.step_s > 0 ? (double) (s.steps - s.step) * s.step_s : -1;
        draw_stat(list, "Left", left >= 0 ? "~" + seconds_text(left) : "-", x + 720, base);
    } else if (generating) {
        draw_stat(list, "Elapsed", seconds_text(s.job_s), x, base);
    } else if (s.state == State::ready || s.state == State::stopped) {
        float at = x;
        if (s.last_job_s >= 0) {
            draw_stat(list, "Last image took", seconds_text(s.last_job_s), at, base);
            at += 560;
        }
        draw_stat(list, "Images", std::to_string(s.images), at, base);
    }
}

void Dashboard::draw_image(DrawList & list, const Rect & r) const {
    const AppStatus::Snapshot & s = snap_;
    const bool  generating        = s.state == State::generating;
    const bool  live              = generating && preview_.id != 0;
    const Picture * newest        = !pictures_.empty() && pictures_[0].large.id != 0 ? &pictures_[0] : nullptr;
    draw_panel(list, r, kRadius, kViolet, live ? 0.6f : 0.0f);

    std::string name = "Image";
    if (live) {
        name = "Live preview";
    } else if (newest != nullptr && !generating) {
        name = "Last image";
    }
    caps(list, name, r.x + 28, r.y + 40, 17, kViolet);
    const Texture * tex = live ? &preview_ : (newest != nullptr ? &newest->large : nullptr);
    if (tex != nullptr) {
        char size[32];
        std::snprintf(size, sizeof(size), "%d x %d", tex->width, tex->height);
        hui::ui::text(list, fonts_.mono, size, r.x + r.w - 28, r.y + 40, 20, with_a(kInk, kMuted), Align::right);
    }

    const Rect area{ r.x + 20, r.y + 58, r.w - 40, r.w - 40 };
    list.rounded_rect(area, 18, with_a(kWell, 0.85f));
    if (tex != nullptr) {
        const float fade = live ? 1.0f : reveal_.value;
        // While a job without a preview runs, the old picture stays but steps back.
        const float dim = generating && !live ? 0.14f : 1.0f;
        list.image(tex->id, contain(area, tex->width, tex->height), hui::gfx::kFullUv,
                   Color{ 1, 1, 1, fade * dim }, 14);
    }
    list.bordered_rect(area, 18, kClear, 1.5f, with_a(kInk, 0.08f));
    if (live) {
        // A bar along the picture's foot: how far the sampling is.
        if (s.steps > 0) {
            const float frac = std::clamp((float) s.step / (float) s.steps, 0.0f, 1.0f);
            const Rect  track{ area.x + 18, area.y + area.h - 26, area.w - 36, 8 };
            list.rounded_rect(track, 4, with_a(kBlack, 0.45f));
            list.rounded_rect({ track.x, track.y, std::max(track.h, track.w * frac), track.h }, 4, kCyan);
        }
        // LIVE, with a dot that breathes.
        const Rect pill{ area.x + 18, area.y + 18, 86, 34 };
        list.rounded_rect(pill, 17, with_a(kBlack, 0.55f));
        const float breath = hui::ui::breathe(clock_);
        list.circle(pill.x + 20, pill.cy(), 6 + 1.5f * breath, kCyan);
        hui::ui::text(list, fonts_.semibold, "LIVE", pill.x + 36, pill.cy() + 6, 17, kInk, Align::left, 2.0f);
    }
    if (tex == nullptr || (generating && !live)) {
        hui::ui::Canvas c = canvas(list);
        empty_.draw(c);
    }
}

void Dashboard::draw_recent(DrawList & list, const Rect & r) const {
    const AppStatus::Snapshot & s = snap_;
    draw_panel(list, r, kRadius, kLime, 0.0f);
    caps(list, log_open_ ? "Log" : "Recent images", r.x + kPad, r.y + 40, 17, log_open_ ? kSteel : kLime);

    if (log_open_) {
        const std::size_t shown = std::min<std::size_t>(s.lines.size(), 7);
        float baseline          = r.y + 78;
        for (std::size_t i = s.lines.size() - shown; i < s.lines.size(); i++) {
            const std::string text = log_text(s.lines[i]);
            hui::ui::text(list, fonts_.mono, fonts_.mono.font->fit(text, 18, r.w - 2 * kPad), r.x + kPad, baseline, 18,
                          with_a(kInk, 0.82f));
            baseline += 24;
        }
        return;
    }

    char count[48];
    if (s.images > 0) {
        std::snprintf(count, sizeof(count), "%d this session", s.images);
    } else {
        std::snprintf(count, sizeof(count), "none yet");
    }
    hui::ui::text(list, fonts_.mono, count, r.x + r.w - kPad, r.y + 40, 20, with_a(kInk, kMuted), Align::right);

    const float size = 160, gap = (r.w - 2 * kPad - kThumbs * size) / (kThumbs - 1);
    if (pictures_.empty()) {
        const Rect well{ r.x + kPad, r.y + 58, r.w - 2 * kPad, size };
        list.rounded_rect(well, 16, with_a(kWell, 0.45f));
        list.bordered_rect(well, 16, kClear, 1.5f, with_a(kInk, 0.06f));
        hui::ui::text(list, fonts_.regular, "Finished images collect here", well.cx(), well.cy() + 8, 24,
                      with_a(kInk, kFaint), Align::center);
        return;
    }
    for (int i = 0; i < kThumbs; i++) {
        const Rect slot{ r.x + kPad + (size + gap) * (float) i, r.y + 58, size, size };
        const bool have = (std::size_t) i < pictures_.size();
        list.rounded_rect(slot, 16, with_a(kWell, have ? 0.85f : 0.45f));
        if (have && pictures_[(std::size_t) i].thumb.id != 0) {
            const Texture & t = pictures_[(std::size_t) i].thumb;
            list.image(t.id, contain(slot, t.width, t.height), hui::gfx::kFullUv, Color{ 1, 1, 1, 1 }, 14);
        } else if (have && pictures_[(std::size_t) i].failed) {
            hui::ui::text(list, fonts_.semibold, "?", slot.cx(), slot.cy() + 12, 36, with_a(kInk, kFaint), Align::center);
        }
        list.bordered_rect(slot, 16, kClear, i == 0 && have ? 2.5f : 1.5f,
                           i == 0 && have ? with_a(kLime, 0.8f) : with_a(kInk, have ? 0.1f : 0.06f));
    }
}

void Dashboard::draw_memory(DrawList & list, const Rect & r) const {
    const AppStatus::Snapshot & s = snap_;
    draw_panel(list, r, 24, kRose, 0.0f);
    caps(list, "Memory", r.x + 24, r.y + 36, 17, kRose);
    char text[64];
    if (s.free_gib >= 0 && s.pool_gib > 0) {
        std::snprintf(text, sizeof(text), "%.1f of %.1f GiB free", s.free_gib, s.pool_gib);
    } else {
        std::snprintf(text, sizeof(text), "not read yet");
    }
    hui::ui::text(list, fonts_.mono, text, r.x + r.w - 24, r.y + 36, 20, kInk, Align::right);
    hui::ui::Canvas c = canvas(list);
    memory_.draw(c);
}

void Dashboard::draw_log_tile(DrawList & list, const Rect & r) const {
    const AppStatus::Snapshot & s = snap_;
    draw_panel(list, r, 24, kSteel, 0.0f);
    caps(list, "Log", r.x + 24, r.y + 36, 17, kSteel);
    const std::size_t shown = std::min<std::size_t>(s.lines.size(), 3);
    float             y     = r.y + 62;
    for (std::size_t i = s.lines.size() - shown; i < s.lines.size(); i++) {
        const std::string text = log_text(s.lines[i]);
        hui::ui::text(list, fonts_.mono, fonts_.mono.font->fit(text, 17, r.w - 48), r.x + 24, y, 17,
                      with_a(kInk, 0.72f));
        y += 24;
    }
    if (s.lines.empty()) {
        hui::ui::text(list, fonts_.regular, "nothing logged yet", r.x + 24, y, 18, with_a(kInk, kFaint));
    }
}

}  // namespace ps5sd
