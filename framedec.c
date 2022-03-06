#include "framecodec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#define HR_MAX(a, b) ((a) > (b) ? (a) : (b))
#define HR_MIN(a, b) ((a) < (b) ? (a) : (b))

static int frame_decode_interlaced;
static int frame_decode_yadif;

static uint8_t accessImageNoCheck(const uint8_t *image, int x, int y, int w, int h)
{
    return image[x * h + y];
}

static inline uint8_t medianOf3_uint8_t(uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t max = a > b ? a : b;
    max = max > c ? max : c;

    uint8_t min = a < b ? a : b;
    min = min < c ? min : c;

    return a + b + c - max - min;
}

static inline uint8_t predictPixel(const uint8_t *image, int x, int y, int w, int h)
{
    if (x == 0 && y == 0)
    {
        return 0;
    }

    if (x == 0)
    {
        return accessImageNoCheck(image, x, y - 1, w, h);
    }

    if (y == 0)
    {
        return accessImageNoCheck(image, x - 1, y, w, h);
    }

    uint8_t t = accessImageNoCheck(image, x, y - 1, w, h);
    uint8_t l = accessImageNoCheck(image, x - 1, y, w, h);
    uint8_t tl = accessImageNoCheck(image, x - 1, y - 1, w, h);

    return medianOf3_uint8_t(t, l, t + l - tl);
}

static inline uint8_t accessDelta(const uint8_t *im, const uint8_t *im_pf, int x, int y, int w, int h)
{
    return accessImageNoCheck(im, x, y, w, h) - accessImageNoCheck(im_pf, x, y, w, h);
}

static inline uint8_t predictPixelDelta(const uint8_t *im, const uint8_t *im_pf, int x, int y, int w, int h)
{
    if (x == 0 && y == 0)
    {
        return 0;
    }

    if (x == 0)
    {
        return accessDelta(im, im_pf, x, y - 1, w, h);
    }

    if (y == 0)
    {
        return accessDelta(im, im_pf, x - 1, y, w, h);
    }

    uint8_t t = accessDelta(im, im_pf, x, y - 1, w, h);
    uint8_t l = accessDelta(im, im_pf, x - 1, y, w, h);
    uint8_t tl = accessDelta(im, im_pf, x - 1, y - 1, w, h);

    return medianOf3_uint8_t(t, l, t + l - tl);
}

static inline void predictImage(uint8_t *dst, const uint8_t *src, int w, int h)
{
    const uint8_t *dst_begin = dst;
    for (int i = 0; i < w; ++i)
    {
        for (int j = 0; j < h; ++j)
        {
            *dst++ = *src++ + predictPixel(dst_begin, i, j, w, h);
        }
    }
}

static inline uint8_t accessImage(const uint8_t *image, int x, int y, int w, int h)
{
    return accessImageNoCheck(image, HR_MAX(HR_MIN(x, w - 1), 0), HR_MAX(HR_MIN(y, h - 1), 0), w, h);
}

static inline uint16_t accessImageUpsampleUnscaled(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
    int ds_w = wOrig / 2;
    int ds_h = hOrig / 2;

    int ds_x0 = xOrig / 2;
    int ds_x1 = ds_x0;
    int ds_y0 = yOrig / 2;
    int ds_y1 = ds_y0;

    if (xOrig > ds_x0 * 2)
    { // xOrig is odd -> ds_x0 * 2 + 1 = xOrig = ds_x1 * 2 - 1
        ++ds_x1;
    }
    else
    { // xOrig is even -> ds_x0 * 2 + 2 = xOrig = ds_x1 * 2
        --ds_x0;
    }

    if (yOrig > ds_y0 * 2)
    {
        ++ds_y1;
    }
    else
    {
        --ds_y0;
    }

    uint16_t a = accessImage(ds_image, ds_x0, ds_y0, ds_w, ds_h);
    uint16_t b = accessImage(ds_image, ds_x1, ds_y0, ds_w, ds_h);
    uint16_t c = accessImage(ds_image, ds_x0, ds_y1, ds_w, ds_h);
    uint16_t d = accessImage(ds_image, ds_x1, ds_y1, ds_w, ds_h);

    if (xOrig < ds_x1 * 2)
    {
        a = (a * 3 + b);
        c = (c * 3 + d);
    }
    else
    {
        a = (a + b * 3);
        c = (c + d * 3);
    }

    if (yOrig < ds_y1 * 2)
    {
        a = (a * 3 + c);
    }
    else
    {
        a = (a + c * 3);
    }

    return a;
}

// #define rshift_to_even(n, s) ({ typeof(n) n_ = n >> (s - 1); uint8_t b_ = n_ & 1; n_ >>= 1; uint8_t c_ = n_ & 1; n_ + (b_ & c_); })
#define rshift_to_even(n, s) ((n + (s > 1 ? (1 << (s - 1)) : 0)) >> s)
// #define rshift_to_even(n, s) ((n + (1 << (s - 1))) >> s)

static inline uint8_t accessImageUpsample(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
    uint16_t p = accessImageUpsampleUnscaled(ds_image, xOrig, yOrig, wOrig, hOrig);
    return rshift_to_even(p, 4);
}

static inline void upsampleImage(uint8_t *dst, const uint8_t *ds_src, int w, int h)
{
    int i = 0, j = 0;
    for (; i < w; ++i)
    {
        j = 0;
        for (; j < h; ++j)
        {
            *dst++ = accessImageUpsample(ds_src, i, j, w, h);
        }
    }
}

