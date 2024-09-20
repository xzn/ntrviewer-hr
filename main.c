#ifdef _WIN32
#define WINVER _WIN32_WINNT_WINBLUE
#define _WIN32_WINNT _WIN32_WINNT_WINBLUE
#include <winsock2.h>
#include <ws2tcpip.h>
#define SOCKET_VALID(s) ((s) != INVALID_SOCKET)
#define sock_errno() WSAGetLastError()
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
typedef int SOCKET;
#define SOCKET_VALID(s) ((int)(s) >= 0)
#define sock_errno() errno
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAETIMEDOUT ETIMEDOUT
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (-1)
#define closesocket close
#define WSAPOLLFD struct pollfd
#define WSAPoll poll
#define SD_BOTH SHUT_RDWR

void Sleep(int milliseconds) {
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

#endif

#include <stdatomic.h>
#include <errno.h>
#include "realcugan-ncnn-vulkan/lib.h"
#include "main.h"
#include "rp_syn.h"
#include "Presentation.h"
#include <SDL2/SDL_syswm.h>

rp_sem_t jpeg_decode_sem;
struct rp_syn_comp_func_t jpeg_decode_queue;

// #define USE_SDL_RENDERER
// #define GL_DEBUG
// #define USE_ANGLE
// #define USE_OGL_ES
#ifndef USE_OGL_ES
#define USE_VAO
#endif
// #define PRINT_PACKET_LOSS_INFO

#ifndef USE_SDL_RENDERER
#define screen_upscale_factor REALCUGAN_SCALE
#define sr_create realcugan_create
#define sr_run realcugan_run
#define sr_next realcugan_next
#define sr_destroy realcugan_destroy
#else
#define screen_upscale_factor (1)
#define sr_create(...) (0)
#define sr_run(...) (0)
#define sr_next(...) ((void)0)
#define sr_destroy(...) ((void)0)
#endif

#define HR_MAX(a, b) ((a) > (b) ? (a) : (b))
#define HR_MIN(a, b) ((a) < (b) ? (a) : (b))

#if defined(USE_SDL_RENDERER) || (defined(USE_ANGLE) && defined(_WIN32))
#define SDL_GL_SINGLE_THREAD
#endif

#define GL_CHANNELS_N 4
#define GL_FORMAT GL_RGBA
#define GL_INT_FORMAT GL_RGBA8
#define TJ_FORMAT TJPF_RGBA
#define JCS_FORMAT JCS_EXT_RGBA

#define err_log(f, ...) fprintf(stderr, "%s:%d:%s " f, __FILE__, __LINE__, __func__, ## __VA_ARGS__)

#ifdef EMBED_JPEG_TURBO
#include "jpeg_turbo/jpeglib.h"
#endif
#include <turbojpeg.h>

#include "ikcp.h"

#define RP_MAX(a,b) ((a) > (b) ? (a) : (b))
#define RP_MIN(a,b) ((a) > (b) ? (b) : (a))

#define RP_SOCKET_INTERVAL (250)

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

  status = shutdown(sock, SD_BOTH);
  if (status != 0)
  {
    err_log("socket shudown failed: %d\n", sock_errno());
  }
  status = closesocket(sock);

  return status;
}

#include <stdint.h>
#include <time.h>

int running = 1;

static inline void itimeofday(int64_t *sec, int64_t *usec)
{
#ifdef _WIN32
  static int64_t mode = 0;
  static int64_t freq = 1;
  int64_t qpc;
  if (mode == 0)
  {
    if (!QueryPerformanceFrequency((LARGE_INTEGER *)&freq)) {
      running = 0;
      *sec = *usec = 0;
      return;
    }
    freq = (freq == 0) ? 1 : freq;
    mode = 1;
  }
  if (!QueryPerformanceCounter((LARGE_INTEGER *)&qpc)) {
    running = 0;
    *sec = *usec = 0;
    return;
  }
  if (sec)
    *sec = (int64_t)(qpc / freq);
  if (usec)
    *usec = (int64_t)((qpc % freq) * 1000000 / freq);
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
    running = 0;
    *sec = *usec = 0;
    return;
  }
  if (sec)
    *sec = ts.tv_sec;
  if (usec)
    *usec = ts.tv_nsec / 1000;
#endif
}

static inline int64_t iclock64(void)
{
  int64_t s, u;
  int64_t value;
  itimeofday(&s, &u);
  value = ((int64_t)s) * 1000000 + u;
  return value;
}

static inline uint32_t iclock()
{
  return (uint32_t)(iclock64() & 0xfffffffful);
}

// #ifndef SDL_GL_SINGLE_THREAD
// #ifndef _WIN32
// #define SDL_GL_SYNC
// #endif
// #endif

static void cond_mutex_flag_signal(int *flag, rp_cond_t *cond, rp_lock_t *mutex) {
  rp_lock_wait(*mutex);
  __atomic_store_n(flag, 1, __ATOMIC_RELAXED);
  rp_cond_rel(*cond);
  rp_lock_rel(*mutex);
}

#ifdef SDL_GL_SYNC
static bool cond_mutex_flag_lock(int *flag, rp_cond_t *cond, rp_lock_t *mutex) {
  rp_lock_wait(*mutex);
  while (!__atomic_load_n(flag, __ATOMIC_RELAXED)) {
    if (!running) {
      rp_lock_rel(*mutex);
      return false;
    }
    int ret = rp_cond_timedwait(*cond, *mutex, NWM_THREAD_WAIT_NS);
    if (ret) {
      if (ret != ETIMEDOUT) {
        err_log("rp_cond_timedwait error: %d", ret);
        running = 0;
        rp_lock_rel(*mutex);
        return false;
      }
    }
  }
  return true;
}
#endif

#ifdef _WIN32
#define thread_t HANDLE
#define thread_ret_t DWORD
#define thread_create(t, f, a) ({ \
  HANDLE _res = CreateThread(NULL, 0, f, a, 0, NULL); \
  (t) = _res; \
  _res ? 0 : -1; \
})
#define thread_exit(n) ExitThread((thread_ret_t)n)
#define thread_join(t) WaitForSingleObject(t, INFINITE)
#define thread_set_cancel(e) SetEvent(e)
#define thread_cancel(t) ((void)0)
#define thread_set_cancel_state(b) ((void)0)
#else
#define thread_t pthread_t
#define thread_ret_t (void *)
#define thread_create(t, f, a) pthread_create(&(t), NULL, f, a)
#define thread_exit(n) pthread_exit((thread_ret_t)n)
#define thread_join(t) pthread_join(t, NULL)
#define thread_cancel(t) pthread_cancel(t)
#define thread_set_cancel_state(b) pthread_setcancelstate(b ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE, NULL)
#endif

//! 8 bit unsigned integer.
typedef uint8_t		byte;

//! 8 bit unsigned integer.
typedef uint8_t		u8;
//! 16 bit unsigned integer.
typedef uint16_t	u16;
//! 32 bit unsigned integer.
typedef uint32_t	u32;
//! 64 bit unsigned integer.
typedef uint64_t	u64;

//! 8 bit signed integer.
typedef int8_t		s8;
//! 16 bit signed integer.
typedef int16_t		s16;
//! 32 bit signed integer.
typedef int32_t		s32;
//! 64 bit signed integer.
typedef int64_t		s64;

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 960
#define WINDOW_WIDTH2 640
#define WINDOW_HEIGHT12 480

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

#define UNUSED(a) (void)a
// #define MIN(a, b) ((a) < (b) ? (a) : (b))
// #define MAX(a, b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a) / sizeof(a)[0])

#define FRAME_STAT_EVERY_X_US 1000000
char window_title_with_fps[500];
uint64_t window_title_last_tick;
int frame_rate_decoded_tracker[SCREEN_COUNT] = {};
int frame_rate_displayed_tracker[SCREEN_COUNT] = {};
int frame_size_tracker[SCREEN_COUNT] = {};
int delay_between_packet_tracker[SCREEN_COUNT] = {};
int frame_fully_received_tracker;
int frame_lost_tracker;

enum FrameBufferStatus
{
  FBS_NOT_AVAIL = -1,
  FBS_NOT_UPDATED,
  FBS_UPDATED,
  FBS_UPDATED_2,
};

/* Platform */
SDL_Window *win[SCREEN_COUNT];
Uint32 win_id[SCREEN_COUNT];
#ifdef USE_SDL_RENDERER
SDL_Renderer *sdlRenderer[SCREEN_COUNT];
SDL_Texture *sdlTexture[SCREEN_COUNT][SCREEN_COUNT];
#else
SDL_GLContext glContext[SCREEN_COUNT];
#endif
int win_width[SCREEN_COUNT], win_height[SCREEN_COUNT];
int ogl_version_major, ogl_version_minor;

#ifdef USE_VAO
GLuint glVao[SCREEN_COUNT][SCREEN_COUNT];
GLuint glVbo[SCREEN_COUNT][SCREEN_COUNT];
GLuint glEbo[SCREEN_COUNT];

GLuint glFboVao[SCREEN_COUNT];
GLuint glFboVbo[SCREEN_COUNT];
GLuint glFboEbo[SCREEN_COUNT];
#endif

enum ConnectionState
{
  CS_DISCONNECTED,
  CS_CONNECTING,
  CS_CONNECTED,
  CS_DISCONNECTING,
  CS_MAX,
} menu_connection, nwm_connection;

atomic_int menu_work_state;
atomic_int nwm_work_state;
atomic_bool menu_remote_play;

static char ip_addr_buf[16];

const char *connection_msg[CS_MAX] = {
  "+",
  "...",
  "-",
  ".",
};

#define TITLE "NTR Viewer HR"

static struct nk_context *nk_ctx;
static const char *nk_property_name = "#";
static enum {
  NK_NAV_NONE,
  NK_NAV_NEXT,
  NK_NAV_PREVIOUS,
  NK_NAV_CONFIRM,
  NK_NAV_CANCEL,
} nk_nav_command;

static nk_bool ntr_rp_priority;
static int ntr_rp_priority_factor;
static int ntr_rp_quality;
static int ntr_rp_qos;

static int ntr_rp_port = 8001;
static int ntr_rp_port_changed;
static int ntr_rp_bound_port;

#ifndef USE_SDL_RENDERER
static nk_bool fsr_filter;
static nk_bool upscaling_filter;
static nk_bool upscaling_filter_created;
#endif
typedef enum {
  VIEW_MODE_TOP_BOT,
  VIEW_MODE_SEPARATE,
  VIEW_MODE_TOP,
  VIEW_MODE_BOT,
} view_mode_t;
static view_mode_t view_mode;
static int fullscreen;

static atomic_uint_fast8_t ip_octets[4];

#define HEART_BEAT_EVERY_MS 250
// #define HEART_BEAT_EVERY_MS 25
#define REST_EVERY_MS 100

#define TCP_MAGIC 0x12345678
#define TCP_ARGS_COUNT 16

static int restart_kcp = 0;
static int ntr_kcp;
static int kcp_active = 0;
static int kcp_cid;
static int kcp_reset_cid = (IUINT16)-1 & ((1 << 2) - 1);
typedef struct _TCPPacketHeader
{
  uint32_t magic;
  uint32_t seq;
  uint32_t type;
  uint32_t cmd;
  uint32_t args[TCP_ARGS_COUNT];

  uint32_t data_len;
} TCPPacketHeader;

static bool socket_poll(SOCKET s)
{
  while (running) {
    WSAPOLLFD pollfd = {
      .fd = s,
      .events = POLLIN,
      .revents = 0,
    };
    int res = WSAPoll(&pollfd, 1, RP_SOCKET_INTERVAL);
    if (res < 0) {
      return false;
    } else if (res > 0) {
      if (pollfd.revents & POLLIN) {
        return true;
      }
    }
  }
  return false;
}

static int tcp_recv(SOCKET sockfd, char *buf, int size)
{
  int ret, pos = 0;
  int tmpsize = size;

  while (running && tmpsize)
  {
    if ((ret = recv(sockfd, &buf[pos], tmpsize, 0)) <= 0)
    {
      if (ret < 0)
      {
        if (sock_errno() == WSAEWOULDBLOCK)
        {
          if (pos) {
            if (socket_poll(sockfd)) {
              continue;
            } else {
              if (running)
                err_log("socket poll failed: %d\n", sock_errno());
              return -1;
            }
          } else {
            return 0;
          }
        }
      }
      return ret;
    }
    pos += ret;
    tmpsize -= ret;
  }

  return size;
}

static int tcp_send(SOCKET sockfd, char *buf, int size)
{
  int ret, pos = 0;
  int tmpsize = size;

  while (running && tmpsize)
  {
    if ((ret = send(sockfd, &buf[pos], tmpsize, 0)) < 0)
    {
      if (sock_errno() == WSAEWOULDBLOCK) {
        if (socket_poll(sockfd)) {
          continue;
        } else {
          if (running)
            err_log("socket poll failed: %d\n", sock_errno());
          return -1;
        }
      }
      return ret;
    }
    pos += ret;
    tmpsize -= ret;
  }

  return size;
}

static int tcp_send_packet_header(SOCKET s, uint32_t seq, uint32_t type, uint32_t cmd, uint32_t *argv, int argc, uint32_t data_len)
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

  char *buf = (char *)&packet;
  int size = sizeof(packet);
  return tcp_send(s, buf, size);
}

static bool socket_set_nonblock(SOCKET s, bool nb)
{
#ifdef _WIN32
  u_long opt = nb;
  if (ioctlsocket(s, FIONBIO, &opt)) {
    return false;
  }
#else
  int flags = fcntl(s, F_GETFL, 0);
  if (flags == -1) {
    return false;
  }
  flags = nb ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
  if (fcntl(s, F_SETFL, flags) != 0) {
    return false;
  }
#endif
  return true;
}

static SOCKET tcp_connect(int port)
{
  struct sockaddr_in servaddr = {0};
  SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (!SOCKET_VALID(sockfd))
  {
    err_log("socket creation failed: %d\n", sock_errno());
    return INVALID_SOCKET;
  }

  if (!socket_set_nonblock(sockfd, 1)) {
    err_log("socket_set_nonblock failed: %d\n", sock_errno());
    closesocket(sockfd);
    return INVALID_SOCKET;
  }

  servaddr.sin_family = AF_INET;
  snprintf(ip_addr_buf, sizeof(ip_addr_buf),
           "%d.%d.%d.%d",
           (int)ip_octets[0],
           (int)ip_octets[1],
           (int)ip_octets[2],
           (int)ip_octets[3]);
  servaddr.sin_addr.s_addr = inet_addr(ip_addr_buf);
  servaddr.sin_port = htons(port);

  err_log("connecting to %s:%d ...\n", ip_addr_buf, port);
  int ret = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
  if (ret != 0 && sock_errno() != WSAEWOULDBLOCK && sock_errno() != EINPROGRESS)
  {
    err_log("connection failed: %d\n", sock_errno());
    sock_close(sockfd);
    return INVALID_SOCKET;
  }

  fd_set fdset;
  struct timeval tv;
  FD_ZERO(&fdset);
  FD_SET(sockfd, &fdset);
  tv.tv_sec = 2;
  tv.tv_usec = 0;

  if (select(sockfd + 1, NULL, &fdset, NULL, &tv) == 1)
  {
    int so_error;
    socklen_t len = sizeof so_error;

    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);

    if (so_error == 0) {
      err_log("connected\n");
      return sockfd;
    }
    err_log("connection failed: %d\n", so_error);
  }

  closesocket(sockfd);
  err_log("connection timeout\n");
  return INVALID_SOCKET;
}

#define RESET_SOCKET(ts, ws) do { \
  sock_close(sockfd); \
  sockfd = INVALID_SOCKET; \
  ts = 0; \
  ws = CS_DISCONNECTED; \
  if (t->remote_play) { \
    *(t->remote_play) = 0; \
  } \
  err_log("disconnected\n"); \
} while (0)

struct tcp_thread_arg {
  atomic_int *work_state;
  atomic_bool *remote_play;
  short port;
};

