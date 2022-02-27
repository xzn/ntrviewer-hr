#include "framecodec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#define HR_MAX(a, b) ((a) > (b) ? (a) : (b))
#define HR_MIN(a, b) ((a) < (b) ? (a) : (b))

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

static inline uint16_t accessImageUpsample(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
    return (accessImageUpsampleUnscaled(ds_image, xOrig, yOrig, wOrig, hOrig) + 8) / 16;
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
    return (accessImageDownsampleUnscaled(image, x, y, w, h) + 2) / 4;
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

    if (lq) {
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
    FrameImage y, ds_y, ds_u, ds_v, ds_ds_u, ds_ds_v;
} FrameDelta;

#define RP_HAS_Y (1 << 2)
#define RP_HAS_UV (1 << 3)
#define RP_DOWNSAMPLE_Y (1 << 0)
#define RP_DOWNSAMPLE2_UV (1 << 1)

#define BYTES_PER_RGB 3

typedef struct _FrameDecodeContext
{
    int width, height;
    int ds_width, ds_height;
    int ds_ds_width, ds_ds_height;

    FrameDelta f, f_pf;
    FrameImage u, v;
    FrameImage rgb;

    uint8_t flags, flags_pf;
} FrameDecodeContext;

void frame_image_init_comp(FrameImage *im, int width, int height, int ncomp)
{
    im->size = width * height * ncomp;
    im->image = malloc(im->size);
}

void frame_image_init(FrameImage *im, int width, int height)
{
    frame_image_init_comp(im, width, height, 1);
}

void frame_image_destroy(FrameImage *im)
{
    free(im->image);
}

void frame_delta_init(FrameDecodeContext *ctx, FrameDelta *dt)
{
    frame_image_init(&dt->y, ctx->width, ctx->height);
    frame_image_init(&dt->ds_y, ctx->ds_width, ctx->ds_height);
    frame_image_init(&dt->ds_u, ctx->ds_width, ctx->ds_height);
    frame_image_init(&dt->ds_v, ctx->ds_width, ctx->ds_height);
    frame_image_init(&dt->ds_ds_u, ctx->ds_ds_width, ctx->ds_ds_height);
    frame_image_init(&dt->ds_ds_v, ctx->ds_ds_width, ctx->ds_ds_height);
}

void frame_delta_destroy(FrameDelta *dt)
{
    frame_image_destroy(&dt->y);
    frame_image_destroy(&dt->ds_y);
    frame_image_destroy(&dt->ds_u);
    frame_image_destroy(&dt->ds_v);
    frame_image_destroy(&dt->ds_ds_u);
    frame_image_destroy(&dt->ds_ds_v);
}

void frame_decode_context_init(FrameDecodeContext *ctx, int width, int height)
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

void frame_decode_context_destroy(FrameDecodeContext *ctx)
{
    frame_delta_destroy(&ctx->f);
    frame_delta_destroy(&ctx->f_pf);

    frame_image_destroy(&ctx->u);
    frame_image_destroy(&ctx->v);
    frame_image_destroy(&ctx->rgb);
}

static FrameDecodeContext top_ctx, bot_ctx;

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

static int handle_image(uint32_t header_flags, int pf_has, int pf_ds, int *ds,
                        FrameImage *im, FrameImage *ds_im,
                        uint8_t *data, int data_size, uint8_t *data2, int data2_size,
                        FrameImage *im_pf, FrameImage *ds_im_pf,
                        int width, int height)
{
    if (header_flags & RP_DATA_FD)
    {
        if (!pf_has)
        {
            return -1;
        }
        if (header_flags & RP_DATA_SPFD)
        {
            if (!data2 || !data2_size)
            {
                return -1;
            }

            if (header_flags & RP_DATA_DS)
            {
                CHECK_DATA_SIZE(width * height / 4);
                CHECK_DATA2_SIZE(ENCODE_SELECT_MASK_SIZE(width / 2, height / 2));
                if (header_flags & RP_DATA_PFD)
                {
                    if (!pf_ds)
                    {
                        downsampleImage(ds_im_pf->image, im_pf->image, width, height);
                    }
                    selectImage(ds_im->image, selectHandlePredict, selectHandlePredictDelta,
                                data, data2, ds_im->image, ds_im_pf->image, 0, width / 2, height / 2);
                }
                else if (pf_ds)
                {
                    selectImage(ds_im->image, selectHandlePredict, selectHandleDifference,
                                data, data2, ds_im->image, ds_im_pf->image, 0, width / 2, height / 2);
                }
                else
                {
                    selectImage(ds_im->image, selectHandlePredict, selectHandleDownsampledDifference,
                                data, data2, ds_im->image, 0, im_pf->image, width / 2, height / 2);
                }
                *ds = 1;
                upsampleImage(im->image, ds_im->image, width, height);
            }
            else
            {
                CHECK_DATA_SIZE(width * height);
                CHECK_DATA2_SIZE(ENCODE_SELECT_MASK_SIZE(width, height));
                if (header_flags & RP_DATA_PFD)
                {
                    if (pf_ds)
                    {
                        selectImage(im->image, selectHandlePredict, selectHandlePredictDeltaFromDownsampled,
                                    data, data2, im->image, im_pf->image, ds_im_pf->image, width, height);
                    }
                    else
                    {
                        selectImage(im->image, selectHandlePredict, selectHandlePredictDelta,
                                    data, data2, im->image, im_pf->image, 0, width, height);
                    }
                }
                else if (pf_ds)
                {
                    selectImage(im->image, selectHandlePredict, selectHandleDifferenceFromDownsampled,
                                data, data2, im->image, 0, ds_im_pf->image, width, height);
                }
                else
                {
                    selectImage(im->image, selectHandlePredict, selectHandleDifference,
                                data, data2, im->image, im_pf->image, 0, width, height);
                }
            }
        }
        else if (header_flags & RP_DATA_DS)
        {
            CHECK_DATA_SIZE(width * height / 4);
            if (header_flags & RP_DATA_PFD)
            {
                predictImage(ds_im->image, data, width / 2, height / 2);
                data = ds_im->image;
            }
            if (pf_ds)
            {
                differenceImage(ds_im->image, data, ds_im_pf->image, width / 2, height / 2);
            }
            else
            {
                downsampledDifference(ds_im->image, data, im_pf->image, width, height);
            }
            *ds = 1;
            upsampleImage(im->image, ds_im->image, width, height);
        }
        else
        {
            CHECK_DATA_SIZE(width * height);
            if (header_flags & RP_DATA_PFD)
            {
                predictImage(im->image, data, width, height);
                data = im->image;
            }
            if (pf_ds)
            {
                differenceFromDownsampled(im->image, data, ds_im_pf->image, width, height);
            }
            else
            {
                differenceImage(im->image, data, im_pf->image, width, height);
            }
        }
    }
    else if (header_flags & RP_DATA_DS)
    {
        CHECK_DATA_SIZE(width * height / 4);
        predictImage(ds_im->image, data, width / 2, height / 2);
        upsampleImage(im->image, ds_im->image, width, height);
        *ds = 1;
    }
    else
    {
        CHECK_DATA_SIZE(width * height);
        predictImage(im->image, data, width, height);
    }
    return 0;
}

uint8_t *frame_decode_screen(DataHeader header, uint8_t *data, int data_size, uint8_t *data2, int data2_size, int is_top)
{
    FrameDecodeContext *ctx;
    if (is_top)
    {
        ctx = &top_ctx;
    }
    else
    {
        ctx = &bot_ctx;
    }
    if (header.flags & RP_DATA_Y)
    {
        frame_delta_destroy(&ctx->f_pf);
        ctx->f_pf = ctx->f;
        frame_delta_init(ctx, &ctx->f);
        ctx->flags_pf = ctx->flags;
        ctx->flags = 0;

        int ds = 0;
        if (handle_image(header.flags, ctx->flags_pf & RP_HAS_Y, ctx->flags_pf & RP_DOWNSAMPLE_Y, &ds,
                         &ctx->f.y, &ctx->f.ds_y,
                         data, data_size, data2, data2_size,
                         &ctx->f_pf.y, &ctx->f_pf.ds_y,
                         ctx->width, ctx->height) < 0)
            return 0;
        // memset(ctx->f.y.image, 128, ctx->f.y.size);
        ctx->flags |= RP_HAS_Y;
        if (ds)
            ctx->flags |= RP_DOWNSAMPLE_Y;
    }
    else
    {
        int ds_u = 0, ds_v = 0;
        if (handle_image(header.flags, ctx->flags_pf & RP_HAS_UV, ctx->flags_pf & RP_DOWNSAMPLE2_UV, &ds_u,
                         &ctx->f.ds_u, &ctx->f.ds_ds_u,
                         data, data_size / 2, data2, data2_size / 2,
                         &ctx->f_pf.ds_u, &ctx->f_pf.ds_ds_u,
                         ctx->ds_width, ctx->ds_height) < 0)
            return 0;
        if (handle_image(header.flags, ctx->flags_pf & RP_HAS_UV, ctx->flags_pf & RP_DOWNSAMPLE2_UV, &ds_v,
                         &ctx->f.ds_v, &ctx->f.ds_ds_v,
                         data + data_size / 2, data_size / 2, data2 + data2_size / 2, data2_size / 2,
                         &ctx->f_pf.ds_v, &ctx->f_pf.ds_ds_v,
                         ctx->ds_width, ctx->ds_height) < 0)
            return 0;
        if (ds_u != ds_v)
        {
            return 0;
        }

        upsampleImage(ctx->u.image, ctx->f.ds_u.image, ctx->width, ctx->height);
        upsampleImage(ctx->v.image, ctx->f.ds_v.image, ctx->width, ctx->height);
        // memset(ctx->u.image, 128, ctx->u.size);
        // memset(ctx->v.image, 128, ctx->v.size);
        ctx->flags |= RP_HAS_UV;
        if (ds_u)
            ctx->flags |= RP_DOWNSAMPLE2_UV;

        if ((ctx->flags & (RP_HAS_Y | RP_HAS_UV)) == (RP_HAS_Y | RP_HAS_UV))
        {
            frame_image_destroy(&ctx->rgb);
            frame_image_init_comp(&ctx->rgb, ctx->width, ctx->height, BYTES_PER_RGB);
            for (int i = 0; i < ctx->width; ++i)
            {
                for (int j = 0; j < ctx->height; ++j)
                {
                    uint8_t y = accessImageNoCheck(ctx->f.y.image, i, j, ctx->width, ctx->height);
                    uint8_t u = accessImageNoCheck(ctx->u.image, i, j, ctx->width, ctx->height);
                    uint8_t v = accessImageNoCheck(ctx->v.image, i, j, ctx->width, ctx->height);
                    convert_to_rgb(y, u, v, &y, &u, &v, header.flags & RP_DATA_LQ);
                    uint8_t *rgb_pixel = ctx->rgb.image + (j * ctx->width + i) * BYTES_PER_RGB;
                    rgb_pixel[0] = y;
                    rgb_pixel[1] = u;
                    rgb_pixel[2] = v;
                    // rgb_pixel[3] = 0;
                }
            }
            uint8_t *ret = ctx->rgb.image;
            ctx->rgb.image = NULL;
            return ret;
        }
    }

    return 0;
}

uint8_t *frame_decode(DataHeader header, uint8_t *data, int data_size, uint8_t *data2, int data2_size)
{
    return frame_decode_screen(header, data, data_size, data2, data2_size, header.flags & RP_DATA_TOP);
}

void frame_decode_init()
{
    frame_decode_context_init(&top_ctx, 400, 240);
    frame_decode_context_init(&bot_ctx, 320, 240);
}

void frame_decode_destroy()
{
    frame_decode_context_destroy(&top_ctx);
    frame_decode_context_destroy(&bot_ctx);
}
