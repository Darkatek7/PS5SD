// Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0): the piece scheme,
// the retries and the record of finished pieces follow its payload's
// downloader (ps5/ps5lm/app.cpp), the queue its app's (ps5/app/market.cpp).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "downloader.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "download_core.hpp"
#include "ftp_writer.hpp"
#include "https_client.hpp"
#include "models.hpp"
#include "version.hpp"

namespace fs = std::filesystem;

namespace ps5sd {
namespace {

using clock_type = std::chrono::steady_clock;

struct Item {
    DownloadRecord    rec;
    std::atomic<bool> cancel{ false };
    bool              active = false;  // run_one() has it (guarded by g_mutex)
};

std::mutex                         g_mutex;  // g_items and every rec in them
std::condition_variable            g_cv;
std::vector<std::unique_ptr<Item>> g_items;
DownloaderConfig                   g_cfg;
std::atomic<bool>                  g_stop{ false };
bool                               g_started = false;

std::string models_path(const std::string & dest) {
    return g_cfg.models_dir + "/" + dest;
}

bool file_exists(const std::string & path) {
    std::error_code ec;
    return fs::is_regular_file(fs::u8path(path), ec) && !ec;
}

uint64_t file_size_of(const std::string & path) {
    std::error_code ec;
    const auto      n = fs::file_size(fs::u8path(path), ec);
    return ec ? 0 : static_cast<uint64_t>(n);
}

std::string read_token(const std::string & path) {
    if (path.empty()) {
        return {};
    }
    std::ifstream in(path);
    std::string   line;
    std::getline(in, line);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\n')) {
        line.pop_back();
    }
    return line;
}

// The headers of one hop: a token only for the host it belongs to, so it
// never reaches a CDN's signed URL (S3 and R2 refuse a second credential).
HttpHeaders headers_for(const Url & u, const std::string & range = {}) {
    HttpHeaders h = { { "User-Agent", "PS5SD/" PS5SD_VERSION } };
    std::string token;
    if (u.host == "huggingface.co") {
        token = read_token(g_cfg.hf_token_file);
    } else if (u.host == "civitai.com") {
        token = read_token(g_cfg.civitai_token_file);
    }
    if (!token.empty()) {
        h.emplace_back("Authorization", "Bearer " + token);
    }
    if (!range.empty()) {
        h.emplace_back("Range", range);
    }
    return h;
}

// downloads.json, written whole (a .tmp, fsync, rename). g_mutex held.
void save_queue_locked() {
    std::vector<DownloadRecord> list;
    for (const auto & it : g_items) {
        list.push_back(it->rec);
    }
    const std::string text = queue_to_json(list);
    const std::string tmp  = g_cfg.queue_file + ".tmp";
    const int         fd   = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        std::printf("ps5sd: download: cannot write %s: %s\n", tmp.c_str(), std::strerror(errno));
        return;
    }
    const bool ok = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    ::fsync(fd);
    ::close(fd);
    if (ok) {
        std::rename(tmp.c_str(), g_cfg.queue_file.c_str());
    }
}

Item * find_item_locked(const std::string & id) {
    for (auto & it : g_items) {
        if (it->rec.id == id) {
            return it.get();
        }
    }
    return nullptr;
}

// A catalog entry whose files are all there gets its preset (through a .tmp
// and a rename). Returns false when it could not be written.
bool write_preset_if_complete(const std::string & catalog_id) {
    const CatalogEntry * e = find_catalog(catalog_id);
    if (e == nullptr || !catalog_files_present(*e, g_cfg.models_dir)) {
        return true;
    }
    std::error_code ec;
    fs::create_directories(fs::u8path(g_cfg.presets_dir), ec);
    const std::string path = g_cfg.presets_dir + "/" + e->id + ".txt";
    const std::string tmp  = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << e->preset;
        if (!out) {
            std::printf("ps5sd: download: cannot write %s\n", tmp.c_str());
            return false;
        }
    }
    const bool ok = std::rename(tmp.c_str(), path.c_str()) == 0;
    std::printf("ps5sd: download: preset %s %s\n", e->id, ok ? "written" : "could not be written");
    return ok;
}

// ---- one file -----------------------------------------------------------------

struct Probe {
    std::string final_url;  // after the redirects (a CDN's signed URL)
    uint64_t    total  = 0;
    bool        ranges = false;
    int         status = 0;
};

std::string http_failure(const HttpResponse & r, const Url & first) {
    const std::string hf_message = r.header("x-error-message");
    if (r.status == 401 || r.status == 403) {
        if (first.host == "civitai.com") {
            return "Civitai refused it (HTTP " + std::to_string(r.status) +
                   "): this model needs an API token in /data/PS5SD/civitai-token.txt";
        }
        return "refused (HTTP " + std::to_string(r.status) + ")" + (hf_message.empty() ? "" : ": " + hf_message) +
               "; a gated or private repository needs a token in /data/PS5SD/hf-token.txt";
    }
    if (r.status == 404) {
        return "not found (HTTP 404)" + (hf_message.empty() ? "" : ": " + hf_message);
    }
    return "HTTP " + std::to_string(r.status) + (hf_message.empty() ? "" : ": " + hf_message);
}

// Where the bytes are and how many: a one-byte range request, following the
// redirects (Hugging Face and Civitai answer with a CDN's signed URL).
bool probe(HttpClient & client, const std::string & url, Probe & p, std::string & error) {
    Url first;
    parse_url(url, first);
    HttpResponse r;
    const bool   ok = http_get_following(
        client, url, [](const Url & u) { return headers_for(u, "bytes=0-0"); },
        [](const HttpResponse &) { return false; }, nullptr, r, p.final_url, error);
    if (!ok) {
        return false;
    }
    p.status = r.status;
    if (r.status == 206) {
        const int64_t total = content_range_total(r.header("content-range"));
        p.ranges            = total > 0;
        p.total             = total > 0 ? static_cast<uint64_t>(total) : 0;
        if (!p.ranges) {
            error = "the server did not say the file's size";
            return false;
        }
        return true;
    }
    if (r.status == 200) {
        p.ranges = false;
        p.total  = r.content_length > 0 ? static_cast<uint64_t>(r.content_length) : 0;
        return true;
    }
    error = http_failure(r, first);
    return false;
}