static thread_ret_t tcp_thread_func(void *arg)
{
  struct tcp_thread_arg *t = (struct tcp_thread_arg *)arg;

  int tcp_status = 0;
  SOCKET sockfd = INVALID_SOCKET;
  int packet_seq = 0;
  while (running)
  {
    if (!tcp_status && *(t->work_state) == CS_CONNECTING)
    {
      sockfd = tcp_connect(t->port);
      if (!SOCKET_VALID(sockfd))
      {
        *(t->work_state) = CS_DISCONNECTED;
        if (t->remote_play) {
          *(t->remote_play) = 0;
        }
        continue;
      }

      packet_seq = 0;
      tcp_status = 1;
      *(t->work_state) = CS_CONNECTED;
    }
    else if (tcp_status && *(t->work_state) == CS_DISCONNECTING)
    {
      RESET_SOCKET(tcp_status, *(t->work_state));
    }
    else if (tcp_status)
    {
      Sleep(HEART_BEAT_EVERY_MS);

      TCPPacketHeader header = {0};
      char *buf = (char *)&header;
      int size = sizeof(header);
      int ret;
      if ((ret = tcp_recv(sockfd, buf, size)) < 0 || !running)
      {
        if (running)
          err_log("tcp recv error: %d\n", sock_errno());
        RESET_SOCKET(tcp_status, *(t->work_state));
        continue;
      }
      if (ret)
      {
        if (header.magic != TCP_MAGIC)
        {
          if (running)
            err_log("broken protocol\n");
          RESET_SOCKET(tcp_status, *(t->work_state));
          continue;
        }
        if (header.cmd == 0)
        {
          // err_log("heartbeat packet: size %d\n", header.data_len);
          if (header.data_len)
          {
            char *buf = malloc(header.data_len + 1);
            if ((ret = tcp_recv(sockfd, buf, header.data_len)) < 0)
            {
              if (running)
                err_log("heart beat recv error: %d\n", sock_errno());
              free(buf);
              RESET_SOCKET(tcp_status, *(t->work_state));
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
        else if (header.data_len)
        {
          err_log("unhandled packet type %d: size %d\n", header.cmd, header.data_len);
          char *buf = malloc(header.data_len);
          if ((ret = tcp_recv(sockfd, buf, header.data_len)) < 0)
          {
            if (running)
              err_log("tcp recv error: %d\n", sock_errno());
            free(buf);
            RESET_SOCKET(tcp_status, *(t->work_state));
            continue;
          }
          free(buf);
        }
      }

      ret = tcp_send_packet_header(sockfd, packet_seq, 0, 0, 0, 0, 0);
      if (ret < 0)
      {
        if (running)
          err_log("heart beat send failed: %d\n", sock_errno());
        RESET_SOCKET(tcp_status, *(t->work_state));
      }
      ++packet_seq;

      if (t->remote_play && *(t->remote_play))
      {
        *(t->remote_play) = 0;

        uint32_t args[] = {
          (ntr_rp_priority << 8) | ntr_rp_priority_factor, ntr_rp_quality, ntr_rp_qos * 128 * 1024,
          1404036572 /* guarding magic */, ntr_rp_bound_port | (ntr_kcp ? (1 << 30) : 0)
        };

        ret = tcp_send_packet_header(sockfd, packet_seq, 0, 901,
                                     args, sizeof(args) / sizeof(*args), 0);

        if (ret < 0)
        {
          if (running)
            err_log("remote play send failed: %d\n", sock_errno());
          RESET_SOCKET(tcp_status, *(t->work_state));
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

void rpConfigSetDefault(void)
{
  ntr_rp_priority = 1;
  ntr_rp_priority_factor = 2;
  ntr_rp_quality = 75;
  ntr_rp_qos = 16;
  ntr_kcp = 1;
}

static char **autoIPs;
static uint8_t **autoIPsOctets;
static int autoIPsCount;

static void freeAutoIPs(void) {
  if(autoIPsCount) {
    for (int i = 0; i < autoIPsCount; ++i) {
      free(autoIPs[i]);
      free(autoIPsOctets[i]);
    }
    free(autoIPs);
    free(autoIPsOctets);
    autoIPs = 0;
    autoIPsOctets = 0;
    autoIPsCount = 0;
  }
}

static void allocAutoIPs(int count) {
  if (count) {
    autoIPs = malloc(sizeof(*autoIPs) * count);
    autoIPsOctets = malloc(sizeof(*autoIPsOctets) * count);
    for (int i = 0; i < count; ++i) {
      autoIPs[i] = malloc(50);
      autoIPsOctets[i] = malloc(4);
    }
    autoIPsCount = count;
  }
}

// taken from Boop's source code https://github.com/miltoncandelero/Boop
static uint8_t const knownMACs[][3] = {
  { 0x00, 0x09, 0xBF }, { 0x00, 0x16, 0x56 }, { 0x00, 0x17, 0xAB }, { 0x00, 0x19, 0x1D }, { 0x00, 0x19, 0xFD },
  { 0x00, 0x1A, 0xE9 }, { 0x00, 0x1B, 0x7A }, { 0x00, 0x1B, 0xEA }, { 0x00, 0x1C, 0xBE }, { 0x00, 0x1D, 0xBC },
  { 0x00, 0x1E, 0x35 }, { 0x00, 0x1E, 0xA9 }, { 0x00, 0x1F, 0x32 }, { 0x00, 0x1F, 0xC5 }, { 0x00, 0x21, 0x47 },
  { 0x00, 0x21, 0xBD }, { 0x00, 0x22, 0x4C }, { 0x00, 0x22, 0xAA }, { 0x00, 0x22, 0xD7 }, { 0x00, 0x23, 0x31 },
  { 0x00, 0x23, 0xCC }, { 0x00, 0x24, 0x1E }, { 0x00, 0x24, 0x44 }, { 0x00, 0x24, 0xF3 }, { 0x00, 0x25, 0xA0 },
  { 0x00, 0x26, 0x59 }, { 0x00, 0x27, 0x09 }, { 0x04, 0x03, 0xD6 }, { 0x18, 0x2A, 0x7B }, { 0x2C, 0x10, 0xC1 },
  { 0x34, 0xAF, 0x2C }, { 0x40, 0xD2, 0x8A }, { 0x40, 0xF4, 0x07 }, { 0x58, 0x2F, 0x40 }, { 0x58, 0xBD, 0xA3 },
  { 0x5C, 0x52, 0x1E }, { 0x60, 0x6B, 0xFF }, { 0x64, 0xB5, 0xC6 }, { 0x78, 0xA2, 0xA0 }, { 0x7C, 0xBB, 0x8A },
  { 0x8C, 0x56, 0xC5 }, { 0x8C, 0xCD, 0xE8 }, { 0x98, 0xB6, 0xE9 }, { 0x9C, 0xE6, 0x35 }, { 0xA4, 0x38, 0xCC },
  { 0xA4, 0x5C, 0x27 }, { 0xA4, 0xC0, 0xE1 }, { 0xB8, 0x78, 0x26 }, { 0xB8, 0x8A, 0xEC }, { 0xB8, 0xAE, 0x6E },
  { 0xCC, 0x9E, 0x00 }, { 0xCC, 0xFB, 0x65 }, { 0xD8, 0x6B, 0xF7 }, { 0xDC, 0x68, 0xEB }, { 0xE0, 0x0C, 0x7F },
  { 0xE0, 0xE7, 0x51 }, { 0xE8, 0x4E, 0xCE }, { 0xEC, 0xC4, 0x0D }, { 0xE8, 0x4E, 0xCE }
};
static int selectedIP = 0;
static int selectedAdaptor;

static char **adaptorIPs;
static uint8_t **adaptorIPsOctets;
static int adaptorIPsCount;

static void freeAdaptorIPs(void) {
  if(adaptorIPsCount) {
    for (int i = 0; i < adaptorIPsCount; ++i) {
      free(adaptorIPs[i]);
      free(adaptorIPsOctets[i]);
    }
    free(adaptorIPs);
    free(adaptorIPsOctets);
    adaptorIPs = 0;
    adaptorIPsOctets = 0;
    adaptorIPsCount = 0;
  }
}

static void allocAdaptorIPs(int count) {
  if (count) {
    adaptorIPs = malloc(sizeof(*adaptorIPs) * count);
    adaptorIPsOctets = malloc(sizeof(*adaptorIPsOctets) * count);
    for (int i = 0; i < count; ++i) {
      adaptorIPs[i] = malloc(50);
      adaptorIPsOctets[i] = malloc(4);
    }
    adaptorIPsCount = count;
  }
}

static void tryAutoSelectAdapterIP(void) {
  selectedAdaptor = 0;
  uint32_t count = 0;
  for (int i = 1; i < adaptorIPsCount - 2; ++i) {
    uint32_t bits = __builtin_bswap32(*(uint32_t *)ip_octets & *(uint32_t *)adaptorIPsOctets[i]);
    if (/*(int)bits < 0 && */bits > count) {
      count = bits;
      selectedAdaptor = i;
    }
  }
}

#ifdef _WIN32
static PMIB_IPNETTABLE ipNetBuf = 0;
static ULONG ipNetBufSize = 0;

static void getIPMapMAC(void) {
  if (ipNetBuf) {
    free(ipNetBuf);
    ipNetBuf = 0;
    ipNetBufSize = 0;
  }

  ipNetBufSize = 0;
  if (GetIpNetTable(NULL, &ipNetBufSize, TRUE) == ERROR_INSUFFICIENT_BUFFER) {
    ipNetBuf = malloc(ipNetBufSize);
    ULONG ret = GetIpNetTable(ipNetBuf, &ipNetBufSize, TRUE);
    if (ret == NO_ERROR) {
      return;
    } else {
      free(ipNetBuf);
      ipNetBuf = 0;
      ipNetBufSize = 0;
    }
  } else {
    ipNetBufSize = 0;
  }
}

static int matchMAC(UCHAR *mac) {
  // err_log("%02x-%02x-%02x\n", (int)mac[0], (int)mac[1], (int)mac[2]);
  for (unsigned i = 0; i < sizeof(knownMACs) / sizeof(*knownMACs); ++i) {
    if (memcmp(mac, knownMACs[i], 3) == 0)
      return 1;
  }
  return 0;
}

static void detect3DSIP(void) {
  getIPMapMAC();

  int detectedIPsCount = 0;
  int *mapIndex = 0;
  if (ipNetBufSize) {
    mapIndex = malloc(ipNetBuf->dwNumEntries * sizeof(*mapIndex));
    for (unsigned i = 0; i < ipNetBuf->dwNumEntries; ++i) {
      PMIB_IPNETROW entry = &ipNetBuf->table[i];
      if (entry->dwType != MIB_IPNET_TYPE_INVALID) {
        if (entry->dwPhysAddrLen == 6) {
          if (matchMAC(entry->bPhysAddr)) {
            mapIndex[detectedIPsCount] = i;
            ++detectedIPsCount;
          }
        }
      }
    }
  }

  freeAutoIPs();
  allocAutoIPs(detectedIPsCount + 1);

  if (detectedIPsCount) {
    strcpy(autoIPs[0], "");
  } else {
    strcpy(autoIPs[0], "None Detected");
  }
  memset(autoIPsOctets[0], 0, 4);

  for (int i = 0; i < detectedIPsCount; ++i) {
    PMIB_IPNETROW entry = &ipNetBuf->table[mapIndex[i]];
    uint8_t *octets = (uint8_t *)&entry->dwAddr;
    sprintf(autoIPs[i + 1], "%d.%d.%d.%d", (int)octets[0], (int)octets[1], (int)octets[2], (int)octets[3]);
    memcpy(autoIPsOctets[i + 1], &entry->dwAddr, 4);
  }
  free(mapIndex);

  selectedIP = detectedIPsCount ? 1 : 0;
  memcpy(ip_octets, autoIPsOctets[selectedIP], 4);
}

static PIP_ADAPTER_INFO adapterInfos;
static ULONG adaptorInfosSize;

static uint32_t parseIPAddress(const char *ip) {
  return inet_addr(ip);
}

static int getAdaptorCount(void) {
  int count = 0;
  PIP_ADAPTER_INFO next = adapterInfos;
  while (next) {
    PIP_ADDR_STRING ip = &next->IpAddressList;
    while (ip) {
      if (parseIPAddress(ip->IpAddress.String) != 0)
        ++count;
      ip = ip->Next;
    }
    next = next->Next;
  }
  return count;
}

static void updateAdapterIPs(void) {
  freeAdaptorIPs();

  int count = getAdaptorCount();

  allocAdaptorIPs(count + 3);

  strcpy(adaptorIPs[0], "0.0.0.0 (Any)");
  memset(adaptorIPsOctets[0], 0, 4);

  PIP_ADAPTER_INFO next = adapterInfos;
  for (int i = 0; i < count;) {
    PIP_ADDR_STRING ip = &next->IpAddressList;
    while (ip) {
      int addr;
      if ((addr = parseIPAddress(ip->IpAddress.String)) != 0) {
        sprintf(adaptorIPs[i + 1], "%s", ip->IpAddress.String);
        memcpy(adaptorIPsOctets[i + 1], &addr, 4);
        ++i;
      }
      ip = ip->Next;
    }
    next = next->Next;
  }

  strcpy(adaptorIPs[1 + count], "Auto-Select");
  memset(adaptorIPsOctets[1 + count], 0, 4);

  strcpy(adaptorIPs[1 + count + 1], "Refresh List");
  memset(adaptorIPsOctets[1 + count + 1], 0, 4);

  tryAutoSelectAdapterIP();
}

static void getAdapterIPs(void) {
  if (adapterInfos) {
    free(adapterInfos);
    adapterInfos = 0;
    adaptorInfosSize = 0;
  }

  if (ERROR_BUFFER_OVERFLOW != GetAdaptersInfo(adapterInfos, &adaptorInfosSize)) {
    adaptorInfosSize = 0;
    return;
  }

  adapterInfos = malloc(adaptorInfosSize);
  ULONG ret = GetAdaptersInfo(adapterInfos, &adaptorInfosSize);
  if (ret == ERROR_SUCCESS) {
    updateAdapterIPs();
  } else {
    free(adapterInfos);
    adapterInfos = 0;
    adaptorInfosSize = 0;
  }
}
#else

// Taken from stackexchange
// https://codereview.stackexchange.com/a/58107
#include <stdio.h>

#define xstr(s) str(s)
#define str(s) #s

#define ARP_CACHE       "/proc/net/arp"
#define ARP_STRING_LEN  1023
#define ARP_BUFFER_LEN  (ARP_STRING_LEN + 1)
#define ARP_LINE_FORMAT "%" xstr(ARP_STRING_LEN) "s %*s %*s " \
                        "%" xstr(ARP_STRING_LEN) "s %*s " \
                        "%" xstr(ARP_STRING_LEN) "s"

struct IPMapMAC_t {
  uint8_t IP_bytes[4];
  uint8_t MAC_bytes[6];
};

static struct IPMapMAC_t *ipNetBuf = 0;
static size_t ipNetBufCount = 0;

static void getIPMapMAC(void) {
  if (ipNetBuf) {
    free(ipNetBuf);
    ipNetBuf = 0;
    ipNetBufCount = 0;
  }

  FILE *arpCache = fopen(ARP_CACHE, "r");
  if (!arpCache)
    return;

  /* Ignore the first line, which contains the header */
  char header[ARP_BUFFER_LEN];
  if (!fgets(header, sizeof(header), arpCache))
    goto final;

  int beg = ftell(arpCache);

  char ipAddr[ARP_BUFFER_LEN], hwAddr[ARP_BUFFER_LEN], device[ARP_BUFFER_LEN];
  int count = 0;
  while (3 == fscanf(arpCache, ARP_LINE_FORMAT, ipAddr, hwAddr, device))
    ++count;

  ipNetBuf = calloc(count, sizeof(struct IPMapMAC_t));
  if (ipNetBuf) {
    fseek(arpCache, beg, SEEK_SET);
    int count = 0;
    while (3 == fscanf(arpCache, ARP_LINE_FORMAT, ipAddr, hwAddr, device)) {
      struct IPMapMAC_t *b = &ipNetBuf[count];
      sscanf(ipAddr, "%hhu.%hhu.%hhu.%hhu",
        &b->IP_bytes[0],
        &b->IP_bytes[1],
        &b->IP_bytes[2],
        &b->IP_bytes[3]);
      sscanf(hwAddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &b->MAC_bytes[0],
        &b->MAC_bytes[1],
        &b->MAC_bytes[2],
        &b->MAC_bytes[3],
        &b->MAC_bytes[4],
        &b->MAC_bytes[5]);
      ++count;
    }
    ipNetBufCount = count;
  }

final:
  fclose(arpCache);
  return;
}

static int matchMAC(uint8_t *mac) {
  for (unsigned i = 0; i < sizeof(knownMACs) / sizeof(*knownMACs); ++i) {
    if (memcmp(mac, knownMACs[i], 3) == 0)
      return 1;
  }
  return 0;
}

static void detect3DSIP(void) {
  getIPMapMAC();

  unsigned detectedIPsCount = 0;
  unsigned *mapIndex = 0;
  if (ipNetBufCount) {
    mapIndex = malloc(ipNetBufCount * sizeof(*mapIndex));
    for (unsigned i = 0; i < ipNetBufCount; ++i) {
      if (matchMAC(ipNetBuf[i].MAC_bytes)) {
        mapIndex[detectedIPsCount] = i;
        ++detectedIPsCount;
      }
    }
  }

  freeAutoIPs();
  allocAutoIPs(detectedIPsCount + 1);

  if (detectedIPsCount) {
    strcpy(autoIPs[0], "");
  } else {
    strcpy(autoIPs[0], "None Detected");
  }
  memset(autoIPsOctets[0], 0, 4);

  for (unsigned i = 0; i < detectedIPsCount; ++i) {
    struct IPMapMAC_t *b = &ipNetBuf[mapIndex[i]];
    sprintf(autoIPs[i + 1], "%d.%d.%d.%d",
      (int)b->IP_bytes[0],
      (int)b->IP_bytes[1],
      (int)b->IP_bytes[2],
      (int)b->IP_bytes[3]);
    memcpy(autoIPsOctets[i + 1], b->IP_bytes, 4);
  }
  free(mapIndex);

  selectedIP = detectedIPsCount ? 1 : 0;
  memcpy(ip_octets, autoIPsOctets[selectedIP], 4);
}

// Taken from stackoverflow
// https://stackoverflow.com/a/12131131
#include <ifaddrs.h>
#include <netdb.h>

static void getAdapterIPs(void) {
  freeAdaptorIPs();

  int count = 0;

  struct ifaddrs *ifaddr = 0, *ifa;
  if (getifaddrs(&ifaddr) != -1) {
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == NULL)
        continue;

      char host[NI_MAXHOST] = { 0 };
      int s = getnameinfo(
        ifa->ifa_addr,
        sizeof(struct sockaddr_in),
        host,
        NI_MAXHOST,
        NULL,
        0,
        NI_NUMERICHOST);

      if (s == 0)
        ++count;
    }
  }

  allocAdaptorIPs(count + 3);

  strcpy(adaptorIPs[0], "0.0.0.0 (Any)");
  memset(adaptorIPsOctets[0], 0, 4);

  if (count) {
    int i = 1;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == NULL)
        continue;

      char host[NI_MAXHOST] = { 0 };
      int s = getnameinfo(
        ifa->ifa_addr,
        sizeof(struct sockaddr_in),
        host,
        NI_MAXHOST,
        NULL,
        0,
        NI_NUMERICHOST);

      if (s == 0) {
        sscanf(host, "%hhu.%hhu.%hhu.%hhu",
          &adaptorIPsOctets[i][0],
          &adaptorIPsOctets[i][1],
          &adaptorIPsOctets[i][2],
          &adaptorIPsOctets[i][3]);
        sprintf(
          adaptorIPs[i],
          "%d.%d.%d.%d",
          (int)adaptorIPsOctets[i][0],
          (int)adaptorIPsOctets[i][1],
          (int)adaptorIPsOctets[i][2],
          (int)adaptorIPsOctets[i][3]);

        ++i;
      }
    }
  }

  if (ifaddr) {
    freeifaddrs(ifaddr);
  }

  strcpy(adaptorIPs[1 + count], "Auto-Select");
  memset(adaptorIPsOctets[1 + count], 0, 4);

  strcpy(adaptorIPs[1 + count + 1], "Refresh List");
  memset(adaptorIPsOctets[1 + count + 1], 0, 4);

  tryAutoSelectAdapterIP();
}
#endif

static void updateViewMode(view_mode_t vm) {
  switch (vm) {
    case VIEW_MODE_TOP_BOT:
    case VIEW_MODE_TOP:
    case VIEW_MODE_BOT:
      SDL_HideWindow(win[SCREEN_BOT]);
      break;

    case VIEW_MODE_SEPARATE:
      SDL_ShowWindow(win[SCREEN_BOT]);
      break;
  }

  SDL_SetWindowFullscreen(win[SCREEN_TOP], 0);
  SDL_RestoreWindow(win[SCREEN_TOP]);
  SDL_SetWindowFullscreen(win[SCREEN_BOT], 0);
  SDL_RestoreWindow(win[SCREEN_BOT]);
  switch (vm) {
    case VIEW_MODE_TOP_BOT:
      SDL_SetWindowSize(win[SCREEN_TOP], WINDOW_WIDTH, WINDOW_HEIGHT);
      break;

    case VIEW_MODE_TOP:
      SDL_SetWindowSize(win[SCREEN_TOP], WINDOW_WIDTH, WINDOW_HEIGHT12);
      break;

    case VIEW_MODE_BOT:
      SDL_SetWindowSize(win[SCREEN_TOP], WINDOW_WIDTH2, WINDOW_HEIGHT12);
      break;

    case VIEW_MODE_SEPARATE:
      SDL_SetWindowSize(win[SCREEN_TOP], WINDOW_WIDTH, WINDOW_HEIGHT12);
      SDL_SetWindowSize(win[SCREEN_BOT], WINDOW_WIDTH2, WINDOW_HEIGHT12);
      break;
  }
}

// HACK
// Try to get Nuklear to accept keyboard navigation

static nk_hash nk_hash_from_name_prev(const char *name, struct nk_window *win, int prev)
{
  // copied from nuklear.h since I don't want to edit that file
  if (name[0] == '#') {
      return nk_murmur_hash(name, (int)nk_strlen(name), win->property.seq - prev);
  } else return nk_murmur_hash(name, (int)nk_strlen(name), 42);
}

static nk_hash nk_hash_from_name(const char *name, struct nk_window *win)
{
  return nk_hash_from_name_prev(name, win, 0);
}

static void focus_next_property(struct nk_context *ctx, const char *name, int val)
{
  struct nk_window *win = ctx->current;
  nk_hash hash = nk_hash_from_name(name, win);

  win->property.active = 1;
  nk_itoa_impl(win->property.buffer, val);
  win->property.length = nk_strlen(win->property.buffer);
  win->property.cursor = 0;
  win->property.state = NK_PROPERTY_EDIT_IMPL;
  win->property.name = hash;
  win->property.select_start = 0;
  win->property.select_end = win->property.length;
}

static void cancel_next_property(struct nk_context *ctx)
{
  struct nk_window *win = ctx->current;

  if (win->property.active && win->property.state == NK_PROPERTY_EDIT_IMPL) {
    win->property.active = 0;
    win->property.buffer[0] = 0;
    win->property.length = 0;
    win->property.cursor = 0;
    win->property.state = 0;
    win->property.name = 0;
    win->property.select_start = 0;
    win->property.select_end = 0;
  }
}

static void confirm_next_property(struct nk_context *ctx)
{
  nk_input_key(ctx, NK_KEY_ENTER, nk_true);
}

static nk_bool check_next_property(struct nk_context *ctx, const char *name)
{
  struct nk_window *win = ctx->current;
  nk_hash hash = nk_hash_from_name(name, win);
  return win->property.active && win->property.name == hash;
}

static enum NK_FOCUS {
  NK_FOCUS_NONE,
  NK_FOCUS_VIEW_MODE,
#ifndef USE_SDL_RENDERER
  NK_FOCUS_UPSCALING_FILTER,
#endif
  NK_FOCUS_IP_OCTET_0,
  NK_FOCUS_IP_OCTET_1,
  NK_FOCUS_IP_OCTET_2,
  NK_FOCUS_IP_OCTET_3,
  NK_FOCUS_IP_AUTO_DETECT,
  NK_FOCUS_IP_COMBO,
  NK_FOCUS_VIEWER_IP,
  NK_FOCUS_VIEWER_PORT,
  NK_FOCUS_PRIORITY_SCREEN,
  NK_FOCUS_PRIORITY_FACTOR,
  NK_FOCUS_QUALITY,
  NK_FOCUS_BANDWIDTH_LIMIT,
  NK_FOCUS_RELIABLE_STREAM,
  NK_FOCUS_DEFAULT,
  NK_FOCUS_CONNECT,
  NK_FOCUS_MIN = NK_FOCUS_VIEW_MODE,
  NK_FOCUS_MAX = NK_FOCUS_CONNECT,
} nk_focus_current;

static enum NK_NAV_FOCUS {
  NK_NAV_FOCUS_NONE,
  NK_NAV_FOCUS_NORMAL,
  NK_NAV_FOCUS_NAV,
} nk_nav_focus;

static int hide_windows = 0;
static struct nk_style nk_style_current;

static void do_nav_next(enum NK_FOCUS nk_focus)
{
  if (nk_focus == nk_focus_current) {
    switch (__atomic_load_n(&nk_nav_command, __ATOMIC_RELAXED)) {
      case NK_NAV_PREVIOUS:
        if (nk_focus_current <= NK_FOCUS_MIN)
          nk_focus_current = NK_FOCUS_MAX;
        else
          --nk_focus_current;
        nk_nav_focus = NK_NAV_FOCUS_NAV;
        break;

      case NK_NAV_NEXT:
        if (nk_focus_current >= NK_FOCUS_MAX)
          nk_focus_current = NK_FOCUS_MIN;
        else
          ++nk_focus_current;
        nk_nav_focus = NK_NAV_FOCUS_NAV;
        break;

      case NK_NAV_CANCEL:
        if (nk_nav_focus == NK_NAV_FOCUS_NONE)
          hide_windows = 1;
        else
          nk_nav_focus = NK_NAV_FOCUS_NONE;
        break;

      case NK_NAV_CONFIRM:
        if (nk_focus == NK_FOCUS_NONE) {
          nk_nav_focus = NK_NAV_FOCUS_NONE;
        } else {
          nk_nav_focus = nk_nav_focus == NK_NAV_FOCUS_NONE ? NK_NAV_FOCUS_NAV : NK_NAV_FOCUS_NONE;
        }
        break;

      default:
        break;
    }
    __atomic_store_n(&nk_nav_command, NK_NAV_NONE, __ATOMIC_RELAXED);
  }
}

static void do_nav_property_next(struct nk_context *ctx, const char *name, enum NK_FOCUS nk_focus, int val)
{
  if (check_next_property(ctx, name)) {
    if (nk_nav_focus == NK_NAV_FOCUS_NAV) {
      cancel_next_property(ctx);
    } else {
      nk_focus_current = nk_focus;
      nk_nav_focus = NK_NAV_FOCUS_NORMAL;
    }
  } else if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE) {
    focus_next_property(ctx, name, val);
    nk_nav_focus = NK_NAV_FOCUS_NORMAL;
  }

  if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE) {
    switch (__atomic_load_n(&nk_nav_command, __ATOMIC_RELAXED)) {
      case NK_NAV_PREVIOUS:
      case NK_NAV_NEXT:
      case NK_NAV_CONFIRM:
        confirm_next_property(ctx);
        break;

      case NK_NAV_CANCEL:
        cancel_next_property(ctx);
        break;

      default:
        break;
    }
  }

  do_nav_next(nk_focus);
}

static void check_nav_property_prev(struct nk_context *ctx, const char *name, enum NK_FOCUS nk_focus)
{
  struct nk_window *win = ctx->current;
  if (win->property.active) {
    nk_hash hash = nk_hash_from_name_prev(name, win, 1);
    if (win->property.name == hash) {
      nk_focus_current = nk_focus;
      nk_nav_focus = NK_NAV_FOCUS_NORMAL;
    }
  } else if (nk_nav_focus != NK_NAV_FOCUS_NAV) {
    nk_nav_focus = NK_NAV_FOCUS_NONE;
  }
  ctx->input.keyboard.keys[NK_KEY_ENTER].clicked = 0;
}

static void do_nav_combobox_next(struct nk_context *ctx, enum NK_FOCUS nk_focus, int *selected, int count)
{
  if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE) {
    ctx->style.combo.border_color = ctx->style.text.color;
    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_DOWN)) {
      ++*selected;
      if (*selected >= count) {
        *selected = 0;
      }
    } else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_UP)) {
      --*selected;
      if (*selected < 0) {
        *selected = count - 1;
      }
    }
  }

  do_nav_next(nk_focus);
}

