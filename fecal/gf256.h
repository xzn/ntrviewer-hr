/** \file
    \brief GF(256) Main C API Header
    \copyright Copyright (c) 2017 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of GF256 nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef CAT_GF256_H
#define CAT_GF256_H

/** \page GF256 GF(256) Math Module

    This module provides efficient implementations of bulk
    GF(2^^8) math operations over memory buffers.

    Addition is done over the base field in GF(2) meaning
    that addition is XOR between memory buffers.

    Multiplication is performed using table lookups via
    SIMD instructions.  This is somewhat slower than XOR,
    but fast enough to not become a major bottleneck when
    used sparingly.
*/

#include <stdint.h> // uint32_t etc
#include <string.h> // memcpy, memset

/// Library header version
#define GF256_VERSION 2

//------------------------------------------------------------------------------
// Platform/Architecture

#if defined(ANDROID) || defined(IOS) || defined(LINUX_ARM) || defined(__powerpc__) || defined(__s390__)
    #define GF256_TARGET_MOBILE
#endif // ANDROID

#if defined(__AVX2__) || (defined (_MSC_VER) && _MSC_VER >= 1900)
    #define GF256_TRY_AVX2 /* 256-bit */
    #include <immintrin.h>
    // #define GF256_ALIGN_BYTES 32
#else // __AVX2__
    // #define GF256_ALIGN_BYTES 16
#endif // __AVX2__

#define GF256_ALIGN_BYTES 32

#if !defined(GF256_TARGET_MOBILE)
    // Note: MSVC currently only supports SSSE3 but not AVX2
    #include <tmmintrin.h> // SSSE3: _mm_shuffle_epi8
    #include <emmintrin.h> // SSE2
#endif // GF256_TARGET_MOBILE

#if defined(HAVE_ARM_NEON_H)
    #include <arm_neon.h>
#endif // HAVE_ARM_NEON_H

#if defined(GF256_TARGET_MOBILE)

    // #define GF256_ALIGNED_ACCESSES /* Inputs must be aligned to GF256_ALIGN_BYTES */

# if defined(HAVE_ARM_NEON_H)
    // Compiler-specific 128-bit SIMD register keyword
    #define GF256_M128 uint8x16_t
    #define GF256_TRY_NEON
#else
    #define GF256_M128 uint64_t
# endif

#else // GF256_TARGET_MOBILE

    // Compiler-specific 128-bit SIMD register keyword
    #define GF256_M128 __m128i

#endif // GF256_TARGET_MOBILE

#define GF256_ALIGNED_ACCESSES

#ifdef GF256_TRY_AVX2
    // Compiler-specific 256-bit SIMD register keyword
    #define GF256_M256 __m256i
#endif

// Compiler-specific C++11 restrict keyword
#define GF256_RESTRICT __restrict

// Compiler-specific force inline keyword
#ifdef _MSC_VER
    #define GF256_FORCE_INLINE inline __forceinline
#else
    #define GF256_FORCE_INLINE inline __attribute__((always_inline))
#endif

// Compiler-specific alignment keyword
// Note: Alignment only matters for ARM NEON where it should be 16
#ifdef _MSC_VER
    #define GF256_ALIGNED __declspec(align(GF256_ALIGN_BYTES))
#else // _MSC_VER
    #define GF256_ALIGNED __attribute__((aligned(GF256_ALIGN_BYTES)))
#endif // _MSC_VER

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


//------------------------------------------------------------------------------
// Portability

/// Swap two memory buffers in-place
// extern void gf256_memswap(void * GF256_RESTRICT vx, void * GF256_RESTRICT vy, int bytes);


//------------------------------------------------------------------------------
// GF(256) Context

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4324) // warning C4324: 'gf256_ctx' : structure was padded due to __declspec(align())
#endif // _MSC_VER

/// The context object stores tables required to perform library calculations
typedef struct gf256_ctx
{
    /// Mul/Div/Inv/Sqr tables
    uint8_t GF256_MUL_TABLE[256 * 256];
    uint8_t GF256_DIV_TABLE[256 * 256];
    uint8_t GF256_INV_TABLE[256];
    uint8_t GF256_SQR_TABLE[256];
} gf256_ctx;

#ifdef _MSC_VER
    #pragma warning(pop)
