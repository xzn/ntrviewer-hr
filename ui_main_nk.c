#include "ui_main_nk.h"
#include "ui_common_sdl.h"
#include "ntr_common.h"
#include "ntr_hb.h"
#include "ntr_rp.h"
#include "ui_input_redirection.h"

enum nk_nav_t nk_nav_cmd;
rp_lock_t ui_nk_lock;

#include "style.h"
#include "web_colors.h"

#define UI_MSG_BUF_LEN_MAX (256)

static struct nk_style nk_style_current;

#include "nuklear_sdl_renderer.h"
#ifndef USE_SDL_RENDERER_ONLY
#include "nuklear_sdl_vulkan.h"
#ifndef __APPLE__
#include "nuklear_sdl_gl3.h"
#include "nuklear_sdl_gles2.h"
#endif
#ifdef _WIN32
#include "nuklear_d3d11.h"
#include "ui_compositor_csc.h"
#endif
#endif

#include <limits.h>

void nk_font_stash_begin(struct nk_font_atlas **atlas) {
    if (is_renderer_d3d11()) {
#ifndef USE_SDL_RENDERER_ONLY
#ifdef _WIN32
        nk_d3d11_font_stash_begin(atlas);
#endif
#endif
    } else if (is_renderer_ogl()) {
#ifndef USE_SDL_RENDERER_ONLY
#ifndef __APPLE__
        nk_sdl_gl3_font_stash_begin(atlas);
#endif
#endif
    } else if (is_renderer_gles()) {
#ifndef USE_SDL_RENDERER_ONLY
#ifndef __APPLE__
        nk_sdl_gles2_font_stash_begin(atlas);
#endif
#endif
    } else if (is_renderer_vulkan()) {
#ifndef USE_SDL_RENDERER_ONLY
        nk_sdl_vk_font_stash_begin(atlas);
#endif
    } else if (is_renderer_sdl_renderer()) {
        nk_sdl_renderer_font_stash_begin(atlas);
    }
}

void nk_font_stash_end(void) {
    if (is_renderer_d3d11()) {
#ifndef USE_SDL_RENDERER_ONLY
#ifdef _WIN32
        nk_d3d11_font_stash_end();
#endif
#endif
    } else if (is_renderer_ogl()) {
#ifndef USE_SDL_RENDERER_ONLY
#ifndef __APPLE__
        nk_sdl_gl3_font_stash_end();
#endif
#endif
    } else if (is_renderer_gles()) {
#ifndef USE_SDL_RENDERER_ONLY
#ifndef __APPLE__
        nk_sdl_gles2_font_stash_end();
#endif
#endif
    } else if (is_renderer_vulkan()) {
#ifndef USE_SDL_RENDERER_ONLY
        nk_sdl_vk_font_stash_end();
#endif
    } else if (is_renderer_sdl_renderer()) {
        nk_sdl_renderer_font_stash_end();
    }
}

struct nk_color nk_window_bgcolor;

void nk_backend_font_init(void)
{
    /* Load Fonts: if none of these are loaded a default font will be used  */
    /* Load Cursor: if you uncomment cursor loading please hide the cursor */
    {
        struct nk_font_atlas *atlas;

        nk_font_stash_begin(&atlas);
        nk_font_stash_end();

        // nk_style_load_all_cursors(ui_nk_ctx, atlas->cursors);
        // nk_style_set_font(ui_nk_ctx, &roboto->handle);
    }

    nk_default_color_style[NK_COLOR_WINDOW].a =
        nk_default_color_style[NK_COLOR_HEADER].a =
        nk_default_color_style[NK_COLOR_EDIT].a =
        255 * 7 / 8;
    set_style(ui_nk_ctx, THEME_BLACK);
    // set_style(ui_nk_ctx, THEME_WHITE);
    // set_style(ui_nk_ctx, THEME_RED);
    // set_style(ui_nk_ctx, THEME_BLUE);
    // set_style(ui_nk_ctx, THEME_DARK);

    ui_nk_ctx->style.checkbox.cursor_normal.data.color =
        ui_nk_ctx->style.checkbox.cursor_hover.data.color =
        ui_nk_ctx->style.text.color;

    web_colors_init(ui_nk_ctx);
    web_colors_add(ui_nk_ctx);

    nk_style_current = ui_nk_ctx->style;
}

atomic_bool ui_hide_nk_windows;

int ui_upscaling_selected;
const char **ui_upscaling_filter_options;
int ui_upscaling_filter_count;

static const char *nk_property_name = "#";
static enum NK_FOCUS {
    NK_FOCUS_VIEW_MODE,
    NK_FOCUS_UPSCALING_FILTER,
    NK_FOCUS_IP_OCTET_0,
    NK_FOCUS_IP_OCTET_1,
    NK_FOCUS_IP_OCTET_2,
    NK_FOCUS_IP_OCTET_3,
    NK_FOCUS_IP_AUTO_DETECT,
    NK_FOCUS_IP_COMBO,
    NK_FOCUS_VIEWER_IP,
    NK_FOCUS_VIEWER_PORT,
    NK_FOCUS_PRIORITY_SCREEN,
    NK_FOCUS_PRIORITY_FACTOR,
    NK_FOCUS_FMT_PROT,
    NK_FOCUS_QUALITY,
    NK_FOCUS_AUTO_QUALITY,
    NK_FOCUS_BANDWIDTH_LIMIT,
    NK_FOCUS_DEFAULT,
    NK_FOCUS_CONNECT,
    NK_FOCUS_INPUT_REDIRECTION,
    NK_FOCUS_INPUT_SWAP_FACE,
    NK_FOCUS_INPUT_CURSOR_SIZE,
    NK_FOCUS_BORDER_ITER,
    NK_FOCUS_BORDER_RADIUS,
    NK_FOCUS_COUNT,
    NK_FOCUS_MIN = 0,
    NK_FOCUS_MAX = NK_FOCUS_COUNT - 1,
} nk_focus_current;

