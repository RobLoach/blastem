/* vim: ts=3 sts=3 sw=3 et list :
 * Copyright 2015 higor Eurípedes
 * This file is part of blastem-libretro.
 * This file is licensed under whatever license BlastEm is licensed under.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "render.h"
#include "blastem.h"
#include "backend.h"
#include "io.h"
#include "util.h"

#include <glad/glad.h>

static uint8_t render_dbg = 0;
static uint8_t debug_pal = 0;

static int16_t * current_psg = NULL;
static int16_t * current_ym = NULL;

//SDL_cond * psg_cond;
//SDL_cond * ym_cond;
static uint8_t quitting = 0;

static retro_environment_t   env_cb   = NULL;
static retro_input_state_t   input_cb = NULL;
static retro_input_poll_t    poll_cb  = NULL;
static retro_video_refresh_t video_cb = NULL;
static struct retro_hw_render_callback hw_render;

static bool bool_env(unsigned env, bool value) { return env_cb(env, &value); }
static bool uint_env(unsigned env, unsigned value) { return env_cb(env, &value); }

/* blastem.c */
extern uint16_t *cart;
extern tern_node * config;
extern const memmap_chunk base_map[3];
extern char * save_filename;
extern uint8_t version_reg;
#define MCLKS_NTSC 53693175
#define MCLKS_PAL  53203395
#define MCLKS_PER_68K 7
#define MCLKS_PER_YM  MCLKS_PER_68K
#define MCLKS_PER_Z80 15
#define MCLKS_PER_PSG (MCLKS_PER_Z80*16)
#define DEFAULT_SYNC_INTERVAL MCLKS_LINE
#define SMD_HEADER_SIZE 512
#define SMD_MAGIC1 0x03
#define SMD_MAGIC2 0xAA
#define SMD_MAGIC3 0xBB
#define SMD_BLOCK_SIZE 0x4000
#ifndef NO_Z80
extern const memmap_chunk z80_map[5];
#endif
void byteswap_rom(int filesize);
void set_region(rom_info *info, uint8_t region);
void init_run_cpu(genesis_context * gen, rom_info *rom, FILE * address_log, char * statefile, uint8_t * debugger);


/* extracted from main() */
static vdp_context v_context;
static genesis_context gen;
static ym2612_context y_context;
static psg_context p_context;
static z80_context z_context;
static z80_options z_opts;
static char * statefile = NULL;

void render_close_audio()
{
   quitting = 1;
}

int render_num_joysticks()
{
   return 8;
}

uint32_t render_map_color(uint8_t r, uint8_t g, uint8_t b)
{
   return 255 << 24 | r << 16 | g << 8 | b;
}

static GLuint textures[3], buffers[2], vshader, fshader, program, un_textures[2], un_width, at_pos;

static GLfloat vertex_data[] = {
   -1.0f, -1.0f,
   1.0f, -1.0f,
   -1.0f,  1.0f,
   1.0f,  1.0f
};

const GLushort element_data[] = {0, 1, 2, 3};

static GLuint load_shader(char * fname, GLenum shader_type)
{
   char * parts[] = {get_home_dir(), "/.config/blastem/shaders/", fname};
   char * shader_path = alloc_concat_m(3, parts);
   FILE * f = fopen(shader_path, "r");
   free(shader_path);
   if (!f) {
      parts[0] = get_exe_dir();
      parts[1] = "/shaders/";
      shader_path = alloc_concat_m(3, parts);
      f = fopen(shader_path, "r");
      free(shader_path);
      if (!f) {
         fprintf(stderr, "Failed to open shader file %s for reading\n", fname);
         return 0;
      }
   }
   long fsize = file_size(f);
   GLchar * text = malloc(fsize);
   if (fread(text, 1, fsize, f) != fsize) {
      fprintf(stderr, "Error reading from shader file %s\n", fname);
      free(text);
      return 0;
   }
   GLuint ret = glCreateShader(shader_type);
   glShaderSource(ret, 1, (const GLchar **)&text, (const GLint *)&fsize);
   free(text);
   glCompileShader(ret);
   GLint compile_status, loglen;
   glGetShaderiv(ret, GL_COMPILE_STATUS, &compile_status);
   if (!compile_status) {
      fprintf(stderr, "Shader %s failed to compile\n", fname);
      glGetShaderiv(ret, GL_INFO_LOG_LENGTH, &loglen);
      text = malloc(loglen);
      glGetShaderInfoLog(ret, loglen, NULL, text);
      fputs(text, stderr);
      free(text);
      glDeleteShader(ret);
      return 0;
   }
   return ret;
}

