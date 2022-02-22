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

#include <stdatomic.h>
#include <pthread.h>
#include "main.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 960

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

#define UNUSED(a) (void)a
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a) / sizeof(a)[0])

#define FRAME_STAT_EVERY_X_FRAMES 60

/* Platform */
SDL_Window *win;
int running = nk_true;
int win_width, win_height;

#define RP_USE_FRAME_DELTA ((uint32_t)1 << 0)
#define RP_PREDICT_FRAME_DELTA ((uint32_t)1 << 1)
#define RP_SELECT_PREDICTION ((uint32_t)1 << 2)
#define RP_DYNAMIC_ENCODE ((uint32_t)1 << 3)
#define RP_RLE_ENCODE ((uint32_t)1 << 4)
#define RP_DEBUG ((uint32_t)1 << 30)
#define RP_EXTENDED ((uint32_t)1 << 31)

enum ConnectionState
{
  CS_DISCONNECTED,
  CS_CONNECTING,
  CS_CONNECTED,
  CS_DISCONNECTING,
  CS_MAX,
} menu_connection,
    nwm_connection;

atomic_int menu_work_state;
atomic_int nwm_work_state;
atomic_bool menu_remote_play;

static char ip_addr_buf[16];

const char *connection_msg[CS_MAX] = {
    "Connect",
    "Connecting ...",
    "Disconnect",
    "...",
};

static nk_bool prioritize_top_screen;
static int priority_factor;
static int target_bitrate;
static int quality_fac_num;
static int quality_fac_denum;

static nk_bool use_frame_delta;
static nk_bool predict_frame_delta;
static nk_bool select_prediction;
static nk_bool use_dynamic_encode;
static nk_bool use_rle_encode;
static nk_bool rp_dbg_msg;

static int ip_octets[4];

#define HEART_BEAT_EVERY_MS 250
#define REST_EVERY_MS 100

#define RP_MAGIC 0xfff54321
#define TCP_MAGIC 0x12345678
#define TCP_ARGS_COUNT 16
typedef struct _TCPPacketHeader
{
  uint32_t magic;
  uint32_t seq;
  uint32_t type;
  uint32_t cmd;
  uint32_t args[TCP_ARGS_COUNT];

  uint32_t data_len;
} TCPPacketHeader;

int tcp_recv(SOCKET sockfd, char *buf, int size)
{
  int ret, pos = 0;
  int tmpsize = size;

  while (tmpsize)
  {
    if ((ret = recv(sockfd, &buf[pos], tmpsize, 0)) <= 0)
    {
      if (ret < 0)
      {
        if (sock_errno() == WSAEWOULDBLOCK)
        {
          if (pos)
            continue;
          else
            return 0;
        }
      }
      return ret;
    }
    pos += ret;
    tmpsize -= ret;
  }

  return size;
}

int tcp_send(SOCKET sockfd, char *buf, int size)
{
  int ret, pos = 0;
  int tmpsize = size;

  while (tmpsize)
  {
    if ((ret = send(sockfd, &buf[pos], tmpsize, 0)) < 0)
    {
      return ret;
    }
    pos += ret;
    tmpsize -= ret;
  }

  return size;
}

int tcp_send_packet_header(SOCKET s, uint32_t seq, uint32_t type, uint32_t cmd, uint32_t *argv, int argc, uint32_t data_len)
{
  TCPPacketHeader packet;
  packet.magic = TCP_MAGIC;
  packet.seq = seq;
  packet.type = type;
  packet.cmd = cmd;
  for (int i = 0; i < TCP_ARGS_COUNT; ++i)
  {
    if (i < argc)
    {
      packet.args[i] = argv[i];
    }
    else
    {
      packet.args[i] = 0;
    }
  }
  packet.data_len = data_len;

  int ret;
  char *buf = (char *)&packet;
  int size = sizeof(packet);
  return tcp_send(s, buf, size);
}

