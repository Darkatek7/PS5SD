// Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "https_client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

// The Mozilla CA list (ps5/app/cacert.pem, embedded by scripts/build-app.sh).
// Without the header (host tests), the file named by PS5SD_CA_FILE is read.
#if __has_include("cacert_pem.h")
#include "cacert_pem.h"
#define PS5SD_HAVE_EMBEDDED_CA 1
#else
#include <fstream>
#include <sstream>
#endif

#ifdef __PROSPERO__
extern "C" {
int sceNetInit(void);
int sceNetPoolCreate(const char * name, int size, int flags);
int sceNetResolverCreate(const char * name, int memid, int flags);
int sceNetResolverStartNtoa(int rid, const char * hostname, in_addr_t * addr, int timeout, int retry, int flags);
int sceNetResolverDestroy(int rid);
}
#else
#include <netdb.h>
#endif

namespace ps5sd {
namespace {

constexpr size_t kMaxHeaderBytes = 64 * 1024;
constexpr size_t kReadBuffer     = 256 * 1024;  // bytes handed to on_body at once, at most

std::mutex               g_init_mutex;
bool                     g_init_done = false;
std::string              g_init_error;
mbedtls_entropy_context  g_entropy;
mbedtls_ctr_drbg_context g_drbg;
mbedtls_x509_crt         g_ca;
mbedtls_ssl_config       g_conf;
#ifdef __PROSPERO__
int g_net_pool = -1;
#endif

std::string tls_error(const char * what, int rc) {
    char text[160];
    mbedtls_strerror(rc, text, sizeof(text));
    char out[256];
    std::snprintf(out, sizeof(out), "%s: -0x%04x %s", what, (unsigned) -rc, text);
    return out;
}

std::string lower(std::string s) {
    for (char & c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string trim(const std::string & s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

// ---- names ----------------------------------------------------------------------

std::mutex                                                                   g_dns_mutex;
std::map<std::string, std::pair<in_addr_t, std::chrono::steady_clock::time_point>> g_dns;

bool resolve(const std::string & host, in_addr_t & addr, std::string & error) {
    in_addr numeric{};
    if (inet_pton(AF_INET, host.c_str(), &numeric) == 1) {
        addr = numeric.s_addr;
        return true;
    }
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(g_dns_mutex);
        const auto it = g_dns.find(host);
        if (it != g_dns.end() && now - it->second.second < std::chrono::minutes(5)) {
            addr = it->second.first;
            return true;
        }
    }
#ifdef __PROSPERO__
    const int rid = sceNetResolverCreate("ps5sd-dl", g_net_pool, 0);
    if (rid < 0) {
        char text[96];
        std::snprintf(text, sizeof(text), "sceNetResolverCreate 0x%08x", (unsigned) rid);
        error = text;
        return false;
    }
    in_addr_t  a  = 0;
    const int  rc = sceNetResolverStartNtoa(rid, host.c_str(), &a, 0, 0, 0);
    sceNetResolverDestroy(rid);
    if (rc < 0 || a == 0) {
        char text[160];
        std::snprintf(text, sizeof(text), "cannot resolve %s (sceNetResolverStartNtoa 0x%08x)", host.c_str(),
                      (unsigned) rc);
        error = text;
        return false;
    }
    addr = a;
#else
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo * res    = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr) {
        error = "cannot resolve " + host;
        return false;
    }
    addr = reinterpret_cast<sockaddr_in *>(res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
#endif
    std::lock_guard<std::mutex> lock(g_dns_mutex);
    g_dns[host] = { addr, now };
    return true;
}

void forget(const std::string & host) {
    std::lock_guard<std::mutex> lock(g_dns_mutex);
    g_dns.erase(host);
}

// ---- mbedTLS on a socket ----------------------------------------------------------

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

int bio_send(void * ctx, const unsigned char * buf, size_t len) {
    const int fd = *static_cast<int *>(ctx);
    for (;;) {
        const ssize_t n = ::send(fd, buf, len, kSendFlags);
        if (n >= 0) {
            return static_cast<int>(n);
        }
        if (errno == EINTR) {
            continue;
        }
        return errno == EPIPE || errno == ECONNRESET ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_SEND_FAILED;
    }
}

int bio_recv(void * ctx, unsigned char * buf, size_t len) {
    const int fd = *static_cast<int *>(ctx);
    for (;;) {
        const ssize_t n = ::recv(fd, buf, len, 0);
        if (n >= 0) {
            return static_cast<int>(n);
        }
        if (errno == EINTR) {
            continue;
        }
        return errno == ECONNRESET ? MBEDTLS_ERR_NET_CONN_RESET : MBEDTLS_ERR_NET_RECV_FAILED;
    }
}

}  // namespace

std::string HttpResponse::header(const std::string & name) const {
    for (const auto & h : headers) {
        if (h.first == name) {
            return h.second;
        }
    }
    return {};
}

bool https_init(std::string & error) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_init_done) {
        error = g_init_error;
        return g_init_error.empty();
    }
    g_init_done = true;
    int rc      = 0;
#ifdef __PROSPERO__
    // As PS5LM's title does before its HTTP library; a second sceNetInit in
    // the process (sd-server's sockets do not call it) is harmless.
    sceNetInit();
    g_net_pool = sceNetPoolCreate("ps5sd-dl", 64 * 1024, 0);
    if (g_net_pool < 0) {
        char text[96];
        std::snprintf(text, sizeof(text), "sceNetPoolCreate 0x%08x", (unsigned) g_net_pool);
        g_init_error = text;
        error        = g_init_error;
        return false;
    }
#endif
    if (const psa_status_t st = psa_crypto_init(); st != PSA_SUCCESS) {
        g_init_error = "psa_crypto_init " + std::to_string(st);
        error        = g_init_error;
        return false;
    }
    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_drbg);
    mbedtls_x509_crt_init(&g_ca);
    mbedtls_ssl_config_init(&g_conf);
    static const char kPers[] = "ps5sd-downloader";
    if ((rc = mbedtls_ctr_drbg_seed(&g_drbg, mbedtls_entropy_func, &g_entropy,
                                    reinterpret_cast<const unsigned char *>(kPers), sizeof(kPers) - 1)) != 0) {
        g_init_error = tls_error("mbedtls_ctr_drbg_seed", rc);
        error        = g_init_error;
        return false;
    }
#ifdef PS5SD_HAVE_EMBEDDED_CA
    rc = mbedtls_x509_crt_parse(&g_ca, ps5sd_cacert_pem, ps5sd_cacert_pem_len + 1);
#else
    std::string pem;
    {
        const char * path = std::getenv("PS5SD_CA_FILE");
        std::ifstream in(path != nullptr ? path : "/etc/ssl/certs/ca-certificates.crt");
        std::ostringstream text;
        text << in.rdbuf();
        pem = text.str();
    }
    rc = mbedtls_x509_crt_parse(&g_ca, reinterpret_cast<const unsigned char *>(pem.c_str()), pem.size() + 1);
#endif
    // A positive result counts certificates that did not parse; the rest are kept.
    if (rc < 0 || g_ca.version == 0) {
        g_init_error = rc < 0 ? tls_error("the CA list", rc) : "the CA list is empty";
        error        = g_init_error;
        return false;
    }
    if ((rc = mbedtls_ssl_config_defaults(&g_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
        g_init_error = tls_error("mbedtls_ssl_config_defaults", rc);
        error        = g_init_error;
        return false;
    }
    mbedtls_ssl_conf_authmode(&g_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&g_conf, &g_ca, nullptr);
    mbedtls_ssl_conf_rng(&g_conf, mbedtls_ctr_drbg_random, &g_drbg);
    return true;
}

// ---- one connection ----------------------------------------------------------------

struct HttpClient::Conn {
    int                 fd = -1;
    bool                tls = false;
    bool                tls_ready = false;
    mbedtls_ssl_context ssl;
    std::string         host;
    int                 port = 0;
    std::string         in;  // received, not consumed
    size_t              at = 0;
    std::vector<char>   chunk = std::vector<char>(kReadBuffer);

    Conn() { mbedtls_ssl_init(&ssl); }
    ~Conn() {
        if (tls_ready) {
            mbedtls_ssl_close_notify(&ssl);
        }
        mbedtls_ssl_free(&ssl);
        if (fd >= 0) {
            ::close(fd);
        }
    }

    // Some bytes from the connection: >0, 0 at its end, <0 on an error (msg set).
    int raw_read(char * dst, size_t max, std::string & msg) {
        if (!tls) {
            const ssize_t n = ::recv(fd, dst, max, 0);
            if (n < 0) {
                msg = std::string("recv: ") + std::strerror(errno);
                return -1;
            }
            return static_cast<int>(n);
        }
        for (;;) {
            const int n = mbedtls_ssl_read(&ssl, reinterpret_cast<unsigned char *>(dst), max);
            if (n >= 0) {
                return n;
            }
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                continue;
            }
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
            if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) {
                continue;
            }
#endif
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                return 0;
            }
            msg = tls_error("read", n);
            return -1;
        }
    }

    bool write_all(const std::string & data, std::string & msg) {
        size_t off = 0;
        while (off < data.size()) {
            int n;
            if (tls) {
                n = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char *>(data.data() + off), data.size() - off);
                if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    continue;
                }
                if (n < 0) {
                    msg = tls_error("write", n);
                    return false;
                }
            } else {
                n = static_cast<int>(::send(fd, data.data() + off, data.size() - off, kSendFlags));
                if (n < 0) {
                    msg = std::string("send: ") + std::strerror(errno);
                    return false;
                }
            }
            off += static_cast<size_t>(n);
        }
        return true;
    }

    // Up to max bytes, the buffered ones first.
    int read_some(char * dst, size_t max, std::string & msg) {
        if (at < in.size()) {
            const size_t n = std::min(max, in.size() - at);
            std::memcpy(dst, in.data() + at, n);
            at += n;
            if (at == in.size()) {
                in.clear();
                at = 0;
            }
            return static_cast<int>(n);
        }
        return raw_read(dst, max, msg);
    }

    // One line (without CRLF) from the buffer, reading more as needed.
    bool read_line(std::string & line, std::string & msg) {
        for (;;) {
            const size_t eol = in.find("\r\n", at);
            if (eol != std::string::npos) {
                line = in.substr(at, eol - at);
                at   = eol + 2;
                return true;
            }
            if (in.size() - at > kMaxHeaderBytes) {
                msg = "a line too long in the response";
                return false;
            }
            char      buf[16384];
            const int n = raw_read(buf, sizeof(buf), msg);
            if (n <= 0) {
                if (n == 0) {
                    msg = "the connection closed";
                }
                return false;
            }
            in.erase(0, at);
            at = 0;
            in.append(buf, static_cast<size_t>(n));
        }
    }
};

