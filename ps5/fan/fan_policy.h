/* The fan payload's choices, without the console (tests/test_fan_policy.cpp).
 *
 * The PS5 drives the fan towards a target temperature (byte 5 of /dev/icc_fan's autoservo object,
 * 91 C on the console we measured). PS5SD's fan modes lower that target; the console's own
 * controller still decides the fan's speed. A lower target by several degrees at once drove the
 * fan to 96% within 2 s (91 to 80 under load, docs/CONSOLE.md), so the target goes down one degree
 * at a time; back up it goes at once.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

enum fan_mode { FAN_QUIET = 0, FAN_BALANCED = 1, FAN_COOL = 2, FAN_MANUAL = 3 };

#define FAN_BALANCED_BELOW 3 /* degrees under the stock target */
#define FAN_COOL_BELOW 6
#define FAN_LOWEST 70 /* no mode goes under this (or under the stock target, when that is lower) */

/* The target a mode asks for, from the console's stock target. */
static inline int fan_wanted(int mode, int manual, int stock) {
  int want = stock;
  if (mode == FAN_BALANCED) want = stock - FAN_BALANCED_BELOW;
  else if (mode == FAN_COOL) want = stock - FAN_COOL_BELOW;
  else if (mode == FAN_MANUAL) want = manual;
  if (want > stock) want = stock;
  if (want < FAN_LOWEST) want = stock < FAN_LOWEST ? stock : FAN_LOWEST;
  return want;
}

/* The next target to write: one degree down when the last step is step_s seconds ago, up at once. */
static inline int fan_next(int current, int wanted, double since_step, double step_s) {
  if (wanted >= current) return wanted;
  return since_step >= step_s ? current - 1 : current;
}

/* "quiet", "balanced", "cool", "manual" as a mode; -1 for anything else. */
static inline int fan_mode_from(const char *s) {
  static const char *const names[] = {"quiet", "balanced", "cool", "manual"};
  for (int i = 0; i < 4; ++i) {
    const char *a = names[i], *b = s;
    while (*a && *a == *b) ++a, ++b;
    if (*a == 0 && (*b == 0 || *b == ' ' || *b == '\n' || *b == '\r')) return i;
  }
  return -1;
}