SOCKET tcp_connect(int port)
{
  struct sockaddr_in servaddr = {0};
  SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (!SOCKET_VALID(sockfd))
  {
    fprintf(stderr, "socket creation failed: %d\n", sock_errno());
    return INVALID_SOCKET;
  }

  servaddr.sin_family = AF_INET;
  snprintf(ip_addr_buf, sizeof(ip_addr_buf),
           "%d.%d.%d.%d",
           ip_octets[0],
           ip_octets[1],
           ip_octets[2],
           ip_octets[3]);
  servaddr.sin_addr.s_addr = inet_addr(ip_addr_buf);
  servaddr.sin_port = htons(port);

  fprintf(stderr, "connecting to %s ...\n", ip_addr_buf);
  int ret = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
  if (ret != 0)
  {
    fprintf(stderr, "connection failed: %d\n", sock_errno());
    sock_close(sockfd);
    return INVALID_SOCKET;
  }
  fprintf(stderr, "connected\n");

  u_long mode = 1;
  ioctlsocket(sockfd, FIONBIO, &mode);

  return sockfd;
}

#define RESET_SOCKET(ts, ws) \
  sock_close(sockfd);        \
  sockfd = INVALID_SOCKET;   \
  ts = 0;                    \
  ws = CS_DISCONNECTED;      \
  fprintf(stderr, "disconnected\n");

void *menu_tcp_thread_func(void *arg)
{
  int menu_tcp_status = 0;
  SOCKET sockfd = INVALID_SOCKET;
  int packet_seq = 0;
  while (running)
  {
    if (!menu_tcp_status && menu_work_state == CS_CONNECTING)
    {
      sockfd = tcp_connect(8000);
      if (!SOCKET_VALID(sockfd))
      {
        menu_work_state = CS_DISCONNECTED;
        continue;
      }

      packet_seq = 0;
      menu_tcp_status = 1;
      menu_work_state = CS_CONNECTED;
    }
    else if (menu_tcp_status && menu_work_state == CS_DISCONNECTING)
    {
      RESET_SOCKET(menu_tcp_status, menu_work_state)
    }
    else if (menu_tcp_status)
    {
      Sleep(HEART_BEAT_EVERY_MS);

      TCPPacketHeader header = {0};
      char *buf = (char *)&header;
      int size = sizeof(header);
      int ret;
      if ((ret = tcp_recv(sockfd, buf, size)) < 0)
      {
        fprintf(stderr, "tcp recv error: %d\n", sock_errno());
        RESET_SOCKET(menu_tcp_status, menu_work_state)
        continue;
      }
      if (ret)
      {
        if (header.magic != TCP_MAGIC)
        {
          fprintf(stderr, "broken protocol\n");
          RESET_SOCKET(menu_tcp_status, menu_work_state)
          continue;
        }
        if (header.cmd == 0)
        {
          if (header.data_len)
          {
            char *buf = malloc(header.data_len + 1);
            if ((ret = tcp_recv(sockfd, buf, header.data_len)) < 0)
            {
              fprintf(stderr, "heart beat recv error: %d\n", sock_errno());
              free(buf);
              RESET_SOCKET(menu_tcp_status, menu_work_state)
              continue;
            }
            if (ret)
            {
              buf[header.data_len] = 0;
              fprintf(stderr, "%s", buf);
            }
            free(buf);
          }
        }
        else
        {
          char *buf = malloc(header.data_len);
          if ((ret = tcp_recv(sockfd, buf, header.data_len)) < 0)
          {
            fprintf(stderr, "tcp recv error: %d\n", sock_errno());
            free(buf);
            RESET_SOCKET(menu_tcp_status, menu_work_state)
            continue;
          }
          free(buf);
        }
      }

      ret = tcp_send_packet_header(sockfd, packet_seq, 0, 0, 0, 0, 0);
      if (ret < 0)
      {
        fprintf(stderr, "heart beat send failed: %d\n", sock_errno());
        RESET_SOCKET(menu_tcp_status, menu_work_state)
      }
      ++packet_seq;

      if (menu_remote_play)
      {
        menu_remote_play = 0;
        uint32_t flags = 0;
        if (use_frame_delta)
          flags |= RP_USE_FRAME_DELTA;
        if (predict_frame_delta)
          flags |= RP_PREDICT_FRAME_DELTA;
        if (select_prediction)
          flags |= RP_SELECT_PREDICTION;
        if (use_dynamic_encode)
          flags |= RP_DYNAMIC_ENCODE;
        if (use_rle_encode)
          flags |= RP_RLE_ENCODE;
        if (rp_dbg_msg)
          flags |= RP_DEBUG;
        uint32_t args[] = {
            !!prioritize_top_screen << 8 | priority_factor,
            RP_MAGIC,
            target_bitrate,
            flags,
            quality_fac_denum << 8 | quality_fac_num};
        ret = tcp_send_packet_header(sockfd, packet_seq, 0, 901,
                                     args, sizeof(args) / sizeof(*args), 0);
        if (ret < 0)
        {
          fprintf(stderr, "remote play send failed: %d\n", sock_errno());
          RESET_SOCKET(menu_tcp_status, menu_work_state)
        }
        ++packet_seq;
      }
    }
    else
    {
      Sleep(REST_EVERY_MS);
    }
  }
  return 0;
}