void render_alloc_surfaces(vdp_context * context)
{
   context->oddbuf = context->framebuf = calloc(1, 512 * 256 * 4 * 2);
   context->evenbuf = ((char *)context->oddbuf) + 512 * 256 * 4;
}

static void context_reset(void)
{
   gladLoadGLLoader((GLADloadproc)hw_render.get_proc_address);

   glGenTextures(3, textures);
   for (int i = 0; i < 3; i++)
   {
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (i < 2) {
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 256, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
      } else {
         uint32_t blank = 255 << 24;
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, &blank);
      }
   }
   glGenBuffers(2, buffers);
   glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
   glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data), vertex_data, GL_STATIC_DRAW);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(element_data), element_data, GL_STATIC_DRAW);
   tern_val def = {.ptrval = "default.v.glsl"};
   vshader = load_shader(tern_find_path_default(config, "video\0vertex_shader\0", def).ptrval, GL_VERTEX_SHADER);
   def.ptrval = "default.f.glsl";
   fshader = load_shader(tern_find_path_default(config, "video\0fragment_shader\0", def).ptrval, GL_FRAGMENT_SHADER);
   program = glCreateProgram();
   glAttachShader(program, vshader);
   glAttachShader(program, fshader);
   glLinkProgram(program);
   GLint link_status;
   glGetProgramiv(program, GL_LINK_STATUS, &link_status);
   if (!link_status) {
      fputs("Failed to link shader program\n", stderr);
      exit(1);
   }
   un_textures[0] = glGetUniformLocation(program, "textures[0]");
   un_textures[1] = glGetUniformLocation(program, "textures[1]");
   un_width = glGetUniformLocation(program, "width");
   at_pos = glGetAttribLocation(program, "pos");
}

static void context_destroy(void)
{
   glDeleteTextures(3, textures);
   glDeleteBuffers(2, buffers);
   glDeleteProgram(program);
}

void render_init(int width, int height, char * title, uint32_t fps, uint8_t fullscreen)
{
   printf("width: %d, height: %d\n", width, height);

   float aspect = (float)width / height;
   tern_val def = {.ptrval = "normal"};
   if (fabs(aspect - 4.0/3.0) > 0.01 && strcmp(tern_find_path_default(config, "video\0aspect\0", def).ptrval, "stretch")) {
      for (int i = 0; i < 4; i++)
      {
         if (aspect > 4.0/3.0) {
            vertex_data[i*2] *= (4.0/3.0)/aspect;
         } else {
            vertex_data[i*2+1] *= aspect/(4.0/3.0);
         }
      }
   }
   //psg_cond = SDL_CreateCond();
   //ym_cond = SDL_CreateCond();

   /* Rate is 48000hz
    * FPS is 60
    */
}

void render_context(vdp_context * context)
{
   glBindTexture(GL_TEXTURE_2D, textures[context->framebuf == context->oddbuf ? 0 : 1]);
   glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 320, 240, GL_BGRA, GL_UNSIGNED_BYTE, context->framebuf);;

   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT);

   glUseProgram(program);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, textures[0]);
   glUniform1i(un_textures[0], 0);

   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, (context->regs[REG_MODE_4] & BIT_INTERLACE) ? textures[1] : textures[2]);
   glUniform1i(un_textures[1], 1);

   glUniform1f(un_width, context->regs[REG_MODE_4] & BIT_H40 ? 320.0f : 256.0f);

   glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
   glVertexAttribPointer(at_pos, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat[2]), (void *)0);
   glEnableVertexAttribArray(at_pos);

   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
   glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, (void *)0);
   glDisableVertexAttribArray(at_pos);

   if (context->regs[REG_MODE_4] & BIT_INTERLACE)
      context->framebuf = context->framebuf == context->oddbuf ? context->evenbuf : context->oddbuf;
}