static enum NK_NAV_FOCUS {
    NK_NAV_FOCUS_NONE,
    NK_NAV_FOCUS_NORMAL,
    NK_NAV_FOCUS_NAV,
} nk_nav_focus;

static const char *const remote_play_wnd = "Remote Play";
static const char *const debug_msg_wnd = "Debug";
static const char *const background_wnd = "Background";

static const char *connection_msg[] = {
    "+",
    "-",
};
_Static_assert(sizeof(connection_msg) / sizeof(*connection_msg) == CONNECTION_STATE_COUNT);

static const char *connection_req_msg[] = {
    ".",
    "...",
    "...",
};
_Static_assert(sizeof(connection_req_msg) / sizeof(*connection_req_msg) == CONNECTION_REQ_STATE_COUNT);

static enum connection_state_t menu_connection, nwm_connection, nwm_o3ds_connection;
static enum connection_req_state_t menu_connection_req, nwm_connection_req, nwm_o3ds_connection_req;

// HACK
// Try to get Nuklear to accept keyboard navigation

static nk_hash nk_hash_from_name_prev(const char *name, struct nk_window *win, int prev)
{
    // copied from nuklear_property.c
    if (name[0] == '#')
    {
        return nk_murmur_hash(name, (int)nk_strlen(name), win->property.seq - prev);
    }
    else
        return nk_murmur_hash(name, (int)nk_strlen(name), 42);
}

static nk_hash nk_hash_from_name(const char *name, struct nk_window *win)
{
    return nk_hash_from_name_prev(name, win, 0);
}

NK_LIB char *nk_itoa(char *s, long n);
static void focus_next_property(struct nk_context *ctx, const char *name, int val)
{
    struct nk_window *win = ctx->current;
    nk_hash hash = nk_hash_from_name(name, win);

    win->property.active = 1;
    nk_itoa(win->property.buffer, val);
    win->property.length = nk_strlen(win->property.buffer);
    win->property.cursor = 0;
    win->property.state = NK_PROPERTY_EDIT;
    win->property.name = hash;
    win->property.select_start = 0;
    win->property.select_end = win->property.length;

    ui_sdl_text_input_needed = 1;
}

static void cancel_next_property(struct nk_context *ctx)
{
    struct nk_window *win = ctx->current;

    if (win->property.active && win->property.state == NK_PROPERTY_EDIT)
    {
        win->property.active = 0;
        win->property.buffer[0] = 0;
        win->property.length = 0;
        win->property.cursor = 0;
        win->property.state = 0;
        win->property.name = 0;
        win->property.select_start = 0;
        win->property.select_end = 0;
    }

    ui_sdl_text_input_needed = 0;
}

static void confirm_next_property(struct nk_context *ctx)
{
    nk_input_key(ctx, NK_KEY_ENTER, nk_true);

    ui_sdl_text_input_needed = 0;
}

static nk_bool check_next_property(struct nk_context *ctx, const char *name)
{
    struct nk_window *win = ctx->current;
    nk_hash hash = nk_hash_from_name(name, win);
    return win->property.active && win->property.name == hash;
}

static enum nk_nav_t do_nav_next(enum NK_FOCUS nk_focus)
{
    enum nk_nav_t ret = NK_NAV_NONE;
    if (nk_focus == nk_focus_current)
    {
        switch ((ret = __atomic_load_n(&nk_nav_cmd, __ATOMIC_RELAXED)))
        {
        case NK_NAV_PREVIOUS:
            if (nk_nav_focus != NK_NAV_FOCUS_NONE) {
                if (nk_focus_current <= NK_FOCUS_MIN)
                    nk_focus_current = NK_FOCUS_MAX;
                else
                    --nk_focus_current;
            }
            nk_nav_focus = NK_NAV_FOCUS_NAV;
            break;

        case NK_NAV_NEXT:
            if (nk_nav_focus != NK_NAV_FOCUS_NONE) {
                if (nk_focus_current >= NK_FOCUS_MAX)
                    nk_focus_current = NK_FOCUS_MIN;
                else
                    ++nk_focus_current;
            }
            nk_nav_focus = NK_NAV_FOCUS_NAV;
            break;

        case NK_NAV_CANCEL:
            if (nk_nav_focus == NK_NAV_FOCUS_NONE)
                ui_set_hide_nk_windows(1);
            else
                nk_nav_focus = NK_NAV_FOCUS_NONE;
            break;

        case NK_NAV_CONFIRM:
            nk_nav_focus = nk_nav_focus == NK_NAV_FOCUS_NONE ? NK_NAV_FOCUS_NAV : NK_NAV_FOCUS_NONE;
            break;

        default:
            break;
        }
        __atomic_store_n(&nk_nav_cmd, NK_NAV_NONE, __ATOMIC_RELAXED);
    }
    return ret;
}

static int *nk_nav_combo_selected;
static int nk_nav_combo_selected_pending;
static void set_nav_next(enum NK_NAV_FOCUS nav_focus, enum NK_FOCUS focus) {
    nk_nav_focus = nav_focus;
    nk_focus_current = focus;
    if (nk_nav_combo_selected) {
        *nk_nav_combo_selected = nk_nav_combo_selected_pending;
        nk_nav_combo_selected = 0;
    }
}

