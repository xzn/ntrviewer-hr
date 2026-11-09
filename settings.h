/*
 * settings -- remembers the viewer's settings between launches.
 *
 * Everything the user can change in the Remote Play and Debug windows
 * (view mode, quality, bandwidth, format, the 3DS IP, the Audio / Full Width /
 * Auto-Reconnect toggles, ...) plus the size and position of the windows is
 * written to "ntrviewer_settings.ini" next to the exe. The file is rewritten
 * about a second after something changes and once more on exit. A missing or
 * unreadable file, or any bad line in it, just leaves the built-in default for
 * that setting.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "nuklear/nuklear.h"

#define SETTINGS_FILE_NAME "ntrviewer_settings.ini"
extern int opt_flag_portable;
extern char *opt_arg_settings;

/* What Auto-Reconnect / Auto-Update Params should start as next time. Only
 * the Debug-window checkboxes change these, so clicking Disconnect (which
 * turns Auto-Reconnect off for this session) isn't remembered. */
extern nk_bool settings_auto_reconnect;
extern nk_bool settings_auto_update_params;

/* Read the file and apply everything except the windows and the 3DS IP.
 * Call after ntr_config_set_default() and after the renderer is up. */
void settings_load(void);
/* Restore window sizes and positions (after ui_view_mode_update). */
void settings_apply_windows(void);
/* Restore the 3DS IP (after ntr_detect_3ds_ip). */
void settings_apply_ip(void);
/* Main thread, every loop: saves when something changed (at most once a second). */
void settings_tick(void);
/* Save now if anything changed (on exit). */
void settings_save(void);

#endif
