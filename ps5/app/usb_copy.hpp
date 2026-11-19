// Copies of the gallery on a USB drive (GitHub issue 17: a recovery after a
// kernel panic can wipe /data).
//
// A copy goes to <mnt>/<usbN>/PS5SD/ under the file's name, and a file's
// parameters (the gallery's .thumbs/<name>.txt: a video's, or a PNG without
// metadata) to <mnt>/<usbN>/PS5SD/.thumbs/<name>.txt, the same layout as
// outputs. A file that is there with the same size is skipped.
//
// One worker thread does all of it, so no HTTP thread and no generation waits
// for a drive: the copies the web UI asks for (POST gallery/copy) and the
// ones the setting usb_copy asks for (each image or video the server saves).
// The bytes go through a CopySink; the title's is FtpSink (ftpsrv on loopback,
// as the downloader writes, because the title's own writes slow down after
// 1.4 GB, docs/CONSOLE.md) with LocalSink as the way out when no ftpsrv answers.
//
// Header only (build-app.sh compiles each .cpp by name). Needs
// download_core.cpp (gallery_name_ok) and gallery_labels.cpp
// (is_temporary_name) linked, as the app and the tests have them.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "download_core.hpp"
#include "ftp_writer.hpp"
#include "gallery_labels.hpp"
#include "json.hpp"
#include "storage.hpp"
#include "thread_guard.hpp"

