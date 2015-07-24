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

#include "glad/glad.h"
#include "libco/libco.h"

#define NUM_JOYPADS 2

static cothread_t main_thread = NULL;
static cothread_t cpu_thread  = NULL;

static const struct retro_game_info *game_info;

static uint8_t render_dbg = 0;
static uint8_t debug_pal = 0;

static int16_t * current_psg = NULL;
static int16_t * current_ym = NULL;

static uint8_t quitting = 0;

static retro_environment_t   env_cb   = NULL;
static retro_input_state_t   input_cb = NULL;
static retro_input_poll_t    poll_cb  = NULL;
static retro_video_refresh_t video_cb = NULL;
static retro_audio_sample_batch_t  audio_batch_cb = NULL;

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

void render_close_audio()
{
   quitting = 1;
}

int render_num_joysticks()
{
   return NUM_JOYPADS;
}

uint32_t render_map_color(uint8_t r, uint8_t g, uint8_t b)
{
   return 255 << 24 | r << 16 | g << 8 | b;
}

void render_alloc_surfaces(vdp_context * context)
{
   context->oddbuf = context->framebuf = calloc(1, 512 * 256 * 4 * 2);
   context->evenbuf = ((char *)context->oddbuf) + 512 * 256 * 4;
}

void render_init(int width, int height, char * title, uint32_t fps, uint8_t fullscreen)
{
}

