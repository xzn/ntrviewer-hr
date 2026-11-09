/* settings.c -- see settings.h */
#include "settings.h"
#include "ntr_common.h"
#include "ui_common_sdl.h"
#include "ui_main_nk.h"
#include "ui_input_redirection.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_VERSION 1
#define SETTINGS_SAVE_DELAY_MS 1000
#define SETTINGS_UPSCALING_NAME_MAX 64

nk_bool settings_auto_reconnect = nk_true;
nk_bool settings_auto_update_params = nk_true;

struct settings_win_t {
    int valid;
    int x, y, w, h;
};

struct settings_t {
    int view_mode;
    int top_screen_priority;
    int screen_priority_factor;
    int jpeg_quality;
    int bandwidth_limit;
    int kcp_mode;
    int lossless_color;
    int auto_quality;
    int audio;
    int full_width;
    int auto_reconnect;
    int auto_update_params;
    int stats;
    int ip[NTR_IP_OCTET_SIZE];
    int viewer_port;
    char upscaling[SETTINGS_UPSCALING_NAME_MAX];
    int swap_face_buttons;
    int cursor_size;
    int ambience_quality;
    int ambience_blur;
    struct settings_win_t win[SCREEN_COUNT];
};

static struct settings_t file_settings; /* as last read or written */
static bool file_settings_valid;
static uint64_t change_time_ms;
static bool write_failed_logged;

struct settings_int_key_t {
    const char *name;
    size_t offset;
    int min, max;
};

#define SK(name, field, min, max) { name, offsetof(struct settings_t, field), min, max }
static const struct settings_int_key_t int_keys[] = {
    SK("view_mode", view_mode, VIEW_MODE_TOP_BOT, VIEW_MODE_BOT),
    SK("top_screen_priority", top_screen_priority, 0, 1),
    SK("screen_priority_factor", screen_priority_factor, 0, 255),
    SK("jpeg_quality", jpeg_quality, NTR_JPEG_QUALITY_MIN, NTR_JPEG_QUALITY_MAX),
    SK("bandwidth_limit", bandwidth_limit, NTR_QOS_MIN, NTR_QOS_MAX),
    SK("compression", kcp_mode, 0, KCP_MODE_L_ON_DELTA),
    SK("color_quality_bias", lossless_color, NTR_COLOR_BIAS_MIN, NTR_COLOR_BIAS_MAX),
    SK("auto_quality", auto_quality, 0, 1),
    SK("audio", audio, 0, 1),
    SK("full_width", full_width, 0, 1),
    SK("auto_reconnect", auto_reconnect, 0, 1),
    SK("auto_update_params", auto_update_params, 0, 1),
    SK("stats", stats, 0, 1),
    SK("viewer_port", viewer_port, 1024, 65535),
    SK("swap_face_buttons", swap_face_buttons, 0, 1),
    SK("cursor_size", cursor_size, 0, 3),
    SK("ambience_quality", ambience_quality, 0, 3),
    SK("ambience_blur", ambience_blur, 3, UI_BLUR_RADIUS_MAX),
    SK("top_window_x", win[SCREEN_TOP].x, -100000, 100000),
    SK("top_window_y", win[SCREEN_TOP].y, -100000, 100000),
    SK("top_window_width", win[SCREEN_TOP].w, 64, 100000),
    SK("top_window_height", win[SCREEN_TOP].h, 64, 100000),
    SK("bottom_window_x", win[SCREEN_BOT].x, -100000, 100000),
    SK("bottom_window_y", win[SCREEN_BOT].y, -100000, 100000),
    SK("bottom_window_width", win[SCREEN_BOT].w, 64, 100000),
    SK("bottom_window_height", win[SCREEN_BOT].h, 64, 100000),
};
#undef SK
#define INT_KEY_COUNT ((int)(sizeof(int_keys) / sizeof(*int_keys)))

#define INT_KEY_WIN_COUNT (4)
#define INT_KEY_WIN_FIRST (INT_KEY_COUNT - INT_KEY_WIN_COUNT * SCREEN_COUNT)

static char *settings_path(const char *suffix) {
    static char path[1024];
    const char *base = opt_flag_portable ? SDL_GetBasePath() : SDL_GetPrefPath("NTRViewer", "NTRViewer-HR");
    snprintf(path, sizeof(path), "%s%s%s",
        base ? base : "",
        opt_arg_settings ? opt_arg_settings : SETTINGS_FILE_NAME,
        suffix);
    return path;
}