void *nwm_tcp_thread_func(void *arg)
{
  int nwm_tcp_status = 0;
  SOCKET sockfd = INVALID_SOCKET;
  int packet_seq = 0;
  while (running)
  {
    if (!nwm_tcp_status && nwm_work_state == CS_CONNECTING)
    {
      sockfd = tcp_connect(5000 + 0x1a);
      if (!SOCKET_VALID(sockfd))
      {
        nwm_work_state = CS_DISCONNECTED;
        continue;
      }

      packet_seq = 0;
      nwm_tcp_status = 1;
      nwm_work_state = CS_CONNECTED;
    }
    else if (nwm_tcp_status && nwm_work_state == CS_DISCONNECTING)
    {
      RESET_SOCKET(nwm_tcp_status, nwm_work_state)
    }
    else if (nwm_tcp_status)
    {
      Sleep(HEART_BEAT_EVERY_MS);

      TCPPacketHeader header = {0};
      char *buf = (char *)&header;
      int size = sizeof(header);
      int ret;
      if ((ret = tcp_recv(sockfd, buf, size)) < 0)
      {
        fprintf(stderr, "tcp recv error: %d\n", sock_errno());
        RESET_SOCKET(nwm_tcp_status, nwm_work_state)
        continue;
      }
      if (ret)
      {
        if (header.magic != TCP_MAGIC)
        {
          fprintf(stderr, "broken protocol\n");
          RESET_SOCKET(nwm_tcp_status, nwm_work_state)
          continue;
        }
        if (header.cmd == 0)
        {
          if (header.data_len)
          {
            char *buf = malloc(header.data_len + 1);
            if ((ret = tcp_recv(sockfd, buf, header.data_len)) < 0)
            {
              fprintf(stderr, "heart beat recv error: %d\n", sock_errno());
              free(buf);
              RESET_SOCKET(nwm_tcp_status, nwm_work_state)
              continue;
            }
            if (ret)
            {
              buf[header.data_len] = 0;
              fprintf(stderr, "%s", buf);
            }
            free(buf);
          }
        }
        else
        {
          char *buf = malloc(header.data_len);
          if ((ret = tcp_recv(sockfd, buf, header.data_len)) < 0)
          {
            fprintf(stderr, "tcp recv error: %d\n", sock_errno());
            free(buf);
            RESET_SOCKET(nwm_tcp_status, nwm_work_state)
            continue;
          }
          free(buf);
        }
      }

      ret = tcp_send_packet_header(sockfd, packet_seq, 0, 0, 0, 0, 0);
      if (ret < 0)
      {
        fprintf(stderr, "heart beat send failed: %d\n", sock_errno());
        RESET_SOCKET(nwm_tcp_status, nwm_work_state)
      }
      ++packet_seq;
    }
    else
    {
      Sleep(REST_EVERY_MS);
    }
  }
  return 0;
}

void rpConfigSetDefault(void)
{
  prioritize_top_screen = 1;
  priority_factor = 10;
  target_bitrate = 1024 * 512 * 24;
  quality_fac_num = 2;
  quality_fac_denum = 1;

  use_frame_delta = 1;
  predict_frame_delta = 0;
  select_prediction = 0;
  use_dynamic_encode = 1;
  use_rle_encode = 1;
  rp_dbg_msg = 0;
}

