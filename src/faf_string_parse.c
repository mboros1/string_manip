#include "faf_string_parse.h"
#include "kernels/faf_kernels.h"
#include "faf_string_build.h"

static inline bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Digits in [p, e) into *out; false if empty, not all digits, or overflow.
static bool parse_digits(const char *p, const char *e, uint64_t *out) {
  if (p == e)
    return false;
  uint64_t v = 0;
  for (; p < e; ++p) {
    if (!is_digit(*p))
      return false;
    unsigned d = (unsigned)(*p - '0');
    if (v > (UINT64_MAX - d) / 10)
      return false;
    v = v * 10 + d;
  }
  *out = v;
  return true;
}

bool faf_string_parse_u64(faf_string str, uint64_t *out) {
  const char *p = str.start;
  if (p < str.end && *p == '+')
    ++p;
  return parse_digits(p, str.end, out);
}

bool faf_string_parse_i64(faf_string str, int64_t *out) {
  const char *p = str.start;
  bool neg = false;
  if (p < str.end && (*p == '+' || *p == '-'))
    neg = *p++ == '-';
  uint64_t mag;
  if (!parse_digits(p, str.end, &mag))
    return false;
  if (neg) {
    if (mag > (uint64_t)INT64_MAX + 1)
      return false;
    *out = (int64_t)(0 - mag); // two's complement: covers INT64_MIN
  } else {
    if (mag > (uint64_t)INT64_MAX)
      return false;
    *out = (int64_t)mag;
  }
  return true;
}

// Exactly representable powers of ten.
static const double pow10_exact[] = {
    1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

bool faf_string_parse_f64(faf_string str, double *out) {
  const char *p = str.start, *e = str.end;
  bool neg = false;
  if (p < e && (*p == '+' || *p == '-'))
    neg = *p++ == '-';

  uint64_t mant = 0; // up to 19 significant digits
  int sig = 0;
  long exp10 = 0;
  bool any = false, truncated = false;

  for (; p < e && is_digit(*p); ++p) {
    any = true;
    if (sig < 19) {
      mant = mant * 10 + (uint64_t)(*p - '0');
      sig += mant != 0;
    } else {
      ++exp10; // integer digit past our precision
      truncated |= *p != '0';
    }
  }
  if (p < e && *p == '.') {
    ++p;
    for (; p < e && is_digit(*p); ++p) {
      any = true;
      if (sig < 19) {
        mant = mant * 10 + (uint64_t)(*p - '0');
        sig += mant != 0;
        --exp10;
      } else {
        truncated |= *p != '0';
      }
    }
  }
  if (!any)
    return false;

  if (p < e && (*p == 'e' || *p == 'E')) {
    ++p;
    bool eneg = false;
    if (p < e && (*p == '+' || *p == '-'))
      eneg = *p++ == '-';
    if (p == e)
      return false;
    long ev = 0;
    for (; p < e && is_digit(*p); ++p)
      if (ev < 100000)
        ev = ev * 10 + (*p - '0');
    exp10 += eneg ? -ev : ev;
  }
  if (p != e)
    return false;

  double v;
  if (mant == 0) {
    v = 0.0;
  } else if (!truncated && mant <= (1ull << 53) && exp10 >= -22 &&
             exp10 <= 22) {
    // Clinger's fast path: both operands exact, so one rounding
    v = (double)mant;
    v = exp10 < 0 ? v / pow10_exact[-exp10] : v * pow10_exact[exp10];
  } else {
    v = (double)mant;
    if (exp10 > 400)
      return false;
    for (long x = exp10; x > 0; x -= 22)
      v *= pow10_exact[x > 22 ? 22 : x];
    for (long x = exp10; x < 0; x += 22)
      v /= pow10_exact[-x > 22 ? 22 : -x];
  }
  if (v - v != 0.0) // inf
    return false;
  *out = neg ? -v : v;
  return true;
}

faf_string faf_string_from_i64(faf_region r, int64_t v) {
  faf_builder b = faf_builder_init(r);
  faf_builder_append_i64(&b, v);
  return faf_builder_finish(&b);
}

faf_string faf_string_from_u64(faf_region r, uint64_t v) {
  faf_builder b = faf_builder_init(r);
  faf_builder_append_u64(&b, v);
  return faf_builder_finish(&b);
}

bool faf_string_is_ascii(faf_string str) {
  size_t n = faf_string_len(str);
  return faf_k_ascii_prefix(str.start, n) == n;
}

bool faf_string_utf8_valid(faf_string str) {
  const unsigned char *s = (const unsigned char *)str.start;
  size_t n = faf_string_len(str);
  size_t i = 0;
  while (i < n) {
    i += faf_k_ascii_prefix((const char *)s + i, n - i);
    if (i == n)
      return true;

    unsigned char c = s[i];
    size_t need;
    uint32_t cp, min;
    if (c >= 0xC2 && c <= 0xDF) {
      need = 1, cp = c & 0x1F, min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
      need = 2, cp = c & 0x0F, min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
      need = 3, cp = c & 0x07, min = 0x10000;
    } else {
      return false; // continuation byte, 0xC0/0xC1, or > 0xF4
    }
    if (n - i - 1 < need)
      return false;
    for (size_t k = 1; k <= need; ++k) {
      unsigned char cc = s[i + k];
      if ((cc & 0xC0) != 0x80)
        return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
      return false;
    i += need + 1;
  }
  return true;
}
