#include "ui_input_redirection.h"
#include "ui_main_nk.h"
#include "main.h"

enum CLARITY_ICON {
    CLARITY_ICON_CROSSHAIRS,
    CLARITY_ICON_ARROW,
    CLARITY_ICON_HAND,
    CLARITY_ICON_HAND_CLICK,
    CLARITY_ICON_HAND_OPEN,
    CLARITY_ICON_HAND_GRAB,
    CLARITY_ICON_COUNT,
};

enum CLARITY_ICON_SIZE {
    CLARITY_ICON_SIZE_SMALL,
    CLARITY_ICON_SIZE_MEDIUM,
    CLARITY_ICON_SIZE_LARGE,
    CLARITY_ICON_SIZE_XLARGE,
    CLARITY_ICON_SIZE_COUNT,
};

#define STBI_ONLY_PNG
#include "stb_image.h"

SDL_PixelFormat *sdl_cursor_pixel_format;
typedef struct {
    SDL_Cursor *cursor;
    SDL_Surface *surface;
} sdl_cursor_t;
static sdl_cursor_t sdl_cursors_curr[CLARITY_ICON_SIZE_COUNT][CLARITY_ICON_COUNT];
static sdl_cursor_t sdl_cursors_next[CLARITY_ICON_SIZE_COUNT][CLARITY_ICON_COUNT];
static bool sdl_cursors_updated;
rp_lock_t sdl_cursors_lock;

#include "clarity/18px/crosshairs-outline.h"
#include "clarity/18px/cursor-arrow-outline.h"
#include "clarity/18px/cursor-hand-outline.h"
#include "clarity/18px/cursor-hand-click-outline.h"
#include "clarity/18px/cursor-hand-open-outline.h"
#include "clarity/18px/cursor-hand-grab-outline.h"

#include "clarity/24px/crosshairs-outline.h"
#include "clarity/24px/cursor-arrow-outline.h"
#include "clarity/24px/cursor-hand-outline.h"
#include "clarity/24px/cursor-hand-click-outline.h"
#include "clarity/24px/cursor-hand-open-outline.h"
#include "clarity/24px/cursor-hand-grab-outline.h"

#include "clarity/27px/crosshairs-outline.h"
#include "clarity/27px/cursor-arrow-outline.h"
#include "clarity/27px/cursor-hand-outline.h"
#include "clarity/27px/cursor-hand-click-outline.h"
#include "clarity/27px/cursor-hand-open-outline.h"
#include "clarity/27px/cursor-hand-grab-outline.h"

#include "clarity/36px/crosshairs-outline.h"
#include "clarity/36px/cursor-arrow-outline.h"
#include "clarity/36px/cursor-hand-outline.h"
#include "clarity/36px/cursor-hand-click-outline.h"
#include "clarity/36px/cursor-hand-open-outline.h"
#include "clarity/36px/cursor-hand-grab-outline.h"

