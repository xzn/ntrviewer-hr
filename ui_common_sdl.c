#include "ui_common_sdl.h"
#include "ui_renderer_sdl.h"
#include "ui_renderer_d3d11.h"
#include "ui_renderer_ogl.h"
#include "ui_input_redirection.h"
#include "main.h"
#include "ikcp.h"
#include <math.h>

int is_renderer_ogl_dbg;

enum ui_renderer_t ui_renderer;
SDL_Window *ui_sdl_win[SCREEN_COUNT];
struct nk_context *ui_nk_ctx;
view_mode_t ui_view_mode;
static int UNUSED ui_view_mode_assert[(sizeof(view_mode_t) == sizeof(int)) - 1];
bool ui_fullscreen;

#ifdef _WIN32
HWND ui_hwnd[SCREEN_COUNT];
HDC ui_hdc[SCREEN_COUNT];
LONG_PTR ui_sdl_wnd_proc[SCREEN_COUNT];
#endif
Uint32 ui_sdl_win_id[SCREEN_COUNT];

int ui_nk_width, ui_nk_height;
float ui_nk_scale;

int ui_win_width[SCREEN_COUNT], ui_win_height[SCREEN_COUNT];
int ui_win_width_drawable[SCREEN_COUNT], ui_win_height_drawable[SCREEN_COUNT];
float ui_win_scale[SCREEN_COUNT];

int ui_ctx_width[SCREEN_COUNT], ui_ctx_height[SCREEN_COUNT];

event_t update_bottom_screen_evt;

int ui_common_sdl_init(void) {
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_USE_D3D9EX, "1");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "direct3d11");
#elif !defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland");
#endif

    if (opt_flag_angle) {
        SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    } else {
        SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "0");
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        err_log("SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    return 0;
}

void ui_common_sdl_destroy(void) {
    SDL_Quit();
}

void ui_view_mode_update(view_mode_t view_mode) {
    SDL_SetCursor(SDL_GetDefaultCursor());

    if (view_mode == VIEW_MODE_SEPARATE)
        SDL_ShowWindow(ui_sdl_win[SCREEN_BOT]);

    SDL_SetWindowFullscreen(ui_sdl_win[SCREEN_TOP], 0);
    SDL_RestoreWindow(ui_sdl_win[SCREEN_TOP]);
    SDL_SetWindowFullscreen(ui_sdl_win[SCREEN_BOT], 0);
    SDL_RestoreWindow(ui_sdl_win[SCREEN_BOT]);

    if (view_mode != VIEW_MODE_SEPARATE)
        SDL_HideWindow(ui_sdl_win[SCREEN_BOT]);

    switch (view_mode) {
        case VIEW_MODE_TOP_BOT:
            SDL_SetWindowSize(ui_sdl_win[SCREEN_TOP], WIN_WIDTH_DEFAULT, WIN_HEIGHT_DEFAULT);
            break;

        case VIEW_MODE_TOP:
            SDL_SetWindowSize(ui_sdl_win[SCREEN_TOP], WIN_WIDTH_DEFAULT, WIN_HEIGHT12_DEFAULT);
            break;

        case VIEW_MODE_BOT:
            SDL_SetWindowSize(ui_sdl_win[SCREEN_TOP], WIN_WIDTH2_DEFAULT, WIN_HEIGHT12_DEFAULT);
            break;

        case VIEW_MODE_SEPARATE:
            SDL_SetWindowSize(ui_sdl_win[SCREEN_TOP], WIN_WIDTH_DEFAULT, WIN_HEIGHT12_DEFAULT);
            SDL_SetWindowSize(ui_sdl_win[SCREEN_BOT], WIN_WIDTH2_DEFAULT, WIN_HEIGHT12_DEFAULT);
            break;
    }

    if (!renderer_single_thread && view_mode == VIEW_MODE_SEPARATE) {
        event_rel(&update_bottom_screen_evt);
    }
}