static void set_nav_combobox_prev(enum NK_FOCUS nk_focus)
{
  nk_nav_focus = NK_NAV_FOCUS_NAV;
  nk_focus_current = nk_focus;
}

static void check_nav_combobox_prev(struct nk_context *ctx)
{
  ctx->style.combo.border_color = nk_style_current.combo.border_color;
}

static bool do_nav_button_next(struct nk_context *ctx, enum NK_FOCUS nk_focus)
{
  bool ret = false;
  if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE) {
    ctx->style.button.border_color = ctx->style.text.color;
    if (__atomic_load_n(&nk_nav_command, __ATOMIC_RELAXED) == NK_NAV_CONFIRM) {
      ret = true;
    }
  }

  if (ret) {
    __atomic_store_n(&nk_nav_command, NK_NAV_NONE, __ATOMIC_RELAXED);
  } else {
    do_nav_next(nk_focus);
  }

  return ret;
}

static void set_nav_button_prev(enum NK_FOCUS nk_focus)
{
  nk_nav_focus = NK_NAV_FOCUS_NAV;
  nk_focus_current = nk_focus;
}

static void check_nav_button_prev(struct nk_context *ctx)
{
  ctx->style.button.border_color = nk_style_current.button.border_color;
}

static nk_bool nk_nav_checkbox_val_current;
static void do_nav_checkbox_next(struct nk_context *ctx, enum NK_FOCUS nk_focus, nk_bool *val)
{
  bool ret = false;
  if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE) {
    ctx->style.checkbox.cursor_hover.data.color = ctx->style.text.color;
    ctx->style.checkbox.cursor_normal.data.color = ctx->style.text.color;
    ctx->style.checkbox.border = 1.0f;
    ctx->style.checkbox.border_color = ctx->style.text.color;
    if (__atomic_load_n(&nk_nav_command, __ATOMIC_RELAXED) == NK_NAV_CONFIRM) {
      ret = true;
      *val = !*val;
    }
  }

  if (ret) {
    __atomic_store_n(&nk_nav_command, NK_NAV_NONE, __ATOMIC_RELAXED);
  } else {
    do_nav_next(nk_focus);
  }

  nk_nav_checkbox_val_current = *val;
}

static void check_nav_checkbox_prev(struct nk_context *ctx, enum NK_FOCUS nk_focus, nk_bool val)
{
  ctx->style.checkbox.border_color = nk_style_current.checkbox.border_color;
  ctx->style.checkbox.border = nk_style_current.checkbox.border;
  ctx->style.checkbox.cursor_normal.data.color = nk_style_current.checkbox.cursor_normal.data.color;
  ctx->style.checkbox.cursor_hover.data.color = nk_style_current.checkbox.cursor_hover.data.color;

  if (nk_nav_checkbox_val_current != val) {
    nk_nav_focus = NK_NAV_FOCUS_NAV;
    nk_focus_current = nk_focus;
  }
}

static int nk_nav_slider_val_current;
static void do_nav_slider_next(struct nk_context *ctx, enum NK_FOCUS nk_focus, int *val)
{
  if (nk_focus_current == nk_focus && nk_nav_focus != NK_NAV_FOCUS_NONE) {
    ctx->style.slider.border = 1.0f;
    ctx->style.slider.border_color = ctx->style.text.color;

    if (nk_input_is_key_pressed(&ctx->input, NK_KEY_RIGHT)) {
      ++*val;
    } else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_LEFT)){
      --*val;
    } else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_DOWN)) {
      *val += 5;
    } else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_UP)) {
      *val -= 5;
    } else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_START)) {
      *val = 0;
    } else if (nk_input_is_key_pressed(&ctx->input, NK_KEY_SCROLL_END)) {
      *val = INT_MAX;
    }
  }

  do_nav_next(nk_focus);
  nk_nav_slider_val_current = *val;
}

static void check_nav_slider_prev(struct nk_context *ctx, enum NK_FOCUS nk_focus, int val)
{
  ctx->style.slider.border_color = nk_style_current.slider.border_color;
  ctx->style.slider.border = nk_style_current.slider.border;

  if (nk_nav_slider_val_current != val) {
    nk_nav_focus = NK_NAV_FOCUS_NAV;
    nk_focus_current = nk_focus;
  }
}

