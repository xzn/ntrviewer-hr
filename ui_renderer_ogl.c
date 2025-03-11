#include "ui_common_sdl.h"
#include "ui_renderer_ogl.h"
#include "main.h"
#ifdef _WIN32
#include "ui_compositor_csc.h"
#include "glad/glad_wgl.h"
#include <SDL2/SDL_syswm.h>
#endif
#include "nuklear_sdl_gl3.h"
#include "nuklear_sdl_gles2.h"
#include "ui_main_nk.h"
#include "placebo.h"
#include "rashader.h"
#include <libplacebo/opengl.h>

SDL_Window *ogl_win[SCREEN_COUNT];
static SDL_Window *csc_win[SCREEN_COUNT];
static struct nk_context *nk_ctx;

SDL_GLContext gl_context[SCREEN_COUNT];
static int ogl_version_major, ogl_version_minor;
static bool gl_use_vao;

static GLuint gl_vao[SCREEN_COUNT][SCREEN_COUNT];
static GLuint gl_vbo[SCREEN_COUNT][SCREEN_COUNT];
static GLuint gl_ebo[SCREEN_COUNT];

static GLuint gl_vao_fbo[SCREEN_COUNT];
static GLuint gl_vbo_fbo[SCREEN_COUNT];
static GLuint gl_ebo_fbo[SCREEN_COUNT];

#ifdef _WIN32
static HWND ogl_hwnd[SCREEN_COUNT];
static HDC ogl_hdc[SCREEN_COUNT];
#endif

static GLuint gl_fbo_sc[SCREEN_COUNT];

#define GLES_GLSL_VERSION "#version 100\n" "precision highp float;\n"
#define OGL_GLSL_VERSION "#version 110\n"
#define vs_str \
    "attribute vec4 a_position;\n" \
    "attribute vec2 a_texCoord;\n" \
    "varying vec2 v_texCoord;\n" \
    "void main()\n" \
    "{\n" \
    " gl_Position = a_position;\n" \
    " v_texCoord = a_texCoord;\n" \
    "}\n"

#define fs_str \
    "varying vec2 v_texCoord;\n" \
    "uniform sampler2D s_texture;\n" \
    "void main()\n" \
    "{\n" \
    " vec4 color = texture2D(s_texture, v_texCoord);\n" \
    " if (color != vec4(0.0))\n" \
    "  color = vec4(color.rgb * (15.0 / 16.0), 15.0 / 16.0);\n" \
    " gl_FragColor = color;\n" \
    "}\n"

#define GLES_GLSL_FBO_VERSION "#version 300 es\n" "precision highp float;\n" "precision highp sampler3D;\n"
#define OGL_GLSL_FBO_VERSION "#version 130\n"
#define vs_str_fbo \
    "in vec4 a_position;\n" \
    "in vec2 a_texCoord;\n" \
    "out vec2 v_texCoord;\n" \
    "void main()\n" \
    "{\n" \
    " gl_Position = a_position;\n" \
    " v_texCoord = a_texCoord;\n" \
    "}\n"

#ifdef _WIN32
#define fs_str_fbo_0 \
    "  texture(s_texture, vec3(v_texCoord, 2.0 / 3.0)).x / 255.0," \
    "  texture(s_texture, vec3(v_texCoord, 1.0 / 3.0)).x / 255.0," \
    "  texture(s_texture, vec3(v_texCoord, 0.0)).x / 255.0,"
#else
#define fs_str_fbo_0 \
    "  texture(s_texture, vec3(v_texCoord, 0.0)).x / 255.0," \
    "  texture(s_texture, vec3(v_texCoord, 1.0 / 3.0)).x / 255.0," \
    "  texture(s_texture, vec3(v_texCoord, 2.0 / 3.0)).x / 255.0,"
#endif
#define fs_str_fbo \
    "in vec2 v_texCoord;\n" \
    "uniform sampler3D s_texture;\n" \
    "out vec4 fragColor;\n" \
    "void main()\n" \
    "{\n" \
    " fragColor = vec4(" \
    fs_str_fbo_0 \
    "  texture(s_texture, vec3(v_texCoord, 1.0)).x / 255.0" \
    " );\n" \
    "}\n"

static GLfloat fbo_vertices_pos[4][3] = {
    {-1.f, -1.f, 0.0f}, // Position 1
    {-1.f, 1.f, 0.0f},  // Position 0
    {1.f, -1.f, 0.0f},  // Position 2
    {1.f, 1.f, 0.0f},   // Position 3
};

static GLfloat fbo_vertices_tex_coord[4][2] = {
    {0.0f, 0.0f}, // TexCoord 2
    {0.0f, 1.0f}, // TexCoord 1
    {1.0f, 0.0f}, // TexCoord 0
    {1.0f, 1.0f}, // TexCoord 3
};
static GLushort fbo_indices[] =
    {0, 1, 2, 1, 2, 3};

UNUSED static GLfloat vertices_pos[4][3] = {
  { -0.5f, 0.5f, 0.0f },  // Position 0
  { -0.5f, -0.5f, 0.0f }, // Position 1
  { 0.5f, -0.5f, 0.0f },  // Position 2
  { 0.5f, 0.5f, 0.0f },   // Position 3
};

