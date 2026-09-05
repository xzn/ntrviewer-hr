#include "ui_common_sdl.h"
#include "ui_renderer_sdl.h"
#include "ui_renderer_d3d11.h"
#include "ui_renderer_ogl.h"
#include "ui_renderer_vulkan.h"
#include "ui_input_redirection.h"
#include "main.h"
#include "ntr_common.h"
#include "ntr_audio.h"
#include "ikcp.h"
#include <math.h>

bool is_win_size_in_pixels;
bool is_cursor_size_in_pixels;
int is_renderer_ogl_dbg;
int is_renderer_vk_dbg;

enum ui_renderer_t ui_renderer;
int ui_blur_radius = 15;
int ui_blur_iter = 1;
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
int ui_ctx_width_drawable[SCREEN_COUNT], ui_ctx_height_drawable[SCREEN_COUNT];

event_t update_bottom_screen_evt;

static void change_working_directory_to_exe_path(void);
void init_local_network_access(void);
int ui_common_sdl_init(void) {
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D_THREADSAFE, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_USE_D3D9EX, "1");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "direct3d11");
#elif !defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "wayland,x11");
#endif

    if (opt_flag_angle) {
        SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
    } else {
        SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "0");
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        err_log("SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

#ifdef _WIN32
    is_win_size_in_pixels = 1;
    is_cursor_size_in_pixels = 1;
#else
    const char *video_driver = SDL_GetCurrentVideoDriver();
    if (video_driver && strcmp(video_driver, "x11") == 0) {
        is_win_size_in_pixels = 1;
    } else {
        is_win_size_in_pixels = 0;
    }
    is_cursor_size_in_pixels = 0;
#endif

    change_working_directory_to_exe_path();
#ifdef __APPLE__
    init_local_network_access();
#endif
    return 0;
}

void ui_common_sdl_destroy(void) {
    ntr_audio_shutdown();
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

    float scale[SCREEN_COUNT] = { 1.0, 1.0 };
    if (is_win_size_in_pixels) {
        scale[SCREEN_TOP] = ui_win_scale[SCREEN_TOP];
        scale[SCREEN_BOT] = ui_win_scale[SCREEN_BOT];
    }
    switch (view_mode) {
        case VIEW_MODE_TOP_BOT:
            SDL_SetWindowSize(ui_sdl_win[SCREEN_TOP], WIN_WIDTH_DEFAULT * scale[SCREEN_TOP], WIN_HEIGHT_DEFAULT * scale[SCREEN_TOP]);
            break;

        case VIEW_MODE_TOP:
            SDL_SetWindowSize(ui_sdl_win[SCREEN_TOP], WIN_WIDTH_DEFAULT * scale[SCREEN_TOP], WIN_HEIGHT12_DEFAULT * scale[SCREEN_TOP]);
            break;

        case VIEW_MODE_BOT:
            SDL_SetWindowSize(ui_sdl_win[SCREEN_TOP], WIN_WIDTH2_DEFAULT * scale[SCREEN_TOP], WIN_HEIGHT12_DEFAULT * scale[SCREEN_TOP]);
            break;

        case VIEW_MODE_SEPARATE:
            SDL_SetWindowSize(ui_sdl_win[SCREEN_TOP], WIN_WIDTH_DEFAULT * scale[SCREEN_TOP], WIN_HEIGHT12_DEFAULT * scale[SCREEN_TOP]);
            SDL_SetWindowSize(ui_sdl_win[SCREEN_BOT], WIN_WIDTH2_DEFAULT * scale[SCREEN_BOT], WIN_HEIGHT12_DEFAULT * scale[SCREEN_BOT]);
            break;
    }

    if (!renderer_single_thread && view_mode == VIEW_MODE_SEPARATE) {
        event_rel(&update_bottom_screen_evt);
    }
}

