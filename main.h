/* nuklear - 1.40.8 - public domain */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <time.h>
#include <setjmp.h>

#include "fsr/fsr_main.h"

#if defined(_WIN32) && !defined(_WIN32_WCE) && !defined(__SCITECH_SNAP__)
    /* Win32 but not WinCE */
#   define KHRONOS_APIENTRY __stdcall
#else
#   define KHRONOS_APIENTRY
#endif

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_BOOL
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include "nuklear.h"
#ifdef USE_SDL_RENDERER
#define NK_SDL_RENDERER_SDL_H <SDL2/SDL.h>
#include "nuklear_sdl_renderer.h"
#else
#if defined(_WIN32) && defined(USE_D3D11)
#include <SDL2/SDL.h>
#include "nuklear_d3d11.h"
#include <d3d11.h>
#elif defined(USE_OGL_ES)
#include "nuklear_sdl_gles2.h"
#else
#include "nuklear_sdl_gl3.h"
#endif
#endif

extern int NK_PROPERTY_DEFAULT_IMPL;
extern int NK_PROPERTY_EDIT_IMPL;
char *nk_itoa_impl(char *s, long n);

#ifdef _WIN32
#include <iphlpapi.h>
#endif