#endif // _MSC_VER

#define GF_NAME_SUFFIX_INNER(name, suffix) name ## suffix
#define GF_NAME_SUFFIX(name, suffix) GF_NAME_SUFFIX_INNER(name, suffix)

#define GF_NAME(name) GF_NAME_SUFFIX(name, GF_SUFFIX)

#ifndef GF_SUFFIX
#define GF_SUFFIX _mobile
#define GF_IMPL_DEFAULT
#endif

extern gf256_ctx GF256Ctx;
extern bool CpuHasAVX2;
extern bool CpuHasSSSE3;

static void _cpuid(unsigned int cpu_info[4U], const unsigned int cpu_info_type)
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64) || defined(_M_IX86))
    __cpuid((int *) cpu_info, cpu_info_type);
#else //if defined(HAVE_CPUID)
    cpu_info[0] = cpu_info[1] = cpu_info[2] = cpu_info[3] = 0;
# ifdef __i386__
    __asm__ __volatile__ ("pushfl; pushfl; "
                          "popl %0; "
                          "movl %0, %1; xorl %2, %0; "
                          "pushl %0; "
                          "popfl; pushfl; popl %0; popfl" :
                          "=&r" (cpu_info[0]), "=&r" (cpu_info[1]) :
                          "i" (0x200000));
    if (((cpu_info[0] ^ cpu_info[1]) & 0x200000) == 0) {
        return; /* LCOV_EXCL_LINE */
    }
# endif
# ifdef __i386__
    __asm__ __volatile__ ("xchgl %%ebx, %k1; cpuid; xchgl %%ebx, %k1" :
                          "=a" (cpu_info[0]), "=&r" (cpu_info[1]),
                          "=c" (cpu_info[2]), "=d" (cpu_info[3]) :
                          "0" (cpu_info_type), "2" (0U));
# elif defined(__x86_64__)
    __asm__ __volatile__ ("xchgq %%rbx, %q1; cpuid; xchgq %%rbx, %q1" :
                          "=a" (cpu_info[0]), "=&r" (cpu_info[1]),
                          "=c" (cpu_info[2]), "=d" (cpu_info[3]) :
                          "0" (cpu_info_type), "2" (0U));
# else
    __asm__ __volatile__ ("cpuid" :
                          "=a" (cpu_info[0]), "=b" (cpu_info[1]),
                          "=c" (cpu_info[2]), "=d" (cpu_info[3]) :
                          "0" (cpu_info_type), "2" (0U));
# endif
#endif
}

#define CPUID_EBX_AVX2    0x00000020
#define CPUID_ECX_SSSE3   0x00000200

static void gf256_architecture_init()
{
#if defined(GF256_TRY_NEON)

    // Check for NEON support on Android platform
#if defined(HAVE_ANDROID_GETCPUFEATURES)
    AndroidCpuFamily family = android_getCpuFamily();
    if (family == ANDROID_CPU_FAMILY_ARM)
    {
        if (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON)
            CpuHasNeon = true;
    }
    else if (family == ANDROID_CPU_FAMILY_ARM64)
    {
        CpuHasNeon = true;
        if (android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_ASIMD)
            CpuHasNeon64 = true;
    }
#endif

#if defined(LINUX_ARM)
    // Check for NEON support on other ARM/Linux platforms
    checkLinuxARMNeonCapabilities(CpuHasNeon);
#endif

#endif //GF256_TRY_NEON

// #if !defined(GF256_TARGET_MOBILE)
    unsigned int cpu_info[4];

    _cpuid(cpu_info, 1);
    CpuHasSSSE3 = ((cpu_info[2] & CPUID_ECX_SSSE3) != 0);

// #if defined(GF256_TRY_AVX2)
    _cpuid(cpu_info, 7);
    CpuHasAVX2 = ((cpu_info[1] & CPUID_EBX_AVX2) != 0);
// #endif // GF256_TRY_AVX2

    // When AVX2 and SSSE3 are unavailable, Siamese takes 4x longer to decode
    // and 2.6x longer to encode.  Encoding requires a lot more simple XOR ops
    // so it is still pretty fast.  Decoding is usually really quick because
    // average loss rates are low, but when needed it requires a lot more
    // GF multiplies requiring table lookups which is slower.

// #endif // GF256_TARGET_MOBILE
}