// x and y are % 2 == 0
static inline uint16_t accessImageDownsampleUnscaled(const uint8_t *image, int x, int y, int w, int h)
{
    int x0 = x; // / 2 * 2;
    int x1 = x0 + 1;
    int y0 = y; // / 2 * 2;
    int y1 = y0 + 1;

    // x1 = x1 >= w ? w - 1 : x1;
    // y1 = y1 >= h ? h - 1 : y1;

    uint8_t a = accessImageNoCheck(image, x0, y0, w, h);
    uint8_t b = accessImageNoCheck(image, x1, y0, w, h);
    uint8_t c = accessImageNoCheck(image, x0, y1, w, h);
    uint8_t d = accessImageNoCheck(image, x1, y1, w, h);

    return (uint16_t)a + b + c + d;
}

// x and y are % 2 == 0, see accessImageDownsampleUnscaled
static inline uint8_t accessImageDownsample(const uint8_t *image, int x, int y, int w, int h)
{
    uint16_t p = accessImageDownsampleUnscaled(image, x, y, w, h);
    return rshift_to_even(p, 2);
}

static inline void downsampleImage(uint8_t *ds_dst, const uint8_t *src, int wOrig, int hOrig)
{
    int i = 0, j = 0;
    for (; i < wOrig; i += 2)
    {
        j = 0;
        for (; j < hOrig; j += 2)
        {
            *ds_dst++ = accessImageDownsample(src, i, j, wOrig, hOrig);
        }
    }
}

// x % 2 == 0
static uint8_t accessImageDownsampleH(const uint8_t *image, int x, int y, int w, int h)
{
    int x0 = x; // / 2 * 2;
    int x1 = x0 + 1;

    // x1 = x1 >= w ? w - 1 : x1;
    // y1 = y1 >= h ? h - 1 : y1;

    uint8_t a = accessImageNoCheck(image, x0, y, w, h);
    uint8_t b = accessImageNoCheck(image, x1, y, w, h);

    uint16_t c = a + b;
    return rshift_to_even(c, 1);
}

// y % 2 == 0
static uint8_t accessImageDownsampleV(const uint8_t *image, int x, int y, int w, int h)
{
    int y0 = y; // / 2 * 2;
    int y1 = y0 + 1;

    // x1 = x1 >= w ? w - 1 : x1;
    // y1 = y1 >= h ? h - 1 : y1;

    uint8_t a = accessImageNoCheck(image, x, y0, w, h);
    uint8_t b = accessImageNoCheck(image, x, y1, w, h);

    uint16_t c = a + b;
    return rshift_to_even(c, 1);
}

static void downsampleImageH(uint8_t *ds_dst, const uint8_t *src, int wOrig, int hOrig)
{
    int i = 0, j = 0;
    for (; i < wOrig; i += 2)
    {
        j = 0;
        for (; j < hOrig; ++j)
        {
            *ds_dst++ = accessImageDownsampleH(src, i, j, wOrig, hOrig);
        }
    }
}

static void downsampleImageV(uint8_t *ds_dst, const uint8_t *src, int wOrig, int hOrig)
{
    int i = 0, j = 0;
    for (; i < wOrig; ++i)
    {
        j = 0;
        for (; j < hOrig; j += 2)
        {
            *ds_dst++ = accessImageDownsampleV(src, i, j, wOrig, hOrig);
        }
    }
}

static inline void differenceImage(uint8_t *dst, const uint8_t *fd_src, const uint8_t *src_prev, int w, int h)
{
    uint8_t *dst_end = dst + w * h;
    while (dst != dst_end)
    {
        *dst++ = *fd_src++ + *src_prev++;
    }
}

static inline void differenceFromDownsampled(uint8_t *dst, const uint8_t *fd_src, const uint8_t *ds_src_prev, int w, int h)
{
    int i = 0, j = 0;
    for (; i < w; ++i)
    {
        j = 0;
        for (; j < h; ++j)
        {
            *dst++ = *fd_src++ + accessImageUpsample(ds_src_prev, i, j, w, h);
        }
    }
}

static inline void downsampledDifference(uint8_t *ds_dst, const uint8_t *fd_ds_src, const uint8_t *src_prev, int w, int h)
{
    int i = 0, j = 0;
    for (; i < w; i += 2)
    {
        j = 0;
        for (; j < h; j += 2)
        {
            *ds_dst++ = *fd_ds_src++ + accessImageDownsample(src_prev, i, j, w, h);
        }
    }
}

static uint8_t accessImageUpsampleH(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
    int ds_w = wOrig / 2;

    int ds_x0 = xOrig / 2;
    int ds_x1 = ds_x0;

    if (xOrig > ds_x0 * 2)
    { // xOrig is odd -> ds_x0 * 2 + 1 = xOrig = ds_x1 * 2 - 1
        ++ds_x1;
    }
    else
    { // xOrig is even -> ds_x0 * 2 + 2 = xOrig = ds_x1 * 2
        --ds_x0;
    }

    uint16_t a = accessImage(ds_image, ds_x0, yOrig, ds_w, hOrig);
    uint16_t b = accessImage(ds_image, ds_x1, yOrig, ds_w, hOrig);

    if (xOrig < ds_x1 * 2)
    {
        a = (a * 3 + b);
    }
    else
    {
        a = (a + b * 3);
    }

    return rshift_to_even(a, 2);
}

static uint8_t accessImageUpsampleV(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
    int ds_h = hOrig / 2;

    int ds_y0 = yOrig / 2;
    int ds_y1 = ds_y0;

    if (yOrig > ds_y0 * 2)
    {
        ++ds_y1;
    }
    else
    {
        --ds_y0;
    }

    uint16_t a = accessImage(ds_image, xOrig, ds_y0, wOrig, ds_h);
    uint16_t b = accessImage(ds_image, xOrig, ds_y1, wOrig, ds_h);

    if (yOrig < ds_y1 * 2)
    {
        a = (a * 3 + b);
    }
    else
    {
        a = (a + b * 3);
    }

    return rshift_to_even(a, 2);
}

