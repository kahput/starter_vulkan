#include <stdio.h>
#include "common.h"
#include "core/arena.h"
#include "core/debug.h"
#include "core/logger.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5_webgl.h>
#include <GLES3/gl3.h>

#include "core/shape2.h"
#include "input.h"
#include "os.h"

#include <stb/stb_image.h>

typedef struct {
	GLuint handle;
	uint32_t width, height;
} Image2D;

static Image2D make_image(void *buffer, uint32_t width, uint32_t height) {
	Image2D result = { 0 };

	bool ok = buffer;
	if (ok) {
		result.width = width, result.height = height;

		glGenTextures(1, &result.handle);
		glBindTexture(GL_TEXTURE_2D, result.handle);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, result.width, result.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	return result;
}

static Image2D load_image(String8 path) {
	Image2D result = { 0 };

	ArenaTemp scratch = arena_scratch_begin(0);
	String8 source = os_file_read_entire(scratch.arena, path);

	bool ok = source.length;
	if (ok) {
		int32_t channels_in_file;
		void *image_data = stbi_load_from_memory(source.text, source.length, (int32_t *)&result.width, (int32_t *)&result.height, &channels_in_file, 4);
		result = make_image(image_data, result.width, result.height);
		stbi_image_free(image_data);
	}

	arena_scratch_end(scratch);
	return result;
}

InputState input = { 0 };
bool on_keypress(int type, const EmscriptenKeyboardEvent *event, void *data) {
	input.keys[event->key[0]].state = true;
	return true;
}

bool on_keyrelease(int type, const EmscriptenKeyboardEvent *event, void *data) {
	input.keys[event->key[0]].state = false;
	return true;
}

typedef struct {
	uint8_t *memory;
	uint64_t offset, capcity;

	uint32_t image_indices[16];
	uint32_t indices_count;
} Batch;

int32_t find_index(Batch *batch, uint32_t id) {
	for (uint32_t index = 0; index < countof(batch->image_indices); ++index) {
		if (batch->image_indices[index] == id) {
			return index;
		}
	}

	return -1;
}

typedef struct {
	float2 position, uv;
	uint32_t color, image_index;
} Vertex2;

void push_rect(Batch *buffer, Rectangle rect, Color color) {
	float x0 = (rect.x - 640.f) / 640.f;
	float y0 = (rect.y - 360.f) / 360.f;
	y0 *= -1;
	float x1 = ((rect.x + rect.width) - 640.f) / 640.f;
	float y1 = ((rect.y + rect.height) - 360.f) / 360.0f;
	y1 *= -1;

	/* float u0 = src.x / image.width; */
	/* float v0 = src.y / image.height; */
	/* float u1 = (src.x + src.width) / image.width; */
	/* float v1 = (src.y + src.height) / image.height; */

	// clang-format off
    Vertex2 quad[] = {
        // pos      // tex
        (Vertex2){.position = {x0, y1}, .uv = {0.0f, 1.0f}, .color = color_pack_uint32(color) }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {1.0f, 0.0f}, .color = color_pack_uint32(color) }, // , .image_id = image_index},
        (Vertex2){.position = {x0, y0}, .uv = {0.0f, 0.0f}, .color = color_pack_uint32(color) }, // , .image_id = image_index}, 

        (Vertex2){.position = {x0, y1}, .uv = {0.0f, 1.0f}, .color = color_pack_uint32(color) }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y1}, .uv = {1.0f, 1.0f}, .color = color_pack_uint32(color) }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {1.0f, 0.0f}, .color = color_pack_uint32(color) }, // , .image_id = image_index}
    };
	// clang-format on

	memory_copy(buffer->memory + buffer->offset, quad, sizeof(quad));
	buffer->offset += sizeof(quad);
}