static void guiMain(struct nk_context *ctx)
{
  const char *remote_play_wnd = "Remote Play";

  ctx->style.window.fixed_background = nk_style_item_hide();
  const char *background_wnd = "Background";
  if (nk_begin(ctx, background_wnd, nk_rect(0, 0, win_width[SCREEN_TOP], win_height[SCREEN_TOP]),
               NK_WINDOW_BACKGROUND))
  {
    if (nk_window_is_hovered(ctx) && nk_window_is_active(ctx, background_wnd) &&
        nk_input_has_mouse_click(&ctx->input, NK_BUTTON_LEFT))
    {
      hide_windows = !hide_windows;
      if (!hide_windows)
        nk_window_set_focus(ctx, remote_play_wnd);
    }
  }
  nk_end(ctx);
  ctx->style.window.fixed_background = nk_style_current.window.fixed_background;

  int nav_command = __atomic_load_n(&nk_nav_command, __ATOMIC_RELAXED);
  if (hide_windows && (nav_command == NK_NAV_CANCEL || nav_command == NK_NAV_CONFIRM)) {
    hide_windows = 0;
    __atomic_store_n(&nk_nav_command, NK_NAV_NONE, __ATOMIC_RELAXED);
    nk_window_set_focus(ctx, remote_play_wnd);
  }

  enum nk_show_states show_window = !hide_windows;

  static char msg_buf[250];

  /* GUI */
  if (nk_begin(ctx, remote_play_wnd, nk_rect(25, 10, 450, 495),
               NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE) && show_window)
  {
    do_nav_next(NK_FOCUS_NONE);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "View Mode", NK_TEXT_CENTERED);
    int selected = view_mode;
    static const struct nk_vec2 comboIPsSize = {225, 200};
    const char *view_mode_options[] = {
      "Top and Bottom",
      "Separate Windows",
      "Top Only",
      "Bottom Only"
    };
    do_nav_combobox_next(ctx, NK_FOCUS_VIEW_MODE, &selected, sizeof(view_mode_options) / sizeof(*view_mode_options));
    nk_combobox(ctx, view_mode_options, sizeof(view_mode_options) / sizeof(*view_mode_options), &selected, 30, comboIPsSize);
    check_nav_combobox_prev(ctx);
    if (selected != (int)view_mode) {
      set_nav_combobox_prev(NK_FOCUS_VIEW_MODE);
      __atomic_store_n(&view_mode, selected, __ATOMIC_RELAXED);
      fullscreen = 0;
    }

#ifndef USE_SDL_RENDERER
    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Upscaling Filter", NK_TEXT_CENTERED);
    int upscaling_selected = upscaling_filter ? 2 : fsr_filter ? 1 : 0;
    selected = upscaling_selected;
    const char *upscaling_filter_options[] = {
      "None",
      "FSR",
      "Real-CUGAN + FSR",
    };
    do_nav_combobox_next(ctx, NK_FOCUS_UPSCALING_FILTER, &selected, sizeof(upscaling_filter_options) / sizeof(*upscaling_filter_options));
    nk_combobox(ctx, upscaling_filter_options, sizeof(upscaling_filter_options) / sizeof(*upscaling_filter_options), &selected, 30, comboIPsSize);
    check_nav_combobox_prev(ctx);
    if (selected != upscaling_selected) {
      set_nav_combobox_prev(NK_FOCUS_UPSCALING_FILTER);
      if (selected == 2) {
        if (!upscaling_filter_created) {
          if (sr_create() < 0) {
            err_log("Failed to create NCNN instance for upscaling filter.\n");
            upscaling_filter = 0;
            fsr_filter = 1;
            selected = 1;
          } else {
            upscaling_filter = 1;
            fsr_filter = 1;
            upscaling_filter_created = 1;
          }
        } else {
          upscaling_filter = 1;
          fsr_filter = 1;
        }
      } else if (selected == 1) {
        upscaling_filter = 0;
        fsr_filter = 1;
      } else {
        upscaling_filter = 0;
        fsr_filter = 0;
      }
    }
#endif

    nk_layout_row_dynamic(ctx, 30, 5);
    nk_label(ctx, "3DS IP", NK_TEXT_CENTERED);

    for (int i = 0; i < 4; ++i)
    {
      int ip_octet = ip_octets[i];
      do_nav_property_next(ctx, nk_property_name, NK_FOCUS_IP_OCTET_0 + i, ip_octet);
      nk_property_int(ctx, nk_property_name, 0, &ip_octet, 255, 1, 1);
      check_nav_property_prev(ctx, nk_property_name, NK_FOCUS_IP_OCTET_0 + i);
      if (ip_octet != ip_octets[i]) {
        ip_octets[i] = ip_octet;
        strcpy(autoIPs[0], "Manual");
        selectedIP = 0;
      }
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    bool button_ret;
    button_ret = do_nav_button_next(ctx, NK_FOCUS_IP_AUTO_DETECT);
    if (nk_button_label(ctx, "Auto-Detect") || button_ret) {
      detect3DSIP();
      tryAutoSelectAdapterIP();
      set_nav_button_prev(NK_FOCUS_IP_AUTO_DETECT);
    }
    check_nav_button_prev(ctx);
    selected = selectedIP;
    do_nav_combobox_next(ctx, NK_FOCUS_IP_COMBO, &selected, autoIPsCount);
    nk_combobox(ctx, (const char **)autoIPs, autoIPsCount, &selected, 30, comboIPsSize);
    check_nav_combobox_prev(ctx);
    if (selected != selectedIP) {
      set_nav_combobox_prev(NK_FOCUS_IP_COMBO);
      selectedIP = selected;
      if (selectedIP) {
        memcpy(ip_octets, autoIPsOctets[selectedIP], 4);
        tryAutoSelectAdapterIP();
      }
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Viewer IP", NK_TEXT_CENTERED);
    selected = selectedAdaptor;
    do_nav_combobox_next(ctx, NK_FOCUS_VIEWER_IP, &selected, adaptorIPsCount);
    nk_combobox(ctx, (const char **)adaptorIPs, adaptorIPsCount, &selected, 30, comboIPsSize);
    check_nav_combobox_prev(ctx);
    if (selected != selectedAdaptor) {
      set_nav_combobox_prev(NK_FOCUS_VIEWER_IP);
      selectedAdaptor = selected;
      if (selectedAdaptor == adaptorIPsCount - 2) {
        tryAutoSelectAdapterIP();
      } else if (selectedAdaptor == adaptorIPsCount - 1) {
        getAdapterIPs();
      }
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Viewer Port", NK_TEXT_CENTERED);
    do_nav_property_next(ctx, nk_property_name, NK_FOCUS_VIEWER_PORT, ntr_rp_port);
    nk_property_int(ctx, nk_property_name, 1024, &ntr_rp_port, 65535, 1, 1);
    check_nav_property_prev(ctx, nk_property_name, NK_FOCUS_VIEWER_PORT);

    nk_layout_row_dynamic(ctx, 30, 1);
    nk_label(ctx, "Press \"F\" to toggle fullscreen.", NK_TEXT_CENTERED);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Prioritize Top Screen", NK_TEXT_CENTERED);
    do_nav_checkbox_next(ctx, NK_FOCUS_PRIORITY_SCREEN, &ntr_rp_priority);
    nk_checkbox_label(ctx, "", &ntr_rp_priority);
    check_nav_checkbox_prev(ctx, NK_FOCUS_PRIORITY_SCREEN, ntr_rp_priority);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Priority Screen Factor", NK_TEXT_CENTERED);
    do_nav_property_next(ctx, nk_property_name, NK_FOCUS_PRIORITY_FACTOR, ntr_rp_priority_factor);
    nk_property_int(ctx, nk_property_name, 0, &ntr_rp_priority_factor, 255, 1, 1);
    check_nav_property_prev(ctx, nk_property_name, NK_FOCUS_PRIORITY_FACTOR);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "JPEG Quality %d", ntr_rp_quality);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    do_nav_slider_next(ctx, NK_FOCUS_QUALITY, &ntr_rp_quality);
    nk_slider_int(ctx, 10, &ntr_rp_quality, 100, 1);
    check_nav_slider_prev(ctx, NK_FOCUS_QUALITY, ntr_rp_quality);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Bandwidth Limit %d Mbps", ntr_rp_qos);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    do_nav_slider_next(ctx, NK_FOCUS_BANDWIDTH_LIMIT, &ntr_rp_qos);
    nk_slider_int(ctx, 4, &ntr_rp_qos, 20, 1);
    check_nav_slider_prev(ctx, NK_FOCUS_BANDWIDTH_LIMIT, ntr_rp_qos);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Reliable Stream", NK_TEXT_CENTERED);
    selected = ntr_kcp;
    struct nk_vec2 comboRSSize = {225, 100};
    const char *reliable_stream_options[] = {
      "Off",
      "On",
    };
    do_nav_combobox_next(ctx, NK_FOCUS_RELIABLE_STREAM, &selected, sizeof(reliable_stream_options) / sizeof(*reliable_stream_options));
    nk_combobox(ctx, reliable_stream_options, sizeof(reliable_stream_options) / sizeof(*reliable_stream_options), &selected, 30, comboRSSize);
    check_nav_combobox_prev(ctx);
    if (selected != (int)ntr_kcp) {
      set_nav_combobox_prev(NK_FOCUS_RELIABLE_STREAM);
      ntr_kcp = selected;
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    button_ret = do_nav_button_next(ctx, NK_FOCUS_DEFAULT);
    if (nk_button_label(ctx, "Default") || button_ret)
    {
      rpConfigSetDefault();
      set_nav_button_prev(NK_FOCUS_DEFAULT);
    }
    check_nav_button_prev(ctx);

    button_ret = do_nav_button_next(ctx, NK_FOCUS_CONNECT);
    if (nk_button_label(ctx, "Connect") || button_ret)
    {
      set_nav_button_prev(NK_FOCUS_CONNECT);
      if (menu_work_state == CS_DISCONNECTED)
      {
        menu_work_state = CS_CONNECTING;
      }
      menu_remote_play = 1;
      if (ntr_rp_port != ntr_rp_bound_port) {
        ntr_rp_bound_port = ntr_rp_port;
        ntr_rp_port_changed = 1;
      }
      restart_kcp = 1;
    }
    check_nav_button_prev(ctx);
  }
  nk_end(ctx);
  nk_window_show(ctx, remote_play_wnd, show_window);

  const char *debug_msg_wnd = "Debug";
  if (nk_begin(ctx, debug_msg_wnd, nk_rect(475, 10, 150, 250),
               NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_TITLE) && show_window)
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

  // HACK to ensure Remote Play config window has keyboard focus
  if (nk_window_is_active(ctx, debug_msg_wnd))
    nk_window_set_focus(ctx, remote_play_wnd);
}

#ifndef USE_SDL_RENDERER
#ifdef USE_OGL_ES
#define GLSL_VERSION "#version 100\n" "precision highp float;\n"
#else
#define GLSL_VERSION "#version 110\n"
#endif
static GLbyte vShaderStr[] =
  GLSL_VERSION
  "attribute vec4 a_position;\n"
  "attribute vec2 a_texCoord;\n"
  "varying vec2 v_texCoord;\n"
  "void main()\n"
  "{\n"
  " gl_Position = a_position;\n"
  " v_texCoord = a_texCoord;\n"
  "}\n";

static GLbyte fShaderStr[] =
  GLSL_VERSION
  "varying vec2 v_texCoord;\n"
  "uniform sampler2D s_texture;\n"
  "void main()\n"
  "{\n"
  " gl_FragColor = texture2D(s_texture, v_texCoord);\n"
  "}\n";

#ifdef USE_OGL_ES
#define GLSL_FBO_VERSION "#version 300 es\n" "precision highp float;\n" "precision highp sampler3D;\n"
#else
#define GLSL_FBO_VERSION "#version 130\n"
#endif
static GLbyte fbo_vShaderStr[] =
  GLSL_FBO_VERSION
  "in vec4 a_position;\n"
  "in vec2 a_texCoord;\n"
  "out vec2 v_texCoord;\n"
  "void main()\n"
  "{\n"
  " gl_Position = a_position;\n"
  " v_texCoord = a_texCoord;\n"
  "}\n";

static GLbyte fbo_fShaderStr[] =
  GLSL_FBO_VERSION
  "in vec2 v_texCoord;\n"
  "uniform sampler3D s_texture;\n"
  "out vec4 fragColor;\n"
  "void main()\n"
  "{\n"
  " fragColor = vec4("
#ifdef _WIN32
    "texture(s_texture, vec3(v_texCoord, 2.0 / 3.0)).x / 255.0,"
    "texture(s_texture, vec3(v_texCoord, 1.0 / 3.0)).x / 255.0,"
    "texture(s_texture, vec3(v_texCoord, 0.0)).x / 255.0,"
#else
    "texture(s_texture, vec3(v_texCoord, 0.0)).x / 255.0,"
    "texture(s_texture, vec3(v_texCoord, 1.0 / 3.0)).x / 255.0,"
    "texture(s_texture, vec3(v_texCoord, 2.0 / 3.0)).x / 255.0,"
#endif
    "texture(s_texture, vec3(v_texCoord, 1.0)).x / 255.0"
    ");\n"
  "}\n";

static GLfloat fbo_vVertices_pos[4][3] = {
  { -1.f, -1.f, 0.0f }, // Position 1
  { -1.f, 1.f, 0.0f },  // Position 0
  { 1.f, -1.f, 0.0f },  // Position 2
  { 1.f, 1.f, 0.0f },   // Position 3
};

static GLfloat fbo_vVertices_tex_coord[4][2] = {
  { 0.0f, 0.0f }, // TexCoord 2
  { 0.0f, 1.0f }, // TexCoord 1
  { 1.0f, 0.0f }, // TexCoord 0
  { 1.0f, 1.0f }, // TexCoord 3
};
static GLushort fbo_indices[] =
  {0, 1, 2, 1, 2, 3};

// static GLfloat vVertices_pos[4][3] = {
//   { -0.5f, 0.5f, 0.0f },  // Position 0
//   { -0.5f, -0.5f, 0.0f }, // Position 1
//   { 0.5f, -0.5f, 0.0f },  // Position 2
//   { 0.5f, 0.5f, 0.0f },   // Position 3
// };

static GLfloat vVertices_tex_coord[4][2] = {
  { 1.0f, 0.0f }, // TexCoord 2
  { 0.0f, 0.0f }, // TexCoord 1
  { 0.0f, 1.0f }, // TexCoord 0
  { 1.0f, 1.0f }, // TexCoord 3
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
      err_log("Error compiling shader: %s\n", infoLog);

      free(infoLog);
    }

    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

static GLuint LoadProgram(const char *vertShaderSrc, const char *fragShaderSrc)
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
      err_log("Error linking program: %s\n", infoLog);

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

GLuint gl_program[SCREEN_COUNT];
GLint gl_position_loc[SCREEN_COUNT];
GLint gl_tex_coord_loc[SCREEN_COUNT];
GLint gl_sampler_loc[SCREEN_COUNT];

GLuint gl_fbo_program[SCREEN_COUNT];
GLint gl_fbo_position_loc[SCREEN_COUNT];
GLint gl_fbo_tex_coord_loc[SCREEN_COUNT];
GLint gl_fbo_sampler_loc[SCREEN_COUNT];
#endif

typedef struct _FrameBufferContext
{
  GLuint gl_tex_id[SCREEN_COUNT];
  GLuint gl_tex_upscaled[SCREEN_COUNT];
  GLuint gl_fbo_upscaled[SCREEN_COUNT];

  uint8_t screen_decoded[FrameBufferCount][400 * 240 * GL_CHANNELS_N];
  uint8_t screen_upscaled[400 * 240 * GL_CHANNELS_N * screen_upscale_factor * screen_upscale_factor];

  rp_lock_t status_lock;
  enum FrameBufferStatus status;
  int index_display_2;
  int index_display;
  int index_ready_display_2;
  int index_ready_display;
  int index_decode;
  uint8_t *prev_data;
  GLuint prev_tex_upscaled[SCREEN_COUNT], prev_tex_fsr[SCREEN_COUNT];
  int prev_win_w, prev_win_h;
  view_mode_t prev_vm;

#ifndef SDL_GL_SINGLE_THREAD
  int decode_updated;
  rp_cond_t decode_updated_cond;
  rp_lock_t decode_updated_mutex;
#endif
} FrameBufferContext;

#ifdef SDL_GL_SINGLE_THREAD
int decode_updated;
rp_cond_t decode_updated_cond;
rp_lock_t decode_updated_mutex;
#endif

FrameBufferContext buffer_ctx[SCREEN_COUNT];

#ifdef USE_VAO
struct vao_vertice_t {
  GLfloat pos[3];
  GLfloat tex_coord[2];
};
#endif

#ifdef USE_SDL_RENDERER
static void do_hr_draw_screen(__attribute__ ((unused)) FrameBufferContext *ctx, uint8_t *data, int width, int height, int top_bot, int tb, __attribute__ ((unused)) int index)
{
  if (data) {
    void *pixels;
    int pitch;
    if (SDL_LockTexture(sdlTexture[tb][top_bot], NULL, &pixels, &pitch) < 0) {
      err_log("SDL_LockTexture: %s\n", SDL_GetError());
      return;
    }

    uint8_t *dst = pixels;
    const int bpp = 4;
    for (int x = 0; x < width; ++x) {
      for (int y = 0; y < height; ++y) {
        memcpy(dst + y * pitch + x * bpp, data + x * height * bpp + (height - y - 1) * bpp, bpp);
      }
    }

    SDL_UnlockTexture(sdlTexture[tb][top_bot]);
  }

  int ctx_left;
  int ctx_top;
  int ctx_width;
  int ctx_height;
  // int tb;
  view_mode_t vm = __atomic_load_n(&view_mode, __ATOMIC_RELAXED);
  if (vm == VIEW_MODE_TOP_BOT) {
    // tb = SCREEN_TOP;

    ctx_height = (double)win_height[tb] / 2;
    if ((double)win_width[tb] / width * height > ctx_height)
    {
      ctx_width = (double)ctx_height / height * width;
      ctx_left = (double)(win_width[tb] - ctx_width) / 2;
      ctx_top = 0;
    }
    else
    {
      ctx_height = (double)win_width[tb] / width * height;
      ctx_left = 0;
      ctx_width = win_width[tb];
      ctx_top = (double)win_height[tb] / 2 - ctx_height;
    }

    if (top_bot != SCREEN_TOP)
    {
      ctx_top = (double)win_height[tb] / 2;
    }
  } else {
    // tb = top_bot;
    // if (vm == VIEW_MODE_BOT)
      // tb = SCREEN_TOP;

    ctx_height = (double)win_height[tb];
    if ((double)win_width[tb] / width * height > ctx_height)
    {
      ctx_width = (double)ctx_height / height * width;
      ctx_left = (double)(win_width[tb] - ctx_width) / 2;
      ctx_top = 0;
    }
    else
    {
      ctx_height = (double)win_width[tb] / width * height;
      ctx_left = 0;
      ctx_width = win_width[tb];
      ctx_top = ((double)win_height[tb] - ctx_height) / 2;
    }
  }

  SDL_Rect rect = { ctx_left, ctx_top, ctx_width, ctx_height };
  SDL_RenderCopy(sdlRenderer[tb], sdlTexture[tb][top_bot], NULL, &rect);
}
#else
static void do_hr_draw_screen(FrameBufferContext *ctx, uint8_t *data, int width, int height, int top_bot, int tb, int index)
{
  double ctx_left_f;
  double ctx_top_f;
  double ctx_right_f;
  double ctx_bot_f;
  int ctx_width;
  int ctx_height;
  int win_w;
  int win_h;
  // int tb;
  view_mode_t vm = __atomic_load_n(&view_mode, __ATOMIC_RELAXED);
  if (vm == VIEW_MODE_TOP_BOT) {
    // tb = SCREEN_TOP;
    win_w = win_width[tb];
    win_h = win_height[tb];

    ctx_height = (double)win_height[tb] / 2;
    int ctx_left;
    int ctx_top;
    if ((double)win_width[tb] / width * height > ctx_height)
    {
      ctx_width = (double)ctx_height / height * width;
      ctx_left = (double)(win_width[tb] - ctx_width) / 2;
      ctx_top = 0;
    }
    else
    {
      ctx_height = (double)win_width[tb] / width * height;
      ctx_left = 0;
      ctx_width = win_width[tb];
      ctx_top = (double)win_height[tb] / 2 - ctx_height;
    }

    if (top_bot == SCREEN_TOP)
    {
      ctx_left_f = (double)ctx_left / win_width[tb] * 2 - 1;
      ctx_top_f = 1 - (double)ctx_top / win_height[tb] * 2;
      ctx_right_f = -ctx_left_f;
      ctx_bot_f = 0;
    }
    else
    {
      ctx_left_f = (double)ctx_left / win_width[tb] * 2 - 1;
      ctx_top_f = 0;
      ctx_right_f = -ctx_left_f;
      ctx_bot_f = -1 + (double)ctx_top / win_height[tb] * 2;
    }
  } else {
    // tb = top_bot;
    // if (vm == VIEW_MODE_BOT)
      // tb = SCREEN_TOP;

    win_w = win_width[tb];
    win_h = win_height[tb];

    ctx_height = (double)win_height[tb];
    int ctx_left;
    int ctx_top;
    if ((double)win_width[tb] / width * height > ctx_height)
    {
      ctx_width = (double)ctx_height / height * width;
      ctx_left = (double)(win_width[tb] - ctx_width) / 2;
      ctx_top = 0;
    }
    else
    {
      ctx_height = (double)win_width[tb] / width * height;
      ctx_left = 0;
      ctx_width = win_width[tb];
      ctx_top = ((double)win_height[tb] - ctx_height) / 2;
    }

    ctx_left_f = (double)ctx_left / win_width[tb] * 2 - 1;
    ctx_top_f = 1 - (double)ctx_top / win_height[tb] * 2;
    ctx_right_f = -ctx_left_f;
    ctx_bot_f = -ctx_top_f;
  }
  GLfloat vVertices_pos[4][3] = { 0 };
  vVertices_pos[0][0] = ctx_left_f;
  vVertices_pos[0][1] = ctx_top_f;
  vVertices_pos[1][0] = ctx_left_f;
  vVertices_pos[1][1] = ctx_bot_f;
  vVertices_pos[2][0] = ctx_right_f;
  vVertices_pos[2][1] = ctx_bot_f;
  vVertices_pos[3][0] = ctx_right_f;
  vVertices_pos[3][1] = ctx_top_f;

#ifdef USE_VAO
  struct vao_vertice_t vertices[4];
  for (int i = 0; i < 4; ++i) {
    memcpy(vertices[i].pos, vVertices_pos[i], sizeof(vertices[i].pos));
    memcpy(vertices[i].tex_coord, vVertices_tex_coord[i], sizeof(vertices[i].tex_coord));
  }
#endif

  nk_bool upscaled = screen_upscale_factor > 1 && upscaling_filter && upscaling_filter_created;
  int scale = upscaled ? screen_upscale_factor : 1;
  GLuint tex = upscaled ? ctx->gl_tex_upscaled[tb] : ctx->gl_tex_id[tb];
  GLuint gl_sem = 0;
  GLuint gl_sem_next = 0;
  GLuint tex_upscaled = 0;
  bool dim3 = false;
  bool success = false;

  if (upscaled) {
    if (!data) {
      if (!ctx->prev_tex_upscaled[tb]) {
        data = ctx->prev_data;
      } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx->prev_tex_upscaled[tb]);
        tex = ctx->prev_tex_upscaled[tb];
      }
    }

    if (data) {
      scale = screen_upscale_factor;
      tex_upscaled = sr_run(tb, top_bot * FrameBufferCount + index, height, width, GL_CHANNELS_N, data, ctx->screen_upscaled, &gl_sem, &gl_sem_next, &dim3, &success);
      if (!tex_upscaled) {
        if (!success) {
          upscaled = 0;
          upscaling_filter = 0;
          err_log("upscaling failed; filter disabled\n");
        } else {
          glActiveTexture(GL_TEXTURE0);
          glBindTexture(GL_TEXTURE_2D, ctx->gl_tex_id[tb]);
          glTexImage2D(
            GL_TEXTURE_2D, 0,
            GL_INT_FORMAT, height * scale,
            width * scale, 0,
            GL_FORMAT, GL_UNSIGNED_BYTE,
            ctx->screen_upscaled);

          tex = ctx->gl_tex_id[tb];
        }
      } else {
        glActiveTexture(GL_TEXTURE0);

        if (dim3) {
          glBindTexture(GL_TEXTURE_3D, tex_upscaled);
          glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->gl_fbo_upscaled[tb]);
          glViewport(0, 0, height * scale, width * scale);
          glDisable(GL_CULL_FACE);
          glDisable(GL_DEPTH_TEST);

          glUseProgram(gl_fbo_program[tb]);

#ifdef USE_VAO
          glBindVertexArray(glFboVao[tb]);
          glBindBuffer(GL_ARRAY_BUFFER, glFboVbo[tb]);
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glFboEbo[tb]);
#else
          glEnableVertexAttribArray(gl_fbo_position_loc[tb]);
          glEnableVertexAttribArray(gl_fbo_tex_coord_loc[tb]);
          glVertexAttribPointer(gl_fbo_position_loc[tb], 3, GL_FLOAT, GL_FALSE, sizeof(*fbo_vVertices_pos), fbo_vVertices_pos);
          glVertexAttribPointer(gl_fbo_tex_coord_loc[tb], 2, GL_FLOAT, GL_FALSE, sizeof(*fbo_vVertices_tex_coord), fbo_vVertices_tex_coord);
#endif

          glUniform1i(gl_fbo_sampler_loc[tb], 0);

          if (gl_sem) {
            GLenum layout = GL_LAYOUT_TRANSFER_DST_EXT;
            glWaitSemaphoreEXT(gl_sem, 0, NULL, 1, &tex_upscaled, &layout);
          }
#ifdef USE_VAO
          glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
#else
          glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, fbo_indices);
#endif
          if (gl_sem && gl_sem_next) {
            GLenum layout = GL_LAYOUT_TRANSFER_DST_EXT;
            glSignalSemaphoreEXT(gl_sem_next, 0, NULL, 1, &tex_upscaled, &layout);
          }

          glBindTexture(GL_TEXTURE_2D, ctx->gl_tex_upscaled[tb]);
          tex = ctx->gl_tex_upscaled[tb];
        } else {
          glBindTexture(GL_TEXTURE_2D, tex_upscaled);
          tex = tex_upscaled;
        }
      }
    }

    ctx->prev_tex_upscaled[tb] = tex;
  }

  if (!upscaled) {
    scale = 1;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx->gl_tex_id[tb]);
    if (!data) {
      if (ctx->prev_tex_upscaled[tb]) {
        data = ctx->prev_data;
      }
    }
    if (data) {
      glTexImage2D(
        GL_TEXTURE_2D, 0,
        GL_INT_FORMAT, height,
        width, 0,
        GL_FORMAT, GL_UNSIGNED_BYTE,
        data);
    }

    tex = ctx->gl_tex_id[tb];

    ctx->prev_tex_upscaled[tb] = 0;
  }

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glViewport(0, 0, win_w, win_h);

  nk_bool use_fsr = ctx_height > height * scale && ctx_width > width * scale && fsr_filter;
#if defined(USE_ANGLE) && defined(_WIN32)
  nk_bool can_use_fsr = 0;
#else
#ifdef USE_OGL_ES
  nk_bool can_use_fsr = use_fsr && ogl_version_major >= 3 && ogl_version_minor >= 1;
#else
  nk_bool can_use_fsr = use_fsr && ogl_version_major >= 4 && ogl_version_minor >= 3;
#endif
#endif

  if (use_fsr) {
    if (!can_use_fsr) {
      if (upscaling_filter) {
        err_log("FSR not supported; using Real-CUGAN only\n");
      } else {
        err_log("FSR not supported; filter disabled\n");
      }
      fsr_filter = 0;
      use_fsr = 0;
    }
  }

  if (use_fsr) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (data || !ctx->prev_tex_fsr[tb] || ctx->prev_win_w != win_w || ctx->prev_win_h != win_h || ctx->prev_vm != vm) {
      if (!dim3 && tex_upscaled && gl_sem) {
        GLenum layout = GL_LAYOUT_TRANSFER_DST_EXT;
        glWaitSemaphoreEXT(gl_sem, 0, NULL, 1, &tex_upscaled, &layout);
      }
      glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
      GLuint out_tex = fsr_main(tb, top_bot, tex, height * scale, width * scale, ctx_height, ctx_width, 0.25f);
      ctx->prev_tex_fsr[tb] = out_tex;
      glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
      if (!dim3 && tex_upscaled && gl_sem && gl_sem_next) {
        GLenum layout = GL_LAYOUT_TRANSFER_DST_EXT;
        glSignalSemaphoreEXT(gl_sem_next, 0, NULL, 1, &tex_upscaled, &layout);
      }

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, out_tex);
    } else {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, ctx->prev_tex_fsr[tb]);
    }

    glUseProgram(gl_program[tb]);

#ifdef USE_VAO
    glBindVertexArray(glVao[tb][top_bot]);
    glBindBuffer(GL_ARRAY_BUFFER, glVbo[tb][top_bot]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glEbo[tb]);
    if (ctx->prev_win_w != win_w || ctx->prev_win_h != win_h || ctx->prev_vm != vm)
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
#else
    glEnableVertexAttribArray(gl_position_loc[tb]);
    glEnableVertexAttribArray(gl_tex_coord_loc[tb]);
    glVertexAttribPointer(gl_position_loc[tb], 3, GL_FLOAT, GL_FALSE, sizeof(*vVertices_pos), vVertices_pos);
    glVertexAttribPointer(gl_tex_coord_loc[tb], 2, GL_FLOAT, GL_FALSE, sizeof(*vVertices_tex_coord), vVertices_tex_coord);
#endif

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

    glUniform1i(gl_sampler_loc[tb], 0);
#ifdef USE_VAO
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
#else
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
#endif
  } else {
    glUseProgram(gl_program[tb]);

#ifdef USE_VAO
    glBindVertexArray(glVao[tb][top_bot]);
    glBindBuffer(GL_ARRAY_BUFFER, glVbo[tb][top_bot]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glEbo[tb]);
    if (ctx->prev_win_w != win_w || ctx->prev_win_h != win_h || ctx->prev_vm != vm)
      glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
#else
    glEnableVertexAttribArray(gl_position_loc[tb]);
    glEnableVertexAttribArray(gl_tex_coord_loc[tb]);
    glVertexAttribPointer(gl_position_loc[tb], 3, GL_FLOAT, GL_FALSE, sizeof(*vVertices_pos), vVertices_pos);
    glVertexAttribPointer(gl_tex_coord_loc[tb], 2, GL_FLOAT, GL_FALSE, sizeof(*vVertices_tex_coord), vVertices_tex_coord);
#endif

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

    glUniform1i(gl_sampler_loc[tb], 0);

    if (!dim3 && tex_upscaled && gl_sem) {
      GLenum layout = GL_LAYOUT_TRANSFER_DST_EXT;
      glWaitSemaphoreEXT(gl_sem, 0, NULL, 1, &tex_upscaled, &layout);
    }
#ifdef USE_VAO
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
#else
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
#endif
    if (!dim3 && tex_upscaled && gl_sem && gl_sem_next) {
      GLenum layout = GL_LAYOUT_TRANSFER_DST_EXT;
      glSignalSemaphoreEXT(gl_sem_next, 0, NULL, 1, &tex_upscaled, &layout);
    }

    ctx->prev_tex_fsr[tb] = 0;
  }

  ctx->prev_win_w = win_w;
  ctx->prev_win_h = win_h;
  ctx->prev_vm = vm;
}
#endif