static void upsampleImageH(uint8_t *dst, const uint8_t *ds_src, int w, int h)
{
    int i = 0, j = 0;
    for (; i < w; ++i)
    {
        j = 0;
        for (; j < h; ++j)
        {
            *dst++ = accessImageUpsampleH(ds_src, i, j, w, h);
        }
    }
}

static void upsampleImageV(uint8_t *dst, const uint8_t *ds_src, int w, int h)
{
    int i = 0, j = 0;
    for (; i < w; ++i)
    {
        j = 0;
        for (; j < h; ++j)
        {
            *dst++ = accessImageUpsampleV(ds_src, i, j, w, h);
        }
    }
}

static void upsampleImageDS(uint8_t *dst, const uint8_t *ds_fd_src, const uint8_t *src_pf, int w, int h)
{
    for (int i = 0; i < w; ++i)
    {
        for (int j = 0; j < h; ++j)
        {
            if (accessImageNoCheck(ds_fd_src, i, j / 2, w, h / 2) == 0)
            {
                dst[i * h + j] = src_pf[i * h + j];
            }
        }
    }
}

static void upsampleImageDS2(uint8_t *dst, const uint8_t *ds2_fd_src, const uint8_t *src_pf, int w, int h)
{
    for (int i = 0; i < w; ++i)
    {
        for (int j = 0; j < h; ++j)
        {
            if (accessImageNoCheck(ds2_fd_src, i / 2, j / 2, w / 2, h / 2) == 0)
            {
                dst[i * h + j] = src_pf[i * h + j];
            }
        }
    }
}

static uint8_t accessSelectFD(const uint8_t *s_src, const uint8_t *m_src, int x, int y, int w, int h)
{
    uint8_t mask = m_src[x * ENCODE_SELECT_MASK_STRIDE(h) + y / (ENCODE_SELECT_MASK_Y_SCALE * BITS_PER_BYTE)];
    uint8_t n = y / ENCODE_SELECT_MASK_Y_SCALE % BITS_PER_BYTE;
    uint8_t mask_bit = (mask >> n) & 1;
    return mask_bit
               ? accessImageNoCheck(s_src, x, y, w, h)
               : -1;
}

static void upsampleImageSelectDS(uint8_t *dst, const uint8_t *ds_s_src, const uint8_t *ds_m_src, const uint8_t *src_pf, int w, int h)
{
    for (int i = 0; i < w; ++i)
    {
        for (int j = 0; j < h; ++j)
        {
            if (accessSelectFD(ds_s_src, ds_m_src, i, j / 2, w, h / 2) == 0)
            {
                dst[i * h + j] = src_pf[i * h + j];
            }
        }
    }
}

static void upsampleImageSelectDS2(uint8_t *dst, const uint8_t *ds2_s_src, const uint8_t *ds2_m_src, const uint8_t *src_pf, int w, int h)
{
    for (int i = 0; i < w; ++i)
    {
        for (int j = 0; j < h; ++j)
        {
            if (accessSelectFD(ds2_s_src, ds2_m_src, i / 2, j / 2, w / 2, h / 2) == 0)
            {
                dst[i * h + j] = src_pf[i * h + j];
            }
        }
    }
}

static void upsampleFDCImageH(uint8_t *dst, const uint8_t *ds_src_a, const uint8_t *ds_c_src, const uint8_t *ds_src, int w, int h)
{
    int ds_w = w / 2, ds_x = 0, mask = 0;
    uint8_t *dst_b = dst + h;
    int j, n;
    for (; ds_x < ds_w; dst += h, dst_b += h, ++ds_x)
    {
        n = j = 0;
        for (; j < h; ++j)
        {
            uint8_t d = accessImageNoCheck(ds_src_a, ds_x, j, ds_w, h);
            uint8_t p = accessImageNoCheck(ds_src, ds_x, j, ds_w, h);

            if (n % BITS_PER_BYTE == 0)
            {
                mask = *ds_c_src++;
            }
            uint8_t c = mask & 1;
            mask >>= 1;
            ++n;

            *dst++ = p + d;
            *dst_b++ = p - d + c;
        }
    }
}

static void upsampleFDCImageV(uint8_t *dst, const uint8_t *ds_src_a, const uint8_t *ds_c_src, const uint8_t *ds_src, int w, int h)
{
    int i = 0, mask = 0, ds_h = h / 2;
    int ds_y, n;
    for (; i < w; ++i)
    {
        n = ds_y = 0;
        for (; ds_y < ds_h; ++ds_y)
        {
            uint8_t d = accessImageNoCheck(ds_src_a, i, ds_y, w, ds_h);
            uint8_t p = accessImageNoCheck(ds_src, i, ds_y, w, ds_h);

            if (n % BITS_PER_BYTE == 0)
            {
                mask = *ds_c_src++;
            }
            uint8_t c = mask & 1;
            mask >>= 1;
            ++n;

            *dst++ = p + d;
            *dst++ = p - d + c;
        }
    }
}

uint8_t selectHandlePredict(
    const uint8_t *s_src, const uint8_t *src, const uint8_t *src_pf, const uint8_t *ds_src_pf,
    int i, int j, int w, int h)
{
    return predictPixel(src, i, j, w, h) + accessImageNoCheck(s_src, i, j, w, h);
}

uint8_t selectHandlePredictDelta(
    const uint8_t *s_src, const uint8_t *src, const uint8_t *src_pf, const uint8_t *ds_src_pf,
    int i, int j, int w, int h)
{
    return predictPixelDelta(src, src_pf, i, j, w, h) +
           accessImageNoCheck(s_src, i, j, w, h) + accessImageNoCheck(src_pf, i, j, w, h);
}

uint8_t selectHandlePredictDeltaFromDownsampled(
    const uint8_t *s_src, const uint8_t *src, const uint8_t *src_pf, const uint8_t *ds_src_pf,
    int i, int j, int w, int h)
{
    return predictPixelDelta(src, src_pf, i, j, w, h) +
           accessImageNoCheck(s_src, i, j, w, h) + accessImageUpsample(ds_src_pf, i, j, w, h);
}