namespace ps5sd {

// A name the copy may take: a file of the gallery (not a "tmp_" one, which is
// not kept).
inline bool copyable_name(const std::string & name) {
    return gallery_name_ok(name) && !is_temporary_name(name);
}

// POST gallery/copy: {"to": "usb0", "names": ["sd_....png", ...]}. names may be
// left out or empty (the whole gallery: names stays empty). False with the
// message for a body that is not that, a drive id that is not usb0 to usb7,
// or a name that is not a gallery file.
inline bool parse_copy_request(const std::string & body, std::string & to, std::vector<std::string> & names,
                               std::string & error) {
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    names.clear();
    if (!j.is_object() || !j.contains("to") || !j["to"].is_string()) {
        error = "the body must be {\"to\": \"usb0\", \"names\": [\"<file>\", ...]}";
        return false;
    }
    to = j["to"].get<std::string>();
    if (!usb_id_ok(to)) {
        error = "to must be a drive id from usb0 to usb7";
        return false;
    }
    if (j.contains("names") && !j["names"].is_null()) {
        if (!j["names"].is_array()) {
            error = "names must be a list of gallery file names";
            return false;
        }
        std::set<std::string> seen;
        for (const auto & n : j["names"]) {
            if (!n.is_string() || !copyable_name(n.get<std::string>())) {
                error = "not a gallery file: " + (n.is_string() ? n.get<std::string>() : n.dump());
                return false;
            }
            if (seen.insert(n.get<std::string>()).second) {
                names.push_back(n.get<std::string>());
            }
        }
    }
    return true;
}

// The skip rule: the copy is there already when it has the source's size.
inline bool copy_is_there(uint64_t source_size, bool dest_exists, uint64_t dest_size) {
    return dest_exists && dest_size == source_size;
}

// The files of the gallery in dir, by name: what the gallery lists (not the
// ones that are not kept).
inline std::vector<std::string> gallery_files(const std::string & dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code          ec;
    fs::directory_iterator   it(dir, ec), end;
    while (!ec && it != end) {
        std::error_code e2;
        const std::string name = it->path().filename().string();
        if (copyable_name(name) && it->is_regular_file(e2) && !e2) {
            out.push_back(name);
        }
        it.increment(ec);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The drive's side of a copy.
class CopySink {
  public:
    virtual ~CopySink() = default;
    // The folder exists afterwards (made with its parents if need be).
    virtual bool ensure_dir(const std::string & dir, std::string & error) = 0;
    // The size of a file that is there.
    virtual bool size(const std::string & path, uint64_t & out) = 0;
    // src to dst, through dst + ".part"; adds to bytes what it wrote. Stops
    // (false, error "stopped") when stop is set.
    virtual bool put(const std::string & src, const std::string & dst, const std::atomic<bool> & stop, uint64_t & bytes,
                     std::string & error) = 0;
};

inline bool local_ensure_dir(const std::string & dir, std::string & error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        return true;
    }
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) {
        error = "cannot make " + dir;
        return false;
    }
    return true;
}

inline bool local_size(const std::string & path, uint64_t & out) {
    std::error_code ec;
    const auto      n = std::filesystem::file_size(path, ec);
    if (ec) {
        return false;
    }
    out = n;
    return true;
}

// Plain writes by the title itself (and what the tests use).
class LocalSink : public CopySink {
  public:
    bool ensure_dir(const std::string & dir, std::string & error) override { return local_ensure_dir(dir, error); }
    bool size(const std::string & path, uint64_t & out) override { return local_size(path, out); }
    bool put(const std::string & src, const std::string & dst, const std::atomic<bool> & stop, uint64_t & bytes,
             std::string & error) override {
        std::ifstream in(src, std::ios::binary);
        if (!in) {
            error = "cannot read " + src;
            return false;
        }
        const std::string part = dst + ".part";
        {
            std::ofstream out(part, std::ios::binary | std::ios::trunc);
            std::vector<char> buffer(256 * 1024);
            uint64_t          wrote = 0;
            while (in) {
                if (stop) {
                    error = "stopped";
                    return false;
                }
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize n = in.gcount();
                if (n > 0) {
                    out.write(buffer.data(), n);
                    wrote += static_cast<uint64_t>(n);
                }
            }
            out.flush();
            if (!out) {
                error = "cannot write " + part;
                std::remove(part.c_str());
                return false;
            }
            bytes += wrote;
        }
        std::error_code ec;
        std::filesystem::rename(part, dst, ec);
        if (ec) {
            error = "cannot rename " + part + ": " + ec.message();
            return false;
        }
        return true;
    }
};

// Through ftpsrv (connect_ftp_writer: the user's on 2121, else PS5SD's own on 2122).
class FtpSink : public CopySink {
  public:
    // False with error when no ftpsrv answers.
    bool open(std::string & error) {
        ftp_.set_timeout(15);
        return connect_ftp_writer(ftp_, error);
    }
    bool ensure_dir(const std::string & dir, std::string & error) override {
        if (local_ensure_dir(dir, error)) {
            return true;
        }
        // the folder and its parents, one MKD each (an existing one answers with an error)
        std::string path;
        size_t      from = 1;
        while (from <= dir.size()) {
            const size_t slash = dir.find('/', from);
            path               = dir.substr(0, slash);
            std::error_code ec;
            if (!std::filesystem::is_directory(path, ec)) {
                std::string e;
                ftp_.mkdir(path, e);
            }
            if (slash == std::string::npos) {
                break;
            }
            from = slash + 1;
        }
        error.clear();
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            error = "cannot make " + dir;
            return false;
        }
        return true;
    }
    bool size(const std::string & path, uint64_t & out) override {
        if (local_size(path, out)) {
            return true;
        }
        std::string e;
        return ftp_.size(path, out, e);
    }
    bool put(const std::string & src, const std::string & dst, const std::atomic<bool> & stop, uint64_t & bytes,
             std::string & error) override {
        std::ifstream in(src, std::ios::binary);
        if (!in) {
            error = "cannot read " + src;
            return false;
        }
        const std::string part = dst + ".part";
        if (!ftp_.begin(part, 0, error)) {
            return false;
        }
        std::vector<char> buffer(256 * 1024);
        uint64_t          wrote = 0;
        while (in) {
            if (stop) {
                std::string ignored;
                ftp_.end(ignored);
                error = "stopped";
                return false;
            }
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize n = in.gcount();
            if (n > 0) {
                if (!ftp_.send(buffer.data(), static_cast<size_t>(n), error)) {
                    return false;
                }
                wrote += static_cast<uint64_t>(n);
            }
        }
        if (!ftp_.end(error)) {
            return false;
        }
        bytes += wrote;
        std::string rename_error;
        std::error_code ec;
        if (!ftp_.rename(part, dst, rename_error)) {
            std::filesystem::rename(part, dst, ec);
            if (ec) {
                error = "cannot rename " + part + ": " + rename_error;
                return false;
            }
        }
        return true;
    }

  private:
    FtpWriter ftp_;
};

enum class CopyResult { Copied, Skipped, Missing, Failed };