static int hr_draw_screen(FrameBufferContext *ctx, int width, int height, int top_bot, int tb, int force)
{
  sr_next(tb, top_bot * FrameBufferCount + ctx->index_display_2);

  rp_lock_wait(ctx->status_lock);
  enum FrameBufferStatus status = ctx->status;
  if (ctx->status == FBS_UPDATED_2) {
    int index = ctx->index_ready_display_2;
    ctx->index_ready_display_2 = ctx->index_display_2;
    ctx->index_display_2 = ctx->index_display;
    ctx->index_display = index;
    ctx->status = FBS_UPDATED;
  } else if (ctx->status == FBS_UPDATED) {
    int index = ctx->index_ready_display;
    ctx->index_ready_display = ctx->index_display_2;
    ctx->index_display_2 = ctx->index_display;
    ctx->index_display = index;
    ctx->status = FBS_NOT_UPDATED;
  }
  int index_display = ctx->index_display;
  rp_lock_rel(ctx->status_lock);

  if (status == FBS_NOT_AVAIL)
    return 0;

  uint8_t *data = ctx->screen_decoded[index_display];
  ctx->prev_data = data;
  if (status >= FBS_UPDATED)
  {
    __atomic_add_fetch(&frame_rate_displayed_tracker[top_bot], 1, __ATOMIC_RELAXED);
    do_hr_draw_screen(ctx, data, width, height, top_bot, tb, index_display);
    return 1;
  }
  else
  {
    if (force)
      do_hr_draw_screen(ctx, NULL, width, height, top_bot, tb, index_display);
    return 0;
  }
}

static double kcp_get_connection_quality(void)
{
  int input_count = __atomic_load_n(&kcp_input_count, __ATOMIC_RELAXED);
  double ret = input_count ? (double)__atomic_load_n(&kcp_recv_pid_count, __ATOMIC_RELAXED) / input_count : 0.0;
  return ret * ret * 100;
}

