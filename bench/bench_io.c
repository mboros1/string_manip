// End-to-end string workloads, each against an idiomatic libc version.
//
//   reading lines   fgets / getline / whole file / 16 KB chunks
//   CSV transform   split, change fields, join, write out
//   word count      tokenize, hash, count; keys outlive the input
//   format / parse  integers and records to text, text to numbers
//
// The input file is written to $TMPDIR first, so reads come from the page
// cache: this measures the parsing, not the disk.

#include "bench.h"
#include "faf.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint64_t rng_state;
static uint64_t rng(void) {
  uint64_t x = rng_state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  return rng_state = x;
}

// The CSV records as one buffer, one record per line, and as a file.
static char *csv;
static size_t csv_len;
static char csv_path[1024];

static void make_csv(void) {
  csv_len = 0;
  for (int i = 0; i < NLINES; ++i)
    csv_len += line_lens[i] + 1;
  csv = malloc(csv_len + 1);
  char *p = csv;
  for (int i = 0; i < NLINES; ++i) {
    memcpy(p, lines[i], line_lens[i]);
    p += line_lens[i];
    *p++ = '\n';
  }
  *p = '\0';

  const char *dir = getenv("TMPDIR");
  snprintf(csv_path, sizeof csv_path, "%s/faf_bench_XXXXXX",
           dir && *dir ? dir : "/tmp");
  int fd = mkstemp(csv_path);
  if (fd < 0 || write(fd, csv, csv_len) != (ssize_t)csv_len) {
    perror("bench_io: writing the input file");
    exit(1);
  }
  close(fd);
}

/* ---- 1. Reading lines ---- */

static void read_fgets(void) {
  size_t acc = 0;
  char buf[4096];
  FILE *f = fopen(csv_path, "r");
  while (fgets(buf, sizeof buf, f))
    acc += strlen(buf);
  fclose(f);
  sink = acc;
}

static void read_getline(void) {
  size_t acc = 0, cap = 0;
  char *line = NULL;
  ssize_t n;
  FILE *f = fopen(csv_path, "r");
  while ((n = getline(&line, &cap, f)) > 0)
    acc += (size_t)n;
  free(line);
  fclose(f);
  sink = acc;
}

static char *read_whole(size_t *len) {
  FILE *f = fopen(csv_path, "r");
  fseek(f, 0, SEEK_END);
  *len = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc(*len + 1);
  *len = fread(buf, 1, *len, f);
  fclose(f);
  return buf;
}

static void read_whole_faf(void) {
  size_t acc = 0, len;
  char *buf = read_whole(&len);
  faf_string rest = faf_string_init_n(buf, len), line;
  while (faf_string_next_token(&rest, '\n', &line))
    acc += faf_string_len(line) + 1;
  free(buf);
  sink = acc;
}

static void read_whole_memchr(void) {
  size_t acc = 0, len;
  char *buf = read_whole(&len);
  const char *p = buf, *end = buf + len;
  while (p < end) {
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    const char *e = nl ? nl : end;
    acc += (size_t)(e - p) + 1;
    p = e + 1;
  }
  free(buf);
  sink = acc;
}

// Streaming in fixed chunks, the shape a region-sized buffer forces: a line
// cut off at the end of a chunk is moved to the front for the next read.
#define CHUNK (16 * 1024)
static void read_chunks(void) {
  size_t acc = 0, have = 0;
  static char buf[CHUNK + 4096];
  FILE *f = fopen(csv_path, "r");
  for (;;) {
    size_t got = fread(buf + have, 1, CHUNK, f);
    size_t total = have + got;
    if (total == 0)
      break;
    faf_string rest = faf_string_init_n(buf, total), line;
    while (faf_string_split_once(rest, '\n', &line, &rest))
      acc += faf_string_len(line) + 1;
    have = faf_string_len(rest);
    if (got == 0) { // last line without a newline
      acc += have;
      break;
    }
    memmove(buf, rest.start, have);
  }
  fclose(f);
  sink = acc;
}

static void reading_lines(void) {
  section("Reading lines",
          "%.1f MB file of %d CSV records, from the page cache; higher is "
          "better",
          csv_len / 1e6, NLINES);
  group_begin(NULL, GB_PER_S);
  THROUGHPUT("fgets, 4 KB buffer", csv_len, read_fgets());
  THROUGHPUT("getline", csv_len, read_getline());
  THROUGHPUT("whole file + faf_string_next_token", csv_len, read_whole_faf());
  THROUGHPUT("whole file + memchr", csv_len, read_whole_memchr());
  THROUGHPUT("16 KB chunks + faf_string_split_once", csv_len, read_chunks());
}

