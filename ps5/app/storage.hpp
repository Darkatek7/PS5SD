// Storage on the console: how much room /data and the USB drives have, what
// the app's folders use, and whether a download fits.
//
// Header only (build-app.sh compiles each .cpp by name; main.cpp, models.cpp
// and downloader.cpp include this), and the PS5 SDK has what it uses: stat.
// Not statfs: the title loads no module that defines it (build-app.sh warns),
// so it would be null at run time.
//
// GET /ps5sd/v1/app/storage:
//   {"console": {"path": "/data", "free": n},
//    "used": {"models": n, "loras": n, "gallery": n, "upscalers": n},
//    "usb": [{"id": "usb0", "path": "/mnt/usb0", "free": n}]}
// free is null when it is not known. The title cannot read it itself (its
// statvfs answers 64 GiB with 16 GiB free for every path), so main.cpp asks
// ftpsrv (ftp_free_space: AVBL). There is no total for the same reason.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/stat.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "json.hpp"

namespace ps5sd {

// A download that is not to fill the console: it needs its size plus this much free.
constexpr uint64_t kSpaceMargin = 1024ull * 1024 * 1024;

// "7.3 GB": decimal gigabytes with one digit.
inline std::string gigabytes_text(uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / 1e9);
    return text;
}

// "" when needed bytes (and the margin) fit in free_bytes, else the error a
// download fails with. needed 0 (size not known) always fits.
inline std::string space_shortfall(uint64_t needed, uint64_t free_bytes) {
    if (needed == 0 || needed + kSpaceMargin <= free_bytes) {
        return {};
    }
    return "Not enough space on the console: it needs " + gigabytes_text(needed) + ", " + gigabytes_text(free_bytes) +
           " are free.";
}

// A drive id: usb0 to usb7, as /mnt has them.
inline bool usb_id_ok(const std::string & id) {
    return id.size() == 4 && id.compare(0, 3, "usb") == 0 && id[3] >= '0' && id[3] <= '7';
}

// Is dir a mounted file system rather than an empty mount point? True when
// dir lies on another device than its parent.
inline bool is_mount_point(uint64_t dev, uint64_t parent_dev) {
    return dev != parent_dev;
}

// Is a drive mounted at <root>/<id>? On the PS5 /mnt/usb0 to usb7 always
// exist as folders; an empty one lies on the same device as /mnt.
inline bool usb_mounted(const std::string & root, const std::string & id) {
    if (!usb_id_ok(id)) {
        return false;
    }
    const std::string path = root + "/" + id;
    struct stat       st, parent;
    if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || stat(root.c_str(), &parent) != 0) {
        return false;
    }
    return is_mount_point(static_cast<uint64_t>(st.st_dev), static_cast<uint64_t>(parent.st_dev));
}

struct UsbDrive {
    std::string id, path;
    int64_t     free_bytes = -1;  // -1: not known
};

// The drives that are mounted under root, in id order, without their free space.
inline std::vector<UsbDrive> usb_drives(const std::string & root) {
    std::vector<UsbDrive> out;
    for (char c = '0'; c <= '7'; c++) {
        UsbDrive d;
        d.id   = std::string("usb") + c;
        d.path = root + "/" + d.id;
        if (usb_mounted(root, d.id)) {
            out.push_back(d);
        }
    }
    return out;
}

// The bytes of the regular files under dir (all folders below it, hidden ones
// too: the gallery's thumbnails count). 0 for a folder that is not there.
inline uint64_t dir_bytes(const std::string & dir) {
    namespace fs = std::filesystem;
    uint64_t      sum = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && it != end) {
        std::error_code e2;
        if (it->is_regular_file(e2) && !e2) {
            const uint64_t n = it->file_size(e2);
            if (!e2) {
                sum += n;
            }
        }
        it.increment(ec);
    }
    return sum;
}

// Remembers a value for a while (the folder sizes: walking the gallery takes
// time). now is a parameter so a test can move the clock.
class SizeCache {
  public:
    explicit SizeCache(int seconds = 30) : seconds_(seconds) {}

    template <class F> uint64_t get(const std::string & key, std::chrono::steady_clock::time_point now, F && compute) {
        {
            std::lock_guard<std::mutex> lock(m_);
            const auto                  it = map_.find(key);
            if (it != map_.end() && now - it->second.at < std::chrono::seconds(seconds_)) {
                return it->second.value;
            }
        }
        const uint64_t value = compute();  // outside the lock: it walks a folder
        std::lock_guard<std::mutex> lock(m_);
        map_[key] = { value, now };
        return value;
    }

  private:
    struct Entry {
        uint64_t                              value;
        std::chrono::steady_clock::time_point at;
    };
    int                          seconds_;
    std::mutex                   m_;
    std::map<std::string, Entry> map_;
};

struct StorageInfo {
    std::string console_path = "/data";
    int64_t     console_free = -1;  // -1: not known
    uint64_t    models = 0, loras = 0, gallery = 0, upscalers = 0;
    std::vector<UsbDrive> usb;
};

inline std::string storage_json(const StorageInfo & s) {
    auto           free_of = [](int64_t n) { return n < 0 ? nlohmann::json(nullptr) : nlohmann::json(n); };
    nlohmann::json usb     = nlohmann::json::array();
    for (const UsbDrive & d : s.usb) {
        usb.push_back({ { "id", d.id }, { "path", d.path }, { "free", free_of(d.free_bytes) } });
    }
    const nlohmann::json out = {
        { "console", { { "path", s.console_path }, { "free", free_of(s.console_free) } } },
        { "used", { { "models", s.models }, { "loras", s.loras }, { "gallery", s.gallery }, { "upscalers", s.upscalers } } },
        { "usb", usb }
    };
    return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace ps5sd