const unsigned char *clarity_icon_get_data(enum CLARITY_ICON icon, enum CLARITY_ICON_SIZE size) {
    switch (size) {
        case CLARITY_ICON_SIZE_SMALL:
            switch (icon) {
                case CLARITY_ICON_CROSSHAIRS:
                    return clarity_18px_crosshairs_outline_png;

                case CLARITY_ICON_ARROW:
                    return clarity_18px_cursor_arrow_outline_png;

                case CLARITY_ICON_HAND:
                    return clarity_18px_cursor_hand_outline_png;

                case CLARITY_ICON_HAND_CLICK:
                    return clarity_18px_cursor_hand_click_outline_png;

                case CLARITY_ICON_HAND_OPEN:
                    return clarity_18px_cursor_hand_open_outline_png;

                case CLARITY_ICON_HAND_GRAB:
                    return clarity_18px_cursor_hand_grab_outline_png;

                default:
                    return NULL;
            };

        case CLARITY_ICON_SIZE_MEDIUM:
            switch (icon) {
                case CLARITY_ICON_CROSSHAIRS:
                    return clarity_24px_crosshairs_outline_png;

                case CLARITY_ICON_ARROW:
                    return clarity_24px_cursor_arrow_outline_png;

                case CLARITY_ICON_HAND:
                    return clarity_24px_cursor_hand_outline_png;

                case CLARITY_ICON_HAND_CLICK:
                    return clarity_24px_cursor_hand_click_outline_png;

                case CLARITY_ICON_HAND_OPEN:
                    return clarity_24px_cursor_hand_open_outline_png;

                case CLARITY_ICON_HAND_GRAB:
                    return clarity_24px_cursor_hand_grab_outline_png;

                default:
                    return NULL;
            };

        case CLARITY_ICON_SIZE_LARGE:
            switch (icon) {
                case CLARITY_ICON_CROSSHAIRS:
                    return clarity_27px_crosshairs_outline_png;

                case CLARITY_ICON_ARROW:
                    return clarity_27px_cursor_arrow_outline_png;

                case CLARITY_ICON_HAND:
                    return clarity_27px_cursor_hand_outline_png;

                case CLARITY_ICON_HAND_CLICK:
                    return clarity_27px_cursor_hand_click_outline_png;

                case CLARITY_ICON_HAND_OPEN:
                    return clarity_27px_cursor_hand_open_outline_png;

                case CLARITY_ICON_HAND_GRAB:
                    return clarity_27px_cursor_hand_grab_outline_png;

                default:
                    return NULL;
            };

        case CLARITY_ICON_SIZE_XLARGE:
            switch (icon) {
                case CLARITY_ICON_CROSSHAIRS:
                    return clarity_36px_crosshairs_outline_png;

                case CLARITY_ICON_ARROW:
                    return clarity_36px_cursor_arrow_outline_png;

                case CLARITY_ICON_HAND:
                    return clarity_36px_cursor_hand_outline_png;

                case CLARITY_ICON_HAND_CLICK:
                    return clarity_36px_cursor_hand_click_outline_png;

                case CLARITY_ICON_HAND_OPEN:
                    return clarity_36px_cursor_hand_open_outline_png;

                case CLARITY_ICON_HAND_GRAB:
                    return clarity_36px_cursor_hand_grab_outline_png;

                default:
                    return 0;
            };

        default:
            return NULL;
    };
}

unsigned int clarity_icon_get_data_len(enum CLARITY_ICON icon, enum CLARITY_ICON_SIZE size) {
    switch (size) {
        case CLARITY_ICON_SIZE_SMALL:
            switch (icon) {
                case CLARITY_ICON_CROSSHAIRS:
                    return clarity_18px_crosshairs_outline_png_len;

                case CLARITY_ICON_ARROW:
                    return clarity_18px_cursor_arrow_outline_png_len;

                case CLARITY_ICON_HAND:
                    return clarity_18px_cursor_hand_outline_png_len;

                case CLARITY_ICON_HAND_CLICK:
                    return clarity_18px_cursor_hand_click_outline_png_len;

                case CLARITY_ICON_HAND_OPEN:
                    return clarity_18px_cursor_hand_open_outline_png_len;

                case CLARITY_ICON_HAND_GRAB:
                    return clarity_18px_cursor_hand_grab_outline_png_len;

                default:
                    return 0;
            };

        case CLARITY_ICON_SIZE_MEDIUM:
            switch (icon) {
                case CLARITY_ICON_CROSSHAIRS:
                    return clarity_24px_crosshairs_outline_png_len;

                case CLARITY_ICON_ARROW:
                    return clarity_24px_cursor_arrow_outline_png_len;

                case CLARITY_ICON_HAND:
                    return clarity_24px_cursor_hand_outline_png_len;

                case CLARITY_ICON_HAND_CLICK:
                    return clarity_24px_cursor_hand_click_outline_png_len;

                case CLARITY_ICON_HAND_OPEN:
                    return clarity_24px_cursor_hand_open_outline_png_len;

                case CLARITY_ICON_HAND_GRAB:
                    return clarity_24px_cursor_hand_grab_outline_png_len;

                default:
                    return 0;
            };

        case CLARITY_ICON_SIZE_LARGE:
            switch (icon) {
                case CLARITY_ICON_CROSSHAIRS:
                    return clarity_27px_crosshairs_outline_png_len;

                case CLARITY_ICON_ARROW:
                    return clarity_27px_cursor_arrow_outline_png_len;

                case CLARITY_ICON_HAND:
                    return clarity_27px_cursor_hand_outline_png_len;

                case CLARITY_ICON_HAND_CLICK:
                    return clarity_27px_cursor_hand_click_outline_png_len;

                case CLARITY_ICON_HAND_OPEN:
                    return clarity_27px_cursor_hand_open_outline_png_len;

                case CLARITY_ICON_HAND_GRAB:
                    return clarity_27px_cursor_hand_grab_outline_png_len;

                default:
                    return 0;
            };

        case CLARITY_ICON_SIZE_XLARGE:
            switch (icon) {
                case CLARITY_ICON_CROSSHAIRS:
                    return clarity_36px_crosshairs_outline_png_len;

                case CLARITY_ICON_ARROW:
                    return clarity_36px_cursor_arrow_outline_png_len;

                case CLARITY_ICON_HAND:
                    return clarity_36px_cursor_hand_outline_png_len;

                case CLARITY_ICON_HAND_CLICK:
                    return clarity_36px_cursor_hand_click_outline_png_len;

                case CLARITY_ICON_HAND_OPEN:
                    return clarity_36px_cursor_hand_open_outline_png_len;

                case CLARITY_ICON_HAND_GRAB:
                    return clarity_36px_cursor_hand_grab_outline_png_len;

                default:
                    return 0;
            };

        default:
            return 0;
    };
}

