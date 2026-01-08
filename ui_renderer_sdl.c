#include "main.h"
#include "ui_common_sdl.h"
#include "ui_renderer_sdl.h"

#include "const.h"

#include "nuklear_sdl_renderer.h"
#include <stdlib.h>

SDL_Renderer *sdl_renderer[SCREEN_COUNT];

static SDL_Window *sdl_win[SCREEN_COUNT];
static struct rp_dims sdl_tex_dims[SCREEN_COUNT][SCREEN_COUNT];
static SDL_Texture *sdl_texture[SCREEN_COUNT][SCREEN_COUNT];
static SDL_Texture *sdl_texture_blur[SCREEN_COUNT][SCREEN_COUNT];
static struct nk_context *nk_ctx;

static SDL_Texture *sdl_texture_update(int screen_top_bot, int ctx_top_bot, int width, int height, SDL_Texture **blur) {
    int j = screen_top_bot;
    int i = ctx_top_bot;

    if (sdl_tex_dims[j][i].width == width && sdl_tex_dims[j][i].height == height) {
        *blur = sdl_texture_blur[j][i];
        return sdl_texture[j][i];
    }

    sdl_tex_dims[j][i].width = sdl_tex_dims[j][i].height = 0;

    if (sdl_texture[j][i]) {
        SDL_DestroyTexture(sdl_texture[j][i]);
        sdl_texture[j][i] = NULL;
    }

    sdl_texture[j][i] = SDL_CreateTexture(sdl_renderer[i], SDL_FORMAT, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!sdl_texture[j][i]) {
        err_log("SDL_CreateTexture: %s\n", SDL_GetError());
        *blur = NULL;
        return NULL;
    }

    sdl_texture_blur[j][i] = SDL_CreateTexture(sdl_renderer[i], SDL_FORMAT, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!sdl_texture_blur[j][i]) {
        err_log("SDL_CreateTexture: %s\n", SDL_GetError());

        SDL_DestroyTexture(sdl_texture[j][i]);
        sdl_texture[j][i] = NULL;

        *blur = NULL;
        return NULL;
    }

    sdl_tex_dims[j][i].width = width;
    sdl_tex_dims[j][i].height = height;

    *blur = sdl_texture_blur[j][i];
    return sdl_texture[j][i];
}

static void sdl_texture_destroy(void) {
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        for (int i = 0; i < SCREEN_COUNT; ++i) {
            if (sdl_texture[j][i]) {
                SDL_DestroyTexture(sdl_texture[j][i]);
                sdl_texture[j][i] = NULL;
            }
            if (sdl_texture_blur[j][i]) {
                SDL_DestroyTexture(sdl_texture_blur[j][i]);
                sdl_texture_blur[j][i] = NULL;
            }
        }
    }
}

static int sdl_renderer_init(void) {
    bool renderer_hw = is_renderer_sdl_hw();
    const char *renderer_name = SDL_getenv(SDL_HINT_RENDER_DRIVER);

    const char *driver_name = NULL;
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        if (i == SCREEN_TOP) {
            int renderer_index = -1;

            int num_renderer = SDL_GetNumRenderDrivers();
            if (num_renderer < 0) {
                err_log("SDL_GetNumRenderDrivers: %s\n", SDL_GetError());
                return -1;
            }

            if (renderer_name) {
                for (int j = 0; j < num_renderer; ++j) {
                    if (!(driver_name = SDL_GetRenderDriver(j))) {
                        err_log("SDL_GetRenderDriver: %s\n", SDL_GetError());
                        continue;
                    }

// D3D12 would crash when using multiple windows
#define TRY_CREATE_RENDERER() ({ \
    if (strcmp(driver_name, "direct3d12") == 0) \
        continue; \
    sdl_renderer[i] = SDL_CreateRenderer(sdl_win[i], driver_name); \
    if (!sdl_renderer[i]) { \
        err_log("SDL_CreateRenderer: %s\n", SDL_GetError()); \
        continue; \
    } \
    renderer_index = j; \
 \
    if (strcmp(driver_name, "direct3d11") == 0) { \
        renderer_evt_sync = 1; \
    } else { \
        renderer_single_thread = 1; \
    } \
    break; \
})

                    if (
                        strcmp(driver_name, renderer_name) == 0 &&
                        (
                            (renderer_hw && (strcmp(driver_name, SDL_SOFTWARE_RENDERER) != 0)) ||
                            (!renderer_hw && (strcmp(driver_name, SDL_SOFTWARE_RENDERER) == 0))
                        )
                    ) {

                        TRY_CREATE_RENDERER();
                        break;
                    }
                }
            }

            if (renderer_index < 0) {
                for (int j = 0; j < num_renderer; ++j) {
                    if (!(driver_name = SDL_GetRenderDriver(j))) {
                        err_log("SDL_GetRenderDriver: %s\n", SDL_GetError());
                        return -1;
                    }

                    if (renderer_hw && (strcmp(driver_name, SDL_SOFTWARE_RENDERER) == 0)) {
                        continue;
                    }

                    if (!renderer_hw && (strcmp(driver_name, SDL_SOFTWARE_RENDERER) != 0)) {
                        continue;
                    }

                    TRY_CREATE_RENDERER();
                    break;
                }
            }
        } else {
            sdl_renderer[i] = SDL_CreateRenderer(sdl_win[i], driver_name);
            if (!sdl_renderer[i]) {
                err_log("SDL_CreateRenderer: %s\n", SDL_GetError());
                return -1;
            }
        }

        if (!sdl_renderer[i]) {
            return -1;
        }

        SDL_SetRenderVSync(sdl_renderer[i], 1);

        if (i == SCREEN_TOP) {
            err_log("%s %s\n", driver_name ? driver_name : "", renderer_single_thread ? "single thread" : renderer_evt_sync ? "evt sync" : "");
        }
    }

    nk_ctx = nk_sdl_renderer_init(sdl_win[SCREEN_TOP], sdl_renderer[SCREEN_TOP]);
    if (!nk_ctx)
        return -1;

    return 0;
}

