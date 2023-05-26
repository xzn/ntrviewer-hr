#include "ffmpeg_jls.h"
#include "jpegls.h"
#include "get_bits.h"

int ffmpeg_jls_decode(uint8_t *dst, int dst_x, int dst_y, int dst_line, const uint8_t *src, int src_size, int bpp) {
    JLSState state = { 0 };
    state.bpp = bpp;
    ff_jpegls_reset_coding_parameters(&state, 0);
    ff_jpegls_init_state(&state);

    int ret, t;

    uint8_t *src_tmp;
    src_tmp = calloc(src_size, 1);

#if 0
    if (!src_tmp)
        return AVERROR(ENOMEM);
    const uint8_t *buf_end = src + src_size;

    PutBitContext pb;
    int b = 0;

    t = 0;
    while (src + t < buf_end) {
        uint8_t x = src[t++];
        if (x == 0xff) {
            while ((src + t < buf_end) && x == 0xff)
                x = src[t++];
            if (x & 0x80) {
                t -= FFMIN(2, t);
                break;
            }
        }
    }

    int bit_count = t * 8;
    init_put_bits(&pb, src_tmp, t);

    while (b < t) {
        uint8_t x = src[b++];
        put_bits(&pb, 8, x);
        if (x == 0xFF && b < t) {
            x = src[b++];
            if (x & 0x80) {
                av_log(NULL, AV_LOG_WARNING, "Invalid escape sequence\n");
                x &= 0x7f;
            }
            put_bits(&pb, 7, x);
            bit_count--;
        }
    }
    flush_put_bits(&pb);

    GetBitContext s;
    ret = init_get_bits(&s, src_tmp, bit_count);
    // av_log(NULL, AV_LOG_INFO, "size = %d bytes (%d bits)\n", (bit_count + 7) >> 3, bit_count);
    if (ret < 0)
    {
        free(src_tmp);
        return ret;
    }
#else
    GetBitContext s;
    ret = init_get_bits8(&s, src, src_size);
    if (ret < 0)
    {
        free(src_tmp);
        return ret;
    }
#endif

    uint8_t *zero, *last, *cur;
    zero = calloc(dst_x, 1);
    if (!zero)
    {
        free(src_tmp);
        return AVERROR(ENOMEM);
    }
    last = zero;
    cur = dst;

    int i;
    t = 0;
    for (i = 0; i < dst_y; ++i) {
        ret = ls_decode_line(&state, &s, last, cur, t, dst_x, 1, 0, 8);
        if (ret < 0)
        {
            av_log(NULL, AV_LOG_ERROR, "At line %d\n", i);
            free(src_tmp);
            free(zero);
            return ret;
        }
        t = last[0];
        last = cur;
        cur += dst_line;
    }

    free(src_tmp);
    free(zero);

    return dst_x * dst_y;
}