HttpClient::HttpClient() = default;
HttpClient::~HttpClient() = default;

void HttpClient::close() {
    conn_.reset();
}

bool HttpClient::request(const std::string & method, const Url & url, const HttpHeaders & headers,
                         const std::function<bool(const HttpResponse &)> & on_response,
                         const std::function<bool(const char *, size_t)> & on_body, HttpResponse & out,
                         std::string & error) {
    out = HttpResponse{};
    const bool tls = url.scheme == "https";
    if (tls && !https_init(error)) {
        return false;
    }
    std::string req = method + " " + url.target + " HTTP/1.1\r\nHost: " + url.host;
    if ((tls && url.port != 443) || (!tls && url.port != 80)) {
        req += ":" + std::to_string(url.port);
    }
    req += "\r\n";
    bool has_agent = false;
    for (const auto & h : headers) {
        req += h.first + ": " + h.second + "\r\n";
        has_agent = has_agent || lower(h.first) == "user-agent";
    }
    if (!has_agent) {
        req += "User-Agent: PS5SD\r\n";
    }
    req += "Accept: */*\r\nConnection: keep-alive\r\n\r\n";

    // A kept connection may have been closed by the server meanwhile: one
    // more try on a new connection when nothing came back on it.
    for (int attempt = 0; attempt < 2; attempt++) {
        const bool reused = conn_ && conn_->host == url.host && conn_->port == url.port && conn_->tls == tls;
        if (!reused) {
            conn_.reset(new Conn());
            Conn &    c = *conn_;
            in_addr_t addr = 0;
            if (!resolve(url.host, addr, error)) {
                conn_.reset();
                return false;
            }
            c.host = url.host;
            c.port = url.port;
            c.tls  = tls;
            c.fd   = ::socket(AF_INET, SOCK_STREAM, 0);
            if (c.fd < 0) {
                error = std::string("socket: ") + std::strerror(errno);
                conn_.reset();
                return false;
            }
            // The console caps the receive buffer at 64 KB whatever is asked
            // (PS5LM); ask for more anyway.
            const int rcvbuf = 4 << 20;
            setsockopt(c.fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
            const int one = 1;
            setsockopt(c.fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
            setsockopt(c.fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
            timeval tv{};
            tv.tv_sec = timeout_;
            setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(c.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            sockaddr_in sa{};
            sa.sin_family      = AF_INET;
            sa.sin_port        = htons(static_cast<uint16_t>(url.port));
            sa.sin_addr.s_addr = addr;
            if (::connect(c.fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0) {
                error = "connect to " + url.host + ": " + std::strerror(errno);
                forget(url.host);
                conn_.reset();
                return false;
            }
            if (tls) {
                int rc = mbedtls_ssl_setup(&c.ssl, &g_conf);
                if (rc == 0) {
                    rc = mbedtls_ssl_set_hostname(&c.ssl, url.host.c_str());
                }
                if (rc != 0) {
                    error = tls_error("mbedtls_ssl_setup", rc);
                    conn_.reset();
                    return false;
                }
                mbedtls_ssl_set_bio(&c.ssl, &c.fd, bio_send, bio_recv, nullptr);
                while ((rc = mbedtls_ssl_handshake(&c.ssl)) != 0) {
                    if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) {
                        error = tls_error(("TLS handshake with " + url.host).c_str(), rc);
                        if (rc == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
                            const uint32_t flags = mbedtls_ssl_get_verify_result(&c.ssl);
                            char           info[256];
                            mbedtls_x509_crt_verify_info(info, sizeof(info), "", flags);
                            error += std::string(" (") + trim(info) + "; is the console's date right?)";
                        }
                        conn_.reset();
                        return false;
                    }
                }
                c.tls_ready = true;
            }
        }
        Conn & c = *conn_;
        std::string status_line;
        if (!c.write_all(req, error) || !c.read_line(status_line, error)) {
            conn_.reset();
            if (reused) {
                continue;
            }
            return false;
        }
        // HTTP/1.1 206 Partial Content
        const size_t sp = status_line.find(' ');
        if (status_line.compare(0, 5, "HTTP/") != 0 || sp == std::string::npos) {
            error = "not an HTTP response: " + status_line.substr(0, 60);
            conn_.reset();
            return false;
        }
        out.status = std::atoi(status_line.c_str() + sp + 1);
        bool keep  = status_line.compare(0, 8, "HTTP/1.0") != 0;
        size_t header_bytes = 0;
        for (std::string line;;) {
            if (!c.read_line(line, error)) {
                conn_.reset();
                return false;
            }
            if (line.empty()) {
                break;
            }
            header_bytes += line.size();
            if (header_bytes > kMaxHeaderBytes) {
                error = "the response's headers are too long";
                conn_.reset();
                return false;
            }
            const size_t colon = line.find(':');
            if (colon != std::string::npos) {
                out.headers.emplace_back(lower(trim(line.substr(0, colon))), trim(line.substr(colon + 1)));
            }
        }
        const std::string length  = out.header("content-length");
        const bool        chunked = lower(out.header("transfer-encoding")).find("chunked") != std::string::npos;
        if (!length.empty() && !chunked) {
            out.content_length = std::atoll(length.c_str());
        }
        if (lower(out.header("connection")).find("close") != std::string::npos) {
            keep = false;
        }
        const bool no_body = method == "HEAD" || out.status == 204 || out.status == 304 ||
                             (out.status >= 100 && out.status < 200);
        if (no_body) {
            if (!keep) {
                conn_.reset();
            }
            if (on_response) {
                on_response(out);
            }
            return true;
        }
        bool want = on_response ? on_response(out) : true;
        // A body that is not wanted: read and drop a small one, so the
        // connection can be used again (a redirect's few bytes).
        const bool drain = !want && !chunked && out.content_length >= 0 && out.content_length <= 65536;
        if (!want && !drain) {
            conn_.reset();
            return true;
        }
        const auto deliver = [&](const char * p, size_t n) { return !want || !on_body || on_body(p, n); };
        if (chunked) {
            for (;;) {
                std::string size_line;
                if (!c.read_line(size_line, error)) {
                    conn_.reset();
                    return false;
                }
                const unsigned long long n = std::strtoull(size_line.c_str(), nullptr, 16);
                if (n == 0) {
                    for (std::string trailer; c.read_line(trailer, error) && !trailer.empty();) {
                    }
                    break;
                }
                unsigned long long left = n;
                while (left > 0) {
                    const int got = c.read_some(c.chunk.data(), std::min<unsigned long long>(left, c.chunk.size()), error);
                    if (got <= 0) {
                        if (got == 0) error = "the connection closed in the body";
                        conn_.reset();
                        return false;
                    }
                    left -= static_cast<unsigned long long>(got);
                    if (!deliver(c.chunk.data(), static_cast<size_t>(got))) {
                        conn_.reset();
                        return true;
                    }
                }
                std::string crlf;
                if (!c.read_line(crlf, error)) {
                    conn_.reset();
                    return false;
                }
            }
        } else {
            const bool    to_end = out.content_length < 0;
            int64_t       left   = to_end ? INT64_MAX : out.content_length;
            size_t        fill   = 0;  // bytes gathered in c.chunk
            while (left > 0) {
                const size_t room = std::min<int64_t>(left, static_cast<int64_t>(c.chunk.size() - fill));
                const int    got  = c.read_some(c.chunk.data() + fill, room, error);
                if (got < 0 || (got == 0 && !to_end)) {
                    if (got == 0) error = "the connection closed in the body";
                    // What came is handed over, so a range can be resumed.
                    if (fill > 0) deliver(c.chunk.data(), fill);
                    conn_.reset();
                    return false;
                }
                if (got == 0) {
                    break;  // the end of a body without a length
                }
                fill += static_cast<size_t>(got);
                left -= got;
                if (fill == c.chunk.size() || left == 0) {
                    if (!deliver(c.chunk.data(), fill)) {
                        conn_.reset();
                        return true;
                    }
                    fill = 0;
                }
            }
            if (fill > 0 && !deliver(c.chunk.data(), fill)) {
                conn_.reset();
                return true;
            }
            if (to_end) {
                keep = false;
            }
        }
        if (!keep) {
            conn_.reset();
        }
        return true;
    }
    error = "the connection closed before an answer";
    return false;
}

bool http_get_following(HttpClient & client, const std::string & url_in,
                        const std::function<HttpHeaders(const Url &)> & headers_for,
                        const std::function<bool(const HttpResponse &)> & on_response,
                        const std::function<bool(const char *, size_t)> & on_body, HttpResponse & out,
                        std::string & final_url, std::string & error) {
    std::string url = url_in;
    for (int hop = 0; hop < 8; hop++) {
        Url u;
        if (!parse_url(url, u)) {
            error = "not a URL: " + url.substr(0, 200);
            return false;
        }
        bool redirect = false;
        const bool ok = client.request(
            "GET", u, headers_for ? headers_for(u) : HttpHeaders{},
            [&](const HttpResponse & r) {
                redirect = r.status >= 300 && r.status < 400 && r.status != 304 && !r.header("location").empty();
                return redirect ? false : (on_response ? on_response(r) : true);
            },
            on_body, out, error);
        if (!ok) {
            return false;
        }
        if (!redirect) {
            final_url = url;
            return true;
        }
        url = resolve_location(u, out.header("location"));
    }
    error = "too many redirects";
    return false;
}

bool http_get_text(HttpClient & client, const std::string & url,
                   const std::function<HttpHeaders(const Url &)> & headers_for, size_t limit, std::string & body,
                   int & status, std::string & error) {
    body.clear();
    HttpResponse r;
    std::string  final_url;
    bool         too_big = false;
    const bool   ok      = http_get_following(
        client, url, headers_for, nullptr,
        [&](const char * p, size_t n) {
            if (body.size() + n > limit) {
                too_big = true;
                return false;
            }
            body.append(p, n);
            return true;
        },
        r, final_url, error);
    status = r.status;
    if (ok && too_big) {
        error = "the answer is larger than " + std::to_string(limit) + " bytes";
        return false;
    }
    return ok;
}

}  // namespace ps5sd