void set_failed(Item & item, const std::string & error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    item.rec.state            = "failed";
    item.rec.error            = error;
    item.rec.bytes_per_second = 0;
    save_queue_locked();
    std::printf("ps5sd: download %s: failed: %s\n", item.rec.dest.c_str(), error.c_str());
}

bool stopping(const Item & item) {
    return item.cancel || g_stop;
}

// ftpsrv's answer when the disk is full (552, or 451/452 with errno's text).
bool ftp_disk_full(const std::string & error) {
    return error.find(" 552 ") != std::string::npos || error.find(" 452 ") != std::string::npos ||
           error.find("No space") != std::string::npos;
}

// The file in one stream from its start, for a server without ranges. With
// ftp, through one long transfer to part; otherwise pwrite to fd.
void fetch_whole(Item & item, const Probe & p, int fd, FtpWriter * ftp, const std::string & part) {
    HttpClient   client;
    HttpResponse r;
    std::string  error, final_url;
    std::atomic<uint64_t> got{ 0 };
    bool         disk_full = false;
    if (ftp != nullptr && !ftp->begin(part, 0, error)) {
        set_failed(item, error);
        return;
    }
    const auto   t0        = clock_type::now();
    std::thread  monitor;
    std::atomic<bool> done{ false };
    monitor = std::thread([&] {
        while (!done) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            const double s = std::chrono::duration<double>(clock_type::now() - t0).count();
            std::lock_guard<std::mutex> lock(g_mutex);
            item.rec.bytes            = got;
            item.rec.bytes_per_second = s > 0 ? got / s : 0;
        }
    });
    const bool ok = http_get_following(
        client, p.final_url, [](const Url & u) { return headers_for(u); },
        [](const HttpResponse & res) { return res.status == 200; },
        [&](const char * data, size_t n) {
            if (ftp != nullptr) {
                if (!ftp->send(data, n, error)) {
                    disk_full = ftp_disk_full(error);
                    return false;
                }
            } else if (::pwrite(fd, data, n, static_cast<off_t>(got.load())) != static_cast<ssize_t>(n)) {
                disk_full = errno == ENOSPC;
                return false;
            }
            got += n;
            return !stopping(item);
        },
        r, final_url, error);
    done = true;
    monitor.join();
    if (ftp != nullptr) {
        std::string end_error;
        if (!ftp->end(end_error) && ok && !stopping(item)) {
            disk_full = ftp_disk_full(end_error);
            set_failed(item, disk_full ? "the console's storage is full" : end_error);
            return;
        }
    }
    if (stopping(item)) {
        return;
    }
    if (!ok || r.status != 200 || (p.total > 0 && got != p.total)) {
        set_failed(item, disk_full ? "the console's storage is full"
                         : !ok     ? error
                                   : "the transfer ended early (" + std::to_string(got.load()) + " bytes)");
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    item.rec.total = got;
    item.rec.bytes = got;
}