/* ---- 2. CSV transform ---- */

// Per record: lowercase field 2, swap fields 0 and 1, join with ';', write.
static FILE *devnull;

static void csv_faf_join(void) {
  faf_string rest = faf_string_init_n(csv, csv_len), line;
  const faf_string sep = faf_string_init(";");
  while (faf_string_next_token(&rest, '\n', &line)) {
    if (faf_string_len(line) == 0)
      continue;
    faf_region r = faf_region_acquire();
    faf_string_arr f = faf_string_split(r, line, ',');
    f.start[2] = faf_string_to_lower(r, f.start[2]);
    faf_string t = f.start[0];
    f.start[0] = f.start[1];
    f.start[1] = t;
    faf_string out = faf_string_join(r, f, sep);
    fwrite(out.start, 1, faf_string_len(out), devnull);
    putc('\n', devnull);
    faf_region_release(r);
  }
}

static void csv_faf_builder(void) {
  faf_string rest = faf_string_init_n(csv, csv_len), line;
  while (faf_string_next_token(&rest, '\n', &line)) {
    if (faf_string_len(line) == 0)
      continue;
    faf_region r = faf_region_acquire();
    faf_string f0, f1, tail;
    faf_string_split_once(line, ',', &f0, &tail);
    faf_string_split_once(tail, ',', &f1, &tail);
    faf_builder b = faf_builder_init(r);
    faf_builder_append(&b, f1);
    faf_builder_append_char(&b, ';');
    faf_builder_append(&b, f0);
    faf_string field;
    for (int k = 2; faf_string_next_token(&tail, ',', &field); ++k) {
      faf_builder_append_char(&b, ';');
      faf_builder_append(&b, k == 2 ? faf_string_to_lower(r, field) : field);
    }
    faf_builder_append_char(&b, '\n');
    faf_string out = faf_builder_finish(&b);
    fwrite(out.start, 1, faf_string_len(out), devnull);
    faf_region_release(r);
  }
}

static void csv_libc(void) {
  const char *p = csv, *end = csv + csv_len;
  while (p < end) {
    const char *nl = memchr(p, '\n', (size_t)(end - p));
    size_t n = (size_t)((nl ? nl : end) - p);
    char buf[512];
    memcpy(buf, p, n);
    buf[n] = '\0';
    p += n + 1;

    char *fields[16], *q = buf;
    int nf = 0;
    while (nf < 16 && (fields[nf] = strsep(&q, ",")))
      ++nf;
    for (char *c = fields[2]; *c; ++c)
      *c = (char)tolower((unsigned char)*c);
    char *t = fields[0];
    fields[0] = fields[1];
    fields[1] = t;
    for (int k = 0; k < nf; ++k) {
      if (k)
        putc(';', devnull);
      fputs(fields[k], devnull);
    }
    putc('\n', devnull);
  }
}

static void csv_transform(void) {
  devnull = fopen("/dev/null", "w");
  setvbuf(devnull, NULL, _IOFBF, 64 * 1024);
  section("CSV transform",
          "per record: lowercase field 2, swap fields 0 and 1, join with ';', "
          "write to /dev/null; GB/s of input");
  group_begin(NULL, GB_PER_S);
  THROUGHPUT("faf: split + to_lower + join", csv_len, csv_faf_join());
  THROUGHPUT("faf: split_once/next_token + builder", csv_len,
             csv_faf_builder());
  THROUGHPUT("libc: strsep + tolower + fputs", csv_len, csv_libc());
  fclose(devnull);
}

/* ---- 3. Word count ---- */

#define VOCAB 600
#define TEXT_WORDS 200000
#define TABLE (1 << 11) // > 3x VOCAB, power of two

static char *text;
static size_t text_len;

// Space-separated words from a fixed vocabulary, skewed so a few words are
// very common (roughly like natural text).
static void make_text(void) {
  static char vocab[VOCAB][12];
  rng_state = 0x9E3779B97F4A7C15ull;
  for (int w = 0; w < VOCAB; ++w) {
    int len = 3 + (int)(rng() % 8);
    for (int c = 0; c < len; ++c)
      vocab[w][c] = (char)('a' + rng() % 26);
    vocab[w][len] = '\0';
  }
  text = malloc((size_t)TEXT_WORDS * 12);
  char *p = text;
  for (int i = 0; i < TEXT_WORDS; ++i) {
    double u = (double)(rng() >> 11) / (double)(1ull << 53);
    const char *w = vocab[(int)(u * u * u * VOCAB)];
    size_t n = strlen(w);
    memcpy(p, w, n);
    p += n;
    *p++ = ' ';
  }
  text_len = (size_t)(p - text);
}

