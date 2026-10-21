// Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
//
// The model downloader: a queue of files from Hugging Face and Civitai, kept
// in /data/PS5SD/downloads.json, fetched one file at a time on its own
// threads, independent of sd-server (which is stopped and started again for
// a model switch while a download goes on).
//
// Each file is fetched in 8 MiB pieces over 16 connections at once, as
// PS5LM's payload does: the console caps a socket's receive buffer at 64 KB,
// so one connection to a CDN about 150 ms away tops out near 0.9 MB/s. The
// pieces land in place in <dest>.part, and the queue records the ones on disk
// (after an fsync), so a cancel, a dropped connection or the app ending costs
// only the pieces in flight. A finished file is renamed to <dest>; a catalog
// entry whose files are all there gets its preset written.
//
// The route functions run on sd-server's HTTP threads and return the HTTP
// status with a JSON body; nothing throws out of them.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace ps5sd {

struct DownloaderConfig {
    std::string models_dir;          // /data/PS5SD/models
    std::string loras_dir;           // /data/PS5SD/loras (kind "loras")
    std::string upscalers_dir;       // /data/PS5SD/upscalers (kind "upscalers")
    std::string presets_dir;         // /data/PS5SD/presets
    std::string queue_file;          // /data/PS5SD/downloads.json
    std::string hf_token_file;       // /data/PS5SD/hf-token.txt (optional)
    std::string civitai_token_file;  // /data/PS5SD/civitai-token.txt (optional)
    int         connections = 16;
    unsigned    piece_mib   = 8;
    // For measuring the write path (env.txt PS5SD_DL_TRUNCATE=1,
    // PS5SD_DL_ORDERED=1, read in downloader_start): make the .part its full
    // size before the first piece, and write the pieces in file order only.
    bool        truncate = false;
    bool        ordered  = false;
    // Seconds between fsyncs of the .part while it downloads
    // (PS5SD_DL_SYNC_S; 0: only at the end, a cancel or the app ending).
    unsigned    sync_s   = 5;
    // Write the files through ftpsrv on 127.0.0.1:2121, else PS5SD's own copy
    // on 2122, started if need be (connect_ftp_writer, ftp_writer.hpp), in
    // file order; directly when neither answers (PS5SD_DL_FTP=0: always).
    bool        ftp      = true;
};

// Loads the queue and starts the worker; queued files carry on.
void downloader_start(const DownloaderConfig & config);

// The title is ending: stop the transfers and save the queue as it is on disk.
// Returns at once; the transfers' threads end on their own.
void downloader_stop();

// GET  catalog                 -> {"entries": [...]}
// GET  downloads               -> {"downloads": [...]}, oldest first
// POST downloads               {"url", "dest", "catalog"} or {"catalog"} alone
//                              -> 202 {"downloads": [...]} (200 and [] when nothing is missing)
// DELETE downloads/<id>        cancel, or remove a finished, failed or cancelled one
// POST downloads/<id>/retry    a failed or cancelled one again, from its .part
// POST downloads/clear         the finished ones leave the list
// POST hf/search               {"q", "limit"} -> {"results": [...]}
// Returns 0 when the path is not the downloader's.
int downloader_route(const std::string & method, const std::string & path, const std::string & body,
                     std::string & out);

}  // namespace ps5sd