// Pieces over several connections, in place. Returns when they are all on
// disk, the item is cancelled or the app stops, or it failed (state set).
// With ftp, each piece goes to part through ftpsrv, in file order; otherwise
// pwrite to fd. note is shown with the record while it runs ("" for none).
void fetch_pieces(Item & item, const Probe & p0, const std::string & url, int fd, FtpWriter * ftp,
                  const std::string & part, const std::string & note) {
    std::string pieces;
    uint64_t    piece = 0, total = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        pieces = item.rec.pieces;
        piece  = item.rec.piece;
        total  = item.rec.total;
    }
    const size_t       count = pieces.size();
    std::vector<char>  st(pieces.begin(), pieces.end());  // '0' to fetch, '2' in flight, '1' done
    std::mutex         m;                                  // st, cdn, last_error
    std::string        cdn = p0.final_url;
    std::string        last_error;
    std::atomic<uint64_t> received{ pieces_bytes(pieces, piece, total) };
    std::atomic<bool>  fatal{ false }, disk_full{ false };
    std::atomic<int>   waiting{ 0 };
    std::atomic<uint64_t> write_us{ 0 };  // time spent in pwrite, all connections together
    uint64_t              fsync_us = 0;   // in the last fsync of the record
    std::mutex         refresh_mutex;
    // One piece is written at a time, whole: 256 KiB pwrites from 16
    // connections at scattered offsets brought /data down to 1-8 MB/s
    // (docs/CONSOLE.md), with every connection blocked in pwrite.
    std::mutex         write_mutex;
    // PS5SD_DL_ORDERED, and always through ftpsrv: a piece waits until the
    // ones before it are written, so the file only ever grows at its end.
    const bool              ordered = g_cfg.ordered || ftp != nullptr;
    std::condition_variable written;
    const auto before_done = [&](size_t k) {  // m held
        for (size_t i = 0; i < k; i++) {
            if (st[i] != '1') {
                return false;
            }
        }
        return true;
    };

    const auto worker = [&](int) {
        HttpClient        client;
        int               streak = 0;  // failures in a row on this connection
        std::vector<char> buffer;      // the piece, until it is written
        for (;;) {
            size_t      k = count;
            std::string url_now;
            {
                std::lock_guard<std::mutex> lock(m);
                for (size_t i = 0; i < count; i++) {
                    if (st[i] == '0') {
                        k     = i;
                        st[i] = '2';
                        break;
                    }
                }
                url_now = cdn;
            }
            if (k == count || stopping(item) || fatal || disk_full) {
                if (k != count) {
                    std::lock_guard<std::mutex> lock(m);
                    st[k] = '0';
                }
                return;
            }
            const uint64_t start = static_cast<uint64_t>(k) * piece;
            const uint64_t len   = std::min(piece, total - start);
            uint64_t       wrote = 0;  // bytes of the piece in the buffer
            Url            u;
            parse_url(url_now, u);
            HttpResponse   r;
            std::string    error;
            if (buffer.size() < len) {
                buffer.resize(len);
            }
            const bool ok = client.request(
                "GET", u, headers_for(u, "bytes=" + std::to_string(start) + "-" + std::to_string(start + len - 1)),
                [](const HttpResponse & res) { return res.status == 206; },
                [&](const char * data, size_t n) {
                    if (wrote + n > len) {
                        return false;
                    }
                    std::memcpy(buffer.data() + wrote, data, n);
                    wrote += n;
                    received += n;
                    return !stopping(item);
                },
                r, error);
            if (ok && r.status == 206 && wrote == len && ordered) {
                std::unique_lock<std::mutex> lock(m);
                while (!before_done(k) && !stopping(item) && !fatal && !disk_full) {
                    written.wait_for(lock, std::chrono::milliseconds(250));
                }
                if (!before_done(k)) {
                    received -= wrote;
                    st[k] = '0';
                    return;
                }
            }
            if (ok && r.status == 206 && wrote == len) {
                bool done;
                {
                    std::lock_guard<std::mutex> lock(write_mutex);
                    const auto w0 = clock_type::now();
                    if (ftp != nullptr) {
                        // Confirmed (226) means in the file. A failed write is
                        // cut off by the next one at this piece's start; one
                        // more try on a new connection (ftpsrv may have
                        // dropped an idle one).
                        done = ftp->write_at(part, start, buffer.data(), len, error);
                        if (!done && !ftp_disk_full(error)) {
                            std::printf("ps5sd: download %s: piece %zu: %s, writing it again\n",
                                        item.rec.dest.c_str(), k, error.c_str());
                            error.clear();
                            done = ftp->write_at(part, start, buffer.data(), len, error);
                        }
                    } else {
                        done = ::pwrite(fd, buffer.data(), len, static_cast<off_t>(start)) == static_cast<ssize_t>(len);
                    }
                    write_us += static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now() - w0).count());
                }
                if (done) {
                    streak = 0;
                    {
                        std::lock_guard<std::mutex> lock(m);
                        st[k] = '1';
                    }
                    written.notify_all();
                    continue;
                }
                if (ftp != nullptr) {
                    disk_full = ftp_disk_full(error);
                } else {
                    disk_full = disk_full || errno == ENOSPC;
                    error     = std::string("pwrite: ") + std::strerror(errno);
                }
            }
            received -= wrote;  // fetched again from its start
            {
                std::lock_guard<std::mutex> lock(m);
                st[k] = '0';
            }
            client.close();
            if (stopping(item) || disk_full) {
                return;
            }
            const std::string why = !error.empty() ? error : r.status != 206 ? http_failure(r, u)
                                                                  : "short piece (" + std::to_string(wrote) + " bytes)";
            {
                std::lock_guard<std::mutex> lock(m);
                last_error = why;
            }
            std::printf("ps5sd: download %s: piece %zu: %s\n", item.rec.dest.c_str(), k, why.c_str());
            // A 4xx other than an expired link (403, 410), 408 or 429 will not
            // get better by asking again.
            if (r.status >= 400 && r.status < 500 && r.status != 403 && r.status != 408 && r.status != 410 &&
                r.status != 429) {
                fatal = true;
                return;
            }
            if (++streak >= 8) {
                fatal = true;
                return;
            }
            if (r.status == 403 || r.status == 410 || (r.status >= 300 && r.status < 400)) {
                // The signed link expired: a fresh one, once for all connections.
                std::lock_guard<std::mutex> refresh(refresh_mutex);
                bool stale;
                {
                    std::lock_guard<std::mutex> lock(m);
                    stale = cdn == url_now;
                }
                if (stale) {
                    HttpClient  probe_client;
                    Probe       again;
                    std::string again_error;
                    if (probe(probe_client, url, again, again_error) && again.total == total) {
                        std::lock_guard<std::mutex> lock(m);
                        cdn = again.final_url;
                    }
                }
            }
            ++waiting;
            const int wait_ms = 1000 << std::min(streak - 1, 4);  // 1, 2, 4, 8, 16 s
            for (int ms = 0; ms < wait_ms && !stopping(item) && !fatal; ms += 250) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
            --waiting;
        }
    };

    size_t todo = static_cast<size_t>(std::count(st.begin(), st.end(), '0'));
    const int n_workers = static_cast<int>(std::min<size_t>(static_cast<size_t>(std::max(1, g_cfg.connections)), todo));
    std::vector<std::thread> threads;
    for (int i = 0; i < n_workers; i++) {
        threads.emplace_back(worker, i);
    }

    // Progress every second; every 5 s the pieces on disk (copied before the
    // fsync, so a piece is never recorded before its bytes are written).
    auto     window_t     = clock_type::now();
    uint64_t window_bytes = received;
    auto     saved        = window_t;
    auto     logged       = window_t;
    auto       synced = window_t;
    const auto record = [&](bool last) {
        std::string copy;
        {
            std::lock_guard<std::mutex> lock(m);
            copy.assign(st.begin(), st.end());
        }
        for (char & c : copy) {
            c = c == '1' ? '1' : '0';
        }
        const auto f0 = clock_type::now();
        if (fd >= 0 && (last || (g_cfg.sync_s > 0 && f0 - synced >= std::chrono::seconds(g_cfg.sync_s)))) {
            ::fsync(fd);
            synced   = clock_type::now();
            fsync_us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(synced - f0).count());
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        item.rec.pieces = copy;
        save_queue_locked();
    };
    for (;;) {
        bool running;
        {
            std::lock_guard<std::mutex> lock(m);
            running = std::any_of(st.begin(), st.end(), [](char c) { return c != '1'; });
        }
        running = running && !fatal && !disk_full && !stopping(item);
        const auto   now = clock_type::now();
        const double s   = std::chrono::duration<double>(now - window_t).count();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            item.rec.bytes = received;
            if (s >= 3.0) {
                item.rec.bytes_per_second = (received - window_bytes) / s;
                window_t                  = now;
                window_bytes              = received;
            }
            if (waiting > 0 && waiting >= n_workers) {
                std::lock_guard<std::mutex> lock2(m);
                item.rec.error = "connection lost (" + last_error + "), trying again";
            } else if (!item.cancel) {
                item.rec.error = note;
            }
        }
        if (!running) {
            break;
        }
        if (now - saved >= std::chrono::seconds(5)) {
            record(false);
            saved = now;
        }
        if (now - logged >= std::chrono::seconds(10)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            // Where the time goes: pwrite (all connections together, per
            // second of wall time) and the last fsync.
            std::printf("ps5sd: download %s: %.1f of %.1f MB, %.2f MB/s, pwrite %.2f s/s, fsync %.2f s, %d waiting\n",
                        item.rec.dest.c_str(), received / 1e6, total / 1e6, item.rec.bytes_per_second / 1e6,
                        write_us.exchange(0) / 1e6 / std::chrono::duration<double>(now - logged).count(),
                        fsync_us / 1e6, waiting.load());
            logged = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    for (auto & t : threads) {
        t.join();
    }
    record(true);
    if (stopping(item)) {
        return;
    }
    if (disk_full) {
        set_failed(item, "the console's storage is full");
    } else if (fatal) {
        set_failed(item, last_error.empty() ? "the transfer failed" : last_error);
    }
}