static size_t wc_unique;
static size_t wc_key_bytes;

static void wc_faf(void) {
  static struct {
    faf_string key;
    uint64_t hash;
    uint32_t count;
  } tab[TABLE];
  memset(tab, 0, sizeof tab);
  faf_region keys = faf_region_acquire();
  size_t unique = 0;
  faf_string rest = faf_string_init_n(text, text_len), w;
  while (faf_string_next_token(&rest, ' ', &w)) {
    if (faf_string_len(w) == 0)
      continue;
    uint64_t h = faf_string_hash(w);
    size_t i = h & (TABLE - 1);
    while (tab[i].count && !(tab[i].hash == h && faf_string_eq(tab[i].key, w)))
      i = (i + 1) & (TABLE - 1);
    if (!tab[i].count) {
      tab[i].key = faf_string_copy(keys, w); // outlives the text
      tab[i].hash = h;
      unique++;
    }
    tab[i].count++;
  }
  wc_unique = unique;
  wc_key_bytes = faf_region_used(keys) * FAF_SLOT_BYTES;
  faf_region_release(keys);
}

static uint64_t fnv1a(const char *p, size_t n) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (size_t i = 0; i < n; ++i)
    h = (h ^ (unsigned char)p[i]) * 0x100000001b3ull;
  return h;
}

static void wc_libc(bool faf_hash) {
  static struct {
    char *key;
    size_t len;
    uint64_t hash;
    uint32_t count;
  } tab[TABLE];
  memset(tab, 0, sizeof tab);
  const char *p = text, *end = text + text_len;
  while (p < end) {
    const char *sp = memchr(p, ' ', (size_t)(end - p));
    size_t n = (size_t)((sp ? sp : end) - p);
    if (n) {
      uint64_t h = faf_hash ? faf_string_hash(faf_string_init_n(p, n))
                            : fnv1a(p, n);
      size_t i = h & (TABLE - 1);
      while (tab[i].count && !(tab[i].hash == h && tab[i].len == n &&
                               !memcmp(tab[i].key, p, n)))
        i = (i + 1) & (TABLE - 1);
      if (!tab[i].count) {
        tab[i].key = strndup(p, n);
        tab[i].len = n;
        tab[i].hash = h;
      }
      tab[i].count++;
    }
    p += n + 1;
  }
  for (size_t i = 0; i < TABLE; ++i)
    free(tab[i].key);
}

static void word_count(void) {
  make_text();
  section("Word count",
          "%.1f MB of space-separated words; count each distinct word in an "
          "open-addressing table; GB/s",
          text_len / 1e6);
  group_begin(NULL, GB_PER_S);
  THROUGHPUT("faf: next_token + hash, keys in a region", text_len, wc_faf());
  group_note("%zu keys, %.1f KB of region", wc_unique, wc_key_bytes / 1024.0);
  THROUGHPUT("libc: memchr + FNV-1a, strndup keys", text_len, wc_libc(false));
  THROUGHPUT("same, with faf_string_hash", text_len, wc_libc(true));
  free(text);
}

/* ---- 4. Formatting and parsing ---- */

#define NUMS 20000
#define FMT_BATCH 128 // results per region

static int64_t ints[NUMS];
static char int_text[NUMS][24];
static size_t int_text_len[NUMS];
static char dbl_text[NUMS][32];
static size_t dbl_text_len[NUMS];

static void make_numbers(void) {
  rng_state = 42;
  for (int i = 0; i < NUMS; ++i) {
    // every magnitude from 1 digit to 19, both signs
    int64_t v = (int64_t)(rng() >> (1 + rng() % 63));
    ints[i] = (rng() & 1) ? -v : v;
    int_text_len[i] = (size_t)snprintf(int_text[i], sizeof int_text[i],
                                       "%" PRId64, ints[i]);
    double d = (double)(int64_t)(rng() >> 20) / (double)(1 + rng() % 100000);
    dbl_text_len[i] =
        (size_t)snprintf(dbl_text[i], sizeof dbl_text[i], "%.*g",
                         (int)(3 + rng() % 15), (rng() & 1) ? -d : d);
  }
}

