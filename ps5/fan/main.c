/* ps5sd-fan: PS5SD's fan modes, as a payload (a title may not open /dev/icc_fan).
 *
 * PS5SD sends it to the ELF loader when a mode other than Quiet is set, and talks to it on
 * 127.0.0.1:9075, one line per connection:
 *   MODE <quiet|balanced|cool|manual> [target]   set the mode; it is also the heartbeat
 *   STATE                                        no change
 * Both answer "OK mode=<m> stock=<C> target=<C> wanted=<C> duty=<0..1024> filtered=<C>".
 *
 * Every 2 s it reads the autoservo object (/dev/icc_fan 8F08) fresh, changes byte 5, the target
 * temperature, writes it (8F07) and reads it back. The console's own controller still sets the fan
 * from it. The target goes down one degree every FAN_STEP_S seconds, up at once (fan_policy.h).
 * When the console changes the target itself (a new fan pattern), that value is the new stock.
 * Without a heartbeat for FAN_BEAT_S seconds, or in Quiet, it puts the stock target back and ends.
 * /data/PS5SD/fan-state.txt keeps the stock target while a lower one is written, for the next
 * instance after a crash.
 *
 * The interface is from drakmor/fan_target and its technical reference (GPL-3.0); so is the way an
 * older instance is found and stopped.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#include "fan_policy.h"

#define PROCESS_NAME "ps5sd-fan.elf"
#define PORT 9075
#define STATE_FILE "/data/PS5SD/fan-state.txt"
#define FAN_GET_AUTOSERVO 0xC01C8F08UL
#define FAN_SET_AUTOSERVO 0xC01C8F07UL
#define FAN_GET_EXTENDED 0xC0148F09UL
#define FAN_STEP_S 20.0
#define FAN_BEAT_S 15.0
#define KINFO_PID_OFFSET 72
#define KINFO_TDNAME_OFFSET 447

int sceKernelDebugOutText(int channel, const char *text);

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int mode = FAN_QUIET, manual = 91, told = 0; /* told: a MODE came */
static double last_beat;
static int stock = -1, current = -1, wanted = -1, duty = -1;
static double filtered = -1;

static void log_line(const char *format, ...) {
  char message[256], line[300];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  snprintf(line, sizeof(line), "[ps5sd-fan] %s\n", message);
  sceKernelDebugOutText(0, line);
}

static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

static pid_t find_old_instance(void) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t size = 0;
  pid_t self = getpid(), found = 0;
  if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0 || size == 0) return size == 0 ? 0 : -1;
  uint8_t *buffer = malloc(size);
  if (buffer == NULL) return -1;
  if (sysctl(mib, 4, buffer, &size, NULL, 0) != 0) {
    free(buffer);
    return -1;
  }
  for (uint8_t *p = buffer, *end = buffer + size; (size_t)(end - p) >= sizeof(int);) {
    int record = 0;
    pid_t pid = 0;
    memcpy(&record, p, sizeof(record));
    if (record <= KINFO_TDNAME_OFFSET || (size_t)record > (size_t)(end - p)) {
      found = -1;
      break;
    }
    memcpy(&pid, p + KINFO_PID_OFFSET, sizeof(pid));
    const char *name = (const char *)(p + KINFO_TDNAME_OFFSET);
    size_t cap = (size_t)record - KINFO_TDNAME_OFFSET;
    if (pid != self && strnlen(name, cap) < cap && strcmp(name, PROCESS_NAME) == 0) {
      found = pid;
      break;
    }
    p += record;
  }
  free(buffer);
  return found;
}

static void stop_old_instances(void) {
  for (int i = 0; i < 16; ++i) {
    pid_t pid = find_old_instance();
    if (pid <= 0) return;
    kill(pid, SIGKILL);
    log_line("stopped the previous instance");
    usleep(100000);
  }
}

static int read_object(int fd, uint8_t obj[28]) {
  memset(obj, 0, 28);
  return ioctl(fd, FAN_GET_AUTOSERVO, obj);
}

/* GET, change byte 5, SET, GET again: 0 when the console has target t now. */
static int write_target(int fd, int t) {
  uint8_t obj[28], back[28];
  if (read_object(fd, obj) != 0) return -1;
  obj[5] = (uint8_t)t;
  if (ioctl(fd, FAN_SET_AUTOSERVO, obj) != 0) return -1;
  if (read_object(fd, back) != 0) return -1;
  return back[5] == t ? 0 : -1;
}

static void save_state(int stock_c, int written) {
  if (written >= stock_c) {
    unlink(STATE_FILE);
    return;
  }
  FILE *f = fopen(STATE_FILE ".tmp", "w");
  if (f == NULL) return;
  fprintf(f, "%d %d\n", stock_c, written);
  fclose(f);
  rename(STATE_FILE ".tmp", STATE_FILE);
}