uint8_t selectHandleDifference(
    const uint8_t *s_src, const uint8_t *src, const uint8_t *src_pf, const uint8_t *ds_src_pf,
    int i, int j, int w, int h)
{
    return accessImageNoCheck(s_src, i, j, w, h) + accessImageNoCheck(src_pf, i, j, w, h);
}

uint8_t selectHandleDifferenceFromDownsampled(
    const uint8_t *s_src, const uint8_t *src, const uint8_t *src_pf, const uint8_t *ds_src_pf,
    int i, int j, int w, int h)
{
    return accessImageNoCheck(s_src, i, j, w, h) + accessImageUpsample(ds_src_pf, i, j, w, h);
}

uint8_t selectHandleDownsampledDifference(
    const uint8_t *s_src, const uint8_t *src, const uint8_t *src_pf, const uint8_t *us_src_pf,
    int i, int j, int w, int h)
{
    return accessImageNoCheck(s_src, i, j, w, h) + accessImageDownsample(us_src_pf, i * 2, j * 2, w * 2, h * 2);
}

typedef uint8_t (*selectHandleFunc)(
    const uint8_t *s_src, const uint8_t *src, const uint8_t *src_pf, const uint8_t *ds_src_pf,
    int i, int j, int w, int h);

static inline void selectImage(
    uint8_t *dst, selectHandleFunc handle_select, selectHandleFunc handle_select_fd,
    const uint8_t *s_src, const uint8_t *m_src, const uint8_t *src,
    const uint8_t *src_pf, const uint8_t *ds_src_pf, int w, int h)
{
    int x = 0, y, i, j, n;
    uint8_t mask;
    while (1)
    {
        n = y = 0;
        while (1)
        {
            if (n % BITS_PER_BYTE == 0)
            {
                n = 0;
                mask = *m_src++;
            }
            uint8_t mask_bit = mask & 1;
            ++n;
            mask >>= 1;
            for (i = x; i < HR_MIN(x + ENCODE_SELECT_MASK_X_SCALE, w); ++i)
                for (j = y; j < HR_MIN(y + ENCODE_SELECT_MASK_Y_SCALE, h); ++j)
                {
                    dst[i * h + j] = (mask_bit
                                          ? handle_select_fd
                                          : handle_select)(s_src, src, src_pf, ds_src_pf, i, j, w, h);
                }

            y += ENCODE_SELECT_MASK_Y_SCALE;
            if (y >= h)
                break;
        }

        x += ENCODE_SELECT_MASK_X_SCALE;
        if (x >= w)
            break;
    }
}

static inline void convert_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b, int lq)
{
    double y_in = y;
    double u_in = u;
    double v_in = v;

    if (lq)
    {
        y_in *= 4;
        u_in *= 8;
        v_in *= 8;
    }

    y_in /= 255;
    u_in -= 128;
    v_in -= 128;
    u_in /= 127;
    v_in /= 127;
    u_in *= .436;
    v_in *= .615;

    double r_out = y_in + 1.13983 * v_in;
    double g_out = y_in + -0.39465 * u_in + -0.58060 * v_in;
    double b_out = y_in + 2.03211 * u_in;

    if (r_out < 0)
        r_out = 0;
    else if (r_out > 1)
        r_out = 1;
    if (g_out < 0)
        g_out = 0;
    else if (g_out > 1)
        g_out = 1;
    if (b_out < 0)
        b_out = 0;
    else if (b_out > 1)
        b_out = 1;

    *r = round(r_out * 255);
    *g = round(g_out * 255);
    *b = round(b_out * 255);
}

typedef struct _FrameImage
{
    int size;
    uint8_t *image;
} FrameImage;

typedef struct _FrameDelta
{
    FrameImage y, ds2_y, ds_y, ds2_u, ds2_v, ds_ds2_u, ds_ds2_v, ds2_ds2_u, ds2_ds2_v;
} FrameDelta;

#define RP_ENC_HAVE_Y (1 << 0)
#define RP_ENC_HAVE_UV (1 << 1)
#define RP_ENC_DS_Y (1 << 2)
#define RP_ENC_DS2_Y (1 << 3)
#define RP_ENC_DS_DS2_UV (1 << 4)
#define RP_ENC_DS2_DS2_UV (1 << 5)

#define BYTES_PER_RGB 3

typedef struct _FrameDecodeContext
{
    int width, height;
    int ds_width, ds_height;
    int ds_ds_width, ds_ds_height;

    FrameDelta f, f_pf;
    FrameImage u, v;

    uint32_t flags, flags_pf;
} FrameDecodeContext;

static void frame_image_init_comp(FrameImage *im, int width, int height, int ncomp)
{
    im->size = width * height * ncomp;
    im->image = malloc(im->size);
    // memset(im->image, 0, im->size);
}

static void frame_image_init(FrameImage *im, int width, int height)
{
    frame_image_init_comp(im, width, height, 1);
}

static void frame_image_destroy(FrameImage *im)
{
    free(im->image);
    im->image = 0;
    im->size = 0;
}

static void frame_delta_init(FrameDecodeContext *ctx, FrameDelta *dt)
{
    frame_image_init(&dt->y, ctx->width, ctx->height);
    frame_image_init(&dt->ds_y, ctx->width, ctx->ds_height);
    frame_image_init(&dt->ds2_y, ctx->ds_width, ctx->ds_height);
    frame_image_init(&dt->ds2_u, ctx->ds_width, ctx->ds_height);
    frame_image_init(&dt->ds2_v, ctx->ds_width, ctx->ds_height);
    frame_image_init(&dt->ds_ds2_u, ctx->ds_width, ctx->ds_ds_height);
    frame_image_init(&dt->ds_ds2_v, ctx->ds_width, ctx->ds_ds_height);
    frame_image_init(&dt->ds2_ds2_u, ctx->ds_ds_width, ctx->ds_ds_height);
    frame_image_init(&dt->ds2_ds2_v, ctx->ds_ds_width, ctx->ds_ds_height);
}

