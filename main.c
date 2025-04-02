#include "main.h"
#include "rp_syn.h"

#include "ui_common_sdl.h"
#include "ui_renderer_sdl.h"
#include "ntr_common.h"
#ifndef USE_SDL_RENDERER_ONLY
#include "ui_renderer_d3d11.h"
#include "ui_renderer_ogl.h"
#include "ui_renderer_vulkan.h"
#ifdef _WIN32
#include "nuklear_d3d11.h"
#include "ui_compositor_csc.h"
#endif
#include "nuklear_sdl_gl3.h"
#include "nuklear_sdl_gles2.h"
#include "nuklear_sdl_vulkan.h"
#endif
#include "nuklear_sdl_renderer.h"
#include "ntr_hb.h"
#include "ntr_rp.h"
#include "ui_main_nk.h"
#include "ui_input_redirection.h"

#include <math.h>

atomic_bool program_running;
#ifdef _WIN32
static OSVERSIONINFO osvi;
static int64_t qpc_freq = 1;
#endif

void itimeofday(int64_t *sec, int64_t *usec)
{
#ifdef _WIN32
    int64_t qpc;
    if (!QueryPerformanceCounter((LARGE_INTEGER *)&qpc))
    {
        program_running = 0;
        *sec = *usec = 0;
        return;
    }
    if (sec)
        *sec = (int64_t)(qpc / qpc_freq);
    if (usec)
        *usec = (int64_t)((qpc % qpc_freq) * 1000000 / qpc_freq);
#else
    struct timespec ts;
#ifdef PTHREAD_SEM_NON_MONOTONIC_CLOCK
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
#else
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
#endif
    {
        program_running = 0;
        *sec = *usec = 0;
        return;
    }
    if (sec)
        *sec = ts.tv_sec;
    if (usec)
        *usec = ts.tv_nsec / 1000;
#endif
}

static void main_init() {
#ifdef _WIN32
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    GetVersionEx(&osvi);
    err_log("Windows version %d.%d.%d\n", (int)osvi.dwMajorVersion, (int)osvi.dwMinorVersion, (int)osvi.dwBuildNumber);

    if (!QueryPerformanceFrequency((LARGE_INTEGER *)&qpc_freq)) {
    }
    qpc_freq = (qpc_freq == 0) ? 1 : qpc_freq;
#endif

    RO_INIT();
    rp_syn_startup();
}

static void main_destroy() {
    RO_UNINIT();
}

static enum ui_renderer_t renderer_list[UI_RENDERER_COUNT];
static const char *renderer_name_list[UI_RENDERER_COUNT];
static int renderer_count;
static event_t renderer_begin_evt;
static event_t renderer_end_evt;
static rp_lock_t nk_input_lock;
static view_mode_t ui_view_mode_prev;
static bool ui_fullscreen_prev;
static float ui_font_scale;
bool nk_gui_next;
bool nk_input_current;
bool renderer_single_thread;
bool renderer_evt_sync;

#include <getopt.h>

int opt_flag_d3d, opt_flag_ogl, opt_flag_gles, opt_flag_angle, opt_flag_metal, opt_flag_vulkan;
int opt_flag_no_csc, opt_flag_sdl_hw, opt_flag_sdl_sw;

#define opt_name_d3d "d3d"
#define opt_name_ogl "ogl"
#define opt_name_gles "gles"
#define opt_name_angle "angle"
#define opt_name_metal "metal"
#define opt_name_vulkan "vulkan"
#define opt_name_sdl_hw "sdl-hw"
#define opt_name_sdl_sw "sdl-sw"
#define opt_name_no_csc "no-csc"
#define opt_name_ogl_dbg "ogl-dbg"
#define opt_name_testing_no_ext_mem "testing-no-ext-mem"
#define opt_name_testing_no_shared_sem "testing-no-shared-sem"
#define opt_name_testing_no_fp16 "testing-no-fp16"

#define mod_name_csc "csc"

