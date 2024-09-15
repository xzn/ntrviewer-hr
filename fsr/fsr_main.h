#include <glad/glad.h>

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

GLuint fsr_main(int tb, int top_bot, GLuint inputTexture, uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h, float rcasAtt);

#ifdef __cplusplus
}
#endif
