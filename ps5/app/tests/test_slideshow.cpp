// Host test for slideshow.hpp: the gallery's list, the controller's steps (D-pad, stick,
// repeat, taps), the wrap, the automatic advance, new pictures joining, and the window of
// pictures that are kept decoded.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "slideshow.hpp"

#include <sys/time.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ps5sd;
namespace fs = std::filesystem;
namespace pad = ps5sd::slide_pad;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void touch(const fs::path & p, long mtime) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << "x";
    struct timeval t[2] = { { (time_t) mtime, 0 }, { (time_t) mtime, 0 } };
    utimes(p.c_str(), t);
}

std::vector<Slide> make(int n) {  // newest first: p0, p1, ...
    std::vector<Slide> v;
    for (int i = 0; i < n; i++) {
        v.push_back({ "/o/p" + std::to_string(i) + ".png", "p" + std::to_string(i) + ".png", 1000 - i });
    }
    return v;
}

std::string cur(const Slideshow & s) {
    return s.current() != nullptr ? s.current()->name : "-";
}

// ---- the list ---------------------------------------------------------------------

void test_names() {
    check(is_slide_name("sd_20261119_101500_000.png"), "a PNG is a slide");
    check(is_slide_name("A.PNG"), "the extension is not case-sensitive");
    check(!is_slide_name("tmp_sd_1.png"), "a temporary image is not in the gallery");
    check(!is_slide_name("sd_1.webm"), "a video is skipped");
    check(!is_slide_name("sd_1.webp"), "webp is skipped (no decoder)");
    check(!is_slide_name(".labels.json"), "the labels are not a slide");
    check(!is_slide_name(".hidden.png"), "a hidden file is not a slide");
    check(!is_slide_name("png"), "a name without an extension is not a slide");
}

void test_scan(const fs::path & dir) {
    touch(dir / "a.png", 100);
    touch(dir / "b.png", 300);
    touch(dir / "c.png", 200);
    touch(dir / "d.png", 300);  // same time as b: the name breaks the tie
    touch(dir / "tmp_e.png", 400);
    touch(dir / "f.webm", 500);
    touch(dir / ".thumbs" / "a.png.jpg", 500);
    fs::create_directories(dir / "dir.png");  // a folder named like a PNG
    GalleryScan scan(dir.string());
    std::vector<Slide> s = scan.scan();
    check(s.size() == 4, "four slides, got " + std::to_string(s.size()));
    check(s.size() == 4 && s[0].name == "d.png" && s[1].name == "b.png" && s[2].name == "c.png" && s[3].name == "a.png",
          "newest first, the name breaks a tie");
    check(!s.empty() && s[0].path == dir.string() + "/d.png", "the path is the folder and the name");

    touch(dir / "g.png", 600);  // a new one joins
    fs::remove(dir / "a.png");  // one goes
    s = scan.scan();
    check(s.size() == 4 && s[0].name == "g.png" && s[3].name == "c.png", "a new file joins, a deleted one goes");
    check(GalleryScan("").scan().empty(), "no folder, no slides");
    check(GalleryScan((dir / "missing").string()).scan().empty(), "a missing folder gives no slides");
}

// ---- the controller ------------------------------------------------------------------

// A frame with the given held buttons and stick, as many samples as asked.
SlideInput::Frame frame_of(SlideInput & in, uint32_t buttons, uint8_t x = 128, float dt = 1.0f / 60.0f) {
    in.sample(buttons, x, true);
    return in.frame(dt);
}

void test_steps() {
    SlideInput in;
    check(frame_of(in, 0).steps == 0, "nothing held, no step");
    check(frame_of(in, pad::kRight).steps == 1, "right steps once");
    check(frame_of(in, pad::kRight).steps == 0, "held right does not step again at once");
    check(frame_of(in, 0).steps == 0, "released");
    check(frame_of(in, pad::kLeft).steps == -1, "left steps back");
    check(frame_of(in, 0).steps == 0, "released again");

    // a tap that goes down and up between two frames is kept
    SlideInput tap;
    tap.sample(pad::kRight, 128, true);
    tap.sample(0, 128, true);
    check(tap.frame(0.016f).steps == 1, "a tap between two frames is kept");
    check(tap.frame(0.016f).steps == 0, "and counted once");

    // both directions together cancel to the one pressed last? neither: left + right is nothing
    SlideInput both;
    check(frame_of(both, pad::kLeft | pad::kRight).steps == 0, "left and right together do nothing");
}

