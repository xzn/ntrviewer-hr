#define NK_IMPLEMENTATION
#include "main.h"

char *nk_itoa_impl(char *s, long n) {
    return nk_itoa(s, n);
}

int NK_PROPERTY_DEFAULT_IMPL = NK_PROPERTY_DEFAULT;
int NK_PROPERTY_EDIT_IMPL = NK_PROPERTY_EDIT;