static void guiMain(struct nk_context *ctx)
{
  static int hide_windows = 0;

  struct nk_style_item fixed_background = ctx->style.window.fixed_background;
  ctx->style.window.fixed_background = nk_style_item_hide();
  const char *background_wnd = "Background";
  if (nk_begin(ctx, background_wnd, nk_rect(0, 0, win_width, win_height),
               NK_WINDOW_BACKGROUND))
  {
    if (nk_window_is_hovered(ctx) && nk_window_is_active(ctx, background_wnd) &&
        nk_input_has_mouse_click(&ctx->input, NK_BUTTON_LEFT))
    {
      hide_windows = !hide_windows;
    }
  }
  nk_end(ctx);
  ctx->style.window.fixed_background = fixed_background;

  enum nk_show_states show_window = !hide_windows;

  static char msg_buf[250];

  /* GUI */
  const char *remote_play_wnd = "Remote Play";
  if (nk_begin(ctx, remote_play_wnd, nk_rect(50, 50, 400, 500),
               NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE))
  {
    nk_layout_row_dynamic(ctx, 30, 5);
    nk_label(ctx, "IP", NK_TEXT_CENTERED);
    nk_property_int(ctx, "#", 0, &ip_octets[0], 255, 1, 1);
    nk_property_int(ctx, "#", 0, &ip_octets[1], 255, 1, 1);
    nk_property_int(ctx, "#", 0, &ip_octets[2], 255, 1, 1);
    nk_property_int(ctx, "#", 0, &ip_octets[3], 255, 1, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Prioritize top screen", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &prioritize_top_screen);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Priority factor %d", priority_factor);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &priority_factor, 15, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Target bitrate %.1f Mbps", (double)target_bitrate / 1024 / 1024);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 1024 * 512 * 3, &target_bitrate, 1024 * 512 * 36, 1024 * 512);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Quality factor %d", quality_fac_num);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 1, &quality_fac_num, 4, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "/ %d", quality_fac_denum);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 1, &quality_fac_denum, 4, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Use frame delta", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &use_frame_delta);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Predict frame delta", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &predict_frame_delta);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Select prediction", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &select_prediction);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Dynamic encode", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &use_dynamic_encode);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "RLE encode", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &use_rle_encode);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Debug Message", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &rp_dbg_msg);

    nk_layout_row_dynamic(ctx, 30, 2);
    if (nk_button_label(ctx, "Default"))
    {
      rpConfigSetDefault();
    }
    if (nk_button_label(ctx, "Connect"))
    {
      if (menu_work_state == CS_DISCONNECTED)
      {
        menu_work_state = CS_CONNECTING;
      }
      menu_remote_play = TRUE;
    }
  }
  nk_end(ctx);
  nk_window_show(ctx, remote_play_wnd, show_window);

  const char *debug_msg_wnd = "Debug Msg";
  if (nk_begin(ctx, debug_msg_wnd, nk_rect(500, 50, 250, 120),
               NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE))
  {
    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Menu", NK_TEXT_CENTERED);
    menu_connection = menu_work_state;
    if (nk_button_label(ctx, connection_msg[menu_connection]))
    {
      if (menu_work_state == CS_DISCONNECTED)
      {
        menu_work_state = CS_CONNECTING;
      }
      else if (menu_work_state == CS_CONNECTED)
      {
        menu_work_state = CS_DISCONNECTING;
      }
    }
    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "NWM", NK_TEXT_CENTERED);
    nwm_connection = nwm_work_state;
    if (nk_button_label(ctx, connection_msg[nwm_connection]))
    {
      if (nwm_work_state == CS_DISCONNECTED)
      {
        nwm_work_state = CS_CONNECTING;
      }
      else if (nwm_work_state == CS_CONNECTED)
      {
        nwm_work_state = CS_DISCONNECTING;
      }
    }
  }
  nk_end(ctx);
  nk_window_show(ctx, debug_msg_wnd, show_window);
}

static GLbyte vShaderStr[] =
    "attribute vec4 a_position; \n"
    "attribute vec2 a_texCoord; \n"
    "varying vec2 v_texCoord; \n"
    "void main() \n"
    "{ \n"
    " gl_Position = a_position; \n"
    " v_texCoord = a_texCoord; \n"
    "} \n";

static GLbyte fShaderStr[] =
    "precision mediump float; \n"
    "varying vec2 v_texCoord; \n"
    "uniform sampler2D s_texture; \n"
    "void main() \n"
    "{ \n"
    " gl_FragColor = texture2D(s_texture, v_texCoord); \n"
    "} \n";

static GLfloat vVertices_pos[4][3] = {
    -0.5f, 0.5f, 0.0f,  // Position 0
    -0.5f, -0.5f, 0.0f, // Position 1
    0.5f, -0.5f, 0.0f,  // Position 2
    0.5f, 0.5f, 0.0f,   // Position 3
};