static void int_to_text_faf(void) {
  size_t acc = 0;
  for (int i = 0; i < NUMS; i += FMT_BATCH) {
    faf_region r = faf_region_acquire();
    for (int k = i; k < i + FMT_BATCH && k < NUMS; ++k)
      acc += faf_string_len(faf_string_from_i64(r, ints[k]));
    faf_region_release(r);
  }
  sink = acc;
}

static void int_to_text_libc(void) {
  size_t acc = 0;
  char buf[24];
  for (int i = 0; i < NUMS; ++i)
    acc += (size_t)snprintf(buf, sizeof buf, "%" PRId64, ints[i]);
  sink = acc;
}

static void record_faf(void) {
  size_t acc = 0;
  for (int i = 0; i < NUMS; i += FMT_BATCH) {
    faf_region r = faf_region_acquire();
    for (int k = i; k < i + FMT_BATCH && k < NUMS; ++k) {
      int v = (int)ints[k];
      acc += faf_string_len(
          faf_string_format(r, "%s=%d (0x%x)", "count", v, (unsigned)v));
    }
    faf_region_release(r);
  }
  sink = acc;
}

static void record_libc(void) {
  size_t acc = 0;
  char buf[64];
  for (int i = 0; i < NUMS; ++i) {
    int v = (int)ints[i];
    acc += (size_t)snprintf(buf, sizeof buf, "%s=%d (0x%x)", "count", v,
                            (unsigned)v);
  }
  sink = acc;
}

static void parse_int_faf(void) {
  int64_t acc = 0, v = 0;
  for (int i = 0; i < NUMS; ++i)
    if (faf_string_parse_i64(faf_string_init_n(int_text[i], int_text_len[i]),
                             &v))
      acc += v;
  sink = (size_t)acc;
}

static void parse_int_libc(void) {
  int64_t acc = 0;
  for (int i = 0; i < NUMS; ++i) {
    char *end;
    long long v = strtoll(int_text[i], &end, 10);
    if (*end == '\0')
      acc += v;
  }
  sink = (size_t)acc;
}

static void parse_dbl_faf(void) {
  double acc = 0, v = 0;
  for (int i = 0; i < NUMS; ++i)
    if (faf_string_parse_f64(faf_string_init_n(dbl_text[i], dbl_text_len[i]),
                             &v))
      acc += v;
  sink = (size_t)(int64_t)acc;
}

static void parse_dbl_libc(void) {
  double acc = 0;
  for (int i = 0; i < NUMS; ++i) {
    char *end;
    double v = strtod(dbl_text[i], &end);
    if (*end == '\0')
      acc += v;
  }
  sink = (size_t)(int64_t)acc;
}

// Inputs where faf_string_parse_f64 and strtod disagree (the faf parser is
// only guaranteed correctly rounded for common inputs).
static int parse_dbl_mismatches(void) {
  int bad = 0;
  for (int i = 0; i < NUMS; ++i) {
    double a = 0, b = strtod(dbl_text[i], NULL);
    if (!faf_string_parse_f64(faf_string_init_n(dbl_text[i], dbl_text_len[i]),
                              &a) ||
        a != b)
      bad++;
  }
  return bad;
}

static void format_parse(void) {
  make_numbers();
  section("Formatting and parsing",
          "%d numbers of every magnitude; ns per number", NUMS);
  group_begin("integer to text", NS_PER_OP);
  BENCH("faf_string_from_i64", NUMS, int_to_text_faf());
  BENCH("snprintf %lld", NUMS, int_to_text_libc());
  group_begin("format \"%s=%d (0x%x)\"", NS_PER_OP);
  BENCH("faf_string_format", NUMS, record_faf());
  BENCH("snprintf", NUMS, record_libc());
  group_begin("parse integer", NS_PER_OP);
  BENCH("faf_string_parse_i64", NUMS, parse_int_faf());
  BENCH("strtoll", NUMS, parse_int_libc());
  group_begin("parse double", NS_PER_OP);
  BENCH("faf_string_parse_f64", NUMS, parse_dbl_faf());
  int bad = parse_dbl_mismatches();
  if (bad)
    group_note("%d of %d differ from strtod in the last bits", bad, NUMS);
  BENCH("strtod", NUMS, parse_dbl_libc());
}

void bench_io(void) {
  make_csv();
  reading_lines();
  csv_transform();
  word_count();
  format_parse();
  group_end();
  unlink(csv_path);
  free(csv);
}
