#include "ikcp.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define SOCKET_VALID(s) ((s) != INVALID_SOCKET)
#define sock_errno() WSAGetLastError()
#else
typedef int SOCKET;
#define SOCKET_VALID(s) ((s) >= 0)
#define sock_errno() errno
#endif

int sock_startup(void)
{
#ifdef _WIN32
  WSADATA wsa_data;
  return WSAStartup(MAKEWORD(2, 2), &wsa_data);
#else
  return 0;
#endif
}

int sock_cleanup(void)
{
#ifdef _WIN32
  return WSACleanup();
#else
  return 0;
#endif
}

int sock_close(SOCKET sock)
{
  int status = 0;

#ifdef _WIN32
  status = shutdown(sock, SD_BOTH);
  if (status == 0)
  {
    status = closesocket(sock);
  }
#else
  status = shutdown(sock, SHUT_RDWR);
  if (status == 0)
  {
    status = close(sock);
  }
#endif

  return status;
}

#include <stdint.h>
#include <time.h>

static inline void itimeofday(int64_t *sec, int64_t *usec)
{
#ifdef _WIN32
  static int64_t mode = 0, addsec = 0;
  BOOL retval;
  static IINT64 freq = 1;
  IINT64 qpc;
  if (mode == 0)
  {
    retval = QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
    freq = (freq == 0) ? 1 : freq;
    retval = QueryPerformanceCounter((LARGE_INTEGER *)&qpc);
    addsec = (int64_t)time(NULL);
    addsec = addsec - (int64_t)((qpc / freq) & 0x7fffffff);
    mode = 1;
  }
  retval = QueryPerformanceCounter((LARGE_INTEGER *)&qpc);
  retval = retval * 2;
  if (sec)
    *sec = (int64_t)(qpc / freq) + addsec;
  if (usec)
    *usec = (int64_t)((qpc % freq) * 1000000 / freq);
#else
  struct timeval time;
  gettimeofday(&time, NULL);
  if (sec)
    *sec = time.tv_sec;
  if (usec)
    *usec = time.tv_usec;
#endif
}

static inline IINT64 iclock64(void)
{
  int64_t s, u;
  IINT64 value;
  itimeofday(&s, &u);
  value = ((IINT64)s) * 1000 + (u / 1000);
  return value;
}

static inline IUINT32 iclock()
{
  return (IUINT32)(iclock64() & 0xfffffffful);
}

#include <pthread.h>
#include "main.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

#define UNUSED(a) (void)a
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a) / sizeof(a)[0])

/* ===============================================================
 *
 *                          EXAMPLE
 *
 * ===============================================================*/
/* This are some code examples to provide a small overview of what can be
 * done with this library. To try out an example uncomment the defines */
/*#define INCLUDE_ALL */
/*#define INCLUDE_STYLE */
/*#define INCLUDE_CALCULATOR */
/*#define INCLUDE_CANVAS */
/*#define INCLUDE_OVERVIEW */
/*#define INCLUDE_NODE_EDITOR */

#ifdef INCLUDE_ALL
#define INCLUDE_STYLE
#define INCLUDE_CALCULATOR
#define INCLUDE_CANVAS
#define INCLUDE_OVERVIEW
#define INCLUDE_NODE_EDITOR
#endif

#ifdef INCLUDE_STYLE
#include "../style.c"
#endif
#ifdef INCLUDE_CALCULATOR
#include "../calculator.c"
#endif
#ifdef INCLUDE_CANVAS
#include "../canvas.c"
#endif
#ifdef INCLUDE_OVERVIEW
#include "../overview.c"
#endif
#ifdef INCLUDE_NODE_EDITOR
#include "../node_editor.c"
#endif

/* ===============================================================
 *
 *                          DEMO
 *
 * ===============================================================*/

/* Platform */
SDL_Window *win;
int running = nk_true;