static GLfloat vVertices_tex_coord[4][2] = {
    0.0f, 1.0f, // TexCoord 0
    0.0f, 0.0f, // TexCoord 1
    1.0f, 0.0f, // TexCoord 2
    1.0f, 1.0f  // TexCoord 3
};
static GLushort indices[] =
    {0, 1, 2, 0, 2, 3};

static GLuint loadShader(GLenum type, const char *shaderSrc)
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

  if (!compiled)
  {
    GLint infoLen = 0;

    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);

    if (infoLen > 1)
    {
      char *infoLog = malloc(sizeof(char) * infoLen);

      glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
      fprintf(stderr, "Error compiling shader: %s\n", infoLog);

      free(infoLog);
    }

    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

GLuint LoadProgram(const char *vertShaderSrc, const char *fragShaderSrc)
{
  GLuint vertexShader;
  GLuint fragmentShader;
  GLuint programObject;
  GLint linked;

  // Load the vertex/fragment shaders
  vertexShader = loadShader(GL_VERTEX_SHADER, vertShaderSrc);
  if (vertexShader == 0)
    return 0;

  fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragShaderSrc);
  if (fragmentShader == 0)
  {
    glDeleteShader(vertexShader);
    return 0;
  }

  // Create the program object
  programObject = glCreateProgram();

  if (programObject == 0)
    return 0;

  glAttachShader(programObject, vertexShader);
  glAttachShader(programObject, fragmentShader);

  // Link the program
  glLinkProgram(programObject);

  // Check the link status
  glGetProgramiv(programObject, GL_LINK_STATUS, &linked);

  if (!linked)
  {
    GLint infoLen = 0;

    glGetProgramiv(programObject, GL_INFO_LOG_LENGTH, &infoLen);

    if (infoLen > 1)
    {
      char *infoLog = malloc(sizeof(char) * infoLen);

      glGetProgramInfoLog(programObject, infoLen, NULL, infoLog);
      fprintf(stderr, "Error linking program: %s\n", infoLog);

      free(infoLog);
    }

    glDeleteProgram(programObject);
    return 0;
  }

  // Free up no longer needed shader resources
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  return programObject;
}

pthread_mutex_t gl_tex_mutex;
GLuint top_tex_id;
GLuint bot_tex_id;
GLuint gl_program;
GLint gl_position_loc;
GLint gl_tex_coord_loc;
GLint gl_sampler_loc;

enum FrameBufferStatus
{
  FBS_NOT_AVAIL,
  FBS_UPDATED,
  FBS_NOT_UPDATED,
};

typedef struct _FrameBufferContext
{
  uint8_t *images[3];
  enum FrameBufferStatus updated;
  int index;
  int next_index;
} FrameBufferContext;

FrameBufferContext top_buffer_ctx, bot_buffer_ctx;

int frame_buffer_context_next_free_index(FrameBufferContext *ctx)
{
  int next_index = (ctx->next_index + 1) % 3;
  if (next_index == ctx->index)
  {
    next_index = (ctx->next_index + 1) % 3;
  }
  return next_index;
}

