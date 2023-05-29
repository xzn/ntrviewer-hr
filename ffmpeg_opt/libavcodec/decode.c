#include "ffmpeg_jls.h"
#include "jpegls.h"

#include <stdio.h>

int ffmpeg_jls_decode(uint8_t *dst, int dst_x, int dst_y, int dst_line, const uint8_t *src, int src_size, int bpp) {
    JLSState state = { 0 };
    state.bpp = bpp;
    ff_jpegls_reset_coding_parameters(&state, 0);
    ff_jpegls_init_state(&state);

    int ret, t;

    GetBitContext s;
    ret = init_get_bits8(&s, src, src_size);
    if (ret < 0)
    {
        return ret;
    }

    uint8_t *zero, *last, *cur;
    zero = calloc(dst_x, 1);
    if (!zero)
    {
        return AVERROR(ENOMEM);
    }
    last = zero;
    cur = dst;

    int i;
    t = 0;
    for (i = 0; i < dst_y; ++i) {
        ret = ls_decode_line(&state, &s, last, cur, t, dst_x);
        if (ret < 0)
        {
            fprintf(stderr, "ls_decode_line failed at col %d\n", i);
            free(zero);
            return ret;
        }
        t = last[0];
        last = cur;
        cur += dst_line;
    }

    free(zero);

    return dst_x * dst_y;
}
