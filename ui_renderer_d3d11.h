#ifndef UI_RENDERER_D3D11_H
#define UI_RENDERER_D3D11_H

#ifdef _WIN32
int ui_renderer_d3d11_init(void);
void ui_renderer_d3d11_destroy(void);

#include "ui_common_sdl.h"
void ui_renderer_d3d11_main(int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, bool win_shared, float bg[4]);
void ui_renderer_d3d11_draw(struct rp_buffer_ctx_t *ctx, uint8_t *data, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, int win_shared);
void ui_renderer_d3d11_present(int screen_top_bot, int ctx_top_bot, bool win_shared);
void ui_renderer_d3d11_gen_cursor(stbi_t *image, const unsigned char *base, int width, int height, int channels, float scale);
#else
#include "const.h"
UNUSED static int ui_renderer_d3d11_init(void) { return -1; }
UNUSED static void ui_renderer_d3d11_destroy(void) {}

#include "ui_common_sdl.h"
UNUSED static void ui_renderer_d3d11_main(int, int, view_mode_t, bool, float[4]) {}
UNUSED static void ui_renderer_d3d11_draw(struct rp_buffer_ctx_t *, uint8_t *, int, int, int, int, view_mode_t, int) {}
UNUSED static void ui_renderer_d3d11_present(int, int, bool) {}
UNUSED static void ui_renderer_d3d11_gen_cursor(stbi_t *, const unsigned char *, int, int, int, float) {}
#endif

int d3d11_ui_init(void);
void d3d11_ui_close(void);

#endif