static void hr_draw_screen(FrameBufferContext *ctx, int width, int height, int isTop)
{
  if (ctx->updated == FBS_NOT_AVAIL)
  {
    return;
  }

  int ctx_height = (double)win_height / 2;
  int ctx_width;
  int ctx_left;
  int ctx_top;
  if ((double)win_width / width * height > ctx_height)
  {
    ctx_width = (double)ctx_height / height * width;
    ctx_left = (double)(win_width - ctx_width) / 2;
    ctx_top = 0;
  }
  else
  {
    ctx_height = (double)win_width / width * height;
    ctx_left = 0;
    ctx_width = win_width;
    ctx_top = (double)win_height / 2 - ctx_height;
  }

  double ctx_left_f;
  double ctx_top_f;
  double ctx_right_f;
  double ctx_bot_f;
  if (isTop)
  {
    ctx_left_f = (double)ctx_left / win_width * 2 - 1;
    ctx_top_f = 1 - (double)ctx_top / win_height * 2;
    ctx_right_f = -ctx_left_f;
    ctx_bot_f = 0;
  }
  else
  {
    ctx_left_f = (double)ctx_left / win_width * 2 - 1;
    ctx_top_f = 0;
    ctx_right_f = -ctx_left_f;
    ctx_bot_f = -1 + (double)ctx_top / win_height * 2;
  }
  vVertices_pos[0][0] = ctx_left_f;
  vVertices_pos[0][1] = ctx_top_f;
  vVertices_pos[1][0] = ctx_left_f;
  vVertices_pos[1][1] = ctx_bot_f;
  vVertices_pos[2][0] = ctx_right_f;
  vVertices_pos[2][1] = ctx_bot_f;
  vVertices_pos[3][0] = ctx_right_f;
  vVertices_pos[3][1] = ctx_top_f;

  glUseProgram(gl_program);
  glVertexAttribPointer(gl_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(*vVertices_pos), vVertices_pos);
  glVertexAttribPointer(gl_tex_coord_loc, 2, GL_FLOAT, GL_FALSE, sizeof(*vVertices_tex_coord), vVertices_tex_coord);

  glEnableVertexAttribArray(gl_position_loc);
  glEnableVertexAttribArray(gl_tex_coord_loc);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, isTop ? top_tex_id : bot_tex_id);

  static int frame_counter = 0;
  static uint64_t frame_count_last_tick = 0;

  pthread_mutex_lock(&gl_tex_mutex);
  if (ctx->updated == FBS_UPDATED)
  {
    int next_index = frame_buffer_context_next_free_index(ctx);
    glTexImage2D(GL_TEXTURE_2D, 0,
                 GL_RGB, width,
                 height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE,
                 ctx->images[ctx->next_index]);
    ctx->index = ctx->next_index;
    ctx->next_index = next_index;
    ctx->updated = FBS_NOT_UPDATED;

    ++frame_counter;
    if (frame_counter == FRAME_STAT_EVERY_X_FRAMES)
    {
      uint64_t next_tick = iclock64();
      if (frame_count_last_tick != 0)
      {
        // fprintf(stderr, "%d ms for %d rendered frames\n", next_tick - frame_count_last_tick, FRAME_STAT_EVERY_X_FRAMES);
      }
      frame_count_last_tick = next_tick;
      frame_counter = 0;
    }
  }
  pthread_mutex_unlock(&gl_tex_mutex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenerateMipmap(GL_TEXTURE_2D);

  glUniform1i(gl_sampler_loc, 0);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
}

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

  guiMain(ctx);

  /* Draw */
  {
    float bg[4];
    nk_color_fv(bg, nk_rgb(28, 48, 62));
    SDL_GetWindowSize(win, &win_width, &win_height);
    glViewport(0, 0, win_width, win_height);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(bg[0], bg[1], bg[2], bg[3]);

    hr_draw_screen(&top_buffer_ctx, 400, 240, 1);
    hr_draw_screen(&bot_buffer_ctx, 320, 240, 0);

    /* IMPORTANT: `nk_sdl_render` modifies some global OpenGL state
     * with blending, scissor, face culling, depth test and viewport and
     * defaults everything back into a default state.
     * Make sure to either a.) save and restore or b.) reset your own state after
     * rendering the UI. */
    nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);
    SDL_GL_SwapWindow(win);
  }
}

#include "huffmancodec.h"
#include "rlecodec.h"
#include "framecodec.h"

#define BUF_SIZE 2000
uint8_t buf[BUF_SIZE];
ikcpcb *kcp;

int recv_new_frame;

#define FRAME_RECV_BUFFER_SIZE (96000 * 3 / 2 + sizeof(DataHeader))
uint8_t frame_recv_buffer[FRAME_RECV_BUFFER_SIZE];

#define FRAME_RLE_DEC_SIZE (96000 * 3 / 2)
uint8_t frame_rle_dec_buffer[FRAME_RLE_DEC_SIZE];

#define FRAME_HUFFMAN_DEC_SIZE (96000 * 3 / 2)
uint8_t frame_huffman_dec_buffer[FRAME_HUFFMAN_DEC_SIZE];

void handle_decode_frame_screen(FrameBufferContext *ctx, uint8_t *rgb)
{
  pthread_mutex_lock(&gl_tex_mutex);
  int next_index = frame_buffer_context_next_free_index(ctx);
  uint8_t **image = &ctx->images[next_index];
  free(*image);
  *image = rgb;
  ctx->updated = FBS_UPDATED;
  ctx->next_index = next_index;
  pthread_mutex_unlock(&gl_tex_mutex);
}

#define RP_CONTROL_TOP_KEY (1 << 0)
#define RP_CONTROL_BOT_KEY (1 << 0)

