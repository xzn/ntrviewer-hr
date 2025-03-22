#ifndef UI_COMMON_SDL_H
#define UI_COMMON_SDL_H

#include "const.h"

#include <stdbool.h>
#include "nuklear/nuklear.h"

enum ui_renderer_t {
    UI_RENDERER_D3D11_CSC,
    UI_RENDERER_D3D11,
    UI_RENDERER_OGL_CSC,
    UI_RENDERER_OGL,
    UI_RENDERER_GLES_CSC,
    UI_RENDERER_GLES,
    UI_RENDERER_GLES_ANGLE,
    UI_RENDERER_SDL_HW,
    UI_RENDERER_SDL_SW,

    UI_RENDERER_COUNT,
};

extern int is_renderer_ogl_dbg;

extern enum ui_renderer_t ui_renderer;
extern SDL_Window *ui_sdl_win[SCREEN_COUNT];
extern struct nk_context *ui_nk_ctx;
typedef enum {
    VIEW_MODE_TOP_BOT,
    VIEW_MODE_SEPARATE,
    VIEW_MODE_TOP,
    VIEW_MODE_BOT,
} view_mode_t;
extern view_mode_t ui_view_mode;
extern bool ui_fullscreen;

#ifdef _WIN32
#include <windows.h>
extern HWND ui_hwnd[SCREEN_COUNT];
extern HDC ui_hdc[SCREEN_COUNT];
extern LONG_PTR ui_sdl_wnd_proc[SCREEN_COUNT];
#endif
extern Uint32 ui_sdl_win_id[SCREEN_COUNT];

extern int ui_nk_width, ui_nk_height;
extern float ui_nk_scale;

extern int ui_win_width[SCREEN_COUNT], ui_win_height[SCREEN_COUNT];
extern int ui_win_width_drawable[SCREEN_COUNT], ui_win_height_drawable[SCREEN_COUNT];
extern float ui_win_scale[SCREEN_COUNT];

extern int ui_ctx_width[SCREEN_COUNT], ui_ctx_height[SCREEN_COUNT];

UNUSED static bool is_renderer_sdl_renderer(void) {
    return ui_renderer >= UI_RENDERER_SDL_HW && ui_renderer <= UI_RENDERER_SDL_SW;
}

UNUSED static bool is_renderer_csc(void) {
    return ui_renderer == UI_RENDERER_D3D11_CSC || ui_renderer == UI_RENDERER_OGL_CSC || ui_renderer == UI_RENDERER_GLES_CSC;
}

UNUSED static bool is_renderer_d3d11(void) {
    return ui_renderer >= UI_RENDERER_D3D11_CSC && ui_renderer <= UI_RENDERER_D3D11;
}

UNUSED static bool is_renderer_sdl_ogl(void) {
    return ui_renderer >= UI_RENDERER_OGL_CSC && ui_renderer <= UI_RENDERER_GLES_ANGLE;
}

UNUSED static bool is_renderer_ogl(void) {
    return ui_renderer >= UI_RENDERER_OGL_CSC && ui_renderer <= UI_RENDERER_OGL;
}

UNUSED static bool is_renderer_gles(void) {
    return ui_renderer >= UI_RENDERER_GLES_CSC && ui_renderer <= UI_RENDERER_GLES_ANGLE;
}

UNUSED static bool is_renderer_gles_angle(void) {
    return ui_renderer == UI_RENDERER_GLES_ANGLE;
}

UNUSED static bool is_renderer_sdl_hw(void) {
    return ui_renderer == UI_RENDERER_SDL_HW;
}

int ui_common_sdl_init(void);
void ui_common_sdl_destroy(void);
int sdl_win_init(SDL_Window *sdl_win[SCREEN_COUNT], bool ogl);
void sdl_win_destroy(SDL_Window *sdl_win[SCREEN_COUNT]);
void sdl_set_wminfo(void);
void sdl_reset_wminfo(void);

extern event_t update_bottom_screen_evt;
void ui_view_mode_update(view_mode_t view_mode);
void ui_window_size_update(int window_top_bot);
void ui_windows_titles_update(void);

#include "ntr_rp.h"
struct rp_buffer_ctx_t;
int draw_screen(struct rp_buffer_ctx_t *ctx, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, bool win_shared);

void draw_screen_get_dims_lite(
    int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, int width, int height,
    int *out_ctx_left,
    int *out_ctx_top,
    int *out_ctx_width,
    int *out_ctx_height
);

void draw_screen_get_dims(
    int screen_top_bot, int ctx_top_bot, int win_shared, view_mode_t view_mode, int width, int height,
    double *out_ctx_left_f,
    double *out_ctx_top_f,
    double *out_ctx_right_f,
    double *out_ctx_bot_f,
    int *out_ctx_width,
    int *out_ctx_height,
    int *out_win_width,
    int *out_win_height
);

typedef struct {
    unsigned char *image;
    int width, height, channels;
} stbi_t;
void generate_cursor_image(stbi_t *image, const unsigned char *base, int width, int height, int channels, float scale);

#endif
