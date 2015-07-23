/*
 Copyright 2013 Michael Pavone
 This file is part of BlastEm.
 BlastEm is free software distributed under the terms of the GNU General Public License version 3 or greater. See COPYING for full license text.
*/
#ifndef RENDER_H_
#define RENDER_H_

//TODO: Throw an ifdef in here once there's more than one renderer
#ifdef __LIBRETRO__
#include "libretro.h"
#define RENDERKEY_UP      RETROK_UP
#define RENDERKEY_DOWN    RETROK_DOWN
#define RENDERKEY_LEFT    RETROK_LEFT
#define RENDERKEY_RIGHT   RETROK_RIGHT
#define RENDERKEY_ESC     RETROK_ESCAPE
#define RENDERKEY_LSHIFT  RETROK_LSHIFT
#define RENDERKEY_RSHIFT  RETROK_RSHIFT
#define RENDER_DPAD_UP    RETRO_DEVICE_ID_JOYPAD_UP
#define RENDER_DPAD_DOWN  RETRO_DEVICE_ID_JOYPAD_DOWN
#define RENDER_DPAD_LEFT  RETRO_DEVICE_ID_JOYPAD_LEFT
#define RENDER_DPAD_RIGHT RETRO_DEVICE_ID_JOYPAD_RIGHT
#define RENDERKEY_UP       RETROK_UP
#define RENDERKEY_DOWN     RETROK_DOWN
#define RENDERKEY_LEFT     RETROK_LEFT
#define RENDERKEY_RIGHT    RETROK_RIGHT
#define RENDERKEY_ESC      RETROK_ESCAPE
#define RENDERKEY_DEL      RETROK_DELETE
#define RENDERKEY_LSHIFT   RETROK_LSHIFT
#define RENDERKEY_RSHIFT   RETROK_RSHIFT
#define RENDERKEY_LCTRL    RETROK_LCTRL
#define RENDERKEY_RCTRL    RETROK_RCTRL
#define RENDERKEY_LALT     RETROK_LALT
#define RENDERKEY_RALT     RETROK_RALT
#define RENDERKEY_HOME     RETROK_HOME
#define RENDERKEY_END      RETROK_END
#define RENDERKEY_PAGEUP   RETROK_PAGEUP
#define RENDERKEY_PAGEDOWN RETROK_PAGEDOWN
#define RENDERKEY_F1       RETROK_F1
#define RENDERKEY_F2       RETROK_F2
#define RENDERKEY_F3       RETROK_F3
#define RENDERKEY_F4       RETROK_F4
#define RENDERKEY_F5       RETROK_F5
#define RENDERKEY_F6       RETROK_F6
#define RENDERKEY_F7       RETROK_F7
#define RENDERKEY_F8       RETROK_F8
#define RENDERKEY_F9       RETROK_F9
#define RENDERKEY_F10      RETROK_F10
#define RENDERKEY_F11      RETROK_F11
#define RENDERKEY_F12      RETROK_F12
#define RENDERKEY_SELECT   RETRO_DEVICE_ID_JOYPAD_SELECT
#define RENDERKEY_PLAY     RETRO_DEVICE_ID_JOYPAD_START
#define RENDERKEY_SEARCH   RETROK_QUESTION
#define RENDERKEY_BACK     RETROK_CLEAR
#define render_relative_mouse 
#else
#include <SDL.h>
#define RENDERKEY_UP       SDLK_UP
#define RENDERKEY_DOWN     SDLK_DOWN
#define RENDERKEY_LEFT     SDLK_LEFT
#define RENDERKEY_RIGHT    SDLK_RIGHT
#define RENDERKEY_ESC      SDLK_ESCAPE
#define RENDERKEY_DEL      SDLK_DELETE
#define RENDERKEY_LSHIFT   SDLK_LSHIFT
#define RENDERKEY_RSHIFT   SDLK_RSHIFT
#define RENDERKEY_LCTRL    SDLK_LCTRL
#define RENDERKEY_RCTRL    SDLK_RCTRL
#define RENDERKEY_LALT     SDLK_LALT
#define RENDERKEY_RALT     SDLK_RALT
#define RENDERKEY_HOME     SDLK_HOME
#define RENDERKEY_END      SDLK_END
#define RENDERKEY_PAGEUP   SDLK_PAGEUP
#define RENDERKEY_PAGEDOWN SDLK_PAGEDOWN
#define RENDERKEY_F1       SDLK_F1
#define RENDERKEY_F2       SDLK_F2
#define RENDERKEY_F3       SDLK_F3
#define RENDERKEY_F4       SDLK_F4
#define RENDERKEY_F5       SDLK_F5
#define RENDERKEY_F6       SDLK_F6
#define RENDERKEY_F7       SDLK_F7
#define RENDERKEY_F8       SDLK_F8
#define RENDERKEY_F9       SDLK_F9
#define RENDERKEY_F10      SDLK_F10
#define RENDERKEY_F11      SDLK_F11
#define RENDERKEY_F12      SDLK_F12
#define RENDERKEY_SELECT   SDLK_SELECT
#define RENDERKEY_PLAY     SDLK_AUDIOPLAY
#define RENDERKEY_SEARCH   SDLK_AC_SEARCH
#define RENDERKEY_BACK     SDLK_AC_BACK
#define RENDER_DPAD_UP     SDL_HAT_UP
#define RENDER_DPAD_DOWN   SDL_HAT_DOWN
#define RENDER_DPAD_LEFT   SDL_HAT_LEFT
#define RENDER_DPAD_RIGHT  SDL_HAT_RIGHT
#define render_relative_mouse SDL_SetRelativeMouseMode
#endif

