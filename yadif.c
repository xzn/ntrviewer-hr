#include "yadif.h"
#include <memory.h>

#include "commondef.h"

#define CHECK(j)                                                                                                                                                          \
    {                                                                                                                                                                     \
        int score = FFABS(cur[mrefs - 1 + (j)] - cur[prefs - 1 - (j)]) + FFABS(cur[mrefs + (j)] - cur[prefs - (j)]) + FFABS(cur[mrefs + 1 + (j)] - cur[prefs + 1 - (j)]); \
        if (score < spatial_score)                                                                                                                                        \
        {                                                                                                                                                                 \
            spatial_score = score;                                                                                                                                        \
            spatial_pred = (cur[mrefs + (j)] + cur[prefs - (j)]) >> 1;
#define CHECK_END \
    }             \
    }

/* The is_not_edge argument here controls when the code will enter a branch
 * which reads up to and including x-3 and x+3. */

#define FILTER(start, end, is_not_edge)                                                                                             \
    for (x = start; x < end; x++)                                                                                                   \
    {                                                                                                                               \
        int c = cur[mrefs];                                                                                                         \
        int d = cur[0];                                                                                                             \
        int e = cur[prefs];                                                                                                         \
        int temporal_diff1 = (FFABS(prev[mrefs] - c) + FFABS(prev[prefs] - e)) >> 1;                                                \
        int temporal_diff2 = (FFABS(next[mrefs] - c) + FFABS(next[prefs] - e)) >> 1;                                                \
        int diff = FFMAX(temporal_diff1, temporal_diff2);                                                                           \
        int spatial_pred = (c + e) >> 1;                                                                                            \
                                                                                                                                    \
        if (is_not_edge)                                                                                                            \
        {                                                                                                                           \
            int spatial_score = FFABS(cur[mrefs - 1] - cur[prefs - 1]) + FFABS(c - e) + FFABS(cur[mrefs + 1] - cur[prefs + 1]) - 1; \
            CHECK(-1)                                                                                                               \
            CHECK(-2)                                                                                                               \
            CHECK_END                                                                                                               \
            CHECK_END                                                                                                               \
            CHECK(1)                                                                                                                \
            CHECK(2)                                                                                                                \
            CHECK_END                                                                                                               \
            CHECK_END                                                                                                               \
        }                                                                                                                           \
                                                                                                                                    \
        if (spatial_pred > d + diff)                                                                                                \
            spatial_pred = d + diff;                                                                                                \
        else if (spatial_pred < d - diff)                                                                                           \
            spatial_pred = d - diff;                                                                                                \
                                                                                                                                    \
        dst[0] = spatial_pred;                                                                                                      \
                                                                                                                                    \
        dst++;                                                                                                                      \
        cur++;                                                                                                                      \
        prev++;                                                                                                                     \
        next++;                                                                                                                     \
    }

static void filter_line_c(uint8_t *dst,
                          const uint8_t *prev, const uint8_t *cur, const uint8_t *next,
                          int w, int prefs, int mrefs)
{
    int x;

    /* The function is called with the pointers already pointing to data[3] and
     * with 6 subtracted from the width.  This allows the FILTER macro to be
     * called so that it processes all the pixels normally.  A constant value of
     * true for is_not_edge lets the compiler ignore the if statement. */
    FILTER(0, w, 1)
}

#define MAX_ALIGN 1
static void filter_edges(uint8_t *dst, const uint8_t *prev, const uint8_t *cur, const uint8_t *next,
                         int w, int prefs, int mrefs)
{
    int x;

    const int edge = MAX_ALIGN - 1;
    int offset = FFMAX(w - edge, 3);

    /* Only edge pixels need to be processed here.  A constant value of false
     * for is_not_edge should let the compiler ignore the whole branch. */
    FILTER(0, FFMIN(3, w), 0)

    dst = dst + offset;
    prev = prev + offset;
    cur = cur + offset;
    next = next + offset;

    FILTER(offset, w - 3, 1)
    offset = FFMAX(offset, w - 3);
    FILTER(offset, w, 0)
}

typedef struct YadifData
{
    uint8_t *frame;
    const uint8_t *cur;
    const uint8_t *next;
    const uint8_t *prev;
    int w, h;
    int parity;
} YadifData;

static int filter_slice(YadifData *td)
{
    YadifData *s = td;
    int linesize = td->w;
    int refs = linesize;
    int df = 1;
    int pix_3 = 3 * df;
    int slice_start = 0;
    int slice_end = td->h;
    int y;
    int edge = 3 + MAX_ALIGN / df - 1;

    /* filtering reads 3 pixels to the left/right; to avoid invalid reads,
     * we need to call the c variant which avoids this for border pixels
     */
    for (y = slice_start; y < slice_end; y++)
    {
        if ((y ^ td->parity) & 1)
        {
            const uint8_t *prev = &s->prev[y * refs];
            const uint8_t *cur = &s->cur[y * refs];
            const uint8_t *next = &s->next[y * refs];
            uint8_t *dst = &td->frame[y * linesize];
            filter_line_c(dst + pix_3, prev + pix_3, cur + pix_3,
                          next + pix_3, td->w - edge,
                          y + 1 < td->h ? refs : -refs,
                          y ? -refs : refs);
            filter_edges(dst, prev, cur, next, td->w,
                         y + 1 < td->h ? refs : -refs,
                         y ? -refs : refs);
        }
        else
        {
            memcpy(&td->frame[y * linesize],
                   &s->cur[y * refs], td->w * df);
        }
    }
    return 0;
}

int yadif_filter(uint8_t *dst, const uint8_t *prev, const uint8_t *cur, const uint8_t *next, int w, int h, int parity)
{
    YadifData yadif = {
        .frame = dst,
        .cur = cur,
        .prev = prev,
        .next = next,
        .w = w,
        .h = h,
        .parity = parity,
    };
    return filter_slice(&yadif);
}
