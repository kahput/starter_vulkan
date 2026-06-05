#include <emscripten/emscripten.h>
#include <emscripten/html5_webgl.h>
#include <stdio.h>
#include "core/arena.h"
#include "core/logger.h"
#include "emscripten.h"
#include "emscripten/html5.h"
#include <GLES2/gl2.h>
#include "os.h"

OS_Event events[256];

bool on_keydown(int type, const EmscriptenKeyboardEvent *event, void *data) {
	bool pressed = type == EMSCRIPTEN_EVENT_KEYPRESS;
	LOG_INFO("%s %s", event->key, pressed ? "pressed" : "released");
	return true;
}

GLuint shaderProgram;
GLuint vbo_handle;

void main_loop(void) {
	// 2. Clear the screen every frame
	glClearColor(170.f / 255.f, 222.f / 255.f, 135.f / 255.f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// 3. Use the shader program
	glUseProgram(shaderProgram);

	// 4. Bind the VBO and link it to the attribute (The GLES 2 way)
	glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);

	// Get the attribute location dynamically since we don't have 'layout (location = 0)'
	GLint posAttrib = glGetAttribLocation(shaderProgram, "aPos");
	glEnableVertexAttribArray(posAttrib);
	glVertexAttribPointer(posAttrib, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);

	// 5. Draw
	glDrawArrays(GL_TRIANGLES, 0, 3);
}

int main(void) {
	ArenaTemp scratch = arena_scratch_begin(NULL);

	emscripten_set_canvas_element_size("#canvas", 1280, 720);
	EmscriptenWebGLContextAttributes context_attribs;
	emscripten_webgl_init_context_attributes(&context_attribs);

	emscripten_set_keydown_callback("#canvas", NULL, true, on_keydown);
	/* emscripten_set_mousemove_callback("#canvas", NULL, true, on_mousemove); */

	context_attribs.majorVersion = 1;
	context_attribs.minorVersion = 0;
	// clang-format off
    float vertices[] = {
        -0.5f, -0.5f, 0.0f,
        0.5f, -0.5f, 0.0f,
        0.0f,  0.5f, 0.0f
    };
	// clang-format on

	uint64_t context = emscripten_webgl_create_context("#canvas", &context_attribs);
	emscripten_webgl_make_context_current(context);

	glGenBuffers(1, &vbo_handle);
	glBindBuffer(GL_ARRAY_BUFFER, vbo_handle);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	const char *vertexShaderSource =
		"attribute vec3 aPos;\n"
		"void main()\n"
		"{\n"
		"   gl_Position = vec4(aPos, 1.0);\n"
		"}\n";
	const char *fragmentShaderSource =
		"precision mediump float;\n"
		"void main()\n"
		"{\n"
		"    gl_FragColor = vec4(1.0, 0.5, 0.2, 1.0);\n"
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

	emscripten_set_main_loop(main_loop, 0, true);
	return 0;
}