void run_one(Item & item) {
    DownloadRecord rec;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        rec = item.rec;
    }
    const auto  t0 = clock_type::now();
    std::string error;
    HttpClient  client;
    Probe       p;
    if (!probe(client, rec.url, p, error)) {
        if (!stopping(item)) {
            set_failed(item, error);
        }
        return;
    }
    client.close();
    const std::string dest = models_path(rec.dest);
    const std::string part = dest + ".part";
    std::error_code   ec;
    fs::create_directories(fs::u8path(dest).parent_path(), ec);
    const uint64_t piece = static_cast<uint64_t>(std::max(1u, g_cfg.piece_mib)) << 20;
    bool           fresh = false;  // nothing of the .part is kept
    // Through ftpsrv when it answers, or PS5SD's own copy, started here if
    // need be: the title's own writes slow to about 2 MB/s after 1.4 GB
    // (ftp_writer.hpp).
    FtpWriter   ftp;
    std::string ftp_error, note;
    const bool  via_ftp = g_cfg.ftp && connect_ftp_writer(ftp, ftp_error);
    if (g_cfg.ftp && !via_ftp) {
        note = "ftpsrv does not answer and did not start, so the app writes the file itself, which gets slow "
               "after about 1.4 GB (" + ftp_error + ")";
        std::printf("ps5sd: download %s: %s\n", rec.dest.c_str(), note.c_str());
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Item & it = item;
        // A different size or piece than the record says: not the same file
        // any more, start over. The .part grows as pieces land (no ftruncate
        // up front, see fetch_pieces), so a piece the map has must lie inside
        // it, and it can never be longer than the file.
        // Through ftpsrv the .part is written in order, and a piece is done
        // once ftpsrv confirmed it; a tail past the done pieces is cut off by
        // the first write.
        const size_t   count     = p.ranges ? static_cast<size_t>((p.total + piece - 1) / piece) : 0;
        const uint64_t part_size = file_size_of(part);
        const bool     same      = p.ranges && it.rec.total == p.total && it.rec.piece == piece &&
                                   it.rec.pieces.size() == count && part_size <= p.total;
        if (same && via_ftp) {
            it.rec.pieces = pieces_in_order(it.rec.pieces, piece, p.total, part_size);
        }
        if (!same || !pieces_within(it.rec.pieces, piece, p.total, part_size)) {
            it.rec.pieces.assign(count, '0');
            fresh = true;
        }
        it.rec.total = p.total;
        it.rec.piece = p.ranges ? piece : 0;
        it.rec.bytes = pieces_bytes(it.rec.pieces, it.rec.piece, it.rec.total);
        save_queue_locked();
        rec = it.rec;
    }
    const std::string how = via_ftp ? "through ftpsrv on " + std::to_string(ftp.port())
                                    : "fsync every " + std::to_string(g_cfg.sync_s) + " s";
    std::printf("ps5sd: download %s: %llu bytes from %s (%s; %d x %u MiB%s%s, %s; %llu bytes kept)\n",
                rec.dest.c_str(), (unsigned long long) p.total, rec.url.c_str(), p.ranges ? "in pieces" : "one stream",
                g_cfg.connections, g_cfg.piece_mib, g_cfg.truncate && !via_ftp ? ", truncate" : "",
                g_cfg.ordered || via_ftp ? ", ordered" : "", how.c_str(), (unsigned long long) rec.bytes);
    if (via_ftp) {
        if (p.ranges) {
            fetch_pieces(item, p, rec.url, -1, &ftp, part, note);
        } else {
            fetch_whole(item, p, -1, &ftp, part);
        }
    } else {
        const int fd = ::open(part.c_str(), O_RDWR | O_CREAT, 0666);
        if (fd < 0) {
            set_failed(item, "cannot write " + part + ": " + std::strerror(errno));
            return;
        }
        if (fresh && ::ftruncate(fd, 0) != 0) {
            ::close(fd);
            set_failed(item, "cannot empty " + part + ": " + std::strerror(errno));
            return;
        }
        if (p.ranges && g_cfg.truncate && file_size_of(part) != p.total &&
            ::ftruncate(fd, static_cast<off_t>(p.total)) != 0) {
            ::close(fd);
            set_failed(item, "cannot make " + part + " " + std::to_string(p.total) + " bytes: " + std::strerror(errno));
            return;
        }
        if (p.ranges) {
            fetch_pieces(item, p, rec.url, fd, nullptr, part, note);
        } else {
            fetch_whole(item, p, fd, nullptr, part);
        }
        ::fsync(fd);
        ::close(fd);
    }

    std::unique_lock<std::mutex> lock(g_mutex);
    if (g_stop) {
        item.rec.bytes_per_second = 0;
        save_queue_locked();  // "running": queued again on the next start
        return;
    }
    if (item.cancel) {
        item.rec.state            = "cancelled";
        item.rec.bytes_per_second = 0;
        item.rec.bytes            = pieces_bytes(item.rec.pieces, item.rec.piece, item.rec.total);
        save_queue_locked();
        std::printf("ps5sd: download %s: cancelled at %llu bytes\n", item.rec.dest.c_str(),
                    (unsigned long long) item.rec.bytes);
        return;
    }
    if (item.rec.state == "failed") {
        return;
    }
    if (p.ranges && item.rec.pieces.find('0') != std::string::npos) {
        item.rec.state = "failed";
        item.rec.error = "some pieces are missing; download it again to fetch the rest";
        save_queue_locked();
        return;
    }
    // ftpsrv made the file, so ftpsrv renames it; the title's own rename if
    // that fails.
    std::string rename_error;
    if (!(via_ftp && ftp.rename(part, dest, rename_error)) && std::rename(part.c_str(), dest.c_str()) != 0) {
        item.rec.state = "failed";
        item.rec.error = "cannot rename " + part + ": " + std::strerror(errno);
        save_queue_locked();
        return;
    }
    const double seconds = std::chrono::duration<double>(clock_type::now() - t0).count();
    item.rec.state            = "done";
    item.rec.error.clear();
    item.rec.bytes            = item.rec.total;
    item.rec.bytes_per_second = 0;
    item.rec.pieces.clear();
    const std::string catalog_id = item.rec.catalog;
    save_queue_locked();
    std::printf("ps5sd: download %s: done, %llu bytes in %.1f s (%.2f MB/s over this run)\n", item.rec.dest.c_str(),
                (unsigned long long) item.rec.total, seconds, seconds > 0 ? item.rec.total / 1e6 / seconds : 0.0);
    lock.unlock();
    if (!catalog_id.empty()) {
        write_preset_if_complete(catalog_id);
    }
}

