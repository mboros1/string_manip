#ifndef FAF_BACKEND_H
#define FAF_BACKEND_H

// Which kernel backend this build uses (see kernels/faf_kernels.h): exactly
// one of FAF_BACKEND_SSE2, FAF_BACKEND_NEON, FAF_BACKEND_REF is defined.
// Force the portable one with -DFAF_BACKEND_REF.

#if defined(FAF_BACKEND_REF)
#define FAF_BACKEND_NAME "ref"
#elif defined(__SSE2__) || defined(_M_X64)
#define FAF_BACKEND_SSE2 1
#define FAF_BACKEND_NAME "sse2"
#elif defined(__ARM_NEON) || defined(__aarch64__)
#define FAF_BACKEND_NEON 1
#define FAF_BACKEND_NAME "neon"
#else
#define FAF_BACKEND_REF 1
#define FAF_BACKEND_NAME "ref"
#endif

// Width of the backend's vector registers, in bytes (0 for ref).
#if defined(FAF_BACKEND_REF)
#define FAF_VECTOR_BYTES 0
#else
#define FAF_VECTOR_BYTES 16
#endif

#endif // FAF_BACKEND_H
