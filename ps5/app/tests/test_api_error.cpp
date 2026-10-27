// Host test for sd-server's api_error.cpp (patches/sdcpp/0076): every error
// answer comes out as {"error": {"code", "message"}}, whatever shape the route
// wrote, and the other fields stay.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "api_error.h"

#include <cstdio>
#include <string>

#include "json.hpp"

using json = nlohmann::json;

namespace {

int failures = 0;

void check(bool ok, const std::string & what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// The body after normalizing, parsed.
json normalized(int status, const std::string & type, std::string body, bool * changed = nullptr) {
    const bool c = api_error_normalize(status, type, "Fallback", body);
    if (changed != nullptr) {
        *changed = c;
    }
    return json::parse(body, nullptr, false);
}

bool shape(const json & j, const std::string & code, const std::string & message) {
    return j.is_object() && j.contains("error") && j["error"].is_object() && j["error"].value("code", "") == code &&
           j["error"].value("message", "") == message;
}

}  // namespace

int main() {
    const std::string js = "application/json";

    check(shape(normalized(404, js, R"({"error":"no model x"})"), "not_found", "no model x"), "a text error");
    check(shape(normalized(400, js, R"({"error":"invalid json","message":"at 1"})"), "invalid_request",
                "invalid json: at 1"),
          "a message next to a text error");
    check(shape(normalized(409, js, R"({"error":"busy","code":"switching"})"), "switching", "busy"), "a top-level code");
    {
        const json j = normalized(503, js, R"({"error":"no model","standby":true})");
        check(shape(j, "standby", "no model") && !j.contains("standby"), "standby is a code, not a flag");
    }
    {
        const json j = normalized(401, js, R"({"error":"key","auth_required":true})");
        check(shape(j, "unauthorized", "key") && !j.contains("auth_required"), "auth_required is dropped");
    }
    {
        const json j = normalized(409, js, R"({"error":"in use","models":[1,2]})");
        check(shape(j, "conflict", "in use") && j["models"].size() == 2, "other fields stay");
    }
    {
        bool       changed = true;
        const json j       = normalized(500, js, R"({"error":{"code":"out_of_memory","message":"m"}})", &changed);
        check(shape(j, "out_of_memory", "m") && !changed, "the new shape is left as it is");
    }
    check(shape(normalized(409, js, R"({"error":{"message":"m"}})"), "conflict", "m"),
          "a missing code is the status's");
    check(shape(normalized(404, "", ""), "not_found", "Fallback"), "an empty body");
    check(shape(normalized(400, "text/plain", "bad range\n"), "invalid_request", "bad range"), "plain text");
    check(shape(normalized(500, "text/html", "<html>"), "server_error", "Fallback"), "html is not a message");
    check(shape(normalized(400, js, "not json"), "invalid_request", "Fallback"), "a JSON type with a broken body");
    check(shape(normalized(418, js, R"({"error":"x"})"), "http_418", "x"), "a status without a code of its own");
    // not UTF-8, so not JSON either: no throw, the fallback
    check(shape(normalized(400, js, "{\"error\":\"bad \xff\"}"), "invalid_request", "Fallback"), "a byte that is not UTF-8");
    check(api_error_code_for_status(507) == "storage_full", "507");
    check(shape(api_error("queue_full", "q"), "queue_full", "q"), "api_error");

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("api_error: all passed\n");
    return 0;
}