// HACK always allow property text edit input in current window
static nk_flags nav_layout_rom;

static void do_nav_property_next(struct nk_context *ctx, const char *name, enum NK_FOCUS nk_focus, int val)
{
    if (check_next_property(ctx, name))
    {
        if (nk_nav_focus == NK_NAV_FOCUS_NAV)
        {
            cancel_next_property(ctx);
        }
        else
        {
            set_nav_next(NK_NAV_FOCUS_NORMAL, nk_focus);
        }
    }
    else if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE)
    {
        focus_next_property(ctx, name, val);
        nk_nav_focus = NK_NAV_FOCUS_NORMAL;
    }

    if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE)
    {
        nav_layout_rom = ctx->current->layout->flags & NK_WINDOW_ROM;
        if (nav_layout_rom)
        {
            ctx->current->layout->flags &= ~NK_WINDOW_ROM;
        }

        switch (__atomic_load_n(&nk_nav_cmd, __ATOMIC_RELAXED))
        {
        case NK_NAV_PREVIOUS:
        case NK_NAV_NEXT:
        case NK_NAV_CONFIRM:
            confirm_next_property(ctx);
            break;

        case NK_NAV_CANCEL:
            cancel_next_property(ctx);
            break;

        default:
            break;
        }
    }

    do_nav_next(nk_focus);
}

static void check_nav_property_prev(struct nk_context *ctx, const char *name, enum NK_FOCUS nk_focus)
{
    struct nk_window *win = ctx->current;
    if (win->property.active)
    {
        nk_hash hash = nk_hash_from_name_prev(name, win, 1);
        if (win->property.name == hash)
        {
            set_nav_next(NK_NAV_FOCUS_NORMAL, nk_focus);
            if (win->property.state == NK_PROPERTY_EDIT)
                ui_sdl_text_input_needed = 1;
        }
    }
    else if (nk_nav_focus != NK_NAV_FOCUS_NAV)
    {
        nk_nav_focus = NK_NAV_FOCUS_NONE;
    }
    ctx->input.keyboard.keys[NK_KEY_ENTER].clicked = 0;

    if (nav_layout_rom)
    {
        ctx->current->layout->flags |= NK_WINDOW_ROM;
        nav_layout_rom = 0;
    }
}

static int nk_nav_combo_selected_previous;
static bool nk_nav_combo_focus;
static void do_nav_combobox_next(struct nk_context *ctx, enum NK_FOCUS nk_focus, int *selected, int *pending, int count)
{
    if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE)
    {
        if (!nk_nav_combo_selected) {
            nk_nav_combo_selected_pending = nk_nav_combo_selected_previous = *selected;
            nk_nav_combo_selected = pending;
        }

        *selected = nk_nav_combo_selected_pending;

        ctx->style.combo.border_color = ctx->style.text.color;
        if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DOWN))
        {
            ++*selected;
            if (*selected >= count)
            {
                *selected = 0;
            }
        }
        else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_UP))
        {
            --*selected;
            if (*selected < 0)
            {
                *selected = count - 1;
            }
        }

        nk_nav_combo_selected_pending = *selected;
        nk_nav_combo_focus = 1;
    } else {
        nk_nav_combo_focus = 0;
    }

    enum nk_nav_t cmd = do_nav_next(nk_focus);

    if (nk_nav_combo_selected) {
        switch (cmd) {
            case NK_NAV_CANCEL:
                if (*selected != nk_nav_combo_selected_previous) {
                    *selected = nk_nav_combo_selected_previous;
                    nk_nav_focus = NK_NAV_FOCUS_NAV;
                }
                goto final;
            case NK_NAV_CONFIRM:
                if (*selected != nk_nav_combo_selected_previous) {
                    nk_nav_focus = NK_NAV_FOCUS_NAV;
                }
                // fallthru
            case NK_NAV_NEXT:
            case NK_NAV_PREVIOUS:
final:
                nk_nav_combo_selected = 0;
                // fallthru
            case NK_NAV_NONE:
                break;
        }
    }
}

static bool nk_nav_combo_applied;
static void set_nav_combobox_prev(enum NK_FOCUS nk_focus)
{
    if (!nk_nav_combo_applied) {
        set_nav_next(NK_NAV_FOCUS_NAV, nk_focus);
    }
}

static void check_nav_combobox_prev(struct nk_context *ctx, int *selected)
{
    nk_nav_combo_applied = 0;
    if (nk_nav_combo_focus) {
        if (nk_nav_combo_selected && *selected == nk_nav_combo_selected_pending) {
            *selected = nk_nav_combo_selected_previous;
        } else {
            nk_nav_combo_selected = 0;
            nk_nav_combo_applied = 1;
        }
        nk_nav_combo_focus = 0;
    }
    ctx->style.combo.border_color = nk_style_current.combo.border_color;
}

static bool do_nav_button_next(struct nk_context *ctx, enum NK_FOCUS nk_focus)
{
    bool ret = false;
    if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE)
    {
        ctx->style.button.border_color = ctx->style.text.color;
        if (__atomic_load_n(&nk_nav_cmd, __ATOMIC_RELAXED) == NK_NAV_CONFIRM)
        {
            ret = true;
        }
    }

    if (ret)
    {
        __atomic_store_n(&nk_nav_cmd, NK_NAV_NONE, __ATOMIC_RELAXED);
    }
    else
    {
        do_nav_next(nk_focus);
    }

    return ret;
}

