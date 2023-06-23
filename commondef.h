#include <stdint.h>

#if !defined(HR_MALLOC) || !defined(HR_FREE) || !defined(HR_REALLOC)
#include <stdlib.h>
#define HR_MALLOC malloc
#define HR_FREE free
#define HR_REALLOC realloc
#endif

#define av_log(...)
#define av_assert0(...)
#define av_assert1(...)
#define av_assert2(...)
#define ff_dlog(...)

#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
#define FFMAX3(a,b,c) FFMAX(FFMAX(a,b),c)
#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
#define FFMIN3(a,b,c) FFMIN(FFMIN(a,b),c)

#define FFABS(a) ((a) >= 0 ? (a) : (-(a)))
#define FFSIGN(a) ((a) > 0 ? 1 : -1)

#define FFSWAP(type, a, b) \
    do                     \
    {                      \
        type SWAP_tmp = b; \
        b = a;             \
        a = SWAP_tmp;      \
    } while (0)

#define AV_WB32(p, val)                  \
    do                                   \
    {                                    \
        uint32_t d = (val);              \
        ((uint8_t *)(p))[3] = (d);       \
        ((uint8_t *)(p))[2] = (d) >> 8;  \
        ((uint8_t *)(p))[1] = (d) >> 16; \
        ((uint8_t *)(p))[0] = (d) >> 24; \
    } while (0)

#define AV_WBBUF AV_WB32
