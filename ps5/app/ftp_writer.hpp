// Writes files through the console's FTP server (ftpsrv, a payload, on
// 127.0.0.1:2121) instead of the title's own write(). The console throttles a
// title's writes to /data: after about 1.4 GB they drop to about 2 MB/s, while
// ftpsrv keeps writing at 80 MB/s and more in the same moment (docs/CONSOLE.md).
//
// ftpsrv's REST n + STOR cuts the file at n and writes from there; a REST past
// the end is refused (551). So a file is written from its start, in order: one
// STOR per piece, at the end of what is already there.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

class FtpWriter {
public:
    explicit FtpWriter(std::string host = "127.0.0.1", int port = 2121) : host_(std::move(host)), port_(port) {}
    ~FtpWriter() { close(); }
    FtpWriter(const FtpWriter &)             = delete;
    FtpWriter & operator=(const FtpWriter &) = delete;

    // The control connection: connects and logs in, unless it already is.
    bool connect(std::string & error);
    // Writes n bytes to path at offset, which must not lie past the file's end;
    // the file is cut to offset + n. True once the server confirmed (226), so
    // the bytes are in the file.
    bool write_at(const std::string & path, uint64_t offset, const char * data, size_t n, std::string & error);
    // One long transfer, for a file in one stream: begin at offset, send, end.
    // end() is true once the server confirmed the whole transfer.
    bool begin(const std::string & path, uint64_t offset, std::string & error);
    bool send(const char * data, size_t n, std::string & error);
    bool end(std::string & error);
    bool rename(const std::string & from, const std::string & to, std::string & error);
    // The file's size; false if it is not there (550) or the server is gone.
    bool size(const std::string & path, uint64_t & out, std::string & error);
    void close();

    void set_timeout(int seconds) { timeout_ = seconds; }

private:
    bool command(const std::string & line, int & code, std::string & reply, std::string & error);
    bool reply(int & code, std::string & text, std::string & error);
    bool open_data(std::string & error);
    int  dial(int port, std::string & error);
    bool send_all(int fd, const char * data, size_t n, std::string & error);

    std::string host_;
    int         port_;
    int         timeout_ = 30;
    int         ctrl_    = -1;
    int         data_    = -1;
    std::string inbox_;  // control bytes read past the last reply line
};
