// Adapted from PS5LM (github.com/cobanov/PS5LM, GPL-3.0)
//
// A small HTTP/1.1 client over plain sockets and mbedTLS 3.6 with the Mozilla
// CA list built in, for the model downloader. PS5LM's payload downloads with
// cpp-httplib over mbedTLS (ps5/ps5lm/app.cpp); here the client is our own,
// because sd-server already links cpp-httplib 0.28, which has no mbedTLS
// backend, and two versions of it in one link would share symbol names.
//
// On the console, names are resolved through libSceNet's resolver: the
// title's getaddrinfo (compat_app.c) takes numeric addresses only. A client
// holds one connection and keeps it alive between requests to the same host;
// use one client per thread.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "download_core.hpp"

namespace ps5sd {

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct HttpResponse {
    int         status = 0;
    HttpHeaders headers;  // names in lower case
    int64_t     content_length = -1;
    std::string header(const std::string & lower_name) const;
};

// The TLS stack (PSA, the RNG, the CA list) and, on the console, libSceNet.
// Once per process; later calls return the first result.
bool https_init(std::string & error);

class HttpClient {
  public:
    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient &)             = delete;
    HttpClient & operator=(const HttpClient &) = delete;

    // One request. on_response sees the status and headers; returning false
    // skips the body. on_body gets the body in pieces; returning false stops
    // it (the connection is then dropped). False with error on a network or
    // TLS failure, or a response that cannot be parsed; out has what came.
    bool request(const std::string & method, const Url & url, const HttpHeaders & headers,
                 const std::function<bool(const HttpResponse &)> & on_response,
                 const std::function<bool(const char *, size_t)> & on_body, HttpResponse & out, std::string & error);

    // Drops the connection.
    void close();

    // Send and receive time out after this many seconds without progress.
    void set_timeout(int seconds) { timeout_ = seconds; }

  private:
    struct Conn;
    std::unique_ptr<Conn> conn_;
    int                   timeout_ = 30;
};

// A GET that follows up to 8 redirects. headers_for(url) gives the headers of
// each hop (so a token goes only to the host it is for). The final URL, after
// the redirects, is in final_url.
bool http_get_following(HttpClient & client, const std::string & url,
                        const std::function<HttpHeaders(const Url &)> & headers_for,
                        const std::function<bool(const HttpResponse &)> & on_response,
                        const std::function<bool(const char *, size_t)> & on_body, HttpResponse & out,
                        std::string & final_url, std::string & error);

// A whole small body (an API's JSON), up to limit bytes.
bool http_get_text(HttpClient & client, const std::string & url,
                   const std::function<HttpHeaders(const Url &)> & headers_for, size_t limit, std::string & body,
                   int & status, std::string & error);

}  // namespace ps5sd
