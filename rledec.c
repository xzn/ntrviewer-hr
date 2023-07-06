#include "rlecodec.h"
#include "rledef.h"

#include <stdio.h>

int rle_decode(uint8_t *dst, int dst_size, const uint8_t *src, int src_size)
{
    const uint8_t *src_end = src + src_size, *dst_begin = dst, *dst_end = dst + dst_size, *src_begin = src;
    uint8_t count = 0, next = 0;

#define RLE_DECODE_CHECK_DST_SIZE \
    do                            \
    {                             \
        if (dst == dst_end)       \
        {                         \
            fprintf(stderr, "rle_decode failed at src %d (%d) (dst full)\n", src - src_begin, src_end - src); \
            return -2;            \
        }                         \
    } while (0)

#define RLE_DECODE_CHECK_SRC_SIZE \
    do                            \
    {                             \
        if (src == src_end)       \
        {                         \
            fprintf(stderr, "rle_decode failed at dst %d (out of src)\n", dst - dst_begin); \
            return -1;            \
        }                         \
    } while (0)

    while (src != src_end)
    {
        count = *src++;
        if (count < 128)
        {
            // run
            count += RLE_MIN_RUN;
            RLE_DECODE_CHECK_SRC_SIZE;
            next = *src++;
            while (count)
            {
                RLE_DECODE_CHECK_DST_SIZE;
                *dst++ = next;
                --count;
            }
        }
        else
        {
            // lit
            count += RLE_MIN_LIT + 128;
            while (count)
            {
                RLE_DECODE_CHECK_DST_SIZE;
                RLE_DECODE_CHECK_SRC_SIZE;
                *dst++ = *src++;
                --count;
            }
        }
    }

#undef RLE_DECODE_CHECK_SRC_SIZE
#undef RLE_DECODE_CHECK_DST_SIZE

    return dst - dst_begin;
}
