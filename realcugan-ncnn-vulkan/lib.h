#ifdef USE_D3D11
#include <d3d11.h>
#else
#include "glad/glad.h"
#endif
#include "stdbool.h"

#include "../fsr/fsr_main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REALCUGAN_SCALE (2)
#ifdef USE_D3D11
extern int realcugan_create(ID3D11Device *device[SCREEN_COUNT], ID3D11DeviceContext *context[SCREEN_COUNT], IDXGIAdapter1 *adapter);
extern ID3D11Resource *realcugan_run(int tb, int top_bot, int index, int w, int h, int c, const unsigned char *indata, unsigned char *outdata, IDXGIKeyedMutex **mutex, ID3D11ShaderResourceView **srv, bool *dim3, bool *success);
#else
extern int realcugan_create();
extern GLuint realcugan_run(int tb, int top_bot, int index, int w, int h, int c, const unsigned char *indata, unsigned char *outdata, GLuint *gl_sem, GLuint *gl_sem_next, bool *dim3, bool *success);
#endif
extern void realcugan_next(int tb, int top_bot, int index);
extern void realcugan_destroy();

#ifdef __cplusplus
}
#endif
