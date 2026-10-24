// The body of a thread the app starts. An exception that leaves a std::thread
// ends the process (std::terminate), and the title with it: on the console a
// std::bad_alloc is a real case, since malloc takes its memory from the pool
// the model fills (docs/CONSOLE.md, "PS5SD ended by a full CPU heap").
// run_guarded runs fn and logs what it threw instead; false when it threw.
//
// No console code in here, so ps5/app/tests builds it on the PC.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>
#include <exception>
#include <string>

namespace ps5sd {

template <class F> bool run_guarded(const char * name, F && fn, std::string * error = nullptr) noexcept {
    const char * what = nullptr;
    try {
        fn();
        return true;
    } catch (const std::exception & e) {
        std::printf("ps5sd: %s: %s\n", name, e.what());
        what = e.what();
        if (error != nullptr) {
            try {
                *error = what;
            } catch (...) {
            }
        }
    } catch (...) {
        std::printf("ps5sd: %s: an exception that is not a std::exception\n", name);
        if (error != nullptr) {
            try {
                *error = "unknown error";
            } catch (...) {
            }
        }
    }
    return false;
}

}  // namespace ps5sd