void draw_sprite_ex(Batch *batch, Rectangle src, Rectangle dst, Image2D image, Color tint) {
	float x0 = (dst.x - 640.f) / 640.f;
	float y0 = (dst.y - 360.f) / 360.f;
	y0 *= -1;
	float x1 = ((dst.x + dst.width) - 640.f) / 640.f;
	float y1 = ((dst.y + dst.height) - 360.f) / 360.0f;
	y1 *= -1;

	float u0 = src.x / image.width;
	float v0 = src.y / image.height;
	float u1 = (src.x + src.width) / image.width;
	float v1 = (src.y + src.height) / image.height;

	int32_t image_index = find_index(batch, image.handle);
	ASSERT((image_index == -1 && batch->indices_count >= countof(batch->image_indices)) == false);
	if (image_index == -1) {
		image_index = batch->indices_count++;
		batch->image_indices[image_index] = image.handle;
	}

	// clang-format off
    Vertex2 quad[] = {
        // pos      // tex
        (Vertex2){ .position = { x0, y1 }, .uv = { u0, v1}, .color = color_pack_uint32(tint) , .image_index = image_index },
        (Vertex2){ .position = { x1, y0 }, .uv = { u1, v0 }, .color = color_pack_uint32(tint) , .image_index = image_index },
        (Vertex2){ .position = { x0, y0 }, .uv = { u0, v0 }, .color = color_pack_uint32(tint) , .image_index = image_index }, 

        (Vertex2){ .position = { x0, y1 }, .uv = { u0, v1 }, .color = color_pack_uint32(tint) , .image_index = image_index },
        (Vertex2){ .position = { x1, y1 }, .uv = { u1, v1 }, .color = color_pack_uint32(tint) , .image_index = image_index },
        (Vertex2){ .position = { x1, y0 }, .uv = { u1, v0 }, .color = color_pack_uint32(tint) , .image_index = image_index }
    };
	// clang-format on

	memory_copy(batch->memory + batch->offset, quad, sizeof(quad));
	batch->offset += sizeof(quad);
}
void draw_sprite(Batch *batch, float2 position, Image2D image, Color tint) {
	draw_sprite_ex(batch, rect(0, 0, image.width, image.height), rect(position.x, position.y, image.width, image.height), image, tint);
}

static Arena frame_arena = { 0 };
static GLuint shaderProgram;
static GLuint vao, vbo;

static Image2D textures[4];