void ui_window_size_update(int window_top_bot) {
    int i = window_top_bot;

    if (!is_win_size_in_pixels) {
        SDL_GetWindowSize(ui_sdl_win[i], &ui_win_width[i], &ui_win_height[i]);
        ui_win_width[i] = NK_MAX(ui_win_width[i], 1);
        ui_win_height[i] = NK_MAX(ui_win_height[i], 1);
    }

    if (is_renderer_sdl_renderer()) {
        SDL_GetRenderOutputSize(sdl_renderer[i], &ui_win_width_drawable[i], &ui_win_height_drawable[i]);
    } else if (is_renderer_sdl_ogl() || is_renderer_vulkan()) {
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

    if (is_win_size_in_pixels) {
        ui_win_scale[i] = SDL_GetWindowDisplayScale(ui_sdl_win[i]);
        ui_win_scale[i] = roundf(ui_win_scale[i] * ui_font_scale_step_factor) / ui_font_scale_step_factor;
        ui_win_width[i] = ui_win_width_drawable[i] / ui_win_scale[i];
        ui_win_height[i] = ui_win_height_drawable[i] / ui_win_scale[i];
    } else {
        float scale_x = (float)(ui_win_width_drawable[i]) / (float)(ui_win_width[i]);
        float scale_y = (float)(ui_win_height_drawable[i]) / (float)(ui_win_height[i]);
        scale_x = roundf(scale_x * ui_font_scale_step_factor) / ui_font_scale_step_factor;
        scale_y = roundf(scale_y * ui_font_scale_step_factor) / ui_font_scale_step_factor;
        ui_win_scale[i] = (scale_x + scale_y) * 0.5;
    }

    if (i == SCREEN_TOP) {
        ui_nk_width = ui_win_width[i];
        ui_nk_height = ui_win_height[i];
        ui_nk_scale = ui_win_scale[i];
    }
}

#define FRAME_STAT_EVERY_X_US 1000000
static uint64_t windows_titles_last_tick;
#define WINDOW_TITLE_LEN_MAX 512

// consumes the kcp counters; call once per stat interval
static double kcp_get_connection_quality(bool *had_input)
{
    int fec_count = __atomic_exchange_n(&kcp_input_fec_count, 0, __ATOMIC_RELAXED);
    int input_count = fec_count ?
        (IUINT32)__atomic_exchange_n(&kcp_input_pid_count, 0, __ATOMIC_RELAXED) * __atomic_exchange_n(&kcp_input_fid_count, 0, __ATOMIC_RELAXED) / fec_count : 0;
    double ret = input_count ? (double)__atomic_exchange_n(&kcp_recv_pid_count, 0, __ATOMIC_RELAXED) / input_count : 0.0;
    if (had_input)
        *had_input = input_count > 0;
    return ret * ret * 100;
}

// AIMD quality controller: drop fast on loss, recover slowly; slider is the ceiling
static int auto_quality_good_streak;
static int auto_quality_cooldown;
static int jpeg_quality_temp;
static void ntr_auto_quality_tick(double health, bool traffic)
{
    if (!ntr_auto_quality || !ntr_jpeg_quality_auto || jpeg_quality_temp != ntr_rp_config.jpeg_quality) {
        jpeg_quality_temp = ntr_rp_config.jpeg_quality;
        ntr_jpeg_quality_auto = jpeg_quality_temp;
        auto_quality_good_streak = 0;
        auto_quality_cooldown = 2;
        return;
    }
    if (!traffic)
        return;
    // ignore outlier
    if (health < 25.0)
        return;
    // cooldown after a change: let the stream settle before re-measuring
    if (auto_quality_cooldown > 0) {
        --auto_quality_cooldown;
        auto_quality_good_streak = 0;
        return;
    }
    int quality = ntr_jpeg_quality_auto;
    int quality_prev = quality;
    if (health < 90.0) {
        quality *= health * 0.009;
        auto_quality_good_streak = 0;
    } else if (health >= 97.5) {
        if (++auto_quality_good_streak >= 1) {
            auto_quality_good_streak = 0;
            quality +=
                jpeg_quality_temp - quality > 25 ? 5 :
                jpeg_quality_temp - quality > 10 ? 3 : 1;
        }
    } else {
        auto_quality_good_streak = 0;
    }
    quality = quality < NTR_JPEG_QUALITY_MIN ? NTR_JPEG_QUALITY_MIN
        : quality > jpeg_quality_temp ? jpeg_quality_temp : quality;
    if (quality != quality_prev) {
        auto_quality_cooldown = 1;
    }
    ntr_jpeg_quality_auto = quality;
}

static void ui_kcp_window_title_update(SDL_Window *win, int tick_diff, double connection_quality)
{
    char window_title[WINDOW_TITLE_LEN_MAX];
    snprintf(window_title, sizeof(window_title),
             WIN_TITLE " (FPS %03d/%03d %03d/%03d)"
                       " (Connection Quality %.1f%%)"
                       " [%s]",
             __atomic_exchange_n(&frame_rate_displayed_tracker[SCREEN_TOP], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
             __atomic_exchange_n(&frame_rate_decoded_tracker[SCREEN_TOP], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
             __atomic_exchange_n(&frame_rate_displayed_tracker[SCREEN_BOT], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
             __atomic_exchange_n(&frame_rate_decoded_tracker[SCREEN_BOT], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
             connection_quality,
             is_lossless ?
                kcp_dq ? "Lossless RS, Delta" : "Lossless RS" :
                kcp_dq ? "JPEG RS, Delta" : "JPEG RS");
    SDL_SetWindowTitle(win, window_title);
}

static void ui_kcp_windows_titles_update(int ctx_top_bot, int screen_top_bot, int tick_diff, double connection_quality)
{
    char window_title[WINDOW_TITLE_LEN_MAX];
    snprintf(window_title, sizeof(window_title),
             ctx_top_bot == SCREEN_TOP
                 ? WIN_TITLE
                 " (FPS %03d/%03d)"
                 " (Connection Quality %.1f%%)"
                 " [%s]"
                 : WIN_TITLE
                 " (FPS %03d/%03d)",
             __atomic_exchange_n(&frame_rate_displayed_tracker[screen_top_bot], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / tick_diff,
             __atomic_exchange_n(&frame_rate_decoded_tracker[screen_top_bot], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / tick_diff,
             connection_quality,
             is_lossless ?
                kcp_dq ? "Lossless RS, Delta" : "Lossless RS" :
                kcp_dq ? "JPEG RS, Delta" : "JPEG RS");
    SDL_SetWindowTitle(ui_sdl_win[ctx_top_bot], window_title);
}

void ui_windows_titles_update(void)
{
    uint64_t next_tick = iclock64();
    uint64_t tick_diff = next_tick - windows_titles_last_tick;
    if (tick_diff >= FRAME_STAT_EVERY_X_US)
    {
        int frame_fully_received = __atomic_exchange_n(&frame_fully_received_tracker, 0, __ATOMIC_RELAXED);
        int frame_lost = __atomic_exchange_n(&frame_lost_tracker, 0, __ATOMIC_RELAXED);
        double packet_rate = frame_fully_received ? (double)frame_fully_received / (frame_fully_received + frame_lost) * 100 : 0.0;

        int packet_received = __atomic_exchange_n(&packet_received_tracker, 0, __ATOMIC_RELAXED);
        int packet_should_receive = __atomic_exchange_n(&packet_should_receive_tracker, 0, __ATOMIC_RELAXED);
        double lossless_rate = packet_should_receive ? packet_rate * packet_received / packet_should_receive : 0.0;

        int view_mode = __atomic_load_n(&ui_view_mode, __ATOMIC_RELAXED);

        bool kcp_had_input = false;
        double kcp_quality = kcp_active ? kcp_get_connection_quality(&kcp_had_input) : 0.0;

        if (view_mode == VIEW_MODE_TOP_BOT)
        {
            if (kcp_active)
            {
                ui_kcp_window_title_update(ui_sdl_win[SCREEN_TOP], (int)tick_diff, kcp_quality);
            } else {
                char window_title[WINDOW_TITLE_LEN_MAX];
                snprintf(
                    window_title, sizeof(window_title),
                    WIN_TITLE
                    " (FPS %03d/%03d %03d/%03d)"
                    " (Packet Rate %.1f%%)"
                    " [%s UDP]",
                    __atomic_exchange_n(&frame_rate_displayed_tracker[SCREEN_TOP], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                    __atomic_exchange_n(&frame_rate_decoded_tracker[SCREEN_TOP], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                    __atomic_exchange_n(&frame_rate_displayed_tracker[SCREEN_BOT], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                    __atomic_exchange_n(&frame_rate_decoded_tracker[SCREEN_BOT], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                    is_lossless ? lossless_rate : packet_rate,
                    is_lossless ? "Uncompresssed" : "JPEG");
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
                    ui_kcp_windows_titles_update(ctx_top_bot, screen_top_bot, (int)tick_diff, kcp_quality);
                } else {
                    char window_title[WINDOW_TITLE_LEN_MAX];
                    snprintf(
                        window_title, sizeof(window_title),
                        ctx_top_bot == SCREEN_TOP
                            ? WIN_TITLE
                            " (FPS %03d/%03d) "
                            " (Packet Rate %.1f%%)"
                            " [%s UDP]"
                            : WIN_TITLE
                            " (FPS %03d/%03d) ",
                        __atomic_exchange_n(&frame_rate_displayed_tracker[screen_top_bot], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                        __atomic_exchange_n(&frame_rate_decoded_tracker[screen_top_bot], 0, __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
                        is_lossless ? lossless_rate : packet_rate,
                        is_lossless ? "Uncompresssed" : "JPEG");
                    SDL_SetWindowTitle(ui_sdl_win[ctx_top_bot], window_title);
                }
                if (view_mode != VIEW_MODE_SEPARATE)
                {
                    break;
                }
            }
        }

        ntr_auto_quality_tick(
            kcp_active ? kcp_quality : packet_rate,
            kcp_active ? kcp_had_input : (frame_fully_received + frame_lost) > 0);

        windows_titles_last_tick = next_tick;
        for (int top_bot = 0; top_bot < SCREEN_COUNT; ++top_bot)
        {
            __atomic_store_n(&frame_size_tracker[top_bot], 0, __ATOMIC_RELAXED);
            __atomic_store_n(&delay_between_packet_tracker[top_bot], 0, __ATOMIC_RELAXED);
        }
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
    } else if (is_renderer_vulkan()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_vk_draw(data, ctx->data_prev, width, height, screen_top_bot, ctx_top_bot, view_mode);
#endif
    } else if (is_renderer_sdl_renderer()) {
        ui_renderer_sdl_draw(data, ctx->data_prev, width, height, screen_top_bot, ctx_top_bot, view_mode);
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
    struct rp_dims *dims = &ctx->dims_decoded[index_display];
    if (dims->width) {
        height = dims->width;
    }
    if (dims->height) {
        width = dims->height;
    }
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

int sdl_win_init(SDL_Window *sdl_win[SCREEN_COUNT], SDL_WindowFlags aflags) {
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        sdl_win[i] = SDL_CreateWindow(WIN_TITLE,
            WIN_WIDTH_DEFAULT, WIN_HEIGHT_DEFAULT, SDL_WIN_FLAGS_DEFAULT | aflags);
        if (!sdl_win[i]) {
            err_log("SDL_CreateWindow (%d): %s\n", i, SDL_GetError());
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
        SDL_PropertiesID id = SDL_GetWindowProperties(ui_sdl_win[i]);
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

void draw_screen_get_blur_dims_lite(
    int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, int in_width, int in_height,
    int *out_left,
    int *out_top,
    int *out_width,
    int *out_height,
    int *out_ctx_left,
    int *out_ctx_top,
    int *out_ctx_width,
    int *out_ctx_height
) {
    int left;
    int top;
    int width;
    int height;
    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;

    int i = ctx_top_bot;

    ctx_height = view_mode == VIEW_MODE_TOP_BOT ? (double)ui_win_height[i] / 2 : ui_win_height[i];
    ctx_width = ui_win_width[i];
    if ((double)ctx_width / in_width * in_height > ctx_height) {
        width = in_width;
        height = (double)width / ctx_width * ctx_height;
        left = 0;
        top = ((double)in_height - height) / 2;
    } else {
        height = in_height;
        width = (double)height / ctx_height * ctx_width;
        top = 0;
        left = ((double)in_width - width) / 2;
    }

    ctx_left = 0;
    if (view_mode == VIEW_MODE_TOP_BOT && screen_top_bot != SCREEN_TOP) {
        ctx_top = (double)ui_win_height[i] / 2;
    } else {
        ctx_top = 0;
    }

    *out_left = left;
    *out_top = top;
    *out_width = width;
    *out_height = height;
    *out_ctx_left = ctx_left;
    *out_ctx_top = ctx_top;
    *out_ctx_width = ctx_width;
    *out_ctx_height = ctx_height;
}

void draw_screen_get_blur_dims_win_shared(
    int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, int win_shared, int in_width, int in_height,
    int *out_left,
    int *out_top,
    int *out_width,
    int *out_height,
    int *out_ctx_left,
    int *out_ctx_top,
    int *out_ctx_width,
    int *out_ctx_height
) {
    int left;
    int top;
    int width;
    int height;
    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;

    int i = ctx_top_bot;

    if (win_shared) {
        ctx_left = 0;
        ctx_top = 0;
        ctx_width = ui_win_width_drawable[SCREEN_TOP];
        ctx_height = (double)ui_win_height_drawable[SCREEN_TOP] / 2;
    } else {
        if (view_mode == VIEW_MODE_TOP_BOT) {
            ctx_left = 0;
            ctx_top = 0;
            ctx_width = ui_win_width_drawable[i];
            ctx_height = (double)ui_win_height_drawable[i] / 2;

            if (screen_top_bot != SCREEN_TOP) {
                ctx_top = (double)ui_win_height_drawable[i] / 2;
            }
        } else {
            ctx_left = 0;
            ctx_top = 0;
            ctx_width = ui_win_width_drawable[i];
            ctx_height = (double)ui_win_height_drawable[i];
        }
    }
    left = ctx_left;
    top = ctx_top;
    width = ctx_width;
    height = ctx_height;

    if ((double)width / in_width * in_height > height) {
        height = (double)width / in_width * in_height;
        top -= ((double)height - ctx_height) / 2;
    } else {
        width = (double)height / in_height * in_width;
        left -= ((double)width - ctx_width) / 2;
    }

    *out_left = left;
    *out_top = top;
    *out_width = width;
    *out_height = height;
    *out_ctx_left = ctx_left;
    *out_ctx_top = ctx_top;
    *out_ctx_width = ctx_width;
    *out_ctx_height = ctx_height;
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

void draw_screen_get_dims_win_shared(
    int screen_top_bot, int ctx_top_bot, int width, int height,
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
        ctx_top = 0;
    }

    *out_ctx_left = ctx_left;
    *out_ctx_top = ctx_top;
    *out_ctx_width = ctx_width;
    *out_ctx_height = ctx_height;
}

static double blur_weights[UI_BLUR_RADIUS_MAX];
static double blur_offsets[UI_BLUR_RADIUS_MAX];

static int blur_radius_prev;

#include "ui_main_nk.h"
int calculate_blur_weights_and_offsets(double out_weights[UI_BLUR_RADIUS_MAX], double out_offsets[UI_BLUR_RADIUS_MAX]) {
    rp_lock_wait(ui_nk_lock);

    const int radius = ui_blur_radius;

    if (blur_radius_prev == radius) {
        goto end;
    }

    const double count = radius * 2 - 1;
    blur_weights[0] = 1 / count;
    for (int i = 1; i < radius; ++i) {
        blur_weights[i] = 1 / count * 2;
    }

    blur_offsets[0] = -(radius - 1);
    for (int i = 1; i < radius; ++i) {
        blur_offsets[i] = blur_offsets[0] + i * 2 - 0.5;
    }

    blur_radius_prev = radius;

end:
    memcpy(out_weights, blur_weights, sizeof(blur_weights));
    memcpy(out_offsets, blur_offsets, sizeof(blur_offsets));
    rp_lock_rel(ui_nk_lock);
    return blur_radius_prev;
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
    } else if (is_renderer_vulkan()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_vk_gen_cursor(image, base, width, height, channels, scale);
#endif
    } else if (is_renderer_sdl_renderer()) {
        ui_renderer_sdl_gen_cursor(image, base, width, height, channels, scale);
    }
    // TODO
}

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif !defined(_WIN32)
#include <linux/limits.h>
#endif

static void change_working_directory_to_exe_path(void) {
#ifdef _WIN32
    DWORD path_len = MAX_PATH;
    LPWSTR path = NULL;
    while (1) {
        path = malloc(path_len * sizeof(*path));
        DWORD ret = GetModuleFileNameW(NULL, path, path_len);
        if (!ret) {
            err_log("get path len for current exe path failed: %d\n", (int)GetLastError());
            return;
        }
        if (ret < path_len) {
            path_len = ret;
            break;
        }
        free(path);
        path_len *= 2;
    }
    // err_log("current exe path: %ls\n", path);
    LPWSTR path_end = wcsrchr(path, L'\\');
    if (path_end) {
        *path_end = 0;
        if (!SetCurrentDirectoryW(path)) {
            err_log("failed to set working directory: %d\n", (int)GetLastError());
        }
    }
    free(path);
#elif __APPLE__
    uint32_t path_len = 0;
    _NSGetExecutablePath(NULL, &path_len);
    if (!path_len) {
        err_log("get path len for current exe path failed.\n");
        return;
    }
    char *path = malloc(path_len);
    if (_NSGetExecutablePath(path, &path_len)) {
        err_log("get current exe path failed.\n");
    } else {
        // err_log("current exe path: %s\n", path);
        char *path_end = strrchr(path, '/');
        if (path_end) {
            *path_end = 0;
            if (chdir(path)) {
                err_log("failed to set working directory: %d\n", errno);
            }
        }
    }
    free(path);
#else
    size_t path_len = PATH_MAX;
    char *path = NULL;
    while (1) {
        path = malloc(path_len * sizeof(*path));
        ssize_t ret = readlink("/proc/self/exe", path, path_len);
        if (ret < 0) {
            err_log("get path len for current exe path failed: %d\n", errno);
            return;
        }
        if ((size_t)ret < path_len) {
            path_len = ret;
            break;
        }
        free(path);
        path_len *= 2;
    }
    path[path_len] = 0;
    // err_log("current exe path: %s\n", path);
    char *path_end = strrchr(path, '/');
    if (path_end) {
        *path_end = 0;
        if (chdir(path)) {
            err_log("failed to set working directory: %d\n", errno);
        }
    }
    free(path);
#endif
}