// One file of the gallery and its parameters to dest_dir (which ensure_dir
// makes). Counts the bytes written in bytes. Copied when the image was
// written, Skipped when it was there; Missing when outputs no longer has it
// (deleted meanwhile).
inline CopyResult copy_gallery_file(CopySink & sink, const std::string & outputs, const std::string & dest_dir,
                                    const std::string & name, const std::atomic<bool> & stop, uint64_t & bytes,
                                    std::string & error) {
    const std::string src = outputs + "/" + name;
    uint64_t          src_size = 0;
    if (!local_size(src, src_size)) {
        return CopyResult::Missing;
    }
    if (!sink.ensure_dir(dest_dir, error)) {
        return CopyResult::Failed;
    }
    // {file, source, destination}: the image, then its parameters if the gallery keeps any
    struct One {
        std::string src, dst;
    };
    std::vector<One> files = { { src, dest_dir + "/" + name } };
    const std::string params = outputs + "/.thumbs/" + name + ".txt";
    uint64_t          params_size = 0;
    bool              image_copied = false;
    if (local_size(params, params_size)) {
        const std::string thumbs = dest_dir + "/.thumbs";
        if (!sink.ensure_dir(thumbs, error)) {
            return CopyResult::Failed;
        }
        files.push_back({ params, thumbs + "/" + name + ".txt" });
    }
    for (size_t i = 0; i < files.size(); i++) {
        uint64_t have = 0, want = 0;
        local_size(files[i].src, want);
        const bool exists = sink.size(files[i].dst, have);
        if (copy_is_there(want, exists, have)) {
            continue;
        }
        if (!sink.put(files[i].src, files[i].dst, stop, bytes, error)) {
            if (error.empty()) {
                error = "could not copy " + name;
            }
            return CopyResult::Failed;
        }
        image_copied = image_copied || i == 0;
    }
    return image_copied ? CopyResult::Copied : CopyResult::Skipped;
}

// The worker, the state GET gallery/copy answers, and the queue of the
// background copies.
class GalleryCopier {
  public:
    using SinkFactory = std::function<std::unique_ptr<CopySink>(std::string & error)>;

    // mounted says whether a drive id has a drive (usb_mounted over mnt_root unless a test gives its own).
    using Mounted = std::function<bool(const std::string & id)>;

    GalleryCopier(std::string outputs, std::string mnt_root, SinkFactory factory, Mounted mounted = nullptr)
        : outputs_(std::move(outputs)), root_(std::move(mnt_root)), factory_(std::move(factory)),
          mounted_(mounted ? std::move(mounted) : Mounted([this](const std::string & id) { return usb_mounted(root_, id); })) {}
    ~GalleryCopier() { stop_and_join(); }

    // Starts a copy of names to the drive (the whole gallery when names is
    // empty). False when one runs already (busy set). The caller has checked
    // the drive id and that it is mounted.
    bool start(const std::string & to, std::vector<std::string> names, bool & busy) {
        if (names.empty()) {
            names = gallery_files(outputs_);
        }
        std::lock_guard<std::mutex> lock(m_);
        busy = manual_active_ || state_ == "copying";
        if (busy || stop_) {
            return false;
        }
        job_      = { to, std::move(names) };
        state_    = "copying";
        to_       = to;
        done_     = skipped_ = 0;
        total_    = job_.names.size();
        bytes_    = 0;
        error_.clear();
        manual_active_ = true;
        ensure_thread();
        cv_.notify_all();
        return true;
    }

    // A file the server saved, for the setting usb_copy. Never blocks.
    void enqueue(const std::string & to, const std::string & name) {
        std::lock_guard<std::mutex> lock(m_);
        if (stop_) {
            return;
        }
        auto_.push_back({ to, name });
        ensure_thread();
        cv_.notify_all();
    }

