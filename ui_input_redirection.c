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

static const SDL_PixelFormatDetails *sdl_cursor_pixel_format;
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
        sdl_cursor_pixel_format = SDL_GetPixelFormatDetails(SDL_FORMAT);
        if (!sdl_cursor_pixel_format)
            return;
    }

    if (cursor->cursor) {
        SDL_DestroyCursor(cursor->cursor);
        cursor->cursor = NULL;
    }

    if (cursor->surface) {
        SDL_DestroySurface(cursor->surface);
        cursor->surface = NULL;
    }

    if (!image->image) {
        return;
    }
    cursor->surface = SDL_CreateSurfaceFrom(image->width, image->height, SDL_FORMAT, image->image, image->width * image->channels);
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

float cursor_scale_prev;
void generate_cursors_images(view_mode_t vm) {
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
    scale = MAX(scale, 1.0);
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

static void set_clarity_sdl_cursor(enum CLARITY_ICON icon, enum CLARITY_ICON_SIZE size) {
    rp_lock_wait(sdl_cursors_lock);
    if (sdl_cursors_updated) {
        for (int icon = 0; icon < CLARITY_ICON_COUNT; ++icon) {
            for (int size = 0; size < CLARITY_ICON_SIZE_COUNT; ++size) {
                sdl_cursor_t *cursor = &sdl_cursors_curr[size][icon];
                if (cursor->cursor) {
                    SDL_DestroyCursor(cursor->cursor);
                    cursor->cursor = NULL;
                }

                if (cursor->surface) {
                    SDL_DestroySurface(cursor->surface);
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

static bool sdl_bottom_screen_grabbing;

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
                if (sdl_bottom_screen_grabbing) {
                    x = MIN(MAX(x, 0), ui_win_width[i] - 1);
                    y = MIN(MAX(y, 0), ui_win_height[i] - 1);
                    break;
                } else {
                    return -1;
                }
            }
        }
    }

    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;

    draw_screen_get_dims_lite(SCREEN_BOT, i, vm, SCREEN_HEIGHT1, SCREEN_WIDTH, &ctx_left, &ctx_top, &ctx_width, &ctx_height);
    if (x < ctx_left || x >= ctx_left + ctx_width || y < ctx_top || y >= ctx_top + ctx_height) {
        if (sdl_bottom_screen_grabbing) {
            x = MIN(MAX(x, ctx_left), ctx_left + ctx_width - 1);
            y = MIN(MAX(y, ctx_top), ctx_top + ctx_height - 1);
        } else {
            return 1;
        }
    }

    *point = (SDL_Point){
        .x = TOUCH_SCREEN_COORD_RANGE * (x - ctx_left) * SCREEN_HEIGHT1 / ctx_width + SCREEN_HEIGHT1 / 2,
        .y = TOUCH_SCREEN_COORD_RANGE * (y - ctx_top) * SCREEN_WIDTH / ctx_height + SCREEN_WIDTH / 2,
    };
    return 0;
}

int sdl_bottom_screen_cursor_size = CLARITY_ICON_SIZE_MEDIUM;

static void sdl_set_bottom_screen_cursor(bool reset) {
    if (reset) {
        SDL_SetCursor(SDL_GetDefaultCursor());
    } else {
        set_clarity_sdl_cursor(CLARITY_ICON_HAND, sdl_bottom_screen_cursor_size);
    }
}

void update_bottom_screen_cursor(void) {
    if (sdl_bottom_screen_grabbing)
        return;

    float x, y;
    UNUSED Uint32 state = SDL_GetMouseState(&x, &y);
    SDL_Point point;
    int i;
    for (i = 0; i < SCREEN_COUNT; ++i) {
        if (SDL_GetWindowFlags(ui_sdl_win[i]) & SDL_WINDOW_MOUSE_FOCUS) {
            break;
        }
    }
    if (i == SCREEN_COUNT && !sdl_bottom_screen_grabbing) {
        sdl_set_bottom_screen_cursor(true);
    }
    int reset = sdl_get_bottom_screen_mouse_coord(__atomic_load_n(&ui_view_mode, __ATOMIC_RELAXED), ui_sdl_win_id[i], x, y, &point);
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
        case SDL_EVENT_MOUSE_MOTION: {
            SDL_Point point;
            int reset = sdl_get_bottom_screen_mouse_coord(vm, evt->motion.windowID, evt->motion.x, evt->motion.y, &point);

            if (sdl_bottom_screen_grabbing) {
                if (!reset)
                    sdl_set_touch_screen_coord(&point);

                set_clarity_sdl_cursor(CLARITY_ICON_HAND_GRAB, sdl_bottom_screen_cursor_size);
                return true;
            } else {
                update_bottom_screen_cursor();
            }
        }
        break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
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

        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (
                evt->button.button != SDL_BUTTON_LEFT ||
                (!sdl_bottom_screen_grabbing && evt->button.windowID == ui_sdl_win_id[SCREEN_BOT])
            ) {
                ui_set_hide_nk_windows(!ui_hide_nk_windows);
                break;
            }
            if (sdl_bottom_screen_grabbing) {
                sdl_bottom_screen_grabbing = false;
                input_redirection_frame.touchScreenState = 0x2000000;
            }
        }
        break;

        default:
            break;
    }
    return false;
}