static int sdl_get_bottom_screen_ctx(view_mode_t vm) {
    return (vm == VIEW_MODE_TOP_BOT || vm == VIEW_MODE_BOT) ? SCREEN_TOP : vm == VIEW_MODE_SEPARATE ? SCREEN_BOT : -1;
}

static void generate_clarity_sdl_cursor(sdl_cursor_t *cursor, stbi_t *image, enum CLARITY_ICON icon) {
    if (!sdl_cursor_pixel_format) {
        sdl_cursor_pixel_format = SDL_AllocFormat(SDL_FORMAT);
        if (!sdl_cursor_pixel_format)
            return;
    }

    if (cursor->cursor) {
        SDL_FreeCursor(cursor->cursor);
        cursor->cursor = NULL;
    }

    if (cursor->surface) {
        SDL_FreeSurface(cursor->surface);
        cursor->surface = NULL;
    }

    if (!image->image) {
        return;
    }
    cursor->surface = SDL_CreateRGBSurfaceWithFormatFrom(image->image, image->width, image->height, 1, image->width * image->channels, SDL_FORMAT);
    if (!cursor->surface) {
        return;
    }

    int hot_x = 15;
    int hot_y = 7;
    if (icon == CLARITY_ICON_ARROW) {
        hot_x = hot_y = 5;
    } else if (icon == CLARITY_ICON_CROSSHAIRS) {
        hot_x = hot_y = 17;
    }
    hot_x = hot_x * image->width / 36;
    hot_y = hot_y * image->height / 36;
    cursor->cursor = SDL_CreateColorCursor(cursor->surface, hot_x, hot_y);
}

static float cursor_scale_prev;
void do_generate_cursors_images(view_mode_t vm) {
    int i = sdl_get_bottom_screen_ctx(vm);
    if (i < 0) {
        return;
    }

    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;

    draw_screen_get_dims_lite(SCREEN_BOT, i, vm, SCREEN_HEIGHT1, SCREEN_WIDTH, &ctx_left, &ctx_top, &ctx_width, &ctx_height);
    float scale_x = (float)ctx_width / SCREEN_HEIGHT1;
    float scale_y = (float)ctx_height / SCREEN_WIDTH;
    float scale = (scale_x + scale_y) / 2;
    if (scale == cursor_scale_prev) {
        return;
    }

    rp_lock_wait(sdl_cursors_lock);
    for (int icon = 0; icon < CLARITY_ICON_COUNT; ++icon) {
        for (int size = 0; size < CLARITY_ICON_SIZE_COUNT; ++size) {
            int width, height, channels;
            stbi_uc *image = stbi_load_from_memory(clarity_icon_get_data(icon, size), clarity_icon_get_data_len(icon, size), &width, &height, &channels, GL_CHANNELS_N);
            if (image) {
                stbi_t im = {};
                generate_cursor_image(&im, image, width, height, GL_CHANNELS_N, scale);
                if (im.image) {
                    generate_clarity_sdl_cursor(&sdl_cursors_next[size][icon], &im, icon);
                    free(im.image);
                }
                stbi_image_free(image);
            }
        }
    }
    cursor_scale_prev = scale;
    sdl_cursors_updated = 1;
    rp_lock_rel(sdl_cursors_lock);
}