/* The stock target a crashed instance noted, when the console still has the target it wrote. */
static int noted_stock(int target_now) {
  int s = -1, w = -1;
  FILE *f = fopen(STATE_FILE, "r");
  if (f == NULL) return -1;
  if (fscanf(f, "%d %d", &s, &w) != 2) s = -1;
  fclose(f);
  return w == target_now && s > target_now && s <= 100 ? s : -1;
}

static void *control(void *arg) {
  (void)arg;
  int fd = -1, written = -1, failures = 0;
  double last_step = 0;
  for (;;) {
    uint8_t obj[28], ext[20];
    if (fd < 0) fd = open("/dev/icc_fan", 0x10002);
    if (fd < 0 || read_object(fd, obj) != 0) {
      log_line("cannot read the fan's target (errno %d)", errno);
      if (++failures >= 5) exit(1);
      sleep(2);
      continue;
    }
    int t = obj[5];
    pthread_mutex_lock(&lock);
    if (stock < 0) {
      int noted = noted_stock(t);
      stock = noted > 0 ? noted : t;
      log_line("stock target %d C, now %d C", stock, t);
    } else if (written >= 0 && t != written) {
      log_line("the console set the target to %d C (was %d C); that is the stock target now", t, written);
      stock = t;
      written = -1;
      last_step = now_s() + FAN_BEAT_S; /* PS5SD closing does this too: no step before its heartbeat is gone */
    }
    double now = now_s();
    int m = mode;
    int gone = now - last_beat > FAN_BEAT_S; /* from the start too: PS5SD sends a MODE right after it */
    int end = gone || (told && m == FAN_QUIET);
    if (gone) m = FAN_QUIET;
    wanted = fan_wanted(m, manual, stock);
    int next = fan_next(t, wanted, now - last_step, FAN_STEP_S);
    int stock_c = stock;
    pthread_mutex_unlock(&lock);
    if (next != t) {
      if (write_target(fd, next) == 0) {
        log_line("target %d C -> %d C", t, next);
        t = written = next;
        last_step = now;
        failures = 0;
        save_state(stock_c, next);
      } else if (++failures >= 3) {
        log_line("cannot change the target; ending");
        exit(1);
      }
    }
    memset(ext, 0, sizeof(ext));
    uint32_t d = 0, f = 0;
    if (ioctl(fd, FAN_GET_EXTENDED, ext) == 0) {
      memcpy(&d, ext + 12, 4);
      memcpy(&f, ext + 16, 4);
    }
    pthread_mutex_lock(&lock);
    current = t;
    duty = (int)d;
    filtered = f / 256.0;
    pthread_mutex_unlock(&lock);
    if (end && t >= stock_c) {
      log_line(gone ? "no word from PS5SD; stock target back, ending" : "quiet: stock target back, ending");
      save_state(stock_c, t);
      exit(0);
    }
    sleep(2);
  }
  return NULL;
}

static void answer(int c) {
  char line[96], out[160];
  size_t n = 0;
  while (n + 1 < sizeof(line)) {
    char ch;
    if (recv(c, &ch, 1, 0) != 1 || ch == '\n') break;
    if (ch != '\r') line[n++] = ch;
  }
  line[n] = 0;
  pthread_mutex_lock(&lock);
  if (strncmp(line, "MODE ", 5) == 0) {
    int m = fan_mode_from(line + 5);
    if (m >= 0) {
      mode = m;
      const char *sp = strchr(line + 5, ' ');
      if (m == FAN_MANUAL && sp != NULL) manual = atoi(sp + 1);
      last_beat = now_s();
      told = 1;
    }
  }
  static const char *const names[] = {"quiet", "balanced", "cool", "manual"};
  snprintf(out, sizeof(out), "OK mode=%s stock=%d target=%d wanted=%d duty=%d filtered=%.1f\n", names[mode], stock,
           current, wanted, duty, filtered);
  pthread_mutex_unlock(&lock);
  send(c, out, strlen(out), 0);
}

int main(void) {
  stop_old_instances();
  syscall(SYS_thr_set_name, -1, PROCESS_NAME);
  signal(SIGPIPE, SIG_IGN);
  int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
  struct sockaddr_in a;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons(PORT);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 4) != 0) {
    log_line("cannot listen on %d", PORT);
    return 1;
  }
  last_beat = now_s();
  pthread_t th;
  if (pthread_create(&th, NULL, control, NULL) != 0) return 1;
  log_line("listening on 127.0.0.1:%d", PORT);
  for (;;) {
    int c = accept(s, NULL, NULL);
    if (c < 0) continue;
    struct timeval tv = {2, 0};
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    answer(c);
    close(c);
  }
}
