#include "bench.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

volatile size_t sink;
char *volatile sink_ptr;
bool use_color;

double now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1e9 + ts.tv_nsec;
}

/* ---- Grouped output ---- */

// Results of one group of alternatives, printed together by group_end.
#define GROUP_MAX 8
#define NOTE_MAX 64
static struct {
  const char *name; // NULL: rows sit directly under the section title
  bench_unit unit;
  int n;
  const char *labels[GROUP_MAX];
  double values[GROUP_MAX];
  bool failed[GROUP_MAX];
  char notes[GROUP_MAX][NOTE_MAX];
} group;

void group_end(void) {
  if (group.n == 0)
    return;
  const char *indent = group.name ? "    " : "  ";
  if (group.name)
    printf("\n  %s%s%s\n", BOLD, group.name, RESET);

  // fastest row that didn't fail; compared only if there are two or more
  int best = -1, compared = 0;
  for (int i = 0; i < group.n; ++i) {
    if (group.failed[i])
      continue;
    compared++;
    if (best < 0 || (group.unit == NS_PER_OP
                         ? group.values[i] < group.values[best]
                         : group.values[i] > group.values[best]))
      best = i;
  }

  for (int i = 0; i < group.n; ++i) {
    double v = group.values[i];
    printf("%s%-*s ", indent, 42 - (int)strlen(indent), group.labels[i]);
    printf("%s", group.failed[i] ? DIM : i == best && compared > 1 ? GREEN : "");
    if (group.unit == NS_PER_OP)
      printf("%9.1f ns/op", v);
    else if (v >= 1.0)
      printf("%9.2f GB/s ", v);
    else // microcontrollers: GB/s would round to 0.0x
      printf("%9.1f MB/s ", v * 1000);
    printf("%s", RESET);

    // how many times longer this takes than the fastest, padded so the
    // notes line up
    char ratio_text[24] = "";
    const char *color = "";
    if (group.failed[i]) {
      snprintf(ratio_text, sizeof ratio_text, "FAILED");
      color = RED;
    } else if (compared > 1) {
      double ratio = group.unit == NS_PER_OP ? v / group.values[best]
                                             : group.values[best] / v;
      if (i == best) {
        snprintf(ratio_text, sizeof ratio_text, "fastest");
        color = GREEN;
      } else if (ratio < 1.02) { // within noise
        snprintf(ratio_text, sizeof ratio_text, "~same");
        color = DIM;
      } else {
        snprintf(ratio_text, sizeof ratio_text, "%5.2fx slower", ratio);
        color = ratio < 1.10 ? DIM : ratio < 2.0 ? YELLOW : RED;
      }
    }
    if (group.notes[i][0])
      printf("  %s%-14s%s  %s%s%s", color, ratio_text, RESET, DIM,
             group.notes[i], RESET);
    else if (ratio_text[0])
      printf("  %s%s%s", color, ratio_text, RESET);
    printf("\n");
  }
  group.n = 0;
}

void group_begin(const char *name, bench_unit unit) {
  group_end();
  group.name = name;
  group.unit = unit;
}

void group_add(const char *label, double value) {
  if (group.n < GROUP_MAX) {
    group.labels[group.n] = label;
    group.values[group.n] = value;
    group.notes[group.n][0] = '\0';
    group.failed[group.n] = false;
    group.n++;
  }
}

void group_note(const char *fmt, ...) {
  if (group.n == 0)
    return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(group.notes[group.n - 1], NOTE_MAX, fmt, ap);
  va_end(ap);
}

void group_failed(void) {
  if (group.n > 0)
    group.failed[group.n - 1] = true;
}

void section(const char *title, const char *fmt, ...) {
  group_end();
  printf("\n%s%s== %s ", BOLD, CYAN, title);
  for (size_t i = strlen(title); i < 60; ++i)
    printf("=");
  printf("%s\n", RESET);
  if (fmt) {
    va_list ap;
    va_start(ap, fmt);
    printf("%s", DIM);
    vprintf(fmt, ap);
    printf("%s\n", RESET);
    va_end(ap);
  }
}
