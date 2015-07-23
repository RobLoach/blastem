/*
 Copyright 2013 Michael Pavone
 This file is part of BlastEm.
 BlastEm is free software distributed under the terms of the GNU General Public License version 3 or greater. See COPYING for full license text.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "render.h"
#include "blastem.h"
#include "io.h"
#include "util.h"

#include <glad/glad.h>

static uint8_t render_dbg = 0;
static uint8_t debug_pal = 0;
static uint32_t last_frame = 0;

static int16_t * current_psg = NULL;
static int16_t * current_ym = NULL;

static uint32_t buffer_samples, sample_rate;
static uint32_t missing_count;

//SDL_cond * psg_cond;
//SDL_cond * ym_cond;
static uint8_t quitting = 0;

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
	context->oddbuf = context->framebuf = malloc(512 * 256 * 4 * 2);
	memset(context->oddbuf, 0, 512 * 256 * 4 * 2);
	context->evenbuf = ((char *)context->oddbuf) + 512 * 256 * 4;
	glGenTextures(3, textures);
	for (int i = 0; i < 3; i++)
	{
		glBindTexture(GL_TEXTURE_2D, textures[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (i < 2) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 256, 0, GL_BGRA, GL_UNSIGNED_BYTE, i ? context->evenbuf : context->oddbuf);
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

void render_init(int width, int height, char * title, uint32_t fps, uint8_t fullscreen)
{
	printf("width: %d, height: %d\n", width, height);

	//gladLoadGLLoader(something_get_proc_address);
	
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
	render_context(context);
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