void manager() {
    for (;;) {
        Item * item = nullptr;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_cv.wait(lock, [] {
                return g_stop || std::any_of(g_items.begin(), g_items.end(),
                                             [](const std::unique_ptr<Item> & i) { return i->rec.state == "queued"; });
            });
            if (g_stop) {
                return;
            }
            for (auto & i : g_items) {
                if (i->rec.state == "queued") {
                    item = i.get();
                    break;
                }
            }
            item->rec.state = "running";
            item->rec.error.clear();
            item->cancel = false;
            item->active = true;
            save_queue_locked();
        }
        try {
            run_one(*item);
        } catch (const std::exception & e) {
            set_failed(*item, std::string("error: ") + e.what());
        } catch (...) {
            set_failed(*item, "unknown error");
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        item->active = false;
        if (item->rec.state == "running" && !g_stop) {
            item->rec.state = "failed";  // should not happen; never left running
            save_queue_locked();
        }
    }
}

// ---- routes ------------------------------------------------------------------------

int answer(std::string & out, int status, const std::string & body) {
    out = body;
    return status;
}

// A new download, or one taken up again. g_mutex held. Returns the HTTP status
// for this file alone (202, or an error with out set).
int enqueue_locked(const std::string & url, const std::string & dest, const std::string & catalog_id,
                   std::vector<DownloadRecord> & added, std::string & out) {
    const std::string id   = download_id(dest);
    Item *            item = find_item_locked(id);
    if (item != nullptr) {
        if (item->active) {
            if (item->rec.state == "cancelled") {
                return answer(out, 409, error_json(dest + " is still stopping; try again in a moment"));
            }
            return answer(out, 409, error_json(dest + " is downloading already"));
        }
        if (item->rec.state == "queued") {
            return answer(out, 409, error_json(dest + " is queued already"));
        }
        if (item->rec.state == "done" && file_exists(models_path(dest))) {
            return answer(out, 409, error_json(dest + " is downloaded already"));
        }
        // Cancelled or failed (or done and gone since): again, from what
        // .part holds (run_one checks it still fits).
        item->rec.url     = url;
        item->rec.catalog = catalog_id.empty() ? item->rec.catalog : catalog_id;
        item->rec.state   = "queued";
        item->rec.error.clear();
        item->cancel = false;
        added.push_back(item->rec);
        return 202;
    }
    if (file_exists(models_path(dest))) {
        return answer(out, 409, error_json(dest + " is in the models directory already"));
    }
    auto fresh          = std::make_unique<Item>();
    fresh->rec.id       = id;
    fresh->rec.url      = url;
    fresh->rec.dest     = dest;
    fresh->rec.catalog  = catalog_id;
    fresh->rec.state    = "queued";
    added.push_back(fresh->rec);
    g_items.push_back(std::move(fresh));
    return 202;
}

int post_catalog(const CatalogEntry & e, std::string & out) {
    std::vector<DownloadRecord> added;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // Every file first, so nothing is queued when one of them is in the way.
        for (const CatalogFile & f : e.files) {
            const std::string path = models_path(f.dest);
            if (file_exists(path) && !catalog_file_present(f, g_cfg.models_dir)) {
                return answer(out, 409, error_json("a different file is at " + std::string(f.dest) + " (" +
                                                   std::to_string(file_size_of(path)) + " bytes, the catalog's has " +
                                                   std::to_string(f.bytes) + "); move it away first"));
            }
        }
        for (const CatalogFile & f : e.files) {
            const std::string path = models_path(f.dest);
            if (file_exists(path)) {
                continue;  // there at its size (checked above); shared files (a VAE) are fetched once
            }
            Item * item = find_item_locked(download_id(f.dest));
            if (item != nullptr && (item->active || item->rec.state == "queued")) {
                if (item->rec.state == "cancelled") {
                    return answer(out, 409, error_json(std::string(f.dest) + " is still stopping; try again in a moment"));
                }
                added.push_back(item->rec);  // on its way already
                continue;
            }
            std::string err;
            const int   status = enqueue_locked(f.url, f.dest, e.id, added, err);
            if (status != 202) {
                return answer(out, status, err);
            }
        }
        if (!added.empty()) {
            save_queue_locked();
        }
    }
    g_cv.notify_all();
    if (added.empty()) {
        // Everything is there: the preset is all that is missing.
        if (!write_preset_if_complete(e.id)) {
            return answer(out, 500, error_json("could not write the preset " + std::string(e.id)));
        }
        return answer(out, 200, downloads_json({}));
    }
    std::printf("ps5sd: download: catalog %s, %zu file(s) queued\n", e.id, added.size());
    return answer(out, 202, downloads_json(added));
}

