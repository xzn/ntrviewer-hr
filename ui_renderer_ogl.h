#ifndef UI_RENDERER_OGL_H
#define UI_RENDERER_OGL_H

extern SDL_Window *ogl_win[SCREEN_COUNT];
extern SDL_GLContext gl_context[SCREEN_COUNT];

int ui_renderer_ogl_init(void);
void ui_renderer_ogl_destroy(void);

#include "ui_common_sdl.h"
void ui_renderer_ogl_main(int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, bool win_shared, float bg[4]);
void ui_renderer_ogl_draw(struct rp_buffer_ctx_t *ctx, uint8_t *data, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, int win_shared);
void ui_renderer_ogl_present(int screen_top_bot, int ctx_top_bot, bool win_shared);

#endif
