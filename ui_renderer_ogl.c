#include "ui_common_sdl.h"
#include "ui_renderer_ogl.h"
#include "main.h"
#ifdef _WIN32
#include "ui_compositor_csc.h"
#include "glad/glad_wgl.h"
#endif
#include "nuklear_sdl_gl3.h"
#include "nuklear_sdl_gles2.h"
#include "ui_main_nk.h"
#include "ui_input_redirection.h"
#include "placebo.h"
#include "rashader.h"
#include <libplacebo/opengl.h>
#include <limits.h>
#include <math.h>

SDL_Window *ogl_win[SCREEN_COUNT];
static SDL_Window *csc_win[SCREEN_COUNT];
static struct nk_context *nk_ctx;

SDL_GLContext gl_context[SCREEN_COUNT];
static int ogl_version_major, ogl_version_minor;
static bool gl_use_vao;
static GLuint gl_vao[SCREEN_COUNT];
static GLuint gl_vao_fbo;

#ifdef _WIN32
static HWND ogl_hwnd[SCREEN_COUNT];
static HDC ogl_hdc[SCREEN_COUNT];
#endif

static GLuint gl_fbo_sc[SCREEN_COUNT];

#define GLES_GLSL_VERSION "#version 100\n" "precision highp float;\n"
#define OGL_GLSL_VERSION "#version 110\n"
#define vs_str \
    "attribute float a_index;\n" \
    "varying vec2 v_texCoord;\n" \
    "void main()\n" \
    "{\n" \
    " v_texCoord = vec2(a_index == 1.0, a_index == 2.0) * 2.0;\n" \
    " gl_Position = vec4(v_texCoord * 2.0 + -1.0, 0.0, 1.0);\n" \
    "}\n"
#define vs_data_str \
    "attribute float a_index;\n" \
    "varying vec2 v_texCoord;\n" \
    "void main()\n" \
    "{\n" \
    " v_texCoord = vec2(a_index == 1.0, a_index == 2.0) * 2.0;\n" \
    " gl_Position = vec4(v_texCoord.yx * 2.0 + -1.0, 0.0, 1.0);\n" \
    "}\n"
#define vs_data_csc_str \
    "attribute float a_index;\n" \
    "varying vec2 v_texCoord;\n" \
    "void main()\n" \
    "{\n" \
    " v_texCoord = vec2(a_index == 1.0, a_index == 2.0) * 2.0;\n" \
    " gl_Position = vec4(v_texCoord.yx * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);\n" \
    "}\n"

#define fs_ui_str_0 \
    " if (color != vec4(0.0))\n" \
    "  color = vec4(color.rgb, 7.0 / 8.0);\n"

#define fs_str_use_0(str_0) \
    "varying vec2 v_texCoord;\n" \
    "uniform sampler2D s_texture;\n" \
    "void main()\n" \
    "{\n" \
    " vec4 color = texture2D(s_texture, v_texCoord);\n" \
    str_0 \
    " gl_FragColor = color;\n" \
    "}\n"

#define fs_str fs_str_use_0("")
#define fs_ui_str fs_str_use_0(fs_ui_str_0)

#define OGL_GLSL3_VERSION "#version 130\n"
#define vs3_str \
    "out vec2 v_texCoord;\n" \
    "void main()\n" \
    "{\n" \
    " v_texCoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n" \
    " gl_Position = vec4(v_texCoord * 2.0f + -1.0f, 0.0f, 1.0f);\n" \
    "}\n"
#define vs3_data_str \
    "out vec2 v_texCoord;\n" \
    "void main()\n" \
    "{\n" \
    " v_texCoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n" \
    " gl_Position = vec4(v_texCoord.yx * 2.0f - 1.0f, 0.0f, 1.0f);\n" \
    "}\n"
#define vs3_data_csc_str \
    "out vec2 v_texCoord;\n" \
    "void main()\n" \
    "{\n" \
    " v_texCoord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n" \
    " gl_Position = vec4(v_texCoord.yx * vec2(2.0f, -2.0f) + vec2(-1.0f, 1.0f), 0.0f, 1.0f);\n" \
    "}\n"

#define fs3_str_use_0(str_0) \
    "in vec2 v_texCoord;\n" \
    "uniform sampler2D s_texture;\n" \
    "out vec4 fragColor;\n" \
    "void main()\n" \
    "{\n" \
    " vec4 color = texture2D(s_texture, v_texCoord);\n" \
    str_0 \
    " fragColor = color;\n" \
    "}\n"