// Civitai: the file's name from its API (/api/v1/model-versions/<v>).
bool civitai_name(const Source & src, std::string & name, std::string & error) {
    HttpClient  client;
    std::string body, url, ignored_url;
    int         status = 0;
    uint64_t    bytes  = 0;
    if (!http_get_text(client, "https://civitai.com/api/v1/model-versions/" + src.civitai_version,
                       [](const Url & u) { return headers_for(u); }, 4u << 20, body, status, error)) {
        return false;
    }
    if (status == 200 && parse_civitai_version(body, src.civitai_file, name, ignored_url, bytes, error)) {
        return true;
    }
    // Without the API: the name the download itself gives (Content-Disposition).
    std::string  api_error = status != 200 ? "Civitai's API answered HTTP " + std::to_string(status) : error;
    HttpResponse r;
    std::string  final_url;
    if (!http_get_following(
            client, src.url, [](const Url & u) { return headers_for(u, "bytes=0-0"); },
            [](const HttpResponse &) { return false; }, nullptr, r, final_url, error)) {
        error = api_error + "; " + error;
        return false;
    }
    name = content_disposition_name(r.header("content-disposition"));
    if (r.status != 200 && r.status != 206) {
        Url first;
        parse_url(src.url, first);
        error = api_error + "; the download: " + http_failure(r, first);
        return false;
    }
    if (name.empty()) {
        error = api_error + "; the download gives no file name: pass a \"dest\"";
        return false;
    }
    return true;
}

int post_download(const std::string & body, std::string & out) {
    DownloadRequest req;
    std::string     error;
    if (!parse_download_request(body, req, error)) {
        return answer(out, 400, error_json(error));
    }
    if (req.url.empty()) {
        const CatalogEntry * e = find_catalog(req.catalog);
        if (e == nullptr) {
            return answer(out, 404, error_json("no catalog entry " + req.catalog));
        }
        return post_catalog(*e, out);
    }
    if (!req.catalog.empty() && find_catalog(req.catalog) == nullptr) {
        return answer(out, 404, error_json("no catalog entry " + req.catalog));
    }
    Source src;
    if (!classify_url(req.url, src, error)) {
        return answer(out, 400, error_json(error));
    }
    std::string dest = req.dest;
    if (dest.empty()) {
        if (src.kind == "civitai") {
            if (!civitai_name(src, dest, error)) {
                return answer(out, 502, error_json(error));
            }
        } else {
            dest = src.file_name;
        }
    }
    if (!valid_dest(dest, error)) {
        return answer(out, 400, error_json(error));
    }
    std::vector<DownloadRecord> added;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const int status = enqueue_locked(src.url, dest, req.catalog, added, out);
        if (status != 202) {
            return status;
        }
        save_queue_locked();
    }
    g_cv.notify_all();
    std::printf("ps5sd: download: %s queued from %s\n", dest.c_str(), src.url.c_str());
    return answer(out, 202, downloads_json(added));
}

int delete_download(const std::string & id, std::string & out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Item * item = find_item_locked(id);
    if (item == nullptr) {
        return answer(out, 404, error_json("no download " + id));
    }
    DownloadRecord & r = item->rec;
    if (r.state == "queued" || r.state == "running") {
        // Cancel: the transfer stops, .part and its record stay for a resume
        // (POST the same URL or catalog entry again).
        item->cancel = true;
        r.state      = "cancelled";
        r.bytes_per_second = 0;
        save_queue_locked();
        std::printf("ps5sd: download %s: cancel asked for\n", r.dest.c_str());
        return answer(out, 200, downloads_json({ r }));
    }
    if (item->active) {
        return answer(out, 409, error_json(r.dest + " is still stopping; try again in a moment"));
    }
    // Done, failed or cancelled: the entry goes; a failed or cancelled one's
    // partial file with it. A finished file stays in the models directory.
    if (r.state != "done") {
        ::unlink(models_path(r.dest + ".part").c_str());
    }
    const DownloadRecord gone = r;
    g_items.erase(std::remove_if(g_items.begin(), g_items.end(),
                                 [&](const std::unique_ptr<Item> & i) { return i.get() == item; }),
                  g_items.end());
    save_queue_locked();
    std::printf("ps5sd: download %s: removed\n", gone.dest.c_str());
    return answer(out, 200, downloads_json({}));
}

// POST downloads/<id>/retry: a failed or cancelled one again, from what its
// .part holds.
int retry_download(const std::string & id, std::string & out) {
    std::vector<DownloadRecord> added;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Item * item = find_item_locked(id);
        if (item == nullptr) {
            return answer(out, 404, error_json("no download " + id));
        }
        if (item->rec.state != "failed" && item->rec.state != "cancelled") {
            return answer(out, 409, error_json(item->rec.dest + " is " + item->rec.state + ", not failed or cancelled"));
        }
        const DownloadRecord rec = item->rec;
        const int status = enqueue_locked(rec.url, rec.dest, rec.catalog, added, out);
        if (status != 202) {
            return status;
        }
        save_queue_locked();
    }
    g_cv.notify_all();
    return answer(out, 202, downloads_json(added));
}