static void
MainLoop(void *loopArg)
{
  struct nk_context *ctx = (struct nk_context *)loopArg;

  /* Input */
  SDL_Event evt;
  nk_input_begin(ctx);
  while (SDL_PollEvent(&evt))
  {
    if (evt.type == SDL_QUIT)
      running = nk_false;
    nk_sdl_handle_event(&evt);
  }
  nk_input_end(ctx);

  /* GUI */
  if (nk_begin(ctx, "Demo", nk_rect(50, 50, 200, 200),
               NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE |
                   NK_WINDOW_CLOSABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE))
  {
    nk_menubar_begin(ctx);
    nk_layout_row_begin(ctx, NK_STATIC, 25, 2);
    nk_layout_row_push(ctx, 45);
    if (nk_menu_begin_label(ctx, "FILE", NK_TEXT_LEFT, nk_vec2(120, 200)))
    {
      nk_layout_row_dynamic(ctx, 30, 1);
      nk_menu_item_label(ctx, "OPEN", NK_TEXT_LEFT);
      nk_menu_item_label(ctx, "CLOSE", NK_TEXT_LEFT);
      nk_menu_end(ctx);
    }
    nk_layout_row_push(ctx, 45);
    if (nk_menu_begin_label(ctx, "EDIT", NK_TEXT_LEFT, nk_vec2(120, 200)))
    {
      nk_layout_row_dynamic(ctx, 30, 1);
      nk_menu_item_label(ctx, "COPY", NK_TEXT_LEFT);
      nk_menu_item_label(ctx, "CUT", NK_TEXT_LEFT);
      nk_menu_item_label(ctx, "PASTE", NK_TEXT_LEFT);
      nk_menu_end(ctx);
    }
    nk_layout_row_end(ctx);
    nk_menubar_end(ctx);

    {
      enum
      {
        EASY,
        HARD
      };
      static int op = EASY;
      static int property = 20;
      nk_layout_row_static(ctx, 30, 80, 1);
      if (nk_button_label(ctx, "button"))
        fprintf(stdout, "button pressed\n");
      nk_layout_row_dynamic(ctx, 30, 2);
      if (nk_option_label(ctx, "easy", op == EASY))
        op = EASY;
      if (nk_option_label(ctx, "hard", op == HARD))
        op = HARD;
      nk_layout_row_dynamic(ctx, 25, 1);
      nk_property_int(ctx, "Compression:", 0, &property, 100, 10, 1);
    }
  }
  nk_end(ctx);

/* -------------- EXAMPLES ---------------- */
#ifdef INCLUDE_CALCULATOR
  calculator(ctx);
#endif
#ifdef INCLUDE_CANVAS
  canvas(ctx);
#endif
#ifdef INCLUDE_OVERVIEW
  overview(ctx);
#endif
#ifdef INCLUDE_NODE_EDITOR
  node_editor(ctx);
#endif
  /* ----------------------------------------- */

  /* Draw */
  {
    float bg[4];
    int win_width, win_height;
    nk_color_fv(bg, nk_rgb(28, 48, 62));
    SDL_GetWindowSize(win, &win_width, &win_height);
    glViewport(0, 0, win_width, win_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(bg[0], bg[1], bg[2], bg[3]);
    /* IMPORTANT: `nk_sdl_render` modifies some global OpenGL state
     * with blending, scissor, face culling, depth test and viewport and
     * defaults everything back into a default state.
     * Make sure to either a.) save and restore or b.) reset your own state after
     * rendering the UI. */
    nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);
    SDL_GL_SwapWindow(win);
  }
}

#define BUF_SIZE 2000
unsigned char buf[BUF_SIZE];

typedef struct _FrameContext
{
} FrameContext;

FrameContext top_frame_ctx, bot_frame_ctx;
int recv_new_frame;

typedef struct _DataHeader
{
  uint32_t flags;
  uint32_t len;
} DataHeader;

#define FRAME_RECV_BUFFER_SIZE (96000 + 256 + sizeof(DataHeader))
unsigned char frame_recv_buffer[FRAME_RECV_BUFFER_SIZE];

