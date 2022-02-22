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
    return accessImageNoCheck(image, HR_MAX(HR_MIN(x, w), 0), HR_MAX(HR_MIN(y, h), 0), w, h);
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

static inline void convert_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    double y_in = y;
    double u_in = u;
    double v_in = v;
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
} Frameimage;

typedef struct _FrameDelta
{
    Frameimage y, ds_y, ds_u, ds_v, ds_ds_u, ds_ds_v;
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
    Frameimage u, v;
    Frameimage rgb;

    uint8_t flags, flags_pf;
} FrameDecodeContext;

void frame_image_init_comp(Frameimage *im, int width, int height, int ncomp)
{
    im->size = width * height * ncomp;
    im->image = malloc(im->size);
}

void frame_image_init(Frameimage *im, int width, int height)
{
    frame_image_init_comp(im, width, height, 1);
}

void frame_image_destroy(Frameimage *im)
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

#define CHECK_DATA_SIZE(s)                                                                  \
    do                                                                                      \
    {                                                                                       \
        if (data_size != (s))                                                               \
        {                                                                                   \
            fprintf(stderr, "frame_decode data missize: %d %d\n", header.flags, data_size); \
            return 0;                                                                       \
        }                                                                                   \
    } while (0)

uint8_t *frame_decode_screen(DataHeader header, uint8_t *data, int data_size, int is_top)
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

        if (header.flags & RP_DATA_FD)
        {
            if (!(ctx->flags_pf & RP_HAS_Y))
            {
                return 0;
            }
            if (header.flags & RP_DATA_DS)
            {
                CHECK_DATA_SIZE(ctx->f.ds_y.size);
                if (ctx->flags_pf & RP_DOWNSAMPLE_Y)
                {
                    differenceImage(ctx->f.ds_y.image, data, ctx->f_pf.ds_y.image, ctx->ds_width, ctx->ds_height);
                }
                else
                {
                    downsampledDifference(ctx->f.ds_y.image, data, ctx->f_pf.y.image, ctx->width, ctx->height);
                }
                ctx->flags |= RP_DOWNSAMPLE_Y;
                upsampleImage(ctx->f.y.image, ctx->f.ds_y.image, ctx->width, ctx->height);
            }
            else
            {
                CHECK_DATA_SIZE(ctx->f.y.size);
                if (ctx->flags_pf & RP_DOWNSAMPLE_Y)
                {
                    differenceFromDownsampled(ctx->f.y.image, data, ctx->f_pf.ds_y.image, ctx->width, ctx->height);
                }
                else
                {
                    differenceImage(ctx->f.y.image, data, ctx->f_pf.y.image, ctx->width, ctx->height);
                }
            }
        }
        else if (header.flags & RP_DATA_DS)
        {
            CHECK_DATA_SIZE(ctx->f.ds_y.size);
            predictImage(ctx->f.ds_y.image, data, ctx->ds_width, ctx->ds_height);
            upsampleImage(ctx->f.y.image, ctx->f.ds_y.image, ctx->width, ctx->height);
            ctx->flags |= RP_DOWNSAMPLE_Y;
        }
        else
        {
            CHECK_DATA_SIZE(ctx->f.y.size);
            predictImage(ctx->f.y.image, data, ctx->width, ctx->height);
        }
        ctx->flags |= RP_HAS_Y;
    }
    else
    {
        if (header.flags & RP_DATA_FD)
        {
            if (!(ctx->flags_pf & RP_HAS_UV))
            {
                return 0;
            }
            if (header.flags & RP_DATA_DS)
            {
                CHECK_DATA_SIZE(ctx->f.ds_ds_u.size + ctx->f.ds_ds_v.size);
                if (ctx->flags_pf & RP_DOWNSAMPLE2_UV)
                {
                    differenceImage(ctx->f.ds_ds_u.image, data, ctx->f_pf.ds_ds_u.image, ctx->ds_ds_width, ctx->ds_ds_height);
                    differenceImage(ctx->f.ds_ds_v.image, data + ctx->f.ds_ds_u.size, ctx->f_pf.ds_ds_v.image, ctx->ds_ds_width, ctx->ds_ds_height);
                }
                else
                {
                    downsampledDifference(ctx->f.ds_ds_u.image, data, ctx->f_pf.ds_u.image, ctx->ds_width, ctx->ds_height);
                    downsampledDifference(ctx->f.ds_ds_v.image, data + ctx->f.ds_ds_u.size, ctx->f_pf.ds_v.image, ctx->ds_width, ctx->ds_height);
                }
                ctx->flags |= RP_DOWNSAMPLE2_UV;
                upsampleImage(ctx->f.ds_u.image, ctx->f.ds_ds_u.image, ctx->ds_width, ctx->ds_height);
                upsampleImage(ctx->f.ds_v.image, ctx->f.ds_ds_v.image, ctx->ds_width, ctx->ds_height);
            }
            else
            {
                CHECK_DATA_SIZE(ctx->f.ds_u.size + ctx->f.ds_v.size);
                if (ctx->flags_pf & RP_DOWNSAMPLE2_UV)
                {
                    differenceFromDownsampled(ctx->f.ds_u.image, data, ctx->f_pf.ds_ds_u.image, ctx->ds_width, ctx->ds_height);
                    differenceFromDownsampled(ctx->f.ds_v.image, data + ctx->f.ds_u.size, ctx->f_pf.ds_ds_v.image, ctx->ds_width, ctx->ds_height);
                }
                else
                {
                    differenceImage(ctx->f.ds_u.image, data, ctx->f_pf.ds_u.image, ctx->ds_width, ctx->ds_height);
                    differenceImage(ctx->f.ds_v.image, data + ctx->f.ds_u.size, ctx->f_pf.ds_v.image, ctx->ds_width, ctx->ds_height);
                }
            }
        }
        else if (header.flags & RP_DATA_DS)
        {
            CHECK_DATA_SIZE(ctx->f.ds_ds_u.size + ctx->f.ds_ds_v.size);
            predictImage(ctx->f.ds_ds_u.image, data, ctx->ds_ds_width, ctx->ds_ds_height);
            predictImage(ctx->f.ds_ds_v.image, data + ctx->f.ds_ds_u.size, ctx->ds_ds_width, ctx->ds_ds_height);
            upsampleImage(ctx->f.ds_u.image, ctx->f.ds_ds_u.image, ctx->ds_width, ctx->ds_height);
            upsampleImage(ctx->f.ds_v.image, ctx->f.ds_ds_v.image, ctx->ds_width, ctx->ds_height);
            ctx->flags |= RP_DOWNSAMPLE2_UV;
        }
        else
        {
            CHECK_DATA_SIZE(ctx->f.ds_u.size + ctx->f.ds_v.size);
            predictImage(ctx->f.ds_u.image, data, ctx->ds_width, ctx->ds_height);
            predictImage(ctx->f.ds_v.image, data + ctx->f.ds_u.size, ctx->ds_width, ctx->ds_height);
        }

        upsampleImage(ctx->u.image, ctx->f.ds_u.image, ctx->width, ctx->height);
        upsampleImage(ctx->v.image, ctx->f.ds_v.image, ctx->width, ctx->height);
        ctx->flags |= RP_HAS_UV;

        if ((ctx->flags & (RP_HAS_Y | RP_HAS_UV)) == (RP_HAS_Y | RP_HAS_UV))
        {
            // frame_image_destroy(&ctx->rgb);
            frame_image_init_comp(&ctx->rgb, ctx->width, ctx->height, BYTES_PER_RGB);
            for (int i = 0; i < ctx->width; ++i)
            {
                for (int j = 0; j < ctx->height; ++j)
                {
                    uint8_t y = accessImageNoCheck(ctx->f.y.image, i, j, ctx->width, ctx->height);
                    uint8_t u = accessImageNoCheck(ctx->u.image, i, j, ctx->width, ctx->height);
                    uint8_t v = accessImageNoCheck(ctx->v.image, i, j, ctx->width, ctx->height);
                    convert_to_rgb(y, u, v, &y, &u, &v);
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

uint8_t *frame_decode(DataHeader header, uint8_t *data, int data_size)
{
    return frame_decode_screen(header, data, data_size, header.flags & RP_DATA_TOP);
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