/* Only remember a window's place while it's visible and a normal window. */
static void capture_window(int i, struct settings_win_t *w) {
    SDL_Window *win = ui_sdl_win[i];
    if (!win)
        return;
    SDL_WindowFlags flags = SDL_GetWindowFlags(win);
    if (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN))
        return;
    int x, y, ww, hh;
    if (!SDL_GetWindowPosition(win, &x, &y) || !SDL_GetWindowSize(win, &ww, &hh))
        return;
    if (ww < 64 || hh < 64)
        return;
    *w = (struct settings_win_t){ .valid = 1, .x = x, .y = y, .w = ww, .h = hh };
}

static void capture(struct settings_t *s) {
    s->view_mode = __atomic_load_n(&ui_view_mode, __ATOMIC_RELAXED);
    s->top_screen_priority = ntr_rp_config.top_screen_priority ? 1 : 0;
    s->screen_priority_factor = ntr_rp_config.screen_priority_factor;
    s->jpeg_quality = ntr_rp_config.jpeg_quality;
    s->bandwidth_limit = ntr_rp_config.bandwidth_limit;
    s->kcp_mode = ntr_rp_config.kcp_mode;
    s->lossless_color = ntr_rp_config.lossless_color;
    s->auto_quality = ntr_auto_quality ? 1 : 0;
    s->audio = ntr_rp_config.audio_enable ? 1 : 0;
    s->full_width = ntr_rp_config.full_width ? 1 : 0;
    s->auto_reconnect = settings_auto_reconnect ? 1 : 0;
    s->auto_update_params = settings_auto_update_params ? 1 : 0;
    s->stats = ntr_stats_overlay ? 1 : 0;
    for (int i = 0; i < NTR_IP_OCTET_SIZE; ++i)
        s->ip[i] = ntr_ip_octet[i];
    s->viewer_port = ntr_rp_port;

    int up = ui_upscaling_selected;
    if (ui_upscaling_filter_options && up >= 0 && up < ui_upscaling_filter_count && ui_upscaling_filter_options[up])
        snprintf(s->upscaling, sizeof(s->upscaling), "%s", ui_upscaling_filter_options[up]);

    s->swap_face_buttons = ui_controller_swap_face_buttons ? 1 : 0;
    s->cursor_size = sdl_bottom_screen_cursor_size;
    s->ambience_quality = ui_blur_iter;
    s->ambience_blur = ui_blur_radius;

    capture_window(SCREEN_TOP, &s->win[SCREEN_TOP]);
    if (s->view_mode == VIEW_MODE_SEPARATE)
        capture_window(SCREEN_BOT, &s->win[SCREEN_BOT]);
}

static char *trim(char *p) {
    while (*p == ' ' || *p == '\t')
        ++p;
    char *e = p + strlen(p);
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        --e;
    *e = 0;
    return p;
}

static bool parse_int(const char *v, int *out) {
    char *end;
    long n = strtol(v, &end, 10);
    if (end == v || *end)
        return false;
    *out = (int)n;
    return true;
}

static bool parse_ip(const char *v, int ip[NTR_IP_OCTET_SIZE]) {
    int a[NTR_IP_OCTET_SIZE];
    char extra;
    if (sscanf(v, "%d.%d.%d.%d%c", &a[0], &a[1], &a[2], &a[3], &extra) != 4)
        return false;
    for (int i = 0; i < NTR_IP_OCTET_SIZE; ++i)
        if (a[i] < 0 || a[i] > 255)
            return false;
    memcpy(ip, a, sizeof(a));
    return true;
}