static void frame_delta_destroy(FrameDelta *dt)
{
    frame_image_destroy(&dt->y);
    frame_image_destroy(&dt->ds_y);
    frame_image_destroy(&dt->ds2_y);
    frame_image_destroy(&dt->ds2_u);
    frame_image_destroy(&dt->ds2_v);
    frame_image_destroy(&dt->ds_ds2_u);
    frame_image_destroy(&dt->ds_ds2_v);
    frame_image_destroy(&dt->ds2_ds2_u);
    frame_image_destroy(&dt->ds2_ds2_v);
}

static void frame_decode_context_init(FrameDecodeContext *ctx, int width, int height)
{
    ctx->width = width;
    ctx->height = height;
    ctx->ds_width = ctx->width / 2;
    ctx->ds_height = ctx->height / 2;
    ctx->ds_ds_width = ctx->ds_width / 2;
    ctx->ds_ds_height = ctx->ds_height / 2;
    frame_delta_init(ctx, &ctx->f);
    frame_delta_init(ctx, &ctx->f_pf);

    frame_image_init(&ctx->u, ctx->width, ctx->height);
    frame_image_init(&ctx->v, ctx->width, ctx->height);
    // frame_image_init_comp(&ctx->rgb, ctx->width, ctx->height, BYTES_PER_RGB);

    ctx->flags_pf = ctx->flags = 0;
}

static void frame_decode_context_destroy(FrameDecodeContext *ctx)
{
    frame_delta_destroy(&ctx->f);
    frame_delta_destroy(&ctx->f_pf);

    frame_image_destroy(&ctx->u);
    frame_image_destroy(&ctx->v);
}

static FrameDecodeContext top_ctx, bot_ctx, il_top_ctx, il_bot_ctx;

#define CHECK_DATA_SIZE(s)                                                                    \
    do                                                                                        \
    {                                                                                         \
        if (data_size != (s))                                                                 \
        {                                                                                     \
            fprintf(stderr, "frame_decode data missize: %d (expected %d)\n", data_size, (s)); \
            return 0;                                                                         \
        }                                                                                     \
    } while (0)

#define CHECK_DATA2_SIZE(s)                                                                     \
    do                                                                                          \
    {                                                                                           \
        if (data2_size != (s))                                                                  \
        {                                                                                       \
            fprintf(stderr, "frame_decode data2 missize: %d (expected %d)\n", data2_size, (s)); \
            return 0;                                                                           \
        }                                                                                       \
    } while (0)

static int handle_image(uint32_t header_flags, int has, int pf_has,
                        int has_ds2, int has_ds, int pf_has_ds2, int pf_has_ds,
                        FrameImage *im, FrameImage *ds_im, FrameImage *ds2_im,
                        uint8_t *data, int data_size, uint8_t *data2, int data2_size,
                        FrameImage *im_pf, FrameImage *ds_im_pf, FrameImage *ds2_im_pf,
                        int width, int height, int adata, int *out_ds2, int *out_ds)
{
    if (adata)
    {
        if (!has)
        {
            return -1;
        }
        if (!(header_flags & RP_DATA_FRAME_DELTA))
        {
            return -1;
        }

        if (header_flags & RP_DATA_DOWNSAMPLE2)
        {
            return -1;
        }
        else if (header_flags & RP_DATA_DOWNSAMPLE)
        {
            if (!has_ds2)
            {
                if (!has_ds)
                {
                    downsampleImageV(ds_im->image, im->image, width, height);
                }
                downsampleImageH(ds2_im->image, ds_im->image, width, height / 2);
            }

            CHECK_DATA_SIZE(width * height / 4);
            CHECK_DATA2_SIZE(ENCODE_UPSAMPLE_CARRY_SIZE(width / 2, height / 2));

            upsampleFDCImageH(ds_im->image, data, data2, ds2_im->image, width, height / 2);
            upsampleImageV(im->image, ds_im->image, width, height);
            *out_ds = 1;
        }
        else
        {
            if (!has_ds)
            {
                downsampleImageV(ds_im->image, im->image, width, height);
            }

            CHECK_DATA_SIZE(width * height / 2);
            CHECK_DATA2_SIZE(ENCODE_UPSAMPLE_CARRY_SIZE(width, height / 2));

            upsampleFDCImageV(im->image, data, data2, ds_im->image, width, height);
        }
    }
    else if (header_flags & RP_DATA_FRAME_DELTA)
    {
        if (!pf_has)
        {
            return -1;
        }
        if (header_flags & RP_DATA_SELECT_FRAME_DELTA)
        {
            if (!data2 || !data2_size)
            {
                return -1;
            }

            if (header_flags & RP_DATA_DOWNSAMPLE2)
            {
                if (!pf_has_ds2)
                {
                    if (!pf_has_ds)
                    {
                        downsampleImageV(ds_im_pf->image, im_pf->image, width, height);
                    }
                    downsampleImageH(ds2_im_pf->image, ds_im_pf->image, width, height / 2);
                }
                CHECK_DATA_SIZE(width * height / 4);
                CHECK_DATA2_SIZE(ENCODE_SELECT_MASK_SIZE(width / 2, height / 2));
                selectImage(ds2_im->image, selectHandlePredict, selectHandleDifference,
                            data, data2, ds2_im->image, ds2_im_pf->image, 0, width / 2, height / 2);
                upsampleImage(im->image, ds2_im->image, width, height);
                upsampleImageSelectDS2(im->image, data, data2, im_pf->image, width, height);
                *out_ds2 = 1;
            }
            else if (header_flags & RP_DATA_DOWNSAMPLE)
            {
                if (!pf_has_ds)
                {
                    downsampleImageV(ds_im_pf->image, im_pf->image, width, height);
                }
                CHECK_DATA_SIZE(width * height / 2);
                CHECK_DATA2_SIZE(ENCODE_SELECT_MASK_SIZE(width, height / 2));
                selectImage(ds_im->image, selectHandlePredict, selectHandleDifference,
                            data, data2, ds_im->image, ds_im_pf->image, 0, width, height / 2);
                upsampleImageV(im->image, ds_im->image, width, height);
                upsampleImageSelectDS(im->image, data, data2, im_pf->image, width, height);
                *out_ds = 1;
            }
            else
            {
                CHECK_DATA_SIZE(width * height);
                CHECK_DATA2_SIZE(ENCODE_SELECT_MASK_SIZE(width, height));
                selectImage(im->image, selectHandlePredict, selectHandleDifference,
                            data, data2, im->image, im_pf->image, 0, width, height);
            }
        }
        else
        {
            if (header_flags & RP_DATA_DOWNSAMPLE2)
            {
                if (!pf_has_ds2)
                {
                    if (!pf_has_ds)
                    {
                        downsampleImageV(ds_im_pf->image, im_pf->image, width, height);
                    }
                    downsampleImageH(ds2_im_pf->image, ds_im_pf->image, width, height / 2);
                }
                CHECK_DATA_SIZE(width * height / 4);
                differenceImage(ds2_im->image, data, ds2_im_pf->image, width / 2, height / 2);
                upsampleImage(im->image, ds2_im->image, width, height);
                upsampleImageDS2(im->image, data, im_pf->image, width, height);
                *out_ds2 = 1;
            }
            else if (header_flags & RP_DATA_DOWNSAMPLE)
            {
                if (!pf_has_ds)
                {
                    downsampleImageV(ds_im_pf->image, im_pf->image, width, height);
                }
                CHECK_DATA_SIZE(width * height / 2);
                differenceImage(ds_im->image, data, ds_im_pf->image, width, height / 2);
                upsampleImageV(im->image, ds_im->image, width, height);
                upsampleImageDS(im->image, data, im_pf->image, width, height);
                *out_ds = 1;
            }
            else
            {
                CHECK_DATA_SIZE(width * height);
                differenceImage(im->image, data, im_pf->image, width, height);
            }
        }
    }
    else
    {
        if (header_flags & RP_DATA_DOWNSAMPLE2)
        {
            CHECK_DATA_SIZE(width * height / 4);
            predictImage(ds2_im->image, data, width / 2, height / 2);
            upsampleImage(im->image, ds2_im->image, width, height);
            *out_ds2 = 1;
        }
        else if (header_flags & RP_DATA_DOWNSAMPLE)
        {
            CHECK_DATA_SIZE(width * height / 2);
            predictImage(ds_im->image, data, width, height / 2);
            upsampleImageV(im->image, ds_im->image, width, height);
            *out_ds = 1;
        }
        else
        {
            CHECK_DATA_SIZE(width * height);
            predictImage(im->image, data, width, height);
        }
    }
    return 0;
}

