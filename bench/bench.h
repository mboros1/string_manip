#ifndef BENCH_H
#define BENCH_H

// Shared timing and output for the benchmarks (bench_*.c).
//
// Results are printed in groups of alternatives; the fastest in each group is
// highlighted and the rest show how much slower they are. Colors are used on
// a terminal unless NO_COLOR is set.

#include <stdbool.h>
#include <stddef.h>

#define RUNS 5
// Input records; lowered on small targets (the ESP32 app uses 400)
#ifndef NLINES
#define NLINES 20000
#endif

// The io group needs a file system (temporary files, getline,
// open_memstream); microcontroller builds leave it out
#ifndef FAF_BENCH_HAVE_FILES
#ifdef ESP_PLATFORM
#define FAF_BENCH_HAVE_FILES 0
#else
#define FAF_BENCH_HAVE_FILES 1
#endif
#endif

// Written by every benchmark so the compiler can't drop the work.
extern volatile size_t sink;
// Escapes malloc results so the compiler can't elide malloc/free pairs.
extern char *volatile sink_ptr;

// CSV-like records: 8 fields of 2..24 mixed-case alphanumerics.
extern char *lines[NLINES];
extern size_t line_lens[NLINES];

extern bool use_color;

#define SGR(code) (use_color ? "\033[" code "m" : "")
#define RESET SGR("0")
#define BOLD SGR("1")
#define DIM SGR("2")
#define RED SGR("31")
#define GREEN SGR("32")
#define YELLOW SGR("33")
#define CYAN SGR("36")

double now_ns(void);

typedef enum { NS_PER_OP, GB_PER_S } bench_unit;

// Section title, with an optional dimmed printf-style description.
void section(const char *title, const char *fmt, ...);
// Start a new group of alternatives; `name` may be NULL to list the rows
// directly under the section title.
void group_begin(const char *name, bench_unit unit);
void group_add(const char *label, double value);
// Extra detail for the row added last, printed dimmed after the ratio.
void group_note(const char *fmt, ...);
// Mark the row added last as failed: it is left out of the comparison.
void group_failed(void);
// Print the current group. Called by section/group_begin automatically.
void group_end(void);

// Best of RUNS runs of the statement(s), reported as ns per `ops`.
#define BENCH(label, ops, ...)                                                  \
  do {                                                                         \
    double best_ = 1e30;                                                       \
    for (int run_ = 0; run_ < RUNS; ++run_) {                                  \
      double t0_ = now_ns();                                                   \
      __VA_ARGS__;                                                             \
      double t_ = now_ns() - t0_;                                              \
      if (t_ < best_)                                                          \
        best_ = t_;                                                            \
    }                                                                          \
    group_add(label, best_ / (ops));                                           \
  } while (0)

// Best of RUNS runs of the statement(s), reported as GB/s over `bytes`.
#define THROUGHPUT(label, bytes, ...)                                          \
  do {                                                                         \
    double best_ = 1e30;                                                       \
    for (int run_ = 0; run_ < RUNS; ++run_) {                                  \
      double t0_ = now_ns();                                                   \
      __VA_ARGS__;                                                             \
      double t_ = now_ns() - t0_;                                              \
      if (t_ < best_)                                                          \
        best_ = t_;                                                            \
    }                                                                          \
    group_add(label, (double)(bytes) / best_);                                 \
  } while (0)

// Benchmark groups, selectable on the command line.
void bench_strings(void);
void bench_kernels(void);
void bench_alloc(void);
void bench_io(void);
void bench_batch(void);

#endif // BENCH_H