static struct option long_options[] = {
#ifdef _WIN32
    {opt_name_no_csc, no_argument, &opt_flag_no_csc, 1},
    {opt_name_d3d, no_argument, &opt_flag_d3d, 1},
#endif
#ifdef __APPLE__
    {opt_name_metal, no_argument, &opt_flag_metal, 1},
#else
    {opt_name_vulkan, no_argument, &opt_flag_vulkan, 1},
    {opt_name_ogl, no_argument, &opt_flag_ogl, 1},
    {opt_name_gles, no_argument, &opt_flag_gles, 1},
    {opt_name_angle, no_argument, &opt_flag_angle, 1},
    {opt_name_ogl_dbg, no_argument, &is_renderer_ogl_dbg, 1},
#endif
    {opt_name_sdl_hw, no_argument, &opt_flag_sdl_hw, 1},
    {opt_name_sdl_sw, no_argument, &opt_flag_sdl_sw, 1},
    {0, 0, 0, 0}};

static void add_arg(enum ui_renderer_t arg, const char *name) {
    for (int i = 0; i < renderer_count; ++i) {
        if (renderer_list[i] == arg) {
            return;
        }
    }

    if (renderer_count < UI_RENDERER_COUNT) {
        renderer_list[renderer_count] = arg;
        renderer_name_list[renderer_count] = name;
        ++renderer_count;
    }
}

static void remove_csc_args(void) {
    for (int i = 0; i < renderer_count;) {
        ui_renderer = renderer_list[i];
        if (is_renderer_csc()) {
            for (int j = i + 1; j < renderer_count; ++j) {
                renderer_list[j - 1] = renderer_list[j];
                renderer_name_list[j - 1] = renderer_name_list[j];
            }
            --renderer_count;
        } else {
            ++i;
        }
    }
}

static void remove_ogl_args(void) {
    for (int i = 0; i < renderer_count;) {
        ui_renderer = renderer_list[i];
        if (is_renderer_sdl_ogl()) {
            for (int j = i + 1; j < renderer_count; ++j) {
                renderer_list[j - 1] = renderer_list[j];
                renderer_name_list[j - 1] = renderer_name_list[j];
            }
            --renderer_count;
        } else {
            ++i;
        }
    }
}

static bool check_osvi_csc(void) {
#ifdef _WIN32
    return osvi.dwMajorVersion >= 10 && osvi.dwBuildNumber >= 22000;
#else
    return 0;
#endif
}