rp_lock_t sdl_game_controller_lock;
static SDL_Gamepad *sdl_game_controller;
static int game_controller_selected;
static void close_game_controller(void) {
    if (sdl_game_controller) {
        SDL_CloseGamepad(sdl_game_controller);
        sdl_game_controller = 0;
    }
    game_controller_selected = 0;
}

int ui_num_controllers;
int ui_controller_selected;
const char **ui_controllers_names;
int *ui_controllers_ids;
nk_bool ui_controller_swap_face_buttons = 1;
static bool need_update_game_controllers;

static void do_ui_update_game_controllers(void) {
    rp_lock_wait(ui_nk_lock);

    ui_num_controllers = 0;
    if (ui_controllers_names) {
        free(ui_controllers_names);
        ui_controllers_names = 0;
    }
    if (ui_controllers_ids) {
        free(ui_controllers_ids);
        ui_controllers_ids = 0;
    }

    int n = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&n);
    if (!ids) {
        n = 0;
    }
    int ui_n = 1 + n + 1;
    ui_controllers_names = malloc(sizeof(const char *) * ui_n);
    ui_controllers_ids = malloc(sizeof(int) * ui_n);

    int nn = 0;
    ui_controllers_names[nn] = ""; // Empty none entry (input redirection disabled)
    ui_controllers_ids[nn] = -1;
    ++nn;
    for (int i = 0; i < n; ++i) {
        SDL_JoystickID id = ids[i];
        if (SDL_IsGamepad(id)) {
            ui_controllers_names[nn] = SDL_GetGamepadNameForID(id);
            if (!ui_controllers_names[nn]) {
                ui_controllers_names[nn] = "(Unknown)";
            }
            ui_controllers_ids[nn] = id;
            err_log("%d %d\n", nn, id);
            ++nn;
        }
    }
    SDL_free(ids);
    ui_controllers_names[nn] = "Refresh List";
    ui_controllers_ids[nn] = -1;
    ui_num_controllers = ++nn;

    ui_controller_selected = nn > 1 + 0 + 1;
    close_game_controller();

    rp_lock_rel(ui_nk_lock);
}

void update_game_controller(void) {
    rp_lock_wait(sdl_game_controller_lock);

    if (need_update_game_controllers) {
        do_ui_update_game_controllers();
        need_update_game_controllers = false;
    }

    bool selected = ui_controller_selected >= 1 && ui_controller_selected < ui_num_controllers - 1;
    if (
        ui_controller_selected != game_controller_selected || !selected
    ) {
        close_game_controller();
    }

    if (!sdl_game_controller && selected) {
        sdl_game_controller = SDL_OpenGamepad(ui_controllers_ids[ui_controller_selected]);
        if (sdl_game_controller) {
            game_controller_selected = ui_controller_selected;
        } else {
            do_ui_update_game_controllers();
        }
    }

    rp_lock_rel(sdl_game_controller_lock);
}

#include <math.h>

