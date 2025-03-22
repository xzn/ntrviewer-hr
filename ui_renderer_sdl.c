#include "main.h"
#include "ui_common_sdl.h"
#include "ui_renderer_sdl.h"

#include "const.h"

#include "nuklear_sdl_renderer.h"

SDL_Renderer *sdl_renderer[SCREEN_COUNT];

static SDL_Window *sdl_win[SCREEN_COUNT];
static SDL_Texture *sdl_texture[SCREEN_COUNT][SCREEN_COUNT];
static struct nk_context *nk_ctx;

static int sdl_texture_init(void) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        for (int i = 0; i < SCREEN_COUNT; ++i) {
            sdl_texture[j][i] = SDL_CreateTexture(sdl_renderer[j], SDL_FORMAT, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, i == SCREEN_TOP ? SCREEN_HEIGHT0 : SCREEN_HEIGHT1);
            if (!sdl_texture[j][i]) {
                err_log("SDL_CreateTexture: %s\n", SDL_GetError());
                return -1;
            }
        }
    }

    return 0;
}

static void sdl_texture_destroy(void) {
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        for (int i = 0; i < SCREEN_COUNT; ++i) {
            if (sdl_texture[j][i]) {
                SDL_DestroyTexture(sdl_texture[j][i]);
                sdl_texture[j][i] = NULL;
            }
        }
    }
}

static int sdl_renderer_init(void) {
    bool renderer_hw = is_renderer_sdl_hw();
    char *renderer_name = SDL_getenv(SDL_HINT_RENDER_DRIVER);

    int renderer_index = -1;
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        int num_renderer = SDL_GetNumRenderDrivers();
        if (num_renderer < 0) {
            err_log("SDL_GetNumRenderDrivers: %s\n", SDL_GetError());
            return -1;
        }

        SDL_RendererInfo info = {};
        if (i == SCREEN_TOP) {
            if (renderer_name) {
                for (int j = 0; j < num_renderer; ++j) {
                    if (SDL_GetRenderDriverInfo(j, &info)) {
                        err_log("SDL_GetRenderDriverInfo: %s\n", SDL_GetError());
                        return -1;
                    }

// D3D12 would crash when using multiple windows
#define TRY_CREATE_RENDERER() ({ \
    if (strcmp(info.name, "direct3d12") == 0) \
        continue; \
    sdl_renderer[i] = SDL_CreateRenderer(sdl_win[i], j, 0); \
    if (!sdl_renderer[i]) { \
        err_log("SDL_CreateRenderer: %s\n", SDL_GetError()); \
        continue; \
    } \
    renderer_index = j; \
 \
    if (strcmp(info.name, "direct3d11") == 0) { \
        renderer_evt_sync = 1; \
    } else { \
        renderer_single_thread = 1; \
    } \
    break; \
})

                    if (
                        strcmp(info.name, renderer_name) == 0 &&
                        (
                            (renderer_hw && (info.flags & SDL_RENDERER_ACCELERATED)) ||
                            (!renderer_hw && (info.flags & SDL_RENDERER_SOFTWARE))
                        )
                    ) {

                        TRY_CREATE_RENDERER();
                        break;
                    }
                }
            }

            if (renderer_index < 0) {
                for (int j = 0; j < num_renderer; ++j) {
                    if (SDL_GetRenderDriverInfo(j, &info)) {
                        err_log("SDL_GetRenderDriverInfo: %s\n", SDL_GetError());
                        return -1;
                    }

                    if (renderer_hw && !(info.flags & SDL_RENDERER_ACCELERATED)) {
                        continue;
                    }

                    if (!renderer_hw && !(info.flags & SDL_RENDERER_SOFTWARE)) {
                        continue;
                    }

                    TRY_CREATE_RENDERER();
                    break;
                }
            }
        } else {
            sdl_renderer[i] = SDL_CreateRenderer(sdl_win[i], renderer_index, 0);
            if (!sdl_renderer[i]) {
                err_log("SDL_CreateRenderer: %s\n", SDL_GetError());
                return -1;
            }
        }

        if (!sdl_renderer[i]) {
            return -1;
        }

        SDL_RenderSetVSync(sdl_renderer[i], 1);

        if (i == SCREEN_TOP) {
            err_log("%s %s\n", info.name ? info.name : "", renderer_single_thread ? "single thread" : renderer_evt_sync ? "evt sync" : "");
        }
    }

    if (sdl_texture_init()) {
        return -1;
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
    SDL_RenderSetScale(sdl_renderer[i], ui_win_scale[i], ui_win_scale[i]);
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

void ui_renderer_sdl_draw(uint8_t *data, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode) {
    int i = ctx_top_bot;
    SDL_Texture *tex = sdl_texture[i][screen_top_bot];

    if (data) {
        void *pixels;
        int pitch;
        if (SDL_LockTexture(tex, NULL, &pixels, &pitch) < 0) {
            err_log("SDL_LockTexture: %s\n", SDL_GetError());
            return;
        }

        uint8_t *dst = pixels;
        const int bpp = GL_CHANNELS_N;
        for (int x = 0; x < width; ++x) {
            memcpy(dst + x * pitch, data + x * height * bpp, height * bpp);
        }

        SDL_UnlockTexture(tex);
    }

    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;

    draw_screen_get_dims_lite(screen_top_bot, i, view_mode, width, height, &ctx_left, &ctx_top, &ctx_width, &ctx_height);

    SDL_Rect rect = { ctx_left, ctx_top + ctx_height, ctx_height, ctx_width };
    SDL_Point center = { 0, 0 };
    SDL_RenderCopyEx(sdl_renderer[i], tex, NULL, &rect, -90, &center, SDL_FLIP_NONE);
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
    if (SDL_UpdateTexture(tex, NULL, base, width * channels) < 0) {
        err_log("SDL_UpdateTexture failed: %s", SDL_GetError());
        goto final_tex;
    }

    int target_width = roundf(width * scale);
    int target_height = roundf(height * scale);
    SDL_Texture *target = SDL_CreateTexture(sdl_renderer[i], SDL_FORMAT, SDL_TEXTUREACCESS_TARGET, target_width, target_height);
    if (!target) {
        goto final_tex;
    }

    if (SDL_SetRenderTarget(sdl_renderer[i], target) < 0) {
        err_log("SDL_SetRenderTarget failed: %s", SDL_GetError());
        goto final_target;
    }

    if (SDL_RenderCopy(sdl_renderer[i], tex, NULL, NULL) < 0) {
        err_log("SDL_RenderCopy failed: %s", SDL_GetError());
        goto final_target;
    }

    image->image = malloc(target_width * target_height * channels);
    if (!image->image) {
        goto final_target;
    }

    if (SDL_RenderReadPixels(sdl_renderer[i], NULL, SDL_FORMAT, image->image, target_width * channels) < 0) {
        err_log("SDL_RenderReadPixels failed: %s", SDL_GetError());
        free(image->image);
        image->image = NULL;
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