static void parse_args(int argc, char **argv)
{
    opt_flag_no_csc = !check_osvi_csc();

    while (1) {
        int option_index = 0;
        int c = getopt_long(argc, argv, "+", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {
            default:
                break;

            case 0: {
                if (long_options[option_index].flag) {
                    const char *const name = long_options[option_index].name;
                    if (strcmp(name, opt_name_d3d) == 0) {
                        if (!opt_flag_no_csc) {
                            add_arg(UI_RENDERER_D3D11_CSC, opt_name_d3d " " mod_name_csc);
                        }
                        add_arg(UI_RENDERER_D3D11, opt_name_d3d);
                    } else if (strcmp(name, opt_name_ogl) == 0) {
                        if (!opt_flag_angle) {
                            remove_ogl_args();
                            if (!opt_flag_no_csc) {
                                add_arg(UI_RENDERER_OGL_CSC, opt_name_ogl " " mod_name_csc);
                            }
                            add_arg(UI_RENDERER_OGL, opt_name_ogl);
                        }
                    } else if (strcmp(name, opt_name_gles) == 0) {
                        if (!opt_flag_angle) {
                            remove_ogl_args();
                            if (!opt_flag_no_csc) {
                                add_arg(UI_RENDERER_GLES_CSC, opt_name_gles " " mod_name_csc);
                            }
                            add_arg(UI_RENDERER_GLES, opt_name_gles);
                        }
                    } else if (strcmp(name, opt_name_angle) == 0) {
                        remove_ogl_args();
                        add_arg(UI_RENDERER_GLES_ANGLE, opt_name_angle);
                    } else if (strcmp(name, opt_name_metal) == 0) {
                        add_arg(UI_RENDERER_METAL, opt_name_metal);
                    } else if (strcmp(name, opt_name_vulkan) == 0) {
                        add_arg(UI_RENDERER_VULKAN, opt_name_vulkan);
                    } else if (strcmp(name, opt_name_sdl_hw) == 0) {
                        add_arg(UI_RENDERER_SDL_HW, opt_name_sdl_hw);
                    } else if (strcmp(name, opt_name_sdl_sw) == 0) {
                        add_arg(UI_RENDERER_SDL_SW, opt_name_sdl_sw);
                    } else if (strcmp(name, opt_name_no_csc) == 0) {
                        remove_csc_args();
                    }
                }
                break;
            }
        }
    }

    if (is_renderer_ogl_dbg) {
        printf("using %s\n", opt_name_ogl_dbg);
    }

    if (renderer_count) {
        printf("using drivers:\n");
        for (int i = 0; i < renderer_count; ++i) {
            printf("%s\n", renderer_name_list[i]);
        }
        return;
    }

    int j = 0;
    for (int i = 0; i < UI_RENDERER_COUNT; ++i) {
        ui_renderer = i;

        if (is_renderer_csc()) {
            if (opt_flag_no_csc || !check_osvi_csc()) {
                continue;
            }
        }
        // Do not attempt ANGLE as a default option
        if (is_renderer_gles_angle())
            continue;

        renderer_list[j] = i;
        ++j;
    }
    renderer_count = j;
}

#ifdef _WIN32
static LRESULT CALLBACK main_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    int i = GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    bool need_handle_input = 0;
    UNUSED LPARAM handled_lparam = lparam;
    bool resize_top_and_ui = i == SCREEN_TOP;

    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SIZE: {
            if (resize_top_and_ui) {
#ifndef USE_SDL_RENDERER_ONLY
                rp_lock_wait(comp_lock);
#endif
            }
            ui_win_width_drawable[i] = NK_MAX(LOWORD(lparam), 1);
            ui_win_height_drawable[i] = NK_MAX(HIWORD(lparam), 1);
            ui_win_width[i] = roundf(ui_win_width_drawable[i] / ui_win_scale[i]);
            ui_win_height[i] = roundf(ui_win_height_drawable[i] / ui_win_scale[i]);
            if (resize_top_and_ui) {
#ifndef USE_SDL_RENDERER_ONLY
                if (is_renderer_d3d11()) {
                    d3d11_ui_close();
                    if (d3d11_ui_init()) {
                        d3d11_ui_close();
                        ui_compositing = 0;
                    }
                }
#endif
                ui_nk_width = ui_win_width[i];
                ui_nk_height = ui_win_height[i];
            }
            if (resize_top_and_ui) {
#ifndef USE_SDL_RENDERER_ONLY
                rp_lock_rel(comp_lock);
#endif
            }
            break;
        }

        case WM_DPICHANGED: {
            ui_win_scale[i] = (float)HIWORD(wparam) / USER_DEFAULT_SCREEN_DPI;
            if (resize_top_and_ui) {
                ui_nk_scale = ui_win_scale[i];
            }
            SDL_SetWindowSize(ui_sdl_win[i], ui_win_scale[i] * ui_win_width[i], ui_win_scale[i] * ui_win_height[i]);
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDBLCLK: {
            int x = (short)LOWORD(lparam);
            int y = (short)HIWORD(lparam);
            x = x / ui_win_scale[i];
            y = y / ui_win_scale[i];
            handled_lparam = MAKELPARAM(x, y);

            SDL_Event event = {};
            switch (msg) {
                case WM_MOUSEMOVE:
                    event.type = SDL_EVENT_MOUSE_MOTION;
                    event.motion.windowID = ui_sdl_win_id[i];
                    event.motion.x = x;
                    event.motion.y = y;
                    break;

                case WM_LBUTTONDOWN:
                    event.button.button = SDL_BUTTON_LEFT;
                    goto mouse_down;
                case WM_RBUTTONDOWN:
                    event.button.button = SDL_BUTTON_RIGHT;
                    goto mouse_down;
                case WM_MBUTTONDOWN:
                    event.button.button = SDL_BUTTON_MIDDLE;
mouse_down:
                    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    goto mouse_button;

                case WM_LBUTTONUP:
                    event.button.button = SDL_BUTTON_LEFT;
                    goto mouse_up;
                case WM_RBUTTONUP:
                    event.button.button = SDL_BUTTON_RIGHT;
                    goto mouse_up;
                case WM_MBUTTONUP:
                    event.button.button = SDL_BUTTON_MIDDLE;
mouse_up:
                    event.type = SDL_EVENT_MOUSE_BUTTON_UP;

mouse_button:
                    event.button.windowID = ui_sdl_win_id[i];
                    event.button.x = x;
                    event.button.y = y;
                    break;

                default:
                    break;
            }
            need_handle_input = !(is_renderer_d3d11() && sdl_process_bottom_screen_event(&event)) && i == SCREEN_TOP;
            break;
        }

        case WM_MOUSEWHEEL:
            need_handle_input = i == SCREEN_TOP;
            break;

        case WM_CHAR:
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
            need_handle_input = 1;
            break;
    }

    if (need_handle_input) {
        if (is_renderer_d3d11()) {
            if (!renderer_single_thread)
                rp_lock_wait(nk_input_lock);

            if (!nk_input_current) {
                nk_input_begin(ui_nk_ctx);
                nk_input_current = 1;
            }

#ifndef USE_SDL_RENDERER_ONLY
            nk_d3d11_handle_event(hwnd, msg, wparam, handled_lparam);
#endif

            if (!renderer_single_thread)
                rp_lock_rel(nk_input_lock);
        }
    }

    return CallWindowProcA((WNDPROC)ui_sdl_wnd_proc[i], hwnd, msg, wparam, lparam);
}
#endif