#undef TRY_CREATE_RENDERER

static void sdl_renderer_destroy(void) {
    if (nk_ctx) {
        nk_sdl_renderer_shutdown();
        nk_ctx = NULL;
    }

    sdl_texture_destroy();

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        if (sdl_renderer[i]) {
            SDL_DestroyRenderer(sdl_renderer[i]);
            sdl_renderer[i] = NULL;
        }
    }
}

int ui_renderer_sdl_init(void) {
    if (sdl_win_init(sdl_win, 0)) {
        return -1;
    }

    for (int i = 0; i < SCREEN_COUNT; ++i)
        ui_sdl_win[i] = sdl_win[i];

    sdl_set_wminfo();

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        ui_win_width_drawable[i] = 1;
        ui_win_height_drawable[i] = 1;
        ui_win_scale[i] = 1.0f;
    }

    if (sdl_renderer_init()) {
        return -1;
    }

    ui_nk_ctx = nk_ctx;

    return 0;
}

void ui_renderer_sdl_destroy(void) {
    ui_nk_ctx = NULL;

    sdl_renderer_destroy();

    sdl_reset_wminfo();

    for (int i = 0; i < SCREEN_COUNT; ++i)
        ui_sdl_win[i] = NULL;

    sdl_win_destroy(sdl_win);
}

#include "ntr_rp.h"
void ui_renderer_sdl_main(int ctx_top_bot, view_mode_t view_mode, float bg[GL_CHANNELS_N]) {
    int i = ctx_top_bot;
    SDL_SetRenderScale(sdl_renderer[i], ui_win_scale[i], ui_win_scale[i]);
    SDL_SetRenderDrawColor(sdl_renderer[i], bg[0] * 255, bg[1] * 255, bg[2] * 255, bg[3] * 255);
    SDL_RenderClear(sdl_renderer[i]);

    if (view_mode == VIEW_MODE_TOP_BOT) {
        draw_screen(&rp_buffer_ctx[SCREEN_TOP], SCREEN_HEIGHT0, SCREEN_WIDTH, SCREEN_TOP, i, view_mode, 0);
        draw_screen(&rp_buffer_ctx[SCREEN_BOT], SCREEN_HEIGHT1, SCREEN_WIDTH, SCREEN_BOT, i, view_mode, 0);
    } else if (view_mode == VIEW_MODE_BOT) {
        draw_screen(&rp_buffer_ctx[SCREEN_BOT], SCREEN_HEIGHT1, SCREEN_WIDTH, SCREEN_BOT, i, view_mode, 0);
    } else {
        draw_screen(&rp_buffer_ctx[i], i == SCREEN_TOP ? SCREEN_HEIGHT0 : SCREEN_HEIGHT1, SCREEN_WIDTH, i, i, view_mode, 0);
    }
}

enum ui_sdl_blur_pass {
    BLUR_H,
    BLUR_V,
    BLUR_PASS_COUNT,
    BLUR_OUT = BLUR_V,
};

static uint8_t sdl_data_blur[SCREEN_COUNT][SCREEN_COUNT][BLUR_PASS_COUNT][SCREEN_HEIGHT0 * SCREEN_WIDTH * GL_CHANNELS_N];
#define KERNEL_I_MIN (-2)
#define KERNEL_I_MAX (2)
#define KERNEL_COUNT (KERNEL_I_MAX - KERNEL_I_MIN + 1)
static const uint8_t KERNEL[] = { 1, 2 ,3, 2, 1};
_Static_assert(sizeof(KERNEL) / sizeof(*KERNEL) == KERNEL_COUNT);