#define fs3_str fs3_str_use_0("")
#define fs3_ui_str fs3_str_use_0(fs_ui_str_0)

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
        goto end;

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
        prog = 0;
    }

end:
    // Free up no longer needed shader resources
    glDeleteShader(vs);
    glDeleteShader(fs);

    return prog;
}

GLuint gl_program[SCREEN_COUNT];
GLuint gl_csc_program[SCREEN_COUNT];
GLuint gl_cursor_program;
GLuint gl_ui_program;

GLint gl_index_loc[SCREEN_COUNT];
GLint gl_sampler_loc[SCREEN_COUNT];

GLint gl_csc_index_loc[SCREEN_COUNT];
GLint gl_csc_sampler_loc[SCREEN_COUNT];

GLuint gl_cursor_index_loc;
GLuint gl_cursor_sampler_loc;

GLint gl_fbo_index_loc;
GLint gl_fbo_sampler_loc;

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

        if (j == SCREEN_TOP) {
            if (is_renderer_gles()) {
                gl_ui_program = Load_program(GLES_GLSL_VERSION vs_str, GLES_GLSL_VERSION fs_ui_str);
                gl_fbo_index_loc = glGetAttribLocation(gl_ui_program, "a_index");
            } else {
                gl_ui_program = Load_program(OGL_GLSL3_VERSION vs3_str, OGL_GLSL3_VERSION fs3_ui_str);
            }
            gl_fbo_sampler_loc = glGetUniformLocation(gl_ui_program, "s_texture");

            if (is_renderer_gles()) {
                gl_cursor_program = Load_program(GLES_GLSL_VERSION vs_str, GLES_GLSL_VERSION fs_str);
                gl_cursor_index_loc = glGetAttribLocation(gl_cursor_program, "a_index");
            } else {
                gl_cursor_program = Load_program(OGL_GLSL3_VERSION vs3_str, OGL_GLSL3_VERSION fs3_str);
            }
            gl_cursor_sampler_loc = glGetUniformLocation(gl_cursor_program, "s_texture");
        }

        if (is_renderer_gles()) {
            gl_program[j] = Load_program(GLES_GLSL_VERSION vs_data_str, GLES_GLSL_VERSION fs_str);
            gl_index_loc[j] = glGetAttribLocation(gl_program[j], "a_index");
            gl_csc_program[j] = Load_program(GLES_GLSL_VERSION vs_data_csc_str, GLES_GLSL_VERSION fs_str);
            gl_csc_index_loc[j] = glGetAttribLocation(gl_csc_program[j], "a_index");
        } else {
            gl_program[j] = Load_program(OGL_GLSL3_VERSION vs3_data_str, OGL_GLSL3_VERSION fs3_str);
            gl_csc_program[j] = Load_program(OGL_GLSL3_VERSION vs3_data_csc_str, OGL_GLSL3_VERSION fs3_str);
        }
        gl_sampler_loc[j] = glGetUniformLocation(gl_program[j], "s_texture");
        gl_csc_sampler_loc[j] = glGetUniformLocation(gl_csc_program[j], "s_texture");

        if (gl_use_vao) {
            glGenVertexArrays(1, &gl_vao[j]);
            if (j == SCREEN_TOP)
                glGenVertexArrays(1, &gl_vao_fbo);
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
            if (gl_vao[j]) {
                glDeleteVertexArrays(1, &gl_vao[j]);
                gl_vao[j] = 0;
            }
            if (j == SCREEN_TOP) {
                if (gl_vao_fbo) {
                    glDeleteVertexArrays(1, &gl_vao_fbo);
                    gl_vao_fbo = 0;
                }
            }
        }
        if (gl_csc_program[j]) {
            glDeleteProgram(gl_csc_program[j]);
            gl_csc_program[j] = 0;
        }
        if (gl_program[j]) {
            glDeleteProgram(gl_program[j]);
            gl_program[j] = 0;
        }
        if (j == SCREEN_TOP) {
            if (gl_cursor_program) {
                glDeleteProgram(gl_cursor_program);
                gl_cursor_program = 0;
            }

            if (gl_ui_program) {
                glDeleteProgram(gl_ui_program);
                gl_ui_program = 0;
            }
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

static bool rashader_delay_init[SCREEN_COUNT][SCREEN_COUNT];

#define GL_GetProcAddress (SDL_GL_GetProcAddress)

static int ogl_upscaling_init(void) {
    bool use_placebo = true;
    bool use_rashader = is_renderer_ogl();

    ui_upscaling_filter_count = UPSCALING_DEFAULT_COUNT;

    pl_log_dev = placebo_log_create();
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        SDL_GL_MakeCurrent(ogl_win[j], gl_context[j]);

        pl_ogl_dev[j] = pl_opengl_create(pl_log_dev, pl_opengl_params(
            .get_proc_addr = (pl_voidfunc_t (*)(const char *))GL_GetProcAddress,
        ));
        if (!pl_ogl_dev[j]) {
            use_placebo = false;
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            placebo_render_mode[j][i] = -1;
            rashader_render_mode[j][i] = -1;

            // HACK workaround rashader not working on init (don't know what I'm doing wrong..)
            rashader_delay_init[j][i] = true;
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

    ui_upscaling_selected = UPSCALING_DEFAULT_NONE;

    return 0;
}

static void ogl_filter_chain_free(void *fc, void *);
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
                rashader_render_close(rashader_render[j][i], ogl_filter_chain_free, NULL);
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
    gl_context[SCREEN_TOP] = SDL_GL_CreateContext(ogl_win[SCREEN_TOP]);
    if (!gl_context[SCREEN_TOP]) {
        err_log("SDL_GL_CreateContext: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetSwapInterval(1);

    if (is_renderer_gles()) {
        if (!gladLoadGLES2Loader((GLADloadproc)GL_GetProcAddress)) {
            err_log("gladLoadGLES2Loader failed\n");
            return -1;
        }
    } else {
        if (!gladLoadGLLoader((GLADloadproc)GL_GetProcAddress)) {
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
            SDL_PropertiesID id = SDL_GetWindowProperties(ogl_win[i]);
            ogl_hwnd[i] = (HWND)SDL_GetPointerProperty(id, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
            ogl_hdc[i] = (HDC)SDL_GetPointerProperty(id, SDL_PROP_WINDOW_WIN32_HDC_POINTER, NULL);
        }

        if (!gladLoadWGLLoader((GLADloadproc)GL_GetProcAddress, ogl_hdc[SCREEN_TOP])) {
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
    glDisable(GL_FRAMEBUFFER_SRGB);

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
    glDisable(GL_FRAMEBUFFER_SRGB);

    if (ogl_res_init())
        return -1;

    if (ogl_upscaling_init())
        return -1;

    err_log("%s %s\n", is_renderer_ogl() ? "ogl" : is_renderer_gles_angle() ? "angle" : "gles", is_renderer_csc() ? "composition swapchain" : "");

    return 0;
}

static void ogl_renderer_destroy(void) {
    ogl_upscaling_close();

    ogl_res_destroy();

    if (nk_ctx) {
        if (is_renderer_gles()) {
            nk_sdl_gles2_shutdown();
        } else {
            nk_sdl_gl3_shutdown();
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
            SDL_GL_DestroyContext(gl_context[i]);
            gl_context[i] = NULL;
        }
    }
}

int ui_renderer_ogl_init(void) {
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
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);

    if (sdl_win_init(ogl_win, SDL_WINDOW_OPENGL)) {
        return -1;
    }

    if (is_renderer_csc()) {
        if (sdl_win_init(csc_win, SDL_WINDOW_OPENGL)) {
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
        ui_win_width_drawable[i] = 1;
        ui_win_height_drawable[i] = 1;
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

#ifdef _WIN32
    if (is_renderer_csc()) {
        sc_fail[p] = 0;
        ui_compositor_csc_main(screen_top_bot, i, win_shared);
        if (sc_fail[p]) {
            return;
        }

        struct render_buffer_t *sc_render_buf = &render_buffers[i][screen_top_bot];
        if (render_buffer_get(sc_render_buf, i, ui_ctx_width_drawable[p], ui_ctx_height_drawable[p], &tex_sc[p], &handle_sc[p]) != 0) {
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

    glViewport(0, 0, ui_ctx_width_drawable[p], ui_ctx_height_drawable[p]);
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

static void *ogl_filter_chain_create(libra_shader_preset_t *preset, void *) {
    bool ogl460 = ogl_version_major > 4 || (ogl_version_major == 4 && ogl_version_minor >= 6);
    struct filter_chain_gl_opt_t opt = {
        .version = libra_instance_api_version(),
        .use_dsa = ogl460,
        .glsl_version = ogl460 ? 460 : 330,
    };
    libra_gl_filter_chain_t out;
    libra_error_t err = libra_gl_filter_chain_create(preset, (libra_gl_loader_t)GL_GetProcAddress, &opt, &out);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
        return NULL;
    }
    return out;
}

static void ogl_filter_chain_free(void *fc, void *) {
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
        rashader_render_close(rashader_render[i][screen_top_bot], ogl_filter_chain_free, NULL);
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
        if (rashader_delay_init[i][screen_top_bot]) {
            reset_mode = 1;
            goto fail;
        }

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

        rashader_render[i][screen_top_bot] = rashader_render_init(rashader, render_mode, &ctx, ogl_filter_chain_create, NULL, (PFN_filter_chain_set_param)libra_gl_filter_chain_set_param);
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

static const GLushort indices[] = {0, 1, 2};
void ui_renderer_ogl_draw(struct rp_buffer_ctx_t *ctx, uint8_t *data, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, int win_shared)
{
    int i = ctx_top_bot;

    int ctx_left;
    int ctx_top;
    int ctx_width;
    int ctx_height;
    if (win_shared)
        draw_screen_get_dims_win_shared(screen_top_bot, i, width, height, &ctx_left, &ctx_top, &ctx_width, &ctx_height);
    else
        // HACK dims are symmetrical, negate screen_top_bot to flip top and bot for OpenGL
        draw_screen_get_dims_lite(!screen_top_bot, i, view_mode, width, height, &ctx_left, &ctx_top, &ctx_width, &ctx_height);
    ctx_left *= ui_win_scale[i];
    ctx_top *= ui_win_scale[i];
    ctx_width *= ui_win_scale[i];
    ctx_height *= ui_win_scale[i];

    int win_width_drawable = ui_win_width_drawable[i];
    int win_height_drawable = ui_win_height_drawable[i];

    if (win_shared) {
        i = screen_top_bot;;
        win_width_drawable = ui_ctx_width_drawable[i];
        win_height_drawable = ui_ctx_height_drawable[i];
    }

    int upscaling_selected = ui_upscaling_selected;
    bool upscaled = upscaling_selected != UPSCALING_DEFAULT_NONE;
    bool do_upscaled = false;

    bool need_tex_update = ctx->upscaling_selected_prev != upscaling_selected ||
        ctx->width_prev != ctx_width || ctx->height_prev != ctx_height ||
        ctx->win_width_prev != win_width_drawable || ctx->win_height_prev != win_height_drawable ||
        ctx->view_mode_prev != view_mode;

    if (!data) {
        if (rashader_delay_init[i][screen_top_bot]) {
            data = ctx->data_prev;
        } else if (upscaled) {
            if (need_tex_update || !ctx->gl_tex_upscaled_prev[i]) {
                data = ctx->data_prev;
            } else {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ctx->gl_tex_upscaled_prev[i]);
            }
        } else {
            ctx->gl_tex_upscaled_prev[i] = 0;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ctx->gl_tex[i]);
        }
    }

    if (data) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx->gl_tex[i]);
        glTexImage2D(
            GL_TEXTURE_2D, 0,
            GL_INT_FORMAT, height, width, 0,
            GL_FORMAT, GL_UNSIGNED_BYTE,
            data);

        if (!IS_PLACEBO(upscaling_selected)) {
            placebo_upscaling_update(-1, i, screen_top_bot);
        }

        if (!IS_RASHADER(upscaling_selected)) {
            rashader_upscaling_update(-1, i, screen_top_bot);
        }

        pl_tex in_tex = NULL;
        pl_tex out_tex = NULL;
        if (IS_PLACEBO(upscaling_selected)) {
            int reset_mode = placebo_upscaling_update(PLACEBO_MODE(upscaling_selected), i, screen_top_bot);
            if (placebo_render[i][screen_top_bot]) {
                struct pl_opengl_wrap_params in_tex_pars = {};
                in_tex_pars.target = GL_TEXTURE_2D;
                in_tex_pars.iformat = GL_INT_FORMAT;

                in_tex_pars.texture = ctx->gl_tex[i];
                in_tex_pars.width = height;
                in_tex_pars.height = width;

                in_tex = pl_opengl_wrap(pl_ogl_dev[i]->gpu, &in_tex_pars);
                if (!in_tex) {
                    goto placebo_fail;
                }

                struct pl_opengl_wrap_params out_tex_pars = {};
                out_tex_pars.target = GL_TEXTURE_2D;
                out_tex_pars.iformat = GL_INT_FORMAT;

                out_tex_pars.texture = ctx->gl_tex_upscaled[i];
                out_tex_pars.width = ctx_height;
                out_tex_pars.height = ctx_width;

                if (ctx->width_upscaled[i] != out_tex_pars.width || ctx->height_upscaled[i] != out_tex_pars.height) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, out_tex_pars.texture);
                    glTexImage2D(
                        GL_TEXTURE_2D, 0, GL_INT_FORMAT,
                        out_tex_pars.width, out_tex_pars.height, 0,
                        GL_FORMAT, GL_UNSIGNED_BYTE,
                        NULL);
                    ctx->width_upscaled[i] = out_tex_pars.width;
                    ctx->height_upscaled[i] = out_tex_pars.height;
                }

                out_tex = pl_opengl_wrap(pl_ogl_dev[i]->gpu, &out_tex_pars);
                if (!out_tex) {
                    goto placebo_fail;
                }

                bool ret = placebo_render_run(placebo_render[i][screen_top_bot], in_tex, out_tex, 0, 0) != NULL;
                if (!ret) {
                    goto placebo_fail;
                }
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, out_tex_pars.texture);
                ctx->gl_tex_upscaled_prev[i] = out_tex_pars.texture;
                do_upscaled = true;
            } else if (!reset_mode) {
placebo_fail:
                err_log("placebo render failed\n");
                upscaling_selected = UPSCALING_DEFAULT_NONE;
            }
        }

        if (in_tex)
            pl_tex_destroy(pl_ogl_dev[i]->gpu, &in_tex);
        if (out_tex)
            pl_tex_destroy(pl_ogl_dev[i]->gpu, &out_tex);

        if (IS_RASHADER(upscaling_selected)) {
            int reset_mode = rashader_upscaling_update(RASHADER_MODE(upscaling_selected), i, screen_top_bot);
            if (rashader_render[i][screen_top_bot]) {
                libra_gl_filter_chain_t *chain = rashader_render_chain(rashader_render[i][screen_top_bot]);
                struct libra_image_gl_t image = {
                    .handle = ctx->gl_tex[i],
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

                if (ctx->width_upscaled[i] != (int)out.width || ctx->height_upscaled[i] != (int)out.height) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, out.handle);
                    glTexImage2D(
                        GL_TEXTURE_2D, 0, GL_INT_FORMAT,
                        out.width, out.height, 0,
                        GL_FORMAT, GL_UNSIGNED_BYTE,
                        NULL);
                    ctx->width_upscaled[i] = out.width;
                    ctx->height_upscaled[i] = out.height;
                }

                libra_error_t err = libra_gl_filter_chain_frame(chain, 1, image, out, NULL, NULL, NULL);
                if (err) {
                    libra_error_print(err);
                    libra_error_free(&err);
                    goto rashader_fail;
                }
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, out.handle);
                ctx->gl_tex_upscaled_prev[i] = out.handle;
                do_upscaled = true;
            } else if (!reset_mode) {
rashader_fail:
                err_log("rashader render failed\n");
                upscaling_selected = UPSCALING_DEFAULT_NONE;
            }
        }

        if (upscaling_selected == UPSCALING_DEFAULT_NONE) {
            ui_upscaling_selected = UPSCALING_DEFAULT_NONE;

            ctx->gl_tex_upscaled_prev[i] = 0;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, ctx->gl_tex[i]);
        }
    }

    if (!do_upscaled) {
        if (rashader_delay_init[i][screen_top_bot]) {
            rashader_delay_init[i][screen_top_bot] = 0;
            if (i == (view_mode == VIEW_MODE_SEPARATE ? SCREEN_BOT : SCREEN_TOP))
                cursor_scale_prev = 0.0f;
        }
    }

    if (is_renderer_csc()) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gl_fbo_sc[i]);
        glUseProgram(gl_csc_program[i]);
        glUniform1i(gl_csc_sampler_loc[i], 0);
    } else {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glUseProgram(gl_program[i]);
        glUniform1i(gl_sampler_loc[i], 0);
    }
    glViewport(ctx_left, ctx_top, MAX(ctx_width, 1), MAX(ctx_height, 1));

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

    if (gl_use_vao) {
        glBindVertexArray(gl_vao[i]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    } else {
        if (is_renderer_csc()) {
            glEnableVertexAttribArray(gl_csc_index_loc[i]);
            glVertexAttribPointer(gl_csc_index_loc[i], 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(*indices), indices);
        } else {
            glEnableVertexAttribArray(gl_index_loc[i]);
            glVertexAttribPointer(gl_index_loc[i], 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(*indices), indices);
        }
        glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
    }

    ctx->width_prev = ctx_width;
    ctx->height_prev = ctx_height;
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
                    nk_sdl_gles2_render(NK_ANTI_ALIASING_OFF, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY, 1);
                } else {
                    nk_sdl_gl3_render(NK_ANTI_ALIASING_OFF, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY, 1);
                }
                nk_gui_next = 0;

                glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, ui_nk_tex);
                glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ui_tex);
                glUseProgram(gl_ui_program);
                glUniform1i(gl_fbo_sampler_loc, 0);
                if (gl_use_vao) {
                    glBindVertexArray(gl_vao_fbo);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                } else {
                    glEnableVertexAttribArray(gl_fbo_index_loc);
                    glVertexAttribPointer(gl_fbo_index_loc, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(*indices), indices);
                    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
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
                    nk_sdl_gles2_render(NK_ANTI_ALIASING_OFF, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY, 0);
                } else {
                    nk_sdl_gl3_render(NK_ANTI_ALIASING_OFF, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY, 0);
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

static void gl_read_tex(unsigned char *buf, GLuint tex, int width, int height) {
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    glReadPixels(0, 0, width, height, GL_FORMAT, GL_UNSIGNED_BYTE, buf);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
}

void ui_renderer_ogl_gen_cursor(stbi_t *image, const unsigned char *base, int width, int height, int channels, float scale) {
    if (channels != GL_CHANNELS_N) {
        return;
    }
    bool fail = true;

    int i = SCREEN_TOP;
    int screen_top_bot = i;

    int target_width = roundf(width * scale);
    int target_height = roundf(height * scale);

    image->image = malloc(target_width * target_height * channels);
    if (!image->image) {
        return;
    }

    unsigned char *base2 = malloc(width * height * GL_CHANNELS_N);
    if (!base2) {
        goto fail_base2;
    }

    unsigned char *image_base2 = malloc(target_width * target_height * GL_CHANNELS_N);
    if (!image_base2) {
        goto fail_image_base2;
    }

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            int base2_i = (y * width + x) * GL_CHANNELS_N;
            int base_i = (y * width + x) * channels;
            base2[base2_i + 2] = base2[base2_i] = ((int)base[base_i] + (int)base[base_i + 1] + (int)base[base_i + 2]) / 3;
            base2[base2_i + 1] = base[base_i + 3];
            base2[base2_i + 3] = UCHAR_MAX;
        }
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(
        GL_TEXTURE_2D, 0,
        GL_INT_FORMAT, width, height, 0,
        GL_FORMAT, GL_UNSIGNED_BYTE,
        base2);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

    int upscaling_selected = ui_upscaling_selected;

    if (
        IS_PLACEBO(upscaling_selected) &&
        placebo_upscaling_update(PLACEBO_MODE(upscaling_selected), i, screen_top_bot) == 0 &&
        placebo_render[i][screen_top_bot]
    ) {
        pl_tex in_tex = NULL;
        pl_tex out_tex = NULL;
        int fail = true;

        GLuint gl_out_tex = 0;
        glGenTextures(1, &gl_out_tex);

        struct pl_opengl_wrap_params in_tex_pars = {};
        in_tex_pars.target = GL_TEXTURE_2D;
        in_tex_pars.iformat = GL_INT_FORMAT;

        in_tex_pars.texture = tex;
        in_tex_pars.width = width;
        in_tex_pars.height = height;

        in_tex = pl_opengl_wrap(pl_ogl_dev[i]->gpu, &in_tex_pars);
        if (!in_tex) {
            goto placebo_fail;
        }

        struct pl_opengl_wrap_params out_tex_pars = {};
        out_tex_pars.target = GL_TEXTURE_2D;
        out_tex_pars.iformat = GL_INT_FORMAT;

        out_tex_pars.texture = gl_out_tex;
        out_tex_pars.width = target_width;
        out_tex_pars.height = target_height;

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, out_tex_pars.texture);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_INT_FORMAT,
            out_tex_pars.width, out_tex_pars.height, 0,
            GL_FORMAT, GL_UNSIGNED_BYTE,
            NULL);

        out_tex = pl_opengl_wrap(pl_ogl_dev[i]->gpu, &out_tex_pars);
        if (!out_tex) {
            goto placebo_fail;
        }

        bool ret = placebo_render_run(placebo_render[i][screen_top_bot], in_tex, out_tex, 0, 0) != NULL;
        if (!ret) {
            goto placebo_fail;
        }

        fail = false;
        gl_read_tex(image_base2, gl_out_tex, target_width, target_height);