static GLfloat vertices_tex_coord[4][2] = {
    {1.0f, 0.0f}, // TexCoord 2
    {0.0f, 0.0f}, // TexCoord 1
    {0.0f, 1.0f}, // TexCoord 0
    {1.0f, 1.0f}, // TexCoord 3
};
static GLushort indices[] =
    {0, 1, 2, 0, 2, 3};

struct vao_vertice_t {
    GLfloat pos[3];
    GLfloat tex_coord[2];
};

static GLuint load_shader(GLenum type, const char *shaderSrc)
{
    GLuint shader;
    GLint compiled;

    // Create the shader object
    shader = glCreateShader(type);

    if (shader == 0)
        return 0;

    // Load the shader source
    glShaderSource(shader, 1, &shaderSrc, NULL);

    // Compile the shader
    glCompileShader(shader);

    // Check the compile status
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        GLint info_len = 0;

        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);

        if (info_len > 1) {
            char *info_log = malloc(sizeof(char) * info_len);

            glGetShaderInfoLog(shader, info_len, NULL, info_log);
            err_log("Error compiling shader: %s\n", info_log);

            free(info_log);
        }

        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

static GLuint Load_program(const char *vs_src, const char *fs_src)
{
    GLuint vs;
    GLuint fs;
    GLuint prog;
    GLint linked;

    // Load the vertex/fragment shaders
    vs = load_shader(GL_VERTEX_SHADER, vs_src);
    if (vs == 0)
        return 0;

    fs = load_shader(GL_FRAGMENT_SHADER, fs_src);
    if (fs == 0) {
        glDeleteShader(vs);
        return 0;
    }

    // Create the program object
    prog = glCreateProgram();

    if (prog == 0)
        return 0;

    glAttachShader(prog, vs);
    glAttachShader(prog, fs);

    // Link the program
    glLinkProgram(prog);

    // Check the link status
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);

    if (!linked) {
        GLint info_len = 0;

        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &info_len);

        if (info_len > 1) {
            char *info_log = malloc(sizeof(char) * info_len);

            glGetProgramInfoLog(prog, info_len, NULL, info_log);
            err_log("Error linking program: %s\n", info_log);

            free(info_log);
        }

        glDeleteProgram(prog);
        return 0;
    }

    // Free up no longer needed shader resources
    glDeleteShader(vs);
    glDeleteShader(fs);

    return prog;
}

GLuint gl_program[SCREEN_COUNT];
GLint gl_position_loc[SCREEN_COUNT];
GLint gl_tex_coord_loc[SCREEN_COUNT];
GLint gl_sampler_loc[SCREEN_COUNT];

GLuint gl_fbo_program[SCREEN_COUNT];
GLint gl_fbo_position_loc[SCREEN_COUNT];
GLint gl_fbo_tex_coord_loc[SCREEN_COUNT];
GLint gl_fbo_sampler_loc[SCREEN_COUNT];

static void on_gl_error(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar *message, const void *)
{
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION)
        fprintf(stderr, "gl_error: %u:%u:%u:%u:%u: %s\n", source, type, id, severity, length, message);
}

static int ogl_res_init(void) {
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        SDL_GL_MakeCurrent(ogl_win[j], gl_context[j]);

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            glGenTextures(1, &rp_buffer_ctx[i].gl_tex[j]);
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            glGenTextures(1, &rp_buffer_ctx[i].gl_tex_upscaled[j]);
        }

        if (is_renderer_csc()) {
            glGenFramebuffers(1, &gl_fbo_sc[j]);
        }

        if (is_renderer_gles()) {
            gl_program[j] = Load_program(GLES_GLSL_VERSION vs_str, GLES_GLSL_VERSION fs_str);
        } else {
            gl_program[j] = Load_program(OGL_GLSL_VERSION vs_str, OGL_GLSL_VERSION fs_str);
        }
        gl_position_loc[j] = glGetAttribLocation(gl_program[j], "a_position");
        gl_tex_coord_loc[j] = glGetAttribLocation(gl_program[j], "a_texCoord");
        gl_sampler_loc[j] = glGetUniformLocation(gl_program[j], "s_texture");

        if (1) {
            if (is_renderer_gles()) {
                gl_fbo_program[j] = Load_program(GLES_GLSL_FBO_VERSION vs_str_fbo, GLES_GLSL_FBO_VERSION fs_str_fbo);
            } else {
                gl_fbo_program[j] = Load_program(OGL_GLSL_FBO_VERSION vs_str_fbo, OGL_GLSL_FBO_VERSION fs_str_fbo);
            }
            gl_fbo_position_loc[j] = glGetAttribLocation(gl_fbo_program[j], "a_position");
            gl_fbo_tex_coord_loc[j] = glGetAttribLocation(gl_fbo_program[j], "a_texCoord");
            gl_fbo_sampler_loc[j] = glGetUniformLocation(gl_fbo_program[j], "s_texture");
        }

        if (gl_use_vao) {
            for (int i = 0; i < SCREEN_COUNT; ++i) {
                glGenVertexArrays(1, &gl_vao[j][i]);
                glGenBuffers(1, &gl_vbo[j][i]);
            }
            glGenBuffers(1, &gl_ebo[j]);

            glGenVertexArrays(1, &gl_vao_fbo[j]);
            glGenBuffers(1, &gl_vbo_fbo[j]);
            glGenBuffers(1, &gl_ebo_fbo[j]);

            for (int i = 0; i < SCREEN_COUNT; ++i) {
                glBindVertexArray(gl_vao[j][i]);
                glBindBuffer(GL_ARRAY_BUFFER, gl_vbo[j][i]);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_ebo[j]);
                if (i == SCREEN_TOP)
                    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
                glEnableVertexAttribArray(gl_position_loc[j]);
                glEnableVertexAttribArray(gl_tex_coord_loc[j]);
                glVertexAttribPointer(gl_position_loc[j], 3, GL_FLOAT, GL_FALSE, sizeof(struct vao_vertice_t), (const void *)offsetof(struct vao_vertice_t, pos));
                glVertexAttribPointer(gl_tex_coord_loc[j], 2, GL_FLOAT, GL_FALSE, sizeof(struct vao_vertice_t), (const void *)offsetof(struct vao_vertice_t, tex_coord));
            }

            glBindVertexArray(gl_vao_fbo[j]);
            glBindBuffer(GL_ARRAY_BUFFER, gl_vbo_fbo[j]);
            struct vao_vertice_t fbo_vertices[4];
            for (int i = 0; i < 4; ++i) {
                memcpy(fbo_vertices[i].pos, fbo_vertices_pos[i], sizeof(fbo_vertices[i].pos));
                memcpy(fbo_vertices[i].tex_coord, fbo_vertices_tex_coord[i], sizeof(fbo_vertices[i].tex_coord));
            }
            glBufferData(GL_ARRAY_BUFFER, sizeof(fbo_vertices), fbo_vertices, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_ebo_fbo[j]);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(fbo_indices), fbo_indices, GL_STATIC_DRAW);

            glEnableVertexAttribArray(gl_fbo_position_loc[j]);
            glEnableVertexAttribArray(gl_fbo_tex_coord_loc[j]);
            glVertexAttribPointer(gl_fbo_position_loc[j], 3, GL_FLOAT, GL_FALSE, sizeof(struct vao_vertice_t), (const void *)offsetof(struct vao_vertice_t, pos));
            glVertexAttribPointer(gl_fbo_tex_coord_loc[j], 2, GL_FLOAT, GL_FALSE, sizeof(struct vao_vertice_t), (const void *)offsetof(struct vao_vertice_t, tex_coord));
        }
    }

    SDL_GL_MakeCurrent(NULL, NULL);
    return 0;
}