static void sdl_data_do_blur(uint8_t *data, uint8_t (*data_blur)[SCREEN_HEIGHT0 * SCREEN_WIDTH * GL_CHANNELS_N], int width, int height) {
    uint8_t *blur = data_blur[BLUR_H];
    for (int y = 0; y < height; ++y) {
        uint8_t *data_y = data + y * width * GL_CHANNELS_N;
        uint8_t *blur_y = blur + y * width * GL_CHANNELS_N;
        for (int x = 0; x < width; ++x) {
            uint8_t *data_x = data_y + x * GL_CHANNELS_N;
            uint8_t *blur_x = blur_y + x * GL_CHANNELS_N;

            int x_min = MAX(x + KERNEL_I_MIN, 0);
            int x_max = MIN(x + KERNEL_I_MAX, width - 1);
            for (int c = 0; c < GL_CHANNELS_N; ++c) {
                int count = 0;
                int acc = 0;
                for (int xx = x_min; xx <= x_max; ++xx) {
                    int ii = xx - x;
                    int mul = KERNEL[ii - KERNEL_I_MIN];
                    count += mul;
                    acc += mul * data_x[c + ii * GL_CHANNELS_N];
                }
                blur_x[c] = acc / count;
            }
        }
    }
    data = blur;
    blur = data_blur[BLUR_V];
    for (int y = 0; y < height; ++y) {
        uint8_t *data_y = data + y * width * GL_CHANNELS_N;
        uint8_t *blur_y = blur + y * width * GL_CHANNELS_N;
        for (int x = 0; x < width; ++x) {
            uint8_t *data_x = data_y + x * GL_CHANNELS_N;
            uint8_t *blur_x = blur_y + x * GL_CHANNELS_N;

            int y_min = MAX(y + KERNEL_I_MIN, 0);
            int y_max = MIN(y + KERNEL_I_MAX, height - 1);
            for (int c = 0; c < GL_CHANNELS_N; ++c) {
                int count = 0;
                int acc = 0;
                for (int yy = y_min; yy <= y_max; ++yy) {
                    int ii = yy - y;
                    int mul = KERNEL[ii - KERNEL_I_MIN];
                    count += mul;
                    acc += mul * data_x[c + ii * width * GL_CHANNELS_N];
                }
                blur_x[c] = acc / count;
            }
        }
    }
}

void ui_renderer_sdl_draw(uint8_t *data, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode) {
    int i = ctx_top_bot;
    SDL_Texture *tex_blur;
    SDL_Texture *tex = sdl_texture_update(screen_top_bot, i, height, width, &tex_blur);
    if (!tex || !tex_blur) {
        return;
    }

    if (data) {
        uint8_t (*data_blur)[SCREEN_HEIGHT0 * SCREEN_WIDTH * GL_CHANNELS_N] = sdl_data_blur[screen_top_bot][i];
        sdl_data_do_blur(data, data_blur, height, width);

        void *pixels;
        int pitch;
        if (!SDL_LockTexture(tex, NULL, &pixels, &pitch)) {
            err_log("SDL_LockTexture: %s\n", SDL_GetError());
            return;
        }
        uint8_t *dst = pixels;
        const int bpp = GL_CHANNELS_N;
        for (int x = 0; x < width; ++x) {
            memcpy(dst + x * pitch, data + x * height * bpp, height * bpp);
        }
        SDL_UnlockTexture(tex);

        if (!SDL_LockTexture(tex_blur, NULL, &pixels, &pitch)) {
            err_log("SDL_LockTexture: %s\n", SDL_GetError());
            return;
        }
        dst = pixels;
        for (int x = 0; x < width; ++x) {
            memcpy(dst + x * pitch, data_blur[BLUR_OUT] + x * height * bpp, height * bpp);
        }
        SDL_UnlockTexture(tex_blur);
    }

    int in_left;
    int in_top;
    int in_width;
    int in_height;
    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;

    draw_screen_get_blur_dims_lite(screen_top_bot, i, view_mode, width, height,
        &in_left, &in_top, &in_width, &in_height, &ctx_left, &ctx_top, &ctx_width, &ctx_height);

    SDL_FRect srcrect = { in_top, in_left, in_height, in_width };
    SDL_FRect dstrect = { ctx_left, ctx_top + ctx_height, ctx_height, ctx_width };
    SDL_FPoint center = { 0, 0 };
    SDL_RenderTextureRotated(sdl_renderer[i], tex_blur, &srcrect, &dstrect, -90, &center, SDL_FLIP_NONE);

    draw_screen_get_dims_lite(screen_top_bot, i, view_mode, width, height, &ctx_left, &ctx_top, &ctx_width, &ctx_height);

    srcrect = (SDL_FRect){ 0, 0, height, width };
    dstrect = (SDL_FRect){ ctx_left, ctx_top + ctx_height, ctx_height, ctx_width };
    center = (SDL_FPoint){ 0, 0 };
    SDL_RenderTextureRotated(sdl_renderer[i], tex, &srcrect, &dstrect, -90, &center, SDL_FLIP_NONE);
}