placebo_fail:
        if (in_tex)
            pl_tex_destroy(pl_ogl_dev[i]->gpu, &in_tex);
        if (out_tex)
            pl_tex_destroy(pl_ogl_dev[i]->gpu, &out_tex);

        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &gl_out_tex);

        if (fail) {
            goto no_upscale;
        }
    } else if (
        IS_RASHADER(upscaling_selected) &&
        rashader_upscaling_update(RASHADER_MODE(upscaling_selected), i, screen_top_bot) == 0 &&
        rashader_render[i][screen_top_bot]
    ) {
        GLuint gl_out_tex = 0;
        glGenTextures(1, &gl_out_tex);
        int fail = true;

        libra_gl_filter_chain_t *chain = rashader_render_chain(rashader_render[i][screen_top_bot]);
        struct libra_image_gl_t in = {
            .handle = tex,
            .format = GL_INT_FORMAT,
            .width = width,
            .height = height,
        };

        struct libra_image_gl_t out = {
            .handle = gl_out_tex,
            .format = GL_INT_FORMAT,
            .width = target_width,
            .height = target_height,
        };

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, out.handle);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_INT_FORMAT,
            out.width, out.height, 0,
            GL_FORMAT, GL_UNSIGNED_BYTE,
            NULL);

        libra_error_t err = libra_gl_filter_chain_frame(chain, 1, in, out, NULL, NULL, NULL);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            goto rashader_fail;
        }

        fail = false;
        gl_read_tex(image_base2, gl_out_tex, target_width, target_height);