static void ogl_res_destroy(void)
{
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        if (!gl_context[j])
            continue;

        SDL_GL_MakeCurrent(ogl_win[j], gl_context[j]);
        if (gl_use_vao) {
            for (int i = 0; i < SCREEN_COUNT; ++i) {
                if (gl_vbo[j][i]) {
                    glDeleteBuffers(1, &gl_vbo[j][i]);
                    gl_vbo[j][i] = 0;
                }
            }
            if (gl_ebo[j]) {
                glDeleteBuffers(1, &gl_ebo[j]);
                gl_ebo[j] = 0;
            }
            if (gl_vbo_fbo[j]) {
                glDeleteBuffers(1, &gl_vbo_fbo[j]);
                gl_vbo_fbo[j] = 0;
            }
            if (gl_ebo_fbo[j]) {
                glDeleteBuffers(1, &gl_ebo_fbo[j]);
                gl_ebo_fbo[j] = 0;
            }
            for (int i = 0; i < SCREEN_COUNT; ++i) {
                if (gl_vao[j][i]) {
                    glDeleteVertexArrays(1, &gl_vao[j][i]);
                    gl_vao[j][i] = 0;
                }
            }
            if (gl_vao_fbo[j]) {
                glDeleteVertexArrays(1, &gl_vao_fbo[j]);
                gl_vao_fbo[j] = 0;
            }
        }
        if (gl_fbo_program[j]) {
            glDeleteProgram(gl_fbo_program[j]);
            gl_fbo_program[j] = 0;
        }
        if (gl_program[j]) {
            glDeleteProgram(gl_program[j]);
            gl_program[j] = 0;
        }

        if (is_renderer_csc()) {
            if (gl_fbo_sc[j]) {
                glDeleteFramebuffers(1, &gl_fbo_sc[j]);
                gl_fbo_sc[j] = 0;
            }
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            if (rp_buffer_ctx[i].gl_tex_upscaled[j]) {
                glDeleteTextures(1, &rp_buffer_ctx[i].gl_tex_upscaled[j]);
                rp_buffer_ctx[i].gl_tex_upscaled[j] = 0;
            }

            if (rp_buffer_ctx[i].gl_tex[j]) {
                glDeleteTextures(1, &rp_buffer_ctx[i].gl_tex[j]);
                rp_buffer_ctx[i].gl_tex[j] = 0;
            }
        }
    }
}

enum {
    UPSCALING_DEFAULT_NONE = 0,
    UPSCALING_DEFAULT_COUNT,
};

#define PLACEBO_UI_INDEX(mode) (UPSCALING_DEFAULT_COUNT + mode)
#define PLACEBO_MODE(ui_index) (ui_index - PLACEBO_UI_INDEX(0))
#define IS_PLACEBO(ui_index) (PLACEBO_MODE(ui_index) >= 0 && PLACEBO_MODE(ui_index) < placebo_count)

static struct placebo_t *placebo;
static int placebo_count;
static struct placebo_render_t *placebo_render[SCREEN_COUNT][SCREEN_COUNT];
static int placebo_render_mode[SCREEN_COUNT][SCREEN_COUNT];