#include "yadif.h"

typedef struct _YadifFrames
{
    FrameImage prev, cur, next;
} YadifFrames;

typedef struct _YadifScreen
{
    YadifFrames y, u, v;
} YadifScreen;

YadifScreen yadif_top, yadif_bot;
const FrameImage fi_zero;

static int is_fi_zero(FrameImage fi)
{
    return !fi.image || !fi.size;
}

static void yadif_destroy_frame(YadifFrames *yadif)
{
    frame_image_destroy(&yadif->prev);
    frame_image_destroy(&yadif->cur);
    frame_image_destroy(&yadif->next);
}

static void yadif_destroy_screen(YadifScreen *yadif)
{
    yadif_destroy_frame(&yadif->y);
    yadif_destroy_frame(&yadif->u);
    yadif_destroy_frame(&yadif->v);
}

static void yadif_add_image(YadifFrames *yadif, FrameImage image)
{
    frame_image_destroy(&yadif->prev);
    if (image.size != yadif->next.size)
    {
        frame_image_destroy(&yadif->cur);
        frame_image_destroy(&yadif->next);
    }
    yadif->prev = yadif->cur;
    yadif->cur = yadif->next;
    yadif->next = image;
}

static void yadif_add_frame(YadifScreen *yadif, FrameImage y, FrameImage u, FrameImage v)
{
    yadif_add_image(&yadif->y, y);
    yadif_add_image(&yadif->u, u);
    yadif_add_image(&yadif->v, v);
}

static FrameImage yadif_process_image(YadifFrames yadif, int w, int h, int parity)
{
    FrameImage dst;
    frame_image_init(&dst, w, h);

    yadif_filter(dst.image, yadif.prev.image, yadif.cur.image, yadif.next.image, w, h, parity);
    return dst;
}

static FrameImage yadif_process(YadifScreen yadif, int w, int h, int parity, int lq)
{
    FrameImage y = yadif_process_image(yadif.y, w, h, parity);
    FrameImage u = yadif_process_image(yadif.u, w, h, parity);
    FrameImage v = yadif_process_image(yadif.v, w, h, parity);

    FrameImage rgb;
    frame_image_init_comp(&rgb, w, h, BYTES_PER_RGB);
    for (int j = 0; j < h; ++j)
    {
        for (int i = 0; i < w; ++i)
        {
            uint8_t y_in = y.image[j * w + i];
            uint8_t u_in = u.image[j * w + i];
            uint8_t v_in = v.image[j * w + i];
            convert_to_rgb(y_in, u_in, v_in, &y_in, &u_in, &v_in, lq);
            uint8_t *rgb_pixel = rgb.image + (j * w + i) * BYTES_PER_RGB;
            rgb_pixel[0] = y_in;
            rgb_pixel[1] = u_in;
            rgb_pixel[2] = v_in;
        }
    }

    frame_image_destroy(&y);
    frame_image_destroy(&u);
    frame_image_destroy(&v);
    return rgb;
}