    std::string status_json() {
        std::lock_guard<std::mutex> lock(m_);
        return nlohmann::json{ { "state", state_ }, { "to", to_ },   { "done", done_ },   { "total", total_ },
                               { "skipped", skipped_ }, { "bytes", bytes_ }, { "error", error_ } }
            .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    // The app is ending: the worker stops after the chunk it writes. Does not wait.
    void request_stop() {
        stop_flag_ = true;
        std::lock_guard<std::mutex> lock(m_);
        stop_ = true;
        cv_.notify_all();
    }
    void stop_and_join() {
        request_stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

  private:
    struct Job {
        std::string              to;
        std::vector<std::string> names;
    };
    struct AutoItem {
        std::string to, name;
    };

    void ensure_thread() {  // m_ held
        if (!started_) {
            started_ = true;
            thread_  = std::thread([this] { run_guarded("gallery copier", [this] { loop(); }); });
        }
    }

    void loop() {
        std::unique_lock<std::mutex> lock(m_);
        while (!stop_) {
            if (manual_active_) {
                Job job = job_;
                lock.unlock();
                std::string thrown;
                if (!run_guarded("gallery copy", [&] { run_manual(job); }, &thrown)) {
                    finish("failed", thrown);
                }
                lock.lock();
            } else if (!auto_.empty()) {
                AutoItem item = auto_.front();
                auto_.pop_front();
                lock.unlock();
                run_guarded("usb_copy", [&] { run_auto(item); });
                lock.lock();
            } else {
                cv_.wait(lock, [this] { return stop_ || manual_active_ || !auto_.empty(); });
            }
        }
    }

    void finish(const char * state, const std::string & error) {
        std::lock_guard<std::mutex> lock(m_);
        state_         = state;
        error_         = error;
        manual_active_ = false;
    }

    void run_manual(const Job & job) {
        std::string error;
        auto        sink = factory_(error);
        if (!sink) {
            std::printf("ps5sd: gallery copy: %s\n", error.c_str());
            finish("failed", error);
            return;
        }
        const std::string dest = root_ + "/" + job.to + "/PS5SD";
        for (const std::string & name : job.names) {
            if (stop_flag_) {
                finish("failed", "the app is stopping");
                return;
            }
            if (!mounted_(job.to)) {
                error = "the drive " + job.to + " is gone";
                std::printf("ps5sd: gallery copy: %s\n", error.c_str());
                finish("failed", error);
                return;
            }
            uint64_t        bytes = 0;
            const CopyResult r     = copy_gallery_file(*sink, outputs_, dest, name, stop_flag_, bytes, error);
            if (r == CopyResult::Failed) {
                std::printf("ps5sd: gallery copy: %s\n", error.c_str());
                {
                    std::lock_guard<std::mutex> lock(m_);
                    bytes_ += bytes;
                }
                finish("failed", error);
                return;
            }
            std::lock_guard<std::mutex> lock(m_);
            done_++;
            bytes_ += bytes;
            if (r == CopyResult::Skipped || r == CopyResult::Missing) {
                skipped_++;
            }
        }
        std::printf("ps5sd: gallery copy: %zu file(s) to %s, %zu skipped\n", job.names.size(), job.to.c_str(),
                    static_cast<size_t>(skipped_));
        finish("done", "");
    }

    void run_auto(const AutoItem & item) {
        if (!mounted_(item.to)) {
            std::printf("ps5sd: usb_copy: no drive at %s/%s, %s not copied\n", root_.c_str(), item.to.c_str(),
                        item.name.c_str());
            return;
        }
        std::string error;
        auto        sink = factory_(error);
        if (!sink) {
            std::printf("ps5sd: usb_copy: %s\n", error.c_str());
            return;
        }
        uint64_t         bytes = 0;
        const CopyResult r     = copy_gallery_file(*sink, outputs_, root_ + "/" + item.to + "/PS5SD", item.name, stop_flag_,
                                                   bytes, error);
        if (r == CopyResult::Failed) {
            std::printf("ps5sd: usb_copy: %s: %s\n", item.name.c_str(), error.c_str());
        }
    }

    std::string                outputs_, root_;
    SinkFactory                factory_;
    Mounted                    mounted_;
    std::mutex                 m_;
    std::condition_variable    cv_;
    std::thread                thread_;
    bool                       started_       = false;
    bool                       stop_          = false;  // guarded by m_
    std::atomic<bool>          stop_flag_{ false };    // the same, for the chunk loops
    bool                       manual_active_ = false;
    Job                        job_;
    std::deque<AutoItem>       auto_;
    std::string                state_ = "idle", to_, error_;
    size_t                     done_ = 0, total_ = 0, skipped_ = 0;
    uint64_t                   bytes_ = 0;
};

}  // namespace ps5sd
