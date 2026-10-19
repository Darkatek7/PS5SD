// Host test for app_status.cpp: the state a phase text stands for, the
// bookkeeping of a job (prompt, steps, results, preview) and the server
// config read from the same files main.cpp reads. No kit, GL or sd.cpp.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_status.hpp"

#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

using namespace ps5sd;
using State = AppStatus::State;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

#define CHECK(cond) check((cond), std::string(#cond) + " (line " + std::to_string(__LINE__) + ")")

void test_classify() {
    CHECK(AppStatus::classify("Opening the GPU") == State::starting);
    CHECK(AppStatus::classify("Loading the model") == State::loading);
    CHECK(AppStatus::classify("Ready") == State::ready);
    CHECK(AppStatus::classify("Done") == State::ready);
    CHECK(AppStatus::classify("Generating") == State::generating);
    CHECK(AppStatus::classify("Generation failed") == State::error);
    CHECK(AppStatus::classify("The model did not load") == State::error);
    CHECK(AppStatus::classify("The server did not start") == State::error);
    CHECK(AppStatus::classify("Stopped by an error") == State::error);
    CHECK(AppStatus::classify("Nothing to do") == State::error);
    CHECK(AppStatus::classify("The image could not be saved") == State::error);
    CHECK(AppStatus::classify("Stopping the server") == State::stopped);
    CHECK(AppStatus::classify("Server stopped") == State::stopped);
    CHECK(AppStatus::classify("The server stopped") == State::stopped);
}

void test_names() {
    CHECK(AppStatus::display_name("/data/PS5SD/models/sdxs-512-0.9_Q8_0.gguf") == "sdxs-512-0.9_Q8_0");
    CHECK(AppStatus::display_name("model.safetensors") == "model");
    CHECK(AppStatus::display_name("noext") == "noext");
    CHECK(AppStatus::display_name("/a/b/") == "");
}

void test_job() {
    AppStatus s;
    s.set_phase("Loading the model", "/data/PS5SD/models/m.gguf");
    s.set_progress(500, 1000, 0);
    CHECK(s.snapshot().state == State::loading);
    CHECK(s.snapshot().model == "m");
    CHECK(s.snapshot().step == 500);

    // sd-server's job_begin: the steps first, then the phase. They survive it.
    s.set_phase("Ready", "API and web UI on port 7860");
    s.set_progress(0, 20, 0);
    s.set_phase("Generating", "an astronaut");
    auto snap = s.snapshot();
    CHECK(snap.state == State::generating);
    CHECK(snap.steps == 20);
    CHECK(snap.prompt == "an astronaut");
    CHECK(snap.job_s >= 0);

    // The one-shot loads a model first: its tensors are not steps of the job.
    AppStatus one;
    one.set_phase("Loading the model", "m.gguf");
    one.set_progress(1200, 1200, 0);
    one.set_phase("Generating", "a cat");
    CHECK(one.snapshot().steps == 0);

    const unsigned char rgb[2 * 2 * 3] = {};
    s.set_preview(rgb, 2, 2);
    snap = s.snapshot();
    CHECK(snap.preview && snap.preview->width == 2 && snap.preview->rgb.size() == 12);
    CHECK(snap.preview_serial == 1);
    s.set_preview(nullptr, 2, 2);
    s.set_preview(rgb, 0, 2);
    CHECK(s.snapshot().preview_serial == 1);  // bad calls are ignored

    s.set_result("/data/PS5SD/outputs/a.png");
    s.set_phase("Ready", "saved");
    snap = s.snapshot();
    CHECK(snap.state == State::ready);
    CHECK(snap.steps == 0);
    CHECK(snap.last_job_s >= 0);
    CHECK(snap.images == 1);
    CHECK(snap.recent.size() == 1 && snap.recent[0].prompt == "an astronaut");
    CHECK(snap.preview != nullptr);  // kept until the next job

    s.set_phase("Generating", "a second one");
    CHECK(s.snapshot().preview == nullptr);
    s.set_result("/data/PS5SD/outputs/b.png");
    s.set_result("/data/PS5SD/outputs/a.png");  // the same path again moves it to the front
    snap = s.snapshot();
    CHECK(snap.recent.size() == 2 && snap.recent[0].path.back() == 'g' && snap.recent[0].path.find("a.png") != std::string::npos);
    for (int i = 0; i < 40; i++) {
        s.set_result("/x/" + std::to_string(i) + ".png");
    }
    CHECK(s.snapshot().recent.size() == 12);

    for (int i = 0; i < 40; i++) {
        s.log("line " + std::to_string(i) + "\n");
    }
    snap = s.snapshot();
    CHECK(snap.lines.size() == 24 && snap.lines.back() == "line 39");

    s.set_port(0);
    s.set_port(70000);
    CHECK(s.snapshot().port == 7860);
    s.set_port(8080);
    CHECK(s.snapshot().port == 8080);
}

void test_config() {
    const std::string model = "/tmp/ps5sd-test-model.txt", args = "/tmp/ps5sd-test-args.txt";
    {
        std::ofstream(model) << "# comment\n/data/PS5SD/models/one.gguf\n";
        std::ofstream(args) << "--listen-port\n8000\n";
    }
    ServerConfig c = read_server_config(model.c_str(), args.c_str(), 7860);
    CHECK(c.model == "one" && c.port == 8000);
    {
        std::ofstream(args) << "-m\n/models/two.safetensors\n--listen-ip\n0.0.0.0\n";
    }
    c = read_server_config(model.c_str(), args.c_str(), 7860);
    CHECK(c.model == "two" && c.port == 7860);  // server-args.txt names the model: it wins
    c = read_server_config("/nonexistent", nullptr, 9000);
    CHECK(c.model.empty() && c.port == 9000);
    unlink(model.c_str());
    unlink(args.c_str());
}

}  // namespace

int main() {
    test_classify();
    test_names();
    test_job();
    test_config();
    std::printf("%s (%d failure%s)\n", failures == 0 ? "ok" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
