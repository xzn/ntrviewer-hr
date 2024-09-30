#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#define NK_D3D11_IMPLEMENTATION
#include "main.h"

char *nk_itoa_impl(char *s, long n) {
    return nk_itoa(s, n);
}

int NK_PROPERTY_DEFAULT_IMPL = NK_PROPERTY_DEFAULT;
int NK_PROPERTY_EDIT_IMPL = NK_PROPERTY_EDIT;
