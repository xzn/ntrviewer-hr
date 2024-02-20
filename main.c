#if 0
#define screen_upscale_factor (4)
#define sr_create realsr_create
#define sr_run realsr_run
#define sr_destroy realsr_destroy
#elif 0
#define screen_upscale_factor (2)
#define sr_create srmd_create
#define sr_run srmd_run
#define sr_destroy srmd_destroy
#elif 1
#define screen_upscale_factor (2)
#define sr_create realcugan_create
#define sr_run realcugan_run
#define sr_destroy realcugan_destroy
#else
#define screen_upscale_factor (1)
#define sr_create() (0)
#define sr_run()
#define sr_destroy()
#endif

#define HR_MAX(a, b) ((a) > (b) ? (a) : (b))
#define HR_MIN(a, b) ((a) < (b) ? (a) : (b))

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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
typedef int SOCKET;
#define SOCKET_VALID(s) ((int)(s) >= 0)
#define sock_errno() errno
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAETIMEDOUT ETIMEDOUT
#define SOCKET_ERROR SO_ERROR
#define INVALID_SOCKET (-1)
#define closesocket close

void Sleep(int milliseconds) {
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  nanosleep(&ts, NULL);
}

#endif

#include <stdatomic.h>
#include <pthread.h>
#include "main.h"
// #define GL_DEBUG
// #define USE_ANGLE
// #define USE_OGL_ES

#define GL_CHANNELS_N 4
#define GL_FORMAT GL_RGBA
#define GL_INT_FORMAT GL_RGBA8
#define TJ_FORMAT TJPF_RGBA
#define JCS_FORMAT JCS_EXT_RGBA

int realcugan_create();
GLuint realcugan_run(int top_bot, int w, int h, int c, const unsigned char *indata);
void realcugan_destroy();

#define fprintf(s, f, ...) fprintf(s, "%s:%d:%s " f, __FILE__, __LINE__, __func__, ## __VA_ARGS__)

#ifdef EMBED_JPEG_TURBO
#include "jpeg_turbo/jpeglib.h"
#else
#include <turbojpeg.h>
#endif

#define RP_MAX(a,b) ((a) > (b) ? (a) : (b))
#define RP_MIN(a,b) ((a) > (b) ? (b) : (a))

#define RP_SOCKET_TIMEOUT (2000)

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
  if (status != 0)
  {
    fprintf(stderr, "socket shudown failed: %d\n", sock_errno());
  }
  status = closesocket(sock);
#else
  status = shutdown(sock, SHUT_RDWR);
  if (status != 0)
  {
    fprintf(stderr, "socket shudown failed: %d\n", sock_errno());
  }
  status = close(sock);
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
  static int64_t freq = 1;
  int64_t qpc;
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

static inline int64_t iclock64(void)
{
  int64_t s, u;
  int64_t value;
  itimeofday(&s, &u);
  value = ((int64_t)s) * 1000 + (u / 1000);
  return value;
}

static inline uint32_t iclock()
{
  return (uint32_t)(iclock64() & 0xfffffffful);
}

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

#define FRAME_STAT_EVERY_X_FRAMES 24
char window_title_with_fps[500];
struct {
  int display;
  int counter;
  uint64_t last_tick;
} frame_rate_tracker[SCREEN_COUNT] = {};
struct {
  int index;
  int counter[FRAME_STAT_EVERY_X_FRAMES];
  int total;
} frame_size_tracker[SCREEN_COUNT] = {};
struct {
  int index;
  uint32_t counter[FRAME_STAT_EVERY_X_FRAMES];
} delay_between_packet_tracker[SCREEN_COUNT] = {};

/* Platform */
SDL_Window *win[2];
SDL_GLContext glContext[2];
int running = nk_true;
int win_width[2], win_height[2];

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

static nk_bool ntr_rp_priority;
static int ntr_rp_priority_factor;
static int ntr_rp_quality;
static int ntr_rp_qos;

static int ntr_rp_port = 8001;
static int ntr_rp_port_changed;
static int ntr_rp_bound_port;

static nk_bool upscaling_filter;
static nk_bool upscaling_filter_created;
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
#define REST_EVERY_MS 100

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

#ifdef _WIN32
  u_long opt = 1;
  if (ioctlsocket(sockfd, FIONBIO, &opt)) {
    fprintf(stderr, "ioctlsocket FIONBIO failed: %d\n", sock_errno());
    return INVALID_SOCKET;
  }
#else
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1) return INVALID_SOCKET;
  flags = flags | O_NONBLOCK;
  if (fcntl(sockfd, F_SETFL, flags) != 0) return INVALID_SOCKET;
#endif


  servaddr.sin_family = AF_INET;
  snprintf(ip_addr_buf, sizeof(ip_addr_buf),
           "%d.%d.%d.%d",
           (int)ip_octets[0],
           (int)ip_octets[1],
           (int)ip_octets[2],
           (int)ip_octets[3]);
  servaddr.sin_addr.s_addr = inet_addr(ip_addr_buf);
  servaddr.sin_port = htons(port);

  fprintf(stderr, "connecting to %s:%d ...\n", ip_addr_buf, port);
  int ret = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
  if (ret != 0 && sock_errno() != WSAEWOULDBLOCK && sock_errno() != EINPROGRESS)
  {
    fprintf(stderr, "connection failed: %d\n", sock_errno());
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
      fprintf(stderr, "connected\n");
      return sockfd;
    }
    fprintf(stderr, "connection failed: %d\n", so_error);
  }

  closesocket(sockfd);
  fprintf(stderr, "connection timeout\n");
  return INVALID_SOCKET;
}

#define RESET_SOCKET(ts, ws) \
  sock_close(sockfd);        \
  sockfd = INVALID_SOCKET;   \
  ts = 0;                    \
  ws = CS_DISCONNECTED;      \
  fprintf(stderr, "disconnected\n");