// POST downloads/clear: the finished ones leave the list (their files stay).
int clear_done(std::string & out) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_items.erase(std::remove_if(g_items.begin(), g_items.end(),
                                 [](const std::unique_ptr<Item> & i) { return !i->active && i->rec.state == "done"; }),
                  g_items.end());
    save_queue_locked();
    std::vector<DownloadRecord> list;
    for (const auto & i : g_items) {
        list.push_back(i->rec);
    }
    return answer(out, 200, downloads_json(list));
}

int hf_search(const std::string & body, std::string & out) {
    SearchRequest req;
    std::string   error;
    if (!parse_search_request(body, req, error)) {
        return answer(out, 400, error_json(error));
    }
    // A few more than asked: repositories without a model file drop out.
    const int ask = std::min(100, req.limit * 2);
    const std::string url = "https://huggingface.co/api/models?sort=downloads&direction=-1&limit=" +
                            std::to_string(ask) + "&expand%5B%5D=siblings&expand%5B%5D=downloads&expand%5B%5D=likes" +
                            (req.q.empty() ? "" : "&search=" + url_encode(req.q));
    HttpClient  client;
    std::string text;
    int         status = 0;
    if (!http_get_text(client, url, [](const Url & u) { return headers_for(u); }, 16u << 20, text, status, error)) {
        return answer(out, 502, error_json("Hugging Face: " + error));
    }
    if (status != 200) {
        return answer(out, 502, error_json("Hugging Face answered HTTP " + std::to_string(status)));
    }
    std::vector<SearchResult> results = parse_hf_search(text);
    if (results.size() > static_cast<size_t>(req.limit)) {
        results.resize(static_cast<size_t>(req.limit));
    }
    // The sizes: one request per repository, on four connections.
    std::atomic<size_t>      next{ 0 };
    std::vector<std::thread> threads;
    for (int t = 0; t < 4 && static_cast<size_t>(t) < results.size(); t++) {
        threads.emplace_back([&] {
            HttpClient c;
            for (size_t i; (i = next++) < results.size();) {
                std::string info, e;
                int         st = 0;
                if (http_get_text(c, "https://huggingface.co/api/models/" + results[i].repo + "?blobs=true",
                                  [](const Url & u) { return headers_for(u); }, 16u << 20, info, st, e) &&
                    st == 200) {
                    parse_hf_model_files(info, results[i]);
                }
            }
        });
    }
    for (auto & t : threads) {
        t.join();
    }
    return answer(out, 200, search_json(results));
}

