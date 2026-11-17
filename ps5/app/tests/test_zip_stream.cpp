// Host test for sd-server's zip_stream.cpp (patches/sdcpp/0086): the gallery's
// archive is a valid ZIP (plain, and with the ZIP64 records, which small limits
// reach with small files), a file that is gone is left out, and the archive
// comes through a real HTTP answer (chunked) as the same bytes.
//
// The archive is read back by a reader of this file (its own CRC-32 and its own
// parser of the central directory), and by python3 and unzip when they exist.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "zip_stream.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>

#include "httplib.h"

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// CRC-32 by the bit, so it shares nothing with the writer's.
uint32_t slow_crc(const std::string& data) {
    uint32_t crc = 0xffffffffu;
    for (unsigned char c : data) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
        }
    }
    return ~crc;
}

std::string content_of(size_t size, unsigned seed) {
    std::string s(size, '\0');
    unsigned x = seed * 2654435761u + 1;
    for (size_t i = 0; i < size; ++i) {
        x    = x * 1664525u + 1013904223u;
        s[i] = static_cast<char>(x >> 24);
    }
    return s;
}

void write_file(const fs::path& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::string whole(ZipStream& zip, size_t* pieces = nullptr) {
    std::string all;
    size_t n = 0;
    for (std::string piece; (piece.clear(), zip.next(piece));) {
        all += piece;
        ++n;
    }
    if (pieces != nullptr) {
        *pieces = n;
    }
    return all;
}

// ---- the reader

struct Reader {
    const std::string& z;
    explicit Reader(const std::string& bytes) : z(bytes) {}

    bool ok(uint64_t at, uint64_t n) const { return at + n <= z.size(); }
    uint64_t u(uint64_t at, int bytes) const {
        uint64_t v = 0;
        for (int i = bytes - 1; i >= 0; --i) {
            v = (v << 8) | static_cast<unsigned char>(z[at + i]);
        }
        return v;
    }
};

struct Found {
    std::string data;
    bool zip64 = false;
};

// Reads every entry through the central directory; false (with the reason in
// why) when anything is not as the format says.
bool read_archive(const std::string& z, std::map<std::string, Found>& out, bool& end64, std::string& why) {
    Reader r(z);
    if (z.size() < 22 || r.u(z.size() - 22, 4) != 0x06054b50) {
        why = "no end record";
        return false;
    }
    const uint64_t eocd = z.size() - 22;
    uint64_t entries = r.u(eocd + 10, 2), dir_size = r.u(eocd + 12, 4), dir_at = r.u(eocd + 16, 4);
    end64 = false;
    if (entries == 0xffff || dir_size == 0xffffffffull || dir_at == 0xffffffffull) {
        end64 = true;
        if (eocd < 20 || r.u(eocd - 20, 4) != 0x07064b50) {
            why = "no ZIP64 locator";
            return false;
        }
        const uint64_t at = r.u(eocd - 20 + 8, 8);
        if (!r.ok(at, 56) || r.u(at, 4) != 0x06064b50 || r.u(at + 4, 8) != 44) {
            why = "bad ZIP64 end record";
            return false;
        }
        if (at + 56 != eocd - 20) {
            why = "ZIP64 end record not before its locator";
            return false;
        }
        entries  = r.u(at + 32, 8);
        dir_size = r.u(at + 40, 8);
        dir_at   = r.u(at + 48, 8);
        if (at != dir_at + dir_size) {
            why = "ZIP64 end record not after the directory";
            return false;
        }
    } else if (eocd != dir_at + dir_size) {
        why = "end record not after the directory";
        return false;
    }

    uint64_t at         = dir_at;
    uint64_t last_data_end = 0;
    for (uint64_t i = 0; i < entries; ++i) {
        if (!r.ok(at, 46) || r.u(at, 4) != 0x02014b50) {
            why = "bad directory record";
            return false;
        }
        const uint64_t flags = r.u(at + 8, 2), method = r.u(at + 10, 2), crc = r.u(at + 16, 4);
        uint64_t csize = r.u(at + 20, 4), usize = r.u(at + 24, 4), offset = r.u(at + 42, 4);
        const uint64_t nlen = r.u(at + 28, 2), xlen = r.u(at + 30, 2), clen = r.u(at + 32, 2);
        if (!r.ok(at, 46 + nlen + xlen + clen) || (flags & 8) == 0 || method != 0) {
            why = "bad directory fields";
            return false;
        }
        const std::string name = z.substr(at + 46, nlen);
        bool zip64 = false;
        uint64_t x = at + 46 + nlen;
        const uint64_t x_end = x + xlen;
        while (x + 4 <= x_end) {
            const uint64_t id = r.u(x, 2), n = r.u(x + 2, 2);
            if (id == 1) {
                uint64_t p = x + 4;
                if (usize == 0xffffffffull) { usize = r.u(p, 8); p += 8; zip64 = true; }
                if (csize == 0xffffffffull) { csize = r.u(p, 8); p += 8; zip64 = true; }
                if (offset == 0xffffffffull) { offset = r.u(p, 8); p += 8; }
                if (p != x + 4 + n) {
                    why = "ZIP64 field of the wrong length in " + name;
                    return false;
                }
            }
            x += 4 + n;
        }
        if (csize != usize) {
            why = "stored entry with two sizes: " + name;
            return false;
        }
        // the local header and the data
        if (!r.ok(offset, 30) || r.u(offset, 4) != 0x04034b50 || (r.u(offset + 6, 2) & 8) == 0) {
            why = "bad local header of " + name;
            return false;
        }
        const uint64_t lnlen = r.u(offset + 26, 2), lxlen = r.u(offset + 28, 2);
        if (z.substr(offset + 30, lnlen) != name) {
            why = "local name differs: " + name;
            return false;
        }
        const bool local64 = lxlen == 20 && r.u(offset + 30 + lnlen, 2) == 1;
        if (local64 != zip64) {
            why = "local and central ZIP64 disagree: " + name;
            return false;
        }
        const uint64_t data = offset + 30 + lnlen + lxlen;
        if (!r.ok(data, usize)) {
            why = "data out of the archive: " + name;
            return false;
        }
        Found f;
        f.data  = z.substr(data, usize);
        f.zip64 = zip64;
        if (slow_crc(f.data) != crc) {
            why = "CRC differs: " + name;
            return false;
        }
        // the descriptor right behind the data
        uint64_t d = data + usize;
        const int w = zip64 ? 8 : 4;
        if (!r.ok(d, 16 + (zip64 ? 8 : 0)) || r.u(d, 4) != 0x08074b50 || r.u(d + 4, 4) != crc ||
            r.u(d + 8, w) != usize || r.u(d + 8 + w, w) != usize) {
            why = "bad data descriptor of " + name;
            return false;
        }
        last_data_end = d + 8 + 2 * w;
        if (out.count(name) != 0) {
            why = "name twice: " + name;
            return false;
        }
        out[name] = f;
        at        = x_end + clen;
    }
    if (at != dir_at + dir_size) {
        why = "directory has another size than its end record says";
        return false;
    }
    if (entries > 0 && last_data_end > dir_at) {
        why = "data past the directory";
        return false;
    }
    return true;
}

// python3 and unzip, when this machine has them
// (unzip does not take an archive without entries, which the route never makes)
void external_tests(const std::string& zip_path, const std::string& label, bool with_unzip = true) {
    const std::string quiet = " >/dev/null 2>&1";
    if (std::system(("command -v python3" + quiet).c_str()) == 0) {
        check(std::system(("python3 -m zipfile -t '" + zip_path + "'" + quiet).c_str()) == 0, label + ": python3 -m zipfile -t");
    } else {
        std::printf("note: no python3, %s not checked with it\n", label.c_str());
    }
    if (!with_unzip) {
        return;
    }
    if (std::system(("command -v unzip" + quiet).c_str()) == 0) {
        check(std::system(("unzip -tq '" + zip_path + "'" + quiet).c_str()) == 0, label + ": unzip -t");
    } else {
        std::printf("note: no unzip, %s not checked with it\n", label.c_str());
    }
}

}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("ps5sd_zip_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);

    std::map<std::string, std::string> files = {
        {"sd_20260101_000000_000.png", content_of(1000, 1)},
        {"sd_20260101_000000_001.webm", content_of(300 * 1024 + 17, 2)},
        {"sd_20260101_000000_002.png.txt", ""},
        {"sd_20260101_000000_003.png", content_of(200 * 1024, 3)},
        {"sd_20260101_000000_004.png", content_of(5, 4)},
    };
    std::vector<ZipSource> sources;
    for (const auto& kv : files) {
        write_file(dir / kv.first, kv.second);
        sources.push_back({kv.first, (dir / kv.first).string()});
    }
    // gone after the listing; and a name that is no file
    sources.insert(sources.begin() + 2, {"gone.png", (dir / "gone.png").string()});
    sources.push_back({"directory.png", dir.string() + "/missing/x.png"});

    // CRC-32 check value
    check(zip_crc32(0, "123456789", 9) == 0xcbf43926u, "CRC-32 of 123456789");
    check(zip_crc32(zip_crc32(0, "1234", 4), "56789", 5) == 0xcbf43926u, "CRC-32 in two parts");

    // ---- the plain archive
    {
        ZipStream zip(sources);
        size_t pieces = 0;
        const std::string z = whole(zip, &pieces);
        check(zip.skipped() == 2, "two files left out, got " + std::to_string(zip.skipped()));
        check(zip.entries() == files.size(), "entries");
        check(zip.written() == z.size(), "written() is the archive's size");
        check(pieces >= 3, "the archive comes in pieces");
        std::string piece;
        check(!zip.next(piece) && piece.empty(), "nothing after the end");

        std::map<std::string, Found> found;
        bool end64 = true;
        std::string why;
        check(read_archive(z, found, end64, why), "plain archive: " + why);
        check(!end64, "plain archive has no ZIP64 end");
        check(found.size() == files.size(), "plain archive: entries read");
        for (const auto& kv : files) {
            check(found.count(kv.first) == 1 && found[kv.first].data == kv.second, "plain archive: " + kv.first);
            check(found.count(kv.first) == 0 || !found[kv.first].zip64, "plain archive: not ZIP64: " + kv.first);
        }
        const fs::path path = dir / "plain.zip";
        write_file(path, z);
        external_tests(path.string(), "plain");
    }

    // ---- ZIP64: entries of 1000 bytes and more, offsets of 250000 and more, 3 entries or more
    ZipLimits small;
    small.size    = 1000;
    small.offset  = 250000;
    small.entries = 3;
    {
        ZipStream zip(sources, small);
        const std::string z = whole(zip);
        std::map<std::string, Found> found;
        bool end64 = false;
        std::string why;
        check(read_archive(z, found, end64, why), "ZIP64 archive: " + why);
        check(end64, "ZIP64 archive has the ZIP64 end");
        for (const auto& kv : files) {
            check(found.count(kv.first) == 1 && found[kv.first].data == kv.second, "ZIP64 archive: " + kv.first);
        }
        check(found["sd_20260101_000000_000.png"].zip64, "1000 bytes is ZIP64 at limit 1000");
        check(found["sd_20260101_000000_001.webm"].zip64, "300 KiB is ZIP64");
        check(!found["sd_20260101_000000_004.png"].zip64, "5 bytes is not");
        const fs::path path = dir / "zip64.zip";
        write_file(path, z);
        external_tests(path.string(), "ZIP64");
    }
    // only the end records (few entries) and only big offsets (no ZIP64 end)
    {
        ZipLimits end_only;
        end_only.entries = 2;
        ZipStream zip(sources, end_only);
        const std::string z = whole(zip);
        std::map<std::string, Found> found;
        bool end64 = false;
        std::string why;
        check(read_archive(z, found, end64, why) && end64 && found.size() == files.size(), "ZIP64 end alone: " + why);
        write_file(dir / "end.zip", z);
        external_tests((dir / "end.zip").string(), "ZIP64 end alone");

        ZipLimits offsets_only;
        offsets_only.offset = 100000;
        ZipStream zip2(sources, offsets_only);
        const std::string z2 = whole(zip2);
        found.clear();
        check(read_archive(z2, found, end64, why) && found.size() == files.size(), "big offsets alone: " + why);
        write_file(dir / "offsets.zip", z2);
        external_tests((dir / "offsets.zip").string(), "big offsets alone");
    }

    // ---- an empty archive is valid
    {
        ZipStream zip(std::vector<ZipSource>{});
        const std::string z = whole(zip);
        check(z.size() == 22, "empty archive is the end record");
        std::map<std::string, Found> found;
        bool end64 = false;
        std::string why;
        check(read_archive(z, found, end64, why) && found.empty(), "empty archive: " + why);
        write_file(dir / "empty.zip", z);
        external_tests((dir / "empty.zip").string(), "empty", false);
    }

    // ---- through HTTP: chunked, the same bytes, names from a query and a form
    {
        httplib::Server svr;
        svr.Get("/all", [&](const httplib::Request&, httplib::Response& res) {
            zip_respond(res, sources, "ps5sd-gallery-test.zip");
        });
        svr.Post("/some", [&](const httplib::Request& req, httplib::Response& res) {
            std::vector<ZipSource> chosen;
            for (const auto& name : zip_names_of(req)) {
                chosen.push_back({name, (dir / name).string()});
            }
            zip_respond(res, chosen, "ps5sd-gallery-test.zip");
        });
        svr.Get("/query", [&](const httplib::Request& req, httplib::Response& res) {
            std::string joined;
            for (const auto& name : zip_names_of(req)) {
                joined += name + "|";
            }
            res.set_content(joined, "text/plain");
        });
        const int port = svr.bind_to_any_port("127.0.0.1");
        check(port > 0, "test server bound");
        std::thread server([&] { svr.listen_after_bind(); });
        svr.wait_until_ready();

        httplib::Client cli("127.0.0.1", port);
        cli.set_read_timeout(30, 0);
        auto all = cli.Get("/all");
        check(all && all->status == 200, "GET /all answered");
        if (all && all->status == 200) {
            check(all->get_header_value("Content-Type") == "application/zip", "content type");
            check(all->get_header_value("Content-Disposition") == "attachment; filename=\"ps5sd-gallery-test.zip\"", "disposition");
            check(all->get_header_value("Transfer-Encoding") == "chunked", "chunked");
            ZipStream zip(sources);
            check(all->body == whole(zip), "the answer is the archive's bytes");
            std::map<std::string, Found> found;
            bool end64 = false;
            std::string why;
            check(read_archive(all->body, found, end64, why) && found.size() == files.size(), "answer reads back: " + why);
        }

        httplib::Params form;
        form.emplace("names", "sd_20260101_000000_003.png\r\nsd_20260101_000000_000.png, sd_20260101_000000_003.png");
        form.emplace("names", "sd_20260101_000000_004.png");
        auto some = cli.Post("/some", form);
        check(some && some->status == 200, "POST /some answered");
        if (some && some->status == 200) {
            std::map<std::string, Found> found;
            bool end64 = false;
            std::string why;
            check(read_archive(some->body, found, end64, why), "form answer reads back: " + why);
            check(found.size() == 3 && found.count("sd_20260101_000000_003.png") && found.count("sd_20260101_000000_000.png") &&
                      found.count("sd_20260101_000000_004.png"),
                  "the form's names, once each");
            check(some->body.find("sd_20260101_000000_003.png") < some->body.find("sd_20260101_000000_000.png"),
                  "in the order given");
        }
        auto q = cli.Get("/query?names=a.png,b.png&names=c.webm%20a.png");
        check(q && q->body == "a.png|b.png|c.webm|", "names of a query: " + (q ? q->body : std::string("no answer")));

        svr.stop();
        server.join();
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
    if (failures == 0) {
        std::printf("zip_stream: ok\n");
    }
    return failures == 0 ? 0 : 1;
}
