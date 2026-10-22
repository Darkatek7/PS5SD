// The console's temperature, power and fan, and PS5SD's own CPU use and job
// time, once a second, for GET /ps5sd/v1/app/console and the TV.
//
// A title may not read the sensors (PS5LM measured 0x80020002); a payload
// may. ps5-exporter (Marice, GPL-3.0, bundled in /app0/payloads) serves them
// on 127.0.0.1:9100. When it does not answer, the title sends it to the ELF
// loader on 9021: at the start, and once more a minute later, not again (a
// payload that fails to start would otherwise be started over and over). It
// keeps running after PS5SD closes.
//
// The CPU is PS5SD's own use (getrusage) of the console's CPUs. The GPU has
// no reading, so "job" is the share of each second a job ran (AppStatus), not
// the GPU's utilisation.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app_status.hpp"

#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>

namespace ps5sd {

// One second's readings; NaN for what is not read.
struct ConsoleReading {
    double                        soc_c   = NAN;  // the SoC's hottest sensor, degrees C
    double                        cpu_c   = NAN;  // the CPU sensor
    double                        watts   = NAN;  // the SoC's power (CPU, GPU and memory together)
    double                        fan     = NAN;  // the fan's duty, 0..1
    double                        cpu_ghz = NAN;
    double                        cpu     = NAN;  // PS5SD's share of the CPUs, 0..1
    double                        job     = NAN;  // the share of the second a job ran, 0..1
    std::map<std::string, double> sensors;        // every temperature sensor that answered ("cpu", "soc0", ...)
};

// A sample of Prometheus text by its name and labels, as it starts a line
// ("ps5_temperature_celsius{sensor=\"cpu\"}"), or NaN.
double metrics_sample(const std::string & text, const std::string & key);

// sceKernelGetSocPowerConsumption's first word, as ps5-exporter passes it on:
// milliwatts in the low 32 bits (PS5LM: 22 W idle, 86 W under load); NaN
// outside 1 to 350 W.
double soc_watts(double raw);

// The sensors from ps5-exporter's /metrics; all NaN for empty text.
ConsoleReading parse_exporter(const std::string & text);

// The last 120 seconds, and the last hour as 120 means of 30 seconds.
class ConsoleHistory {
  public:
    static constexpr std::size_t kPoints = 120;
    static constexpr int         kMean   = 30;

    void add(const ConsoleReading & r);
    // {"exporter": state, "now": {...}, "sensors": {...}, "seconds": {...}, "minutes": {...}}: each series
    // oldest first, null for a point not read.
    std::string json(const std::string & exporter) const;
    const std::deque<ConsoleReading> & seconds() const { return seconds_; }
    const std::deque<ConsoleReading> & minutes() const { return minutes_; }

  private:
    std::deque<ConsoleReading> seconds_;
    std::deque<ConsoleReading> minutes_;
    std::deque<ConsoleReading> pending_;  // the seconds of the next mean
};

// The thread that reads once a second, and its JSON for the route.
class ConsoleStats {
  public:
    void        start(AppStatus & status);
    std::string json();

  private:
    void run(AppStatus & status);

    std::mutex     mutex_;
    ConsoleHistory history_;
    std::string    exporter_ = "starting";  // "running", "not running", "no ELF loader"
};

}  // namespace ps5sd