#define GF_DECL(SUFFIX) \
    extern int GF_NAME_SUFFIX(gf256_init_, SUFFIX)(int version); \
    extern void GF_NAME_SUFFIX(gf256_add_mem, SUFFIX)(void * GF256_RESTRICT vx, const void * GF256_RESTRICT vy, int bytes); \
    extern void GF_NAME_SUFFIX(gf256_add2_mem, SUFFIX)(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx, const void * GF256_RESTRICT vy, int bytes); \
    extern void GF_NAME_SUFFIX(gf256_addset_mem, SUFFIX)(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx, const void * GF256_RESTRICT vy, int bytes); \
    extern void GF_NAME_SUFFIX(gf256_mul_mem, SUFFIX)(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx, uint8_t y, int bytes); \
    extern void GF_NAME_SUFFIX(gf256_muladd_mem, SUFFIX)(void * GF256_RESTRICT vz, uint8_t y, const void * GF256_RESTRICT vx, int bytes); \
    extern void GF_NAME_SUFFIX(gf256_memswap, SUFFIX)(void * GF256_RESTRICT vx, void * GF256_RESTRICT vy, int bytes);

GF_DECL(_mobile)
GF_DECL(_ssse3)
GF_DECL(_avx2)
GF_DECL(_ssse3_avx2)

//------------------------------------------------------------------------------
// Initialization

/**
    Initialize a context, filling in the tables.

    Thread-safety / Usage Notes:

    It is perfectly safe and encouraged to use a gf256_ctx object from multiple
    threads.  The gf256_init() is relatively expensive and should only be done
    once, though it will take less than a millisecond.

    The gf256_ctx object must be aligned to 16 byte boundary.
    Simply tag the object with GF256_ALIGNED to achieve this.

    Example:
       static GF256_ALIGNED gf256_ctx TheGF256Context;
       gf256_init(&TheGF256Context, 0);

    Returns 0 on success and other values on failure.
*/
static GF256_FORCE_INLINE int gf256_init()
{
    gf256_architecture_init();
    if (CpuHasAVX2 && CpuHasSSSE3) {
        return gf256_init__ssse3_avx2(GF256_VERSION);
    } else if (CpuHasAVX2) {
        return gf256_init__avx2(GF256_VERSION);
    } else if (CpuHasSSSE3) {
        return gf256_init__ssse3(GF256_VERSION);
    } else {
        return gf256_init__mobile(GF256_VERSION);
    }
}

#define gf256_init_ GF_NAME(gf256_init_)

//------------------------------------------------------------------------------
// Math Operations

/// return x + y
static GF256_FORCE_INLINE uint8_t gf256_add(uint8_t x, uint8_t y)
{
    return (uint8_t)(x ^ y);
}

/// return x * y
/// For repeated multiplication by a constant, it is faster to put the constant in y.
static GF256_FORCE_INLINE uint8_t gf256_mul(uint8_t x, uint8_t y)
{
    return GF256Ctx.GF256_MUL_TABLE[((unsigned)y << 8) + x];
}

/// return x / y
/// Memory-access optimized for constant divisors in y.
static GF256_FORCE_INLINE uint8_t gf256_div(uint8_t x, uint8_t y)
{
    return GF256Ctx.GF256_DIV_TABLE[((unsigned)y << 8) + x];
}

/// return 1 / x
static GF256_FORCE_INLINE uint8_t gf256_inv(uint8_t x)
{
    return GF256Ctx.GF256_INV_TABLE[x];
}

/// return x * x
static GF256_FORCE_INLINE uint8_t gf256_sqr(uint8_t x)
{
    return GF256Ctx.GF256_SQR_TABLE[x];
}

//------------------------------------------------------------------------------
// Bulk Memory Math Operations

/// Performs "x[] += y[]" bulk memory XOR operation
static GF256_FORCE_INLINE void gf256_add_mem(void * GF256_RESTRICT vx, const void * GF256_RESTRICT vy, int bytes)
{
    if (CpuHasAVX2 && CpuHasSSSE3) {
        gf256_add_mem_ssse3_avx2(vx, vy, bytes);
    } else if (CpuHasAVX2) {
        gf256_add_mem_avx2(vx, vy, bytes);
    } else if (CpuHasSSSE3) {
        gf256_add_mem_ssse3(vx, vy, bytes);
    } else {
        gf256_add_mem_mobile(vx, vy, bytes);
    }
}