static input_redirection_frame_t input_redirection_frame_send;
Uint32 SDLCALL input_redirection_timer_cb(void *, SDL_TimerID, Uint32 interval) {
    if (!program_running)
        return 0;

    input_redirection_frame.hidPad = input_redirection_frame_default.hidPad;
    input_redirection_frame.circlePadState = input_redirection_frame_default.circlePadState;
    input_redirection_frame.cppState = input_redirection_frame_default.cppState;
    input_redirection_frame.interfaceButtons = input_redirection_frame_default.interfaceButtons;

    bool swap_face = ui_controller_swap_face_buttons;

    rp_lock_wait(sdl_game_controller_lock);

    if (sdl_game_controller) {
        SDL_ClearError();

        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_GUIDE)) {
            input_redirection_frame.interfaceButtons |= 1;
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_MISC1)) {
            input_redirection_frame.interfaceButtons |= 2;
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_SOUTH)) {
            input_redirection_frame.hidPad &= swap_face ? ~(1 << 1) : ~(1 << 0);
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_EAST)) {
            input_redirection_frame.hidPad &= swap_face ? ~(1 << 0) : ~(1 << 1);
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_WEST)) {
            input_redirection_frame.hidPad &= swap_face ? ~(1 << 11) : ~(1 << 10);
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_NORTH)) {
            input_redirection_frame.hidPad &= swap_face ? ~(1 << 10) : ~(1 << 11);
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_BACK)) {
            input_redirection_frame.hidPad &= ~(1 << 2);
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_START)) {
            input_redirection_frame.hidPad &= ~(1 << 3);
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
            input_redirection_frame.hidPad &= ~(1 << 4);
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
            input_redirection_frame.hidPad &= ~(1 << 5);
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
            input_redirection_frame.hidPad &= ~(1 << 6);
        }
        if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
            input_redirection_frame.hidPad &= ~(1 << 7);
        }
        if (
            SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_RIGHT_STICK) &&
            SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_LEFT_STICK) &&
            SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) &&
            SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
        ) {
            input_redirection_frame.interfaceButtons |= 4;
        } else {
            if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                input_redirection_frame.hidPad &= ~(1 << 8);
            }
            if (SDL_GetGamepadButton(sdl_game_controller, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
                input_redirection_frame.hidPad &= ~(1 << 9);
            }
        }

        const int DEAD_ZONE = (1 << 15) / 10;

        Sint16 left_x = SDL_GetGamepadAxis(sdl_game_controller, SDL_GAMEPAD_AXIS_LEFTX);
        Sint16 left_y = SDL_GetGamepadAxis(sdl_game_controller, SDL_GAMEPAD_AXIS_LEFTY);
        left_x = abs(left_x) >= DEAD_ZONE ? left_x : 0;
        left_y = abs(left_y) >= DEAD_ZONE ? left_y : 0;

        const int axis_range = (1 << 15) - 1;

        if (left_x < -axis_range)
            left_x = -axis_range;
        if (left_y < -axis_range)
            left_y = -axis_range;

        const int CPAD_BOUND = 0x5d0;

        if (left_x != 0 || left_y != 0) {
            left_x = (int32_t)left_x * CPAD_BOUND / axis_range + 0x800;
            left_y = (int32_t)-left_y * CPAD_BOUND / axis_range + 0x800;
            if (left_x > 0xfff) {
                left_x = 0xfff;
            } else if (left_x < 1) {
                left_x = 1;
            }
            if (left_y > 0xfff) {
                left_y = 0xfff;
            } else if (left_y < 1) {
                left_y = 1;
            }
            input_redirection_frame.circlePadState = (left_y << 12) | left_x;
        }

        Sint16 right_x = SDL_GetGamepadAxis(sdl_game_controller, SDL_GAMEPAD_AXIS_RIGHTX);
        Sint16 right_y = SDL_GetGamepadAxis(sdl_game_controller, SDL_GAMEPAD_AXIS_RIGHTY);
        right_x = abs(right_x) >= DEAD_ZONE ? right_x : 0;
        right_y = abs(right_y) >= DEAD_ZONE ? right_y : 0;

        if (right_x < -axis_range)
            right_x = -axis_range;
        if (right_y < -axis_range)
            right_y = -axis_range;

        const int TRIGGER_ZONE = (1 << 4);

        Sint16 trigger_l = SDL_GetGamepadAxis(sdl_game_controller, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        Sint16 trigger_r = SDL_GetGamepadAxis(sdl_game_controller, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        trigger_l = trigger_l >= TRIGGER_ZONE ? 1 : 0;
        trigger_r = trigger_r >= TRIGGER_ZONE ? 1 : 0;

        const int CPP_BOUND = 0x7f;
        // rotate the c-stick position 45 degrees according to TuxSH
        if (right_x != 0 || right_y != 0 || trigger_l || trigger_r) {
            int32_t x = (int32_t)round(M_SQRT1_2 * ((int32_t)right_x - right_y) * CPP_BOUND) / axis_range + 0x80;
            int32_t y = (int32_t)round(M_SQRT1_2 * ((int32_t)-right_y - right_x) * CPP_BOUND) / axis_range + 0x80;
            if (x > 0xff) {
                x = 0xff;
            } else if (x < 1) {
                x = 1;
            }
            if (y > 0xff) {
                y = 0xff;
            } else if (y < 1) {
                y = 1;
            }

            input_redirection_frame.cppState = (y << 24) | (x << 16) | (((trigger_r << 1) | (trigger_l << 2)) << 8) | 0x81;
        }

        if (*SDL_GetError()) {
            close_game_controller();
            ui_controller_selected = 0;
        }
    }

    rp_lock_rel(sdl_game_controller_lock);

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

void ui_update_game_controllers(void) {
    need_update_game_controllers = true;
}