void *menu_tcp_thread_func(void *)
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
          // fprintf(stderr, "heartbeat packet: size %d\n", header.data_len);
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
        else if (header.data_len)
        {
          fprintf(stderr, "unhandled packet type %d: size %d\n", header.cmd, header.data_len);
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

        uint32_t args[] = {
          (ntr_rp_priority << 8) | ntr_rp_priority_factor, ntr_rp_quality, ntr_rp_qos * 128 * 1024,
          1404036572 /* guarding magic */, ntr_rp_bound_port
        };

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

void *nwm_tcp_thread_func(void *)
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
          // fprintf(stderr, "heartbeat packet: size %d\n", header.data_len);
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
        else if (header.data_len)
        {
          fprintf(stderr, "unhandled packet type %d: size %d\n", header.cmd, header.data_len);
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
  ntr_rp_priority = 1;
  ntr_rp_priority_factor = 3;
  ntr_rp_quality = 75;
  ntr_rp_qos = 16;
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
  // fprintf(stderr, "%02x-%02x-%02x\n", (int)mac[0], (int)mac[1], (int)mac[2]);
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

static void updateViewMode(void) {
  switch (view_mode) {
    case VIEW_MODE_TOP_BOT:
    case VIEW_MODE_TOP:
    case VIEW_MODE_BOT:
      SDL_HideWindow(win[1]);
      break;

    case VIEW_MODE_SEPARATE:
      SDL_ShowWindow(win[1]);
      break;
  }

  SDL_SetWindowFullscreen(win[0], 0);
  SDL_SetWindowFullscreen(win[1], 0);
  fullscreen = 0;
  switch (view_mode) {
    case VIEW_MODE_TOP_BOT:
      SDL_SetWindowSize(win[0], WINDOW_WIDTH, WINDOW_HEIGHT);
      break;

    case VIEW_MODE_TOP:
      SDL_SetWindowSize(win[0], WINDOW_WIDTH, WINDOW_HEIGHT12);
      break;

    case VIEW_MODE_BOT:
      SDL_SetWindowSize(win[0], WINDOW_WIDTH2, WINDOW_HEIGHT12);
      break;

    case VIEW_MODE_SEPARATE:
      SDL_SetWindowSize(win[0], WINDOW_WIDTH, WINDOW_HEIGHT12);
      SDL_SetWindowSize(win[1], WINDOW_WIDTH2, WINDOW_HEIGHT12);
      break;
  }
}

int hide_windows = 0;
static void guiMain(struct nk_context *ctx)
{
  struct nk_style_item fixed_background = ctx->style.window.fixed_background;
  ctx->style.window.fixed_background = nk_style_item_hide();
  const char *background_wnd = "Background";
  if (nk_begin(ctx, background_wnd, nk_rect(0, 0, win_width[0], win_height[0]),
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
  if (nk_begin(ctx, remote_play_wnd, nk_rect(25, 10, 450, 465),
               NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE))
  {
    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "View Mode", NK_TEXT_CENTERED);
    int selected = view_mode;
    static struct nk_vec2 comboIPsSize = {225, 200};
    const char *view_mode_options[] = {
      "Top and Bottom",
      "Separate Windows",
      "Top Only",
      "Bottom Only"
    };
    nk_combobox(ctx, view_mode_options, sizeof(view_mode_options) / sizeof(*view_mode_options), &selected, 30, comboIPsSize);
    if (selected != (int)view_mode) {
      view_mode = selected;
      updateViewMode();
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Upscaling Filter", NK_TEXT_CENTERED);
    if (nk_checkbox_label(ctx, "", &upscaling_filter)) {
      if (upscaling_filter) {
        if (!upscaling_filter_created) {
          if (sr_create() < 0) {
            fprintf(stderr, "Failed to create NCNN instance for upscaling filter.\n");
            upscaling_filter = 0;
          } else
            upscaling_filter_created = 1;
        }
      }
    }

    nk_layout_row_dynamic(ctx, 30, 5);
    nk_label(ctx, "3DS IP", NK_TEXT_CENTERED);

    for (int i = 0; i < 4; ++i)
    {
      int ip_octet = ip_octets[i];
      nk_property_int(ctx, "#", 0, &ip_octet, 255, 1, 1);
      if (ip_octet != ip_octets[i]) {
        ip_octets[i] = ip_octet;
        strcpy(autoIPs[0], "Manual");
        selectedIP = 0;
      }
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    if (nk_button_label(ctx, "Auto-Detect")) {
      detect3DSIP();
      tryAutoSelectAdapterIP();
    }
    selected = selectedIP;
    nk_combobox(ctx, (const char **)autoIPs, autoIPsCount, &selected, 30, comboIPsSize);
    if (selected != selectedIP) {
      selectedIP = selected;
      if (selectedIP) {
        memcpy(ip_octets, autoIPsOctets[selectedIP], 4);
        tryAutoSelectAdapterIP();
      }
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Viewer IP", NK_TEXT_CENTERED);
    selected = selectedAdaptor;
    nk_combobox(ctx, (const char **)adaptorIPs, adaptorIPsCount, &selected, 30, comboIPsSize);
    if (selected != selectedAdaptor) {
      selectedAdaptor = selected;
      if (selectedAdaptor == adaptorIPsCount - 2) {
        tryAutoSelectAdapterIP();
      } else if (selectedAdaptor == adaptorIPsCount - 1) {
        getAdapterIPs();
      }
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Viewer Port", NK_TEXT_CENTERED);
    nk_property_int(ctx, "#", 1024, &ntr_rp_port, 65535, 1, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Prioritize Top Screen", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &ntr_rp_priority);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Priority Screen Factor", NK_TEXT_CENTERED);
    nk_property_int(ctx, "#", 0, &ntr_rp_priority_factor, 255, 1, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "JPEG Quality %d", ntr_rp_quality);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 10, &ntr_rp_quality, 100, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    int qos = ntr_rp_qos * 128 * 1024;
    snprintf(msg_buf, sizeof(msg_buf), "QoS %d.%d MBps", qos / 1024 / 1024, qos / 1024 % 1024 / 128 * 125);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 4, &ntr_rp_qos, 20, 1);

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
      menu_remote_play = 1;
      if (ntr_rp_port != ntr_rp_bound_port) {
        ntr_rp_bound_port = ntr_rp_port;
        ntr_rp_port_changed = 1;
      }
    }

    nk_layout_row_dynamic(ctx, 30, 1);
    nk_label(ctx, "Press \"F\" to toggle fullscreen.", NK_TEXT_CENTERED);
  }
  nk_end(ctx);
  nk_window_show(ctx, remote_play_wnd, show_window);

  const char *debug_msg_wnd = "Debug";
  if (nk_begin(ctx, debug_msg_wnd, nk_rect(475, 10, 150, 250),
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

#ifdef USE_OGL_ES
#define GLSL_FBO_VERSION "#version 320 es\n"
#else
#define GLSL_FBO_VERSION "#version 430\n"
#endif
static GLbyte fbo_vShaderStr[] =
    GLSL_FBO_VERSION
    "in vec4 a_position; \n"
    "in vec2 a_texCoord; \n"
    "out vec2 v_texCoord; \n"
    "void main() \n"
    "{ \n"
    " gl_Position = a_position; \n"
    " v_texCoord = a_texCoord; \n"
    "} \n";

static GLbyte fbo_fShaderStr[] =
    GLSL_FBO_VERSION
    "precision mediump float; \n"
    "in vec2 v_texCoord; \n"
    "uniform samplerBuffer mediump s_texture; \n"
    "uniform int v_texSize; \n"
    "out vec4 outColor; \n"
    "void main() \n"
    "{ \n"
    " outColor = texelFetch(s_texture, int(v_texCoord.y) * v_texSize + int(v_texCoord.x)); \n"
    "} \n";

static GLfloat fbo_vVertices_pos[4][3] = {
    { -1.f, -1.f, 0.0f }, // Position 1
    { -1.f, 1.f, 0.0f },  // Position 0
    { 1.f, -1.f, 0.0f },  // Position 2
    { 1.f, 1.f, 0.0f },   // Position 3
};

static GLfloat fbo_vVertices_tex_coord[4][2] = {
    { 1.0f, 0.0f }, // TexCoord 2
    { 0.0f, 0.0f }, // TexCoord 1
    { 0.0f, 1.0f }, // TexCoord 0
    { 1.0f, 1.0f }, // TexCoord 3
};
static GLushort fbo_indices[] =
    {0, 1, 2, 1, 2, 3};

static GLfloat vVertices_pos[4][3] = {
    { -0.5f, 0.5f, 0.0f },  // Position 0
    { -0.5f, -0.5f, 0.0f }, // Position 1
    { 0.5f, -0.5f, 0.0f },  // Position 2
    { 0.5f, 0.5f, 0.0f },   // Position 3
};

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

GLuint gl_program;
GLint gl_position_loc;
GLint gl_tex_coord_loc;
GLint gl_sampler_loc;

GLuint gl_fbo_program;
GLint gl_fbo_position_loc;
GLint gl_fbo_tex_coord_loc;
GLint gl_fbo_sampler_loc;
GLint gl_fbo_tex_size_loc;

enum FrameBufferStatus
{
  FBS_NOT_AVAIL,
  FBS_UPDATED,
  FBS_NOT_UPDATED,
};

typedef struct _FrameBufferContext
{
  pthread_mutex_t gl_tex_mutex;
  uint8_t *images[3];
  GLuint gl_tex_id;
  enum FrameBufferStatus updated;
  int index;
  int next_index;

  GLuint gl_tex_upscaled;
  GLuint gl_fbo_upscaled[SCREEN_COUNT];
} FrameBufferContext;

int gl_updated = 0;
pthread_cond_t gl_updated_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t gl_updated_mutex = PTHREAD_MUTEX_INITIALIZER;

FrameBufferContext buffer_ctx[SCREEN_COUNT];

int frame_buffer_context_next_free_index(int index, int skip_index)
{
  int next_index = (index + 1) % 3;
  if (next_index == skip_index)
  {
    next_index = (index + 1) % 3;
  }
  return next_index;
}

static void do_hr_draw_screen(FrameBufferContext *ctx, int index, int width, int height, int top_bot)
{
  double ctx_left_f;
  double ctx_top_f;
  double ctx_right_f;
  double ctx_bot_f;
  int ctx_width;
  int ctx_height;
  int win_w;
  int win_h;
  int tb;
  if (view_mode == VIEW_MODE_TOP_BOT) {
    tb = 0;
    win_w = win_width[0];
    win_h = win_height[0];

    ctx_height = (double)win_height[0] / 2;
    int ctx_left;
    int ctx_top;
    if ((double)win_width[0] / width * height > ctx_height)
    {
      ctx_width = (double)ctx_height / height * width;
      ctx_left = (double)(win_width[0] - ctx_width) / 2;
      ctx_top = 0;
    }
    else
    {
      ctx_height = (double)win_width[0] / width * height;
      ctx_left = 0;
      ctx_width = win_width[0];
      ctx_top = (double)win_height[0] / 2 - ctx_height;
    }

    if (top_bot == SCREEN_TOP)
    {
      ctx_left_f = (double)ctx_left / win_width[0] * 2 - 1;
      ctx_top_f = 1 - (double)ctx_top / win_height[0] * 2;
      ctx_right_f = -ctx_left_f;
      ctx_bot_f = 0;
    }
    else
    {
      ctx_left_f = (double)ctx_left / win_width[0] * 2 - 1;
      ctx_top_f = 0;
      ctx_right_f = -ctx_left_f;
      ctx_bot_f = -1 + (double)ctx_top / win_height[0] * 2;
    }
  } else {
    tb = top_bot;
    if (view_mode == VIEW_MODE_BOT)
      tb = 0;

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
  vVertices_pos[0][0] = ctx_left_f;
  vVertices_pos[0][1] = ctx_top_f;
  vVertices_pos[1][0] = ctx_left_f;
  vVertices_pos[1][1] = ctx_bot_f;
  vVertices_pos[2][0] = ctx_right_f;
  vVertices_pos[2][1] = ctx_bot_f;
  vVertices_pos[3][0] = ctx_right_f;
  vVertices_pos[3][1] = ctx_top_f;

  nk_bool upscaled = screen_upscale_factor > 1 && upscaling_filter && upscaling_filter_created;
  int scale = upscaled ? screen_upscale_factor : 1;
  GLuint tex = upscaled ? ctx->gl_tex_upscaled : ctx->gl_tex_id;

  if (upscaled) {
    GLuint tex_upscaled = sr_run(top_bot, height, width, GL_CHANNELS_N, ctx->images[index]);
    if (!tex_upscaled) {
      upscaled = 0;
      upscaling_filter = 0;
    }

    scale = screen_upscale_factor;
    glActiveTexture(GL_TEXTURE0);

    if (0) {
      glBindTexture(GL_TEXTURE_BUFFER, tex_upscaled);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->gl_fbo_upscaled[tb]);
      glViewport(0, 0, height * scale, width * scale);
      glDisable(GL_CULL_FACE);
      glDisable(GL_DEPTH_TEST);

      glUseProgram(gl_fbo_program);
      fbo_vVertices_tex_coord[0][0] = 0.5;
      fbo_vVertices_tex_coord[0][1] = 0.5;
      fbo_vVertices_tex_coord[1][0] = 0.5;
      fbo_vVertices_tex_coord[1][1] = width * scale - 0.5;
      fbo_vVertices_tex_coord[2][0] = height * scale - 0.5;
      fbo_vVertices_tex_coord[2][1] = 0.5;
      fbo_vVertices_tex_coord[3][0] = height * scale - 0.5;
      fbo_vVertices_tex_coord[3][1] = width * scale - 0.5;

      glVertexAttribPointer(gl_fbo_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(*fbo_vVertices_pos), fbo_vVertices_pos);
      glVertexAttribPointer(gl_fbo_tex_coord_loc, 2, GL_FLOAT, GL_FALSE, sizeof(*fbo_vVertices_tex_coord), fbo_vVertices_tex_coord);
      glEnableVertexAttribArray(gl_fbo_position_loc);
      glEnableVertexAttribArray(gl_fbo_tex_coord_loc);

      glUniform1i(gl_fbo_sampler_loc, 0);
      glUniform1i(gl_fbo_tex_size_loc, height * scale);
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, fbo_indices);

      glBindTexture(GL_TEXTURE_2D, ctx->gl_tex_upscaled);
      tex = ctx->gl_tex_upscaled;
    } else {
      glBindTexture(GL_TEXTURE_2D, tex_upscaled);
      tex = tex_upscaled;
    }
  }

  if (!upscaled) {
    scale = 1;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ctx->gl_tex_id);
    glTexImage2D(
      GL_TEXTURE_2D, 0,
      GL_INT_FORMAT, height,
      width, 0,
      GL_FORMAT, GL_UNSIGNED_BYTE,
      ctx->images[index]);

    tex = ctx->gl_tex_id;
  }

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glViewport(0, 0, win_w, win_h);

#ifdef USE_ANGLE
  nk_bool use_fsr = 0;
#else
  nk_bool use_fsr = ctx_height > height * scale && ctx_width > width * scale && upscaled;
#endif

  if (use_fsr) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint out_tex = fsr_main(top_bot, tex, height * scale, width * scale, ctx_height, ctx_width, 0.25f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, out_tex);

    glUseProgram(gl_program);
    glVertexAttribPointer(gl_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(*vVertices_pos), vVertices_pos);
    glVertexAttribPointer(gl_tex_coord_loc, 2, GL_FLOAT, GL_FALSE, sizeof(*vVertices_tex_coord), vVertices_tex_coord);

    glEnableVertexAttribArray(gl_position_loc);
    glEnableVertexAttribArray(gl_tex_coord_loc);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

    glUniform1i(gl_sampler_loc, 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
  } else {
    glUseProgram(gl_program);
    glVertexAttribPointer(gl_position_loc, 3, GL_FLOAT, GL_FALSE, sizeof(*vVertices_pos), vVertices_pos);
    glVertexAttribPointer(gl_tex_coord_loc, 2, GL_FLOAT, GL_FALSE, sizeof(*vVertices_tex_coord), vVertices_tex_coord);

    glEnableVertexAttribArray(gl_position_loc);
    glEnableVertexAttribArray(gl_tex_coord_loc);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

    glUniform1i(gl_sampler_loc, 0);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
  }
}

static int hr_draw_screen(FrameBufferContext *ctx, int width, int height, int top_bot, int force)
{
  if (ctx->updated == FBS_NOT_AVAIL)
  {
    return 0;
  }

  pthread_mutex_lock(&ctx->gl_tex_mutex);
  int index = ctx->index;
  if (ctx->updated == FBS_UPDATED)
  {
    int next_index = frame_buffer_context_next_free_index(ctx->index, ctx->next_index);
    index = ctx->index = ctx->next_index;
    ctx->next_index = next_index;
    ctx->updated = FBS_NOT_UPDATED;

    pthread_mutex_unlock(&ctx->gl_tex_mutex);

    do_hr_draw_screen(ctx, index, width, height, top_bot);

    int frame_stat_updated = 0;
    static uint32_t max_delay_between_packet[SCREEN_COUNT] = {};
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      int frame_counter_current;
      if ((frame_counter_current = __atomic_load_n(&frame_rate_tracker[i].counter, __ATOMIC_RELAXED)) >=
        FRAME_STAT_EVERY_X_FRAMES
      )
      {
        uint64_t next_tick = iclock64();
        if (frame_rate_tracker[i].last_tick != 0)
        {
          // fprintf(stderr, "%d ms for %d rendered frames\n", next_tick - frame_count_last_tick, FRAME_STAT_EVERY_X_FRAMES);
          frame_rate_tracker[i].display = frame_counter_current * 1000 / (next_tick - frame_rate_tracker[i].last_tick);
        }
        frame_rate_tracker[i].last_tick = next_tick;

        int frame_counter_next = 0, frame_counter_prev = frame_counter_current;
        while (!__atomic_compare_exchange_n(&frame_rate_tracker[i].counter,
          &frame_counter_current, frame_counter_next, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            frame_counter_current = __atomic_load_n(&frame_rate_tracker[i].counter, __ATOMIC_RELAXED);
            frame_counter_next = frame_counter_current - frame_counter_prev;
        }

        frame_stat_updated = 1;
      }

      if (frame_size_tracker[i].index == 0)
        frame_stat_updated = 1;

      if (delay_between_packet_tracker[i].index == 0) {
        frame_stat_updated = 1;

        max_delay_between_packet[i] = 0;
        for (int j = 0; j < FRAME_STAT_EVERY_X_FRAMES; ++j)
          if (max_delay_between_packet[i] < delay_between_packet_tracker[i].counter[j])
            max_delay_between_packet[i] = delay_between_packet_tracker[i].counter[j];
      }
    }
    if (frame_stat_updated) {
      if (view_mode == VIEW_MODE_TOP_BOT) {
        snprintf(window_title_with_fps, sizeof(window_title_with_fps), TITLE " (FPS %03d %03d) (Size %06d | %06d) (Packet time %04d %04d)",
          frame_rate_tracker[SCREEN_TOP].display, frame_rate_tracker[SCREEN_BOT].display,
          frame_size_tracker[SCREEN_TOP].total / FRAME_STAT_EVERY_X_FRAMES,
          frame_size_tracker[SCREEN_BOT].total / FRAME_STAT_EVERY_X_FRAMES,
          max_delay_between_packet[SCREEN_TOP], max_delay_between_packet[SCREEN_BOT]);
        SDL_SetWindowTitle(win[0], window_title_with_fps);
      } else {
        int tb = top_bot;
        if (view_mode == VIEW_MODE_BOT)
          tb = 0;
        snprintf(window_title_with_fps, sizeof(window_title_with_fps), TITLE " (FPS %03d) (Size %06d) (Packet time %04d)",
          frame_rate_tracker[top_bot].display,
          frame_size_tracker[top_bot].total / FRAME_STAT_EVERY_X_FRAMES,
          max_delay_between_packet[top_bot]);
        SDL_SetWindowTitle(win[tb], window_title_with_fps);
      }
    }
    return 1;
  }
  else
  {
    pthread_mutex_unlock(&ctx->gl_tex_mutex);

    if (force)
      do_hr_draw_screen(ctx, index, width, height, top_bot);
    return 0;
  }
}

static void
MainLoop(void *loopArg)
{
  /* Draw */
  int screen_count = SCREEN_COUNT;
  if (view_mode == VIEW_MODE_TOP || view_mode == VIEW_MODE_BOT || view_mode == VIEW_MODE_TOP_BOT)
    screen_count = 1;

  for (int i = 0; i < screen_count; ++i)
  {
    float bg[4];
    nk_color_fv(bg, nk_rgb(28, 48, 62));
    // SDL_GetWindowSize(win[i], &win_width[i], &win_height[i]);
    SDL_GL_GetDrawableSize(win[i], &win_width[i], &win_height[i]);

    SDL_GL_MakeCurrent(win[i], glContext[i]);
    glViewport(0, 0, win_width[i], win_height[i]);
    glClearColor(bg[0], bg[1], bg[2], bg[3]);
    glClear(GL_COLOR_BUFFER_BIT);

#define MIN_UPDATE_INTERVAL_MS (33)

    int force = 0;
    if (i == 0) {
      pthread_mutex_lock(&gl_updated_mutex);
      while (!gl_updated) {
        struct timespec to;
        clock_gettime(CLOCK_REALTIME, &to);
        to.tv_nsec += MIN_UPDATE_INTERVAL_MS * 1000 * 1000;
        if (ETIMEDOUT == pthread_cond_timedwait(&gl_updated_cond, &gl_updated_mutex, &to)) {
          force = 1;
          break;
        }
      }
      gl_updated = 0;
      pthread_mutex_unlock(&gl_updated_mutex);
    }

    int updated = 0;
    static uint64_t lastUpdated[2] = { 0 };
    uint64_t nextUpdated = iclock64();
    if (nextUpdated - lastUpdated[i] > MIN_UPDATE_INTERVAL_MS)
      force = 1;

    if (view_mode == VIEW_MODE_TOP_BOT) {
      force = 1;
      updated |= hr_draw_screen(&buffer_ctx[SCREEN_TOP], 400, 240, SCREEN_TOP, force);
      updated |= hr_draw_screen(&buffer_ctx[SCREEN_BOT], 320, 240, SCREEN_BOT, force);
    } else if (view_mode == VIEW_MODE_BOT)
      updated = hr_draw_screen(&buffer_ctx[1], 320, 240, 1, force);
    else
      updated = hr_draw_screen(&buffer_ctx[i], i == 0 ? 400 : 320, 240, i, force);

    /* IMPORTANT: `nk_sdl_render` modifies some global OpenGL state
     * with blending, scissor, face culling, depth test and viewport and
     * defaults everything back into a default state.
     * Make sure to either a.) save and restore or b.) reset your own state after
     * rendering the UI. */
    if (updated || force) {
      if (i == 0) {
        struct nk_context *ctx = (struct nk_context *)loopArg;
        /* Input */
        SDL_Event evt;
        nk_input_begin(ctx);
        while (SDL_PollEvent(&evt))
        {
          if (
            evt.type == SDL_QUIT ||
            (evt.type == SDL_WINDOWEVENT && evt.window.event == SDL_WINDOWEVENT_CLOSE)
          )
            running = nk_false;
          else if (
            evt.type == SDL_KEYDOWN &&
            evt.key.keysym.sym == SDLK_f
          ) {
            if (fullscreen) {
              SDL_SetWindowFullscreen(win[0], 0);
              if (view_mode == VIEW_MODE_SEPARATE)
                SDL_SetWindowFullscreen(win[1], 0);
              fullscreen = 0;
            } else {
              SDL_SetWindowFullscreen(win[0], SDL_WINDOW_FULLSCREEN_DESKTOP);
              if (view_mode == VIEW_MODE_SEPARATE)
                SDL_SetWindowFullscreen(win[1], SDL_WINDOW_FULLSCREEN_DESKTOP);
              fullscreen = 1;
            }
          }
          else
            nk_sdl_handle_event(&evt);
        }
        nk_input_end(ctx);

        guiMain(ctx);
        nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);
      }
      SDL_GL_SwapWindow(win[i]);
      lastUpdated[i] = nextUpdated;
    }
  }
}

void handle_decode_frame_screen(FrameBufferContext *ctx, uint8_t *rgb, int top_bot, int frame_size, int delay_between_packet)
{
  pthread_mutex_lock(&ctx->gl_tex_mutex);
  int next_index = frame_buffer_context_next_free_index(ctx->next_index, ctx->index);
  uint8_t **pimage = &ctx->images[next_index];
  *pimage = rgb;
  ctx->updated = FBS_UPDATED;
  ctx->next_index = next_index;
  pthread_mutex_unlock(&ctx->gl_tex_mutex);
  __atomic_add_fetch(&frame_rate_tracker[top_bot].counter, 1, __ATOMIC_RELAXED);

  frame_size_tracker[top_bot].total += frame_size;
  frame_size_tracker[top_bot].total -= frame_size_tracker[top_bot].counter[frame_size_tracker[top_bot].index];
  frame_size_tracker[top_bot].counter[frame_size_tracker[top_bot].index] = frame_size;
  frame_size_tracker[top_bot].index = (frame_size_tracker[top_bot].index + 1) % FRAME_STAT_EVERY_X_FRAMES;

  delay_between_packet_tracker[top_bot].counter[delay_between_packet_tracker[top_bot].index] = delay_between_packet;
  delay_between_packet_tracker[top_bot].index = (delay_between_packet_tracker[top_bot].index + 1) % FRAME_STAT_EVERY_X_FRAMES;

  pthread_mutex_lock(&gl_updated_mutex);
  gl_updated = 1;
  pthread_cond_signal(&gl_updated_cond);
  pthread_mutex_unlock(&gl_updated_mutex);
}

uint8_t screen_decoded[SCREEN_COUNT][400 * 240 * GL_CHANNELS_N];
// uint8_t screen_upscaled[SCREEN_COUNT][400 * 240 * GL_CHANNELS_N * screen_upscale_factor * screen_upscale_factor];

uint8_t upscaled_u_image[400 * 240];
uint8_t upscaled_v_image[400 * 240];

#define BUF_SIZE 2000
uint8_t buf[BUF_SIZE];

#define PACKET_SIZE 1448
#define rp_data_hdr_size (4)
#define rp_packet_data_size (PACKET_SIZE - rp_data_hdr_size)
#define rp_data_hdr_id_size (2)

#define MAX_PACKET_COUNT (128)

#define rp_work_count (2)
uint8_t recv_buf[rp_work_count][rp_packet_data_size * MAX_PACKET_COUNT];
uint8_t recv_track[rp_work_count][MAX_PACKET_COUNT];
uint8_t recv_hdr[rp_work_count][rp_data_hdr_id_size];
uint8_t recv_end[rp_work_count];
uint8_t recv_end_packet[rp_work_count];
uint8_t recv_end_incomp[rp_work_count];
uint32_t recv_end_size[rp_work_count];
uint32_t recv_delay_between_packets[rp_work_count];
uint32_t recv_last_packet_time[rp_work_count];
uint8_t recv_work;

#ifdef EMBED_JPEG_TURBO
void* rpMalloc(j_common_ptr cinfo, u32 size)
{
  void* ret = cinfo->alloc.buf + cinfo->alloc.stats.offset;
  u32 totalSize = size;
  if (totalSize % 32 != 0) {
    totalSize += 32 - (totalSize % 32);
  }
  if (cinfo->alloc.stats.remaining < totalSize) {
    u32 alloc_size = cinfo->alloc.stats.offset + cinfo->alloc.stats.remaining;
    fprintf(stderr, "bad alloc, size: %d/%d\n", totalSize, alloc_size);
    return 0;
  }
  cinfo->alloc.stats.offset += totalSize;
  cinfo->alloc.stats.remaining -= totalSize;

#if 0
  if (cinfo->alloc.stats.offset > cinfo->alloc.max_offset) {
    cinfo->alloc.max_offset = cinfo->alloc.stats.offset;
    nsDbgPrint("cinfo %08x alloc.max_offset: %d\n", cinfo, cinfo->alloc.max_offset);
  }
#endif

  return ret;
}

void rpFree(j_common_ptr, void*) {}

jmp_buf jpeg_jmp;

void jpeg_error_exit(j_common_ptr cinfo)
{
  /* Always display the message */
  (*cinfo->err->output_message) (cinfo);

  /* Let the memory manager delete any temp files before we die */
  // jpeg_destroy(cinfo);
  longjmp(jpeg_jmp, 1);
}

void jpeg_emit_message(j_common_ptr cinfo, int msg_level)
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
    longjmp(jpeg_jmp, 1);
  } else {
    /* It's a trace message.  Show it if trace_level >= msg_level. */
    if (err->trace_level >= msg_level)
      (*err->output_message) (cinfo);
  }
}

int handle_decode(uint8_t *out, uint8_t *in, int size, int w, int h) {
  struct jpeg_decompress_struct cinfo;

  cinfo.alloc.buf = malloc(400 * 240 * GL_CHANNELS_N);
  if (cinfo.alloc.buf) {
    cinfo.alloc.stats.offset = 0;
    cinfo.alloc.stats.remaining = 400 * 240 * GL_CHANNELS_N;
  } else {
    return -1;
  }

  struct jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jerr.error_exit = jpeg_error_exit;
  jerr.emit_message = jpeg_emit_message;

  int i = setjmp(jpeg_jmp);
  int ret = 0;
  if (i == 0) {
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, in, size);
    ret = jpeg_read_header(&cinfo, TRUE);
    if (ret == JPEG_HEADER_OK) {
      cinfo.out_color_space = JCS_FORMAT;
      jpeg_start_decompress(&cinfo);
      // fprintf(stderr, "jpeg_read_header: %d %d (%d %d)\n", (int)cinfo.output_width, (int)cinfo.output_height, h, w);
      if ((int)cinfo.output_width == h && (int)cinfo.output_height == w) {
        while (cinfo.output_scanline < cinfo.output_height) {
          uint8_t *buffer = out + cinfo.output_scanline * cinfo.output_width * GL_CHANNELS_N;
          jpeg_read_scanlines(&cinfo, &buffer, 1);
        }
        jpeg_finish_decompress(&cinfo);
        // jpeg_destroy_decompress(&cinfo);
        ret = 0;
      } else {
        // jpeg_destroy_decompress(&cinfo);
        ret = -1;
      }
    } else {
      // jpeg_destroy_decompress(&cinfo);
      ret = -1;
    }
  } else {
    ret = -1;
  }
  free(cinfo.alloc.buf);
  return ret;
}
#else
int handle_decode(uint8_t *out, uint8_t *in, int size, int w, int h) {
  tjhandle tjInstance = NULL;
  if ((tjInstance = tj3Init(TJINIT_DECOMPRESS)) == NULL) {
    fprintf(stderr, "create turbo jpeg decompressor failed\n");
    return -1;
  }

  int ret = -1;

  if (tj3DecompressHeader(tjInstance, in, size) != 0 ) {
    fprintf(stderr, "jpeg header error\n");
    goto final;
  }

  if (
    h != tj3Get(tjInstance, TJPARAM_JPEGWIDTH) ||
    w != tj3Get(tjInstance, TJPARAM_JPEGHEIGHT)
  ) {
    fprintf(stderr, "jpeg unexpected dimensions\n");
    goto final;
  }

  if (tj3Decompress8(tjInstance, in, size, out, h * GL_CHANNELS_N, TJ_FORMAT) != 0) {
    fprintf(stderr, "jpeg decompression error\n");
    goto final;
  }

  ret = 0;

final:
  tj3Destroy(tjInstance);
  return ret;
}
#endif

int handle_recv(uint8_t *buf, int size)
{
  if (size < rp_data_hdr_size) {
    fprintf(stderr, "recv header too small\n");
    return 0;
  }
  uint8_t *hdr = buf;
  buf += rp_data_hdr_size;
  size -= rp_data_hdr_size;

  if (hdr[2] != 2) {
    fprintf(stderr, "recv invalid header\n");
    return 0;
  }

  uint8_t end = 0;
  if (hdr[1] & 0x10) {
    end = 1;
  } else if (size != rp_packet_data_size) {
    fprintf(stderr, "recv incorrect size\n");
    return 0;
  }
  hdr[1] &= 0x1;
  uint8_t isTop = hdr[1];

  uint8_t work = recv_work;
  if (memcmp(recv_hdr[recv_work], hdr, rp_data_hdr_id_size) != 0) {
    if (memcmp(recv_hdr[!recv_work], hdr, rp_data_hdr_id_size) != 0) {
      recv_work = !recv_work;
      memset(recv_track[recv_work], 0, MAX_PACKET_COUNT);
      memcpy(recv_hdr[recv_work], hdr, rp_data_hdr_id_size);
      if (recv_end[recv_work] != 2) {
        fprintf(stderr, "recv incomplete skipping frame\n");
      }
      recv_end[recv_work] = 0;
      recv_delay_between_packets[recv_work] = 0;
      recv_last_packet_time[recv_work] = iclock();
      recv_end_incomp[recv_work] = 0;
    }
    work = !work;
  }

  uint8_t packet = hdr[3];
  if (packet >= MAX_PACKET_COUNT) {
    fprintf(stderr, "recv packet number too high\n");
    return 0;
  }

  {
    uint32_t packet_time = iclock();
    uint32_t delay_from_last_packet = packet_time - recv_last_packet_time[work];
    if (delay_from_last_packet > recv_delay_between_packets[work]) {
      recv_delay_between_packets[work] = delay_from_last_packet;
    }
    recv_last_packet_time[work] = packet_time;
  }

  // fprintf(stderr, "%d %d %d %d (%d %d)\n", hdr[0], hdr[1], hdr[2], hdr[3], size, end);

  memcpy(&recv_buf[work][rp_packet_data_size * packet], buf, size);
  recv_track[work][packet] = 1;
  if (end) {
    recv_end[work] = 1;
    recv_end_packet[work] = packet;
    recv_end_size[work] = rp_packet_data_size * packet + size;
    // fprintf(stderr, "size %d\n", recv_end_size[work]);
  }

  if (recv_end[work] == 1) {
    for (int i = 0; i < recv_end_packet[work]; ++i) {
      if (!recv_track[work][i]) {
        if (!recv_end_incomp[work]) {
          recv_end_incomp[work] = 1;
          fprintf(stderr, "recv end packet incomplete\n");
        }
        return 0;
      }
    }

    recv_end[work] = 2;
    int top_bot = !recv_hdr[work][1];
    if (handle_decode(screen_decoded[top_bot], recv_buf[work], recv_end_size[work], isTop ? 400 : 320, 240) != 0) {
      fprintf(stderr, "recv decode error\n");
      return 0;
    }

    handle_decode_frame_screen(&buffer_ctx[top_bot], screen_decoded[top_bot], top_bot, recv_end_size[work], recv_delay_between_packets[work]);
    recv_work = !work;
  }

  return 0;
}

SOCKET s;
struct sockaddr_in remoteAddr;

void receive_from_socket(SOCKET s)
{
  while (running && !ntr_rp_port_changed)
  {
    socklen_t nAddrLen = sizeof(remoteAddr);

    int ret = recvfrom(s, (char *)buf, sizeof(buf), 0, (struct sockaddr *)&remoteAddr, &nAddrLen);
    if (ret == 0)
    {
      continue;
    }
    else if (ret < 0)
    {
      int err = sock_errno();
      if (err != WSAETIMEDOUT && err != WSAEWOULDBLOCK)
      {
        fprintf(stderr, "recvfrom failed: %d\n", err);
      }
      else
      {
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

    if (handle_recv(buf, ret) < 0)
    {
      return;
    }
  }
}

void socket_error_pause(void) {
  Sleep(RP_SOCKET_TIMEOUT);
}

void *udp_recv_thread_func(void *)
{
  while (running)
  {
    // fprintf(stderr, "new connection\n");
    for (int top_bot = 0; top_bot < SCREEN_COUNT; ++top_bot) {
      buffer_ctx[top_bot].updated = FBS_NOT_AVAIL;
    }
    for (int i = 0; i < rp_work_count; ++i) {
      recv_end[i] = 2;
    }

    memset(frame_rate_tracker, 0, sizeof(frame_rate_tracker));
    memset(frame_size_tracker, 0, sizeof(frame_size_tracker));
    memset(delay_between_packet_tracker, 0, sizeof(delay_between_packet_tracker));

    s = 0;
    int ret;
    if (!SOCKET_VALID(s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)))
    {
      fprintf(stderr, "socket creation failed\n");
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
      fprintf(stderr, "socket bind failed for port %d\n", ntr_rp_bound_port);
      // running = 0;
      socket_error_pause();
      continue;
    }
    uint8_t *octets = adaptorIPsOctets[selectedAdaptor];
    fprintf(stderr, "port bound at %d.%d.%d.%d:%d\n", (int)octets[0], (int)octets[1], (int)octets[2], (int)octets[3], ntr_rp_bound_port);
    ntr_rp_port_changed = 0;
    ntr_rp_port = ntr_rp_bound_port;

    int buff_size = 6 * 1024 * 1024;
    socklen_t tmp = sizeof(buff_size);

    ret = setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)(&buff_size), sizeof(buff_size));
    buff_size = 0;
    ret = getsockopt(s, SOL_SOCKET, SO_RCVBUF, (char *)(&buff_size), &tmp);
    if (ret)
    {
      fprintf(stderr, "setsockopt buf size failed\n");
      // running = 0;
      socket_error_pause();
      continue;
    }

#ifdef _WIN32
    DWORD timeout = RP_SOCKET_TIMEOUT;
#else
    struct timeval timeout;
    timeout.tv_sec = RP_SOCKET_TIMEOUT / 1000;
    timeout.tv_usec = (RP_SOCKET_TIMEOUT % 1000) * 1000;
#endif
    ret = setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    if (ret)
    {
      fprintf(stderr, "setsockopt timeout failed\n");
      // running = 0;
      socket_error_pause();
      continue;
    }

    receive_from_socket(s);

    closesocket(s);

    // Sleep(250);
  }

  return 0;
}

#include "style.h"

#ifdef GL_DEBUG
static void on_gl_error(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar *message, const void *)
{
  fprintf(stderr, "gl_error: %u:%u:%u:%u:%u: %s\n", source, type, id, severity, length, message);
}
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
  CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif
  if (upscaling_filter) {
    if (sr_create() < 0)
      return -1;
    upscaling_filter_created = 1;
  }

  /* GUI */
  struct nk_context *ctx;
  int ret;

  NK_UNUSED(argc);
  NK_UNUSED(argv);

  /* SDL setup */
  SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#ifdef USE_ANGLE
  SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
#endif
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS);
#ifdef USE_ANGLE
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_EGL, 1);
#endif
#ifdef USE_OGL_ES
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
#ifdef USE_ANGLE
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#endif
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif
#ifdef GL_DEBUG
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetSwapInterval(1);

  win[0] = SDL_CreateWindow(TITLE,
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  if (!win[0])
  {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return -1;
  }
  win[1] = SDL_CreateWindow(TITLE,
                         SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
  if (!win[1])
  {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return -1;
  }

  glContext[0] = SDL_GL_CreateContext(win[0]);
  if (!glContext[0])
  {
    fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
    return -1;
  }

  if (!gladLoadGLES2Loader((GLADloadproc)SDL_GL_GetProcAddress))
  {
    fprintf(stderr, "gladLoadGLES2 failed\n");
    return -1;
  }

#ifdef GL_DEBUG
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
  glDebugMessageCallback(on_gl_error, NULL);
#endif

  fprintf(stderr, "ogl version: %s\n", glGetString(GL_VERSION));

  SDL_GL_MakeCurrent(win[0], glContext[0]);

  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
  glContext[1] = SDL_GL_CreateContext(win[1]);
  if (!glContext[1])
  {
    fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
    return -1;
  }

  /* OpenGL setup */
  glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

  ctx = nk_sdl_init(win[0]);
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

  for (int i = 0; i < SCREEN_COUNT; ++i) {
    glGenTextures(1, &buffer_ctx[i].gl_tex_id);

    glGenTextures(1, &buffer_ctx[i].gl_tex_upscaled);
    glActiveTexture(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, buffer_ctx[i].gl_tex_upscaled);
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

    pthread_mutex_init(&buffer_ctx[i].gl_tex_mutex, NULL);
  }

  for (int j = 0; j < SCREEN_COUNT; ++j) {
    for (int i = 0; i < SCREEN_COUNT; ++i) {
      SDL_GL_MakeCurrent(win[j], glContext[j]);
      glGenFramebuffers(1, &buffer_ctx[i].gl_fbo_upscaled[j]);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, buffer_ctx[i].gl_fbo_upscaled[j]);
      glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, buffer_ctx[i].gl_tex_upscaled, 0);
      GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
      glDrawBuffers(1, &draw_buffer);
      if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "fbo init error\n");
        return -1;
      }
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }
  }

  gl_program = LoadProgram((const char *)vShaderStr, (const char *)fShaderStr);
  gl_position_loc = glGetAttribLocation(gl_program, "a_position");
  gl_tex_coord_loc = glGetAttribLocation(gl_program, "a_texCoord");
  gl_sampler_loc = glGetUniformLocation(gl_program, "s_texture");

  gl_fbo_program = LoadProgram((const char *)fbo_vShaderStr, (const char *)fbo_fShaderStr);
  gl_fbo_position_loc = glGetAttribLocation(gl_fbo_program, "a_position");
  gl_fbo_tex_coord_loc = glGetAttribLocation(gl_fbo_program, "a_texCoord");
  gl_fbo_sampler_loc = glGetUniformLocation(gl_fbo_program, "s_texture");
  gl_fbo_tex_size_loc = glGetUniformLocation(gl_fbo_program, "v_texSize");

  rpConfigSetDefault();
  detect3DSIP();
  getAdapterIPs();
  tryAutoSelectAdapterIP();

  pthread_t udp_recv_thread;
  if ((ret = pthread_create(&udp_recv_thread, NULL, udp_recv_thread_func, NULL)))
  {
    fprintf(stderr, "pthread_create failed\n");
    return -1;
  }
  pthread_t menu_tcp_thread;
  if ((ret = pthread_create(&menu_tcp_thread, NULL, menu_tcp_thread_func, NULL)))
  {
    fprintf(stderr, "pthread_create failed\n");
    return -1;
  }
  pthread_t nwm_tcp_thread;
  if ((ret = pthread_create(&nwm_tcp_thread, NULL, nwm_tcp_thread_func, NULL)))
  {
    fprintf(stderr, "pthread_create failed\n");
    return -1;
  }

#ifdef _WIN32
  SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED | ES_DISPLAY_REQUIRED);
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

  pthread_join(udp_recv_thread, NULL);
  pthread_join(menu_tcp_thread, NULL);
  pthread_join(nwm_tcp_thread, NULL);

  glDeleteProgram(gl_fbo_program);
  glDeleteProgram(gl_program);
  for (int i = 0; i < SCREEN_COUNT; ++i) {
    pthread_mutex_destroy(&buffer_ctx[i].gl_tex_mutex);
    glDeleteTextures(1, &buffer_ctx[i].gl_tex_id);
    for (int j = 0; j < SCREEN_COUNT; ++j) {
      glDeleteFramebuffers(1, &buffer_ctx[i].gl_fbo_upscaled[j]);
    }
    glDeleteTextures(1, &buffer_ctx[i].gl_tex_upscaled);
  }

  sock_cleanup();
  nk_sdl_shutdown();
  SDL_GL_DeleteContext(glContext[1]);
  SDL_GL_DeleteContext(glContext[0]);
  SDL_DestroyWindow(win[1]);
  SDL_DestroyWindow(win[0]);
  SDL_Quit();

  if (upscaling_filter_created)
    sr_destroy();
  return 0;
}