/// Performs "z[] += x[] + y[]" bulk memory operation
static GF256_FORCE_INLINE void gf256_add2_mem(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx, const void * GF256_RESTRICT vy, int bytes)
{
    if (CpuHasAVX2 && CpuHasSSSE3) {
        gf256_add2_mem_ssse3_avx2(vz, vx, vy, bytes);
    } else if (CpuHasAVX2) {
        gf256_add2_mem_avx2(vz, vx, vy, bytes);
    } else if (CpuHasSSSE3) {
        gf256_add2_mem_ssse3(vz, vx, vy, bytes);
    } else {
        gf256_add2_mem_mobile(vz, vx, vy, bytes);
    }
}

/// Performs "z[] = x[] + y[]" bulk memory operation
static GF256_FORCE_INLINE void gf256_addset_mem(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx, const void * GF256_RESTRICT vy, int bytes)
{
    if (CpuHasAVX2 && CpuHasSSSE3) {
        gf256_addset_mem_ssse3_avx2(vz, vx, vy, bytes);
    } else if (CpuHasAVX2) {
        gf256_addset_mem_avx2(vz, vx, vy, bytes);
    } else if (CpuHasSSSE3) {
        gf256_addset_mem_ssse3(vz, vx, vy, bytes);
    } else {
        gf256_addset_mem_mobile(vz, vx, vy, bytes);
    }
}

/// Performs "z[] = x[] * y" bulk memory operation
static GF256_FORCE_INLINE void gf256_mul_mem(void * GF256_RESTRICT vz, const void * GF256_RESTRICT vx, uint8_t y, int bytes)
{
    if (CpuHasAVX2 && CpuHasSSSE3) {
        gf256_mul_mem_ssse3_avx2(vz, vx, y, bytes);
    } else if (CpuHasAVX2) {
        gf256_mul_mem_avx2(vz, vx, y, bytes);
    } else if (CpuHasSSSE3) {
        gf256_mul_mem_ssse3(vz, vx, y, bytes);
    } else {
        gf256_mul_mem_mobile(vz, vx, y, bytes);
    }
}

/// Performs "z[] += x[] * y" bulk memory operation
static GF256_FORCE_INLINE void gf256_muladd_mem(void * GF256_RESTRICT vz, uint8_t y, const void * GF256_RESTRICT vx, int bytes)
{
    if (CpuHasAVX2 && CpuHasSSSE3) {
        gf256_muladd_mem_ssse3_avx2(vz, y, vx, bytes);
    } else if (CpuHasAVX2) {
        gf256_muladd_mem_avx2(vz, y, vx, bytes);
    } else if (CpuHasSSSE3) {
        gf256_muladd_mem_ssse3(vz, y, vx, bytes);
    } else {
        gf256_muladd_mem_mobile(vz, y, vx, bytes);
    }
}

/// Performs "x[] /= y" bulk memory operation
static GF256_FORCE_INLINE void gf256_div_mem(void * GF256_RESTRICT vz,
                                             const void * GF256_RESTRICT vx, uint8_t y, int bytes)
{
    // Multiply by inverse
    gf256_mul_mem(vz, vx, y == 1 ? (uint8_t)1 : GF256Ctx.GF256_INV_TABLE[y], bytes);
}


//------------------------------------------------------------------------------
// Misc Operations

/// Swap two memory buffers in-place
static GF256_FORCE_INLINE void gf256_memswap(void * GF256_RESTRICT vx, void * GF256_RESTRICT vy, int bytes)
{
    if (CpuHasAVX2 && CpuHasSSSE3) {
        gf256_memswap_ssse3_avx2(vx, vy, bytes);
    } else if (CpuHasAVX2) {
        gf256_memswap_avx2(vx, vy, bytes);
    } else if (CpuHasSSSE3) {
        gf256_memswap_ssse3(vx, vy, bytes);
    } else {
        gf256_memswap_mobile(vx, vy, bytes);
    }
}

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CAT_GF256_H