static pl_opengl pl_ogl_dev[SCREEN_COUNT];
static pl_log pl_log_dev;

#define RASHADER_UI_INDEX(mode) (UPSCALING_DEFAULT_COUNT + placebo_count + mode)
#define RASHADER_MODE(ui_index) (ui_index - RASHADER_UI_INDEX(0))
#define IS_RASHADER(ui_index) (RASHADER_MODE(ui_index) >= 0 && RASHADER_MODE(ui_index) < rashader_count)

static struct rashader_t *rashader;
static int rashader_count;
static struct rashader_render_t *rashader_render[SCREEN_COUNT][SCREEN_COUNT];
static int rashader_render_mode[SCREEN_COUNT][SCREEN_COUNT];

static void ogl_upscaling_update(int ctx_top_bot) {
    int i = ctx_top_bot;

    rp_lock_wait(upscaling_update_lock);

    if (i == SCREEN_TOP) {
    }

    rp_lock_rel(upscaling_update_lock);
}

static int ogl_upscaling_init(void) {
    bool use_placebo = true;
    bool use_rashader = is_renderer_ogl();

    ui_upscaling_filter_count = UPSCALING_DEFAULT_COUNT;

    pl_log_dev = placebo_log_create();
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        SDL_GL_MakeCurrent(ogl_win[j], gl_context[j]);

        pl_ogl_dev[j] = pl_opengl_create(pl_log_dev, pl_opengl_params(
            .get_proc_addr = (pl_voidfunc_t (*)(const char *))SDL_GL_GetProcAddress,
        ));
        if (!pl_ogl_dev[j]) {
            use_placebo = false;
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            placebo_render_mode[j][i] = -1;
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            rashader_render_mode[j][i] = -1;
        }
    }
    SDL_GL_MakeCurrent(NULL, NULL);

    if (use_placebo) {
        placebo = placebo_load("placebo.json");
        if (placebo) {
            placebo_count = placebo_mode_count(placebo);
            ui_upscaling_filter_count += placebo_count;
        }
    }

    if (use_rashader) {
        rashader = rashader_load("rashader.json");
        if (rashader) {
            rashader_count = rashader_mode_count(rashader);
            ui_upscaling_filter_count += rashader_count;
        }
    }

    ui_upscaling_filter_options = malloc(ui_upscaling_filter_count * sizeof(*ui_upscaling_filter_options));
    if (!ui_upscaling_filter_options) {
        return -1;
    }

    ui_upscaling_filter_options[UPSCALING_DEFAULT_NONE] = NK_UPSCALE_TYPE_TEXT_NONE "None";

    for (int i = 0; i < placebo_count; ++i) {
        ui_upscaling_filter_options[PLACEBO_UI_INDEX(i)] = placebo_mode_name(placebo, i, NK_UPSCALE_TYPE_TEXT_PLACEBO);
    }

    for (int i = 0; i < rashader_count; ++i) {
        ui_upscaling_filter_options[RASHADER_UI_INDEX(i)] = rashader_mode_name(rashader, i, NK_UPSCALE_TYPE_TEXT_RASHADER);
    }

    ui_upscaling_selected = 0;

    return 0;
}

static void ogl_filter_chain_free(void *fc);
static void ogl_upscaling_close(void) {
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        SDL_GL_MakeCurrent(ogl_win[j], gl_context[j]);

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            if (placebo_render[j][i]) {
                placebo_render_close(placebo_render[j][i]);
                placebo_render[j][i] = 0;
            }
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            if (rashader_render[j][i]) {
                rashader_render_close(rashader_render[j][i], ogl_filter_chain_free);
                rashader_render[j][i] = 0;
            }
        }

        pl_opengl_destroy(&pl_ogl_dev[j]);
    }
    SDL_GL_MakeCurrent(NULL, NULL);
    placebo_log_destroy(&pl_log_dev);

    if (placebo) {
        placebo_unload(placebo);
        placebo = 0;
    }
    placebo_count = 0;

    if (rashader) {
        rashader_unload(rashader);
        rashader = 0;
    }
    rashader_count = 0;

    if (ui_upscaling_filter_options) {
        free(ui_upscaling_filter_options);
        ui_upscaling_filter_options = 0;
    }
    ui_upscaling_filter_count = 0;
}