void ui_renderer_sdl_present(int ctx_top_bot) {
    int i = ctx_top_bot;
    if (i == SCREEN_TOP) {
        if (nk_gui_next) {
            nk_sdl_renderer_render(NK_ANTI_ALIASING_OFF);
            nk_gui_next = 0;
        }
    }
    SDL_RenderPresent(sdl_renderer[i]);
}

#include <math.h>

void ui_renderer_sdl_gen_cursor(stbi_t *image, const unsigned char *base, int width, int height, int channels, float scale) {
    if (channels != GL_CHANNELS_N) {
        return;
    }

    int i = SCREEN_TOP;
    SDL_Texture *tex = SDL_CreateTexture(sdl_renderer[i], SDL_FORMAT, SDL_TEXTUREACCESS_STATIC, width, height);
    if (!tex) {
        return;
    }
    if (!SDL_UpdateTexture(tex, NULL, base, width * channels)) {
        err_log("SDL_UpdateTexture failed: %s\n", SDL_GetError());
        goto final_tex;
    }

    int target_width = roundf(width * scale);
    int target_height = roundf(height * scale);
    SDL_Texture *target = SDL_CreateTexture(sdl_renderer[i], SDL_FORMAT, SDL_TEXTUREACCESS_TARGET, target_width, target_height);
    if (!target) {
        goto final_tex;
    }

    if (!SDL_SetRenderTarget(sdl_renderer[i], target)) {
        err_log("SDL_SetRenderTarget failed: %s\n", SDL_GetError());
        goto final_target;
    }

    if (!SDL_SetRenderDrawColor(sdl_renderer[i], 0, 0, 0, 0)) {
        err_log("SDL_SetRenderDrawColor failed: %s\n", SDL_GetError());
        goto final_target;
    }

    if (!SDL_RenderClear(sdl_renderer[i])) {
        err_log("SDL_RenderClear failed: %s\n", SDL_GetError());
        goto final_target;
    }

    if (!SDL_RenderTexture(sdl_renderer[i], tex, NULL, NULL)) {
        err_log("SDL_RenderTexture failed: %s\n", SDL_GetError());
        goto final_target;
    }

    image->image = malloc(target_width * target_height * channels);
    if (!image->image) {
        goto final_target;
    }

    SDL_Surface *read_surface = SDL_RenderReadPixels(sdl_renderer[i], NULL);
    if (!read_surface) {
        err_log("SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        free(image->image);
        image->image = NULL;
    } else {
        if (read_surface->w != target_width || read_surface->h != target_height) {
            err_log("SDL_RenderReadPixels unexpected: %d %d %d\n", (int)read_surface->format, read_surface->w, read_surface->h);
            free(image->image);
            image->image = NULL;
        } else if (read_surface->format != SDL_FORMAT) {
            const SDL_PixelFormatDetails *fmt = SDL_GetPixelFormatDetails(read_surface->format);
            for (int y = 0; y < read_surface->h; ++y) {
                for (int x = 0; x < read_surface->w; ++x) {
                    uint32_t pix = *(uint32_t *)((const char *)read_surface->pixels + y * read_surface->pitch + x * fmt->bytes_per_pixel);
                    uint32_t r = (pix & fmt->Rmask) >> fmt->Rshift;
                    uint32_t g = (pix & fmt->Gmask) >> fmt->Gshift;
                    uint32_t b = (pix & fmt->Bmask) >> fmt->Bshift;
                    uint32_t a = (pix & fmt->Amask) >> fmt->Ashift;
                    unsigned char *out = image->image + (y * target_width + x) * channels;
                    out[0] = r;
                    out[1] = g;
                    out[2] = b;
                    out[3] = a;
                }
            }
        } else {
            for (int y = 0; y < read_surface->h; ++y) {
                memcpy(image->image + y * target_width * channels, (const char *)read_surface->pixels + y * read_surface->pitch, target_width * channels);
            }
        }
        SDL_DestroySurface(read_surface);
    }
    image->width = target_width;
    image->height = target_height;
    image->channels = channels;

final_target:
    SDL_SetRenderTarget(sdl_renderer[i], NULL);
    SDL_DestroyTexture(target);

final_tex:
    SDL_DestroyTexture(tex);
}
