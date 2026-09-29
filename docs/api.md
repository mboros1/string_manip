# API

Everything works on bytes: case functions are ASCII only, and
`faf_string_utf8_valid` is the only UTF-8 aware function.

Include `faf.h` for everything, or the individual headers:

| Header | What's in it |
|---|---|
| `faf_string.h` | `faf_string` (a start/end pointer pair, passed by value), `faf_string_init` |
| `faf_string_mem.h` | Region allocator: `faf_region_acquire` / `faf_reserve` / `faf_region_release`, arenas over caller memory (`faf_arena_init` / `faf_arena_acquire`), `faf_string_copy`, `faf_memcpy` / `faf_memset` |
| `faf_string_search.h` | `eq`, `starts_with`, `ends_with`, `find` / `rfind` (substring or char), `contains`, `count`, `find_any` |
| `faf_string_view.h` | `slice`, `trim` / `ltrim` / `rtrim`, `next_token` (split iterator), `split_once` |
| `faf_string_strsplit.h` | `split` (array of views), `split_owned` (copies, NUL terminated tokens) |
| `faf_string_build.h` | `faf_builder` (grows in place in its region), `join`, `repeat`, `pad_left` / `pad_right`, `replace`, `reverse`, `format` |
| `faf_string_case.h` | `to_lower`, `to_upper`, `eq_icase`, `cmp_icase` |
| `faf_string_cmp.h`, `faf_string_concat.h` | `cmp` (unsigned bytes, like `memcmp`), `concat` |
| `faf_string_parse.h` | `parse_i64` / `parse_u64` / `parse_f64`, `from_i64` / `from_u64`, `is_ascii`, `utf8_valid` |
| `faf_string_ring.h` | `faf_ring`: a ring of strings over your buffer; old strings are overwritten, and handles detect it (`push`, `get`, `valid`) |
| `faf_string_hash.h`, `faf_string_sort.h` | 64 bit `hash`, `sort_chars`, `arr_sort` |

Functions that allocate take a `faf_region` and return `FAF_STRING_NONE` when the region is out of space. Everything allocated from a region is freed at once by `faf_region_release`.

## Source layout

| Directory | What's in it |
|---|---|
| `src/` | the library: sources and headers together |
| `src/kernels/` | the per-architecture byte kernels ([backends.md](backends.md)) |
| `tests/` | one test program per module, and the test framework |
| `tests/esp32/` | an ESP-IDF app that runs every test suite on an ESP32 (`make esp32_test`) |
| `bench/` | benchmarks (`make bench`) |
| `tools/` | `gen_compile_commands.py`, random test data generators |
| `experiments/` | standalone experiments, not part of the library (the simde ones, a C port of pdqsort) |


To use the library, compile `src/*.c` and `src/kernels/*.c` (no include paths needed) and add `-Isrc` to your own code.