static uint8_t *yadif_try_process(YadifScreen yadif, int w, int h, int parity, int lq)
{
    if (is_fi_zero(yadif.y.prev) || is_fi_zero(yadif.u.prev) || is_fi_zero(yadif.v.prev))
    {
        return 0;
    }
    return yadif_process(yadif, w, h, parity, lq).image;
}

static FrameImage il_convert_yuv_row_major(FrameImage im_even, FrameImage im_odd, int width, int height)
{
    FrameImage im_out;
    frame_image_init(&im_out, width, height * 2);

    for (int i = 0; i < width; ++i)
    {
        for (int j = 0; j < height; ++j)
        {
            uint8_t c = accessImageNoCheck(im_even.image, i, j, width, height);
            uint8_t il_c = accessImageNoCheck(im_odd.image, i, j, width, height);

            uint8_t *pixel = im_out.image + j * 2 * width + i;
            uint8_t *il_pixel = im_out.image + (j * 2 + 1) * width + i;

            *pixel = c;
            *il_pixel = il_c;
        }
    }
    return im_out;
}

static uint8_t *frame_decode_screen(DataHeader header, uint8_t *data, int data_size, uint8_t *data2, int data2_size, int is_top, int adata)
{
    FrameDecodeContext *ctx, *il_ctx;
    if (is_top)
    {
        ctx = &top_ctx;
    }
    else
    {
        ctx = &bot_ctx;
    }
    if (header.flags & RP_DATA_INTERLACE)
    {
        if (!frame_decode_interlaced)
        {
            return 0;
        }
        if (header.flags & RP_DATA_INTERLACE_EVEN_ODD)
        {
            il_ctx = ctx;
            if (is_top)
            {
                ctx = &il_top_ctx;
            }
            else
            {
                ctx = &il_bot_ctx;
            }
        }
        else
        {
            if (is_top)
            {
                il_ctx = &il_top_ctx;
            }
            else
            {
                il_ctx = &il_bot_ctx;
            }
        }
    }
    if (!(header.flags & RP_DATA_Y_UV))
    {
        if (!adata)
        {
            frame_delta_destroy(&ctx->f_pf);
            ctx->f_pf = ctx->f;
            frame_delta_init(ctx, &ctx->f);
            ctx->flags_pf = ctx->flags;
            ctx->flags = 0;
        }

        int out_ds2 = 0, out_ds = 0;
        if (handle_image(header.flags, ctx->flags & RP_ENC_HAVE_Y, ctx->flags_pf & RP_ENC_HAVE_Y,
                         ctx->flags & RP_ENC_DS2_Y, ctx->flags & RP_ENC_DS_Y,
                         ctx->flags_pf & RP_ENC_DS2_Y, ctx->flags_pf & RP_ENC_DS_Y,
                         &ctx->f.y, &ctx->f.ds_y, &ctx->f.ds2_y,
                         data, data_size, data2, data2_size,
                         &ctx->f_pf.y, &ctx->f_pf.ds_y, &ctx->f_pf.ds2_y,
                         ctx->width, ctx->height, adata, &out_ds2, &out_ds) < 0)
            return 0;
        // memset(ctx->f.y.image, 128, ctx->f.y.size);
        ctx->flags |= RP_ENC_HAVE_Y | (out_ds2 ? RP_ENC_DS2_Y : 0) | (out_ds ? RP_ENC_DS_Y : 0);
    }
    else
    {
        int out_ds2_u = 0, out_ds_u = 0, out_ds2_v = 0, out_ds_v = 0;
        if (handle_image(header.flags, ctx->flags & RP_ENC_HAVE_UV, ctx->flags_pf & RP_ENC_HAVE_UV,
                         ctx->flags & RP_ENC_DS2_DS2_UV, ctx->flags & RP_ENC_DS_DS2_UV,
                         ctx->flags_pf & RP_ENC_DS2_DS2_UV, ctx->flags_pf & RP_ENC_DS_DS2_UV,
                         &ctx->f.ds2_u, &ctx->f.ds_ds2_u, &ctx->f.ds2_ds2_u,
                         data, data_size / 2, data2, data2_size / 2,
                         &ctx->f_pf.ds2_u, &ctx->f_pf.ds_ds2_u, &ctx->f_pf.ds2_ds2_u,
                         ctx->ds_width, ctx->ds_height, adata, &out_ds2_u, &out_ds_u) < 0)
            return 0;
        if (handle_image(header.flags, ctx->flags & RP_ENC_HAVE_UV, ctx->flags_pf & RP_ENC_HAVE_UV,
                         ctx->flags & RP_ENC_DS2_DS2_UV, ctx->flags & RP_ENC_DS_DS2_UV,
                         ctx->flags_pf & RP_ENC_DS2_DS2_UV, ctx->flags_pf & RP_ENC_DS_DS2_UV,
                         &ctx->f.ds2_v, &ctx->f.ds_ds2_v, &ctx->f.ds2_ds2_v,
                         data + data_size / 2, data_size / 2, data2 + data2_size / 2, data2_size / 2,
                         &ctx->f_pf.ds2_v, &ctx->f_pf.ds_ds2_v, &ctx->f_pf.ds2_ds2_v,
                         ctx->ds_width, ctx->ds_height, adata, &out_ds2_v, &out_ds_v) < 0)
            return 0;

        if (out_ds2_u != out_ds2_v || out_ds_u != out_ds_v)
        {
            return 0;
        }

        upsampleImage(ctx->u.image, ctx->f.ds2_u.image, ctx->width, ctx->height);
        upsampleImage(ctx->v.image, ctx->f.ds2_v.image, ctx->width, ctx->height);
        // memset(ctx->u.image, 128, ctx->u.size);
        // memset(ctx->v.image, 128, ctx->v.size);
        ctx->flags |= RP_ENC_HAVE_UV | (out_ds2_u ? RP_ENC_DS2_DS2_UV : 0) | (out_ds_u ? RP_ENC_DS_DS2_UV : 0);

        if ((ctx->flags & (RP_ENC_HAVE_Y | RP_ENC_HAVE_UV)) == (RP_ENC_HAVE_Y | RP_ENC_HAVE_UV))
        {
            if (header.flags & RP_DATA_INTERLACE)
            {
                int even_odd = !!(header.flags & RP_DATA_INTERLACE_EVEN_ODD);
                int lq = !!(header.flags & RP_DATA_YUV_LQ);

                if (frame_decode_yadif)
                {
                    YadifScreen *yadif = is_top ? &yadif_top : &yadif_bot;
                    if (even_odd)
                    {
                        FrameImage y = il_convert_yuv_row_major(il_ctx->f.y, ctx->f.y, ctx->width, ctx->height);
                        FrameImage u = il_convert_yuv_row_major(il_ctx->u, ctx->u, ctx->width, ctx->height);
                        FrameImage v = il_convert_yuv_row_major(il_ctx->v, ctx->v, ctx->width, ctx->height);
                        yadif_add_frame(yadif, y, u, v);
                    }
                    uint8_t *rgb = yadif_try_process(*yadif, ctx->width, ctx->height * 2, !even_odd, lq);
                    return rgb;
                }
                else
                {
                    FrameImage y;
                    FrameImage u;
                    FrameImage v;
                    if (even_odd == 0)
                    {
                        y = il_convert_yuv_row_major(ctx->f.y, il_ctx->f.y, ctx->width, ctx->height);
                        u = il_convert_yuv_row_major(ctx->u, il_ctx->u, ctx->width, ctx->height);
                        v = il_convert_yuv_row_major(ctx->v, il_ctx->v, ctx->width, ctx->height);
                    }
                    else
                    {
                        y = il_convert_yuv_row_major(il_ctx->f.y, ctx->f.y, ctx->width, ctx->height);
                        u = il_convert_yuv_row_major(il_ctx->u, ctx->u, ctx->width, ctx->height);
                        v = il_convert_yuv_row_major(il_ctx->v, ctx->v, ctx->width, ctx->height);
                    }

                    FrameImage rgb;
                    frame_image_init_comp(&rgb, ctx->width, ctx->height * 2, BYTES_PER_RGB);
                    for (int j = 0; j < ctx->height * 2; ++j)
                    {
                        for (int i = 0; i < ctx->width; ++i)
                        {
                            uint8_t y_in = y.image[j * ctx->width + i];
                            uint8_t u_in = u.image[j * ctx->width + i];
                            uint8_t v_in = v.image[j * ctx->width + i];
                            convert_to_rgb(y_in, u_in, v_in, &y_in, &u_in, &v_in, lq);
                            uint8_t *rgb_pixel = rgb.image + (j * ctx->width + i) * BYTES_PER_RGB;
                            rgb_pixel[0] = y_in;
                            rgb_pixel[1] = u_in;
                            rgb_pixel[2] = v_in;
                        }
                    }
                    return rgb.image;
                }
            }
            else
            {
                FrameImage rgb;
                frame_image_init_comp(&rgb, ctx->width, ctx->height, BYTES_PER_RGB);
                for (int i = 0; i < ctx->width; ++i)
                {
                    for (int j = 0; j < ctx->height; ++j)
                    {
                        uint8_t y = accessImageNoCheck(ctx->f.y.image, i, j, ctx->width, ctx->height);
                        uint8_t u = accessImageNoCheck(ctx->u.image, i, j, ctx->width, ctx->height);
                        uint8_t v = accessImageNoCheck(ctx->v.image, i, j, ctx->width, ctx->height);
                        convert_to_rgb(y, u, v, &y, &u, &v, header.flags & RP_DATA_YUV_LQ);
                        uint8_t *rgb_pixel = rgb.image + (j * ctx->width + i) * BYTES_PER_RGB;
                        rgb_pixel[0] = y;
                        rgb_pixel[1] = u;
                        rgb_pixel[2] = v;
                        // rgb_pixel[3] = 0;
                    }
                }
                return rgb.image;
            }
        }
    }

    return 0;
}