static void set_nav_button_prev(enum NK_FOCUS nk_focus)
{
    set_nav_next(NK_NAV_FOCUS_NAV, nk_focus);
}

static void check_nav_button_prev(struct nk_context *ctx)
{
    ctx->style.button.border_color = nk_style_current.button.border_color;
}

static nk_bool nk_nav_checkbox_val_current;
static void do_nav_checkbox_next(struct nk_context *ctx, enum NK_FOCUS nk_focus, nk_bool *val)
{
    bool ret = false;
    if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE)
    {
        // ctx->style.checkbox.cursor_hover.data.color = ctx->style.text.color;
        // ctx->style.checkbox.cursor_normal.data.color = ctx->style.text.color;
        ctx->style.checkbox.border = 1.0f;
        ctx->style.checkbox.border_color = ctx->style.text.color;
        if (__atomic_load_n(&nk_nav_cmd, __ATOMIC_RELAXED) == NK_NAV_CONFIRM)
        {
            ret = true;
            *val = !*val;
        }
    }

    if (ret)
    {
        __atomic_store_n(&nk_nav_cmd, NK_NAV_NONE, __ATOMIC_RELAXED);
    }
    else
    {
        do_nav_next(nk_focus);
    }

    nk_nav_checkbox_val_current = *val;
}

static void check_nav_checkbox_prev(struct nk_context *ctx, enum NK_FOCUS nk_focus, nk_bool val)
{
    ctx->style.checkbox.border_color = nk_style_current.checkbox.border_color;
    ctx->style.checkbox.border = nk_style_current.checkbox.border;
    // ctx->style.checkbox.cursor_normal.data.color = nk_style_current.checkbox.cursor_normal.data.color;
    // ctx->style.checkbox.cursor_hover.data.color = nk_style_current.checkbox.cursor_hover.data.color;

    if (nk_nav_checkbox_val_current != val)
    {
        set_nav_next(NK_NAV_FOCUS_NAV, nk_focus);
    }
}

static int nk_nav_slider_val_current;
static void do_nav_slider_next(struct nk_context *ctx, enum NK_FOCUS nk_focus, int *val)
{
    if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE)
    {
        ctx->style.slider.border = 1.0f;
        ctx->style.slider.border_color = ctx->style.text.color;

        if (nk_input_is_key_pressed(&ctx->input, NK_KEY_RIGHT))
        {
            ++*val;
        }
        else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_LEFT))
        {
            --*val;
        }
        else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_DOWN))
        {
            *val += 5;
        }
        else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_UP))
        {
            *val -= 5;
        }
        else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_START))
        {
            *val = 0;
        }
        else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_END))
        {
            *val = INT_MAX;
        }
    }

    do_nav_next(nk_focus);
    nk_nav_slider_val_current = *val;
}

static void check_nav_slider_prev(struct nk_context *ctx, enum NK_FOCUS nk_focus, int val)
{
    ctx->style.slider.border_color = nk_style_current.slider.border_color;
    ctx->style.slider.border = nk_style_current.slider.border;

    if (nk_nav_slider_val_current != val)
    {
        set_nav_next(NK_NAV_FOCUS_NAV, nk_focus);
    }
}

static int window_closed;
void ui_set_hide_nk_windows(bool hide) {
    ui_hide_nk_windows = hide;
    if (hide) {
        window_closed = -1;
    }
}

