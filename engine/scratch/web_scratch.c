#include <stdio.h>
#include "common.h"
#include "core/arena.h"
#include "core/logger.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5_webgl.h>
#include <GLES3/gl3.h>

#include "core/shape2.h"
#include "input.h"
#include "os.h"

OS_Event events[256];

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
} Batch;

typedef struct {
	float2 position, uv;
	float4 color;

	uint32_t image_id;
} Vertex2;

void push_rect(Batch *buffer, Rectangle rect, Color color) {
	float4 f_color = {
		.x = color.r / 255.f,
		.y = color.g / 255.f,
		.z = color.b / 255.f,
		.w = color.a / 255.f,
	};

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
        (Vertex2){.position = {x0, y1}, .uv = {0.0f, 1.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {1.0f, 0.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x0, y0}, .uv = {0.0f, 0.0f}, .color = f_color }, // , .image_id = image_index}, 

        (Vertex2){.position = {x0, y1}, .uv = {0.0f, 1.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y1}, .uv = {1.0f, 1.0f}, .color = f_color }, // , .image_id = image_index},
        (Vertex2){.position = {x1, y0}, .uv = {1.0f, 0.0f}, .color = f_color }, // , .image_id = image_index}
    };
	// clang-format on

	memory_copy(buffer->memory + buffer->offset, quad, sizeof(quad));
	buffer->offset += sizeof(quad);
}

Batch batch;
GLuint shaderProgram;
GLuint vao;

void main_loop(void) {
	input_update();

	glClearColor(170.f / 255.f, 222.f / 255.f, 135.f / 255.f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	glUseProgram(shaderProgram);
	glBindVertexArray(vao);

	if (input_key_down(KEY_CODE_A)) {
		LOG_INFO("A");
	}

	glDrawArrays(GL_TRIANGLES, 0, batch.offset / sizeof(Vertex2));
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

	uint64_t context = emscripten_webgl_create_context("#canvas", &context_attribs);
	emscripten_webgl_make_context_current(context);

	batch = (Batch){
		.memory = arena_push(scratch.arena, MiB(1), 16, true),
		.capcity = MiB(1),
	};

	const char *vertexShaderSource =
		"#version 300 es\n"
		"precision highp float;\n"
		"in vec2 in_position;\n"
		"in vec2 in_uv;\n"
		"in vec4 in_color;\n"
		"out vec2 f_uv;\n"
		"out vec4 f_color;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(in_position, 0.0f, 1.0);\n"
		"   f_color = in_color;\n"
		"   f_uv = in_uv;\n"
		"}\n";
	const char *fragmentShaderSource =
		"#version 300 es\n"
		"precision mediump float;\n"
		"in vec4 f_color;"
		"out vec4 out_color;"
		"void main()\n"
		"{\n"
		"    out_color = f_color;\n"
		"}\n";

	unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
	glCompileShader(vertexShader);

	unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
	glCompileShader(fragmentShader);

	shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);

	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	push_rect(&batch, rect(0, 0, 200, 200), RED);
	push_rect(&batch, rect(300, 300, 100, 100), GREEN);
	push_rect(&batch, rect(250, 0, 100, 100), BLUE);

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	GLuint vbo = 0;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, batch.offset, batch.memory, GL_STATIC_DRAW);

	GLuint pos_attrib = glGetAttribLocation(shaderProgram, "in_position");
	glVertexAttribPointer(pos_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2), (void *)offsetof(Vertex2, position));
	glEnableVertexAttribArray(pos_attrib);

	GLuint uv_attrib = glGetAttribLocation(shaderProgram, "in_uv");
	glVertexAttribPointer(uv_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2), (void *)offsetof(Vertex2, uv));
	glEnableVertexAttribArray(uv_attrib);

	GLuint color_attrib = glGetAttribLocation(shaderProgram, "in_color");
	glVertexAttribPointer(color_attrib, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex2), (void *)offsetof(Vertex2, color));
	glEnableVertexAttribArray(color_attrib);

	glBindVertexArray(0);

	emscripten_set_main_loop(main_loop, 0, true);
	return 0;
}