static void kcpUpdateWindowTitle(SDL_Window *win, int tick_diff)
{
  snprintf(window_title_with_fps, sizeof(window_title_with_fps),
    TITLE " (FPS %03d %03d | %03d %03d)"
    " (Connection Quality %.1f%%)"
    // " (Counter %d %d %d %d)"
    " [Reliable Stream Mode]"
    ,
    __atomic_load_n(&frame_rate_decoded_tracker[SCREEN_TOP], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
    __atomic_load_n(&frame_rate_decoded_tracker[SCREEN_BOT], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
    __atomic_load_n(&frame_rate_displayed_tracker[SCREEN_TOP], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
    __atomic_load_n(&frame_rate_displayed_tracker[SCREEN_BOT], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
    kcp_get_connection_quality()
    // __atomic_load_n(&kcp_input_count, __ATOMIC_RELAXED),
    // __atomic_load_n(&kcp_input_fid_count, __ATOMIC_RELAXED),
    // __atomic_load_n(&kcp_input_pid_count, __ATOMIC_RELAXED),
    // __atomic_load_n(&kcp_recv_pid_count, __ATOMIC_RELAXED)
  );
  SDL_SetWindowTitle(win, window_title_with_fps);
}

static void kcpUpdateWindowsTitles(int tb, int top_bot, int tick_diff)
{
  snprintf(window_title_with_fps, sizeof(window_title_with_fps),
    tb == SCREEN_TOP ?
      TITLE " (FPS %03d | %03d)"
      " (Connection Quality %.1f%%)"
      // " (Counter %d %d %d %d)"
      " [Reliable Stream Mode]"
      :
      TITLE " (FPS %03d | %03d)",
    __atomic_load_n(&frame_rate_decoded_tracker[top_bot], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / tick_diff,
    __atomic_load_n(&frame_rate_displayed_tracker[top_bot], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / tick_diff,
    kcp_get_connection_quality()
    // __atomic_load_n(&kcp_input_count, __ATOMIC_RELAXED),
    // __atomic_load_n(&kcp_input_fid_count, __ATOMIC_RELAXED),
    // __atomic_load_n(&kcp_input_pid_count, __ATOMIC_RELAXED),
    // __atomic_load_n(&kcp_recv_pid_count, __ATOMIC_RELAXED)
  );
  SDL_SetWindowTitle(win[tb], window_title_with_fps);
}

static void updateWindowsTitles(void)
{
  uint64_t next_tick = iclock64();
  uint64_t tick_diff = next_tick - window_title_last_tick;
  if (tick_diff >= FRAME_STAT_EVERY_X_US) {
    int frame_fully_received = __atomic_load_n(&frame_fully_received_tracker, __ATOMIC_RELAXED);
    int frame_lost = __atomic_load_n(&frame_lost_tracker, __ATOMIC_RELAXED);
    double packet_rate = frame_fully_received ? (double)frame_fully_received / (frame_fully_received + frame_lost) * 100 : 0.0;

    int vm = __atomic_load_n(&view_mode, __ATOMIC_RELAXED);

    if (vm == VIEW_MODE_TOP_BOT) {
      if (kcp_active) {
        kcpUpdateWindowTitle(win[SCREEN_TOP], (int)tick_diff);
      } else {
        snprintf(window_title_with_fps, sizeof(window_title_with_fps),
          TITLE " (FPS %03d %03d | %03d %03d)"
          " (Packet Rate %.1f%%)"
          // " (Size %06d %06d) (Packet time %04d %04d)"
          " [Compatibility Mode]"
          ,
          __atomic_load_n(&frame_rate_decoded_tracker[SCREEN_TOP], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
          __atomic_load_n(&frame_rate_decoded_tracker[SCREEN_BOT], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
          __atomic_load_n(&frame_rate_displayed_tracker[SCREEN_TOP], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
          __atomic_load_n(&frame_rate_displayed_tracker[SCREEN_BOT], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
          packet_rate
          // __atomic_load_n(&frame_size_tracker[SCREEN_TOP], __ATOMIC_RELAXED),
          // __atomic_load_n(&frame_size_tracker[SCREEN_BOT], __ATOMIC_RELAXED),
          // __atomic_load_n(&delay_between_packet_tracker[SCREEN_TOP], __ATOMIC_RELAXED),
          // __atomic_load_n(&delay_between_packet_tracker[SCREEN_BOT], __ATOMIC_RELAXED)
        );
        SDL_SetWindowTitle(win[SCREEN_TOP], window_title_with_fps);
      }
    } else {
      for (int top_bot = 0; top_bot < SCREEN_COUNT; ++top_bot) {
        int tb = top_bot;
        if (vm == VIEW_MODE_BOT) {
          tb = SCREEN_TOP;
          top_bot = SCREEN_BOT;
        }
        if (kcp_active) {
          kcpUpdateWindowsTitles(tb, top_bot, (int)tick_diff);
        } else {
          snprintf(window_title_with_fps, sizeof(window_title_with_fps),
            tb == SCREEN_TOP ?
              TITLE " (FPS %03d | %03d) "
              " (Packet Rate %.1f%%)"
              // "(Size %06d) (Packet time %04d)"
              " [Compatibility Mode]"
              :
              TITLE " (FPS %03d | %03d) "
              // "(Size %06d) (Packet time %04d)"
            ,
            __atomic_load_n(&frame_rate_decoded_tracker[top_bot], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
            __atomic_load_n(&frame_rate_displayed_tracker[top_bot], __ATOMIC_RELAXED) * FRAME_STAT_EVERY_X_US / (int)tick_diff,
            packet_rate
            // __atomic_load_n(&frame_size_tracker[top_bot], __ATOMIC_RELAXED),
            // __atomic_load_n(&delay_between_packet_tracker[top_bot], __ATOMIC_RELAXED)
          );
          SDL_SetWindowTitle(win[tb], window_title_with_fps);
        }
        if (vm != VIEW_MODE_SEPARATE) {
          break;
        }
      }
    }

    window_title_last_tick = next_tick;
    for (int top_bot = 0; top_bot < SCREEN_COUNT; ++top_bot) {
      __atomic_store_n(&frame_rate_decoded_tracker[top_bot], 0, __ATOMIC_RELAXED);
      __atomic_store_n(&frame_rate_displayed_tracker[top_bot], 0, __ATOMIC_RELAXED);
      __atomic_store_n(&frame_size_tracker[top_bot], 0, __ATOMIC_RELAXED);
      __atomic_store_n(&delay_between_packet_tracker[top_bot], 0, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&kcp_input_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&kcp_input_fid_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&kcp_input_pid_count, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&kcp_recv_pid_count, 0, __ATOMIC_RELAXED);

    __atomic_store_n(&frame_fully_received_tracker, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&frame_lost_tracker, 0, __ATOMIC_RELAXED);
  }
}

static view_mode_t prev_view_mode;
static int prev_fullscreen;
static uint64_t lastUpdated[SCREEN_COUNT];
static int nk_input_current;
#ifndef SDL_GL_SINGLE_THREAD
static rp_lock_t nk_input_lock;
#endif

#ifdef SDL_GL_SYNC
static int gl_swapbuffer_flag;
static rp_cond_t gl_swapbuffer_cond;
static rp_lock_t gl_swapbuffer_mutex;

static int gl_render_flag;
static rp_cond_t gl_render_cond;
static rp_lock_t gl_render_mutex;
#endif

static struct nk_color nk_window_bgcolor = { 28, 48, 62, 255 };

#define MIN_UPDATE_INTERVAL_US (33333)

static bool decode_cond_wait(int *updated, rp_cond_t *cond, rp_lock_t *mutex, int *force)
{
  rp_lock_wait(*mutex);
  while (!*updated) {
    if (!running) {
      rp_lock_rel(*mutex);
      return false;
    }
    int ret;
    if ((ret = rp_cond_timedwait(*cond, *mutex, MIN_UPDATE_INTERVAL_US * 1000))) {
      if (ret == ETIMEDOUT) {
        *force = 1;
        break;
      } else {
        err_log("decode_cond_wait wait error: %d\n", ret);
        rp_lock_rel(*mutex);
        return false;
      }
    }
  }
  *updated = 0;
  rp_lock_rel(*mutex);
  return true;
}

#ifdef USE_SDL_RENDERER
float font_scale;
#endif

static void
ThreadLoop(int i)
{
  /* Draw */
  int screen_count = SCREEN_COUNT;
  view_mode_t vm = __atomic_load_n(&view_mode, __ATOMIC_RELAXED);
  if (vm != VIEW_MODE_SEPARATE)
    screen_count = 1;

  if (i >= screen_count) {
#ifndef SDL_GL_SINGLE_THREAD
    Sleep(REST_EVERY_MS);
#endif
    return;
  }

  float bg[4];
  nk_color_fv(bg, nk_window_bgcolor);
#ifdef USE_SDL_RENDERER
  /* scale the renderer output for High-DPI displays */
  {
    int render_w, render_h;
    float scale_x, scale_y;
    SDL_GetRendererOutputSize(sdlRenderer[i], &render_w, &render_h);
    SDL_GetWindowSize(win[i], &win_width[i], &win_height[i]);
    scale_x = (float)(render_w) / (float)(win_width[i]);
    scale_y = (float)(render_h) / (float)(win_height[i]);
    SDL_RenderSetScale(sdlRenderer[i], scale_x, scale_y);
    if (i == SCREEN_TOP && font_scale != scale_y) {
      font_scale = scale_y;

      struct nk_context *ctx = nk_ctx;

      /* Load Fonts: if none of these are loaded a default font will be used  */
      /* Load Cursor: if you uncomment cursor loading please hide the cursor */
      struct nk_font_atlas *atlas;
      struct nk_font_config config = nk_font_config(0);
      struct nk_font *font;

      /* set up the font atlas and add desired font; note that font sizes are
        * multiplied by font_scale to produce better results at higher DPIs */
      nk_sdl_font_stash_begin(&atlas);
      font = nk_font_atlas_add_default(atlas, 13 * font_scale, &config);
      nk_sdl_font_stash_end();

      /* this hack makes the font appear to be scaled down to the desired
        * size and is only necessary when font_scale > 1 */
      font->handle.height /= font_scale;
      /*nk_style_load_all_cursors(ctx, atlas->cursors);*/
      nk_style_set_font(ctx, &font->handle);
    }
  }
  SDL_SetRenderDrawColor(sdlRenderer[i], bg[0]* 255, bg[1] * 255, bg[2] * 255, bg[3] * 255);
  SDL_RenderClear(sdlRenderer[i]);
#else
  // SDL_GetWindowSize(win[i], &win_width[i], &win_height[i]);
  SDL_GL_GetDrawableSize(win[i], &win_width[i], &win_height[i]);

#ifdef SDL_GL_SINGLE_THREAD
  SDL_GL_MakeCurrent(win[i], glContext[i]);
#endif
  glViewport(0, 0, win_width[i], win_height[i]);
  glClearColor(bg[0], bg[1], bg[2], bg[3]);
  glClear(GL_COLOR_BUFFER_BIT);
#endif

  int force = 0;
#ifndef SDL_GL_SINGLE_THREAD
  int tb = vm == VIEW_MODE_BOT ? SCREEN_BOT : i;
  if (!decode_cond_wait(&buffer_ctx[tb].decode_updated, &buffer_ctx[tb].decode_updated_cond, &buffer_ctx[tb].decode_updated_mutex, &force))
    return;
#else
  if (i == SCREEN_TOP && !decode_cond_wait(&decode_updated, &decode_updated_cond, &decode_updated_mutex, &force))
    return;
#endif

  int updated = 0;
  uint64_t nextUpdated = iclock64();
  if (nextUpdated - lastUpdated[i] > MIN_UPDATE_INTERVAL_US)
    force = 1;

  if (vm == VIEW_MODE_TOP_BOT) {
    force = 1;
    updated |= hr_draw_screen(&buffer_ctx[SCREEN_TOP], 400, 240, SCREEN_TOP, i, force);
    updated |= hr_draw_screen(&buffer_ctx[SCREEN_BOT], 320, 240, SCREEN_BOT, i, force);
  } else if (vm == VIEW_MODE_BOT)
    updated = hr_draw_screen(&buffer_ctx[SCREEN_BOT], 320, 240, 1, i, force);
  else
    updated = hr_draw_screen(&buffer_ctx[i], i == 0 ? 400 : 320, 240, i, i, force);

  /* IMPORTANT: `nk_sdl_render` modifies some global OpenGL state
    * with blending, scissor, face culling, depth test and viewport and
    * defaults everything back into a default state.
    * Make sure to either a.) save and restore or b.) reset your own state after
    * rendering the UI. */
  if (updated || force) {
    if (i == 0) {
      struct nk_context *ctx = nk_ctx;

#ifndef SDL_GL_SINGLE_THREAD
      rp_lock_wait(nk_input_lock);
#endif
      if (nk_input_current) {
        // nk_sdl_handle_grab();
        nk_input_end(ctx);
      } else {
        nk_input_begin(ctx);
        // nk_sdl_handle_grab();
        nk_input_end(ctx);
      }
      nk_input_current = 0;

      guiMain(ctx);
#ifndef SDL_GL_SINGLE_THREAD
      rp_lock_rel(nk_input_lock);
#endif
    }

#ifdef SDL_GL_SYNC
    if (!cond_mutex_flag_lock(&gl_swapbuffer_flag, &gl_swapbuffer_cond, &gl_swapbuffer_mutex))
      return;
    gl_swapbuffer_flag = 0;
    rp_lock_rel(gl_swapbuffer_mutex);
#endif

#ifdef USE_SDL_RENDERER
    if (i == 0) {
      nk_sdl_render(NK_ANTI_ALIASING_ON);
    }
    SDL_RenderPresent(sdlRenderer[i]);
#else
    if (i == 0) {
      nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);
    }
    // thread_set_cancel_state(false);
    SDL_GL_SwapWindow(win[i]);
    // thread_set_cancel_state(true);
#endif

#ifdef SDL_GL_SYNC
    cond_mutex_flag_signal(&gl_render_flag, &gl_render_cond, &gl_render_mutex);
#endif
    lastUpdated[i] = nextUpdated;
  }
}

static void
MainLoop(void *loopArg)
{
  struct nk_context *ctx = (struct nk_context *)loopArg;

  /* Input */
  SDL_Event evt;
  while (SDL_PollEvent(&evt))
  {
    if (
      evt.type == SDL_QUIT ||
      (evt.type == SDL_WINDOWEVENT && evt.window.event == SDL_WINDOWEVENT_CLOSE)
    ) {
      running = 0;
      return;
    } else if (
      evt.type == SDL_KEYDOWN &&
      evt.key.keysym.sym == SDLK_f
    ) {
      fullscreen = !fullscreen;
    } else {
      switch (evt.type) {
        case SDL_MOUSEMOTION:
          if (evt.motion.windowID != win_id[SCREEN_TOP]) {
            goto skip_evt;
          }
          break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
          if (evt.button.windowID != win_id[SCREEN_TOP]) {
            goto skip_evt;
          }
          break;
        case SDL_MOUSEWHEEL:
          if (evt.wheel.windowID != win_id[SCREEN_TOP]) {
            goto skip_evt;
          }
          break;
        case SDL_KEYDOWN:
          switch (evt.key.keysym.sym) {
            case SDLK_TAB: {
              const Uint8* state = SDL_GetKeyboardState(0);
              __atomic_store_n(&nk_nav_command, state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT] ? NK_NAV_PREVIOUS : NK_NAV_NEXT, __ATOMIC_RELAXED);
              goto skip_evt;
            }

            case SDLK_SPACE:
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
              __atomic_store_n(&nk_nav_command, NK_NAV_CONFIRM, __ATOMIC_RELAXED);
              goto skip_evt;

            case SDLK_ESCAPE:
              __atomic_store_n(&nk_nav_command, NK_NAV_CANCEL, __ATOMIC_RELAXED);
              goto skip_evt;
          }
          break;
      }

#ifndef SDL_GL_SINGLE_THREAD
      rp_lock_wait(nk_input_lock);
#endif
      if (!nk_input_current) {
        nk_input_begin(ctx);
        nk_input_current = 1;
      }
      nk_sdl_handle_event(&evt);
#ifndef SDL_GL_SINGLE_THREAD
      rp_lock_rel(nk_input_lock);
#endif

skip_evt:
    }
  }

  view_mode_t vm = __atomic_load_n(&view_mode, __ATOMIC_RELAXED);
  if (prev_view_mode != vm) {
    updateViewMode(vm);
    prev_view_mode = vm;
  }

  if (prev_fullscreen != fullscreen) {
    if (fullscreen) {
      SDL_SetWindowFullscreen(win[SCREEN_TOP], SDL_WINDOW_FULLSCREEN_DESKTOP);
      if (SDL_GetWindowDisplayIndex(win[SCREEN_TOP]) != SDL_GetWindowDisplayIndex(win[SCREEN_BOT])) {
        SDL_SetWindowFullscreen(win[SCREEN_BOT], SDL_WINDOW_FULLSCREEN_DESKTOP);
      }
    } else {
      updateViewMode(vm);
    }
    prev_fullscreen = fullscreen;
  }

  updateWindowsTitles();

#ifdef SDL_GL_SINGLE_THREAD
  for (int i = 0; i < SCREEN_COUNT; ++i)
    ThreadLoop(i);
#endif

#ifdef SDL_GL_SYNC
  cond_mutex_flag_signal(&gl_swapbuffer_flag, &gl_swapbuffer_cond, &gl_swapbuffer_mutex);

  if (!cond_mutex_flag_lock(&gl_render_flag, &gl_render_cond, &gl_render_mutex))
    return;
  gl_render_flag = 0;
  rp_lock_rel(gl_render_mutex);
#endif
}

#ifndef SDL_GL_SINGLE_THREAD
static thread_ret_t window_thread_func(void *arg)
{
  int i = (int)(uintptr_t)arg;
  SDL_GL_MakeCurrent(win[i], glContext[i]);
  while (running)
    ThreadLoop(i);
  SDL_GL_MakeCurrent(NULL, NULL);
  return (thread_ret_t)(uintptr_t)NULL;
}
#endif

static int acquire_sem(rp_sem_t *sem) {
  while (1) {
    if (!running)
      return -1;
    int res = rp_sem_timedwait(*sem, NWM_THREAD_WAIT_NS, NULL);
    if (res == 0)
      return 0;
    if (res != ETIMEDOUT) {
      return -1;
    }
  }
}

void handle_decode_frame_screen(FrameBufferContext *ctx, int top_bot, int frame_size, int delay_between_packet, __attribute__ ((unused)) FrameBufferContext *ctx_sync)
{
  __atomic_add_fetch(&frame_rate_decoded_tracker[top_bot], 1, __ATOMIC_RELAXED);
  if (__atomic_load_n(&frame_size_tracker[top_bot], __ATOMIC_RELAXED) < frame_size) {
    __atomic_store_n(&frame_size_tracker[top_bot], frame_size, __ATOMIC_RELAXED);
  }
  if (__atomic_load_n(&delay_between_packet_tracker[top_bot], __ATOMIC_RELAXED) < delay_between_packet) {
    __atomic_store_n(&delay_between_packet_tracker[top_bot], delay_between_packet, __ATOMIC_RELAXED);
  }

  rp_lock_wait(ctx->status_lock);
  // ctx_sync is set when view mode is top and bot in one window.
  // we can use this check to enable "triple buffering" only when the update rate is likely to exceed monitor refresh rate.
  if (/* ctx_sync && */ctx->status >= FBS_UPDATED) {
    int index = ctx->index_ready_display_2;
    ctx->index_ready_display_2 = ctx->index_ready_display;
    ctx->index_ready_display = ctx->index_decode;
    ctx->index_decode = index;
    ctx->status = FBS_UPDATED_2;
  } else {
    int index = ctx->index_ready_display;
    ctx->index_ready_display = ctx->index_decode;
    ctx->index_decode = index;
    ctx->status = FBS_UPDATED;
  }
  rp_lock_rel(ctx->status_lock);

#ifndef SDL_GL_SINGLE_THREAD
  if (ctx_sync)
    cond_mutex_flag_signal(&ctx_sync->decode_updated, &ctx_sync->decode_updated_cond, &ctx_sync->decode_updated_mutex);
  else
    cond_mutex_flag_signal(&ctx->decode_updated, &ctx->decode_updated_cond, &ctx->decode_updated_mutex);
#else
  cond_mutex_flag_signal(&decode_updated, &decode_updated_cond, &decode_updated_mutex);
#endif
}

uint8_t upscaled_u_image[400 * 240];
uint8_t upscaled_v_image[400 * 240];

#define BUF_SIZE 2000
uint8_t buf[BUF_SIZE];
ikcpcb *kcp;

#define PACKET_SIZE 1448
#define rp_data_hdr_size (4)
#define rp_packet_data_size (PACKET_SIZE - rp_data_hdr_size)
#define rp_data_hdr_id_size (2)

#define MAX_PACKET_COUNT (128)

#define rp_work_count (3)
uint8_t recv_buf[rp_work_count][PACKET_SIZE * MAX_PACKET_COUNT];
uint8_t recv_track[rp_work_count][MAX_PACKET_COUNT];
uint8_t recv_hdr[rp_work_count][rp_data_hdr_id_size];
uint8_t recv_end[rp_work_count];
uint8_t recv_end_packet[rp_work_count];
uint8_t recv_end_incomp[rp_work_count];
uint32_t recv_end_size[rp_work_count];
uint32_t recv_delay_between_packets[rp_work_count];
uint32_t recv_last_packet_time[rp_work_count];
uint8_t recv_work;
bool recv_has_last_frame_id[SCREEN_COUNT];
uint8_t recv_last_frame_id[SCREEN_COUNT];
uint8_t recv_last_packet_id[SCREEN_COUNT];

#define rp_core_count_max (3)

#ifdef EMBED_JPEG_TURBO
// void* rpMalloc(j_common_ptr cinfo, u32 size)
// {
//   void* ret = cinfo->alloc.buf + cinfo->alloc.stats.offset;
//   u32 totalSize = size;
//   if (totalSize % 32 != 0) {
//     totalSize += 32 - (totalSize % 32);
//   }
//   if (cinfo->alloc.stats.remaining < totalSize) {
//     u32 alloc_size = cinfo->alloc.stats.offset + cinfo->alloc.stats.remaining;
//     err_log("bad alloc, size: %d/%d\n", totalSize, alloc_size);
//     return 0;
//   }
//   cinfo->alloc.stats.offset += totalSize;
//   cinfo->alloc.stats.remaining -= totalSize;

// #if 0
//   if (cinfo->alloc.stats.offset > cinfo->alloc.max_offset) {
//     cinfo->alloc.max_offset = cinfo->alloc.stats.offset;
//     nsDbgPrint("cinfo %08x alloc.max_offset: %d\n", cinfo, cinfo->alloc.max_offset);
//   }
// #endif

//   return ret;
// }

// void rpFree(j_common_ptr, void*) {}

jmp_buf jpeg_jmp;

static void jpeg_error_exit(j_common_ptr cinfo)
{
  /* Always display the message */
  (*cinfo->err->output_message) (cinfo);

  /* Let the memory manager delete any temp files before we die */
  // jpeg_destroy(cinfo);
  longjmp(jpeg_jmp, 1);
}

static void jpeg_emit_message(j_common_ptr cinfo, int msg_level)
{
  struct jpeg_error_mgr *err = cinfo->err;

  if (msg_level < 0) {
    /* It's a warning message.  Since corrupt files may generate many warnings,
     * the policy implemented here is to show only the first warning,
     * unless trace_level >= 3.
     */
    if (err->num_warnings == 0 || err->trace_level >= 3)
      (*err->output_message) (cinfo);
    /* Always count warnings in num_warnings. */
    err->num_warnings++;
    // longjmp(jpeg_jmp, 1);
  } else {
    /* It's a trace message.  Show it if trace_level >= msg_level. */
    if (err->trace_level >= msg_level)
      (*err->output_message) (cinfo);
  }
}

static int handle_decode(uint8_t *out, uint8_t *in, int size, int w, int h) {
  struct jpeg_decompress_struct cinfo;

  // cinfo.alloc.buf = malloc(400 * 240 * GL_CHANNELS_N);
  // if (cinfo.alloc.buf) {
  //   cinfo.alloc.stats.offset = 0;
  //   cinfo.alloc.stats.remaining = 400 * 240 * GL_CHANNELS_N;
  // } else {
  //   return -1;
  // }

  struct jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = jpeg_error_exit;
  jerr.emit_message = jpeg_emit_message;

  int ret = 0;
  if (setjmp(jpeg_jmp) == 0) {
    jpeg_create_decompress(&cinfo);
    if (setjmp(jpeg_jmp) == 0) {
      jpeg_mem_src(&cinfo, in, size);
      ret = jpeg_read_header(&cinfo, TRUE);
      if (ret == JPEG_HEADER_OK) {
        cinfo.out_color_space = JCS_FORMAT;
        jpeg_start_decompress(&cinfo);
        // err_log("jpeg_read_header: %d %d (%d %d)\n", (int)cinfo.output_width, (int)cinfo.output_height, h, w);
        if ((int)cinfo.output_width == h && (int)cinfo.output_height == w) {
          while (cinfo.output_scanline < cinfo.output_height) {
            uint8_t *buffer = out + cinfo.output_scanline * cinfo.output_width * GL_CHANNELS_N;
            jpeg_read_scanlines(&cinfo, &buffer, 1);
          }
          jpeg_finish_decompress(&cinfo);
          jpeg_destroy_decompress(&cinfo);
          ret = 0;
        } else {
          jpeg_destroy_decompress(&cinfo);
          ret = -1;
        }
      } else {
        jpeg_destroy_decompress(&cinfo);
        ret = -1;
      }
    } else {
      jpeg_destroy_decompress(&cinfo);
      ret = -1;
    }
  } else {
    ret = -1;
  }
  // free(cinfo.alloc.buf);
  return ret;
}
#else
static int handle_decode(uint8_t *out, uint8_t *in, int size, int w, int h) {
  tjhandle tjInstance = NULL;
  if ((tjInstance = tj3Init(TJINIT_DECOMPRESS)) == NULL) {
    err_log("create turbo jpeg decompressor failed\n");
    return -1;
  }

  int ret = -1;

  if (tj3Set(tjInstance, TJPARAM_STOPONWARNING, 1) != 0) {
    goto final;
  }

  if (tj3DecompressHeader(tjInstance, in, size) != 0 ) {
    err_log("jpeg header error\n");
    goto final;
  }

  if (
    h != tj3Get(tjInstance, TJPARAM_JPEGWIDTH) ||
    w != tj3Get(tjInstance, TJPARAM_JPEGHEIGHT)
  ) {
    err_log("jpeg unexpected dimensions\n");
    goto final;
  }

  if (tj3Decompress8(tjInstance, in, size, out, h * GL_CHANNELS_N, TJ_FORMAT) != 0) {
    err_log("jpeg decompression error\n");
    goto final;
  }

  ret = 0;

final:
  tj3Destroy(tjInstance);
  return ret;
}
#endif

struct DecodeInfo {
  int top_bot;
  uint32_t in_size;
  uint32_t in_delay;

  union {
    struct {
      uint8_t *in;
      bool not_queued;
      uint8_t frame_id;
    };

    struct {
      int kcp_w;
      int kcp_queue_w;
    };
  };

  bool is_kcp;
} decode_info[rp_work_count];

struct DecodeInfo *decode_ptr[rp_work_count];

static int queue_decode(int work) {
  struct DecodeInfo *ptr = &decode_info[work];
  if (rp_syn_rel(&jpeg_decode_queue, ptr) != 0) {
    running = 0;
    return -1;
  }
  return 0;
}

static int acquire_decode() {
  int ret;
  if ((ret = acquire_sem(&jpeg_decode_sem)) != 0) {
    if (running) {
      running = 0;
      err_log("jpeg_decode_sem wait error\n");
    }
  }
  return ret;
}

static int handle_recv(uint8_t *buf, int size)
{
  if (size < rp_data_hdr_size) {
    err_log("recv header too small\n");
    return 0;
  }
  uint8_t *hdr = buf;
  buf += rp_data_hdr_size;
  size -= rp_data_hdr_size;

  // err_log("%d %d %d %d (%d)\n", hdr[0], hdr[1], hdr[2], hdr[3], size);

  if (hdr[2] != 2) {
    err_log("recv invalid header\n");
    return 0;
  }

  uint8_t end = 0;
  if (hdr[1] & 0x10) {
    end = 1;
  } else if (size != rp_packet_data_size) {
    err_log("recv incorrect size: %d\n", size);
    return 0;
  }
  hdr[1] &= 0x1;
  uint8_t work = recv_work;

#ifdef PRINT_PACKET_LOSS_INFO
  uint8_t frame_id = hdr[0];
  int frame_id_out_of_order = 0;
  uint8_t isTop = hdr[1];
  int top_bot = isTop ? 0 : 1;

  if (frame_id != recv_last_frame_id[top_bot]) {
    if ((uint8_t)(recv_last_frame_id[top_bot] + 1) == frame_id) {
      recv_has_last_frame_id[top_bot] = 1;
      recv_last_frame_id[top_bot] = frame_id;
      recv_last_packet_id[top_bot] = 0;
    } else if (recv_has_last_frame_id[top_bot]) {
      if ((int8_t)(frame_id - recv_last_frame_id[top_bot]) > 0) {
        err_log("recv frame id skipped: %d to %d (%d)\n", recv_last_frame_id[top_bot], frame_id, top_bot);
        recv_last_frame_id[top_bot] = frame_id;
        recv_last_packet_id[top_bot] = 0;
      } else {
        frame_id_out_of_order = 1;
        if ((int8_t)(frame_id - recv_last_frame_id[top_bot]) > -rp_work_count) {
          err_log("recv frame id out of order: %d current %d\n", frame_id, recv_last_frame_id[top_bot]);
        }
      }
    }
  }
#endif

  int work_next = 0;
  if (memcmp(recv_hdr[work], hdr, rp_data_hdr_id_size) != 0) {
    // If no decode_info is set at this point, it means network receive has skipped frame.
    // Queue empty info to keep in sync.
    if (decode_info[work].not_queued) {
      if (queue_decode(work) != 0) {
        return -1;
      }
    }

    work = (work + 1) % rp_work_count;
    work_next = 1;
  }

  if (work_next) {
    if (acquire_decode() != 0) {
      return -1;
    }

    decode_info[work] = (struct DecodeInfo) {0};
    decode_info[work].not_queued = true;

    memcpy(recv_hdr[work], hdr, rp_data_hdr_id_size);
    decode_info[work].frame_id = recv_hdr[work][0];
    decode_info[work].top_bot = !recv_hdr[work][1];

    recv_delay_between_packets[work] = 0;
    recv_last_packet_time[work] = iclock();

    memset(recv_track[work], 0, MAX_PACKET_COUNT);
    if (recv_end[work] != 2) {
#ifndef PRINT_PACKET_LOSS_INFO
      err_log("recv incomplete skipping frame\n");
#endif
    }
    recv_end[work] = 0;
    recv_end_incomp[work] = 0;

    recv_work = work;
  }

  uint8_t packet = hdr[3];
  if (packet >= MAX_PACKET_COUNT) {
    err_log("recv packet number too high\n");
    return 0;
  }

#ifdef PRINT_PACKET_LOSS_INFO
  if (recv_has_last_frame_id[top_bot] && !frame_id_out_of_order && packet != recv_last_packet_id[top_bot]) {
    if ((uint8_t)(recv_last_packet_id[top_bot] + 1) == packet) {
      recv_last_packet_id[top_bot] = packet;
    } else if ((int8_t)(packet - recv_last_packet_id[top_bot]) > 0) {
      err_log("recv packet skipped: %d to %d (%d:%d)\n", recv_last_packet_id[top_bot], packet, top_bot, recv_last_frame_id[top_bot]);
      recv_last_packet_id[top_bot] = packet;
    } else {
      err_log("recv packet out of order: %d current %d\n", packet, recv_last_packet_id[top_bot]);
    }
  }
#endif

  {
    uint32_t packet_time = iclock();
    uint32_t delay_from_last_packet = packet_time - recv_last_packet_time[work];
    if (delay_from_last_packet > recv_delay_between_packets[work]) {
      recv_delay_between_packets[work] = delay_from_last_packet;
    }
    recv_last_packet_time[work] = packet_time;
  }

  // err_log("%d %d %d %d (%d %d)\n", hdr[0], hdr[1], hdr[2], hdr[3], size, end);

  memcpy(&recv_buf[work][rp_packet_data_size * packet], buf, size);
  recv_track[work][packet] = 1;
  if (end) {
    recv_end[work] = 1;
    recv_end_packet[work] = packet;
    recv_end_size[work] = rp_packet_data_size * packet + size;
    // err_log("size %d\n", recv_end_size[work]);
  }

  if (recv_end[work] == 1) {
    for (int i = 0; i < recv_end_packet[work]; ++i) {
      if (!recv_track[work][i]) {
        if (!recv_end_incomp[work]) {
          recv_end_incomp[work] = 1;
#ifndef PRINT_PACKET_LOSS_INFO
          err_log("recv end packet incomplete\n");
#endif
        }
        return 0;
      }
    }

    recv_end[work] = 2;
    int top_bot = !recv_hdr[work][1];

    decode_info[work] = (struct DecodeInfo) {
      .top_bot = top_bot,
      .in_delay = recv_delay_between_packets[work],
      .in = recv_buf[work],
      .frame_id = recv_hdr[work][0],
      .in_size = recv_end_size[work],
    };
    if (queue_decode(work) != 0)
      return -1;
  }

  return 0;
}

SOCKET s;
struct sockaddr_in remoteAddr;
int received_from_remote;

static int kcp_udp_output(const char *buf, int len, ikcpcb *, void *)
{
  // if (len == sizeof(IUINT16)) {
  //   err_log("%x\n", (int)*(IUINT16 *)buf);
  // }
  // err_log("udp_output: %d\n", len);
  // if (len >= (int)sizeof(uint32_t)) {
  //   err_log("udp_output magic: %x\n", *(uint32_t *)buf);
  // }
  if (!received_from_remote)
    return 0;
  // err_log("remoteAddr: %d.%d.%d.%d:%d\n",
  //   (int)remoteAddr.sin_addr.S_un.S_un_b.s_b1,
  //   (int)remoteAddr.sin_addr.S_un.S_un_b.s_b2,
  //   (int)remoteAddr.sin_addr.S_un.S_un_b.s_b3,
  //   (int)remoteAddr.sin_addr.S_un.S_un_b.s_b4,
  //   (int)ntohs(remoteAddr.sin_port));
  return sendto(s, buf, len, 0, (struct sockaddr *)&remoteAddr, sizeof(remoteAddr));
}

#define rp_kcp_work_count (2)
#define RP_KCP_HDR_W_NBITS (1)
#define RP_KCP_HDR_T_NBITS (2)
#define RP_KCP_HDR_QUALITY_NBITS (7)
#define RP_KCP_HDR_SIZE_NBITS (11)
#define RP_KCP_HDR_RC_NBITS (5)

u8 kcp_recv_w[rp_kcp_work_count];

static struct KcpRecv {
  u8 buf[MAX_PACKET_COUNT][PACKET_SIZE - sizeof(IUINT16) - sizeof(u16)];
  u8 count; // packet count including term
  u16 term_size; // term packet size
} kcp_recv[rp_kcp_work_count][rp_work_count][rp_core_count_max];

static struct KcpRecvInfo {
  bool is_top;
  u16 jpeg_quality;
  u8 core_count;
  u8 v_adjusted;
  u8 v_last_adjusted;
  u16 term_sizes[rp_core_count_max];
  u8 term_count; // term count

  u8 last_term; // term being saved
  u16 last_term_size; // size saved so far
} kcp_recv_info[rp_kcp_work_count][rp_work_count];

static void init_kcp(ikcpcb *kcp) {
  kcp->output = kcp_udp_output;
  ikcp_setmtu(kcp, PACKET_SIZE);

  kcp_active = 0;
  restart_kcp = 0;

  memset(kcp_recv, 0, sizeof(kcp_recv));
  memset(kcp_recv_info, 0, sizeof(kcp_recv_info));
}

static int test_kcp_magic(int magic) {
  return !((magic & (~0x00001100 & 0x0000ff00)) == 0 && (magic & 0x00ff0000) == 0x00020000);
}

static void socket_action(int ret) {
  int ntr_is_kcp_test = 0;
  if (ret == (int)sizeof(uint16_t)) {
    ntr_is_kcp_test = 1;
  } else {
    if (ret < (int)sizeof(uint32_t))
    {
      return;
    }
    int magic = *(uint32_t *)buf;
    // err_log("magic: 0x%x\n", magic);
    ntr_is_kcp_test = test_kcp_magic(magic);
  }
  // err_log("recvfrom: %d\n", ret);
  if (ntr_is_kcp_test) {
    kcp_active = 1;
  }

  if (kcp_active)
  {
    if ((ret = ikcp_input(kcp, (const char *)buf, ret)) != 0)
    {
      restart_kcp = 1;
      if (kcp->input_cid == kcp_reset_cid) {
        ikcp_reset(kcp, kcp_reset_cid);
      } else if (kcp->should_reset) {
        err_log("ikcp_reset: %d\n", kcp->cid);
        ikcp_reset(kcp, kcp->cid);
        kcp_reset_cid = kcp->cid;
        kcp_cid = kcp->input_cid;
      } else {
        if (ret < 0) {
          err_log("ikcp_input failed: %d\n", ret);
        }
        ikcp_reset(kcp, kcp->cid);
        kcp_reset_cid = kcp->cid;
        kcp_cid = kcp->cid + 1;
      }
      return;
    }
    // Sleep(1);
    if (kcp->session_just_established) {
      kcp->session_just_established = false;
      if (!kcp->session_established) {
        err_log("kcp session_established\n");
        kcp->session_established = true;
      }
    }
  }
  else if (handle_recv(buf, ret) < 0)
  {
    return;
  }
}

static unsigned char jpeg_header_top_buffer_kcp[400 * 240 * 3 + 2048];
static unsigned char jpeg_header_bot_buffer_kcp[320 * 240 * 3 + 2048];
static unsigned char jpeg_header_empty_src_kcp[400 * 240 * 3];
static u16 jpeg_header_top_quality_kcp;
static u16 jpeg_header_bot_quality_kcp;
static int set_decode_quality_kcp(bool is_top, int quality, int rc)
{
  u16 *hdr_quality = is_top ? &jpeg_header_top_quality_kcp : &jpeg_header_bot_quality_kcp;

  // No need to check for rc as we change restart interval manually later
  if (*hdr_quality != quality) {
    tjhandle tjInst = tj3Init(TJINIT_COMPRESS);
    if (!tjInst) {
      return -1;
    }
    int ret = 0;

    ret = tj3Set(tjInst, TJPARAM_NOREALLOC, 1);
    if (ret < 0) {
      ret = ret * 0x10 - 2;
      goto final;
    }

    ret = tj3Set(tjInst, TJPARAM_RESTARTROWS, rc);
    if (ret < 0) {
      ret = ret * 0x10 - 5;
      goto final;
    }

    ret = tj3Set(tjInst, TJPARAM_QUALITY, quality);
    if (ret < 0) {
      ret = ret * 0x10 - 6;
      goto final;
    }

    ret = tj3Set(tjInst, TJPARAM_SUBSAMP, TJSAMP_420);
    if (ret < 0) {
      ret = ret * 0x10 - 7;
      goto final;
    }

    size_t size = is_top ? sizeof(jpeg_header_top_buffer_kcp) : sizeof(jpeg_header_bot_buffer_kcp);
    size_t buf_size = tj3JPEGBufSize(240, is_top ? 400 : 320, TJSAMP_420);
    if (size < buf_size) {
      err_log("buf size %d size %d\n", (int)buf_size, (int)size);
      ret = -3;
      goto final;
    }

    unsigned char *jpeg_buf = is_top ? jpeg_header_top_buffer_kcp : jpeg_header_bot_buffer_kcp;

    ret = tj3Compress8(tjInst, jpeg_header_empty_src_kcp, 240, 0, is_top ? 400 : 320, TJPF_RGB,
      &jpeg_buf,
      &size);

    if (ret < 0) {
      err_log("tj3Compress8 error (%d): %s\n", tj3GetErrorCode(tjInst), tj3GetErrorStr(tjInst));
      ret = ret * 0x10 - 4;
      goto final;
    }

    ret = 0;
    *hdr_quality = quality;

final:
    tj3Destroy(tjInst);
    return ret;
  }

  return 0;
}

static uint8_t *copy_with_escape(uint8_t *out, const uint8_t *in, int size)
{
  while (size) {
    if (*in == 0xff) {
      *out = 0xff;
      ++out;
      *out = 0;
      ++out;
      ++in;
    } else {
      *out = *in;
      ++out;
      ++in;
    }
    --size;
  }
  return out;
}

static unsigned char jpeg_buffer_kcp[400 * 240 * 3 + 2048];
static int handle_decode_kcp(uint8_t *out, int w, int queue_w)
{
  struct KcpRecv *recvs = kcp_recv[w][queue_w];
  struct KcpRecvInfo *info = &kcp_recv_info[w][queue_w];

  int ret;
  if ((ret = set_decode_quality_kcp(info->is_top, info->jpeg_quality, info->v_adjusted)) < 0) {
    return ret * 0x100 - 1;
  }

  unsigned char *jpeg_header = info->is_top ? jpeg_header_top_buffer_kcp : jpeg_header_bot_buffer_kcp;
  size_t jpeg_header_size_max = info->is_top ? sizeof(jpeg_header_top_buffer_kcp) : sizeof(jpeg_header_bot_buffer_kcp);
  size_t jpeg_header_size = 0;
  for (size_t i = 0; i < jpeg_header_size_max; ++i) {
    if (jpeg_header[i] == 0xff) {
      if (i + 1 < jpeg_header_size_max) {
        if (jpeg_header[i + 1] == 0xdd) {
          if (i + 6 >= jpeg_header_size_max) {
            return -6;
          }
          *(u16 *)&jpeg_header[i + 4] = htons(info->v_adjusted * (240 / (8 * 2)));
        } else if (jpeg_header[i + 1] == 0xda) {
          jpeg_header_size = i + 2;
          if (jpeg_header_size + 2 >= jpeg_header_size_max) {
            return -4;
          }
          jpeg_header_size += ntohs(*(u16 *)&jpeg_header[jpeg_header_size]);
          if (jpeg_header_size >= jpeg_header_size_max) {
            return -5;
          }
          break;
        }
      }
    }
  }
  if (jpeg_header_size == 0) {
    return -2;
  }

  memcpy(jpeg_buffer_kcp, jpeg_header, jpeg_header_size);
  unsigned char *ptr = jpeg_buffer_kcp + jpeg_header_size;
  for (int t = 0; t < info->core_count; ++t) {
    struct KcpRecv *recv = &recvs[t];
    for (int i = 0; i < recv->count; ++i) {
      if (i == recv->count - 1) {
        ptr = copy_with_escape(ptr, recv->buf[i], recv->term_size);
      } else {
        ptr = copy_with_escape(ptr, recv->buf[i], PACKET_SIZE - sizeof(IUINT16) - sizeof(u16));
      }
    }
    *ptr = 0xff;
    ++ptr;
    if (t == info->core_count - 1) {
      *ptr = 0xd9;
    } else {
      *ptr = 0xd0 + t;
    }
    ++ptr;
  }

  if (handle_decode(out, jpeg_buffer_kcp, ptr - jpeg_buffer_kcp, info->is_top ? 400 : 320, 240) != 0) {
    return -3;
  }

  memset(recvs, 0, sizeof(struct KcpRecv) * rp_core_count_max);
  memset(info, 0, sizeof(struct KcpRecvInfo));

  return 0;
}

static int queue_decode_kcp(int w, int queue_w) {
  if (acquire_decode() != 0) {
    return -1;
  }

  // err_log("recv_work %d\n", recv_work);

  struct DecodeInfo *ptr = &decode_info[recv_work];
  int top_bot = kcp_recv_info[w][queue_w].is_top ? 0 : 1;
  *ptr = (struct DecodeInfo) {
    .top_bot = top_bot,
    .kcp_w = w,
    .kcp_queue_w = queue_w,
    .is_kcp = true,
  };
  if (rp_syn_rel(&jpeg_decode_queue, ptr) != 0) {
    running = 0;
    return -1;
  }

  recv_work = (recv_work + 1) % rp_work_count;

  return 0;

}

static int handle_recv_kcp(uint8_t *buf, int size)
{
  if (size < (int)sizeof(u16)) {
    return -1;
  }
  u16 hdr = *(u16 *)buf;
  buf += sizeof(u16);
  size -= sizeof(u16);

  u16 w = (hdr >> (PID_NBITS + CID_NBITS)) & ((1 << RP_KCP_HDR_W_NBITS) - 1);
  u16 queue_w = kcp_recv_w[w];

  u16 t = (hdr >> (PID_NBITS + CID_NBITS + RP_KCP_HDR_W_NBITS)) & ((1 << RP_KCP_HDR_T_NBITS) - 1);

  if (t < rp_core_count_max) {
    if (kcp_recv_info[w][queue_w].term_count != 0) {
      err_log("%d %d %d %d %d %d\n", w, queue_w,
        (int)kcp_recv_info[w][queue_w].term_count,
        (int)kcp_recv_info[w][queue_w].last_term,
        (int)kcp_recv_info[w][queue_w].last_term_size,
        (int)kcp_recv_info[w][queue_w].term_sizes[kcp_recv_info[w][queue_w].last_term]);
      return -9;
    }
    if (size != PACKET_SIZE - sizeof(IUINT16) - sizeof(u16)) {
      return -2;
    }
    struct KcpRecv *recv = &kcp_recv[w][queue_w][t];
    if (recv->count < MAX_PACKET_COUNT) {
      memcpy(recv->buf[recv->count], buf, size);
      ++recv->count;
    } else {
      return -4;
    }
  } else { // t == rp_core_count_max
    // err_log("%d %d\n", w, queue_w);

    struct KcpRecvInfo *info = &kcp_recv_info[w][queue_w];
    if (info->term_count == 0) {
      if (size < (int)sizeof(u16)) {
        return -3;
      }
      hdr = *(u16 *)buf;
      buf += sizeof(u16);
      size -= sizeof(u16);

      u16 jpeg_quality = hdr & ((1 << RP_KCP_HDR_QUALITY_NBITS) - 1);
      u16 core_count = (hdr >> RP_KCP_HDR_QUALITY_NBITS) & ((1 << RP_KCP_HDR_T_NBITS) - 1);
      bool top_bot = (hdr >> (RP_KCP_HDR_QUALITY_NBITS + RP_KCP_HDR_T_NBITS)) & ((1 << 1) - 1);

      // err_log("w %d quality %d cores %d top %d\n", (int)w, (int)jpeg_quality, (int)core_count, (int)is_top);

      if (core_count == 0) {
        // ignore core_count == 0 for future extension
        return 0;
      }

      info->jpeg_quality = jpeg_quality;
      info->core_count = core_count;
      info->is_top = top_bot == 0;

      for (int t = 0; t < core_count; ++t) {
        if (size < (int)sizeof(u16)) {
          return -6;
        }
        hdr = *(u16 *)buf;
        buf += sizeof(u16);
        size -= sizeof(u16);

        u16 v_adjusted = (hdr >> RP_KCP_HDR_SIZE_NBITS) & ((1 << RP_KCP_HDR_RC_NBITS) - 1);
        u16 term_size = hdr & ((1 << RP_KCP_HDR_SIZE_NBITS) - 1);

        // err_log("t %d rc %d size %d\n", (int)t, (int)v_adjusted, (int)term_size);

        info->term_sizes[t] = term_size;
        if (t == core_count - 1) {
          if (core_count > 1 && v_adjusted > info->v_adjusted) {
            return -8;
          }
          info->v_last_adjusted = v_adjusted;
        } else if (t == 0) {
          info->v_adjusted = v_adjusted;
        } else if (info->v_adjusted != v_adjusted) {
          return -7;
        }
      }
    }

    while (1) {
      struct KcpRecv *recv = &kcp_recv[w][queue_w][info->last_term];
      u16 left_size = info->term_sizes[info->last_term] - info->last_term_size;
      // err_log("left size %d size %d last term %d last term size %d\n",
      //   (int)left_size, (int)size, (int)info->last_term, (int)info->last_term_size);
      if (left_size == 0) {
        ++recv->count;
        recv->term_size = info->last_term_size;;
        // err_log("%d\n", (int)recv->term_size);

        ++info->last_term;
        info->last_term_size = 0;

        if (info->last_term == info->core_count) {
          int ret = queue_decode_kcp(w, queue_w);
          if (ret < 0)
            return ret * 0x100 - 10;
          kcp_recv_w[w] = (kcp_recv_w[w] + 1) % rp_work_count;
          return 0;
        }

        continue;
      }

      if (!size)
        break;

      left_size = left_size <= size ? left_size : size;
      memcpy(recv->buf[recv->count] + info->last_term_size, buf, left_size);
      buf += left_size;
      size -= left_size;
      info->last_term_size += left_size;
    }

    ++info->term_count;
  }

  return 0;
}

static uint32_t reply_time;
void socket_reply(void) {
  if (kcp_active) {
    if (!kcp->session_just_established) {
      int ret;
      while ((ret = ikcp_recv(kcp, (char *)buf, sizeof(buf))) > 0)
      {
        // err_log("ikcp_recv: %d\n", ret);
        if ((ret = handle_recv_kcp(buf, ret)) != 0) {
          err_log("handle_recv_kcp failed: %d\n", ret);
          restart_kcp = 1;
          ikcp_reset(kcp, kcp->cid);
          kcp_reset_cid = kcp->cid;
          kcp_cid = kcp->cid + 1;
          return;
        }
      }
      if (ret < 0)
      {
        err_log("ikcp_recv failed: %d\n", ret);
        restart_kcp = 1;
        return;
      }
      bool reply = false;
      uint32_t current_time = iclock();
      if (kcp->recv_pid == kcp->input_pid) {
        // Send ack every 1/8 second or 125 ms; do not spam as that slows thing down considerably
        if (current_time - reply_time >= 125000) {
          reply = true;
        }
      } else {
        // Likewise nack every 1/80 second or 12.5 ms
        if (current_time - reply_time >= 12500) {
          reply = true;
        }
      }
      if (reply) {
        if ((ret = ikcp_reply(kcp)) < 0)
        {
          if (ret < -0x100) {
            if (sock_errno() == WSAEWOULDBLOCK) {
              return;
            }
          }
          err_log("ikcp_reply failed: %d\n", ret);
          restart_kcp = 1;
          return;
        } else {
          reply_time = current_time;
        }
      }
    }
  }
}

static void receive_from_socket(SOCKET s)
{
  while (running && !restart_kcp)
  {
    socklen_t nAddrLen = sizeof(remoteAddr);

    int ret = recvfrom(s, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&remoteAddr, &nAddrLen);
    if (
      ret == 0
      // || (rand() & 0xf) == 0
    )
    {
      continue;
    }
    else if (ret < 0)
    {
      int err = sock_errno();
      if (err != WSAETIMEDOUT && err != WSAEWOULDBLOCK)
      {
        // err_log("recvfrom failed: %d\n", err);
        Sleep(RP_SOCKET_INTERVAL);
        return;
      }
      else if (err == WSAEWOULDBLOCK)
      {
        socket_reply();
        if (!socket_poll(s)) {
          if (running)
            err_log("socket poll failed: %d\n", sock_errno());
          return;
        }
      }
      continue;
    }

#ifdef _WIN32
    if (ip_octets[0] == 0 &&
      ip_octets[1] == 0 &&
      ip_octets[2] == 0 &&
      ip_octets[3] == 0)
    {
      ip_octets[0] = remoteAddr.sin_addr.S_un.S_un_b.s_b1;
      ip_octets[1] = remoteAddr.sin_addr.S_un.S_un_b.s_b2;
      ip_octets[2] = remoteAddr.sin_addr.S_un.S_un_b.s_b3;
      ip_octets[3] = remoteAddr.sin_addr.S_un.S_un_b.s_b4;
    }
#endif

    received_from_remote = 1;

    socket_action(ret);
  }
}

static uint8_t last_decoded_frame_id[SCREEN_COUNT];
static thread_ret_t jpeg_decode_thread_func(void *e)
{
  while (running && !restart_kcp) {
    struct DecodeInfo *ptr;
    while (1) {
      if (!(running && !restart_kcp))
        return 0;
      thread_set_cancel_state(true);
      int res = rp_syn_acq(&jpeg_decode_queue, NWM_THREAD_WAIT_NS, (void **)&ptr, e);
      thread_set_cancel_state(false);
      if (res == 0)
        break;
      if (res != ETIMEDOUT) {
        err_log("rp_syn_acq failed\n");
        running = 0;
        return 0;
      }
    }

    int top_bot = ptr->top_bot;
    FrameBufferContext *ctx = &buffer_ctx[top_bot];
    int index = ctx->index_decode;
    uint8_t *out = ctx->screen_decoded[index];

    view_mode_t vm = __atomic_load_n(&view_mode, __ATOMIC_RELAXED);
    FrameBufferContext *ctx_sync = vm == VIEW_MODE_TOP_BOT ? &buffer_ctx[SCREEN_TOP] : NULL;

    int ret;
    if (ptr->is_kcp) {
      // err_log("%d %d\n", ptr->kcp_w, ptr->kcp_queue_w);
      if ((ret = handle_decode_kcp(out, ptr->kcp_w, ptr->kcp_queue_w)) != 0) {
        err_log("kcp recv decode error: %d\n", ret);
        restart_kcp = 1;

        // ikcp_reset(kcp, kcp->cid);
        kcp_reset_cid = kcp->cid;
        kcp_cid = kcp->cid + 1;
      } else {
        // err_log("%d\n", kcp_recv_info[ptr->kcp_w][ptr->kcp_queue_w].term_count);
        handle_decode_frame_screen(ctx, top_bot, ptr->in_size, ptr->in_delay, ctx_sync);
      }
    } else {
      if (ptr->in) {
        if (handle_decode(out, ptr->in, ptr->in_size, top_bot == 0 ? 400 : 320, 240) != 0) {
          err_log("recv decode error\n");
          __atomic_add_fetch(&frame_lost_tracker, 1, __ATOMIC_RELAXED);
        } else {
          handle_decode_frame_screen(ctx, top_bot, ptr->in_size, ptr->in_delay, ctx_sync);
          __atomic_add_fetch(&frame_fully_received_tracker, 1, __ATOMIC_RELAXED);
        }
      } else {
        __atomic_add_fetch(&frame_lost_tracker, (uint8_t)(ptr->frame_id - last_decoded_frame_id[top_bot]), __ATOMIC_RELAXED);
      }
      last_decoded_frame_id[top_bot] = ptr->frame_id;
    }

    rp_sem_rel(jpeg_decode_sem);
  }
  return 0;
}

static int jpeg_decode_sem_inited;
static int jpeg_decode_queue_inited;
void receive_from_socket_loop(SOCKET s)
{
  while (running && !ntr_rp_port_changed) {
    kcp = ikcp_create(kcp_cid, 0);
    if (!kcp) {
      err_log("ikcp_create failed\n");
      Sleep(RP_SOCKET_INTERVAL);
      continue;
    }
    init_kcp(kcp);

    received_from_remote = 0;

    for (int i = 0; i < SCREEN_COUNT; ++i) {
      recv_has_last_frame_id[i] = 0;
      recv_last_frame_id[i] = 0;
      recv_last_packet_id[i] = 0;
    }

    // err_log("new connection\n");
    for (int top_bot = 0; top_bot < SCREEN_COUNT; ++top_bot) {
      rp_lock_wait(buffer_ctx[top_bot].status_lock);
      buffer_ctx[top_bot].status = FBS_NOT_AVAIL;
      rp_lock_rel(buffer_ctx[top_bot].status_lock);
    }
    for (int i = 0; i < rp_work_count; ++i) {
      recv_end[i] = 2;
    }
    memset(recv_hdr, 0, sizeof(recv_hdr));
    // recv_work = 0;

    memset(frame_rate_decoded_tracker, 0, sizeof(frame_rate_decoded_tracker));
    memset(frame_rate_displayed_tracker, 0, sizeof(frame_rate_displayed_tracker));
    memset(frame_size_tracker, 0, sizeof(frame_size_tracker));
    memset(delay_between_packet_tracker, 0, sizeof(delay_between_packet_tracker));

    if (jpeg_decode_sem_inited) {
      if (rp_sem_close(jpeg_decode_sem) != 0) {
        err_log("jpeg_decode_sem close failed\n");
        break;
      }
      jpeg_decode_sem_inited = 0;
    }
    if (rp_sem_create(jpeg_decode_sem, rp_work_count, rp_work_count) != 0) {
      err_log("jpeg_decode_sem init failed\n");
      break;
    }
    jpeg_decode_sem_inited = 1;

    if (jpeg_decode_queue_inited) {
      if (rp_syn_close1(&jpeg_decode_queue)) {
        err_log("jpeg_decode_queue close failed\n");
        break;
      }
      jpeg_decode_queue_inited = 0;
    }
    if (rp_syn_init1(&jpeg_decode_queue, 0, 0, 0, rp_work_count, (void **)decode_ptr) != 0) {
      err_log("jpeg_decode_queue init failed\n");
      break;
    }
    jpeg_decode_queue_inited = 1;

    memset(decode_ptr, 0, sizeof(decode_ptr));
    memset(decode_info, 0, sizeof(decode_info));

    thread_t jpeg_decode_thread;
    int ret;
#ifdef _WIN32
    HANDLE jpeg_decode_thread_e = CreateEventA(NULL, FALSE, FALSE, NULL);
#else
    void *jpeg_decode_thread_e = NULL;
#endif
    if ((ret = thread_create(jpeg_decode_thread, jpeg_decode_thread_func, jpeg_decode_thread_e)))
    {
      err_log("jpeg_decode_thread create failed\n");
      break;
    }

    receive_from_socket(s);
    // Sleep(RP_SOCKET_INTERVAL);

#ifdef _WIN32
    thread_set_cancel(jpeg_decode_thread_e);
    thread_join(jpeg_decode_thread);
    CloseHandle(jpeg_decode_thread_e);
#else
    thread_cancel(jpeg_decode_thread);
    thread_join(jpeg_decode_thread);
#endif

    if (kcp) {
      ikcp_release(kcp);
      kcp = 0;
    }
  }
}

static void socket_error_pause(void) {
  Sleep(RP_SOCKET_INTERVAL);
}

static thread_ret_t udp_recv_thread_func(void *)
{
  while (running)
  {
    s = 0;
    int ret;
    if (!SOCKET_VALID(s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)))
    {
      err_log("socket creation failed\n");
      // running = 0;
      socket_error_pause();
      continue;
    }

    ntr_rp_bound_port = ntr_rp_port;
    struct sockaddr_in si_other;
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(ntr_rp_bound_port);
    si_other.sin_addr.s_addr = *(uint32_t *)adaptorIPsOctets[selectedAdaptor];

    if (bind(s, (struct sockaddr *)&si_other, sizeof(si_other)) == SOCKET_ERROR)
    {
      err_log("socket bind failed for port %d\n", ntr_rp_bound_port);
      // running = 0;
      socket_error_pause();
      goto final_socket;
    }
    uint8_t *octets = adaptorIPsOctets[selectedAdaptor];
    err_log("port bound at %d.%d.%d.%d:%d\n", (int)octets[0], (int)octets[1], (int)octets[2], (int)octets[3], ntr_rp_bound_port);
    ntr_rp_port_changed = 0;
    ntr_rp_port = ntr_rp_bound_port;

    int buff_size = 6 * 1024 * 1024;
    socklen_t tmp = sizeof(buff_size);

    ret = setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)(&buff_size), sizeof(buff_size));
    buff_size = 0;
    ret = getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)(&buff_size), &tmp);
    if (ret)
    {
      err_log("setsockopt buf size failed\n");
      // running = 0;
      socket_error_pause();
      goto final_socket;
    }

#ifdef _WIN32
    DWORD timeout = RP_SOCKET_INTERVAL;
#else
    struct timeval timeout;
    timeout.tv_sec = RP_SOCKET_INTERVAL / 1000;
    timeout.tv_usec = (RP_SOCKET_INTERVAL % 1000) * 1000;
#endif
    ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    if (ret)
    {
      err_log("setsockopt timeout failed\n");
      // running = 0;
      socket_error_pause();
      goto final_socket;
    }

    if (!socket_set_nonblock(s, 1)) {
      err_log("socket_set_nonblock failed, %d\n", sock_errno());
      socket_error_pause();
      goto final_socket;
    }

    receive_from_socket_loop(s);

final_socket:
    closesocket(s);
  }

  running = 0;

  return 0;
}

#include "style.h"

#ifdef GL_DEBUG
static void on_gl_error(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar *message, const void *)
{
  err_log("gl_error: %u:%u:%u:%u:%u: %s\n", source, type, id, severity, length, message);
}
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif
  /* GUI */
  int ret;

  NK_UNUSED(argc);
  NK_UNUSED(argv);

  /* SDL setup */
  SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#ifdef USE_SDL_RENDERER
  SDL_Init(SDL_INIT_VIDEO);
#else
#ifdef USE_ANGLE
  SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
#else
  SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "0");
#endif
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS);
#ifdef USE_ANGLE
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_EGL, 1);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_EGL, 0);
#endif
#ifdef USE_OGL_ES
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  // SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  // SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
#ifdef GL_DEBUG
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#endif

  Uint32 win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
#ifndef USE_SDL_RENDERER
  win_flags |= SDL_WINDOW_OPENGL;
#endif

  win[SCREEN_TOP] = SDL_CreateWindow(TITLE,
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         WINDOW_WIDTH, WINDOW_HEIGHT, win_flags);
  if (!win[SCREEN_TOP])
  {
    err_log("SDL_CreateWindow: %s\n", SDL_GetError());
    return -1;
  }
  win_id[SCREEN_TOP] = SDL_GetWindowID(win[SCREEN_TOP]);

  win[SCREEN_BOT] = SDL_CreateWindow(TITLE,
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         WINDOW_WIDTH, WINDOW_HEIGHT, win_flags | SDL_WINDOW_HIDDEN);
  if (!win[SCREEN_BOT])
  {
    err_log("SDL_CreateWindow: %s\n", SDL_GetError());
    return -1;
  }
  win_id[SCREEN_BOT] = SDL_GetWindowID(win[SCREEN_BOT]);

#ifdef _WIN32
  SDL_SysWMinfo wmInfo[SCREEN_COUNT];
  HWND hwnd[SCREEN_COUNT];

  SDL_VERSION(&wmInfo[SCREEN_TOP].version);
  SDL_GetWindowWMInfo(win[SCREEN_TOP], &wmInfo[SCREEN_TOP]);
  hwnd[SCREEN_TOP] = wmInfo[SCREEN_TOP].info.win.window;

  SDL_VERSION(&wmInfo[SCREEN_BOT].version);
  SDL_GetWindowWMInfo(win[SCREEN_BOT], &wmInfo[SCREEN_BOT]);
  hwnd[SCREEN_BOT] = wmInfo[SCREEN_BOT].info.win.window;
#endif

#ifdef USE_SDL_RENDERER
  Uint32 renderer_flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC;
  sdlRenderer[SCREEN_TOP] = SDL_CreateRenderer(win[SCREEN_TOP], -1, renderer_flags);
  if (!sdlRenderer[SCREEN_TOP])
  {
    err_log("SDL_CreateRenderer: %s\n", SDL_GetError());
    return -1;
  }

  sdlRenderer[SCREEN_BOT] = SDL_CreateRenderer(win[SCREEN_BOT], -1, renderer_flags);
  if (!sdlRenderer[SCREEN_BOT])
  {
    err_log("SDL_CreateRenderer: %s\n", SDL_GetError());
    return -1;
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  for (int j = 0; j < SCREEN_COUNT; ++j) {
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      sdlTexture[j][i] = SDL_CreateTexture(sdlRenderer[j], SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, i == SCREEN_TOP ? 400 : 320, 240);
      if (!sdlTexture[j][i]) {
        err_log("SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
      }
    }
  }

  struct nk_context *ctx = nk_ctx = nk_sdl_init(win[SCREEN_TOP], sdlRenderer[SCREEN_TOP]);
#else
  glContext[SCREEN_TOP] = SDL_GL_CreateContext(win[SCREEN_TOP]);
  if (!glContext[SCREEN_TOP])
  {
    err_log("SDL_GL_CreateContext: %s\n", SDL_GetError());
    return -1;
  }
  SDL_GL_SetSwapInterval(1);

#ifdef USE_OGL_ES
  if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress))
  {
    err_log("gladLoadGLES2 failed\n");
    return -1;
  }