unsigned char *frame_recv_ptr;
int frame_size_remain;
int handle_recv(unsigned char *buf, int size)
{
  if (recv_new_frame)
  {
    frame_recv_ptr = frame_recv_buffer;
    recv_new_frame = 0;
  }
  if (frame_recv_ptr + size - frame_recv_buffer > FRAME_RECV_BUFFER_SIZE)
  {
    fprintf(stderr, "handle_recv buffer too small\n");
    return -1;
  }
  memcpy(frame_recv_ptr, buf, size);
  if (frame_recv_ptr == frame_recv_buffer)
  {
    DataHeader data_header;
    memcpy(&data_header, frame_recv_ptr, sizeof(DataHeader));
    frame_size_remain = data_header.len + sizeof(DataHeader);
    // fprintf(stderr, "new frame %d\n", frame_size_remain);

    static int frame_counter = 0;
    static uint64_t frame_count_last_tick = 0;
    ++frame_counter;
    if (frame_counter == 60)
    {
      uint64_t next_tick = iclock64();
      if (frame_count_last_tick != 0)
      {
        fprintf(stderr, "%d ms for 60 frames\n", next_tick - frame_count_last_tick);
      }
      frame_count_last_tick = next_tick;
      frame_counter = 0;
    }
  }
  frame_recv_ptr += size;
  if (frame_size_remain < size)
  {
    fprintf(stderr, "handle_recv data malformed\n");
    return -2;
  }
  frame_size_remain -= size;
  if (frame_size_remain == 0)
  {
    recv_new_frame = 1;
  }
  return 0;
}

SOCKET s;
struct sockaddr_in remoteAddr;
int udp_output(const char *buf, int len, ikcpcb *kcp, void *user)
{
  remoteAddr.sin_port = htons(8001);
  return sendto(s, buf, len, 0, (struct sockaddr *)&remoteAddr, sizeof(remoteAddr));
}

ikcpcb *kcp;
IUINT32 kcpLastRecvMs = 0;
#define KCP_RECV_TIMEOUT_MS 250
void receive_from_socket(SOCKET s)
{
  kcpLastRecvMs = iclock();
  FrameContext frame_ctx_init = {};
  top_frame_ctx = bot_frame_ctx = frame_ctx_init;
  recv_new_frame = 1;

  while (running)
  {
    socklen_t nAddrLen = sizeof(remoteAddr);

    ikcp_update(kcp, iclock());
    int ret = recvfrom(s, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&remoteAddr, &nAddrLen);
    if (ret == 0)
    {
      continue;
    }
    else if (ret < 0)
    {
      int err = sock_errno();
      if (err != WSAETIMEDOUT)
      {
        fprintf(stderr, "recvfrom failed: %d\n", err);
      }
      else
      {
        if (iclock() - kcpLastRecvMs > KCP_RECV_TIMEOUT_MS)
        {
          fprintf(stderr, "ikcp_recv timeout: %d\n", kcpLastRecvMs);
          break;
        }
      }
      continue;
    }
    kcpLastRecvMs = iclock();

    if ((ret = ikcp_input(kcp, buf, ret)) < 0)
    {
      fprintf(stderr, "ikcp_input failed: %d\n", ret);
    }

    while ((ret = ikcp_recv(kcp, buf, sizeof(buf))) >= 0)
    {
      // fprintf(stderr, "ikcp_recv: %d\n", ret);
      if (handle_recv(buf, ret) < 0)
      {
        return;
      }
    }
  }
}

