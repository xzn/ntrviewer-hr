#ifdef USE_D3D11
#include <d3d11.h>
#else
#include "glad/glad.h"
#endif
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REALCUGAN_SCALE (2)
#ifdef USE_D3D11
#else
extern int realcugan_create();
extern GLuint realcugan_run(int tb, int top_bot, int index, int w, int h, int c, const unsigned char *indata, unsigned char *outdata, GLuint *gl_sem, GLuint *gl_sem_next, bool *dim3, bool *success);
extern void realcugan_next(int tb, int top_bot, int index);
extern void realcugan_destroy();
#endif

#ifdef __cplusplus
}
#endif