#else
  if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
  {
    err_log("gladLoadGLLoader failed\n");
    return -1;
  }
#endif

#ifdef GL_DEBUG
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(on_gl_error, NULL);
#endif

  err_log("ogl version string: %s\n", glGetString(GL_VERSION));
#if 0
#ifdef USE_OGL_ES
  if (sscanf((const char *)glGetString(GL_VERSION), "OpenGL ES %d.%d", &ogl_version_major, &ogl_version_minor) != 2) {
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &ogl_version_major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &ogl_version_minor);
  }
#else
  if (sscanf((const char *)glGetString(GL_VERSION), "%d.%d", &ogl_version_major, &ogl_version_minor) != 2) {
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &ogl_version_major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &ogl_version_minor);
  }
#endif
#endif
  glGetIntegerv(GL_MAJOR_VERSION, &ogl_version_major);
  glGetIntegerv(GL_MINOR_VERSION, &ogl_version_minor);
  err_log("ogl version: %d.%d\n", ogl_version_major, ogl_version_minor);

#ifdef SDL_GL_SINGLE_THREAD
  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);]
#endif
  glContext[SCREEN_BOT] = SDL_GL_CreateContext(win[SCREEN_BOT]);
  if (!glContext[SCREEN_BOT])
  {
    err_log("SDL_GL_CreateContext: %s\n", SDL_GetError());
    return -1;
  }
  SDL_GL_SetSwapInterval(1);

  if (upscaling_filter) {
    if (sr_create() < 0)
      upscaling_filter = 0;
    else
      upscaling_filter_created = 1;
  }

  /* OpenGL setup */
  SDL_GL_MakeCurrent(win[SCREEN_TOP], glContext[SCREEN_TOP]);
  glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

  struct nk_context *ctx = nk_ctx = nk_sdl_init(win[SCREEN_TOP]);