static int ogl_renderer_init(void) {
    if (is_renderer_gles()) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    } else {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        // SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        // SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    }
    if (is_renderer_ogl_dbg) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    }
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    gl_context[SCREEN_TOP] = SDL_GL_CreateContext(ogl_win[SCREEN_TOP]);
    if (!gl_context[SCREEN_TOP]) {
        err_log("SDL_GL_CreateContext: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetSwapInterval(1);

    if (is_renderer_gles()) {
        if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress)) {
            err_log("gladLoadGLES2Loader failed\n");
            return -1;
        }
    } else {
        if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
            err_log("gladLoadGLLoader failed\n");
            return -1;
        }
    }

    if (is_renderer_gles_angle()) {
        renderer_single_thread = 1;
    }

    if (!is_renderer_gles()) {
        gl_use_vao = 1;
    }

    if (is_renderer_csc()) {
#ifdef _WIN32
        for (int i = 0; i < SCREEN_COUNT; ++i) {
            SDL_SysWMinfo wmInfo;

            SDL_VERSION(&wmInfo.version);
            SDL_GetWindowWMInfo(ogl_win[i], &wmInfo);

            ogl_hwnd[i] = wmInfo.info.win.window;
            ogl_hdc[i] = wmInfo.info.win.hdc;
        }

        if (!gladLoadWGLLoader((GLADloadproc)SDL_GL_GetProcAddress, ogl_hdc[SCREEN_TOP])) {
            err_log("gladLoadWGLLoader failed\n");
        } else if (!(GLAD_WGL_NV_DX_interop && GLAD_WGL_NV_DX_interop2)) {
            err_log("WGL DX interop not available\n");
            return -1;
        }

        if (dxgi_init())
            return -1;
        if (composition_swapchain_init(ui_hwnd))
            return -1;
        ui_compositing = 1;
#endif
    }

    if (is_renderer_ogl_dbg) {
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(on_gl_error, NULL);
    }

    err_log("ogl version string: %s\n", glGetString(GL_VERSION));
    glGetIntegerv(GL_MAJOR_VERSION, &ogl_version_major);
    glGetIntegerv(GL_MINOR_VERSION, &ogl_version_minor);
    err_log("ogl version: %d.%d\n", ogl_version_major, ogl_version_minor);

    if (is_renderer_gles()) {
        nk_ctx = nk_sdl_gles2_init(ogl_win[SCREEN_TOP]);
    } else {
        nk_ctx = nk_sdl_gl3_init(ogl_win[SCREEN_TOP]);
    }
    if (!nk_ctx)
        return -1;

    if (renderer_single_thread) {
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    }

    gl_context[SCREEN_BOT] = SDL_GL_CreateContext(ogl_win[SCREEN_BOT]);
    if (!gl_context[SCREEN_BOT]) {
        err_log("SDL_GL_CreateContext: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetSwapInterval(1);

    if (ogl_res_init())
        return -1;

    if (ogl_upscaling_init())
        return -1;

    ui_upscaling_filters = 1;

    err_log("%s %s\n", is_renderer_ogl() ? "ogl" : is_renderer_gles_angle() ? "angle" : "gles", is_renderer_csc() ? "composition swapchain" : "");

    return 0;
}

static void ogl_renderer_destroy(void) {
    ui_upscaling_filters = 0;

    ogl_upscaling_close();

    ogl_res_destroy();

    if (nk_ctx) {
        if (is_renderer_gles()) {
            nk_sdl_gl3_shutdown();
        } else {
            nk_sdl_gles2_shutdown();
        }
        nk_ctx = 0;
    }

    if (is_renderer_csc()) {
#ifdef _WIN32
        ui_compositing = 0;
        composition_swapchain_close();
        dxgi_close();
#endif
    }

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        if (gl_context[i]) {
            SDL_GL_DeleteContext(gl_context[i]);
            gl_context[i] = NULL;
        }
    }
}

int ui_renderer_ogl_init(void) {
    if (sdl_win_init(ogl_win, 1)) {
        return -1;
    }

    if (is_renderer_csc()) {
        if (sdl_win_init(csc_win, 1)) {
            return -1;
        }
        for (int i = 0; i < SCREEN_COUNT; ++i)
            ui_sdl_win[i] = csc_win[i];
    } else {
        for (int i = 0; i < SCREEN_COUNT; ++i)
            ui_sdl_win[i] = ogl_win[i];
    }

    sdl_set_wminfo();

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        ui_win_width_drawable[i] = WIN_WIDTH_DEFAULT;
        ui_win_height_drawable[i] = WIN_HEIGHT_DEFAULT;
        ui_win_scale[i] = 1.0f;
    }

    if (ogl_renderer_init()) {
        return -1;
    }

    ui_nk_ctx = nk_ctx;

    return 0;
}

void ui_renderer_ogl_destroy(void) {
    ui_nk_ctx = NULL;

    ogl_renderer_destroy();

    sdl_reset_wminfo();

    for (int i = 0; i < SCREEN_COUNT; ++i)
        ui_sdl_win[i] = NULL;

    if (is_renderer_csc()) {
        sdl_win_destroy(csc_win);
    }
    sdl_win_destroy(ogl_win);
}

#ifdef _WIN32
static GLuint tex_sc[SCREEN_COUNT];
static HANDLE handle_sc[SCREEN_COUNT];
#endif

void ui_renderer_ogl_main(int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, bool win_shared, float bg[4]) {
    int i = ctx_top_bot;
    int p = win_shared ? screen_top_bot : i;

    if (renderer_single_thread) {
        SDL_GL_MakeCurrent(ogl_win[p], gl_context[p]);
    }

    GLenum gl_err;
    while ((gl_err = glGetError()) != GL_NO_ERROR) {
        err_log("gl error: %d\n", (int)gl_err);
        if (gl_err == GL_OUT_OF_MEMORY) {
            err_log("gl error unrecoverable, shutting down\n");
            program_running = 0;
        }
    }

    ogl_upscaling_update(i);

#ifdef _WIN32
    if (is_renderer_csc()) {
        sc_fail[p] = 0;
        ui_compositor_csc_main(screen_top_bot, i, win_shared);
        if (sc_fail[p]) {
            return;
        }

        struct render_buffer_t *sc_render_buf = &render_buffers[i][screen_top_bot];
        if (render_buffer_get(sc_render_buf, i, ui_ctx_width[p], ui_ctx_height[p], &tex_sc[p], &handle_sc[p]) != 0) {
            ui_compositing = 0;
            sc_fail[p] = 1;
            return;
        }
        if (!wglDXLockObjectsNV(gl_d3ddevice[i], 1, &handle_sc[p])) {
            err_log("wglDXLockObjectsNV failed: %d\n", (int)GetLastError());
            ui_compositing = 0;
            sc_fail[p] = 1;
            return;
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_fbo_sc[screen_top_bot]);
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, tex_sc[p]);
    }
