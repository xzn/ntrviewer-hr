#ifndef UI_MAIN_NK_H
#define UI_MAIN_NK_H

#include "const.h"
#include "rp_syn.h"

enum nk_nav_t {
    NK_NAV_NONE,
    NK_NAV_NEXT,
    NK_NAV_PREVIOUS,
    NK_NAV_CONFIRM,
    NK_NAV_CANCEL,
};
extern enum nk_nav_t nk_nav_cmd;
extern rp_lock_t ui_nk_lock;

#include <stdatomic.h>

extern atomic_bool ui_hide_nk_windows;
void ui_set_hide_nk_windows(bool hide);
extern bool ui_upscaling_filters;

extern int ui_upscaling_selected;
extern const char **ui_upscaling_filter_options;
extern int ui_upscaling_filter_count;

void ui_main_nk(void);
void nk_backend_font_init(void);

struct nk_font_atlas;
void nk_font_stash_begin(struct nk_font_atlas **atlas);
void nk_font_stash_end(void);

#define NK_UPSCALE_TYPE_TEXT_NONE "  "
#define NK_UPSCALE_TYPE_TEXT_PLACEBO "[color=\"placebo\"]P[/color] "
#define NK_UPSCALE_TYPE_TEXT_RASHADER "[color=\"rashader\"]R[/color] "

#endif