#endif


  /* Load Fonts: if none of these are loaded a default font will be used  */
  /* Load Cursor: if you uncomment cursor loading please hide the cursor */
  {
    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);
    nk_sdl_font_stash_end();
    /*nk_style_load_all_cursors(ctx, atlas->cursors);*/
    /*nk_style_set_font(ctx, &roboto->handle)*/;
  }

  /* style.c */
  // set_style(ctx, THEME_WHITE);
  // set_style(ctx, THEME_RED);
  // set_style(ctx, THEME_BLUE);
  set_style(ctx, THEME_DARK);
  nk_style_current = ctx->style;

#ifdef _WIN32
  HBRUSH brush = CreateSolidBrush(
      RGB(nk_window_bgcolor.r, nk_window_bgcolor.g, nk_window_bgcolor.b));
  SetClassLongPtr(hwnd[SCREEN_TOP], GCLP_HBRBACKGROUND, (LONG_PTR)brush);
  SetClassLongPtr(hwnd[SCREEN_BOT], GCLP_HBRBACKGROUND, (LONG_PTR)brush);
#endif

  sock_startup();

#ifndef USE_SDL_RENDERER
  for (int j = 0; j < SCREEN_COUNT; ++j) {
    SDL_GL_MakeCurrent(win[j], glContext[j]);
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      glGenTextures(1, &buffer_ctx[i].gl_tex_id[j]);

      glGenTextures(1, &buffer_ctx[i].gl_tex_upscaled[j]);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, buffer_ctx[i].gl_tex_upscaled[j]);
      glTexImage2D(
        GL_TEXTURE_2D, 0,
        GL_INT_FORMAT, 240 * screen_upscale_factor,
        (i == 0 ? 400 : 320) * screen_upscale_factor, 0,
        GL_FORMAT, GL_UNSIGNED_BYTE,
        0);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindTexture(GL_TEXTURE_2D, 0);
    }
  }

  for (int j = 0; j < SCREEN_COUNT; ++j) {
    SDL_GL_MakeCurrent(win[j], glContext[j]);
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      glGenFramebuffers(1, &buffer_ctx[i].gl_fbo_upscaled[j]);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buffer_ctx[i].gl_fbo_upscaled[j]);
      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buffer_ctx[i].gl_tex_upscaled[j], 0);
      GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
      glDrawBuffers(1, &draw_buffer);
      if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        err_log("fbo init error\n");
        return -1;
      }
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }
  }

  for (int j = 0; j < SCREEN_COUNT; ++j) {
    SDL_GL_MakeCurrent(win[j], glContext[j]);

    gl_program[j] = LoadProgram((const char *)vShaderStr, (const char *)fShaderStr);
    gl_position_loc[j] = glGetAttribLocation(gl_program[j], "a_position");
    gl_tex_coord_loc[j] = glGetAttribLocation(gl_program[j], "a_texCoord");
    gl_sampler_loc[j] = glGetUniformLocation(gl_program[j], "s_texture");

    if (1) {
      gl_fbo_program[j] = LoadProgram((const char *)fbo_vShaderStr, (const char *)fbo_fShaderStr);
      gl_fbo_position_loc[j] = glGetAttribLocation(gl_fbo_program[j], "a_position");
      gl_fbo_tex_coord_loc[j] = glGetAttribLocation(gl_fbo_program[j], "a_texCoord");
      gl_fbo_sampler_loc[j] = glGetUniformLocation(gl_fbo_program[j], "s_texture");
    }

#ifdef USE_VAO
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      glGenVertexArrays(1, &glVao[j][i]);
      glGenBuffers(1, &glVbo[j][i]);
    }
    glGenBuffers(1, &glEbo[j]);

    glGenVertexArrays(1, &glFboVao[j]);
    glGenBuffers(1, &glFboVbo[j]);
    glGenBuffers(1, &glFboEbo[j]);

    for (int i = 0; i < SCREEN_COUNT; ++i) {
      glBindVertexArray(glVao[j][i]);
      glBindBuffer(GL_ARRAY_BUFFER, glVbo[j][i]);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glEbo[j]);
      if (i == SCREEN_TOP)
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
      glEnableVertexAttribArray(gl_position_loc[j]);
      glEnableVertexAttribArray(gl_tex_coord_loc[j]);
      glVertexAttribPointer(gl_position_loc[j], 3, GL_FLOAT, GL_FALSE, sizeof(struct vao_vertice_t), (const void *)offsetof(struct vao_vertice_t, pos));
      glVertexAttribPointer(gl_tex_coord_loc[j], 2, GL_FLOAT, GL_FALSE, sizeof(struct vao_vertice_t), (const void *)offsetof(struct vao_vertice_t, tex_coord));
    }

    glBindVertexArray(glFboVao[j]);
    glBindBuffer(GL_ARRAY_BUFFER, glFboVbo[j]);
    struct vao_vertice_t fbo_vertices[4];
    for (int i = 0; i < 4; ++i) {
      memcpy(fbo_vertices[i].pos, fbo_vVertices_pos[i], sizeof(fbo_vertices[i].pos));
      memcpy(fbo_vertices[i].tex_coord, fbo_vVertices_tex_coord[i], sizeof(fbo_vertices[i].tex_coord));
    }
    glBufferData(GL_ARRAY_BUFFER, sizeof(fbo_vertices), fbo_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, glFboEbo[j]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(fbo_indices), fbo_indices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(gl_fbo_position_loc[j]);
    glEnableVertexAttribArray(gl_fbo_tex_coord_loc[j]);
    glVertexAttribPointer(gl_fbo_position_loc[j], 3, GL_FLOAT, GL_FALSE, sizeof(struct vao_vertice_t), (const void *)offsetof(struct vao_vertice_t, pos));
    glVertexAttribPointer(gl_fbo_tex_coord_loc[j], 2, GL_FLOAT, GL_FALSE, sizeof(struct vao_vertice_t), (const void *)offsetof(struct vao_vertice_t, tex_coord));
#endif
  }
#endif

  for (int i = 0; i < SCREEN_COUNT; ++i) {
    rp_lock_init(buffer_ctx[i].status_lock);
    buffer_ctx[i].index_display_2 = FBI_DISPLAY_2;
    buffer_ctx[i].index_display = FBI_DISPLAY;
    buffer_ctx[i].index_ready_display_2 = FBI_READY_DISPLAY_2;
    buffer_ctx[i].index_ready_display = FBI_READY_DISPLAY;
    buffer_ctx[i].index_decode = FBI_DECODE;

#ifndef SDL_GL_SINGLE_THREAD
    rp_cond_init(buffer_ctx[i].decode_updated_cond);
    rp_lock_init(buffer_ctx[i].decode_updated_mutex);
#endif
  }

#ifdef SDL_GL_SINGLE_THREAD
  rp_cond_init(decode_updated_cond);
  rp_lock_init(decode_updated_mutex);
#else
  rp_lock_init(nk_input_lock);
#endif

#ifdef SDL_GL_SYNC
  rp_cond_init(gl_swapbuffer_cond);
  rp_lock_init(gl_swapbuffer_mutex);
  rp_cond_init(gl_render_cond);
  rp_lock_init(gl_render_mutex);
#endif

  rpConfigSetDefault();
  detect3DSIP();
  getAdapterIPs();
  tryAutoSelectAdapterIP();

  thread_t udp_recv_thread;
  if ((ret = thread_create(udp_recv_thread, udp_recv_thread_func, NULL)))
  {
    err_log("udp_recv_thread create failed\n");
    return -1;
  }
  thread_t menu_tcp_thread;
  struct tcp_thread_arg menu_tcp_thread_arg = {
    &menu_work_state,
    &menu_remote_play,
    8000,
  };
  if ((ret = thread_create(menu_tcp_thread, tcp_thread_func, &menu_tcp_thread_arg)))
  {
    err_log("menu_tcp_thread create failed\n");
    return -1;
  }
  thread_t nwm_tcp_thread;
  struct tcp_thread_arg nwm_tcp_thread_arg = {
    &nwm_work_state,
    NULL,
    5000 + 0x1a,
  };
  if ((ret = thread_create(nwm_tcp_thread, tcp_thread_func, &nwm_tcp_thread_arg)))
  {
    err_log("nwm_tcp_thread create failed\n");
    return -1;
  }

#ifdef _WIN32
  SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED | ES_DISPLAY_REQUIRED);
#endif

#ifndef SDL_GL_SINGLE_THREAD
  SDL_GL_MakeCurrent(NULL, NULL);
  thread_t window_top_thread;
  if ((ret = thread_create(window_top_thread, window_thread_func, (void *)SCREEN_TOP)))
  {
    err_log("window_top_thread create failed\n");
    return -1;
  }
  thread_t window_bot_thread;
  if ((ret = thread_create(window_bot_thread, window_thread_func, (void *)SCREEN_BOT)))
  {
    err_log("window_bot_thread create failed\n");
    return -1;
  }
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
  emscripten_set_main_loop_arg(MainLoop, (void *)ctx, 0, nk_true);
#else
  while (running)
    MainLoop((void *)ctx);
#endif

#ifdef _WIN32
  SetThreadExecutionState(ES_CONTINUOUS);
#endif

  // Apparently cancelling a thread that has OpenGL stuff causes hangs, so let them exit on their own.

#ifndef SDL_GL_SINGLE_THREAD
  // thread_cancel(window_bot_thread);
  thread_join(window_bot_thread);
  // thread_cancel(window_top_thread);
  thread_join(window_top_thread);
#endif

  thread_cancel(udp_recv_thread);
  thread_join(udp_recv_thread);
  thread_cancel(menu_tcp_thread);
  thread_join(menu_tcp_thread);
  thread_cancel(nwm_tcp_thread);
  thread_join(nwm_tcp_thread);

  rp_syn_close1(&jpeg_decode_queue);
  rp_sem_close(jpeg_decode_sem);

  for (int i = 0; i < SCREEN_COUNT; ++i) {
    rp_lock_close(buffer_ctx[i].status_lock);
  }

#ifdef USE_SDL_RENDERER
  for (int j = 0; j < SCREEN_COUNT; ++j) {
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      SDL_DestroyTexture(sdlTexture[j][i]);
    }
  }
#else
  for (int j = 0; j < SCREEN_COUNT; ++j) {
    SDL_GL_MakeCurrent(win[j], glContext[j]);
#ifdef USE_VAO
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      glDeleteBuffers(1, &glVbo[j][i]);
    }
    glDeleteBuffers(1, &glEbo[j]);
    glDeleteBuffers(1, &glFboVbo[j]);
    glDeleteBuffers(1, &glFboEbo[j]);
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      glDeleteVertexArrays(1, &glVao[j][i]);
    }
    glDeleteVertexArrays(1, &glFboVao[j]);
#endif
    glDeleteProgram(gl_fbo_program[j]);
    glDeleteProgram(gl_program[j]);
  }

  for (int j = 0; j < SCREEN_COUNT; ++j) {
    SDL_GL_MakeCurrent(win[j], glContext[j]);
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      glDeleteTextures(1, &buffer_ctx[i].gl_tex_id[j]);
      glDeleteFramebuffers(1, &buffer_ctx[i].gl_fbo_upscaled[j]);
      glDeleteTextures(1, &buffer_ctx[i].gl_tex_upscaled[j]);
    }
  }
#endif

  sock_cleanup();
  nk_sdl_shutdown();

#ifdef USE_SDL_RENDERER
  SDL_DestroyRenderer(sdlRenderer[SCREEN_TOP]);
  SDL_DestroyRenderer(sdlRenderer[SCREEN_BOT]);
#else
  if (upscaling_filter_created)
    sr_destroy();
  SDL_GL_DeleteContext(glContext[SCREEN_BOT]);
  SDL_GL_DeleteContext(glContext[SCREEN_TOP]);
#endif
  SDL_DestroyWindow(win[SCREEN_BOT]);
  SDL_DestroyWindow(win[SCREEN_TOP]);
  SDL_Quit();

  return 0;
}