uint8_t *frame_decode(DataHeader header, uint8_t *data, int data_size, uint8_t *data2, int data2_size, int adata)
{
    return frame_decode_screen(header, data, data_size, data2, data2_size, !(header.flags & RP_DATA_TOP_BOT), adata);
}

void frame_decode_init(int interlaced)
{
    frame_decode_context_init(&top_ctx, 400, interlaced ? 120 : 240);
    frame_decode_context_init(&bot_ctx, 320, interlaced ? 120 : 240);
    if (interlaced)
    {
        frame_decode_context_init(&il_top_ctx, 400, interlaced ? 120 : 240);
        frame_decode_context_init(&il_bot_ctx, 320, interlaced ? 120 : 240);
    }
    frame_decode_interlaced = interlaced;
}

void frame_decode_destroy(void)
{
    frame_decode_context_destroy(&top_ctx);
    frame_decode_context_destroy(&bot_ctx);
    if (frame_decode_interlaced)
    {
        frame_decode_context_destroy(&il_top_ctx);
        frame_decode_context_destroy(&il_bot_ctx);
        yadif_destroy_screen(&yadif_top);
        yadif_destroy_screen(&yadif_bot);
    }
}

void yadif_start(void)
{
    frame_decode_yadif = 1;
}

void yadif_stop(void)
{
    frame_decode_yadif = 0;
    yadif_destroy_screen(&yadif_top);
    yadif_destroy_screen(&yadif_bot);
}