void test_repeat() {
    SlideInput in;
    int        steps = 0;
    float      at_first = -1, at_second = -1;
    float      t = 0;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; i++, t += dt) {
        const int s = frame_of(in, pad::kRight, 128, dt).steps;
        steps += s;
        if (s != 0 && at_first < 0) {
            at_first = t;
        } else if (s != 0 && at_second < 0) {
            at_second = t;
        }
    }
    check(at_first == 0, "the first step is at the press");
    check(at_second > SlideInput::kDelay - 0.05f && at_second < SlideInput::kDelay + 0.05f,
          "the repeat starts after the delay, at " + std::to_string(at_second));
    // 2 s held: one at once, then every 0.22 s after 0.45 s: about 1 + (2 - 0.45) / 0.22 = 8
    check(steps >= 7 && steps <= 9, "a held direction repeats, steps " + std::to_string(steps));

    // let go and press again: a new delay
    frame_of(in, 0);
    check(frame_of(in, pad::kRight).steps == 1, "a new press steps at once");
    int again = 0;
    for (int i = 0; i < 20; i++) {  // a third of a second: not repeated yet
        again += frame_of(in, pad::kRight, 128, dt).steps;
    }
    check(again == 0, "no repeat inside the delay");

    // a long frame does not step more than once
    SlideInput slow;
    frame_of(slow, pad::kRight);
    int burst = 0;
    burst += frame_of(slow, pad::kRight, 128, 2.0f).steps;
    check(burst == 1, "a long frame steps once");
}

void test_stick() {
    SlideInput in;
    check(frame_of(in, 0, 128 + 40).steps == 0, "a small push is inside the dead zone");
    check(frame_of(in, 0, 128 + 100).steps == 1, "a push to the right steps once");
    check(frame_of(in, 0, 128 + 100).steps == 0, "held, it waits");
    check(frame_of(in, 0, 128 + 60).steps == 0, "back a little, still held (hysteresis)");
    check(frame_of(in, 0, 128).steps == 0, "released");
    check(frame_of(in, 0, 128 - 100).steps == -1, "left steps back");
    check(frame_of(in, 0, 128 + 100).steps == 1, "straight across to the right steps at once");
    frame_of(in, 0, 128);
    // 128 - 127 = 1: full left
    check(frame_of(in, 0, 0).steps == -1, "full left");
    // the stick repeats as the D-pad does
    int steps = 0;
    for (int i = 0; i < 90; i++) {
        steps += frame_of(in, 0, 0).steps;
    }
    check(steps <= -4 && steps >= -6, "the stick repeats too, " + std::to_string(steps));
    // D-pad and stick the same way are one direction
    SlideInput both;
    check(frame_of(both, pad::kRight, 255).steps == 1, "D-pad and stick together step once");
}

void test_buttons() {
    SlideInput in;
    SlideInput::Frame f = frame_of(in, pad::kOptions);
    check(f.toggle_auto && !f.back && !f.open, "Options toggles");
    f = frame_of(in, pad::kOptions);
    check(!f.toggle_auto, "held Options toggles once");
    frame_of(in, 0);
    f = frame_of(in, pad::kCircle);
    check(f.back && !f.toggle_auto, "Circle goes back");
    frame_of(in, 0);
    f = frame_of(in, pad::kTriangle);
    check(f.open && !f.back, "Triangle opens");

    // the system has the input (its menu is open): no presses, and one still down is no press on return
    SlideInput lost;
    lost.sample(pad::kRight | pad::kCircle | pad::kIntercepted, 128, true);
    check(lost.frame(0.016f).steps == 0, "intercepted input is ignored");
    lost.sample(pad::kCircle, 128, true);
    check(!lost.frame(0.016f).back, "a button still down when the menu gives the input back is not a press");
    lost.sample(0, 128, true);
    lost.frame(0.016f);
    lost.sample(pad::kCircle, 128, true);
    check(lost.frame(0.016f).back, "a new press counts");

    // the pad keeps the system's bit until the next change: the first sample after the menu is a press
    SlideInput menu;
    menu.sample(0, 128, true);
    menu.frame(0.016f);
    menu.sample(pad::kIntercepted, 128, true);
    menu.frame(0.016f);
    menu.sample(pad::kTriangle, 128, true);
    check(menu.frame(0.016f).open, "Triangle right after the PS menu opens");

    // a pad that was not connected: what is down when it comes back is not a press
    SlideInput away;
    away.sample(0, 128, false);
    away.frame(0.016f);
    away.sample(pad::kCircle, 128, true);
    check(!away.frame(0.016f).back, "a button down when the pad comes back is not a press");
    SlideInput gone;
    gone.sample(pad::kRight, 128, false);
    check(gone.frame(0.016f).steps == 0, "a pad that is not connected gives nothing");
}

// ---- the show ------------------------------------------------------------------------

void test_step_and_wrap() {
    Slideshow s;
    s.open(make(4));
    check(s.is_open() && cur(s) == "p0.png", "opens on the newest");
    check(!s.auto_on(), "not running by itself at first");
    s.step(1);
    check(cur(s) == "p1.png" && s.direction() == 1, "right is the next, older one");
    s.step(-1);
    s.step(-1);
    check(cur(s) == "p3.png" && s.direction() == -1, "left of the newest wraps to the oldest");
    s.step(1);
    check(cur(s) == "p0.png", "right of the oldest wraps to the newest");
    s.step(2);
    s.step(-3);
    check(cur(s) == "p3.png", "several steps at once, with the wrap");
    s.step(-8);
    check(cur(s) == "p3.png", "a whole number of rounds is where it was");

    Slideshow one;
    one.open(make(1));
    one.step(1);
    check(cur(one) == "p0.png" && one.index() == 0, "one picture: a step stays");
    Slideshow none;
    none.open({});
    none.step(1);
    none.tick(10, true);
    check(none.current() == nullptr && none.empty() && none.window().empty(), "no pictures, nothing to do");
    none.close();
    check(!none.is_open(), "closed");
}

