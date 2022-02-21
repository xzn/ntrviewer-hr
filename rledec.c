#include "rlecodec.h"
#include "rledef.h"

int rle_decode(uint8_t *dst, int dst_size, const uint8_t *src, int src_size)
{
    const uint8_t *src_end = src + src_size, *dst_begin = dst, *dst_end = dst + dst_size;
    uint8_t count = 0, next = 0;

#define RLE_DECODE_CHECK_DST_SIZE \
    do                            \
    {                             \
        if (dst == dst_end)       \
        {                         \
            return -2;            \
        }                         \
    } while (0)

#define RLE_DECODE_CHECK_SRC_SIZE \
    do                            \
    {                             \
        if (src == src_end)       \
        {                         \
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
