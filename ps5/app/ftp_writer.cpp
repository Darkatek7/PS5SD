// FtpWriter (ftp_writer.hpp): a minimal FTP client for the console's ftpsrv.
#include "ftp_writer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

namespace {

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

}  // namespace

int FtpWriter::dial(int port, std::string & error) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return -1;
    }
    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    timeval tv{};
    tv.tv_sec = timeout_;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host_.c_str(), &sa.sin_addr) != 1) {
        error = "bad address " + host_;
        ::close(fd);
        return -1;
    }
    if (::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0) {
        error = "connect to " + host_ + ":" + std::to_string(port) + ": " + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    return fd;
}

bool FtpWriter::send_all(int fd, const char * data, size_t n, std::string & error) {
    while (n > 0) {
        const ssize_t k = ::send(fd, data, n, kSendFlags);
        if (k < 0 && errno == EINTR) {
            continue;
        }
        if (k <= 0) {
            error = std::string("send: ") + (k < 0 ? std::strerror(errno) : "connection closed");
            return false;
        }
        data += k;
        n -= static_cast<size_t>(k);
    }
    return true;
}

// One reply: "ddd text", or "ddd-" lines up to the "ddd " that ends them.
bool FtpWriter::reply(int & code, std::string & text, std::string & error) {
    std::string first;
    for (;;) {
        size_t eol;
        while ((eol = inbox_.find('\n')) == std::string::npos) {
            char          buf[512];
            const ssize_t k = ::recv(ctrl_, buf, sizeof(buf), 0);
            if (k < 0 && errno == EINTR) {
                continue;
            }
            if (k <= 0) {
                error = std::string("ftpsrv: ") + (k < 0 ? std::strerror(errno) : "connection closed");
                return false;
            }
            inbox_.append(buf, static_cast<size_t>(k));
        }
        std::string line = inbox_.substr(0, eol);
        inbox_.erase(0, eol + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (first.empty()) {
            if (line.size() < 3) {
                error = "ftpsrv: bad reply '" + line + "'";
                return false;
            }
            first = line;
        }
        // The last line of a reply repeats the code with a space after it.
        if (line.size() >= 4 && line.compare(0, 3, first, 0, 3) == 0 && line[3] == ' ') {
            code = std::atoi(first.substr(0, 3).c_str());
            text = line;
            return true;
        }
        if (line.size() == 3 && line == first.substr(0, 3)) {
            code = std::atoi(line.c_str());
            text = line;
            return true;
        }
    }
}

bool FtpWriter::command(const std::string & line, int & code, std::string & text, std::string & error) {
    if (!send_all(ctrl_, (line + "\r\n").data(), line.size() + 2, error)) {
        error = "ftpsrv: " + error;
        return false;
    }
    return reply(code, text, error);
}

void FtpWriter::close() {
    if (data_ >= 0) {
        ::close(data_);
        data_ = -1;
    }
    if (ctrl_ >= 0) {
        ::close(ctrl_);
        ctrl_ = -1;
    }
    inbox_.clear();
}

bool FtpWriter::connect(std::string & error) {
    if (ctrl_ >= 0) {
        return true;
    }
    ctrl_    = dial(port_, error);
    refused_ = ctrl_ < 0;
    if (ctrl_ < 0) {
        return false;
    }
    int         code = 0;
    std::string text;
    if (!reply(code, text, error) || code != 220) {
        if (error.empty()) {
            error = "ftpsrv: " + text;
        }
        close();
        return false;
    }
    if (!command("USER anonymous", code, text, error)) {
        close();
        return false;
    }
    if (code == 331 && !command("PASS ps5sd", code, text, error)) {
        close();
        return false;
    }
    if (code != 230) {
        error = "ftpsrv: " + text;
        close();
        return false;
    }
    if (!command("TYPE I", code, text, error) || code != 200) {
        if (error.empty()) {
            error = "ftpsrv: " + text;
        }
        close();
        return false;
    }
    return true;
}

// EPSV: "229 Entering Extended Passive Mode (|||port|)"; or, for a server
// that does not know it (500, 502, 504), PASV: "227 Entering Passive Mode
// (h1,h2,h3,h4,p1,p2)". The address is the server's own, so only the port is
// taken.
bool FtpWriter::open_data(std::string & error) {
    int         code = 0;
    std::string text;
    if (!pasv_) {
        if (!command("EPSV", code, text, error)) {
            return false;
        }
        const size_t a = text.find("(|||");
        const size_t b = a == std::string::npos ? a : text.find('|', a + 4);
        if (code == 229 && b != std::string::npos) {
            data_ = dial(std::atoi(text.substr(a + 4, b - a - 4).c_str()), error);
            return data_ >= 0;
        }
        if (code != 500 && code != 502 && code != 504) {
            error = "ftpsrv: " + text;
            return false;
        }
        pasv_ = true;
    }
    if (!command("PASV", code, text, error)) {
        return false;
    }
    const size_t a = text.find('(');
    const size_t b = a == std::string::npos ? a : text.find(')', a);
    int          h[6];
    if (code != 227 || b == std::string::npos ||
        std::sscanf(text.c_str() + a + 1, "%d,%d,%d,%d,%d,%d", &h[0], &h[1], &h[2], &h[3], &h[4], &h[5]) != 6) {
        error = "ftpsrv: " + text;
        return false;
    }
    data_ = dial(h[4] * 256 + h[5], error);
    return data_ >= 0;
}

bool FtpWriter::begin(const std::string & path, uint64_t offset, std::string & error) {
    if (!connect(error)) {
        return false;
    }
    int         code = 0;
    std::string text;
    const auto  fail = [&] {
        if (error.empty()) {
            error = "ftpsrv: " + text;
        }
        close();  // the control connection may be out of step now
        return false;
    };
    if (!command("REST " + std::to_string(offset), code, text, error) || code != 350) {
        return fail();
    }
    if (!open_data(error)) {
        return fail();
    }
    if (!command("STOR " + path, code, text, error) || (code != 150 && code != 125)) {
        return fail();
    }
    return true;
}

bool FtpWriter::send(const char * data, size_t n, std::string & error) {
    if (data_ < 0) {
        error = "ftpsrv: no transfer";
        return false;
    }
    if (!send_all(data_, data, n, error)) {
        error = "ftpsrv: " + error;
        close();
        return false;
    }
    return true;
}

bool FtpWriter::end(std::string & error) {
    if (data_ < 0) {
        error = "ftpsrv: no transfer";
        return false;
    }
    ::close(data_);
    data_ = -1;
    int         code = 0;
    std::string text;
    if (!reply(code, text, error) || code != 226) {
        if (error.empty()) {
            error = "ftpsrv: " + text;
        }
        close();
        return false;
    }
    return true;
}

bool FtpWriter::write_at(const std::string & path, uint64_t offset, const char * data, size_t n,
                         std::string & error) {
    return begin(path, offset, error) && send(data, n, error) && end(error);
}

bool FtpWriter::rename(const std::string & from, const std::string & to, std::string & error) {
    if (!connect(error)) {
        return false;
    }
    int         code = 0;
    std::string text;
    // RNTO: 250, or 226 from ftpsrv up to 0.15
    if (!command("RNFR " + from, code, text, error) || code != 350 || !command("RNTO " + to, code, text, error) ||
        code / 100 != 2) {
        if (error.empty()) {
            error = "ftpsrv: " + text;
        }
        return false;
    }
    return true;
}

bool FtpWriter::size(const std::string & path, uint64_t & out, std::string & error) {
    if (!connect(error)) {
        return false;
    }
    int         code = 0;
    std::string text;
    if (!command("SIZE " + path, code, text, error)) {
        close();
        return false;
    }
    if (code != 213 || text.size() < 5) {
        error = "ftpsrv: " + text;
        return false;
    }
    out = std::strtoull(text.c_str() + 4, nullptr, 10);
    return true;
}

namespace {

// Hands the ELF to the loader, which runs what it read once the connection ends.
bool send_own_ftpsrv(std::string & error) {
    std::ifstream     in(kOwnFtpsrvElf, std::ios::binary);
    std::vector<char> elf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (elf.empty()) {
        error = std::string("cannot read ") + kOwnFtpsrvElf;
        return false;
    }
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    sockaddr_in sa{};
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(kElfLoaderPort);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0) {
        error = "no ELF loader on port " + std::to_string(kElfLoaderPort) + ": " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    const char * p = elf.data();
    size_t       n = elf.size();
    while (n > 0) {
        const ssize_t k = ::send(fd, p, n, kSendFlags);
        if (k < 0 && errno == EINTR) {
            continue;
        }
        if (k <= 0) {
            error = std::string("sending to the ELF loader: ") + (k < 0 ? std::strerror(errno) : "connection closed");
            ::close(fd);
            return false;
        }
        p += k;
        n -= static_cast<size_t>(k);
    }
    ::close(fd);
    return true;
}

// One byte through the user's ftpsrv, once per run: a server without a command
// PS5SD needs (an old ftpsrv answers 502) fails here instead of in every piece
// of a download.
bool user_ftpsrv_works(FtpWriter & ftp, std::string & error) {
    static std::mutex m;
    static int        works = -1;  // not checked yet
    std::lock_guard<std::mutex> lock(m);
    if (works < 0) {
        std::string check_error;
        works = ftp.write_at(kFtpCheckFile, 0, "x", 1, check_error) ? 1 : 0;
        ::unlink(kFtpCheckFile);
        if (works) {
            std::printf("ps5sd: ftpsrv: the one on %d works\n", kFtpsrvPort);
        } else {
            error = check_error;
            std::printf("ps5sd: ftpsrv: the one on %d cannot write for PS5SD (%s); using PS5SD's own copy on %d\n",
                        kFtpsrvPort, check_error.c_str(), kOwnFtpsrvPort);
        }
    }
    if (!works && error.empty()) {
        error = "it failed a test write";
    }
    return works == 1;
}

}  // namespace

bool connect_ftp_writer(FtpWriter & ftp, std::string & error) {
    static std::mutex m;
    static bool       sent = false;
    std::string       error_2121, error_2122;
    ftp.set_port(kFtpsrvPort);
    if (ftp.connect(error_2121)) {
        if (user_ftpsrv_works(ftp, error_2121) && ftp.connect(error_2121)) {
            return true;
        }
        ftp.close();
    }
    ftp.set_port(kOwnFtpsrvPort);
    if (ftp.connect(error_2122)) {
        return true;
    }
    const auto none = [&](const std::string & why) {
        error = "no ftpsrv on 2121 (" + error_2121 + ") or 2122 (" + why + ")";
        return false;
    };
    std::lock_guard<std::mutex> lock(m);
    if (sent) {
        // Sent by this run already, maybe by another thread a moment ago.
        return ftp.connect(error_2122) || none(error_2122);
    }
    if (!ftp.refused()) {
        // Something listens on 2122 and does not answer: a copy that is busy,
        // not one to start again on top of it.
        return none(error_2122);
    }
    sent = true;
    std::string send_error;
    if (!send_own_ftpsrv(send_error)) {
        std::printf("ps5sd: ftpsrv: starting PS5SD's own copy: %s\n", send_error.c_str());
        return none(error_2122 + "; not started: " + send_error);
    }
    std::printf("ps5sd: ftpsrv: sent %s to the ELF loader on %d\n", kOwnFtpsrvElf, kElfLoaderPort);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 25; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (ftp.connect(error_2122)) {
            std::printf("ps5sd: ftpsrv: PS5SD's own copy answers on %d after %.1f s\n", kOwnFtpsrvPort,
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            return true;
        }
    }
    std::printf("ps5sd: ftpsrv: PS5SD's own copy does not answer on %d: %s\n", kOwnFtpsrvPort, error_2122.c_str());
    return none(error_2122 + "; sent to the ELF loader, but it did not start within 5 s");
}