bool need_generate_cursors_images;
static void set_clarity_sdl_cursor(enum CLARITY_ICON icon, enum CLARITY_ICON_SIZE size) {
    rp_lock_wait(sdl_cursors_lock);
    if (sdl_cursors_updated) {
        for (int icon = 0; icon < CLARITY_ICON_COUNT; ++icon) {
            for (int size = 0; size < CLARITY_ICON_SIZE_COUNT; ++size) {
                sdl_cursor_t *cursor = &sdl_cursors_curr[size][icon];
                if (cursor->cursor) {
                    SDL_FreeCursor(cursor->cursor);
                    cursor->cursor = NULL;
                }

                if (cursor->surface) {
                    SDL_FreeSurface(cursor->surface);
                    cursor->surface = NULL;
                }
            }
        }
        memcpy(sdl_cursors_curr, sdl_cursors_next, sizeof(sdl_cursors_curr));
        memset(sdl_cursors_next, 0, sizeof(sdl_cursors_next));
        sdl_cursors_updated = 0;
    }
    SDL_SetCursor(sdl_cursors_curr[size][icon].cursor);
    rp_lock_rel(sdl_cursors_lock);
}

void generate_cursors_images() {
    need_generate_cursors_images = 1;
}

#define TOUCH_SCREEN_COORD_RANGE (0xfff)
static int sdl_get_bottom_screen_mouse_coord(view_mode_t vm, Uint32 wid, Sint32 x, Sint32 y, SDL_Point *point) {
    int i = sdl_get_bottom_screen_ctx(vm);
    if (i < 0) {
        return 1;
    }

    if (i == SCREEN_TOP && !ui_hide_nk_windows) {
        return 1;
    }

    if (wid != ui_sdl_win_id[i]) {
        return 1;
    }

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        if (wid == ui_sdl_win_id[i]) {
            if (x < 0 || y < 0 || x >= ui_win_width[i] || y >= ui_win_height[i]) {
                return -1;
            }
        }
    }

    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;

    draw_screen_get_dims_lite(SCREEN_BOT, i, vm, SCREEN_HEIGHT1, SCREEN_WIDTH, &ctx_left, &ctx_top, &ctx_width, &ctx_height);
    if (x < ctx_left || x >= ctx_left + ctx_width || y < ctx_top || y >= ctx_top + ctx_height) {
        return 1;
    }

    *point = (SDL_Point){
        .x = TOUCH_SCREEN_COORD_RANGE * (x - ctx_left) * SCREEN_HEIGHT1 / ctx_width,
        .y = TOUCH_SCREEN_COORD_RANGE * (y - ctx_top) * SCREEN_WIDTH / ctx_height,
    };
    return 0;
}

static bool sdl_bottom_screen_grabbing;
static enum CLARITY_ICON_SIZE sdl_bottom_screen_cursor_size = CLARITY_ICON_SIZE_MEDIUM;

static void sdl_set_bottom_screen_cursor(bool reset) {
    if (reset) {
        SDL_SetCursor(SDL_GetDefaultCursor());
    } else {
        set_clarity_sdl_cursor(CLARITY_ICON_HAND, sdl_bottom_screen_cursor_size);
    }
}

void sdl_update_bottom_screen_cursor(void) {
    int x, y;
    UNUSED Uint32 state = SDL_GetMouseState(&x, &y);
    SDL_Point point;
    int reset = sdl_get_bottom_screen_mouse_coord(__atomic_load_n(&ui_view_mode, __ATOMIC_RELAXED), ui_sdl_win_id[SCREEN_TOP], x, y, &point);
    if (reset >= 0)
        sdl_set_bottom_screen_cursor(reset);
}

const input_redirection_frame_t input_redirection_frame_default = {
    .hidPad = 0xfff,
    .touchScreenState = 0x2000000,
    .circlePadState = 0x7ff7ff,
    .cppState = 0x80800081,
    .interfaceButtons = 0,
};

input_redirection_frame_t input_redirection_frame = input_redirection_frame_default;
static void sdl_set_touch_screen_coord(SDL_Point *point) {
    uint32_t x = MIN(MAX(0, point->x), TOUCH_SCREEN_COORD_RANGE * SCREEN_HEIGHT1) / SCREEN_HEIGHT1;
    uint32_t y = MIN(MAX(0, point->y), TOUCH_SCREEN_COORD_RANGE * SCREEN_WIDTH) / SCREEN_WIDTH;
    input_redirection_frame.touchScreenState = (1 << 24) | (y << 12) | x;
}

