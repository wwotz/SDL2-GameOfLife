#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <SDL2/SDL_opengl.h>

#define LINEARLIB_IMPLEMENTATION
#include "linear.h"

#define WINDOW_W 1200
#define WINDOW_H 800

#define CELL_W 10
#define CELL_H 10
#define CELL_ROWS (WINDOW_W / CELL_W)
#define CELL_COLS (WINDOW_H / CELL_H)
#define CELLS_X   1
#define CELLS_Y   1
#define CELL_COUNT ((CELL_ROWS + 2) * (CELL_COLS + 2))

static SDL_Window *window;
static SDL_GLContext *context;
static SDL_Event event;
static int running;

static struct cell_t {
	vec2_t pos;
	enum {
		DEAD,
		ALIVE,
	} state;
	int hovered;
} cells[CELL_COUNT];

enum gl_buffers {
	BUFFER_VBO = 0,
	BUFFER_EBO,
	BUFFER_VAO,
	BUFFER_COUNT
};
static GLuint buffers[BUFFER_COUNT];

static GLuint cell_shader;

static int previous_cell;
static int left_btn;
static int mouse_x, mouse_y;
static int current_cell;
static int changed_cell;

static void
edit(void);

static void
simulate(void);

static int
get_alive_neighbours(size_t x, size_t y);

void (*active_state)(void);

int
main(int argc, char **argv)
{
	SDL_Init(SDL_INIT_VIDEO);
	window = SDL_CreateWindow("Game Of Life",
				  SDL_WINDOWPOS_UNDEFINED,
				  SDL_WINDOWPOS_UNDEFINED,
				  WINDOW_W,
				  WINDOW_H,
				  SDL_WINDOW_OPENGL);
	context = SDL_GL_CreateContext(window);
	glewInit();

	glViewport(0.0, 0.0, WINDOW_W, WINDOW_H);
	glClearColor(1.0, 0.0, 0.0, 1.0);

	ll_matrix_mode(LL_MATRIX_MODEL);
	ll_matrix_identity();
	ll_matrix_mode(LL_MATRIX_PROJECTION);
	ll_matrix_orthographic(0.0, WINDOW_W, WINDOW_H, 0.0, 10.0, -10.0);

	const char *vsource = "#version 330 core\n"
		"uniform mat4 model;"
		"uniform mat4 projection;"
		"layout (location = 0) in vec2 pos;"
		"void main()"
		"{"
		"gl_Position = projection * model * vec4(pos, 0.0, 1.0);"
		"}";
		
	const char *fsource = "#version 330 core\n"
		"uniform int state;"
		"void main()"
		"{"
		"if (state == 0) {"
		"gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);"
		"} else if (state == 1) {"
		"gl_FragColor = vec4(gl_FragCoord.xy/vec2(800.0, 800.0), 1.0, 1.0);"
		"} else {"
		"gl_FragColor = vec4(0.3, 0.3, 0.3, 1.0);"
		"}"
		"}";

	GLuint vshader, fshader;
	GLint status;
	GLchar *msg;
	GLsizei max_len, len;
	
	vshader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vshader, 1, (const GLchar **) &vsource, NULL);
	glCompileShader(vshader);
	fshader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fshader, 1, (const GLchar **) &fsource, NULL);
	glCompileShader(fshader);

	glGetShaderiv(fshader, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE) {
		glGetShaderiv(fshader, GL_INFO_LOG_LENGTH, &max_len);
		msg = malloc(max_len);
		glGetShaderInfoLog(fshader, max_len,
				&len, msg);
		fprintf(stderr, "Failed to compile fragment shader: %s!\n", msg);
		free(msg);
		exit(EXIT_FAILURE);
	}

	cell_shader = glCreateProgram();
	glAttachShader(cell_shader, vshader);
	glAttachShader(cell_shader, fshader);
	glDeleteShader(vshader);
	glDeleteShader(fshader);
	glLinkProgram(cell_shader);

	glUseProgram(cell_shader);
	
	for (size_t i = CELLS_Y; i < CELL_COLS + CELLS_Y; i++) {
		for (size_t j = CELLS_X; j < CELL_ROWS + CELLS_X; j++) {
			float x = (j-CELLS_X) * CELL_W;
			float y = (i-CELLS_Y) * CELL_H;
			cells[j + i*CELL_ROWS] = (struct cell_t) { (vec2_t) { x, y }, DEAD, 0 };
		}
	}

	glGenVertexArrays(1, buffers+BUFFER_VAO);
	glBindVertexArray(buffers[BUFFER_VAO]);

	const vec2_t vertices[4] = {
		{ 0.0, 0.0 },
		{ 1.0, 0.0 },
		{ 1.0, 1.0 },
		{ 0.0, 1.0 },
	};

	const GLuint indices[6] = {
		0, 1, 2,
		2, 3, 0,
	};

	glGenBuffers(2, buffers);
	glBindBuffer(GL_ARRAY_BUFFER, buffers[BUFFER_VBO]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[BUFFER_EBO]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(0);

	ll_matrix_mode(LL_MATRIX_PROJECTION);
	glUniformMatrix4fv(glGetUniformLocation(cell_shader, "projection"), 1, GL_FALSE,
			   ll_matrix_get_copy().data);

	running = 1;
	active_state = edit;
	while (running) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				running = 0;
				break;
			} else if (event.type == SDL_MOUSEMOTION) {
				size_t current_x = (event.motion.x + CELL_W) / CELL_W;
				size_t current_y = (event.motion.y + CELL_H) / CELL_H;
				if (current_y < 0 || current_y >= CELL_COLS + 2
				    || current_x < 0 || current_x >= CELL_ROWS + 2)
					break;
				mouse_x = current_x;
				mouse_y = current_y;
				current_cell = mouse_y*CELL_ROWS + mouse_x;
				
				cells[previous_cell].hovered = 0;
				previous_cell = current_cell;
				cells[current_cell].hovered = 1;
			} else if (event.type == SDL_MOUSEBUTTONDOWN) {
				switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					left_btn = 1;
					break;
				}
			} else if (event.type == SDL_MOUSEBUTTONUP) {
				switch (event.button.button) {
				case SDL_BUTTON_LEFT:
					left_btn = 0;
					break;
				}
			} else if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
				case SDLK_a:
					active_state = simulate;
					break;
				case SDLK_ESCAPE:
					running = 0;
					break;
				}
			}
		}

		
		glClear(GL_COLOR_BUFFER_BIT);
		active_state();
		SDL_GL_SwapWindow(window);
		
	}

	SDL_DestroyWindow(window);
	SDL_GL_DeleteContext(context);
	SDL_Quit();
	return 0;
}