int render_joystick_num_buttons(int joystick)
{
   return 16;
}

int render_joystick_num_hats(int joystick)
{
   return 4; // 1 or 4?
}

void render_wait_quit(vdp_context * context)
{
   //SDL_Event event;
   //while(SDL_WaitEvent(&event)) {
   //	switch (event.type) {
   //	case SDL_KEYDOWN:
   //		if (event.key.keysym.sym == SDLK_LEFTBRACKET) {
   //			render_dbg++;
   //			if (render_dbg == 4) {
   //				render_dbg = 0;
   //			}
   //			render_context(context);
   //		} else if(event.key.keysym.sym ==  SDLK_RIGHTBRACKET) {
   //			debug_pal++;
   //			if (debug_pal == 4) {
   //				debug_pal = 0;
   //			}
   //		}
   //		break;
   //	case SDL_QUIT:
   //		return;
   //	}
   //}
}

void render_debug_mode(uint8_t mode)
{
   if (mode < 4) {
      render_dbg = mode;
   }
}

void render_debug_pal(uint8_t pal)
{
   if (pal < 4) {
      debug_pal = pal;
   }
}

static int32_t handle_event(void *event)
{
   //	switch (event->type) {
   //	case SDL_KEYDOWN:
   //		handle_keydown(event->key.keysym.sym);
   //		break;
   //	case SDL_KEYUP:
   //		handle_keyup(event->key.keysym.sym);
   //		break;
   //	case SDL_JOYBUTTONDOWN:
   //		handle_joydown(event->jbutton.which, event->jbutton.button);
   //		break;
   //	case SDL_JOYBUTTONUP:
   //		handle_joyup(event->jbutton.which, event->jbutton.button);
   //		break;
   //	case SDL_JOYHATMOTION:
   //		handle_joy_dpad(event->jbutton.which, event->jhat.hat, event->jhat.value);
   //		break;
   //	case SDL_QUIT:
   //		puts("");
   //		exit(0);
   //	}
   return 0;
}

int wait_render_frame(vdp_context * context, int frame_limit)
{
   //	SDL_Event event;
   int ret = 0;
   //	while(SDL_PollEvent(&event)) {
   //		ret = handle_event(&event);
   //	}
//   render_context(context);
   return ret;
}

void process_events()
{
   //	SDL_Event event;
   //	while(SDL_PollEvent(&event)) {
   //		handle_event(&event);
   //	}
}

void render_wait_psg(psg_context * context)
{
   /* flush audio */
   //	SDL_LockMutex(audio_mutex);
   //		while (current_psg != NULL) {
   //			SDL_CondWait(psg_cond, audio_mutex);
   //		}
   current_psg = context->audio_buffer;
   //		SDL_CondSignal(audio_ready);

   context->audio_buffer = context->back_buffer;
   context->back_buffer = current_psg;
   //	SDL_UnlockMutex(audio_mutex);
   context->buffer_pos = 0;
}

void render_wait_ym(ym2612_context * context)
{
   /* flush audio */
   //	SDL_LockMutex(audio_mutex);
   //		while (current_ym != NULL) {
   //			SDL_CondWait(ym_cond, audio_mutex);
   //		}
   current_ym = context->audio_buffer;
   //		SDL_CondSignal(audio_ready);

   context->audio_buffer = context->back_buffer;
   context->back_buffer = current_ym;
   //	SDL_UnlockMutex(audio_mutex);
   context->buffer_pos = 0;
}

void render_fps(uint32_t fps)
{
}

uint32_t render_audio_buffer()
{
   return 512;
}

uint32_t render_sample_rate()
{
   return 48000;
}

static int parse_smd_rom(const uint8_t *data, size_t size)
{
   return -1;
}

static int parse_rom(const uint8_t *data, size_t size)
{
   uint8_t header[10];

   if (size < sizeof(header))
      return -1;
   memcpy(header, data, sizeof(header));

   if (header[1] == SMD_MAGIC1 && header[8] == SMD_MAGIC2 && header[9] == SMD_MAGIC3) {
      int i;
      for (i = 3; i < 8; i++) {
         if (header[i] != 0) {
            break;
         }
      }
      if (i == 8) {
         if (header[2]) {
            fprintf(stderr, "Unsupported SMD ROM");
            exit(1);
         }
         return parse_smd_rom(data, size);
      }
   }
   cart = malloc(size);
   memcpy(cart, data, size);

   return size;
}

