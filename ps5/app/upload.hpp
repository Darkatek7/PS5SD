// Uploads from the web UI: PUT /ps5sd/v1/app/uploads/<kind>/<name> with the
// file as the body (patches/sdcpp/0057 streams it here). kind is models,
// loras, embeddings, presets or upscalers (upload_target in
// download_core.hpp). The file goes to <name>.part through ftpsrv
// (ftp_writer.hpp), or with the title's own writes when ftpsrv does not
// answer, and is renamed once all of it is there. A file that is already there is not replaced.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ps5sd {

struct UploadDirs {
    std::string models, loras, embeddings, presets, upscalers, tags;
};

// Passes the body's pieces to sink in order; true once all of it went
// through, false when the connection failed or sink returned false.
using UploadReader = std::function<bool(const std::function<bool(const char *, size_t)> & sink)>;

// path is <kind>/<name>, length the Content-Length (0: not given). Returns
// the HTTP status and the JSON answer in out.
int upload_file(const UploadDirs & dirs, const std::string & path, uint64_t length, const UploadReader & read,
                std::string & out);

}  // namespace ps5sd
