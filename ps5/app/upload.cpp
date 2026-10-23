// Uploads from the web UI (upload.hpp).
#include "upload.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <vector>

#include "download_core.hpp"
#include "ftp_writer.hpp"
#include "json.hpp"

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace ps5sd {

namespace {

std::mutex            g_mutex;
std::set<std::string> g_busy;  // the files being uploaded

constexpr size_t   kBlock     = 1u << 20;  // bytes passed on to ftpsrv at once
constexpr uint64_t kPresetMax = 64u << 10;
constexpr uint64_t kTagsMax   = 64u << 20;

std::string error_json(const std::string & message) {
    return json{ { "error", message } }.dump();
}

bool write_all(int fd, const char * data, size_t n) {
    while (n > 0) {
        const ssize_t k = ::write(fd, data, n);
        if (k < 0 && errno == EINTR) {
            continue;
        }
        if (k <= 0) {
            return false;
        }
        data += k;
        n -= static_cast<size_t>(k);
    }
    return true;
}

}  // namespace

int upload_file(const UploadDirs & dirs, const std::string & path, uint64_t length, const UploadReader & read,
                std::string & out) {
    std::string kind, name, error;
    if (!upload_target(path, kind, name, error)) {
        out = error_json(error);
        return 400;
    }
    if (kind == "tags" && length > kTagsMax) {
        out = error_json("a tag file is at most 64 MB; this one has " + std::to_string(length) + " bytes");
        return 413;
    }
    if (kind == "presets" && length > kPresetMax) {
        out = error_json("a preset is a small text file; this one has " + std::to_string(length) + " bytes");
        return 413;
    }
    const std::string & dir  = kind == "models" ? dirs.models
                               : kind == "loras" ? dirs.loras
                               : kind == "embeddings" ? dirs.embeddings
                               : kind == "upscalers" ? dirs.upscalers
                               : kind == "tags" ? dirs.tags
                                                      : dirs.presets;
    const std::string   dest = dir + "/" + name;
    const std::string   part = dest + ".part";
    std::error_code     ec;
    if (fs::exists(fs::u8path(dest), ec)) {
        out = error_json(kind + "/" + name + " is there already; delete it first or upload it under another name");
        return 409;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_busy.insert(dest).second) {
            out = error_json(kind + "/" + name + " is being uploaded already");
            return 409;
        }
    }
    struct Release {
        const std::string & dest;
        ~Release() {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_busy.erase(dest);
        }
    } release{ dest };
    fs::create_directories(fs::u8path(dest).parent_path(), ec);

    // Through ftpsrv, or PS5SD's own copy, started here if need be: the
    // title's own writes slow to about 2 MB/s after 1.4 GB (ftp_writer.hpp).
    // Directly when neither answers.
    FtpWriter   ftp;
    std::string ftp_error;
    const bool  via_ftp = connect_ftp_writer(ftp, ftp_error) && ftp.begin(part, 0, ftp_error);
    int         fd      = -1;
    if (!via_ftp) {
        std::printf("ps5sd: upload %s: ftpsrv: %s; writing directly\n", dest.c_str(), ftp_error.c_str());
        fd = ::open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            out = error_json("cannot write " + part + ": " + std::strerror(errno));
            return 500;
        }
    }
    std::printf("ps5sd: upload %s: %llu bytes, %s\n", dest.c_str(), (unsigned long long) length,
                via_ftp ? ("through ftpsrv on " + std::to_string(ftp.port())).c_str() : "directly");
    const auto        t0 = std::chrono::steady_clock::now();
    std::vector<char> block;
    block.reserve(kBlock);
    uint64_t    got = 0;
    std::string write_error;
    bool        disk_full = false;
    const auto  flush     = [&] {
        if (block.empty()) {
            return true;
        }
        bool ok;
        if (via_ftp) {
            ok        = ftp.send(block.data(), block.size(), write_error);
            disk_full = !ok && (write_error.find(" 552 ") != std::string::npos || write_error.find(" 452 ") != std::string::npos);
        } else {
            ok        = write_all(fd, block.data(), block.size());
            disk_full = !ok && errno == ENOSPC;
            if (!ok) {
                write_error = std::string("write: ") + std::strerror(errno);
            }
        }
        block.clear();
        return ok;
    };
    const bool complete = read([&](const char * data, size_t n) {
        got += n;
        if (length > 0 && got > length) {
            write_error = "the body is longer than its Content-Length";
            return false;
        }
        block.insert(block.end(), data, data + n);
        return block.size() < kBlock || flush();
    });
    bool ok = complete && flush();
    if (via_ftp) {
        std::string end_error;
        if (!ftp.end(end_error) && ok) {
            ok          = false;
            write_error = end_error;
        }
    } else {
        ::fsync(fd);
        ::close(fd);
    }
    if (ok && length > 0 && got != length) {
        ok          = false;
        write_error = "the body is shorter than its Content-Length";
    }
    if (!ok) {
        ::unlink(part.c_str());
        const std::string why = disk_full             ? "the console's storage is full"
                                : !write_error.empty() ? write_error
                                                       : "the upload broke off";
        std::printf("ps5sd: upload %s: failed at %llu bytes: %s\n", dest.c_str(), (unsigned long long) got, why.c_str());
        out = error_json(why + " (" + std::to_string(got) + " bytes in)");
        return disk_full ? 507 : 400;
    }
    std::string rename_error;
    if (!(via_ftp && ftp.rename(part, dest, rename_error)) && std::rename(part.c_str(), dest.c_str()) != 0) {
        out = error_json("cannot rename " + part + ": " + std::strerror(errno));
        return 500;
    }
    const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("ps5sd: upload %s: done, %llu bytes in %.1f s (%.1f MB/s)\n", dest.c_str(), (unsigned long long) got,
                s, s > 0 ? got / 1e6 / s : 0.0);
    out = json{ { "kind", kind }, { "name", name }, { "bytes", got }, { "through_ftpsrv", via_ftp } }.dump();
    return 200;
}

}  // namespace ps5sd