bool sdl_process_bottom_screen_event(SDL_Event *evt) {
    view_mode_t vm = __atomic_load_n(&ui_view_mode, __ATOMIC_RELAXED);
    switch (evt->type) {
        case SDL_MOUSEMOTION: {
            SDL_Point point;
            int reset = sdl_get_bottom_screen_mouse_coord(vm, evt->motion.windowID, evt->motion.x, evt->motion.y, &point);

            if (sdl_bottom_screen_grabbing) {
                if (!reset)
                    sdl_set_touch_screen_coord(&point);

                set_clarity_sdl_cursor(CLARITY_ICON_HAND_GRAB, sdl_bottom_screen_cursor_size);
                return true;
            } else {
                if (reset >= 0)
                    sdl_set_bottom_screen_cursor(reset);
            }
        }
        break;

        case SDL_MOUSEBUTTONDOWN: {
            if (evt->button.button != SDL_BUTTON_LEFT) {
                break;
            }
            SDL_Point point;
            int reset = sdl_get_bottom_screen_mouse_coord(vm, evt->button.windowID, evt->button.x, evt->button.y, &point);
            if (!reset) {
                sdl_bottom_screen_grabbing = true;
                sdl_set_touch_screen_coord(&point);

                set_clarity_sdl_cursor(CLARITY_ICON_HAND_CLICK, sdl_bottom_screen_cursor_size);
                return true;
            }
        }
        break;

        case SDL_MOUSEBUTTONUP: {
            if (evt->button.button != SDL_BUTTON_LEFT) {
                ui_set_hide_nk_windows(!ui_hide_nk_windows);
                sdl_update_bottom_screen_cursor();
                break;
            }
            if (sdl_bottom_screen_grabbing) {
                sdl_bottom_screen_grabbing = false;
                input_redirection_frame.touchScreenState = 0x2000000;

                SDL_Point point;
                int reset = sdl_get_bottom_screen_mouse_coord(vm, evt->button.windowID, evt->button.x, evt->button.y, &point);
                if (reset >= 0) {
                    sdl_set_bottom_screen_cursor(reset);
                    return true;
                }
            }
        }
        break;

        default:
            break;
    }
    return false;
}

static input_redirection_frame_t input_redirection_frame_send;
Uint32 SDLCALL input_redirection_timer_cb(Uint32 interval, void *) {
    if (!program_running)
        return 0;

    input_redirection_frame_t frame = {
        .hidPad = input_redirection_frame.hidPad,
        .touchScreenState = __atomic_load_n(&input_redirection_frame.touchScreenState, __ATOMIC_RELAXED),
        .circlePadState = input_redirection_frame.circlePadState,
        .cppState = input_redirection_frame.cppState,
        .interfaceButtons = input_redirection_frame.interfaceButtons,
    };
    if (memcmp(&input_redirection_frame_send, &frame, sizeof(input_redirection_frame_t)) != 0) {
        input_redirection_frame_send = frame;
        input_redirection_send_frame(&frame);
    }
    return interval;
}

int ui_num_controllers;
int ui_controller_selected;
const char **ui_controllers_names;
int *ui_controllers_ids;
nk_bool ui_controller_swap_face_buttons;

void ui_update_game_controllers(void) {
    ui_num_controllers = 0;
    if (ui_controllers_names) {
        free(ui_controllers_names);
        ui_controllers_names = 0;
    }
    if (ui_controllers_ids) {
        free(ui_controllers_ids);
        ui_controllers_ids = 0;
    }

    int n = SDL_NumJoysticks();
    int ui_n = 1 + n + 1;
    ui_controllers_names = malloc(sizeof(const char *) * ui_n);
    ui_controllers_ids = malloc(sizeof(int) * ui_n);

    int nn = 0;
    ui_controllers_names[nn] = "";
    ui_controllers_ids[nn] = -1;
    ++nn;
    for (int i = 0; i < n; ++i) {
        if (SDL_IsGameController(i)) {
            ui_controllers_names[nn] = SDL_GameControllerNameForIndex(i);
            if (!ui_controllers_names[nn]) {
                ui_controllers_names[nn] = "(Unknown)";
            }
            ui_controllers_ids[nn] = i;
            ++nn;
        }
    }
    ui_controllers_names[nn] = "Refresh List";
    ui_controllers_ids[nn] = -1;
    ui_num_controllers = ++nn;

    ui_controller_selected = 0;
}