#endif

    glViewport(0, 0, ui_ctx_width[p], ui_ctx_height[p]);
    if (!win_shared) {
        glClearColor(bg[0], bg[1], bg[2], bg[3]);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    if (view_mode == VIEW_MODE_TOP_BOT && !win_shared) {
        draw_screen(&rp_buffer_ctx[SCREEN_TOP], SCREEN_HEIGHT0, SCREEN_WIDTH, SCREEN_TOP, i, view_mode, 0);
        draw_screen(&rp_buffer_ctx[SCREEN_BOT], SCREEN_HEIGHT1, SCREEN_WIDTH, SCREEN_BOT, i, view_mode, 0);
    } else if (view_mode == VIEW_MODE_BOT) {
        draw_screen(&rp_buffer_ctx[SCREEN_BOT], SCREEN_HEIGHT1, SCREEN_WIDTH, SCREEN_BOT, i, view_mode, win_shared);
    } else {
        if (!draw_screen(&rp_buffer_ctx[screen_top_bot], screen_top_bot == SCREEN_TOP ? SCREEN_HEIGHT0 : SCREEN_HEIGHT1, SCREEN_WIDTH, screen_top_bot, i, view_mode, win_shared)) {
            glClearColor(bg[0], bg[1], bg[2], bg[3]);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }
}

static int placebo_upscaling_update(int selected, int ctx_top_bot, int screen_top_bot) {
    int i = ctx_top_bot;

    int render_mode = -1;
    bool reset_mode = 0;
    if (selected >= 0) {
        render_mode = selected;
    } else {
        reset_mode = 1;
    }

    if (
        placebo_render[i][screen_top_bot] && (
            placebo_render_mode[i][screen_top_bot] != render_mode ||
            reset_mode
        )
    ) {
        placebo_render_close(placebo_render[i][screen_top_bot]);
        placebo_render[i][screen_top_bot] = 0;
    }

    if (!reset_mode && !placebo_render[i][screen_top_bot] && render_mode >= 0 && placebo) {
        placebo_render[i][screen_top_bot] = placebo_render_init(placebo, render_mode, pl_ogl_dev[i]->gpu, pl_log_dev);
        if (!placebo_render[i][screen_top_bot]) {
            err_log("placebo_render_init failed\n");
            goto fail;
        }

        placebo_render_mode[i][screen_top_bot] = render_mode;
    }

fail:
    return reset_mode;
}

static void *ogl_filter_chain_create(libra_shader_preset_t *preset) {
    bool ogl460 = ogl_version_major > 4 || (ogl_version_major == 4 && ogl_version_minor >= 6);
    struct filter_chain_gl_opt_t opt = {
        .version = libra_instance_api_version(),
        .use_dsa = ogl460,
        .glsl_version = ogl460 ? 460 : 330,
    };
    libra_gl_filter_chain_t out;
    libra_error_t err = libra_gl_filter_chain_create(preset, (libra_gl_loader_t)SDL_GL_GetProcAddress, &opt, &out);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
        return NULL;
    }
    return out;
}

static void ogl_filter_chain_free(void *fc) {
    libra_error_t err = libra_gl_filter_chain_free((libra_gl_filter_chain_t *)fc);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
    }
}

static int rashader_upscaling_update(int selected, int ctx_top_bot, int screen_top_bot) {
    int i = ctx_top_bot;

    int render_mode = -1;
    bool reset_mode = 0;
    if (selected >= 0) {
        render_mode = selected;
    } else {
        reset_mode = 1;
    }

    if (
        rashader_render[i][screen_top_bot] && (
            rashader_render_mode[i][screen_top_bot] != render_mode ||
            reset_mode
        )
    ) {
        rashader_render_close(rashader_render[i][screen_top_bot], ogl_filter_chain_free);
        rashader_render[i][screen_top_bot] = 0;

        GLint i_max; 
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &i_max);
        for (int i = 0; i < i_max; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindSampler(i, 0);
        }
    }

    static libra_preset_ctx_t ctx = 0;
    if (!reset_mode && !rashader_render[i][screen_top_bot] && render_mode >= 0) {
        libra_error_t err = libra_preset_ctx_create(&ctx);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            ctx = 0;
            goto fail;
        }
        err = libra_preset_ctx_set_runtime(&ctx, LIBRA_PRESET_CTX_RUNTIME_GL_CORE);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            goto fail;
        }

        rashader_render[i][screen_top_bot] = rashader_render_init(rashader, render_mode, &ctx, ogl_filter_chain_create);
        if (!rashader_render[i][screen_top_bot]) {
            err_log("rashader_render_init failed\n");
            goto fail;
        }

        rashader_render_mode[i][screen_top_bot] = render_mode;
    }

fail:
    if (ctx)
        libra_preset_ctx_free(&ctx);
    return reset_mode;
}