struct nk_color nk_window_bgcolor = { 28, 48, 62, 255 };

#ifndef _WIN32
static bool sdl_win_resize_evt_watcher(void *, SDL_Event *event) {
    if (
        event->type == SDL_EVENT_WINDOW_RESIZED ||
        event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event->type == SDL_EVENT_WINDOW_METAL_VIEW_RESIZED
    ) {
        int i;
        for (i = 0; i < SCREEN_COUNT; ++i) {
            if (event->window.windowID == ui_sdl_win_id[i]) {
                break;
            }
        }
        if (i < SCREEN_COUNT) {
            ui_window_size_update(i);
        }
    }
    return 0;
}
#endif

#define MIN_UPDATE_INTERVAL_US (33333)
static bool decode_cond_wait(event_t *event)
{
    int res = event_wait(event, MIN_UPDATE_INTERVAL_US * 1000);
    if (res == ETIMEDOUT) {
    } else if (res) {
        err_log("decode_cond_wait failed: %d\n", res);
        return false;
    }
    return true;
}

static void thread_loop(int i) {
    // TODO csc
    int ctx_top_bot = i;
    int screen_top_bot = i;
    bool win_shared = 0;

    int screen_count= SCREEN_COUNT;
    view_mode_t view_mode = __atomic_load_n(&ui_view_mode, __ATOMIC_RELAXED);
    if (view_mode != VIEW_MODE_SEPARATE) {
        screen_count = 1;
        if (is_renderer_csc()) {
            win_shared = view_mode == VIEW_MODE_TOP_BOT;
            if (win_shared) {
                screen_count = SCREEN_COUNT;
                ctx_top_bot = SCREEN_TOP;
            }
        }
    }

    if (i == SCREEN_TOP) {
        generate_cursors_images(view_mode);
    }

    if (i >= screen_count) {
        if (!renderer_single_thread)
            event_wait(&update_bottom_screen_evt, NWM_THREAD_WAIT_NS);
        return;
    }

    if (renderer_single_thread) {
        if (i == SCREEN_TOP && !decode_cond_wait(&decode_updated_event))
            return;
    } else {
        int buf_top_bot = view_mode == VIEW_MODE_BOT ? SCREEN_BOT : screen_top_bot;
        if (!decode_cond_wait(&rp_buffer_ctx[buf_top_bot].decode_updated_event))
            return;
    }

    float bg[GL_CHANNELS_N];
    nk_color_fv(bg, nk_window_bgcolor);

    if (!renderer_single_thread && renderer_evt_sync)
        if (!cond_mutex_flag_lock(&renderer_begin_evt))
            return;

    if (is_renderer_d3d11()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_d3d11_main(screen_top_bot, ctx_top_bot, view_mode, win_shared, bg);
#endif
    } else if (is_renderer_sdl_ogl()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_ogl_main(screen_top_bot, ctx_top_bot, view_mode, win_shared, bg);
#endif
    } else if (is_renderer_vulkan()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_vk_main(ctx_top_bot, view_mode, bg);
#endif
    } else if (is_renderer_sdl_renderer()) {
        ui_renderer_sdl_main(ctx_top_bot, view_mode, bg);
    }

    if (i == SCREEN_TOP) {
        if (
            fabsf(ui_font_scale - ui_win_scale[ctx_top_bot]) >= ui_font_scale_epsilon)
        {
            ui_font_scale = ui_win_scale[ctx_top_bot];

            /* Load Fonts: if none of these are loaded a default font will be used  */
            /* Load Cursor: if you uncomment cursor loading please hide the cursor */
            struct nk_font_atlas *atlas;
            struct nk_font_config config = nk_font_config(0);
            struct nk_font *font;

            /* set up the font atlas and add desired font; note that font sizes are
             * multiplied by font_scale to produce better results at higher DPIs */
            nk_font_stash_begin(&atlas);
            font = nk_font_atlas_add_default(atlas, 13 * ui_font_scale, &config);
            nk_font_stash_end();

            /* this hack makes the font appear to be scaled down to the desired
             * size and is only necessary when font_scale > 1 */

            if (font) {
                font->handle.height = font->handle.height / ui_font_scale;
                // nk_style_load_all_cursors(ui_nk_ctx, atlas->cursors);
                nk_style_set_font(ui_nk_ctx, &font->handle);
            }
        }

        if (!nk_gui_next) {
            if (nk_input_current) {
                nk_input_end(ui_nk_ctx);
                nk_input_current = 0;
            } else {
                nk_input_begin(ui_nk_ctx);
                nk_input_end(ui_nk_ctx);
            }

            ui_main_nk();
            nk_gui_next = 1;
        }
    }

    if (is_renderer_d3d11()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_d3d11_present(screen_top_bot, ctx_top_bot, win_shared);
#endif
    } else if (is_renderer_sdl_ogl()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_ogl_present(screen_top_bot, ctx_top_bot, win_shared);
#endif
    } else if (is_renderer_vulkan()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_vk_present(ctx_top_bot);
#endif
    } else if (is_renderer_sdl_renderer()) {
        ui_renderer_sdl_present(ctx_top_bot);
    }

    if (!renderer_single_thread && renderer_evt_sync)
        cond_mutex_flag_signal(&renderer_end_evt);
}