void ui_window_size_update(int window_top_bot) {
    int i = window_top_bot;

    SDL_GetWindowSize(ui_sdl_win[i], &ui_win_width[i], &ui_win_height[i]);

    if (is_renderer_sdl_renderer()) {
        SDL_GetCurrentRenderOutputSize(sdl_renderer[i], &ui_win_width_drawable[i], &ui_win_height_drawable[i]);
    } else if (is_renderer_sdl_ogl()) {
        SDL_GetWindowSizeInPixels(ui_sdl_win[i], &ui_win_width_drawable[i], &ui_win_height_drawable[i]);
    } else if (is_renderer_d3d11()) {
#ifdef _WIN32
        RECT rect = {};
        GetClientRect(ui_hwnd[i], &rect);
        ui_win_width_drawable[i] = rect.right;
        ui_win_height_drawable[i] = rect.bottom;
#endif
    }

    ui_win_width_drawable[i] = NK_MAX(ui_win_width_drawable[i], 1);
    ui_win_height_drawable[i] = NK_MAX(ui_win_height_drawable[i], 1);

    float scale_x = (float)(ui_win_width_drawable[i]) / (float)(ui_win_width[i]);
    float scale_y = (float)(ui_win_height_drawable[i]) / (float)(ui_win_height[i]);
    scale_x = roundf(scale_x * ui_font_scale_step_factor) / ui_font_scale_step_factor;
    scale_y = roundf(scale_y * ui_font_scale_step_factor) / ui_font_scale_step_factor;
    ui_win_scale[i] = (scale_x + scale_y) * 0.5;

    if (i == SCREEN_TOP) {
        ui_nk_width = ui_win_width[i];
        ui_nk_height = ui_win_height[i];
        ui_nk_scale = ui_win_scale[i];
    }
}

#define FRAME_STAT_EVERY_X_US 1000000
static uint64_t windows_titles_last_tick;
#define WINDOW_TITLE_LEN_MAX 512

static double kcp_get_connection_quality(void)
{
    int fec_count = __atomic_load_n(&kcp_input_fec_count, __ATOMIC_RELAXED);
    int input_count = fec_count ?
        (IUINT32)__atomic_load_n(&kcp_input_pid_count, __ATOMIC_RELAXED) * __atomic_load_n(&kcp_input_fid_count, __ATOMIC_RELAXED) / fec_count : 0;
    double ret = input_count ? (double)__atomic_load_n(&kcp_recv_pid_count, __ATOMIC_RELAXED) / input_count : 0.0;
    return ret * ret * 100;
}