RETRO_API void retro_run(void)
{
    poll_cb();
    video_cb(RETRO_HW_FRAME_BUFFER_VALID, 320, 240, 320*sizeof(uint32_t));
}

RETRO_API void retro_init(void)
{
   save_filename = "/xxxx_blastem__/a";
   set_exe_str("./blastem");
   config = load_config();
}

RETRO_API void retro_deinit(void) { }


RETRO_API void retro_reset(void)
{

}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
   int rom_size  = parse_rom(game->data, game->size);

   if (rom_size <= 0)
      return false;

   tern_node *rom_db = load_rom_db();
   rom_info info = configure_rom(rom_db, cart, rom_size, base_map, sizeof(base_map)/sizeof(base_map[0]));
   byteswap_rom(rom_size);
   set_region(&info, 0);

   uint_env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, RETRO_PIXEL_FORMAT_XRGB8888);

   hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
   hw_render.context_reset = context_reset;
   hw_render.context_destroy = context_destroy;
   hw_render.depth = false;
   hw_render.stencil = false;
   hw_render.bottom_left_origin = true;

   if (!env_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render))
   {
      env_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
      quitting = 1;
      abort();
   }

   render_init(320, 240, "BlastEm", 60, true);

   memset(&gen, 0, sizeof(gen));
   gen.master_clock = gen.normal_clock = MCLKS_NTSC;//fps == 60 ? MCLKS_NTSC : MCLKS_PAL;

   init_vdp_context(&v_context, version_reg & 0x40);
   gen.frame_end = vdp_cycles_to_frame_end(&v_context);
   char * config_cycles = tern_find_path(config, "clocks\0max_cycles\0").ptrval;
   gen.max_cycles = config_cycles ? atoi(config_cycles) : DEFAULT_SYNC_INTERVAL;

   ym_init(&y_context, render_sample_rate(), gen.master_clock, MCLKS_PER_YM, render_audio_buffer(), 0);

   psg_init(&p_context, render_sample_rate(), gen.master_clock, MCLKS_PER_PSG, render_audio_buffer());

#ifndef NO_Z80
   init_z80_opts(&z_opts, z80_map, 5, MCLKS_PER_Z80);
   init_z80_context(&z_context, &z_opts);
   z80_assert_reset(&z_context, 0);
#endif

   z_context.system = &gen;
   z_context.mem_pointers[0] = z80_ram;
   z_context.mem_pointers[1] = z_context.mem_pointers[2] = (uint8_t *)cart;

   gen.z80 = &z_context;
   gen.vdp = &v_context;
   gen.ym = &y_context;
   gen.psg = &p_context;
   genesis = &gen;
   setup_io_devices(config, gen.ports);

   set_keybindings(gen.ports);

   init_run_cpu(&gen, &info, NULL, NULL, NULL);

   return true;
}

RETRO_API void retro_unload_game(void)
{

}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{

}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
   info->library_name     = "BlastEm";
   info->library_version  = "hg";
   info->need_fullpath    = false;
   info->valid_extensions = "";
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
   info->geometry.base_width   = 320;
   info->geometry.base_height  = 240;
   info->geometry.aspect_ratio = 320/240;
   info->geometry.max_width    = info->geometry.base_width;
   info->geometry.max_height   = info->geometry.base_height;
   info->timing.fps            = 60;
   info->timing.sample_rate    = 48000;
}

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_set_video_refresh(retro_video_refresh_t p) { video_cb = p; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t p) { }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t p) { }
RETRO_API void retro_set_input_poll(retro_input_poll_t p) { poll_cb = p; }
RETRO_API void retro_set_input_state(retro_input_state_t p) { input_cb = p; }
RETRO_API void retro_set_environment(retro_environment_t p)
{
   env_cb = p;
}

RETRO_API bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
   return false;
}

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *data, size_t size) { return false; }
RETRO_API bool retro_unserialize(const void *data, size_t size) { return false; }
RETRO_API void retro_cheat_reset(void) { }
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { }
RETRO_API void *retro_get_memory_data(unsigned id) { return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { return 0; }

