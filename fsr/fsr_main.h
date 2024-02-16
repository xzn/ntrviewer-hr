#include <glad/glad.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  SCREEN_TOP,
  SCREEN_BOT,
  SCREEN_COUNT,
};

GLuint fsr_main(int top_bot, GLuint inputTexture, uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h, float rcasAtt);

#ifdef __cplusplus
}
#endif
