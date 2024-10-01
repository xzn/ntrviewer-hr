#ifndef FSR_MAIN_H
#define FSR_MAIN_H

#ifndef USE_D3D11
#include <glad/glad.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
  SCREEN_TOP,
  SCREEN_BOT,
  SCREEN_COUNT,
};

enum FrameBufferIndexInit
{
  FBI_DECODE,
  FBI_READY_DISPLAY,
  FBI_READY_DISPLAY_2,
  FBI_DISPLAY,
  FBI_DISPLAY_2,
  FBI_COUNT,
};

#define FrameBufferCount (FBI_COUNT)

#ifdef USE_D3D11
#else
GLuint fsr_main(int tb, int top_bot, GLuint inputTexture, uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h, float rcasAtt);
#endif

#ifdef __cplusplus
}
#endif

#endif