static thread_ret_t window_thread_func(void *arg) {
    RO_INIT();

    int i = (int)(uintptr_t)arg;
#ifndef USE_SDL_RENDERER_ONLY
#ifndef __APPLE__
    if (is_renderer_sdl_ogl())
        SDL_GL_MakeCurrent(ogl_win[i], gl_context[i]);
#endif
#endif
    while (program_running)
        thread_loop(i);
    // TODO csc

    RO_UNINIT();

    return (thread_ret_t)(uintptr_t)NULL;
}

static void main_loop(void) {
    // TODO csc

    SDL_Event evt;
    while (!renderer_single_thread && !renderer_evt_sync ? SDL_WaitEventTimeout(&evt, REST_EVERY_MS) : SDL_PollEvent(&evt))
    {
        if (
            evt.type == SDL_EVENT_QUIT ||
            (evt.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        ) {
            program_running = 0;
            return;
        } else if (
            evt.type == SDL_EVENT_KEY_DOWN &&
            evt.key.key == SDLK_F
        ) {
            ui_fullscreen = !ui_fullscreen;
        } else if (
            evt.type == SDL_EVENT_KEY_DOWN &&
            evt.key.key == SDLK_R
        ) {
#ifdef TDR_TEST_HOTKEY
#error TODO
#endif
        } else if (
            evt.type == SDL_EVENT_KEY_DOWN &&
            evt.key.key == SDLK_T
        ) {
#ifdef TDR_TEST_HOTKEY
#error TODO
#endif
        } else {
            switch (evt.type) {
                case SDL_EVENT_MOUSE_MOTION:
                    if (
                        (!is_renderer_d3d11() && sdl_process_bottom_screen_event(&evt)) ||
                        evt.motion.windowID != ui_sdl_win_id[SCREEN_TOP]
                    ) {
                        goto skip_evt;
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (
                        (!is_renderer_d3d11() && sdl_process_bottom_screen_event(&evt)) ||
                        evt.button.windowID != ui_sdl_win_id[SCREEN_TOP]
                    ) {
                        goto skip_evt;
                    }
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    if (evt.wheel.windowID != ui_sdl_win_id[SCREEN_TOP]) {
                        goto skip_evt;
                    }
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (evt.key.windowID != ui_sdl_win_id[SCREEN_TOP]) {
                        goto skip_evt;
                    }
                    switch (evt.key.key) {
                        case SDLK_TAB: {
                            int shift_down = SDL_GetModState() & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT);
                            __atomic_store_n(&nk_nav_cmd, shift_down ? NK_NAV_PREVIOUS : NK_NAV_NEXT, __ATOMIC_RELAXED);
                            goto skip_evt;
                        }

                        case SDLK_SPACE:
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                            __atomic_store_n(&nk_nav_cmd, NK_NAV_CONFIRM, __ATOMIC_RELAXED);
                            goto skip_evt;

                        case SDLK_ESCAPE:
                            __atomic_store_n(&nk_nav_cmd, NK_NAV_CANCEL, __ATOMIC_RELAXED);
                            goto skip_evt;
                    }
                    break;
            }

            if (!is_renderer_d3d11()) {
                if (!renderer_single_thread) {
                    rp_lock_wait(nk_input_lock);
                }

                if (!nk_input_current) {
                    nk_input_begin(ui_nk_ctx);
                    nk_input_current = 1;
                }

                if (is_renderer_ogl()) {
#ifndef USE_SDL_RENDERER_ONLY
#ifndef __APPLE__
                    nk_sdl_gl3_handle_event(&evt);
#endif
#endif
                } else if (is_renderer_gles()) {
#ifndef USE_SDL_RENDERER_ONLY
#ifndef __APPLE__
                    nk_sdl_gles2_handle_event(&evt);
#endif
#endif
                } else if (is_renderer_vulkan()) {
#ifndef USE_SDL_RENDERER_ONLY
                    nk_sdl_vk_handle_event(&evt);
#endif
                } else if (is_renderer_sdl_renderer()) {
                    nk_sdl_renderer_handle_event(&evt);
                }

                if (!renderer_single_thread) {
                    rp_lock_rel(nk_input_lock);
                }
            }
skip_evt:
        }
    }

    update_game_controller();
    update_bottom_screen_cursor();

    view_mode_t view_mode = __atomic_load_n(&ui_view_mode, __ATOMIC_RELAXED);
    if (ui_view_mode_prev != view_mode) {
        ui_view_mode_update(view_mode);
        ui_view_mode_prev = view_mode;
        SDL_RaiseWindow(ui_sdl_win[SCREEN_TOP]);
    }

    if (ui_fullscreen_prev != ui_fullscreen) {
        if (ui_fullscreen) {
            if (SDL_GetDisplayForWindow(ui_sdl_win[SCREEN_TOP]) != SDL_GetDisplayForWindow(ui_sdl_win[SCREEN_BOT])) {
                SDL_SetWindowFullscreen(ui_sdl_win[SCREEN_TOP], true);
                SDL_SetWindowFullscreen(ui_sdl_win[SCREEN_BOT], true);
            } else if (SDL_GetWindowFlags(ui_sdl_win[SCREEN_BOT]) & SDL_WINDOW_INPUT_FOCUS) {
                SDL_SetWindowFullscreen(ui_sdl_win[SCREEN_BOT], true);
                SDL_RaiseWindow(ui_sdl_win[SCREEN_BOT]);
            } else {
                SDL_SetWindowFullscreen(ui_sdl_win[SCREEN_TOP], true);
                SDL_RaiseWindow(ui_sdl_win[SCREEN_TOP]);
            }
        } else {
            bool top_focus = SDL_GetWindowFlags(ui_sdl_win[SCREEN_TOP]) & SDL_WINDOW_INPUT_FOCUS;
            ui_view_mode_update(view_mode);
            if (top_focus)
                SDL_RaiseWindow(ui_sdl_win[SCREEN_TOP]);
        }
        ui_fullscreen_prev = ui_fullscreen;
    }

    ui_windows_titles_update();

    if (renderer_single_thread) {
        for (int i = 0; i < SCREEN_COUNT; ++i)
            thread_loop(i);
    } else if (renderer_evt_sync) {
        cond_mutex_flag_signal(&renderer_begin_evt);
        if (!cond_mutex_flag_lock(&renderer_end_evt))
            return;
    }
}

static void main_ntr(void) {
#ifdef _WIN32
    socket_startup();
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED | ES_DISPLAY_REQUIRED);
#endif

    rp_buffer_init();

    ntr_config_set_default();
    ntr_detect_3ds_ip();
    ntr_get_adapter_list();
    ntr_try_auto_select_adapter();
    ui_update_game_controllers();

    program_running = true;
    int ret;

    thread_t udp_recv_thread;
    if ((ret = thread_create(udp_recv_thread, udp_recv_thread_func, NULL)))
    {
        err_log("udp_recv_thread create failed\n");
        program_running = false;
        goto join_udp_recv;
    }
    thread_t menu_tcp_thread;
    struct tcp_thread_arg menu_tcp_thread_arg = {
        &menu_work_state,
        &menu_remote_play,
        8000,
    };
    if ((ret = thread_create(menu_tcp_thread, tcp_thread_func, &menu_tcp_thread_arg)))
    {
        err_log("menu_tcp_thread create failed\n");
        program_running = false;
        goto join_menu_tcp;
    }
    thread_t nwm_tcp_thread;
    struct tcp_thread_arg nwm_tcp_thread_arg = {
        &nwm_work_state,
        NULL,
        5000 + 0x1a,
    };
    if ((ret = thread_create(nwm_tcp_thread, tcp_thread_func, &nwm_tcp_thread_arg)))
    {
        err_log("nwm_tcp_thread create failed\n");
        program_running = false;
        goto join_nwm_tcp;
    }

    rp_lock_init(sdl_cursors_lock);
    rp_lock_init(sdl_game_controller_lock);
    rp_lock_init(ui_nk_lock);
    thread_t window_top_thread = 0;
    thread_t window_bot_thread = 0;

    if (!renderer_single_thread) {
        if ((ret = thread_create(window_top_thread, window_thread_func, (void *)SCREEN_TOP)))
        {
            err_log("window_top_thread create failed\n");
            program_running = false;
            goto join_win_top;
        }
        if ((ret = thread_create(window_bot_thread, window_thread_func, (void *)SCREEN_BOT)))
        {
            err_log("window_bot_thread create failed\n");
            program_running = false;
            goto join_win_bot;
        }
    }

    SDL_TimerID timer_id;
    if (!(timer_id = SDL_AddTimer(1000 / 60, input_redirection_timer_cb, 0))) {
        err_log("input redirection timer add failed: %s\n", SDL_GetError());
        program_running = false;
        goto join_timer;
    }

    while (program_running)
        main_loop();

join_timer:
    SDL_RemoveTimer(timer_id);
    input_redirection_frame = input_redirection_frame_default;
    input_redirection_send_frame(&input_redirection_frame);

    if (!renderer_single_thread) {
        thread_join(window_bot_thread);
join_win_bot:
        thread_join(window_top_thread);
join_win_top:
    }
    rp_lock_close(ui_nk_lock);
    rp_lock_close(sdl_game_controller_lock);
    rp_lock_close(sdl_cursors_lock);

#ifndef _WIN32
    thread_cancel(nwm_tcp_thread);
    thread_cancel(menu_tcp_thread);
    thread_cancel(udp_recv_thread);
#endif
    thread_join(nwm_tcp_thread);
join_nwm_tcp:
    thread_join(menu_tcp_thread);
join_menu_tcp:
    thread_join(udp_recv_thread);
join_udp_recv:

    rp_buffer_destroy();

#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
    socket_shutdown();
#endif
}