void ui_renderer_ogl_draw(struct rp_buffer_ctx_t *ctx, uint8_t *data, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, int win_shared)
{
    double ctx_left_f;
    double ctx_top_f;
    double ctx_right_f;
    double ctx_bot_f;
    int ctx_width;
    int ctx_height;
    int win_width_drawable;
    int win_height_drawable;
    draw_screen_get_dims(
        screen_top_bot, ctx_top_bot, win_shared, view_mode, width, height,
        &ctx_left_f, &ctx_top_f, &ctx_right_f, &ctx_bot_f, &ctx_width, &ctx_height, &win_width_drawable, &win_height_drawable);

    int i = ctx_top_bot;
    if (win_shared) {
        i = screen_top_bot;;
    }
    GLfloat vertices_pos[4][3] = {0};
    vertices_pos[0][0] = ctx_left_f;
    vertices_pos[0][1] = ctx_top_f;
    vertices_pos[1][0] = ctx_left_f;
    vertices_pos[1][1] = ctx_bot_f;
    vertices_pos[2][0] = ctx_right_f;
    vertices_pos[2][1] = ctx_bot_f;
    vertices_pos[3][0] = ctx_right_f;
    vertices_pos[3][1] = ctx_top_f;
    if (is_renderer_csc()) {
        vertices_pos[0][1] = ctx_bot_f;
        vertices_pos[1][1] = ctx_top_f;
        vertices_pos[2][1] = ctx_top_f;
        vertices_pos[3][1] = ctx_bot_f;
    }
    struct vao_vertice_t vertices[4] = {};
    if (gl_use_vao) {
        for (int i = 0; i < 4; ++i) {
            memcpy(vertices[i].pos, vertices_pos[i], sizeof(vertices[i].pos));
            memcpy(vertices[i].tex_coord, vertices_tex_coord[i], sizeof(vertices[i].tex_coord));
        }
    }
    GLuint tex = ctx->gl_tex[i];

    int upscaling_selected = ui_upscaling_selected;
    struct pl_opengl_wrap_params tex_pars = {};
    tex_pars.target = GL_TEXTURE_2D;
    tex_pars.iformat = GL_INT_FORMAT;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx->gl_tex[i]);
    if (data) {
        glTexImage2D(
            GL_TEXTURE_2D, 0,
            GL_INT_FORMAT, height,
            width, 0,
            GL_FORMAT, GL_UNSIGNED_BYTE,
            data);
    }

    tex = ctx->gl_tex[i];

    tex_pars.texture = tex;
    tex_pars.width = height;
    tex_pars.height = width;

    bool need_tex_update = ctx->upscaling_selected_prev != upscaling_selected || ctx->win_width_prev != win_width_drawable || ctx->win_height_prev != win_height_drawable || ctx->view_mode_prev != view_mode;

    if (!IS_PLACEBO(upscaling_selected)) {
        placebo_upscaling_update(-1, i, screen_top_bot);
    }

    if (!IS_RASHADER(upscaling_selected)) {
        rashader_upscaling_update(-1, i, screen_top_bot);
    }

    if (IS_PLACEBO(upscaling_selected)) {
        int reset_mode = placebo_upscaling_update(PLACEBO_MODE(upscaling_selected), i, screen_top_bot);
        if (placebo_render[i][screen_top_bot]) {
            pl_tex in_tex = pl_opengl_wrap(pl_ogl_dev[i]->gpu, &tex_pars);
            if (!in_tex) {
                goto placebo_fail;
            }
            pl_tex out_tex = placebo_render_run(placebo_render[i][screen_top_bot], in_tex, ctx_height, ctx_width);
            if (!out_tex) {
                goto placebo_fail;
            }
            GLuint out_fbo;
            GLint out_iformat;
            GLuint out_target;
            GLuint out_ogl_tex = pl_opengl_unwrap(pl_ogl_dev[i]->gpu, out_tex, &out_target, &out_iformat, &out_fbo);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(out_target, out_ogl_tex);
        } else if (!reset_mode) {
    placebo_fail:
            err_log("placebo render failed\n");
            ui_upscaling_selected = UPSCALING_DEFAULT_NONE;
        }
    }

    if (IS_RASHADER(upscaling_selected)) {
        int reset_mode = rashader_upscaling_update(RASHADER_MODE(upscaling_selected), i, screen_top_bot);
        if (rashader_render[i][screen_top_bot]) {
            libra_gl_filter_chain_t *chain = rashader_render_chain(rashader_render[i][screen_top_bot]);
            struct libra_image_gl_t image = {
                .handle = tex,
                .format = GL_INT_FORMAT,
                .width = height,
                .height = width,
            };

            struct libra_image_gl_t out = {
                .handle = ctx->gl_tex_upscaled[i],
                .format = GL_INT_FORMAT,
                .width = ctx_height,
                .height = ctx_width,
            };
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, out.handle);
            glTexImage2D(
                GL_TEXTURE_2D, 0,
                GL_INT_FORMAT, out.width,
                out.height, 0,
                GL_FORMAT, GL_UNSIGNED_BYTE,
                NULL);
            libra_error_t err = libra_gl_filter_chain_frame(chain, 1, image, out, NULL, NULL, NULL);
            if (err) {
                libra_error_print(err);
                libra_error_free(&err);
                goto rashader_fail;
            }
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, out.handle);
        } else if (!reset_mode) {
    rashader_fail:
            err_log("rashader render failed\n");
            ui_upscaling_selected = UPSCALING_DEFAULT_NONE;
        }
    }

    if (is_renderer_csc())
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_fbo_sc[i]);
    else
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glViewport(0, 0, win_width_drawable, win_height_drawable);

    glUseProgram(gl_program[i]);

    if (gl_use_vao) {
        glBindVertexArray(gl_vao[i][screen_top_bot]);
        glBindBuffer(GL_ARRAY_BUFFER, gl_vbo[i][screen_top_bot]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_ebo[i]);
        if (need_tex_update)
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    } else {
        glEnableVertexAttribArray(gl_position_loc[i]);
        glEnableVertexAttribArray(gl_tex_coord_loc[i]);
        glVertexAttribPointer(gl_position_loc[i], 3, GL_FLOAT, GL_FALSE, sizeof(*vertices_pos), vertices_pos);
        glVertexAttribPointer(gl_tex_coord_loc[i], 2, GL_FLOAT, GL_FALSE, sizeof(*vertices_tex_coord), vertices_tex_coord);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

    glUniform1i(gl_sampler_loc[i], 0);

    if (gl_use_vao) {
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    } else {
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    }

    ctx->win_width_prev = win_width_drawable;
    ctx->win_height_prev = win_height_drawable;
    ctx->view_mode_prev = view_mode;
    ctx->upscaling_selected_prev = upscaling_selected;
}

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