void test_auto() {
    Slideshow s;
    s.open(make(3));
    s.tick(100, true);
    check(cur(s) == "p0.png", "not running: the time does not matter");
    s.toggle_auto();
    check(s.auto_on(), "Options starts it");
    s.tick(Slideshow::kInterval - 0.1f, true);
    check(cur(s) == "p0.png", "not yet");
    s.tick(0.2f, false);
    check(cur(s) == "p0.png", "the time does not count while the picture is not on the screen");
    s.tick(0.2f, true);
    check(cur(s) == "p1.png", "then the next one");
    s.tick(Slideshow::kInterval * 2.5f, true);
    check(cur(s) == "p2.png", "one step a tick");
    s.step(1);
    check(s.shown_for() == 0, "a step starts the time again");
    s.tick(Slideshow::kInterval - 0.5f, true);
    s.step(-1);
    s.tick(Slideshow::kInterval - 0.5f, true);
    check(cur(s) == "p2.png", "a manual step gives a full time again");
    s.toggle_auto();
    s.tick(100, true);
    check(cur(s) == "p2.png" && !s.auto_on(), "Options stops it");

    Slideshow one;
    one.open(make(1));
    one.set_auto(true);
    one.tick(Slideshow::kInterval + 1, true);
    check(cur(one) == "p0.png" && one.shown_for() == 0, "one picture: the timer only starts again");
}

void test_new_pictures() {
    // a viewer on the newest one moves to what joins
    Slideshow s;
    s.open(make(3));
    s.take_changed();
    std::vector<Slide> more = make(3);
    more.insert(more.begin(), { "/o/new.png", "new.png", 2000 });
    s.update_list(more);
    check(cur(s) == "new.png" && s.take_changed(), "the newest picture is followed to the one that joined");

    // a viewer elsewhere stays on their picture
    Slideshow t;
    t.open(make(3));
    t.step(1);
    t.take_changed();
    t.update_list(more);
    check(cur(t) == "p1.png" && t.index() == 2 && !t.take_changed(), "a picture further back stays, its index moves");

    // several join at once: the newest of them
    Slideshow u;
    u.open(make(2));
    std::vector<Slide> batch = make(2);
    batch.insert(batch.begin(), { { "/o/n1.png", "n1.png", 3000 }, { "/o/n2.png", "n2.png", 2999 } });
    u.update_list(batch);
    check(cur(u) == "n1.png", "a batch: the first of the list");

    // the list the same: nothing changes
    Slideshow v;
    v.open(make(3));
    v.step(1);
    v.take_changed();
    v.update_list(make(3));
    check(cur(v) == "p1.png" && !v.take_changed(), "the same list changes nothing");

    // the picture on the screen is deleted: its place goes to a neighbour
    Slideshow w;
    w.open(make(4));
    w.step(2);
    std::vector<Slide> fewer = make(4);
    fewer.erase(fewer.begin() + 2);
    w.update_list(fewer);
    check(cur(w) == "p3.png" && w.take_changed(), "a deleted picture: the next one takes its place");
    fewer.pop_back();
    w.update_list(fewer);  // the last one gone while on it
    check(cur(w) == "p1.png", "the last picture gone: the new last one");
    w.update_list({});
    check(w.empty() && w.current() == nullptr, "everything deleted");
    w.update_list(make(2));
    check(cur(w) == "p0.png", "pictures come back");
}

void test_window() {
    Slideshow s;
    s.open(make(10));
    std::vector<std::string> w = s.window();
    check(w.size() == 3 && w[0] == "/o/p0.png" && w[1] == "/o/p1.png" && w[2] == "/o/p9.png",
          "current, then the next (heading older), then the previous");
    s.step(-1);  // heading to newer ones
    w = s.window();
    check(w.size() == 3 && w[0] == "/o/p9.png" && w[1] == "/o/p8.png" && w[2] == "/o/p0.png",
          "after a step left, the newer neighbour comes before the older one");
    Slideshow two;
    two.open(make(2));
    check(two.window().size() == 2, "two pictures: two in the window");
    Slideshow three;
    three.open(make(3));
    check(three.window().size() == 3, "three pictures: three");
    Slideshow one;
    one.open(make(1));
    check(one.window().size() == 1, "one picture: one");
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("ps5sd_test_slideshow_" + std::to_string((long) getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    test_names();
    test_scan(dir / "outputs");
    test_steps();
    test_repeat();
    test_stick();
    test_buttons();
    test_step_and_wrap();
    test_auto();
    test_new_pictures();
    test_window();
    fs::remove_all(dir);
    if (failures == 0) {
        std::printf("slideshow: all checks passed\n");
        return 0;
    }
    std::printf("slideshow: %d check(s) failed\n", failures);
    return 1;
}
