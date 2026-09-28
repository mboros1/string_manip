#ifndef FAF_BACKEND_H
#define FAF_BACKEND_H

// Which kernel backend this build uses (see kernels/faf_kernels.h): exactly
// one of FAF_BACKEND_SSE2, FAF_BACKEND_NEON, FAF_BACKEND_SWAR and
// FAF_BACKEND_REF is defined.
//
//   sse2  x86 / x86-64
//   neon  AArch64 / ARMv7 with NEON
//   swar  any other 32- or 64-bit little-endian CPU: a machine word used as
//         4 or 8 byte lanes ("SIMD within a register")
//   ref   byte at a time: 8- and 16-bit CPUs, big-endian ones, or forced
//
// Force one with -DFAF_BACKEND_REF or -DFAF_BACKEND_SWAR.

#include <stdint.h>

#if defined(FAF_BACKEND_REF)
#define FAF_BACKEND_NAME "ref"
#elif defined(FAF_BACKEND_SWAR)
#define FAF_BACKEND_NAME "swar"
#elif defined(__SSE2__) || defined(_M_X64)
#define FAF_BACKEND_SSE2 1
#define FAF_BACKEND_NAME "sse2"
#elif defined(__ARM_NEON) || defined(__aarch64__)
#define FAF_BACKEND_NEON 1
#define FAF_BACKEND_NAME "neon"
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ &&  \
    UINTPTR_MAX >= 0xFFFFFFFFu
#define FAF_BACKEND_SWAR 1
#define FAF_BACKEND_NAME "swar"
#else
#define FAF_BACKEND_REF 1
#define FAF_BACKEND_NAME "ref"
#endif

#if defined(FAF_BACKEND_SWAR) &&                                              \
    !(defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#error "the SWAR kernels assume a little-endian CPU"
#endif

// Width of the backend's registers, in bytes (0 for ref): the unit the
// kernels work in, and the default slot size (faf_string_mem.h).
#if defined(FAF_BACKEND_REF)
#define FAF_VECTOR_BYTES 0
#elif defined(FAF_BACKEND_SWAR)
#if UINTPTR_MAX > 0xFFFFFFFFu
#define FAF_VECTOR_BYTES 8
#else
#define FAF_VECTOR_BYTES 4
#endif
#else
#define FAF_VECTOR_BYTES 16
#endif

// CPUs where unaligned 8- and 16-byte loads and stores are single, fast
// instructions. Elsewhere (the original ESP32, most microcontrollers) each
// unaligned access is split into bytes or becomes a library call.
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) ||             \
    defined(_M_IX86) || defined(__aarch64__) || defined(_M_ARM64) ||           \
    defined(__riscv_misaligned_fast)
#define FAF_FAST_UNALIGNED 1
#else
#define FAF_FAST_UNALIGNED 0
#endif

#endif // FAF_BACKEND_H