void ui_main_nk(void)
{
    struct nk_context *ctx = ui_nk_ctx;

    int focus_window = 0;
    ctx->style.window.fixed_background = nk_style_item_hide();
    if (nk_begin(ctx, background_wnd, nk_rect(0, 0, ui_win_width[SCREEN_TOP], ui_win_height[SCREEN_TOP]),
                 NK_WINDOW_BACKGROUND))
    {
        if (nk_window_is_hovered(ctx) && nk_window_is_active(ctx, background_wnd) &&
            nk_input_has_mouse_click(&ctx->input, NK_BUTTON_LEFT))
        {
            if (window_closed > 0) {
                window_closed = -1;
            } else {
                ui_set_hide_nk_windows(!ui_hide_nk_windows);
                if (ui_hide_nk_windows)
                    focus_window = 1;
            }
        }
    }
    nk_end(ctx);
    ctx->style.window.fixed_background = nk_style_current.window.fixed_background;

    int nav_command = __atomic_load_n(&nk_nav_cmd, __ATOMIC_RELAXED);
    if (ui_hide_nk_windows && (nav_command == NK_NAV_CANCEL || nav_command == NK_NAV_CONFIRM))
    {
        ui_set_hide_nk_windows(0);
        __atomic_store_n(&nk_nav_cmd, NK_NAV_NONE, __ATOMIC_RELAXED);
        focus_window = 1;
    }

    enum nk_show_states show_window = !ui_hide_nk_windows;
    char msg_buf[UI_MSG_BUF_LEN_MAX];

    if (window_closed && show_window) {
        nk_window_show(ctx, remote_play_wnd, 1);
        window_closed = 0;
    }
    if (nk_begin(ctx, remote_play_wnd, nk_rect(10, 10, 600, 650),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE | NK_WINDOW_CLOSABLE) &&
        show_window)
    {
        rp_lock_wait(ui_nk_lock);

        const char *combo_items_null = NULL;
        int combo_width = nk_window_get_width(ctx) / 2.0f - 15.0f;
        combo_width = MAX(combo_width, 285);

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "View Mode", NK_TEXT_CENTERED);
        int selected = ui_view_mode;
        struct nk_vec2 combo_size = {combo_width, 250};
        const char *view_mode_options[] = {
            "Top and Bottom",
            "Separate Windows",
            "Top Only",
            "Bottom Only"};
        int combo_count = sizeof(view_mode_options) / sizeof(*view_mode_options);
        do_nav_combobox_next(ctx, NK_FOCUS_VIEW_MODE, &selected, (int *)&ui_view_mode, combo_count);
        nk_combobox(ctx, view_mode_options, combo_count, &selected, 30, combo_size);
        check_nav_combobox_prev(ctx, &selected);
        if (selected != (int)ui_view_mode)
        {
            set_nav_combobox_prev(NK_FOCUS_VIEW_MODE);
            switch ((view_mode_t)selected) {
                case VIEW_MODE_TOP_BOT:
                case VIEW_MODE_SEPARATE:
                    if (!ntr_rp_config.screen_priority_factor) {
                        if (ntr_screen_priority_factor_prev) {
                            ntr_rp_config.top_screen_priority = ntr_top_screen_priority_prev;
                            ntr_rp_config.screen_priority_factor = ntr_screen_priority_factor_prev;
                            ntr_screen_priority_factor_prev = 0;
                        } else {
                            ntr_rp_config.top_screen_priority = true;
                            ntr_rp_config.screen_priority_factor = NTR_SCREEN_PRIORITY_FACTOR_DEFAULT;
                        }
                    }
                    break;
                case VIEW_MODE_TOP:
                    if (ntr_rp_config.screen_priority_factor || !ntr_rp_config.top_screen_priority) {
                        if (!ntr_screen_priority_factor_prev) {
                            ntr_top_screen_priority_prev = ntr_rp_config.top_screen_priority;
                            ntr_screen_priority_factor_prev = ntr_rp_config.screen_priority_factor;
                            if (!ntr_screen_priority_factor_prev)
                                ntr_screen_priority_factor_prev = NTR_SCREEN_PRIORITY_FACTOR_DEFAULT;
                        }

                        ntr_rp_config.top_screen_priority = true;
                        ntr_rp_config.screen_priority_factor = 0;
                    }
                    break;
                case VIEW_MODE_BOT:
                    if (ntr_rp_config.screen_priority_factor || ntr_rp_config.top_screen_priority) {
                        if (!ntr_screen_priority_factor_prev) {
                            ntr_top_screen_priority_prev = ntr_rp_config.top_screen_priority;
                            ntr_screen_priority_factor_prev = ntr_rp_config.screen_priority_factor;
                            if (!ntr_screen_priority_factor_prev)
                                ntr_screen_priority_factor_prev = NTR_SCREEN_PRIORITY_FACTOR_DEFAULT;
                        }

                        ntr_rp_config.top_screen_priority = false;
                        ntr_rp_config.screen_priority_factor = 0;
                    }
                    break;
            }
            ui_view_mode = selected;
            ui_fullscreen = 0;
        }

        nk_draw_push_color_inline(ctx, NK_COLOR_INLINE_TAG);
        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Upscaling Filter", NK_TEXT_CENTERED);
        selected = ui_upscaling_selected;
        do_nav_combobox_next(ctx, NK_FOCUS_UPSCALING_FILTER, &selected, &ui_upscaling_selected, ui_upscaling_filter_options ? ui_upscaling_filter_count : 0);
        if (ui_upscaling_filter_options)
            nk_combobox(ctx, ui_upscaling_filter_options, ui_upscaling_filter_count, &selected, 30, combo_size);
        else
            nk_combobox(ctx, &combo_items_null, 0, &selected, 30, combo_size);
        check_nav_combobox_prev(ctx, &selected);
        if (selected != ui_upscaling_selected) {
            set_nav_combobox_prev(NK_FOCUS_UPSCALING_FILTER);
            ui_upscaling_selected = selected;
            cursor_scale_prev = 0.0f;
        }
        nk_draw_pop_color_inline(ctx);

        nk_layout_row_dynamic(ctx, 30, 5);
        nk_label(ctx, "3DS IP", NK_TEXT_CENTERED);

        for (int i = 0; i < NTR_IP_OCTET_SIZE; ++i)
        {
            int ip_octet = ntr_ip_octet[i];
            do_nav_property_next(ctx, nk_property_name, NK_FOCUS_IP_OCTET_0 + i, ip_octet);
            nk_property_int(ctx, nk_property_name, 0, &ip_octet, 255, 1, 1);
            check_nav_property_prev(ctx, nk_property_name, NK_FOCUS_IP_OCTET_0 + i);
            if (ip_octet != ntr_ip_octet[i])
            {
                ntr_ip_octet[i] = ip_octet;
                if (ntr_auto_ip_list)
                    strcpy(ntr_auto_ip_list[0], "Manual");
                ntr_selected_ip = 0;
                *(uint32_t *)ntr_ip_octet_incoming = 0;
                ntr_get_adapter_list();
                if (menu_work_state == CONNECTION_STATE_CONNECTED)
                {
                    menu_work_req_state = CONNECTION_REQ_STATE_DISCONNECTING;
                }
            }
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        bool button_ret;
        button_ret = do_nav_button_next(ctx, NK_FOCUS_IP_AUTO_DETECT);
        if (nk_button_label(ctx, "Auto-Detect") || button_ret)
        {
            set_nav_button_prev(NK_FOCUS_IP_AUTO_DETECT);
            if (menu_work_state == CONNECTION_STATE_CONNECTED)
            {
                menu_work_req_state = CONNECTION_REQ_STATE_DISCONNECTING;
            }
            ntr_detect_3ds_ip();
#ifndef __APPLE__
            ntr_get_adapter_list();
#endif
        }
        check_nav_button_prev(ctx);
        selected = ntr_selected_ip;
        do_nav_combobox_next(ctx, NK_FOCUS_IP_COMBO, &selected, &ntr_selected_ip, ntr_auto_ip_list ? ntr_auto_ip_count : 0);
        if (ntr_auto_ip_list)
            nk_combobox(ctx, (const char **)ntr_auto_ip_list, ntr_auto_ip_count, &selected, 30, combo_size);
        else
            nk_combobox(ctx, &combo_items_null, 0, &selected, 30, combo_size);
        check_nav_combobox_prev(ctx, &selected);
        if (selected != ntr_selected_ip)
        {
            set_nav_combobox_prev(NK_FOCUS_IP_COMBO);
            ntr_selected_ip = selected;
            if (ntr_selected_ip)
            {
                memcpy(ntr_ip_octet, ntr_auto_ip_octet_list[ntr_selected_ip], NTR_IP_OCTET_SIZE);
                if (menu_work_state == CONNECTION_STATE_CONNECTED)
                {
                    menu_work_req_state = CONNECTION_REQ_STATE_DISCONNECTING;
                }
                ntr_get_adapter_list();
            }
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Viewer IP", NK_TEXT_CENTERED);
        selected = ntr_selected_adapter;
        do_nav_combobox_next(ctx, NK_FOCUS_VIEWER_IP, &selected, &ntr_selected_adapter, ntr_adapter_list ? ntr_adapter_count : 0);
        if (ntr_adapter_list)
            nk_combobox(ctx, (const char **)ntr_adapter_list, ntr_adapter_count, &selected, 30, combo_size);
        else
            nk_combobox(ctx, &combo_items_null, 0, &selected, 30, combo_size);
        check_nav_combobox_prev(ctx, &selected);
        if (selected != ntr_selected_adapter)
        {
            set_nav_combobox_prev(NK_FOCUS_VIEWER_IP);
            ntr_selected_adapter = selected;
            if (ntr_selected_adapter == ntr_adapter_count - NTR_ADAPTER_POST_COUNT + NTR_ADAPTER_POST_AUTO)
            {
                ntr_try_auto_select_adapter();
            }
            else if (ntr_selected_adapter == ntr_adapter_count - NTR_ADAPTER_POST_COUNT + NTR_ADAPTER_POST_REFRESH)
            {
                ntr_get_adapter_list();
            } else {
                ntr_rp_port_changed = 1;
                kcp_restart = 1;
            }
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Viewer Port", NK_TEXT_CENTERED);
        do_nav_property_next(ctx, nk_property_name, NK_FOCUS_VIEWER_PORT, ntr_rp_port);
        nk_property_int(ctx, nk_property_name, 1024, &ntr_rp_port, 65535, 1, 1);
        check_nav_property_prev(ctx, nk_property_name, NK_FOCUS_VIEWER_PORT);
        if (ntr_rp_port_bound != ntr_rp_port)
        {
            ntr_rp_port_bound = ntr_rp_port;
            ntr_rp_port_changed = 1;
            kcp_restart = 1;
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Prioritize Top Screen", NK_TEXT_CENTERED);
        do_nav_checkbox_next(ctx, NK_FOCUS_PRIORITY_SCREEN, &ntr_rp_config.top_screen_priority);
        nk_checkbox_label(ctx, "", &ntr_rp_config.top_screen_priority);
        check_nav_checkbox_prev(ctx, NK_FOCUS_PRIORITY_SCREEN, ntr_rp_config.top_screen_priority);

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Priority Screen Factor", NK_TEXT_CENTERED);
        do_nav_property_next(ctx, nk_property_name, NK_FOCUS_PRIORITY_FACTOR, ntr_rp_config.screen_priority_factor);
        nk_property_int(ctx, nk_property_name, 0, &ntr_rp_config.screen_priority_factor, 255, 1, 1);
        check_nav_property_prev(ctx, nk_property_name, NK_FOCUS_PRIORITY_FACTOR);

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Compression Format and Protocol", NK_TEXT_CENTERED);
        selected = ntr_rp_config.kcp_mode;
        const char *compression_fmt_prot[] = {
            "JPEG Compat (UDP)",
            "JPEG (Reliable Stream)",
            "JPEG (Reliable Stream, Delta)",
            "Uncompressed (UDP)",
            "Lossless (Reliable Stream)",
            "Lossless (Reliable Stream, Delta)",
        };
        combo_count = sizeof(compression_fmt_prot) / sizeof(*compression_fmt_prot);
        do_nav_combobox_next(ctx, NK_FOCUS_FMT_PROT, &selected, &ntr_rp_config.kcp_mode, combo_count);
        nk_combobox(ctx, compression_fmt_prot, combo_count, &selected, 30, combo_size);
        check_nav_combobox_prev(ctx, &selected);
        if (selected != ntr_rp_config.kcp_mode)
        {
            set_nav_combobox_prev(NK_FOCUS_FMT_PROT);
            ntr_rp_config.kcp_mode = selected;
        }

        if (ntr_rp_config.kcp_mode / KCP_MODE_COUNT) {
            nk_layout_row_dynamic(ctx, 30, 2);
            snprintf(msg_buf, sizeof(msg_buf), "Color Quality Bias %d", ntr_rp_config.lossless_color - NTR_COLOR_BIAS_MAX);
            nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
            do_nav_slider_next(ctx, NK_FOCUS_QUALITY, &ntr_rp_config.lossless_color);
            nk_slider_int(ctx, NTR_COLOR_BIAS_MIN, &ntr_rp_config.lossless_color, NTR_COLOR_BIAS_MAX, 1);
            check_nav_slider_prev(ctx, NK_FOCUS_QUALITY, ntr_rp_config.lossless_color);
        } else {
            nk_layout_row_dynamic(ctx, 30, 2);
            snprintf(msg_buf, sizeof(msg_buf), "JPEG Quality %d", ntr_rp_config.jpeg_quality);
            nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
            do_nav_slider_next(ctx, NK_FOCUS_QUALITY, &ntr_rp_config.jpeg_quality);
            nk_slider_int(ctx, NTR_JPEG_QUALITY_MIN, &ntr_rp_config.jpeg_quality, NTR_JPEG_QUALITY_MAX, 1);
            check_nav_slider_prev(ctx, NK_FOCUS_QUALITY, ntr_rp_config.jpeg_quality);

            nk_layout_row_dynamic(ctx, 30, 2);
            if (ntr_auto_quality) {
                snprintf(msg_buf, sizeof(msg_buf), "Auto Quality (now %d)",
                    MIN(ntr_rp_config.jpeg_quality, (int)ntr_jpeg_quality_auto));
                nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
            } else {
                nk_label(ctx, "Auto Quality", NK_TEXT_CENTERED);
            }
            do_nav_checkbox_next(ctx, NK_FOCUS_AUTO_QUALITY, &ntr_auto_quality);
            nk_checkbox_label(ctx, "", &ntr_auto_quality);
            check_nav_checkbox_prev(ctx, NK_FOCUS_AUTO_QUALITY, ntr_auto_quality);
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        snprintf(msg_buf, sizeof(msg_buf), "Bandwidth Limit %d Mbps", ntr_rp_config.bandwidth_limit);
        nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
        do_nav_slider_next(ctx, NK_FOCUS_BANDWIDTH_LIMIT, &ntr_rp_config.bandwidth_limit);
        nk_slider_int(ctx, 4, &ntr_rp_config.bandwidth_limit, 20, 1);
        check_nav_slider_prev(ctx, NK_FOCUS_BANDWIDTH_LIMIT, ntr_rp_config.bandwidth_limit);

        nk_layout_row_dynamic(ctx, 30, 2);
        button_ret = do_nav_button_next(ctx, NK_FOCUS_DEFAULT);
        if (nk_button_label(ctx, "Default") || button_ret)
        {
            set_nav_button_prev(NK_FOCUS_DEFAULT);
            ntr_config_set_default();
        }
        check_nav_button_prev(ctx);

        button_ret = do_nav_button_next(ctx, NK_FOCUS_CONNECT);
        if (nk_button_label(ctx, "Connect") || button_ret)
        {
            set_nav_button_prev(NK_FOCUS_CONNECT);
            menu_remote_play = 1;
            if (menu_work_state == CONNECTION_STATE_DISCONNECTED)
            {
                menu_work_req_state = CONNECTION_REQ_STATE_CONNECTING;
            }
            kcp_restart = 1;
        }
        check_nav_button_prev(ctx);

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Input Redirection", NK_TEXT_CENTERED);
        selected = ui_controller_selected;
        do_nav_combobox_next(ctx, NK_FOCUS_INPUT_REDIRECTION, &selected, &ui_controller_selected, ui_controllers_names ? ui_num_controllers : 0);
        if (ui_controllers_names)
            nk_combobox(ctx, ui_controllers_names, ui_num_controllers, &selected, 30, combo_size);
        else
            nk_combobox(ctx, &combo_items_null, 0, &selected, 30, combo_size);
        check_nav_combobox_prev(ctx, &selected);
        if (selected != ui_controller_selected)
        {
            set_nav_combobox_prev(NK_FOCUS_INPUT_REDIRECTION);
            if (selected == ui_num_controllers - 1) { // Refresh List
                ui_update_game_controllers();
            } else {
                ui_controller_selected = selected;
            }
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Swap A/B X/Y", NK_TEXT_CENTERED);
        do_nav_checkbox_next(ctx, NK_FOCUS_INPUT_SWAP_FACE, &ui_controller_swap_face_buttons);
        nk_checkbox_label(ctx, "", &ui_controller_swap_face_buttons);
        check_nav_checkbox_prev(ctx, NK_FOCUS_INPUT_SWAP_FACE, ui_controller_swap_face_buttons);

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Bottom Screen Cursor", NK_TEXT_CENTERED);
        selected = sdl_bottom_screen_cursor_size;
        const char *cursor_size_options[] = {
            "Small",
            "Medium",
            "Large",
            "Extra Large",
        };
        combo_count = sizeof(cursor_size_options) / sizeof(*cursor_size_options);
        do_nav_combobox_next(ctx, NK_FOCUS_INPUT_CURSOR_SIZE, &selected, &sdl_bottom_screen_cursor_size, combo_count);
        nk_combobox(ctx, cursor_size_options, combo_count, &selected, 30, combo_size);
        check_nav_combobox_prev(ctx, &selected);
        if (selected != sdl_bottom_screen_cursor_size)
        {
            set_nav_combobox_prev(NK_FOCUS_INPUT_CURSOR_SIZE);
            sdl_bottom_screen_cursor_size = selected;
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Ambience Quality", NK_TEXT_CENTERED);
        selected = ui_blur_iter;
        const char *blue_iter_options[] = {
            "Off",
            "1",
            "2",
            "3",
        };
        combo_count = sizeof(blue_iter_options) / sizeof(*blue_iter_options);
        do_nav_combobox_next(ctx, NK_FOCUS_BORDER_ITER, &selected, &ui_blur_iter, combo_count);
        nk_combobox(ctx, blue_iter_options, combo_count, &selected, 30, combo_size);
        check_nav_combobox_prev(ctx, &selected);
        if (selected != ui_blur_iter)
        {
            set_nav_combobox_prev(NK_FOCUS_BORDER_ITER);
            ui_blur_iter = selected;
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        snprintf(msg_buf, sizeof(msg_buf), "Ambience Blur %d", ui_blur_radius);
        nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
        do_nav_slider_next(ctx, NK_FOCUS_BORDER_RADIUS, &ui_blur_radius);
        nk_slider_int(ctx, 3, &ui_blur_radius, UI_BLUR_RADIUS_MAX, 1);
        check_nav_slider_prev(ctx, NK_FOCUS_BORDER_RADIUS, ui_blur_radius);

        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label(ctx, "Press \"F\" to toggle fullscreen.", NK_TEXT_CENTERED);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label(ctx, "Input redirection can be enabled in Luma3DS/Rosalina's menu.", NK_TEXT_CENTERED);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label(ctx, "(Default key combo: L+Down+Select)", NK_TEXT_CENTERED);
        nk_layout_row_dynamic(ctx, 60, 1);
        nk_label_wrap(ctx,
            "To enable remote play with games that disable Wi-Fi during gameplay, "
            "such as games in the Pokemon series, "
            "enable either the debugger or the input redirection feature in Luma3DS/Rosalina's menu."
        );
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label(ctx, "Additional options available in the NTR-HR menu.", NK_TEXT_CENTERED);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label(ctx, "(Default key combo: X+Y)", NK_TEXT_CENTERED);

        rp_lock_rel(ui_nk_lock);
    }
    nk_end(ctx);
    if (!window_closed) {
        window_closed = nk_window_is_hidden(ctx, remote_play_wnd);
        if (window_closed) {
            ui_hide_nk_windows = 1;
            show_window = 0;
        }
    }
    nk_window_show(ctx, remote_play_wnd, show_window);

    if (focus_window)
        nk_window_set_focus(ctx, remote_play_wnd);

    if (nk_begin(ctx, debug_msg_wnd, nk_rect(615, 10, 175, 275),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE) &&
        show_window)
    {
        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "Menu", NK_TEXT_CENTERED);
        menu_connection = menu_work_state;
        menu_connection_req = menu_work_req_state;
        if (nk_button_label(ctx, menu_connection_req ? connection_req_msg[menu_connection_req] : connection_msg[menu_connection]))
        {
            if (menu_connection == CONNECTION_STATE_DISCONNECTED)
            {
                menu_work_req_state = CONNECTION_REQ_STATE_CONNECTING;
            }
            else if (menu_connection == CONNECTION_STATE_CONNECTED)
            {
                menu_work_req_state = CONNECTION_REQ_STATE_DISCONNECTING;
                ntr_auto_reconnect = nk_false;
            }
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "NWM", NK_TEXT_CENTERED);
        nwm_connection = nwm_work_state;
        nwm_connection_req = nwm_work_req_state;
        if (nk_button_label(ctx, nwm_connection_req ? connection_req_msg[nwm_work_req_state] : connection_msg[nwm_connection]))
        {
            if (nwm_connection == CONNECTION_STATE_DISCONNECTED)
            {
                nwm_work_req_state = CONNECTION_REQ_STATE_CONNECTING;
            }
            else if (nwm_connection == CONNECTION_STATE_CONNECTED)
            {
                nwm_work_req_state = CONNECTION_REQ_STATE_DISCONNECTING;
            }
        }

        nk_layout_row_dynamic(ctx, 30, 2);
        nk_label(ctx, "NWM O3DS", NK_TEXT_CENTERED);
        nwm_o3ds_connection = nwm_o3ds_work_state;
        nwm_o3ds_connection_req = nwm_o3ds_work_req_state;
        if (nk_button_label(ctx, nwm_o3ds_connection_req ? connection_req_msg[nwm_o3ds_work_req_state] : connection_msg[nwm_o3ds_connection]))
        {
            if (nwm_o3ds_connection == CONNECTION_STATE_DISCONNECTED)
            {
                nwm_o3ds_work_req_state = CONNECTION_REQ_STATE_CONNECTING;
            }
            else if (nwm_o3ds_connection == CONNECTION_STATE_CONNECTED)
            {
                nwm_o3ds_work_req_state = CONNECTION_REQ_STATE_DISCONNECTING;
            }
        }

        nk_layout_row_dynamic(ctx, 30, 1);
        nk_checkbox_label(ctx, "Stats", &ntr_stats_overlay);

        nk_layout_row_dynamic(ctx, 30, 1);
        nk_checkbox_label(ctx, "Auto-Reconnect", &ntr_auto_reconnect);
        if (!ntr_auto_reconnect) {
            ntr_auto_update_params = nk_false;
        }

        nk_layout_row_dynamic(ctx, 30, 1);
        nk_checkbox_label(ctx, "Auto-Update Params", &ntr_auto_update_params);
        if (ntr_auto_update_params) {
            ntr_auto_reconnect = nk_true;
        }
    }
    nk_end(ctx);
    nk_window_show(ctx, debug_msg_wnd, show_window);
}