static void ui_kcp_window_title_update(SDL_Window *win, int tick_diff)
{
    char window_title[WINDOW_TITLE_LEN_MAX];
    snprintf(window_title, sizeof(window_title),
             WIN_TITLE " (FPS %03d %03d | %03d %03d)"
                       " (Connection Quality %.1f%%)"
                       " [Reliable Stream Mode%s]",
             __atomic_load_n(&frame_rate_decoded_tracker[SCREEN_TOP], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
             __atomic_load_n(&frame_rate_decoded_tracker[SCREEN_BOT], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
             __atomic_load_n(&frame_rate_displayed_tracker[SCREEN_TOP], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
             __atomic_load_n(&frame_rate_displayed_tracker[SCREEN_BOT], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
             kcp_get_connection_quality(),
             kcp_dq ? " + Delta" : "");
    SDL_SetWindowTitle(win, window_title);
}

static void ui_kcp_windows_titles_update(int ctx_top_bot, int screen_top_bot, int tick_diff)
{
    char window_title[WINDOW_TITLE_LEN_MAX];
    snprintf(window_title, sizeof(window_title),
             ctx_top_bot == SCREEN_TOP
                 ? WIN_TITLE
                 " (FPS %03d | %03d)"
                 " (Connection Quality %.1f%%)"
                 " [Reliable Stream Mode%s]"
                 : WIN_TITLE
                 " (FPS %03d | %03d)",
             __atomic_load_n(&frame_rate_decoded_tracker[screen_top_bot], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / tick_diff,
             __atomic_load_n(&frame_rate_displayed_tracker[screen_top_bot], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / tick_diff,
             kcp_get_connection_quality(),
             kcp_dq ? " + Delta" : "");
    SDL_SetWindowTitle(ui_sdl_win[ctx_top_bot], window_title);
}

void ui_windows_titles_update(void)
{
    uint64_t next_tick = iclock64();
    uint64_t tick_diff = next_tick - windows_titles_last_tick;
    if (tick_diff >= FRAME_STAT_EVERY_X_US)
    {
        int frame_fully_received = __atomic_load_n(&frame_fully_received_tracker, __ATOMIC_RELAXED);
        int frame_lost = __atomic_load_n(&frame_lost_tracker, __ATOMIC_RELAXED);
        double packet_rate = frame_fully_received ? (double)frame_fully_received / (frame_fully_received + frame_lost) * 100 : 0.0;

        int view_mode = __atomic_load_n(&ui_view_mode, __ATOMIC_RELAXED);

        if (view_mode == VIEW_MODE_TOP_BOT)
        {
            if (kcp_active)
            {
                ui_kcp_window_title_update(ui_sdl_win[SCREEN_TOP], (int)tick_diff);
            }
            else
            {
                char window_title[WINDOW_TITLE_LEN_MAX];
                snprintf(
                    window_title, sizeof(window_title),
                    WIN_TITLE
                    " (FPS %03d %03d | %03d %03d)"
                    " (Packet Rate %.1f%%)"
                    " [Compatibility Mode]",
                    __atomic_load_n(&frame_rate_decoded_tracker[SCREEN_TOP], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                    __atomic_load_n(&frame_rate_decoded_tracker[SCREEN_BOT], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                    __atomic_load_n(&frame_rate_displayed_tracker[SCREEN_TOP], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                    __atomic_load_n(&frame_rate_displayed_tracker[SCREEN_BOT], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                    packet_rate);
                SDL_SetWindowTitle(ui_sdl_win[SCREEN_TOP], window_title);
            }
        }
        else
        {
            for (int screen_top_bot = 0; screen_top_bot < SCREEN_COUNT; ++screen_top_bot)
            {
                int ctx_top_bot = screen_top_bot;
                if (view_mode == VIEW_MODE_BOT)
                {
                    ctx_top_bot = SCREEN_TOP;
                    screen_top_bot = SCREEN_BOT;
                }
                if (kcp_active)
                {
                    ui_kcp_windows_titles_update(ctx_top_bot, screen_top_bot, (int)tick_diff);
                }
                else
                {
                    char window_title[WINDOW_TITLE_LEN_MAX];
                    snprintf(
                        window_title, sizeof(window_title),
                        ctx_top_bot == SCREEN_TOP
                            ? WIN_TITLE
                            " (FPS %03d | %03d) "
                            " (Packet Rate %.1f%%)"
                            " [Compatibility Mode]"
                            : WIN_TITLE
                            " (FPS %03d | %03d) ",
                        __atomic_load_n(&frame_rate_decoded_tracker[screen_top_bot], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                        __atomic_load_n(&frame_rate_displayed_tracker[screen_top_bot], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                        packet_rate);
                    SDL_SetWindowTitle(ui_sdl_win[ctx_top_bot], window_title);
                }
                if (view_mode != VIEW_MODE_SEPARATE)
                {
                    break;
                }
            }
        }

        windows_titles_last_tick = next_tick;
        for (int top_bot = 0; top_bot < SCREEN_COUNT; ++top_bot)
        {
            __atomic_store_n(&frame_rate_decoded_tracker[top_bot], 0, __ATOMIC_RELAXED);
            __atomic_store_n(&frame_rate_displayed_tracker[top_bot], 0, __ATOMIC_RELAXED);
            __atomic_store_n(&frame_size_tracker[top_bot], 0, __ATOMIC_RELAXED);
            __atomic_store_n(&delay_between_packet_tracker[top_bot], 0, __ATOMIC_RELAXED);
        }
        __atomic_store_n(&kcp_input_fec_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&kcp_input_fid_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&kcp_input_pid_count, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&kcp_recv_pid_count, 0, __ATOMIC_RELAXED);

        __atomic_store_n(&frame_fully_received_tracker, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&frame_lost_tracker, 0, __ATOMIC_RELAXED);
    }
}

static void draw_screen_dispatch(UNUSED struct rp_buffer_ctx_t *ctx, uint8_t *data, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, UNUSED bool win_shared) {
    if (is_renderer_d3d11()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_d3d11_draw(ctx, data, width, height, screen_top_bot, ctx_top_bot, view_mode, win_shared);
#endif
    } else if (is_renderer_sdl_ogl()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_ogl_draw(ctx, data, width, height, screen_top_bot, ctx_top_bot, view_mode, win_shared);
#endif
    } else if (is_renderer_sdl_renderer()) {
        ui_renderer_sdl_draw(data, width, height, screen_top_bot, ctx_top_bot, view_mode);
    }
    // TODO
}

int draw_screen(struct rp_buffer_ctx_t *ctx, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, bool win_shared) {
    rp_lock_wait(ctx->status_lock);
    enum frame_buffer_status_t status = ctx->status;
    if (ctx->status == FBS_UPDATED_2) {
        int index = ctx->index_ready_display_2;
        ctx->index_ready_display_2 = ctx->index_display_2;
        ctx->index_display_2 = ctx->index_display;
        ctx->index_display = index;
        ctx->status = FBS_UPDATED;
    } else if (ctx->status == FBS_UPDATED) {
        int index = ctx->index_ready_display;
        ctx->index_ready_display = ctx->index_display_2;
        ctx->index_display_2 = ctx->index_display;
        ctx->index_display = index;
        ctx->status = FBS_NOT_UPDATED;
    }
    int index_display = ctx->index_display;
    rp_lock_rel(ctx->status_lock);

    if (status == FBS_NOT_AVAIL)
        return 0;

    uint8_t *data = ctx->screen_decoded[index_display];
    ctx->data_prev = data;
    if (status >= FBS_UPDATED)
    {
        __atomic_add_fetch(&frame_rate_displayed_tracker[screen_top_bot], 1, __ATOMIC_RELAXED);
        draw_screen_dispatch(ctx, data, width, height, screen_top_bot, ctx_top_bot, view_mode, win_shared);
        return 1;
    }
    else
    {
        draw_screen_dispatch(ctx, NULL, width, height, screen_top_bot, ctx_top_bot, view_mode, win_shared);
        return -1;
    }
}

int sdl_win_init(SDL_Window *sdl_win[SCREEN_COUNT], bool ogl) {
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        sdl_win[i] = SDL_CreateWindow(WIN_TITLE,
            WIN_WIDTH_DEFAULT, WIN_HEIGHT_DEFAULT, SDL_WIN_FLAGS_DEFAULT | (ogl ? SDL_WINDOW_OPENGL : 0));
        if (!sdl_win[i]) {
            err_log("SDL_CreateWindow: %s\n", SDL_GetError());
            return -1;
        }
    }

    return 0;
}

void sdl_win_destroy(SDL_Window *sdl_win[SCREEN_COUNT]) {
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        if (sdl_win[i]) {
            SDL_DestroyWindow(sdl_win[i]);
            sdl_win[i] = NULL;
        }
    }
}

void sdl_set_wminfo(void) {
#ifdef _WIN32
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        SDL_PropertyID id = SDL_GetWindowProperties(ui_sdl_win[i]);
        ui_hwnd[i] = (HWND)SDL_GetPointerProperty(id, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        ui_hdc[i] = (HDC)SDL_GetPointerProperty(id, SDL_PROP_WINDOW_WIN32_HDC_POINTER, NULL);
    }
#endif
}

void sdl_reset_wminfo(void) {
#ifdef _WIN32
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        ui_hdc[i] = NULL;
        ui_hwnd[i] = NULL;
    }
#endif
}

void draw_screen_get_dims_lite(
    int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, int width, int height,
    int *out_ctx_left,
    int *out_ctx_top,
    int *out_ctx_width,
    int *out_ctx_height
) {
    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;

    int i = ctx_top_bot;

    if (view_mode == VIEW_MODE_TOP_BOT) {
        ctx_height = (double)ui_win_height[i] / 2;
        if ((double)ui_win_width[i] / width * height > ctx_height) {
            ctx_width = (double)ctx_height / height * width;
            ctx_left = (double)(ui_win_width[i] - ctx_width) / 2;
            ctx_top = 0;
        } else {
            ctx_height = (double)ui_win_width[i] / width * height;
            ctx_left = 0;
            ctx_width = ui_win_width[i];
            ctx_top = (double)ui_win_height[i] / 2 - ctx_height;
        }

        if (screen_top_bot != SCREEN_TOP) {
            ctx_top = (double)ui_win_height[i] / 2;
        }
    } else {
        ctx_height = (double)ui_win_height[i];
        if ((double)ui_win_width[i] / width * height > ctx_height) {
            ctx_width = (double)ctx_height / height * width;
            ctx_left = (double)(ui_win_width[i] - ctx_width) / 2;
            ctx_top = 0;
        } else {
            ctx_height = (double)ui_win_width[i] / width * height;
            ctx_left = 0;
            ctx_width = ui_win_width[i];
            ctx_top = ((double)ui_win_height[i] - ctx_height) / 2;
        }
    }

    *out_ctx_left = ctx_left;
    *out_ctx_top = ctx_top;
    *out_ctx_width = ctx_width;
    *out_ctx_height = ctx_height;
}

void draw_screen_get_dims(
    int screen_top_bot, int ctx_top_bot, int win_shared, view_mode_t view_mode, int width, int height,
    double *out_ctx_left_f,
    double *out_ctx_top_f,
    double *out_ctx_right_f,
    double *out_ctx_bot_f,
    int *out_ctx_width,
    int *out_ctx_height,
    int *out_win_width_drawable,
    int *out_win_height_drawable
) {
    double ctx_left_f;
    double ctx_top_f;
    double ctx_right_f;
    double ctx_bot_f;
    int ctx_width;
    int ctx_height;
    int win_width_drawable;
    int win_height_drawable;

    int i = ctx_top_bot;

    if (win_shared) {
        win_width_drawable = ctx_width = ui_ctx_width[screen_top_bot];
        win_height_drawable = ctx_height = ui_ctx_height[screen_top_bot];
        ctx_left_f = -1.0f;
        ctx_top_f = 1.0f;
        ctx_right_f = 1.0f;
        ctx_bot_f = -1.0f;
    } else {
        if (view_mode == VIEW_MODE_TOP_BOT) {
            win_width_drawable = ui_win_width_drawable[i];
            win_height_drawable = ui_win_height_drawable[i];

            ctx_height = (double)ui_win_height_drawable[i] / 2;
            int ctx_left;
            int ctx_top;
            if ((double)ui_win_width_drawable[i] / width * height > ctx_height) {
                ctx_width = (double)ctx_height / height * width;
                ctx_left = (double)(ui_win_width_drawable[i] - ctx_width) / 2;
                ctx_top = 0;
            } else {
                ctx_height = (double)ui_win_width_drawable[i] / width * height;
                ctx_left = 0;
                ctx_width = ui_win_width_drawable[i];
                ctx_top = (double)ui_win_height_drawable[i] / 2 - ctx_height;
            }

            if (screen_top_bot == SCREEN_TOP) {
                ctx_left_f = (double)ctx_left / ui_win_width_drawable[i] * 2 - 1;
                ctx_top_f = 1 - (double)ctx_top / ui_win_height_drawable[i] * 2;
                ctx_right_f = -ctx_left_f;
                ctx_bot_f = 0;
            } else {
                ctx_left_f = (double)ctx_left / ui_win_width_drawable[i] * 2 - 1;
                ctx_top_f = 0;
                ctx_right_f = -ctx_left_f;
                ctx_bot_f = -1 + (double)ctx_top / ui_win_height_drawable[i] * 2;
            }
        } else {
            win_width_drawable = ui_win_width_drawable[i];
            win_height_drawable = ui_win_height_drawable[i];

            ctx_height = (double)ui_win_height_drawable[i];
            int ctx_left;
            int ctx_top;
            if ((double)ui_win_width_drawable[i] / width * height > ctx_height) {
                ctx_width = (double)ctx_height / height * width;
                ctx_left = (double)(ui_win_width_drawable[i] - ctx_width) / 2;
                ctx_top = 0;
            } else {
                ctx_height = (double)ui_win_width_drawable[i] / width * height;
                ctx_left = 0;
                ctx_width = ui_win_width_drawable[i];
                ctx_top = ((double)ui_win_height_drawable[i] - ctx_height) / 2;
            }

            ctx_left_f = (double)ctx_left / ui_win_width_drawable[i] * 2 - 1;
            ctx_top_f = 1 - (double)ctx_top / ui_win_height_drawable[i] * 2;
            ctx_right_f = -ctx_left_f;
            ctx_bot_f = -ctx_top_f;
        }
    }

    *out_ctx_left_f = ctx_left_f;
    *out_ctx_top_f = ctx_top_f;
    *out_ctx_right_f = ctx_right_f;
    *out_ctx_bot_f = ctx_bot_f;
    *out_ctx_width = ctx_width;
    *out_ctx_height = ctx_height;
    *out_win_width_drawable = win_width_drawable;
    *out_win_height_drawable = win_height_drawable;
}

void generate_cursor_image(stbi_t *image, const unsigned char *base, int width, int height, int channels, float scale) {
    if (is_renderer_d3d11()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_d3d11_gen_cursor(image, base, width, height, channels, scale);
#endif
    } else if (is_renderer_sdl_ogl()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_ogl_gen_cursor(image, base, width, height, channels, scale);
#endif
    } else if (is_renderer_sdl_renderer()) {
        ui_renderer_sdl_gen_cursor(image, base, width, height, channels, scale);
    }
    // TODO
}