/* Fills s from the file; keys that are missing or bad keep what s had. */
static bool read_file(struct settings_t *s) {
    size_t size = 0;
    char *data = SDL_LoadFile(settings_path(""), &size);
    if (!data) {
        err_log("no settings file yet (%s), using defaults\n", settings_path(""));
        return false;
    }

    bool win_seen[SCREEN_COUNT][4] = {};
    char *save = NULL;
    for (char *line = SDL_strtok_r(data, "\n", &save); line; line = SDL_strtok_r(NULL, "\n", &save)) {
        line = trim(line);
        if (!*line || *line == ';' || *line == '#' || *line == '[')
            continue;
        char *eq = strchr(line, '=');
        if (!eq) {
            err_log("settings: ignoring line \"%s\"\n", line);
            continue;
        }
        *eq = 0;
        char *key = trim(line), *val = trim(eq + 1);

        if (strcmp(key, "ip") == 0) {
            if (!parse_ip(val, s->ip))
                err_log("settings: bad ip \"%s\"\n", val);
            continue;
        }
        if (strcmp(key, "upscaling_filter") == 0) {
            snprintf(s->upscaling, sizeof(s->upscaling), "%s", val);
            continue;
        }
        int k = 0;
        for (; k < INT_KEY_COUNT; ++k)
            if (strcmp(key, int_keys[k].name) == 0)
                break;
        if (k == INT_KEY_COUNT)
            continue; /* unknown key, e.g. "version" or one from a newer build */
        int n;
        if (!parse_int(val, &n) || n < int_keys[k].min || n > int_keys[k].max) {
            err_log("settings: bad value for %s: \"%s\"\n", key, val);
            continue;
        }
        *(int *)((char *)s + int_keys[k].offset) = n;
        if (k >= INT_KEY_WIN_FIRST) {
            int w = (k - INT_KEY_WIN_FIRST) / INT_KEY_WIN_COUNT;
            win_seen[w][(k - INT_KEY_WIN_FIRST) % INT_KEY_WIN_COUNT] = true;
        }
    }
    SDL_free(data);

    for (int i = 0; i < SCREEN_COUNT; ++i)
        s->win[i].valid = win_seen[i][0] && win_seen[i][1] && win_seen[i][2] && win_seen[i][3];
    return true;
}

static bool write_file(const struct settings_t *s) {
    const char *tmp = settings_path(".tmp");
    char tmp_copy[1024];
    snprintf(tmp_copy, sizeof(tmp_copy), "%s", tmp);

    SDL_IOStream *io = SDL_IOFromFile(tmp_copy, "wb");
    if (!io)
        goto fail;

    bool ok = true;
    ok = ok && SDL_IOprintf(io,
        "; NTR-VC settings. Saved automatically; delete this file to go back to the defaults.\n"
        "version=%d\n\n[remote_play]\n", SETTINGS_VERSION);
    ok = ok && SDL_IOprintf(io, "ip=%d.%d.%d.%d\n", s->ip[0], s->ip[1], s->ip[2], s->ip[3]);
    ok = ok && SDL_IOprintf(io, "upscaling_filter=%s\n", s->upscaling);
    for (int k = 0; ok && k < INT_KEY_COUNT; ++k) {
        if (k == INT_KEY_WIN_FIRST)
            ok = SDL_IOprintf(io, "\n[windows]\n");
        if (k >= INT_KEY_WIN_FIRST && !s->win[(k - INT_KEY_WIN_FIRST) / INT_KEY_WIN_COUNT].valid)
            continue;
        ok = ok && SDL_IOprintf(io, "%s=%d\n", int_keys[k].name,
            *(const int *)((const char *)s + int_keys[k].offset));
    }
    if (!SDL_CloseIO(io))
        ok = false;
    if (!ok)
        goto fail;

    if (!SDL_RenamePath(tmp_copy, settings_path("")))
        goto fail;

    write_failed_logged = false;
    return true;

fail:
    if (!write_failed_logged) {
        err_log("could not save settings to %s: %s\n", settings_path(""), SDL_GetError());
        write_failed_logged = true;
    }
    return false;
}

