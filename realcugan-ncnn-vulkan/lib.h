#include "glad/glad.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REALCUGAN_SCALE (2)
extern int realcugan_create();
extern GLuint realcugan_run(int tb, int index, int w, int h, int c, const unsigned char *indata, unsigned char *outdata, GLuint *gl_sem, GLuint *gl_sem_next, bool *dim3, bool *success);
extern void realcugan_next(int tb, int index);
extern void realcugan_destroy();

#ifdef __cplusplus
}
#endif
