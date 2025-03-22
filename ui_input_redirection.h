#ifndef UI_INPUT_REDIRECTION_H
#define UI_INPUT_REDIRECTION_H

#include "ntr_rp.h"

extern const input_redirection_frame_t input_redirection_frame_default;
extern input_redirection_frame_t input_redirection_frame;
extern rp_lock_t sdl_cursors_lock;
extern int sdl_bottom_screen_cursor_size;

void sdl_update_bottom_screen_cursor(void);
void generate_cursors_images(void);

extern bool need_generate_cursors_images;
void do_generate_cursors_images(view_mode_t vm);
bool sdl_process_bottom_screen_event(SDL_Event *evt);
Uint32 SDLCALL input_redirection_timer_cb(Uint32 interval, void *);

extern int ui_num_controllers;
extern int ui_controller_selected;
extern const char **ui_controllers_names;
extern int *ui_controllers_ids;
extern nk_bool ui_controller_swap_face_buttons;
void ui_update_game_controllers(void);

#endif
