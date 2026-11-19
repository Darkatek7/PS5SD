// PS5SD's screen: a dashboard built from ps5-homebrew-ui's components, in the
// design language of the kit's "Pulse" design (night-blue tiles on a dot
// matrix, one accent per tile, tracked capitals for labels, mono numerals).
//
// What it shows, read from a snapshot of AppStatus (app_status.hpp):
//   - the app, its state and the address to type into a browser;
//   - the running job: prompt, progress, speed, time left, the live preview;
//   - the last finished image, and a strip of the recent ones;
//   - the direct memory pool and the tail of the log;
//   - on a button of the controller (Triangle), the gallery in full screen, a slideshow
//     (slideshow.hpp has the logic; the hint is in the recent images' tile).
//
// One frame is update(snapshot, dt), then compose(renderer, version), then the
// platform presents the renderer. main.cpp and scripts/preview-app-ui.sh make
// the same calls, so the PC preview proves the wiring.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app_status.hpp"
#include "slideshow.hpp"
#include "core/tween.hpp"
#include "gfx/backdrop_spec.hpp"
#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"
#include "ui/components/badge.hpp"
#include "ui/components/progress.hpp"
#include "ui/components/stat.hpp"
#include "ui/fonts.hpp"
#include "ui/motion.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hui::gfx {
class Renderer;
}

namespace ps5sd {

class Dashboard {
  public:
    Dashboard();
    Dashboard(const Dashboard &)             = delete;
    Dashboard & operator=(const Dashboard &) = delete;
    ~Dashboard();

    // Loads the fonts. font_path is the title's DejaVu Sans Mono (the one
    // scripts/build-app.sh has always copied); Inter and Montserrat are read
    // from the same folder. Needs a current GL context.
    bool load(hui::gfx::Renderer & renderer, const std::string & font_path);

    // Takes the new state and advances the animations by dt seconds. Decodes
    // at most one finished image and uploads the textures it needs (GL).
    void update(const AppStatus::Snapshot & s, float dt);

    // Queues the frame on the renderer: the backdrop, then the tiles.
    void compose(hui::gfx::Renderer & renderer, const char * version);

    // Shows the log in place of the recent images, or back (for a button once
    // the main loop reads the controller).
    void toggle_log() { log_open_ = !log_open_; }
    bool log_open() const { return log_open_; }

    // The controller (pad_input.cpp reads it, the preview makes it up): the gallery's folder, whether
    // a pad is open, and one sample of it, as many a frame as were read (a tap is kept). Triangle
    // opens the slideshow, then left and right, Options and Circle (slideshow.hpp).
    void set_gallery_dir(const std::string & dir) { scan_.set_dir(dir); }
    void set_pad_open(bool open) { pad_open_ = open; }
    void pad_sample(uint32_t buttons, uint8_t left_x, bool connected) {
        pad_connected_ = connected;
        input_.sample(buttons, left_x, connected);
    }
    bool slideshow_open() const { return show_.is_open(); }
    std::size_t slideshow_index() const { return show_.index(); }  // 0 is the newest
    std::size_t slideshow_size() const { return show_.size(); }

    // Skips the entrance animation and the springs' travel (the PC preview
    // calls it before a picture; the console never does).
    void settle();

  private:
    // A GL texture and what it holds.
    struct Texture {
        uint32_t id     = 0;
        int      width  = 0;
        int      height = 0;
    };
    // A finished image the dashboard has met: its thumbnail, and the large
    // picture while it is the newest.
    struct Picture {
        std::string path;
        std::string prompt;
        Texture     thumb;
        Texture     large;
        bool        failed = false;
    };

    // A picture of the slideshow, decoded.
    struct SlideTex {
        std::string path;
        Texture     tex;
        bool        failed = false;  // not readable, or too large
    };

    bool load_font(const std::string & path, hui::gfx::Font & font, hui::ui::FontRef & ref);
    void upload(Texture & texture, int width, int height, const uint8_t * rgba);
    void release(Texture & texture);
    void sync_pictures(const AppStatus::Snapshot & s);
    void sync_preview(const AppStatus::Snapshot & s);
    void sync_components(float dt);
    void update_slideshow(const AppStatus::Snapshot & s, float dt);
    void sync_slides();
    const SlideTex * slide_tex(const std::string & path) const;

