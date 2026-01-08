#ifndef UI_RENDERER_SDL_H
#define UI_RENDERER_SDL_H

extern SDL_Renderer *sdl_renderer[SCREEN_COUNT];

int ui_renderer_sdl_init(void);
void ui_renderer_sdl_destroy(void);

#include "ui_common_sdl.h"
void ui_renderer_sdl_main(int ctx_top_bot, view_mode_t view_mode, float bg[4]);
void ui_renderer_sdl_draw(uint8_t *data, uint8_t *data_prev, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode);
void ui_renderer_sdl_present(int ctx_top_bot);
void ui_renderer_sdl_gen_cursor(stbi_t *image, const unsigned char *base, int width, int height, int channels, float scale);

#endif