void handle_decoded_frame(DataHeader header, uint8_t *data, int data_size)
{
  uint8_t *frame = frame_decode(header, data, data_size);
  if (frame)
  {
    if (header.flags & RP_DATA_TOP)
    {
      handle_decode_frame_screen(&top_buffer_ctx, frame);
    }
    else
    {
      handle_decode_frame_screen(&bot_buffer_ctx, frame);
    }
  }
  else if (!(header.flags & RP_DATA_Y))
  {
    uint8_t flags;
    if (header.flags & RP_DATA_TOP)
    {
      flags |= RP_CONTROL_TOP_KEY;
    }
    else
    {
      flags |= RP_CONTROL_BOT_KEY;
    }
    ikcp_send(kcp, &flags, sizeof(flags));
  }
}

int handle_frame_recv(void)
{
  DataHeader header;
  memcpy(&header, frame_recv_buffer, sizeof(DataHeader));

  uint8_t *compressed = frame_recv_buffer + sizeof(DataHeader);
  int ret;

  int expected_size = header.flags & RP_DATA_TOP ? 96000 : 76800;
  if (!(header.flags & RP_DATA_Y))
  {
    expected_size /= 2;
  }
  if (header.flags & RP_DATA_DS)
  {
    expected_size /= 4;
  }

  if (expected_size != header.uncompressed_len)
  {
    fprintf(stderr, "frame data error\n");
  }
  // fprintf(stderr, "frame receive %d %d\n",
  //         header.flags, header.len + sizeof(DataHeader));
  // fprintf(stderr, "receive %d %d %d %d %d\n", header.flags, header.len, header.id, header.uncompressed_len, expected_size);

  if (FRAME_HUFFMAN_DEC_SIZE < expected_size)
  {
    fprintf(stderr, "FRAME_HUFFMAN_DEC_SIZE too small %d\n", expected_size);
    return -1;
  }

  if (header.flags & RP_DATA_RLE)
  {
    ret = rle_decode(frame_rle_dec_buffer, FRAME_RLE_DEC_SIZE, compressed, header.len);
    if (ret < 0)
    {
      fprintf(stderr, "rle_decode error: %d\n", ret);
      return -1;
    }

    ret = huffman_decode(frame_huffman_dec_buffer, expected_size, frame_rle_dec_buffer, ret);
    if (ret < 0)
    {
      fprintf(stderr, "huffman_decode after rle error: %d\n", ret);
      return -1;
    }
  }
  else
  {
    ret = huffman_decode(frame_huffman_dec_buffer, expected_size, compressed, header.len);
    if (ret < 0)
    {
      fprintf(stderr, "huffman_decode error: %d\n", ret);
      return -1;
    }
  }
  handle_decoded_frame(header, frame_huffman_dec_buffer, ret);
  return 0;
}

static uint32_t rpFrameId = 0;
static uint32_t rpPacketId = 0;
typedef struct _PacketHeader
{
  uint32_t id;
  uint32_t len;
} PacketHeader;

