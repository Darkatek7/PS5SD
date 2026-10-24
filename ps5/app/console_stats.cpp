// SPDX-License-Identifier: GPL-3.0-or-later
#include "console_stats.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include "json.hpp"
#include "thread_guard.hpp"

namespace ps5sd {

namespace {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

constexpr int          kExporterPort = 9100;
constexpr int          kLoaderPort   = 9021;
constexpr const char * kExporterElf  = "/app0/payloads/ps5-exporter.elf";
constexpr int          kFanPort      = 9075;
constexpr const char * kFanElf       = "/app0/payloads/ps5sd-fan.elf";

int connect_loopback(int port, int timeout_s) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    timeval tv{ timeout_s, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    sockaddr_in sa{};
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(static_cast<uint16_t>(port));
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool send_all(int fd, const char * p, size_t n) {
    while (n > 0) {
        const ssize_t k = ::send(fd, p, n, kSendFlags);
        if (k <= 0) {
            return false;
        }
        p += k;
        n -= static_cast<size_t>(k);
    }
    return true;
}

// The body of GET <path> on 127.0.0.1:<port>, or empty.
std::string http_get(int port, const char * path) {
    const int fd = connect_loopback(port, 2);
    if (fd < 0) {
        return {};
    }
    const std::string req = std::string("GET ") + path + " HTTP/1.0\r\nHost: localhost\r\n\r\n";
    std::string       out;
    if (send_all(fd, req.data(), req.size())) {
        char buf[8192];
        for (ssize_t got; (got = ::recv(fd, buf, sizeof(buf), 0)) > 0 && out.size() < (1u << 20);) {
            out.append(buf, static_cast<size_t>(got));
        }
    }
    ::close(fd);
    const size_t at = out.find("\r\n\r\n");
    return at == std::string::npos ? std::string() : out.substr(at + 4);
}

// The fan payload's answer to one line, or empty.
std::string fan_command(const std::string & line) {
    const int fd = connect_loopback(kFanPort, 2);
    if (fd < 0) {
        return {};
    }
    std::string       out;
    const std::string req = line + "\n";
    if (send_all(fd, req.data(), req.size())) {
        char buf[256];
        for (ssize_t got; (got = ::recv(fd, buf, sizeof(buf), 0)) > 0 && out.size() < 1024;) {
            out.append(buf, static_cast<size_t>(got));
        }
    }
    ::close(fd);
    return out;
}

// Hands the ELF to the loader, which runs it once the connection ends.
bool send_payload(const char * path, int port) {
    std::ifstream     in(path, std::ios::binary);
    std::vector<char> elf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (elf.empty()) {
        return false;
    }
    const int fd = connect_loopback(port, 10);
    if (fd < 0) {
        return false;
    }
    const bool ok = send_all(fd, elf.data(), elf.size());
    ::close(fd);
    return ok;
}

double cpu_seconds() {
    rusage u{};
    getrusage(RUSAGE_SELF, &u);
    return u.ru_utime.tv_sec + u.ru_stime.tv_sec + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The mean of the read values of a field, or NaN.
double mean(const std::deque<ConsoleReading> & rs, double ConsoleReading::*field) {
    double sum = 0;
    int    n   = 0;
    for (const ConsoleReading & r : rs) {
        if (!std::isnan(r.*field)) {
            sum += r.*field;
            n++;
        }
    }
    return n > 0 ? sum / n : NAN;
}

struct Field {
    const char * name;
    double ConsoleReading::*value;
};
constexpr Field kFields[] = {
    { "soc_c", &ConsoleReading::soc_c }, { "cpu_c", &ConsoleReading::cpu_c }, { "watts", &ConsoleReading::watts },
    { "fan", &ConsoleReading::fan },     { "cpu_ghz", &ConsoleReading::cpu_ghz }, { "cpu", &ConsoleReading::cpu },
    { "job", &ConsoleReading::job },
};

// A number to one more decimal than the screen shows, or null.
nlohmann::json value(double v) {
    return std::isnan(v) ? nlohmann::json(nullptr) : nlohmann::json(std::round(v * 1000.0) / 1000.0);
}

nlohmann::json series(const std::deque<ConsoleReading> & rs) {
    nlohmann::json out = nlohmann::json::object();
    for (const Field & f : kFields) {
        nlohmann::json points = nlohmann::json::array();
        for (const ConsoleReading & r : rs) {
            points.push_back(value(r.*f.value));
        }
        out[f.name] = std::move(points);
    }
    return out;
}

}  // namespace

double metrics_sample(const std::string & text, const std::string & key) {
    const std::string line = key + " ";
    size_t            at   = text.rfind(line, 0) == 0 ? 0 : text.find("\n" + line);
    if (at == std::string::npos) {
        return NAN;
    }
    at += (text[at] == '\n' ? 1 : 0) + line.size();
    char *       end = nullptr;
    const double v   = std::strtod(text.c_str() + at, &end);
    return end == text.c_str() + at ? NAN : v;
}

double soc_watts(double raw) {
    if (std::isnan(raw) || raw < 0) {
        return NAN;
    }
    const uint64_t mw = static_cast<uint64_t>(raw) & 0xffffffffu;
    return mw >= 1000 && mw <= 350000 ? static_cast<double>(mw) / 1000.0 : NAN;
}

ConsoleReading parse_exporter(const std::string & text) {
    ConsoleReading r;
    if (text.empty()) {
        return r;
    }
    for (int k = -1; k < 16; k++) {
        const std::string name = k < 0 ? std::string("cpu") : "soc" + std::to_string(k);
        const double      t    = metrics_sample(text, "ps5_temperature_celsius{sensor=\"" + name + "\"}");
        if (!std::isnan(t) && t > -100 && t < 200) {
            r.sensors[name] = t;
            if (k < 0) {
                r.cpu_c = t;
            } else if (std::isnan(r.soc_c) || t > r.soc_c) {
                r.soc_c = t;
            }
        }
    }
    r.watts         = soc_watts(metrics_sample(text, "ps5_soc_power_raw_value"));
    const double fan = metrics_sample(text, "ps5_fan_duty_ratio");
    r.fan           = std::isnan(fan) ? NAN : std::clamp(fan, 0.0, 1.0);
    const double hz = metrics_sample(text, "ps5_cpu_frequency_hertz");
    r.cpu_ghz       = hz > 0 ? hz / 1e9 : NAN;
    return r;
}

void ConsoleHistory::add(const ConsoleReading & r) {
    seconds_.push_back(r);
    if (seconds_.size() > kPoints) {
        seconds_.pop_front();
    }
    pending_.push_back(r);
    if (pending_.size() >= static_cast<std::size_t>(kMean)) {
        ConsoleReading m;
        for (const Field & f : kFields) {
            m.*f.value = mean(pending_, f.value);
        }
        minutes_.push_back(m);
        if (minutes_.size() > kPoints) {
            minutes_.pop_front();
        }
        pending_.clear();
    }
}

std::string ConsoleHistory::json(const std::string & exporter) const {
    nlohmann::json out;
    out["exporter"] = exporter;
    const ConsoleReading last = seconds_.empty() ? ConsoleReading{} : seconds_.back();
    nlohmann::json       now  = nlohmann::json::object();
    for (const Field & f : kFields) {
        now[f.name] = value(last.*f.value);
    }
    out["now"]     = std::move(now);
    nlohmann::json sensors = nlohmann::json::object();
    for (const auto & s : last.sensors) {
        sensors[s.first] = s.second;
    }
    out["sensors"]          = std::move(sensors);
    out["seconds"]          = series(seconds_);
    out["seconds_interval"] = 1;
    out["minutes"]          = series(minutes_);
    out["minutes_interval"] = kMean;
    return out.dump();
}

bool parse_fan_reply(const std::string & line, FanState & out) {
    if (line.compare(0, 3, "OK ") != 0) {
        return false;
    }
    FanState    f;
    std::size_t at = 3;
    while (at < line.size()) {
        std::size_t end = line.find_first_of(" \r\n", at);
        if (end == std::string::npos) {
            end = line.size();
        }
        const std::string word = line.substr(at, end - at);
        const std::size_t eq   = word.find('=');
        if (eq != std::string::npos) {
            const std::string k = word.substr(0, eq), v = word.substr(eq + 1);
            if (k == "mode") {
                f.mode = v;
            } else if (k == "filtered") {
                f.filtered = std::atof(v.c_str());
            } else if (k == "stock") {
                f.stock = std::atoi(v.c_str());
            } else if (k == "target") {
                f.target = std::atoi(v.c_str());
            } else if (k == "wanted") {
                f.wanted = std::atoi(v.c_str());
            } else if (k == "duty") {
                f.duty = std::atoi(v.c_str());
            }
        }
        at = end + 1;
    }
    if (f.mode.empty()) {
        return false;
    }
    out = f;
    return true;
}

void ConsoleStats::set_fan(const std::string & mode, int target) {
    std::lock_guard<std::mutex> lock(mutex_);
    fan_mode_   = mode;
    fan_target_ = target;
}

// Tells the payload the mode (its heartbeat), and starts it when it does not
// answer: at most three times, 20 s apart.
void ConsoleStats::fan_tick(double up) {
    std::string mode;
    int         target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mode   = fan_mode_;
        target = fan_target_;
    }
    if (mode == "quiet" && !fan_on_) {
        std::lock_guard<std::mutex> lock(mutex_);
        fan_status_ = "off";
        fan_        = FanState{};
        return;
    }
    const std::string line  = "MODE " + mode + (mode == "manual" ? " " + std::to_string(target) : std::string());
    const std::string reply = fan_command(line);
    FanState          f;
    std::string       status;
    if (parse_fan_reply(reply, f)) {
        fan_on_ = mode != "quiet";  // in quiet it puts the console's own target back and ends
        status  = mode == "quiet" ? "off" : "running";
    } else if (mode == "quiet") {
        fan_on_ = false;
        status  = "off";
    } else if (fan_sent_ < 3 && up - fan_sent_at_ >= 20) {
        fan_sent_++;
        fan_sent_at_  = up;
        const bool ok = send_payload(kFanElf, kLoaderPort);
        std::printf("ps5sd: fan mode %s: %s\n", mode.c_str(),
                    ok ? "sent ps5sd-fan.elf to the ELF loader" : "no ELF loader on 9021 took ps5sd-fan.elf");
        status = ok ? "starting" : "no ELF loader";
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        status = fan_sent_ >= 3 && up - fan_sent_at_ >= 20 ? "not answering" : fan_status_;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    fan_status_ = status;
    fan_        = f;
}

void ConsoleStats::start(AppStatus & status) {
    std::thread([this, &status] { run_guarded("console stats", [&] { run(status); }); }).detach();
}

std::string ConsoleStats::json() {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json j = nlohmann::json::parse(history_.json(exporter_));
    auto           c = [](int v) { return v < 0 ? nlohmann::json() : nlohmann::json(v); };
    j["fan_control"] = {
        { "mode", fan_mode_ },
        { "manual_target", fan_target_ },
        { "state", fan_status_ },
        { "stock", c(fan_.stock) },
        { "target", c(fan_.target) },
        { "wanted", c(fan_.wanted) },
    };
    return j.dump();
}

void ConsoleStats::run(AppStatus & status) {
    const unsigned cpus  = std::max(1u, std::thread::hardware_concurrency());
    const double   start = now_seconds();
    double         last_wall = start, last_cpu = cpu_seconds(), last_job = status.job_seconds();
    int            sent = 0;  // times the exporter was sent: at the start and a minute later, no more
    unsigned       ticks = 0;
    std::printf("ps5sd: console stats: %u CPUs, sensors from ps5-exporter on port %d\n", cpus, kExporterPort);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (++ticks % 3 == 0) {
            fan_tick(now_seconds() - start);
        }
        const std::string text = http_get(kExporterPort, "/metrics");
        std::string       state = "running";
        if (text.empty()) {
            state           = "not running";
            const double up = now_seconds() - start;
            if ((sent == 0 && up >= 2) || (sent == 1 && up >= 62)) {
                sent++;
                const bool ok = send_payload(kExporterElf, kLoaderPort);
                std::printf("ps5sd: ps5-exporter does not answer on %d; %s\n", kExporterPort,
                            ok ? "sent the bundled copy to the ELF loader" : "no ELF loader on 9021 took it");
                if (!ok) {
                    state = "no ELF loader";
                }
            } else if (sent == 2 && up >= 75) {
                state = "not answering";  // the loader took it twice; it does not run on this console
            }
        }
        ConsoleReading r  = parse_exporter(text);
        const double   wall = now_seconds(), cpu = cpu_seconds(), job = status.job_seconds();
        const double   dt   = wall - last_wall;
        if (dt > 0) {
            r.cpu = std::clamp((cpu - last_cpu) / (dt * cpus), 0.0, 1.0);
            r.job = std::clamp((job - last_job) / dt, 0.0, 1.0);
        }
        last_wall = wall;
        last_cpu  = cpu;
        last_job  = job;
        AppStatus::Console c;
        c.soc_c = r.soc_c;
        c.watts = r.watts;
        c.fan   = r.fan;
        c.cpu   = r.cpu;
        c.job   = r.job;
        status.set_console(c);
        std::lock_guard<std::mutex> lock(mutex_);
        if (exporter_ != "no ELF loader" || state == "running") {
            exporter_ = state;
        }
        history_.add(r);
    }
}

}  // namespace ps5sd