    // Drawing, all const: the frame is a pure function of the state.
    void draw(hui::gfx::DrawList & list, const char * version) const;
    void draw_header(hui::gfx::DrawList & list, const char * version) const;
    void draw_server(hui::gfx::DrawList & list, const hui::gfx::Rect & r) const;
    void sync_qr();
    // The QR code of the web UI's address, its right edge at right, centred on cy; its width.
    float draw_qr(hui::gfx::DrawList & list, float right, float cy) const;
    void draw_activity(hui::gfx::DrawList & list, const hui::gfx::Rect & r) const;
    void draw_image(hui::gfx::DrawList & list, const hui::gfx::Rect & r) const;
    void draw_recent(hui::gfx::DrawList & list, const hui::gfx::Rect & r) const;
    void draw_slideshow(hui::gfx::DrawList & list) const;
    void draw_slide_hint(hui::gfx::DrawList & list, const hui::gfx::Rect & r, float right) const;
    void draw_log_tile(hui::gfx::DrawList & list, const hui::gfx::Rect & r) const;
    void draw_memory(hui::gfx::DrawList & list, const hui::gfx::Rect & r) const;
    void draw_stat(hui::gfx::DrawList & list, const char * name, const std::string & value, float x,
                   float baseline) const;
    void draw_panel(hui::gfx::DrawList & list, const hui::gfx::Rect & r, float radius, hui::gfx::Color accent,
                    float lit) const;
    float caps(hui::gfx::DrawList & list, const std::string & text, float x, float baseline, float size,
               hui::gfx::Color color, hui::gfx::Align align = hui::gfx::Align::left) const;
    hui::ui::Canvas canvas(hui::gfx::DrawList & list) const;
    hui::gfx::Color state_color(AppStatus::State state) const;

    hui::gfx::Renderer * renderer_ = nullptr;
    hui::gfx::Font       regular_font_, semibold_font_, display_font_, mono_font_;
    hui::ui::Fonts       fonts_;
    hui::ui::Theme       theme_;
    hui::gfx::BackdropSpec backdrop_;
    hui::gfx::DrawList     scene_;  // the renderer keeps a pointer to it until the frame is presented

    AppStatus::Snapshot snap_;
    float               clock_ = 0;  // seconds since the first frame
    float               age_   = 0;  // the same, for the entrance
    bool                log_open_ = false;
    bool                loaded_   = false;

    // The state's colour, eased, so a change of state recolours the screen.
    hui::ui::SpringColor accent_;
    bool                 accent_set_ = false;
    hui::tween::Spring   reveal_;  // the large picture's fade in

    // Components (ps5-homebrew-ui's own): they keep their springs here.
    hui::ui::Chip        state_chip_;
    hui::ui::ProgressBar progress_;
    hui::ui::ProgressBar memory_;
    hui::ui::Spinner     spinner_;
    hui::ui::EmptyState  empty_;
    bool                 progress_shown_ = false;

    Texture                preview_;
    uint64_t               preview_serial_ = 0;
    std::vector<uint8_t>   scratch_;  // RGBA of a preview frame
    std::vector<Picture>   pictures_;  // newest first, the snapshot's recent list
    std::string            shown_key_;  // what the large image area shows, to fade on a change

    // The slideshow (slideshow.hpp): the gallery's list, the controller, and the pictures decoded for
    // it, which are the current one and its two neighbours at most.
    GalleryScan            scan_;
    Slideshow              show_;
    SlideInput             input_;
    bool                   pad_open_      = false;
    bool                   pad_connected_ = false;
    float                  scan_age_      = 0;   // seconds since the list was read
    int                    scan_images_   = 0;   // snapshot.images then: a finished job reads it again
    std::vector<SlideTex>  slide_tex_;
    std::string            shown_slide_;         // the picture on the screen (it waits for the next one's decode)
    float                  hud_           = 0;   // seconds the hints stay after the last press
    hui::tween::Spring     show_vis_;            // the slideshow's fade in and out
    hui::tween::Spring     slide_fade_;          // a picture's fade in

    // The address as a QR code (qrcodegen), made again only when the text changes.
    std::string            qr_text_;
    int                    qr_size_ = 0;  // modules a side, 0 for none
    std::vector<uint8_t>   qr_;           // qr_size_ * qr_size_, 1 for dark
};

}  // namespace ps5sd