static void main_windows(void) {
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        ui_win_scale[i] = 1.0f;
    }
    event_init(&renderer_begin_evt);
    event_init(&renderer_end_evt);
    rp_lock_init(nk_input_lock);
    nk_gui_next = 0;
    nk_input_current = 0;

#ifdef _WIN32
    HBRUSH bg_brush = CreateSolidBrush(
        RGB(nk_window_bgcolor.r, nk_window_bgcolor.g, nk_window_bgcolor.b));
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        SetWindowLongPtrA(ui_hwnd[i], GWLP_USERDATA, i);
        ui_sdl_wnd_proc[i] = GetWindowLongPtrA(ui_hwnd[i], GWLP_WNDPROC);
        SetWindowLongPtrA(ui_hwnd[i], GWLP_WNDPROC, (LONG_PTR)main_window_proc);
        SetClassLongPtr(ui_hwnd[i], GCLP_HBRBACKGROUND, (LONG_PTR)bg_brush);
    }

#ifndef USE_SDL_RENDERER_ONLY
    rp_lock_init(comp_lock);
#endif
#else
    // TODO
    SDL_AddEventWatch(sdl_win_resize_evt_watcher, NULL);
#endif

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        ui_sdl_win_id[i] = SDL_GetWindowID(ui_sdl_win[i]);
        SDL_SetWindowFullscreenMode(ui_sdl_win[i], NULL);
    }

    event_init(&update_bottom_screen_evt);

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        ui_window_size_update(i);
    }
    ui_view_mode_update(ui_view_mode);
    SDL_ShowWindow(ui_sdl_win[SCREEN_TOP]);

    nk_backend_font_init();

