// Host test for thread_guard.hpp: an exception in a thread's body is logged
// and reported, and the process goes on.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "thread_guard.hpp"

#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>

using namespace ps5sd;

namespace {

int failures = 0;

void check(bool ok, const char * what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main() {
    std::string error = "unchanged";
    check(run_guarded("returns", [] {}, &error) && error == "unchanged", "a body that returns: true, no error");

    check(!run_guarded("throws", [] { throw std::runtime_error("disk gone"); }, &error) && error == "disk gone",
          "a std::exception: false, its text");

    check(!run_guarded("throws an int", [] { throw 7; }, &error) && error == "unknown error",
          "something else: false, unknown error");

    // The case that ended the title: std::bad_alloc on a thread of its own.
    bool        ran_to_end = false;
    std::thread t([&] {
        ran_to_end = !run_guarded("thread", [] { throw std::bad_alloc(); }, &error);
    });
    t.join();
    check(ran_to_end && error == "std::bad_alloc", "std::bad_alloc on a thread is caught there");

    check(!run_guarded("no error string", [] { throw std::logic_error("x"); }), "works without an error string");

    if (failures == 0) {
        std::printf("thread_guard: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