uint8_t *frame_recv_ptr;
int frame_size_remain;
int handle_recv(uint8_t *buf, int size)
{

  if (size >= sizeof(PacketHeader))
  {
    PacketHeader packet_header;
    memcpy(&packet_header, buf, sizeof(PacketHeader));
    if (packet_header.id != ++rpPacketId)
    {
      // fprintf(stderr, "Packet id mismatch %d (expected %d)\n", packet_header.id, rpPacketId);
      rpPacketId = packet_header.id;
    }
    if (packet_header.len != size - sizeof(PacketHeader))
    {
      fprintf(stderr, "Packet size mismatch %d (expected %d)\n", size - sizeof(PacketHeader), packet_header.len);
    }
  }
  else
  {
    fprintf(stderr, "Packet too small\n");
    return -1;
  }
  buf += sizeof(PacketHeader);
  size -= sizeof(PacketHeader);
  if (recv_new_frame)
  {
    frame_recv_ptr = frame_recv_buffer;
    recv_new_frame = 0;
  }
  // fprintf(stderr, "handle_recv %d %d\n", frame_recv_ptr - frame_recv_buffer, size);
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
    if (data_header.id != ++rpFrameId)
    {
      // fprintf(stderr, "Frame id mismatch %d (expected %d)\n", data_header.id, rpFrameId);
      rpFrameId = data_header.id;
    }
    // fprintf(stderr, "new frame %d %d\n", data_header.flags, data_header.len + sizeof(DataHeader));

    static int frame_counter = 0;
    static uint64_t frame_count_last_tick = 0;
    if (!(data_header.flags & RP_DATA_Y))
    {
      ++frame_counter;
    }
    if (frame_counter == FRAME_STAT_EVERY_X_FRAMES)
    {
      uint64_t next_tick = iclock64();
      if (frame_count_last_tick != 0)
      {
        // fprintf(stderr, "%d ms for %d frames\n", next_tick - frame_count_last_tick, FRAME_STAT_EVERY_X_FRAMES);
      }
      frame_count_last_tick = next_tick;
      frame_counter = 0;
    }
  }
  if (frame_size_remain < size)
  {
    fprintf(stderr, "handle_recv data malformed\n");
    return -2;
    // recv_new_frame = 1;
    // if (handle_frame_recv() < 0)
    // {
    //   return -2;
    // }
    // return handle_recv(buf + frame_size_remain, size - frame_size_remain);
  }
  frame_recv_ptr += size;
  frame_size_remain -= size;
  if (frame_size_remain == 0)
  {
    recv_new_frame = 1;
    if (handle_frame_recv() < 0)
    {
      return -3;
    }
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

IUINT32 kcpLastRecvMs = 0;
#define KCP_RECV_TIMEOUT_MS 250
void receive_from_socket(SOCKET s)
{
  kcpLastRecvMs = iclock();
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
          // fprintf(stderr, "ikcp_recv timeout: %d\n", kcpLastRecvMs);
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
    rpFrameId = 0;
    rpPacketId = 0;
    // ikcp_setmtu(kcp, 1400);
    ikcp_nodelay(kcp, 1, 10, 2, 1);
    ikcp_wndsize(kcp, 128, 256);
    // fprintf(stderr, "new connection\n");
    frame_decode_destroy();
    frame_decode_init();
    top_buffer_ctx.updated = FBS_NOT_AVAIL;
    bot_buffer_ctx.updated = FBS_NOT_AVAIL;

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

    Sleep(1000);
  }

  return 0;
}

#include "style.c"

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
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_EGL, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  win = SDL_CreateWindow("NTR Viewer HR",
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
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
  // set_style(ctx, THEME_WHITE);
  // set_style(ctx, THEME_RED);
  // set_style(ctx, THEME_BLUE);
  set_style(ctx, THEME_DARK);

  sock_startup();

  glGenTextures(1, &top_tex_id);
  glGenTextures(1, &bot_tex_id);
  gl_program = LoadProgram(vShaderStr, fShaderStr);
  gl_position_loc = glGetAttribLocation(gl_program, "a_position");
  gl_tex_coord_loc = glGetAttribLocation(gl_program, "a_texCoord");
  gl_sampler_loc = glGetUniformLocation(gl_program, "s_texture");
  pthread_mutex_init(&gl_tex_mutex, NULL);
  frame_decode_init();

  pthread_t udp_recv_thread;
  if (ret = pthread_create(&udp_recv_thread, NULL, udp_recv_thread_func, NULL))
  {
    fprintf(stderr, "pthread_create failed\n");
    return -1;
  }
  pthread_t menu_tcp_thread;
  if (ret = pthread_create(&menu_tcp_thread, NULL, menu_tcp_thread_func, NULL))
  {
    fprintf(stderr, "pthread_create failed\n");
    return -1;
  }
  pthread_t nwm_tcp_thread;
  if (ret = pthread_create(&nwm_tcp_thread, NULL, nwm_tcp_thread_func, NULL))
  {
    fprintf(stderr, "pthread_create failed\n");
    return -1;
  }

  rpConfigSetDefault();

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
  emscripten_set_main_loop_arg(MainLoop, (void *)ctx, 0, nk_true);
#else
  while (running)
    MainLoop((void *)ctx);
#endif

  pthread_join(udp_recv_thread, NULL);
  pthread_join(menu_tcp_thread, NULL);
  pthread_join(nwm_tcp_thread, NULL);

  frame_decode_destroy();
  pthread_mutex_destroy(&gl_tex_mutex);

  glDeleteProgram(gl_program);
  glDeleteTextures(1, &top_tex_id);
  glDeleteTextures(1, &bot_tex_id);

  sock_cleanup();
  nk_sdl_shutdown();
  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