#ifdef _WIN32
#ifndef USE_SDL_RENDERER_ONLY
    if (is_renderer_csc() && is_renderer_d3d11() && ui_compositing) {
        if (d3d11_ui_init()) {
            d3d11_ui_close();
            ui_compositing = 0;
        }
    }
#endif
#endif
    main_ntr();
#ifdef _WIN32
#ifndef USE_SDL_RENDERER_ONLY
    d3d11_ui_close();
#endif
#endif

    event_close(&update_bottom_screen_evt);

#ifdef _WIN32
#ifndef USE_SDL_RENDERER_ONLY
    rp_lock_close(comp_lock);
#endif

    DeleteObject(bg_brush);
#endif

    rp_lock_close(nk_input_lock);
    event_close(&renderer_end_evt);
    event_close(&renderer_begin_evt);
}

int main(int argc, char **argv) {
    main_init();

    parse_args(argc, argv);
    if (ui_common_sdl_init()) {
        return -1;
    }

    bool renderer_inited = 0;
    for (int i = 0; i < renderer_count; ++i) {
        ui_renderer = renderer_list[i];
        renderer_single_thread = 0;
        renderer_evt_sync = 0;

        if (is_renderer_d3d11()) {
#ifndef USE_SDL_RENDERER_ONLY
            if (ui_renderer_d3d11_init())
                ui_renderer_d3d11_destroy();
            else
                renderer_inited = 1;
#endif
        } else if (is_renderer_sdl_ogl()) {
#ifndef USE_SDL_RENDERER_ONLY
            if (ui_renderer_ogl_init())
                ui_renderer_ogl_destroy();
            else
                renderer_inited = 1;
#endif
        } else if (is_renderer_vulkan()) {
#ifndef USE_SDL_RENDERER_ONLY
            if (ui_renderer_vk_init())
                ui_renderer_vk_destroy();
            else
                renderer_inited = 1;
#endif
        } else if (is_renderer_sdl_renderer()) {
            if (ui_renderer_sdl_init())
                ui_renderer_sdl_destroy();
            else
                renderer_inited = 1;
        }
        if (renderer_inited)
            break;
    }
    if (!renderer_inited) {
        err_log("failed to initializer driver(s)\n");
        return -1;
    }

    main_windows();

    if (is_renderer_d3d11()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_d3d11_destroy();
#endif
    } else if (is_renderer_sdl_ogl()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_ogl_destroy();
#endif
    } else if (is_renderer_vulkan()) {
#ifndef USE_SDL_RENDERER_ONLY
        ui_renderer_vk_destroy();
#endif
        } else if (is_renderer_sdl_renderer()) {
        ui_renderer_sdl_destroy();
    }

    ui_common_sdl_destroy();

    main_destroy();

    return 0;
}