void render_context(vdp_context * context)
{
   static uint32_t screen[320 * 480];
   unsigned width  = context->regs[REG_MODE_4] & BIT_H40 ? 320.0f : 256.0f;
   unsigned height = 224;
   unsigned skip   = width;
   uint32_t *src = (uint32_t*)context->framebuf;
   uint32_t *dst = screen;
   int i;

   if (context->regs[REG_MODE_4] & BIT_INTERLACE)
   {
      skip   *= 2;
      height *= 2;

      if (context->framebuf == context->evenbuf)
         dst += width;
   }

   for (i = 0; i < 224; ++i)
   {
      memcpy(dst, src, width*sizeof(uint32_t));
      src += width;
      dst += skip;
   }

   video_cb(screen, width, height, width*sizeof(uint32_t));
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

static int16_t pads[NUM_JOYPADS][16];
static int32_t handle_events(void)
{
   int16_t now[NUM_JOYPADS][16];
   int i,j;
   for (i = 0; i < NUM_JOYPADS; ++i)
   {
      for (j = 0; j < RETRO_DEVICE_ID_JOYPAD_R+1; ++j)
      {
         now[i][j] = input_cb(i, RETRO_DEVICE_JOYPAD, 0, j);

         if (pads[i][j] != now[i][j])
         {
            /* TODO: use handle_joy_dpad() for dpad */
            if (now[i][j])
               handle_joydown(i, j);
            else
               handle_joyup(i, j);
         }

         pads[i][j] = now[i][j];
      }
   }

   return 0;
}

int wait_render_frame(vdp_context * context, int frame_limit)
{
   /* lets pray we're being called from cpu thread */
   static int16_t audio_buf[512*2];
   int16_t *buf, *ym;
   int i;

   poll_cb();
   handle_events();

   render_context(context);

   if (!current_psg || !current_ym)
      return 0;

   buf = audio_buf;
   ym  = current_ym;

   for (i = 0; i < 512; ++i)
   {
      *buf++ = current_psg[i] + *ym++;
      *buf++ = current_psg[i] + *ym++;
   }

   audio_batch_cb(audio_buf, 512);

   current_psg = NULL;
   current_ym  = NULL;

   co_switch(main_thread);

   return 0;
}

void process_events()
{
   poll_cb();
   handle_events();
}

void render_wait_psg(psg_context * context)
{
   /* TODO: flush audio */
   current_psg = context->audio_buffer;
   context->audio_buffer = context->back_buffer;
   context->back_buffer = current_psg;
   context->buffer_pos = 0;
}

void render_wait_ym(ym2612_context * context)
{
   /* TODO: flush audio */
   current_ym = context->audio_buffer;
   context->audio_buffer = context->back_buffer;
   context->back_buffer = current_ym;
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
   cart = (uint16_t*)malloc(size);
   memcpy(cart, data, size);

   return size;
}

static void cpu_thread_wrapper()
{
   int rom_size  = parse_rom((const uint8_t*)game_info->data, game_info->size);

   if (rom_size <= 0)
   {
      game_info = NULL;
      return;
   }

   co_switch(main_thread);

   tern_node *rom_db = load_rom_db();
   rom_info info = configure_rom(rom_db, cart, rom_size, base_map, sizeof(base_map)/sizeof(base_map[0]));
   byteswap_rom(rom_size);
   set_region(&info, 0);

   /* TODO: Get correct fps for the region */
   render_init(0, 0, NULL, 60, true);

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

   co_switch(main_thread);

   init_run_cpu(&gen, &info, NULL, NULL, NULL);

   quitting = 1;
}

static tern_node *init_config(void)
{
   tern_node *head = NULL;
   tern_node *bindings = NULL;
   tern_node *pads = NULL;
   tern_node *devices = NULL;
   tern_node *io = NULL;

   int i = 0;
   for (i = 0; i < NUM_JOYPADS; ++i)
   {
      char key[50], value[50], padid[50];
      tern_node *pad = NULL;
      tern_node *dpads = NULL;
      tern_node *buttons = NULL;

#define insert_dpad(name, val) do{\
   snprintf(value, sizeof(value), "gamepads.%i.%s", i+1, name);\
   dpads = tern_insert_ptr(dpads, (char*)name, strdup(value));\
   } while(0)
      insert_dpad("up",    RETRO_DEVICE_ID_JOYPAD_UP);
      insert_dpad("down",  RETRO_DEVICE_ID_JOYPAD_DOWN);
      insert_dpad("left",  RETRO_DEVICE_ID_JOYPAD_LEFT);
      insert_dpad("right", RETRO_DEVICE_ID_JOYPAD_RIGHT);
      dpads = tern_insert_node(NULL, (char*)"0", dpads);
#undef insert_dpad

#define insert_button(name, val) do{\
   snprintf(key, sizeof(key), "%i", val);\
   snprintf(value, sizeof(value), "gamepads.%i.%s", i+1, name);\
   buttons = tern_insert_ptr(buttons, key, strdup(value));\
   }while(0)

      insert_button("a",     RETRO_DEVICE_ID_JOYPAD_Y);
      insert_button("b",     RETRO_DEVICE_ID_JOYPAD_B);
      insert_button("c",     RETRO_DEVICE_ID_JOYPAD_A);
      insert_button("x",     RETRO_DEVICE_ID_JOYPAD_L);
      insert_button("y",     RETRO_DEVICE_ID_JOYPAD_X);
      insert_button("z",     RETRO_DEVICE_ID_JOYPAD_R);
      insert_button("mode",  RETRO_DEVICE_ID_JOYPAD_SELECT);
      insert_button("start", RETRO_DEVICE_ID_JOYPAD_START);
      insert_button("up",    RETRO_DEVICE_ID_JOYPAD_UP);
      insert_button("down",  RETRO_DEVICE_ID_JOYPAD_DOWN);
      insert_button("left",  RETRO_DEVICE_ID_JOYPAD_LEFT);
      insert_button("right", RETRO_DEVICE_ID_JOYPAD_RIGHT);
#undef insert_button

      pad = tern_insert_node(NULL, (char*)"dpads", dpads);
      pad = tern_insert_node(pad, (char*)"buttons", buttons);

      snprintf(padid, sizeof(padid), "%i", i);
      pads = tern_insert_node(pads, padid, pad);

      snprintf(key, sizeof(key), "%i", i+1);
      snprintf(value, sizeof(value), "gamepad6.%i", i+1);
      devices = tern_insert_ptr(devices, key, strdup(value));
   }

   bindings = tern_insert_node(bindings, (char*)"pads", pads);
   io       = tern_insert_node(io, (char*)"devices", devices);

   head = tern_insert_node(head, (char*)"bindings", bindings);
   head = tern_insert_node(head, (char*)"io", io);

   return head;
}

RETRO_API void retro_run(void)
{
   co_switch(cpu_thread);
}

RETRO_API void retro_init(void)
{
   main_thread = co_active();
   cpu_thread  = co_create(65536 * sizeof(void*), cpu_thread_wrapper);

   save_filename = (char*)"/xxxx_blastem__/a";
   set_exe_str((char*)"./blastem");
   config = init_config();
}

RETRO_API void retro_deinit(void) { }


RETRO_API void retro_reset(void)
{

}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
   game_info = game;
   co_switch(cpu_thread);

   if (game_info == NULL)
      return false;

   uint_env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, RETRO_PIXEL_FORMAT_XRGB8888);

   co_switch(cpu_thread);

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
   info->geometry.base_height  = 224;
   info->geometry.aspect_ratio = 320.0f/224.0f;
   info->geometry.max_width    = info->geometry.base_width;
   info->geometry.max_height   = info->geometry.base_height;
   info->timing.fps            = 60;
   info->timing.sample_rate    = 48000;
}

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_set_video_refresh(retro_video_refresh_t p) { video_cb = p; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t p) { }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t p) { audio_batch_cb = p; }
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

