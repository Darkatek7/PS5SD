// Host test for console_stats.cpp's pure part: ps5-exporter's metrics as the
// console served them (fixtures/ps5-exporter-0.2.0.txt, FW 13.00, PS5SD idle),
// the power word, the history and its JSON. No socket is opened.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "console_stats.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "json.hpp"

using namespace ps5sd;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

#define CHECK(cond) check((cond), std::string(#cond) + " (line " + std::to_string(__LINE__) + ")")

std::string fixture() {
    std::ifstream in(PS5SD_FIXTURES "/ps5-exporter-0.2.0.txt", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void test_parse() {
    const std::string text = fixture();
    CHECK(!text.empty());
    const ConsoleReading r = parse_exporter(text);
    CHECK(r.cpu_c == 45);
    CHECK(r.soc_c == 45);  // the hottest of soc0..soc15 (soc4, soc5, soc15)
    CHECK(r.sensors.size() == 17 && r.sensors.at("soc0") == 43 && r.sensors.at("soc11") == 41);
    CHECK(std::fabs(r.watts - 19.526) < 1e-9);  // 4647154633798: 19526 mW in the low half
    CHECK(std::fabs(r.fan - 0.2305) < 1e-9);
    CHECK(std::fabs(r.cpu_ghz - 3.2) < 1e-9);
    CHECK(std::isnan(r.cpu) && std::isnan(r.job));  // not from the exporter

    const ConsoleReading none = parse_exporter("");
    CHECK(std::isnan(none.soc_c) && std::isnan(none.watts) && std::isnan(none.fan) && none.sensors.empty());
}

void test_samples() {
    CHECK(metrics_sample("a 1\nb 2\n", "a") == 1);
    CHECK(metrics_sample("a 1\nb 2\n", "b") == 2);
    CHECK(std::isnan(metrics_sample("ab 1\n", "a")));       // a name is matched whole
    CHECK(std::isnan(metrics_sample("# a 1\n", "a")));      // only where a line starts
    CHECK(std::isnan(metrics_sample("a x\n", "a")));        // not a number
    CHECK(std::isnan(soc_watts(0)) && std::isnan(soc_watts(999)) && std::isnan(soc_watts(NAN)));
    CHECK(soc_watts(350000) == 350 && std::isnan(soc_watts(350001)));
    CHECK(soc_watts((7.0 * 4294967296.0) + 86000) == 86);  // the high half is not power
}

void test_history() {
    ConsoleHistory h;
    for (int i = 0; i < 150; i++) {
        ConsoleReading r;
        r.soc_c = i;
        r.cpu   = 0.5;
        if (i % 2 == 0) {
            r.watts = 20;  // every other second unread
        }
        h.add(r);
    }
    CHECK(h.seconds().size() == ConsoleHistory::kPoints);
    CHECK(h.seconds().front().soc_c == 30 && h.seconds().back().soc_c == 149);
    CHECK(h.minutes().size() == 5);
    CHECK(h.minutes()[0].soc_c == 14.5 && h.minutes()[1].soc_c == 44.5);  // means of 0..29 and 30..59
    CHECK(h.minutes()[0].watts == 20);  // the unread seconds are left out
    CHECK(std::isnan(h.minutes()[0].fan));  // never read

    const nlohmann::json j = nlohmann::json::parse(h.json("running"));
    CHECK(j["exporter"] == "running");
    CHECK(j["now"]["soc_c"] == 149 && j["now"]["watts"].is_null() && j["now"]["fan"].is_null());
    CHECK(j["seconds"]["soc_c"].size() == ConsoleHistory::kPoints && j["seconds"]["watts"][0] == 20 &&
          j["seconds"]["watts"][1].is_null());
    CHECK(j["minutes"]["cpu"].size() == 5 && j["minutes"]["cpu"][0] == 0.5);
    CHECK(j["seconds_interval"] == 1 && j["minutes_interval"] == ConsoleHistory::kMean);

    const nlohmann::json e = nlohmann::json::parse(ConsoleHistory().json("not running"));
    CHECK(e["now"]["soc_c"].is_null() && e["seconds"]["soc_c"].empty() && e["sensors"].empty());
}

void test_job_seconds() {
    AppStatus s;
    CHECK(s.job_seconds() == 0);
    s.set_phase("Generating", "a lighthouse");
    s.set_phase("Ready");
    const double one = s.job_seconds();
    CHECK(one >= 0 && one < 1);
    s.set_phase("Generating", "a windmill");
    CHECK(s.job_seconds() >= one);
}

}  // namespace

int main() {
    test_parse();
    test_samples();
    test_history();
    test_job_seconds();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("console_stats: all checks passed\n");
    return 0;
}