void main_loop(void) {
	input_update();

	glClearColor(170.f / 255.f, 222.f / 255.f, 135.f / 255.f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	Batch batch = {
		.memory = arena_push_count(&frame_arena, uint8_t, 1024 * sizeof(Vertex2) * 6),
		.capcity = 1024 * sizeof(Vertex2) * 6,
		.image_indices = { [0] = textures[0].handle },
		.indices_count = 1,
	};

	push_rect(&batch, rect(0, 0, 256, 256), RED);
    draw_sprite(&batch, make2(256, 256), textures[2], WHITE);

	if (input_key_down(KEY_CODE_A)) {
		LOG_INFO("A");
	}

	glBindVertexArray(vao);
	if (batch.offset)
		glBufferSubData(GL_ARRAY_BUFFER, 0, batch.offset, batch.memory);

	glUseProgram(shaderProgram);
	GLint samplers[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	glUniform1iv(glGetUniformLocation(shaderProgram, "u_textures"), 16, samplers);

	LOG_INFO("#count = %d", batch.indices_count);
	for (uint32_t index = 0; index < batch.indices_count; ++index) {
		glActiveTexture(GL_TEXTURE0 + index);
		glBindTexture(GL_TEXTURE_2D, batch.image_indices[index]);
	}

	glDrawArrays(GL_TRIANGLES, 0, batch.offset / sizeof(Vertex2));

	arena_reset(&frame_arena);
}

int main(void) {
	ArenaTemp scratch = arena_scratch_begin(NULL);

	emscripten_set_canvas_element_size("#canvas", 1280, 720);
	EmscriptenWebGLContextAttributes context_attribs;
	emscripten_webgl_init_context_attributes(&context_attribs);

	input_set_context(&input);

	emscripten_set_keypress_callback("#canvas", 0, true, on_keypress);
	emscripten_set_keyup_callback("#canvas", 0, true, on_keyrelease);
	/* emscripten_set_mousemove_callback("#canvas", NULL, true, on_mousemove); */

	context_attribs.majorVersion = 2;
	context_attribs.minorVersion = 0;
	// clang-format off
    float vertices[] = {
        0.5f,  0.5f, 0.0f,  // top right
        0.5f, -0.5f, 0.0f,  // bottom right
        -0.5f, -0.5f, 0.0f,  // bottom left
        -0.5f,  0.5f, 0.0f   // top left 
    };
    unsigned int indices[] = {  // note that we start from 0!
        0, 1, 3,   // first triangle
        1, 2, 3    // second triangle
    };
	// clang-format on

    frame_arena = arena_make(MiB(16));

	uint64_t context = emscripten_webgl_create_context("#canvas", &context_attribs);
	emscripten_webgl_make_context_current(context);

	const char *vertexShaderSource =
		"#version 300 es\n"
		"precision highp float;\n"
		"in vec2 in_position;\n"
		"in vec2 in_uv;\n"
		"in int in_color;\n"
		"in int in_imageid;\n"
		"out vec2 f_uv;\n"
		"flat out int f_imageid;"
		"out vec4 f_color;\n"
		"vec4 packed_to_color(int packed) {\n"
		"   return vec4(float((packed >> 0) & 0xFF), float((packed >> 8) & 0xFF), float((packed >> 16) & 0xFF), float((packed >> 24) & 0xFF));\n"
		"}"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(in_position, 0.0f, 1.0);\n"
		"   f_color = packed_to_color(in_color) / 255.f;\n"
		"   f_imageid = in_imageid;\n"
		"   f_uv = in_uv;\n"
		"}\n";
	const char *fragmentShaderSource =
		"#version 300 es\n"
		"precision mediump float;\n"
		"uniform sampler2D u_textures[16];\n"
		"in vec2 f_uv;\n"
		"in vec4 f_color;\n"
		"flat in int f_imageid;\n"
		"vec4 sample_image_index(int imageid) {\n"
		"   switch(f_imageid) {\n"
		"      case 0: return  texture(u_textures[0], f_uv); break;\n"
		"      case 1: return  texture(u_textures[1], f_uv); break;\n"
		"      case 2: return  texture(u_textures[2], f_uv); break;\n"
		"      case 3: return  texture(u_textures[3], f_uv); break;\n"
		"      case 4: return  texture(u_textures[4], f_uv); break;\n"
		"      case 5: return  texture(u_textures[5], f_uv); break;\n"
		"      case 6: return  texture(u_textures[6], f_uv); break;\n"
		"      case 7: return  texture(u_textures[7], f_uv); break;\n"
		"      case 8: return  texture(u_textures[8], f_uv); break;\n"
		"      case 9: return  texture(u_textures[9], f_uv); break;\n"
		"      case 10: return  texture(u_textures[10], f_uv); break;\n"
		"      case 11: return  texture(u_textures[11], f_uv); break;\n"
		"      case 12: return  texture(u_textures[12], f_uv); break;\n"
		"      case 13: return  texture(u_textures[13], f_uv); break;\n"
		"      case 14: return  texture(u_textures[14], f_uv); break;\n"
		"      case 15: return  texture(u_textures[15], f_uv); break;\n"
		"      default: return  texture(u_textures[0], f_uv); break;\n"
		"   }\n"
		"}\n"
		"out vec4 out_color;\n"
		"void main()\n"
		"{\n"
		"    vec4 albedo = sample_image_index(f_imageid) * f_color;\n"
		"    if (albedo.a < 0.5) discard;"
		"    out_color = albedo;\n"
		"}\n";

	unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShader);

	GLint vertex_compiled;
	glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &vertex_compiled);
	if (vertex_compiled != GL_TRUE) {
		GLsizei log_length = 0;
		GLchar message[1024];
		glGetShaderInfoLog(vertexShader, 1024, &log_length, message);
		LOG_ERROR("#shader_compile_error: %s", message);

		return -1;
	}
	unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShader);

	GLint fragment_compiled;
	glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &fragment_compiled);
	if (fragment_compiled != GL_TRUE) {
		GLsizei log_length = 0;
		GLchar message[1024];
		glGetShaderInfoLog(fragmentShader, 1024, &log_length, message);

		LOG_ERROR("#shader_compile_error: %s", message);
		return -1;
	}

	shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	textures[0] = make_image((uint32_t[]){ 0xFFFFFFFF }, 1, 1);
	textures[1] = load_image(s("assets/textures/base_grass.png"));
	textures[2] = load_image(s("assets/textures/grass.png"));
	textures[3] = load_image(s("assets/textures/heightmap.png"));

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	GLuint vbo = 0;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(Vertex2) * 6, 0, GL_DYNAMIC_DRAW);

	{
		GLuint pos_attrib = glGetAttribLocation(shaderProgram, "in_position");
		glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2), (void *)offsetof(Vertex2, position));
		glEnableVertexAttribArray(pos_attrib);
	}

	{
		GLuint uv_attrib = glGetAttribLocation(shaderProgram, "in_uv");
		glVertexAttribPointer(uv_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2), (void *)offsetof(Vertex2, uv));
		glEnableVertexAttribArray(uv_attrib);
	}

	{
		GLuint color_attrib = glGetAttribLocation(shaderProgram, "in_color");
		glVertexAttribIPointer(color_attrib, 1, GL_INT, sizeof(Vertex2), (void *)offsetof(Vertex2, color));
		glEnableVertexAttribArray(color_attrib);
	}

	{
		GLuint image_attrib = glGetAttribLocation(shaderProgram, "in_imageid");
		glVertexAttribIPointer(image_attrib, 1, GL_INT, sizeof(Vertex2), (void *)offsetof(Vertex2, image_index));
		glEnableVertexAttribArray(image_attrib);
	}

	glBindVertexArray(0);

	/* draw_sprite(&batch, make2(500, 200), textures[0], RED); */

	emscripten_set_main_loop(main_loop, 0, true);
	return 0;
}