// One disk write test, "dir:MB:sync_mb:prealloc:pause_s:max_s": MB of zeros in
// 8 MiB write()s, one after the other, to <dir>/.ps5sd-writetest; an fsync
// every sync_mb MB (0: only at the end); prealloc 1 makes the file its full
// size first (ftruncate); it stops after max_s seconds (0: when it is all
// written), then pauses pause_s seconds. The rate is logged every 2 s, and
// the file is removed at the end. A dir of "ftp/data/..." writes through
// ftpsrv instead (connect_ftp_writer), one 8 MiB STOR after the other (fsync
// and prealloc unused).
void write_test(const std::string & spec) {
    std::vector<std::string> f;
    size_t                   at = 0;
    for (;;) {
        const size_t c = spec.find(':', at);
        f.push_back(spec.substr(at, c == std::string::npos ? std::string::npos : c - at));
        if (c == std::string::npos) {
            break;
        }
        at = c + 1;
    }
    if (f.empty() || f[0].empty()) {
        return;
    }
    const auto num = [&](size_t i, long dflt) { return i < f.size() && !f[i].empty() ? std::atol(f[i].c_str()) : dflt; };
    const uint64_t    total    = static_cast<uint64_t>(std::max(1L, num(1, 1000))) * 1000000;
    const uint64_t    sync_mb  = static_cast<uint64_t>(std::max(0L, num(2, 0)));
    const bool        prealloc = num(3, 0) != 0;
    const long        pause_s  = std::max(0L, num(4, 0));
    const long        max_s    = std::max(0L, num(5, 0));  // 0: no limit
    const bool        via_ftp  = f[0].rfind("ftp/", 0) == 0;
    const std::string path     = (via_ftp ? f[0].substr(3) : f[0]) + "/.ps5sd-writetest";
    std::printf("ps5sd: write test %s: %.0f MB, %s\n", path.c_str(), total / 1e6,
                via_ftp  ? "through ftpsrv"
                : prealloc ? ("fsync every " + std::to_string(sync_mb) + " MB, preallocated").c_str()
                           : ("fsync every " + std::to_string(sync_mb) + " MB, growing").c_str());
    FtpWriter   ftp;
    std::string ftp_error;
    const int   fd = via_ftp ? -1 : ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (via_ftp && !connect_ftp_writer(ftp, ftp_error)) {
        std::printf("ps5sd: write test: %s\n", ftp_error.c_str());
        return;
    }
    if (!via_ftp && fd < 0) {
        std::printf("ps5sd: write test %s: %s\n", path.c_str(), std::strerror(errno));
        return;
    }
    if (!via_ftp && prealloc && ::ftruncate(fd, static_cast<off_t>(total)) != 0) {
        std::printf("ps5sd: write test: ftruncate: %s\n", std::strerror(errno));
    }
    std::vector<char> block(8u << 20, 0);
    uint64_t          done = 0, window = 0, since_sync = 0;
    double            slowest = 0, sync_s = 0;
    const auto        t0 = clock_type::now();
    auto              tw = t0;
    while (done < total && !g_stop &&
           (max_s == 0 || clock_type::now() - t0 < std::chrono::seconds(max_s))) {
        const size_t n  = static_cast<size_t>(std::min<uint64_t>(block.size(), total - done));
        const auto   w0 = clock_type::now();
        if (via_ftp) {
            if (!ftp.write_at(path, done, block.data(), n, ftp_error)) {
                std::printf("ps5sd: write test: %s\n", ftp_error.c_str());
                break;
            }
        } else if (::write(fd, block.data(), n) != static_cast<ssize_t>(n)) {
            std::printf("ps5sd: write test: write: %s\n", std::strerror(errno));
            break;
        }
        done += n;
        since_sync += n;
        if (!via_ftp && sync_mb > 0 && since_sync >= sync_mb * 1000000) {
            const auto f0 = clock_type::now();
            ::fsync(fd);
            sync_s += std::chrono::duration<double>(clock_type::now() - f0).count();
            since_sync = 0;
        }
        const auto now = clock_type::now();
        slowest        = std::max(slowest, std::chrono::duration<double>(now - w0).count());
        const double s = std::chrono::duration<double>(now - tw).count();
        if (s >= 2.0) {
            std::printf("ps5sd: write test: %.0f MB, %.1f MB/s, slowest write %.2f s, fsync %.2f s\n", done / 1e6,
                        (done - window) / 1e6 / s, slowest, sync_s);
            tw      = now;
            window  = done;
            slowest = 0;
            sync_s  = 0;
        }
    }
    const double before = std::chrono::duration<double>(clock_type::now() - t0).count();
    const auto   f0     = clock_type::now();
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
    const double fs_s = std::chrono::duration<double>(clock_type::now() - f0).count();
    const double all = std::chrono::duration<double>(clock_type::now() - t0).count();
    std::printf("ps5sd: write test %s: %.0f MB in %.1f s (%.1f MB/s; %.1f MB/s before the last fsync of %.1f s)\n",
                path.c_str(), done / 1e6, all, all > 0 ? done / 1e6 / all : 0.0,
                before > 0 ? done / 1e6 / before : 0.0, fs_s);
    // The title's own unlink, also of a file ftpsrv made (a cancelled
    // download's .part, a deleted model).
    if (::unlink(path.c_str()) != 0) {
        std::printf("ps5sd: write test: unlink %s: %s\n", path.c_str(), std::strerror(errno));
    }
    for (long i = 0; i < pause_s * 4 && !g_stop; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

}  // namespace

void downloader_start(const DownloaderConfig & config) {
    std::vector<DownloadRecord> loaded;
    {
        std::ifstream      in(config.queue_file);
        std::ostringstream text;
        text << in.rdbuf();
        loaded = queue_from_json(text.str());
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_started) {
            return;
        }
        g_started = true;
        g_cfg     = config;
        if (const char * v = std::getenv("PS5SD_DL_TRUNCATE"); v != nullptr) {
            g_cfg.truncate = std::atoi(v) != 0;
        }
        if (const char * v = std::getenv("PS5SD_DL_ORDERED"); v != nullptr) {
            g_cfg.ordered = std::atoi(v) != 0;
        }
        if (const char * v = std::getenv("PS5SD_DL_FTP"); v != nullptr) {
            g_cfg.ftp = std::atoi(v) != 0;
        }
        if (const char * v = std::getenv("PS5SD_DL_SYNC_S"); v != nullptr) {
            g_cfg.sync_s = static_cast<unsigned>(std::max(0, std::atoi(v)));
        }
        for (const auto & r : loaded) {
            auto item = std::make_unique<Item>();
            item->rec = r;
            g_items.push_back(std::move(item));
        }
        std::error_code ec;
        fs::create_directories(fs::u8path(g_cfg.models_dir), ec);
    }
    size_t queued = 0;
    for (const auto & r : loaded) {
        queued += r.state == "queued";
    }
    std::printf("ps5sd: download: %zu in the queue, %zu to fetch\n", loaded.size(), queued);
    // For measuring (env.txt), PS5SD_DL_WRITETEST: disk write tests without
    // the network, run one after the other before the queue starts. Each is
    // dir:MB:sync_mb:prealloc:pause_s:max_s, separated by ';' (see write_test).
    if (const char * v = std::getenv("PS5SD_DL_WRITETEST"); v != nullptr && *v != '\0') {
        const std::string spec = v;
        std::thread([spec] {
            size_t at = 0;
            while (at <= spec.size() && !g_stop) {
                size_t end = spec.find(';', at);
                if (end == std::string::npos) {
                    end = spec.size();
                }
                write_test(spec.substr(at, end - at));
                at = end + 1;
            }
            std::printf("ps5sd: write tests done\n");
            manager();
        }).detach();
        return;
    }
    std::thread(manager).detach();
}

void downloader_stop() {
    g_stop = true;
    g_cv.notify_all();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_started) {
        save_queue_locked();
    }
}

int downloader_route(const std::string & method, const std::string & path, const std::string & body,
                     std::string & out) {
    try {
        if (method == "GET" && path == "catalog") {
            return answer(out, 200, catalog_json(g_cfg.models_dir, g_cfg.presets_dir));
        }
        if (method == "GET" && path == "downloads") {
            std::lock_guard<std::mutex> lock(g_mutex);
            std::vector<DownloadRecord> list;
            for (const auto & i : g_items) {
                list.push_back(i->rec);
            }
            return answer(out, 200, downloads_json(list));
        }
        if (method == "POST" && path == "downloads") {
            return post_download(body, out);
        }
        if (method == "POST" && path == "downloads/clear") {
            return clear_done(out);
        }
        if (method == "POST" && path.rfind("downloads/", 0) == 0 && path.size() > 16 &&
            path.compare(path.size() - 6, 6, "/retry") == 0) {
            return retry_download(path.substr(10, path.size() - 16), out);
        }
        if (method == "DELETE" && path.rfind("downloads/", 0) == 0) {
            return delete_download(path.substr(10), out);
        }
        if (method == "POST" && path == "hf/search") {
            return hf_search(body, out);
        }
    } catch (const std::exception & e) {
        return answer(out, 500, error_json(std::string("error: ") + e.what()));
    } catch (...) {
        return answer(out, 500, error_json("unknown error"));
    }
    return 0;
}

}  // namespace ps5sd