void ui_renderer_ogl_present(int screen_top_bot, int ctx_top_bot, bool win_shared) {
    int i = ctx_top_bot;
    int p = win_shared ? screen_top_bot : i;

    if (is_renderer_csc()) {
#ifdef _WIN32
        if (!sc_fail[p]) {
            if (p == SCREEN_TOP) {
                GLuint ui_tex;
                HANDLE ui_handle;

                int width = ui_win_width_drawable_prev[p];
                int height = ui_win_height_drawable_prev[p];

                if (render_buffer_get(&ui_render_buf, i, width, height, &ui_tex, &ui_handle)) {
                    ui_compositing = 0;
                    sc_fail[p] = 1;
                    goto fail;
                }

                if (!wglDXLockObjectsNV(gl_d3ddevice[i], 1, &ui_handle)) {
                    err_log("wglDXLockObjectsNV failed: %d\n", (int)GetLastError());
                    ui_compositing = 0;
                    sc_fail[p] = 1;
                    goto fail;
                }

                GLuint ui_nk_tex = ui_render_tex_get(width, height);
                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ui_nk_tex, 0);

                glViewport(0, 0, width, height);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                glBindTexture(GL_TEXTURE_2D, ui_nk_tex);
                if (is_renderer_gles()) {
                    nk_sdl_gles2_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY, 1);
                } else {
                    nk_sdl_gl3_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY, 1);
                }
                nk_gui_next = 0;

                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ui_nk_tex);
                glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ui_tex);
                glUseProgram(gl_program[i]);
                if (gl_use_vao) {
                    glBindVertexArray(gl_vao_fbo[i]);
                    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo_fbo[i]);
                    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_ebo_fbo[i]);
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
                } else {
                    glEnableVertexAttribArray(gl_fbo_position_loc[i]);
                    glEnableVertexAttribArray(gl_fbo_tex_coord_loc[i]);
                    glVertexAttribPointer(gl_fbo_position_loc[i], 3, GL_FLOAT, GL_FALSE, sizeof(*fbo_vertices_pos), fbo_vertices_pos);
                    glVertexAttribPointer(gl_fbo_tex_coord_loc[i], 2, GL_FLOAT, GL_FALSE, sizeof(*fbo_vertices_tex_coord), fbo_vertices_tex_coord);
                    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, fbo_indices);
                }
                glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, 0);

                if (!wglDXUnlockObjectsNV(gl_d3ddevice[i], 1, &ui_handle)) {
                    err_log("wglDXUnlockObjectsNV failed: %d\n", (int)GetLastError());
                }

                if (update_hide_ui()) {
                    sc_fail[p] = 1;
                    goto fail;
                }

                if (!ui_hide_nk_windows && ui_tex_present(COMPAT_PRESENATTION_BUFFER_COUNT_PER_SCREEN)) {
                    ui_compositing = 0;
                    sc_fail[p] = 1;
                    goto fail;
                }
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            if (!wglDXUnlockObjectsNV(gl_d3ddevice[i], 1, &handle_sc[p])) {
                err_log("wglDXUnlockObjectsNV failed: %d\n", (int)GetLastError());
            }
            presentation_tex_present(i, screen_top_bot, win_shared, COMPAT_PRESENATTION_BUFFER_COUNT_PER_SCREEN);
        }
fail:
        ui_compositor_csc_present(i);
#endif
    } else {
#ifdef _WIN32
        if (!sc_fail[p]) {
#endif
            if (p == SCREEN_TOP) {
                if (is_renderer_gles()) {
                    nk_sdl_gles2_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY, 0);
                } else {
                    nk_sdl_gl3_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY, 0);
                }
                nk_gui_next = 0;
            }
            SDL_GL_SwapWindow(ogl_win[i]);
#ifdef _WIN32
        }
#endif
    }

#ifdef _WIN32
    if (sc_fail[p]) {
        Sleep(REST_EVERY_MS);
        sc_fail[p] = 0;
    }
#endif
}