void settings_load(void) {
    struct settings_t s;
    memset(&s, 0, sizeof(s));
    capture(&s); /* the defaults */
    memset(s.win, 0, sizeof(s.win));

    if (!read_file(&s))
        return;

    ui_view_mode = s.view_mode;
    ntr_rp_config.top_screen_priority = s.top_screen_priority;
    ntr_rp_config.screen_priority_factor = s.screen_priority_factor;
    ntr_rp_config.jpeg_quality = s.jpeg_quality;
    ntr_rp_config.bandwidth_limit = s.bandwidth_limit;
    ntr_rp_config.kcp_mode = s.kcp_mode;
    ntr_rp_config.lossless_color = s.lossless_color;
    ntr_rp_config.audio_enable = s.audio;
    ntr_rp_config.full_width = s.full_width;
    ntr_auto_quality = s.auto_quality;
    if (s.auto_update_params)
        s.auto_reconnect = 1; /* the Debug window keeps these two in step */
    settings_auto_reconnect = ntr_auto_reconnect = s.auto_reconnect;
    settings_auto_update_params = ntr_auto_update_params = s.auto_update_params;
    ntr_stats_overlay = s.stats;
    ntr_rp_port = s.viewer_port;
    ui_controller_swap_face_buttons = s.swap_face_buttons;
    sdl_bottom_screen_cursor_size = s.cursor_size;
    ui_blur_iter = s.ambience_quality;
    ui_blur_radius = s.ambience_blur;

    if (ui_upscaling_filter_options) {
        for (int i = 0; i < ui_upscaling_filter_count; ++i) {
            if (ui_upscaling_filter_options[i] && strcmp(ui_upscaling_filter_options[i], s.upscaling) == 0) {
                ui_upscaling_selected = i;
                break;
            }
        }
    }

    file_settings = s;
    file_settings_valid = true;
    err_log("settings loaded from %s\n", settings_path(""));
}

/* True when at least 100x50 of the rect is on some display, so a window saved
 * on a monitor that's gone now doesn't come back off-screen. */
static bool rect_on_screen(int x, int y, int w, int h) {
    int count = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&count);
    if (!ids)
        return false;
    bool ok = false;
    SDL_Rect r = { x, y, w, h };
    for (int i = 0; i < count && !ok; ++i) {
        SDL_Rect b, out;
        if (SDL_GetDisplayUsableBounds(ids[i], &b) && SDL_GetRectIntersection(&r, &b, &out))
            ok = out.w >= 100 && out.h >= 50;
    }
    SDL_free(ids);
    return ok;
}

void settings_apply_windows(void) {
    if (!file_settings_valid)
        return;
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        const struct settings_win_t *w = &file_settings.win[i];
        if (!w->valid || !ui_sdl_win[i])
            continue;
        if (i == SCREEN_BOT && file_settings.view_mode != VIEW_MODE_SEPARATE)
            continue;
        SDL_SetWindowSize(ui_sdl_win[i], w->w, w->h);
        if (rect_on_screen(w->x, w->y, w->w, w->h))
            SDL_SetWindowPosition(ui_sdl_win[i], w->x, w->y);
    }
}

void settings_apply_ip(void) {
    if (!file_settings_valid)
        return;
    const int *ip = file_settings.ip;
    if (!(ip[0] | ip[1] | ip[2] | ip[3]))
        return;

    if (ntr_selected_ip && ntr_auto_ip_octet_list) {
        /* Auto-detect found a 3DS. Prefer the saved one if it's in the list,
         * otherwise trust detection (the 3DS may have a new address). */
        for (int i = 1; i < ntr_auto_ip_count; ++i) {
            const uint8_t *o = ntr_auto_ip_octet_list[i];
            if (o[0] == ip[0] && o[1] == ip[1] && o[2] == ip[2] && o[3] == ip[3]) {
                ntr_selected_ip = i;
                memcpy(ntr_ip_octet, o, NTR_IP_OCTET_SIZE);
                break;
            }
        }
        return;
    }

    for (int i = 0; i < NTR_IP_OCTET_SIZE; ++i)
        ntr_ip_octet[i] = ip[i];
    if (ntr_auto_ip_list)
        strcpy(ntr_auto_ip_list[0], "Manual");
    ntr_selected_ip = 0;
    err_log("using saved 3DS IP %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
}

static void save_if_changed(bool now) {
    struct settings_t s = file_settings;
    capture(&s);
    if (file_settings_valid && memcmp(&s, &file_settings, sizeof(s)) == 0) {
        change_time_ms = 0;
        return;
    }
    uint64_t t = SDL_GetTicks();
    if (!now) {
        if (!change_time_ms) {
            change_time_ms = t ? t : 1;
            return;
        }
        if (t - change_time_ms < SETTINGS_SAVE_DELAY_MS)
            return;
    }
    change_time_ms = 0;
    /* Remember what we tried even if the write fails, so a read-only folder
     * isn't retried every second; the next change tries again. */
    file_settings = s;
    file_settings_valid = true;
    write_file(&s);
}

void settings_tick(void) {
    save_if_changed(false);
}

void settings_save(void) {
    save_if_changed(true);
}
