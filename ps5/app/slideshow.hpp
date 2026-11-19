// The gallery as a slideshow on the TV: what to show, in which order, and what the
// controller asks for. No GL, no kit and no console code in here (header-only), so
// ps5/app/tests builds it on the PC; the dashboard draws it (dashboard.cpp) and
// pad_input.cpp reads the DualSense.
//
//   - GalleryScan lists the gallery's pictures, newest first, as the web UI's gallery
//     does (PNG, not the "tmp_" ones that are not kept, no videos), and looks at a file's
//     date once.
//   - SlideInput folds the controller's samples of a frame into steps (left and right
//     on the D-pad or the left stick, with a dead zone and a repeat), Options, Circle
//     and Triangle. A tap between two frames is not lost.
//   - Slideshow holds the list, the current picture, the automatic advance, and the
//     window of pictures that are decoded (the current one and its two neighbours).
//
// Right is the next picture in the list, which is an older one; left is a newer one.
// Both wrap around.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace ps5sd {

// ---- the list ---------------------------------------------------------------------

struct Slide {
    std::string path;
    std::string name;   // the file name
    int64_t     mtime = 0;
};

// Whether a file of the outputs folder is a picture of the slideshow: a PNG that is kept.
// A video has no picture the TV can show, a "tmp_" image is not in the gallery, and
// ".thumbs" and ".labels.json" are the app's own files.
inline bool is_slide_name(const std::string & name) {
    if (name.size() < 5 || name[0] == '.' || name.compare(0, 4, "tmp_") == 0) {
        return false;
    }
    std::string ext = name.substr(name.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return ext == ".png";
}

// Newest first; the names carry a date and a counter, so they break ties (as the server's list does).
inline void sort_newest_first(std::vector<Slide> & slides) {
    std::sort(slides.begin(), slides.end(), [](const Slide & a, const Slide & b) {
        return a.mtime != b.mtime ? a.mtime > b.mtime : a.name > b.name;
    });
}

// The gallery folder, read again and again while the slideshow is open. A folder of thousands
// of files costs one readdir; the date of a file is asked for once (a file of the gallery is
// not changed once it is written), and a file that is gone drops out of the cache.
class GalleryScan {
  public:
    explicit GalleryScan(std::string dir = {}) : dir_(std::move(dir)) {}
    void set_dir(std::string dir) {
        if (dir != dir_) {
            dir_ = std::move(dir);
            mtimes_.clear();
        }
    }
    const std::string & dir() const { return dir_; }

    std::vector<Slide> scan() {
        std::vector<Slide>             slides;
        std::map<std::string, int64_t> seen;
        if (DIR * d = dir_.empty() ? nullptr : opendir(dir_.c_str())) {
            while (const dirent * e = readdir(d)) {
                const std::string name = e->d_name;
                if (!is_slide_name(name)) {
                    continue;
                }
                Slide s;
                s.name = name;
                s.path = dir_ + "/" + name;
                auto it = mtimes_.find(name);
                if (it != mtimes_.end()) {
                    s.mtime = it->second;
                } else {
                    struct stat st {};
                    if (stat(s.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
                        continue;
                    }
                    s.mtime = (int64_t) st.st_mtime;
                }
                seen[name] = s.mtime;
                slides.push_back(std::move(s));
            }
            closedir(d);
        }
        mtimes_.swap(seen);
        sort_newest_first(slides);
        return slides;
    }

  private:
    std::string                    dir_;
    std::map<std::string, int64_t> mtimes_;
};

// ---- the controller -----------------------------------------------------------------

// The scePad layout (ps5-homebrew-ui's core/input.hpp has the same bits, pad_input.cpp checks it).
namespace slide_pad {
constexpr uint32_t kOptions     = 0x00000008;
constexpr uint32_t kRight       = 0x00000020;
constexpr uint32_t kLeft        = 0x00000080;
constexpr uint32_t kTriangle    = 0x00001000;
constexpr uint32_t kCircle      = 0x00002000;
constexpr uint32_t kIntercepted = 0x80000000;  // the system has the input (its menu is open)
}  // namespace slide_pad

class SlideInput {
  public:
    static constexpr float kDelay    = 0.45f;  // seconds a direction is held before it repeats
    static constexpr float kInterval = 0.22f;  // then a step this often (a picture takes a moment to decode)
    static constexpr float kStickOn  = 0.55f;  // the left stick is "left" or "right" beyond this (0..1)
    static constexpr float kStickOff = 0.35f;  // and stays so until it is back inside this

    // What happened since the last frame().
    struct Frame {
        int  steps       = 0;      // pictures to move: right is positive; a tap counts, a held direction repeats
        bool toggle_auto = false;  // Options went down
        bool back        = false;  // Circle went down
        bool open        = false;  // Triangle went down
    };

    // One sample of the pad (several a frame; all of them, so a tap is kept). `usable` is false
    // when the pad is not connected or the system took the input.
    void sample(uint32_t buttons, uint8_t left_x, bool usable) {
        if (usable && (buttons & slide_pad::kIntercepted) != 0) {
            // The system has the input (its menu). The pad keeps this bit on its last sample until the
            // next button changes, so the first sample without it is often a new press (Triangle after
            // the PS menu on the console, 2026-11-19): only what was down under the menu stays down.
            prev_        = buttons & ~slide_pad::kIntercepted;
            dir_         = 0;
            stick_       = 0;
            usable_      = false;
            from_system_ = true;
            return;
        }
        if (!usable) {
            // Everything let go, without edges: a button still down when the pad comes back is not a press.
            prev_        = 0;
            dir_         = 0;
            stick_       = 0;
            usable_      = false;
            from_system_ = false;
            return;
        }
        // A pad that has just come back: what is down now counts as down already; after the system's
        // menu, what was down under it.
        const uint32_t before = usable_ || from_system_ ? prev_ : buttons;
        from_system_          = false;
        const uint32_t down   = buttons & ~before;
        usable_               = true;
        prev_                 = buttons;

        const float x = ((float) left_x - 128.0f) / 127.0f;
        const float ax = std::fabs(x);
        if (stick_ == 0) {
            stick_ = ax >= kStickOn ? (x > 0 ? 1 : -1) : 0;
        } else if (ax < kStickOff) {
            stick_ = 0;
        } else if (ax >= kStickOn && (x > 0) != (stick_ > 0)) {
            stick_ = x > 0 ? 1 : -1;  // straight across to the other side
        }
        int dir = 0;
        if ((buttons & slide_pad::kRight) != 0) {
            dir += 1;
        }
        if ((buttons & slide_pad::kLeft) != 0) {
            dir -= 1;
        }
        if (dir == 0) {
            dir = stick_;
        }
        if (dir != 0 && dir != dir_) {
            pending_ += dir;  // went down (or changed side): a step now, then the repeat
            pending_ = std::clamp(pending_, -2, 2);
            wait_    = kDelay;
        }
        dir_ = dir;
        if ((down & slide_pad::kOptions) != 0) {
            toggle_ = true;
        }
        if ((down & slide_pad::kCircle) != 0) {
            back_ = true;
        }
        if ((down & slide_pad::kTriangle) != 0) {
            open_ = true;
        }
    }

    // Ends the frame: the steps and presses of its samples, and the repeat of a direction held on.
    Frame frame(float dt) {
        Frame f;
        f.steps       = pending_;
        f.toggle_auto = toggle_;
        f.back        = back_;
        f.open        = open_;
        if (dir_ != 0 && pending_ == 0) {
            wait_ -= dt;
            if (wait_ <= 0) {
                f.steps = dir_;  // at most one a frame
                wait_ += kInterval;
                if (wait_ < 0) {
                    wait_ = kInterval;
                }
            }
        }
        pending_ = 0;
        toggle_ = back_ = open_ = false;
        return f;
    }

  private:
    uint32_t prev_    = 0;
    bool     usable_  = true;   // the last sample was usable (the first one counts as it is)
    bool     from_system_ = false;  // not usable because the system had the input; prev_ is what was down
    int      dir_     = 0;   // held: -1 left, +1 right
    int      stick_   = 0;   // the stick's own, with its hysteresis
    int      pending_ = 0;   // taps since the last frame
    float    wait_    = 0;
    bool     toggle_ = false, back_ = false, open_ = false;
};

// ---- the show ------------------------------------------------------------------------

class Slideshow {
  public:
    static constexpr float kInterval = 5.0f;  // seconds a picture is shown while it runs by itself

    bool is_open() const { return open_; }
    // Opens on the newest picture, not running by itself.
    void open(std::vector<Slide> slides) {
        open_         = true;
        auto_         = false;
        slides_       = std::move(slides);
        index_        = 0;
        direction_    = 1;
        timer_        = 0;
        changed_      = true;
    }
    void close() {
        open_ = false;
        slides_.clear();
        index_ = 0;
        auto_  = false;
    }

    std::size_t size() const { return slides_.size(); }
    std::size_t index() const { return index_; }
    bool        empty() const { return slides_.empty(); }
    const Slide * current() const { return slides_.empty() ? nullptr : &slides_[index_]; }
    bool        auto_on() const { return auto_; }
    void        set_auto(bool on) {
        auto_  = on;
        timer_ = 0;
    }
    void toggle_auto() { set_auto(!auto_); }
    // The way of the last step: 1 to the next picture (an older one), -1 to a newer one.
    int direction() const { return direction_; }
    // The seconds the current picture has been there, while it counts (see tick()).
    float shown_for() const { return timer_; }
    // True once after the current picture changed (a step, or a new picture that joined).
    bool take_changed() {
        const bool c = changed_;
        changed_     = false;
        return c;
    }

    // Moves n pictures (right is positive), around the ends.
    void step(int n) {
        const int count = (int) slides_.size();
        if (count == 0 || n == 0) {
            return;
        }
        direction_ = n > 0 ? 1 : -1;
        if (count == 1) {
            return;
        }
        index_   = (std::size_t) ((((int) index_ + n) % count + count) % count);
        timer_   = 0;
        changed_ = true;
    }

    // The automatic advance: counts while the current picture is on the screen (`shown`), and
    // moves to the next one when the time is up.
    void tick(float dt, bool shown) {
        if (!open_ || !auto_ || !shown) {
            return;
        }
        timer_ += dt;
        if (timer_ >= kInterval) {
            if (slides_.size() > 1) {
                step(1);
            } else {
                timer_ = 0;
            }
        }
    }

    // The gallery as it is now (newest first). The picture on the screen stays where it is, whatever
    // joined or went; but a viewer on the newest picture moves to a newer one that joined, so a
    // picture that has just been made shows up. A picture that went leaves its place to its neighbour.
    void update_list(std::vector<Slide> slides) {
        const bool at_newest = !slides_.empty() && index_ == 0;
        const std::string was = slides_.empty() ? std::string() : slides_[index_].path;
        const std::size_t old_index = index_;
        slides_               = std::move(slides);
        if (slides_.empty()) {
            index_ = 0;
            changed_ = true;
            return;
        }
        auto it = std::find_if(slides_.begin(), slides_.end(), [&](const Slide & s) { return s.path == was; });
        if (it != slides_.end()) {
            index_ = (std::size_t) (it - slides_.begin());
            if (at_newest && index_ != 0) {
                index_   = 0;  // something newer joined while the newest was shown
                timer_   = 0;
                changed_ = true;
            }
        } else {
            index_   = std::min(old_index, slides_.size() - 1);
            timer_   = 0;
            changed_ = true;
        }
    }

    // The files that are kept decoded, the most wanted first: the current picture, then the one
    // the viewer is heading for, then the other neighbour. Three at most, each once.
    std::vector<std::string> window() const {
        std::vector<std::string> w;
        const int count = (int) slides_.size();
        if (count == 0) {
            return w;
        }
        const auto at = [&](int offset) { return slides_[(std::size_t) ((((int) index_ + offset) % count) + count) % count].path; };
        w.push_back(at(0));
        for (const int offset : { direction_, -direction_ }) {
            const std::string p = at(offset);
            if (std::find(w.begin(), w.end(), p) == w.end()) {
                w.push_back(p);
            }
        }
        return w;
    }

  private:
    bool               open_ = false;
    bool               auto_ = false;
    std::vector<Slide> slides_;
    std::size_t        index_     = 0;
    int                direction_ = 1;
    float              timer_     = 0;
    bool               changed_   = false;
};

}  // namespace ps5sd
