// Host test for ps5/fan/fan_policy.h: the target each fan mode asks for, and
// the steps the payload takes towards it.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fan_policy.h"

#include <cstdio>

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
    check(fan_wanted(FAN_QUIET, 0, 91) == 91, "quiet: the stock target");
    check(fan_wanted(FAN_BALANCED, 0, 91) == 88, "balanced: 3 under stock");
    check(fan_wanted(FAN_COOL, 0, 91) == 85, "cool: 6 under stock");
    check(fan_wanted(FAN_MANUAL, 80, 91) == 80, "manual: its target");
    check(fan_wanted(FAN_MANUAL, 95, 91) == 91, "manual: never over stock");
    check(fan_wanted(FAN_MANUAL, 50, 91) == FAN_LOWEST, "manual: never under the lowest");
    check(fan_wanted(FAN_COOL, 0, 72) == FAN_LOWEST, "cool on a low stock target: the lowest");
    check(fan_wanted(FAN_COOL, 0, 69) == 69, "a stock target under the lowest stays");

    check(fan_next(91, 85, 100, 20) == 90, "down: one degree");
    check(fan_next(90, 85, 5, 20) == 90, "down: not before the step time");
    check(fan_next(85, 85, 100, 20) == 85, "there: no change");
    check(fan_next(85, 91, 0, 20) == 91, "up: at once");

    check(fan_mode_from("quiet") == FAN_QUIET, "quiet");
    check(fan_mode_from("manual 80") == FAN_MANUAL, "manual with a target");
    check(fan_mode_from("cool\r") == FAN_COOL, "cool with CR");
    check(fan_mode_from("coolest") == -1, "a longer word is no mode");
    check(fan_mode_from("") == -1, "empty");

    if (failures == 0) {
        std::printf("fan_policy: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