#define MAX_JOYSTICKS 8
#define MAX_MICE 8
#define MAX_MOUSE_BUTTONS 8

#define FRAMEBUFFER_ODD 0
#define FRAMEBUFFER_EVEN 1

#include "vdp.h"
#include "psg.h"
#include "ym2612.h"

typedef enum {
	VID_NTSC,
	VID_PAL,
	NUM_VID_STD
} vid_std;

#define RENDER_DPAD_BIT 0x40000000
#define RENDER_AXIS_BIT 0x20000000
#define RENDER_INVALID_NAME -1
#define RENDER_NOT_MAPPED -2
#define RENDER_NOT_PLUGGED_IN -3

typedef void (*drop_handler)(const char *filename);

uint32_t render_map_color(uint8_t r, uint8_t g, uint8_t b);
void render_save_screenshot(char *path);
uint32_t *render_get_framebuffer(uint8_t which, int *pitch);
void render_framebuffer_updated(uint8_t which, int width);
void render_init(int width, int height, char * title, uint8_t fullscreen);
void render_set_video_standard(vid_std std);
void render_toggle_fullscreen();
void render_update_caption(char *title);
void render_wait_quit(vdp_context * context);
void render_wait_psg(psg_context * context);
void render_wait_ym(ym2612_context * context);
void render_disable_ym();
void render_enable_ym();
uint32_t render_audio_buffer();
uint32_t render_sample_rate();
void process_events();
int render_width();
int render_height();
int render_fullscreen();
void render_set_drag_drop_handler(drop_handler handler);
void process_events();
int32_t render_translate_input_name(int32_t controller, char *name, uint8_t is_axis);
int32_t render_dpad_part(int32_t input);
int32_t render_axis_part(int32_t input);
uint8_t render_direction_part(int32_t input);
void render_errorbox(char *title, char *message);
void render_warnbox(char *title, char *message);
void render_infobox(char *title, char *message);
uint32_t render_emulated_width();
uint32_t render_emulated_height();
uint32_t render_overscan_top();
uint32_t render_overscan_left();

#endif //RENDER_H_