rashader_fail:
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &gl_out_tex);

        if (fail) {
            goto no_upscale;
        }
    } else {
no_upscale:
        int fail = true;
        GLuint fbo = 0;
        GLuint gl_out_tex = 0;
        glGenTextures(1, &gl_out_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gl_out_tex);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_INT_FORMAT,
            target_width, target_height, 0,
            GL_FORMAT, GL_UNSIGNED_BYTE,
            NULL);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_out_tex, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);

        glViewport(0, 0, target_width, target_height);
        glUseProgram(gl_cursor_program);

        glUniform1i(gl_cursor_sampler_loc, 0);

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);

        if (gl_use_vao) {
            glBindVertexArray(gl_vao[i]);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        } else {
            glEnableVertexAttribArray(gl_cursor_index_loc);
            glVertexAttribPointer(gl_cursor_index_loc, 1, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(*indices), indices);
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
        }

        fail = false;

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        gl_read_tex(image_base2, gl_out_tex, target_width, target_height);

        glDeleteFramebuffers(1, &fbo);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &gl_out_tex);
        if (fail)
            goto fail;
    }
    fail = false;
    for (int x = 0; x < target_width; ++x) {
        for (int y = 0; y < target_height; ++y) {
            int image_i = (y * target_width + x) * channels;
            int image2_i = (y * target_width + x) * GL_CHANNELS_N;
            image->image[image_i + 2] = image->image[image_i + 1] = image->image[image_i] =
                ((int)image_base2[image2_i] + (int)image_base2[image2_i + 2]) / 2;
            image->image[image_i + 3] = image_base2[image2_i + 1];
        }
    }

    image->width = target_width;
    image->height = target_height;
    image->channels = channels;

fail:
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &tex);
    free(image_base2);
fail_image_base2:
    free(base2);
fail_base2:
    if (fail) {
        free(image->image);
        image->image = 0;
    }
}