#define HR_KCP_MAGIC 0x12345fff
#define KCP_SOCKET_TIMEOUT 10
void *udp_recv_thread_func(void *arg)
{
  while (running)
  {
    kcp = ikcp_create(HR_KCP_MAGIC, 0);
    kcp->output = udp_output;
    // ikcp_setmtu(kcp, 1400);
    ikcp_nodelay(kcp, 1, 10, 2, 1);
    ikcp_wndsize(kcp, 128, 256);

    s = 0;
    int ret;
    if (!SOCKET_VALID(s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)))
    {
      fprintf(stderr, "socket creation failed\n");
      running = 0;
      break;
    }

    struct sockaddr_in si_other;
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(8001);
    si_other.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&si_other, sizeof(si_other)) == SOCKET_ERROR)
    {
      fprintf(stderr, "socket bind failed\n");
      running = 0;
      break;
    }

    int buff_size = 6 * 1024 * 1024;
    socklen_t tmp = sizeof(buff_size);

    ret = setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)(&buff_size), sizeof(buff_size));
    buff_size = 0;
    ret = getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)(&buff_size), &tmp);
    if (ret)
    {
      fprintf(stderr, "setsockopt buf size failed\n");
      running = 0;
      break;
    }

#ifdef _WIN32
    DWORD timeout = KCP_SOCKET_TIMEOUT;
#else
    struct timeval timeout;
    timeout.tv_sec = KCP_SOCKET_TIMEOUT / 1000;
    timeout.tv_usec = (KCP_SOCKET_TIMEOUT % 1000) * 1000;
#endif
    ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    if (ret)
    {
      fprintf(stderr, "setsockopt timeout failed\n");
      running = 0;
      break;
    }

    receive_from_socket(s);

    sock_close(s);

    ikcp_release(kcp);
  }

  return 0;
}

int main(int argc, char *argv[])
{
  /* GUI */
  struct nk_context *ctx;
  SDL_GLContext glContext;
  int ret;

  NK_UNUSED(argc);
  NK_UNUSED(argv);

  /* SDL setup */
  SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
  SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS);
  // SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_EGL, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  win = SDL_CreateWindow("Demo",
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!win)
  {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return -1;
  }
  glContext = SDL_GL_CreateContext(win);
  if (!glContext)
  {
    fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
    return -1;
  }

  if (!gladLoadGLES2((GLADloadfunc)SDL_GL_GetProcAddress))
  {
    fprintf(stderr, "gladLoadGLES2 failed\n");
    return -1;
  }

  /* OpenGL setup */
  glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

  ctx = nk_sdl_init(win);
  /* Load Fonts: if none of these are loaded a default font will be used  */
  /* Load Cursor: if you uncomment cursor loading please hide the cursor */
  {
    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);
    /*struct nk_font *droid = nk_font_atlas_add_from_file(atlas, "../../../extra_font/DroidSans.ttf", 14, 0);*/
    /*struct nk_font *roboto = nk_font_atlas_add_from_file(atlas, "../../../extra_font/Roboto-Regular.ttf", 16, 0);*/
    /*struct nk_font *future = nk_font_atlas_add_from_file(atlas, "../../../extra_font/kenvector_future_thin.ttf", 13, 0);*/
    /*struct nk_font *clean = nk_font_atlas_add_from_file(atlas, "../../../extra_font/ProggyClean.ttf", 12, 0);*/
    /*struct nk_font *tiny = nk_font_atlas_add_from_file(atlas, "../../../extra_font/ProggyTiny.ttf", 10, 0);*/
    /*struct nk_font *cousine = nk_font_atlas_add_from_file(atlas, "../../../extra_font/Cousine-Regular.ttf", 13, 0);*/
    nk_sdl_font_stash_end();
    /*nk_style_load_all_cursors(ctx, atlas->cursors);*/
    /*nk_style_set_font(ctx, &roboto->handle)*/;
  }

  /* style.c */
  /*set_style(ctx, THEME_WHITE);*/
  /*set_style(ctx, THEME_RED);*/
  /*set_style(ctx, THEME_BLUE);*/
  /*set_style(ctx, THEME_DARK);*/

  sock_startup();

  pthread_t udp_recv_thread;
  if (ret = pthread_create(&udp_recv_thread, NULL, udp_recv_thread_func, NULL))
  {
    fprintf(stderr, "pthread_create failed\n");
    return -1;
  }

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
  emscripten_set_main_loop_arg(MainLoop, (void *)ctx, 0, nk_true);
#else
  while (running)
    MainLoop((void *)ctx);
#endif

  sock_cleanup();
  nk_sdl_shutdown();
  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