static void
edit(void)
{
	if (left_btn) {
		if (current_cell != changed_cell) {
			cells[current_cell].state = ALIVE - cells[current_cell].state;
		}
		changed_cell = current_cell;
	}

	ll_matrix_mode(LL_MATRIX_MODEL);

	for (size_t i = CELLS_Y; i < CELL_COLS + CELLS_Y; i++) {
		for (size_t j = CELLS_X; j < CELL_ROWS + CELLS_X; j++) {
			struct cell_t cell = cells[j + i*CELL_ROWS];
			ll_matrix_identity();
			ll_matrix_scale3f(CELL_W, CELL_H, 1.0);
			ll_matrix_translate3f(cell.pos.x, cell.pos.y, 0.0);
			glUniformMatrix4fv(glGetUniformLocation(cell_shader, "model"), 1, GL_FALSE,
					   ll_matrix_get_copy().data);
			glUniform1i(glGetUniformLocation(cell_shader, "state"), cell.state);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
		}
	}
}

static int
get_alive_neighbours(size_t x, size_t y)
{
	int count = 0;

	for (int i = -1; i <= 1; i++) {
		for (int j = -1; j <= 1; j++) {
			if (i == 0 && j == 0) continue;
			struct cell_t cell = cells[(x + j) + (y + i) * CELL_ROWS];
			if (cell.state == ALIVE)
				count++;
		}
	}

	return count;
}

static void
simulate(void)
{
	#define FPS 60.0

	Uint32 start_time, end_time;
	float elapsed;
	
	start_time = SDL_GetTicks();
	
	struct cell_t new_cells[CELL_COUNT];
	memcpy(new_cells, cells, sizeof(new_cells));
	for (size_t i = CELLS_Y; i < CELL_COLS + CELLS_Y; i++) {
		for (size_t j = CELLS_X; j < CELL_ROWS + CELLS_X; j++) {
			size_t pos = j + i*CELL_ROWS;
			int alive_neighbours = get_alive_neighbours(j, i);
			new_cells[pos] = cells[pos];
			if (cells[pos].state == ALIVE) {
				if (alive_neighbours < 2 || alive_neighbours > 3) {
					new_cells[pos].state = DEAD;
				} else {
					new_cells[pos].state = ALIVE;
				}
			} else {
				if (alive_neighbours == 3) {
					new_cells[pos].state = ALIVE;
				}
			}
		}
	}

	ll_matrix_mode(LL_MATRIX_MODEL);

	for (size_t i = CELLS_Y; i < CELL_COLS + CELLS_Y; i++) {
		for (size_t j = CELLS_X; j < CELL_ROWS + CELLS_X; j++) {
			cells[j + i*CELL_ROWS] = new_cells[j + i*CELL_ROWS];
			struct cell_t cell = cells[j + i*CELL_ROWS];
			ll_matrix_identity();
			ll_matrix_scale3f(CELL_W, CELL_H, 1.0);
			ll_matrix_translate3f(cell.pos.x, cell.pos.y, 0.0);
			glUniformMatrix4fv(glGetUniformLocation(cell_shader, "model"), 1, GL_FALSE,
					   ll_matrix_get_copy().data);
			glUniform1i(glGetUniformLocation(cell_shader, "state"), cell.state);
			glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
		}
	}

	end_time = SDL_GetTicks();
	elapsed = (end_time - start_time) / 1000.0;
	float given = elapsed - (1.0 / FPS);
	if (given > 0.001) {
		SDL_Delay(given * 1000);
	}
}
