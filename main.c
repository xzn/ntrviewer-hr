#include "ikcp.h"

#define HR_MAX(a, b) ((a) > (b) ? (a) : (b))
#define HR_MIN(a, b) ((a) < (b) ? (a) : (b))

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

#include "ffmpeg_opt/libavcodec/ffmpeg_jls.h"
#include "ffmpeg_opt/libavcodec/jpegls.h"
#include "imagezero/iz_c.h"
#include "jpeg_turbo/jpeglib.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd/zstd.h"
#include "zstd/decompress/zstd_decompress_internal.h"
#include "lz4.h"
#include "huffmancodec.h"
#include "rlecodec.h"

#define RP_MAX(a,b) ((a) > (b) ? (a) : (b))
#define RP_MIN(a,b) ((a) > (b) ? (b) : (a))

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

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

#define UNUSED(a) (void)a
// #define MIN(a, b) ((a) < (b) ? (a) : (b))
// #define MAX(a, b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a) / sizeof(a)[0])

enum {
  RP_SCREEN_SPLIT_LEFT,
  RP_SCREEN_SPLIT_RIGHT,
  RP_SCREEN_SPLIT_COUNT,
  RP_SCREEN_SPLIT_FULL = (u8)-1,
};

enum {
  SCREEN_TOP,
  SCREEN_BOT,
  SCREEN_COUNT,
};

#define FRAME_STAT_EVERY_X_FRAMES 24
char window_title_with_fps[500];
struct {
  int display;
  int counter;
  uint64_t last_tick;
} frame_rate_tracker[SCREEN_COUNT] = {};
struct {
  int index;
  int counter[RP_SCREEN_SPLIT_COUNT][FRAME_STAT_EVERY_X_FRAMES];
  int total[RP_SCREEN_SPLIT_COUNT];
} frame_size_tracker[SCREEN_COUNT] = {};
struct {
  int index;
  uint32_t counter[RP_SCREEN_SPLIT_COUNT][FRAME_STAT_EVERY_X_FRAMES];
} delay_between_packet_tracker[SCREEN_COUNT] = {};

/* Platform */
SDL_Window *win;
SDL_GLContext glThreadContext;
int running = nk_true;
int win_width, win_height;

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

#define RP_ENCODE_STATIC_LQ (0)

#define RP_KCP_SNDWNDSIZE_BITS (6)
#define RP_KCP_MAX_SNDWNDSIZE (RP_KCP_MIN_SNDWNDSIZE + (1 << RP_KCP_SNDWNDSIZE_BITS) - 1)
#define RP_KCP_MIN_MINRTO (10)
#define RP_KCP_MIN_SNDWNDSIZE (32)
#define RP_ME_MIN_BLOCK_SIZE_LOG2 (2)
#define RP_ME_MIN_BLOCK_SIZE (1 << RP_ME_MIN_BLOCK_SIZE_LOG2)
#define RP_ME_MIN_SEARCH_PARAM (8)
#define RP_IMAGE_ME_SELECT_BITS (6)
#define RP_IMAGE_FRAME_N_BITS (3)
#define RP_IMAGE_FRAME_N_RANGE (1 << RP_IMAGE_FRAME_N_BITS)
#define RP_ZSTD_COMP_LEVEL_BITS (3)
#define RP_ZSTD_COMP_LEVEL_HALF_RANGE (1 << (RP_ZSTD_COMP_LEVEL_BITS - 1))

#define RP_HUFF_SHARE_STATS (0)

static int yuv_option;
static int color_transform_hp;
static int encoder_which;
static int downscale_uv;
static int encode_lq;
static int jpeg_quality;
static int zstd_comp_level;
static int me_method;
static int me_block_size;
static int me_search_param;
static nk_bool me_downscale;
static int me_select;
static nk_bool me_interpolate;
static nk_bool dynamic_priority;
static int min_dp_frame_rate;
static int max_frame_rate;
static int target_mbit_rate;
static nk_bool multicore_encode;
static nk_bool multicore_network;
static nk_bool multicore_screen;
static nk_bool low_latency;
static int top_priority;
static int bot_priority;
static int kcp_minrto;
static int kcp_snd_wnd_size;
static nk_bool kcp_nocwnd;
static int kcp_fastresend;
static int kcp_nodelay;

union rp_conf_arg0_t {
  int arg0;
  struct {
    u32 kcp_nocwnd : 1;
    u32 kcp_fastresend : 2;
    u32 me_select : RP_IMAGE_ME_SELECT_BITS;
    u32 me_downscale : 1;
    u32 me_interpolate : 1;
    u32 multicore_network : 1;
    u32 multicore_screen : 1;
    u32 encode_lq : 2;
    u32 jpeg_quality : 7;
    u32 zstd_comp_level : RP_ZSTD_COMP_LEVEL_BITS;
  };
};

union rp_conf_arg1_t {
  int arg1;
  struct {
    u32 yuv_option : 2;
    u32 color_transform_hp : 2;
    u32 downscale_uv : 2;
    u32 encoder_which : 3;
    u32 me_block_size : 2;
    u32 me_method : 3;
    u32 me_search_param : 5;
    u32 kcp_minrto : 7;
    u32 kcp_snd_wnd_size : RP_KCP_SNDWNDSIZE_BITS;
  };
};

union rp_conf_arg2_t {
  int arg2;
  struct {
    u32 top_priority : 4;
    u32 bot_priority : 4;
    u32 low_latency : 1;
    u32 multicore_encode : 1;
    u32 target_mbit_rate : 5;
    u32 dynamic_priority : 1;
    u32 min_dp_frame_rate : 7;
    u32 max_frame_rate : 7;
    u32 kcp_nodelay : 2;
  };
};

_Static_assert(sizeof(union rp_conf_arg0_t) == sizeof(int));
_Static_assert(sizeof(union rp_conf_arg1_t) == sizeof(int));
_Static_assert(sizeof(union rp_conf_arg2_t) == sizeof(int));

static int ntr_yuv_option;
static int ntr_color_transform_hp;
static int ntr_downscale_uv;
static int ntr_encoder_which;
static nk_bool ntr_encode_split_image;
static unsigned int ntr_me_enabled;
static int ntr_me_block_size;
static int ntr_me_search_param;
static nk_bool ntr_me_downscale;
static nk_bool ntr_me_interpolate;

static int debug_view_plane;

static atomic_uint_fast8_t ip_octets[4];

enum {
  RP_ENCODER_FFMPEG_JLS,
  RP_ENCODER_HP_JLS,
  RP_ENCODER_JLS_USE_LUT_COUNT,
  RP_ENCODER_ZSTD_JLS = RP_ENCODER_JLS_USE_LUT_COUNT,
  RP_ENCODER_LZ4_JLS,
  RP_ENCODER_HUFF_JLS,
  RP_ENCODER_JLS_COUNT,
  RP_ENCODER_IMAGE_ZERO = RP_ENCODER_JLS_COUNT,
  RP_ENCODER_JPEG_TURBO,
};

enum rp_send_header_type {
  RP_SEND_HEADER_TYPE_CONF,
  RP_SEND_HEADER_TYPE_DATA,
};

struct rp_send_info_header {
  u32 type_conf : 1;
  u32 downscale_uv : 2;
  u32 yuv_option : 2;
  u32 color_transform_hp : 2;
  u32 encoder_which : 3;
  u32 encode_split_image: 1;
  u32 me_enabled : 2;
  u32 me_downscale : 1;
  u32 me_search_param : 5;
  u32 me_block_size : 2;
  u32 me_interpolate : 1;
};

struct rp_send_data_header {
  u32 type_data : 1;
  u32 top_bot : 1;
  u32 frame_n : RP_IMAGE_FRAME_N_BITS;
  u32 left_right : 1;
  u32 data_stats : 1;
  u32 p_frame : 1;
  u32 bpp : 3;
  u32 data_end : 1;
  u32 data_size : 11;
  u32 plane_type : 1;
  u32 plane_comp : 2;
} send_header;

_Static_assert(sizeof(struct rp_send_info_header) == sizeof(u32));
_Static_assert(sizeof(struct rp_send_data_header) == sizeof(u32));

#define HEART_BEAT_EVERY_MS 250
#define REST_EVERY_MS 100

#define TCP_MAGIC 0x12345678
#define TCP_ARGS_COUNT 16
#define KCP_MAGIC_DEF 0x87654321
static int kcp_magic = KCP_MAGIC_DEF;
static int restart_kcp = 0;
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

#define TCP_SOCKET_TIMEOUT 2000
SOCKET tcp_connect(int port)
{
  struct sockaddr_in servaddr = {0};
  SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (!SOCKET_VALID(sockfd))
  {
    fprintf(stderr, "socket creation failed: %d\n", sock_errno());
    return INVALID_SOCKET;
  }

  u_long opt = 1;
  if (ioctlsocket(sockfd, FIONBIO, &opt)) {
    fprintf(stderr, "ioctlsocket FIONBIO failed: %d\n", sock_errno());
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

  fprintf(stderr, "connecting to %s:%d ...\n", ip_addr_buf, port);
  int ret = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
  if (ret != 0 && sock_errno() != WSAEWOULDBLOCK)
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

        union rp_conf_arg0_t arg0 = {
          .kcp_nocwnd = kcp_nocwnd,
          .kcp_fastresend = kcp_fastresend,
          .me_select = me_select,
          .multicore_network = multicore_network,
          .multicore_screen = multicore_screen,
          .jpeg_quality = jpeg_quality,
          .me_downscale = me_downscale,
          .me_interpolate = me_interpolate,
          .zstd_comp_level = zstd_comp_level,
          .encode_lq = encode_lq,
        };
        union rp_conf_arg1_t arg1 = {
          .yuv_option = yuv_option,
          .color_transform_hp = color_transform_hp,
          .downscale_uv = downscale_uv,
          .encoder_which = encoder_which,
          .me_block_size = me_block_size,
          .me_method = me_method,
          .me_search_param = me_search_param - RP_ME_MIN_SEARCH_PARAM,
          .kcp_minrto = kcp_minrto - RP_KCP_MIN_MINRTO,
          .kcp_snd_wnd_size = kcp_snd_wnd_size - RP_KCP_MIN_SNDWNDSIZE,
        };
        union rp_conf_arg2_t arg2 = {
          .top_priority = top_priority,
          .bot_priority = bot_priority,
          .low_latency = low_latency,
          .multicore_encode = multicore_encode,
          .target_mbit_rate = target_mbit_rate,
          .dynamic_priority = dynamic_priority,
          .min_dp_frame_rate = min_dp_frame_rate,
          .max_frame_rate = max_frame_rate,
          .kcp_nodelay = kcp_nodelay,
        };
        uint32_t args[] = {arg0.arg0, arg1.arg1, arg2.arg2};

        ret = tcp_send_packet_header(sockfd, packet_seq, 0, 901,
                                     args, sizeof(args) / sizeof(*args), 0);

        restart_kcp = 1;

        if (0) {
          ntr_downscale_uv = downscale_uv;
          ntr_yuv_option = yuv_option;
          ntr_color_transform_hp = color_transform_hp;
          ntr_encoder_which = encoder_which;
          ntr_me_enabled = me_method > 1 ? 1 : me_method == 1 ? -1 : 0;
          ntr_me_block_size = RP_ME_MIN_BLOCK_SIZE << me_block_size;
          ntr_me_search_param = me_search_param;
          ntr_me_downscale = me_downscale;
          ntr_me_interpolate = me_interpolate;
        }

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
  yuv_option = 2;
  color_transform_hp = 0;
  encoder_which = 4;
  downscale_uv = 1;
  encode_lq = 1;
  jpeg_quality = 90;
  zstd_comp_level = 4;
  me_method = 1;
  me_block_size = 3;
  me_search_param = 32;
  me_downscale = 0;
  me_interpolate = 0;
  min_dp_frame_rate = 0;
  max_frame_rate = 0;
  me_select = 7;
  target_mbit_rate = 12;
  dynamic_priority = 1;
  multicore_encode = 1;
  low_latency = 1;
  top_priority = 3;
  bot_priority = 5;
  multicore_network = 0;
  multicore_screen = 1;
  kcp_minrto = 24;
  kcp_snd_wnd_size = 32;
  kcp_nocwnd = 1;
  kcp_fastresend = 2;
  kcp_nodelay = 2;
}

int hide_windows = 0;
static void guiMain(struct nk_context *ctx)
{
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
  if (nk_begin(ctx, remote_play_wnd, nk_rect(25, 50, 600, 800),
               NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_TITLE))
  {
    nk_layout_row_dynamic(ctx, 30, 5);
    nk_label(ctx, "IP", NK_TEXT_CENTERED);

    for (int i = 0; i < 4; ++i)
    {
      int ip_octet = ip_octets[i];
      nk_property_int(ctx, "#", 0, &ip_octet, 255, 1, 1);
      ip_octets[i] = ip_octet;
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "YUV option", NK_TEXT_CENTERED);
    const char *yuv_options_text[] = {
      "None (RGB)",
      "Color Transform",
      "Full Swing",
      "Studio Swing",
    };
    nk_combobox(ctx, yuv_options_text, sizeof(yuv_options_text) / sizeof(*yuv_options_text),
      &yuv_option, 30, nk_vec2(150, 9999)
    );
    if (yuv_option != 1) {
      color_transform_hp = 0;
    }
    if (yuv_option < 2) {
      downscale_uv = 0;
      if (yuv_option == 1) {
        if (color_transform_hp == 0) {
          color_transform_hp = 3;
        }
      }
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Color Transform", NK_TEXT_CENTERED);
    const char *color_transform_text[] = {
      "None (RGB)",
      "HP1",
      "HP2",
      "HP3",
    };
    nk_combobox(ctx, color_transform_text, sizeof(color_transform_text) / sizeof(*color_transform_text),
      &color_transform_hp, 30, nk_vec2(150, 9999)
    );
    if (color_transform_hp > 0) {
      yuv_option = 1;
      downscale_uv = 0;
    } else if (yuv_option == 1) {
      yuv_option = 0;
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Motion Estimation", NK_TEXT_CENTERED);
    const char *motion_estimation_text[] = {
      "Disabled",
      "None (Diff Only)",
      // "Three Step",
      // "Two Dimensional Log",
      // "New Three Step",
      // "Four Step",
      // "Diamond",
      // "Hexagon-Based",
    };
    nk_combobox(ctx, motion_estimation_text, sizeof(motion_estimation_text) / sizeof(*motion_estimation_text),
      &me_method, 30, nk_vec2(250, 9999)
    );

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "ME Block Size %d", RP_ME_MIN_BLOCK_SIZE << me_block_size);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &me_block_size, 3, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "ME Search Param %d", me_search_param);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, RP_ME_MIN_SEARCH_PARAM, &me_search_param, 36, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    if (me_select) {
      snprintf(msg_buf, sizeof(msg_buf), "ME Select %d/64", me_select);
    } else {
      snprintf(msg_buf, sizeof(msg_buf), "ME Select Off");
    }
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &me_select, 63, 1);
    if (me_select)
      me_interpolate = 0;

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "ME Downscale", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &me_downscale);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "ME Interpolate", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &me_interpolate);
    if (me_interpolate)
      me_select = 0;

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Encoder", NK_TEXT_CENTERED);
    const char *encoder_which_text[] = {
      "FFmpeg JPEG-LS",
      "HP JPEG-LS",
      "ZSTD Med-Pred",
      "LZ4 Med-Pred",
      "Huff Med-Pred",
      "ImageZero",
      "JPEG Turbo",
    };
    nk_combobox(ctx, encoder_which_text, sizeof(encoder_which_text) / sizeof(*encoder_which_text),
      &encoder_which, 30, nk_vec2(150, 9999)
    );

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Encode LQ %d", encode_lq);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &encode_lq, 3, 1);

    int zstd_comp_level_disp = zstd_comp_level - RP_ZSTD_COMP_LEVEL_HALF_RANGE;
    if (zstd_comp_level_disp >= 0)
      ++zstd_comp_level_disp;
    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "ZSTD Comp Level %d", zstd_comp_level_disp);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &zstd_comp_level, 7, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "JPEG Quality %d", jpeg_quality);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 1, &jpeg_quality, 100, 1);

    // nk_layout_row_dynamic(ctx, 30, 2);
    // nk_label(ctx, "Downscale UV", NK_TEXT_CENTERED);
    // nk_checkbox_label(ctx, "", &downscale_uv);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Downscale UV %d", downscale_uv);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &downscale_uv, 3, 1);
    if (downscale_uv > 0) {
      color_transform_hp = 0;
      if (yuv_option < 2) {
        yuv_option = 2;
      }
    }

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Low Latency", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &low_latency);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "MT Encode", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &multicore_encode);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "MT Network Transfer", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &multicore_network);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "MT Screen Capture", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &multicore_screen);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "Dynamic Priority", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &dynamic_priority);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Top Priority %d", top_priority);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &top_priority, 15, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Bot Priority %d", bot_priority);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &bot_priority, 15, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Min DP Frame Rate %d", min_dp_frame_rate);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &min_dp_frame_rate, 120, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    if (max_frame_rate) {
      snprintf(msg_buf, sizeof(msg_buf), "Max Frame Rate %d", max_frame_rate);
    } else {
      snprintf(msg_buf, sizeof(msg_buf), "Max Frame Rate Off");
    }
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &max_frame_rate, 120, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "Target MBit Rate %d", target_mbit_rate);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &target_mbit_rate, 20, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "KCP MinRTO %d", kcp_minrto);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 10, &kcp_minrto, 120, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "KCP Snd Wnd Size %d", kcp_snd_wnd_size);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, RP_KCP_MIN_SNDWNDSIZE, &kcp_snd_wnd_size, RP_KCP_MAX_SNDWNDSIZE, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "KCP Nodelay %d", kcp_nodelay);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &kcp_nodelay, 2, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    snprintf(msg_buf, sizeof(msg_buf), "KCP FastResend %d", kcp_fastresend);
    nk_label(ctx, msg_buf, NK_TEXT_CENTERED);
    nk_slider_int(ctx, 0, &kcp_fastresend, 2, 1);

    nk_layout_row_dynamic(ctx, 30, 2);
    nk_label(ctx, "KCP NoCwnd", NK_TEXT_CENTERED);
    nk_checkbox_label(ctx, "", &kcp_nocwnd);

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

  const char *debug_msg_wnd = "Debug";
  if (nk_begin(ctx, debug_msg_wnd, nk_rect(625, 50, 150, 250),
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

    nk_layout_row_dynamic(ctx, 30, 1);
    const char *debug_view_plane_text[] = {
      "",
      "Y",
      "U",
      "V",
      "ME X",
      "ME Y",
      "Y ME",
      "U ME",
      "V ME",
    };
    nk_combobox(ctx, debug_view_plane_text, sizeof(debug_view_plane_text) / sizeof(*debug_view_plane_text),
      &debug_view_plane, 30, nk_vec2(150, 9999)
    );
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

static void hr_draw_screen(FrameBufferContext *ctx, int width, int height, int top_bot)
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
  if (top_bot == SCREEN_TOP)
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

  glBindTexture(GL_TEXTURE_2D, ctx->gl_tex_id);

  pthread_mutex_lock(&ctx->gl_tex_mutex);
  int index = ctx->index;
  if (ctx->updated == FBS_UPDATED)
  {
    int next_index = frame_buffer_context_next_free_index(ctx->index, ctx->next_index);
    index = ctx->index = ctx->next_index;
    ctx->next_index = next_index;
    ctx->updated = FBS_NOT_UPDATED;

    pthread_mutex_unlock(&ctx->gl_tex_mutex);

    glTexImage2D(GL_TEXTURE_2D, 0,
                 GL_RGB, height,
                 width, 0,
                 GL_RGB, GL_UNSIGNED_BYTE,
                 ctx->images[index]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);

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
          for (int k = 0; k < RP_SCREEN_SPLIT_COUNT; ++k)
            if (max_delay_between_packet[i] < delay_between_packet_tracker[i].counter[k][j])
              max_delay_between_packet[i] = delay_between_packet_tracker[i].counter[k][j];
      }
    }
    if (frame_stat_updated) {
      snprintf(window_title_with_fps, sizeof(window_title_with_fps), "NTR Viewer HR (FPS %03d %03d) (Size %06d %06d | %06d %06d) (Packet time %04d %04d)",
        frame_rate_tracker[SCREEN_TOP].display, frame_rate_tracker[SCREEN_BOT].display,
        frame_size_tracker[SCREEN_TOP].total[RP_SCREEN_SPLIT_LEFT] / FRAME_STAT_EVERY_X_FRAMES, frame_size_tracker[SCREEN_TOP].total[RP_SCREEN_SPLIT_RIGHT] / FRAME_STAT_EVERY_X_FRAMES,
        frame_size_tracker[SCREEN_BOT].total[RP_SCREEN_SPLIT_LEFT] / FRAME_STAT_EVERY_X_FRAMES, frame_size_tracker[SCREEN_BOT].total[RP_SCREEN_SPLIT_RIGHT] / FRAME_STAT_EVERY_X_FRAMES,
        max_delay_between_packet[SCREEN_TOP], max_delay_between_packet[SCREEN_BOT]);
      SDL_SetWindowTitle(win, window_title_with_fps);
    }
  }
  else
  {
    pthread_mutex_unlock(&ctx->gl_tex_mutex);
  }

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

    pthread_mutex_lock(&gl_updated_mutex);
    while (!gl_updated) {
      struct timespec to;
      clock_gettime(CLOCK_REALTIME, &to);
      if (hide_windows) {
        to.tv_nsec += 100 * 1000 * 1000;
      } else {
        to.tv_nsec += 10 * 1000 * 1000;
      }
      if (ETIMEDOUT == pthread_cond_timedwait(&gl_updated_cond, &gl_updated_mutex, &to)) {
        break;
      }
    }
    gl_updated = 0;
    pthread_mutex_unlock(&gl_updated_mutex);

    hr_draw_screen(&buffer_ctx[SCREEN_TOP], 400, 240, SCREEN_TOP);
    hr_draw_screen(&buffer_ctx[SCREEN_BOT], 320, 240, SCREEN_BOT);

    /* IMPORTANT: `nk_sdl_render` modifies some global OpenGL state
     * with blending, scissor, face culling, depth test and viewport and
     * defaults everything back into a default state.
     * Make sure to either a.) save and restore or b.) reset your own state after
     * rendering the UI. */
    nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);
    SDL_GL_SwapWindow(win);
  }
}

void handle_decode_frame_screen(FrameBufferContext *ctx, uint8_t *rgb, int top_bot, int frame_size_left, int frame_size_right, int delay_between_packet_left, int delay_between_packet_right)
{
  pthread_mutex_lock(&ctx->gl_tex_mutex);
  int next_index = frame_buffer_context_next_free_index(ctx->next_index, ctx->index);
  uint8_t **pimage = &ctx->images[next_index];
  *pimage = rgb;
  ctx->updated = FBS_UPDATED;
  ctx->next_index = next_index;
  pthread_mutex_unlock(&ctx->gl_tex_mutex);
  __atomic_add_fetch(&frame_rate_tracker[top_bot].counter, 1, __ATOMIC_RELAXED);

  frame_size_tracker[top_bot].total[RP_SCREEN_SPLIT_LEFT] += frame_size_left;
  frame_size_tracker[top_bot].total[RP_SCREEN_SPLIT_LEFT] -= frame_size_tracker[top_bot].counter[RP_SCREEN_SPLIT_LEFT][frame_size_tracker[top_bot].index];
  frame_size_tracker[top_bot].counter[RP_SCREEN_SPLIT_LEFT][frame_size_tracker[top_bot].index] = frame_size_left;
  frame_size_tracker[top_bot].total[RP_SCREEN_SPLIT_RIGHT] += frame_size_right;
  frame_size_tracker[top_bot].total[RP_SCREEN_SPLIT_RIGHT] -= frame_size_tracker[top_bot].counter[RP_SCREEN_SPLIT_RIGHT][frame_size_tracker[top_bot].index];
  frame_size_tracker[top_bot].counter[RP_SCREEN_SPLIT_RIGHT][frame_size_tracker[top_bot].index] = frame_size_right;
  frame_size_tracker[top_bot].index = (frame_size_tracker[top_bot].index + 1) % FRAME_STAT_EVERY_X_FRAMES;

  delay_between_packet_tracker[top_bot].counter[RP_SCREEN_SPLIT_LEFT][delay_between_packet_tracker[top_bot].index] = delay_between_packet_left;
  delay_between_packet_tracker[top_bot].counter[RP_SCREEN_SPLIT_RIGHT][delay_between_packet_tracker[top_bot].index] = delay_between_packet_right;
  delay_between_packet_tracker[top_bot].index = (delay_between_packet_tracker[top_bot].index + 1) % FRAME_STAT_EVERY_X_FRAMES;

  pthread_mutex_lock(&gl_updated_mutex);
  gl_updated = 1;
  pthread_cond_signal(&gl_updated_cond);
  pthread_mutex_unlock(&gl_updated_mutex);
}

void render_greyscale_to_comp3(uint8_t *dst, const uint8_t *src, int w, int h, int bpp) {
  int scale = (1 << (8 - bpp));
  for (int j = 0; j < w; ++j) {
    for (int i = 0; i < h; ++i) {
      dst[2] = dst[1] = dst[0] = *src++ * scale;
      dst += 3;
    }
  }
}

void render_greyscale_upscale_to_comp3(uint8_t *dst, int w_orig, int h_orig, const uint8_t *src, int w, int h, int bpp) {
  int w_scale = w_orig / w;
  int h_scale = h_orig / h;
  int w_off = w_orig % w / 2;
  int h_off = h_orig % h / 2;
  int scale = (1 << (8 - bpp));

  for (int v = 0; v < w_off; ++v) {
    for (int u = 0; u < h_off; ++u) {
      dst[2] = dst[1] = dst[0] = src[0] * scale;
        dst += 3;
    }

    for (int i = 0; i < h; ++i) {
      for (int y = 0; y < h_scale; ++y) {
        dst[2] = dst[1] = dst[0] = src[i] * scale;
        dst += 3;
      }
    }

    for (int u = 0; u < h_off; ++u) {
      dst[2] = dst[1] = dst[0] = src[h - 1] * scale;
        dst += 3;
    }
  }

  for (int j = 0; j < w; ++j) {
    for (int x = 0; x < w_scale; ++x) {
      for (int u = 0; u < h_off; ++u) {
        dst[2] = dst[1] = dst[0] = src[j * h + 0] * scale;
          dst += 3;
      }

      for (int i = 0; i < h; ++i) {
        for (int y = 0; y < h_scale; ++y) {
          dst[2] = dst[1] = dst[0] = src[j * h + i] * scale;
          dst += 3;
        }
      }

      for (int u = 0; u < h_off; ++u) {
        dst[2] = dst[1] = dst[0] = src[j * h + h - 1] * scale;
          dst += 3;
      }
    }
  }

  for (int v = 0; v < w_off; ++v) {
    for (int u = 0; u < h_off; ++u) {
      dst[2] = dst[1] = dst[0] = src[h * (w - 1)] * scale;
        dst += 3;
    }

    for (int i = 0; i < h; ++i) {
      for (int y = 0; y < h_scale; ++y) {
        dst[2] = dst[1] = dst[0] = src[h * (w - 1) + i] * scale;
        dst += 3;
      }
    }

    for (int u = 0; u < h_off; ++u) {
      dst[2] = dst[1] = dst[0] = src[h * (w - 1) + h - 1] * scale;
        dst += 3;
    }
  }

}

void convert_to_rgb_hp(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b,
  int y_bpp, int u_bpp, int v_bpp
) {
  int bpp = (y_bpp + u_bpp + v_bpp) / 3;
  // u8 half_range = 1 << (bpp - 1);
  int bpp_2 = y_bpp - bpp;
  u8 half_y = y >> bpp_2;
  // u8 bpp_mask = (1 << bpp) - 1;
  // u8 bpp_2_mask = (1 << (bpp + 1)) - 1;

  if (ntr_color_transform_hp == 1) {
    *r = ((half_y + u) << (8 - bpp)) - 128;
    *g = y << (8 - y_bpp);
    *b = ((half_y + v) << (8 - bpp)) - 128;
  } else if (ntr_color_transform_hp == 2) {
    *r = ((half_y + u) << (8 - bpp)) - 128;
    *g = y << (8 - y_bpp);
    *b = ((v + (((*r >> (8 - bpp)) + half_y) >> 1)) << (8 - bpp)) - 128;
  } else if (ntr_color_transform_hp == 3) {
    *g = ((y - ((u + v) >> (2 - bpp_2))) << (8 - y_bpp)) + 64;
    u8 half_g = *g >> (8 - bpp);
    *r = ((u + half_g) << (8 - bpp)) - 128;
    *b = ((v + half_g) << (8 - bpp)) - 128;
  } else {
    *r = u << (8 - u_bpp);
    *g = y << (8 - y_bpp);
    *b = v << (8 - v_bpp);
    return;
  }

  double r_in = *r;
  double g_in = *g;
  double b_in = *b;

  g_in = g_in / (double)(((1 << y_bpp) - 1) << (8 - y_bpp)) * (double)((1 << 8) - 1);
  r_in = r_in / (double)(((1 << u_bpp) - 1) << (8 - u_bpp)) * (double)((1 << 8) - 1);
  b_in = b_in / (double)(((1 << v_bpp) - 1) << (8 - v_bpp)) * (double)((1 << 8) - 1);

  *r = r_in;
  *g = g_in;
  *b = b_in;
}

void convert_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b,
  int y_bpp, int u_bpp, int v_bpp
)
{
  if (ntr_yuv_option == 1) {
    convert_to_rgb_hp(y, u, v, r, g, b, y_bpp, u_bpp, v_bpp);
    return;
  }

  double y_in = y;
  // double u_in = u;
  // double v_in = v;
  double u_in = (s8)u;
  double v_in = (s8)v;

  if (ntr_yuv_option == 2)
  {
    y_in = y_in / (double)((1 << y_bpp) - 1) * (double)((1 << 8) - 1);
    y_in /= 255;
    // y_in = (y_in - 1) / (double)((1 << y_bpp) - 2) * (double)((1 << 8) - 2);
    // y_in /= 254;

    // u_in = RP_MAX(u_in, 1);
    // u_in -= 1;
    // u_in = u_in / (double)((1 << u_bpp) - 2) * (double)((1 << 8) - 2);
    // u_in -= 127;
    u_in = u_in / (double)((1 << (u_bpp - 1)) - 1) * (double)((1 << 7) - 1);
    u_in /= 127;

    // v_in = RP_MAX(v_in, 1);
    // v_in -= 1;
    // v_in = v_in / (double)((1 << v_bpp) - 2) * (double)((1 << 8) - 2);
    // v_in -= 127;
    v_in = v_in / (double)((1 << (v_bpp - 1)) - 1) * (double)((1 << 7) - 1);
    v_in /= 127;
  } else if (ntr_yuv_option == 3) {
    // y_in -= 16 >> (8 - y_bpp);
    y_in = y_in / (double)((1 << y_bpp) - 1 - (36 >> (8 - y_bpp))) * (double)((1 << 8) - 1 - 36);
    y_in /= 219;
    // y_in = (y_in - 1) / (double)((1 << y_bpp) - 1 - (36 >> (8 - y_bpp)) - 1) * (double)((1 << 8) - 1 - 36 - 1);
    // y_in /= 219 - 1;

    // u_in = RP_MAX(u_in, 1);
    // u_in -= 16;
    // u_in = u_in / (double)((1 << u_bpp) - (32 >> (8 - u_bpp))) * (double)((1 << 8) - 32);
    // u_in -= 112;
    u_in = u_in / (double)((1 << (u_bpp - 1)) - (32 >> (8 - (u_bpp - 1)))) * (double)((1 << 7) - 16);
    u_in /= 112;

    // v_in = RP_MAX(v_in, 1);
    // v_in -= 16;
    // v_in = v_in / (double)((1 << v_bpp) - (32 >> (8 - v_bpp))) * (double)((1 << 8) - 32);
    // v_in -= 112;
    v_in = v_in / (double)((1 << (v_bpp - 1)) - (32 >> (8 - (v_bpp - 1)))) * (double)((1 << 7) - 16);
    v_in /= 112;
  } else {
    y_in = y_in / (double)((1 << y_bpp) - 1) * (double)((1 << 8) - 1);
    u_in = u_in / (double)((1 << u_bpp) - 1) * (double)((1 << 8) - 1);
    v_in = v_in / (double)((1 << v_bpp) - 1) * (double)((1 << 8) - 1);

    y = round(y_in);
    u = round(u_in);
    v = round(v_in);

    *r = u;
    *g = y;
    *b = v;
    return;
  }

  u_in *= .436;
  v_in *= .615;

  double r_out;
  double g_out;
  double b_out;

  r_out = y_in + 1.13983 * v_in;
  g_out = y_in + -0.39465 * u_in + -0.58060 * v_in;
  b_out = y_in + 2.03211 * u_in;

  if (r_out < 0)
    r_out = 0;
  else if (r_out > 1)
    r_out = 1;
  if (g_out < 0)
    g_out = 0;
  else if (g_out > 1)
    g_out = 1;
  if (b_out < 0)
    b_out = 0;
  else if (b_out > 1)
    b_out = 1;

  *r = round(r_out * 255);
  *g = round(g_out * 255);
  *b = round(b_out * 255);
}

void render_yuv_to_rgb(uint8_t *dst,
  const uint8_t *y_image, const uint8_t *u_image, const uint8_t *v_image,
  int w, int h, int y_bpp, int u_bpp, int v_bpp
) {
  for (int j = 0; j < w; ++j) {
    for (int i = 0; i < h; ++i) {
      convert_to_rgb(*y_image++, *u_image++, *v_image++, dst, dst + 1, dst + 2,
        y_bpp, u_bpp, v_bpp
      );
      dst += 3;
    }
  }
}

void render_rgb_bpp(uint8_t *rgb_image, int w, int h, int bpp) {
  for (int j = 0; j < w; ++j) {
    for (int i = 0; i < h; ++i) {
      for (int k = 0; k < 3; ++k) {
        *rgb_image++ <<= (8 - bpp);
      }
    }
  }
}

uint8_t screen_decoded[SCREEN_COUNT][400 * 240 * 3];

uint8_t upscaled_u_image[400 * 240];
uint8_t upscaled_v_image[400 * 240];

static uint8_t accessImageNoCheck(const uint8_t *image, int x, int y, int, int h)
{
    return image[x * h + y];
}

static inline uint8_t accessImage(const uint8_t *image, int x, int y, int w, int h)
{
    return accessImageNoCheck(image, HR_MAX(HR_MIN(x, w - 1), 0), HR_MAX(HR_MIN(y, h - 1), 0), w, h);
}

#define rshift_to_even(n, s) (((n) + ((s) > 1 ? (1 << ((s) - 1)) : 0)) >> (s))
#define srshift_to_even(n, s) rshift_to_even(n, s)
#define srshift_s32(n, s) ((n) / (1 << (s)))
#define RP_CLIP(n, min, max) RP_MIN(RP_MAX((n), (typeof(n))(min)), (typeof(n))(max))
#define DSX_DIM(w, dsx) (((w) + (dsx) - 1) / (dsx))

static inline int16_t accessImageUpsampleUnscaled(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
  int ds_w = wOrig / 2;
  int ds_h = hOrig / 2;

  xOrig -= 1;
  yOrig -= 1;

  int ds_x0 = xOrig / 2;
  int ds_x1 = ds_x0;
  int ds_y0 = yOrig / 2;
  int ds_y1 = ds_y0;

  // if (xOrig > ds_x0 * 2)
  // { // xOrig is odd -> ds_x0 * 2 + 1 = xOrig = ds_x1 * 2 - 1
    ++ds_x1;
  // }
  // else
  // { // xOrig is even -> ds_x0 * 2 + 2 = xOrig = ds_x1 * 2
    // --ds_x0;
  // }

  // if (yOrig > ds_y0 * 2)
  // {
    ++ds_y1;
  // }
  // else
  // {
  //   --ds_y0;
  // }

  int16_t a = (int8_t)accessImage(ds_image, ds_x0, ds_y0, ds_w, ds_h);
  int16_t b = (int8_t)accessImage(ds_image, ds_x1, ds_y0, ds_w, ds_h);
  int16_t c = (int8_t)accessImage(ds_image, ds_x0, ds_y1, ds_w, ds_h);
  int16_t d = (int8_t)accessImage(ds_image, ds_x1, ds_y1, ds_w, ds_h);

  if (xOrig == ds_x0 * 2)
  {
    a = (a * 3 + b);
    c = (c * 3 + d);
  }
  else
  {
    a = (a + b * 3);
    c = (c + d * 3);
  }

  if (yOrig == ds_y0 * 2)
  {
    a = (a * 3 + c);
  }
  else
  {
    a = (a + c * 3);
  }

  return a;
}

static inline uint8_t accessImageUpsample(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
  int16_t p = accessImageUpsampleUnscaled(ds_image, xOrig, yOrig, wOrig, hOrig);
  return srshift_to_even(p, 4);
}

static inline void upsampleImage(uint8_t *dst, const uint8_t *ds_src, int w, int h)
{
  int i = 0, j = 0;
  for (; i < w; ++i)
  {
    j = 0;
    for (; j < h; ++j)
    {
      *dst++ = accessImageUpsample(ds_src, i, j, w, h);
    }
  }
}

static inline int16_t accessImageUpsample4Unscaled(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
  int ds_w = wOrig / 4;
  int ds_h = hOrig / 4;

  xOrig -= 2;
  yOrig -= 2;

  int ds_x0 = xOrig / 4;
  int ds_x1 = ds_x0;
  int ds_y0 = yOrig / 4;
  int ds_y1 = ds_y0;

  ++ds_x1;
  ++ds_y1;

  int16_t a = (int8_t)accessImage(ds_image, ds_x0, ds_y0, ds_w, ds_h);
  int16_t b = (int8_t)accessImage(ds_image, ds_x1, ds_y0, ds_w, ds_h);
  int16_t c = (int8_t)accessImage(ds_image, ds_x0, ds_y1, ds_w, ds_h);
  int16_t d = (int8_t)accessImage(ds_image, ds_x1, ds_y1, ds_w, ds_h);

  if (xOrig == ds_x0 * 4)
  {
    a = (a * 7 + b);
    c = (c * 7 + d);
  }
  else if (xOrig == ds_x0 * 4 + 1)
  {
    a = (a * 5 + b * 3);
    c = (c * 5 + d * 3);
  }
  else if (xOrig == ds_x0 * 4 + 2)
  {
    a = (a * 3 + b * 5);
    c = (c * 3 + d * 5);
  }
  else
  {
    a = (a + b * 7);
    c = (c + d * 7);
  }

  if (yOrig == ds_y0 * 4)
  {
    a = (a * 7 + c);
  }
  else if (yOrig == ds_y0 * 4 + 1)
  {
    a = (a * 5 + c * 3);
  }
  else if (yOrig == ds_y0 * 4 + 2)
  {
    a = (a * 3 + c * 5);
  }
  else
  {
    a = (a + c * 7);
  }

  return a;
}

static inline uint8_t accessImageUpsample4(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
  int16_t p = accessImageUpsample4Unscaled(ds_image, xOrig, yOrig, wOrig, hOrig);
  return srshift_to_even(p, 6);
}

static inline void upsample4Image(uint8_t *dst, const uint8_t *ds_src, int w, int h)
{
  int i = 0, j = 0;
  for (; i < w; ++i)
  {
    j = 0;
    for (; j < h; ++j)
    {
      *dst++ = accessImageUpsample4(ds_src, i, j, w, h);
    }
  }
}

static inline int16_t accessImageUpsample3Unscaled(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
  int ds_w = DSX_DIM(wOrig, 3);
  int ds_h = DSX_DIM(hOrig, 3);

  xOrig -= 1;
  yOrig -= 1;

  int ds_x0 = xOrig / 3;
  int ds_x1 = ds_x0;
  int ds_y0 = yOrig / 3;
  int ds_y1 = ds_y0;

  ++ds_x1;
  ++ds_y1;

  int16_t a = (int8_t)accessImage(ds_image, ds_x0, ds_y0, ds_w, ds_h);
  int16_t b = (int8_t)accessImage(ds_image, ds_x1, ds_y0, ds_w, ds_h);
  int16_t c = (int8_t)accessImage(ds_image, ds_x0, ds_y1, ds_w, ds_h);
  int16_t d = (int8_t)accessImage(ds_image, ds_x1, ds_y1, ds_w, ds_h);

  if (xOrig == ds_x0 * 3)
  {
    a = a * 6;
    c = c * 6;
  }
  else if (xOrig == ds_x0 * 3 + 1)
  {
    a = (a * 4 + b * 2);
    c = (c * 4 + d * 2);
  }
  else
  {
    a = (a * 2 + b * 4);
    c = (c * 2 + d * 4);
  }

  if (yOrig == ds_y0 * 3)
  {
    a = (a * 6);
  }
  else if (yOrig == ds_y0 * 3 + 1)
  {
    a = (a * 4 + c * 2);
  }
  else
  {
    a = (a * 2 + c * 4);
  }

  return a;
}

static inline uint8_t accessImageUpsample3(const uint8_t *ds_image, int xOrig, int yOrig, int wOrig, int hOrig)
{
  int16_t p = accessImageUpsample3Unscaled(ds_image, xOrig, yOrig, wOrig, hOrig);
  return round((double)p * (1.0 / 36.0));
}

static inline void upsample3Image(uint8_t *dst, const uint8_t *ds_src, int w, int h)
{
  int i = 0, j = 0;
  for (; i < w; ++i)
  {
    j = 0;
    for (; j < h; ++j)
    {
      *dst++ = accessImageUpsample3(ds_src, i, j, w, h);
    }
  }
}

static inline void upsampleXImage(uint8_t *dst, const uint8_t *ds_src, int w, int h, int dsx)
{
  switch (dsx) {
    default:
      fprintf(stderr, "upsampleXImage dsx err %d\n", dsx);
      return;

    case 2:
      upsampleImage(dst, ds_src, w, h);
      return;

    case 3:
      upsample3Image(dst, ds_src, w, h);
      return;

    case 4:
      upsample4Image(dst, ds_src, w, h);
      return;
  }
}

#define BUF_SIZE 2000
uint8_t buf[BUF_SIZE];
ikcpcb *kcp;

uint32_t recv_data_remain;
uint8_t buf_leftover[BUF_SIZE];
int leftover_size;

#define ENCODE_BUFFER_COUNT RP_IMAGE_FRAME_N_RANGE

enum {
  RECV_STATE_HEADER,
  RECV_STATE_DATA,
} recv_state;

enum {
  Y_COMP,
  U_COMP,
  V_COMP,
  UV_COMP,
  R_COMP = Y_COMP,
  G_COMP,
  B_COMP,
  COMP_COUNT,
  ME_X_COMP = 0,
  ME_Y_COMP,
  ME_COMP_COUNT,
};

enum {
  Y_DATA,
  U_DATA,
  V_DATA,
  ME_X_DATA,
  ME_Y_DATA,
  PLANE_COUNT,
};

int16_t screen_frame_n[SCREEN_COUNT][ENCODE_BUFFER_COUNT];
uint8_t screen_pos[SCREEN_COUNT];

char *state_string[COMP_COUNT] = {
  "Y", "U", "V"
};

uint8_t screen_done[SCREEN_COUNT][ENCODE_BUFFER_COUNT];
uint32_t screen_data_size[SCREEN_COUNT][ENCODE_BUFFER_COUNT][RP_SCREEN_SPLIT_COUNT];
uint32_t screen_data_delay_between_packet[SCREEN_COUNT][ENCODE_BUFFER_COUNT][RP_SCREEN_SPLIT_COUNT];
uint32_t screen_data_last_packet_time[SCREEN_COUNT][ENCODE_BUFFER_COUNT][RP_SCREEN_SPLIT_COUNT];
int8_t screen_buf_valid[SCREEN_COUNT][ENCODE_BUFFER_COUNT];
uint8_t screen_buf[SCREEN_COUNT][ENCODE_BUFFER_COUNT][COMP_COUNT][400 * 240];
uint8_t screen_bpp[SCREEN_COUNT][ENCODE_BUFFER_COUNT][COMP_COUNT];
int8_t screen_me_buf[SCREEN_COUNT][ENCODE_BUFFER_COUNT][ME_COMP_COUNT][400 * 240 / RP_ME_MIN_BLOCK_SIZE / RP_ME_MIN_BLOCK_SIZE / 4];
uint8_t screen_recv_buf[SCREEN_COUNT][ENCODE_BUFFER_COUNT][PLANE_COUNT][RP_SCREEN_SPLIT_COUNT][400 * 240 * 16 / 15];
uint8_t *screen_recv_buf_head[SCREEN_COUNT][ENCODE_BUFFER_COUNT][PLANE_COUNT][RP_SCREEN_SPLIT_COUNT];
uint8_t screen_recv_done[SCREEN_COUNT][ENCODE_BUFFER_COUNT][PLANE_COUNT];
uint8_t screen_split_recv_done[SCREEN_COUNT][ENCODE_BUFFER_COUNT][PLANE_COUNT][2];

uint8_t screen_rgb_buf[SCREEN_COUNT][ENCODE_BUFFER_COUNT][RP_SCREEN_SPLIT_COUNT][400 * 240 * 3 * 16 / 15];
uint8_t *screen_rgb_buf_head[SCREEN_COUNT][ENCODE_BUFFER_COUNT][RP_SCREEN_SPLIT_COUNT];
uint8_t screen_rgb_done[SCREEN_COUNT][ENCODE_BUFFER_COUNT];

uint8_t frame_n[SCREEN_COUNT];

uint8_t image_me[SCREEN_COUNT][ENCODE_BUFFER_COUNT][COMP_COUNT][400 * 240];

#define RECV_BUF_SIZE (2000)
uint8_t recv_buf[RECV_BUF_SIZE];
uint8_t *recv_buf_head;

int decode_image(uint8_t *dst, int, const uint8_t *src, int src_size, int w, int h, int bpp) {
  JLSState state = { 0 };
  state.bpp = bpp;
  ff_jpegls_reset_coding_parameters(&state, 0);
  ff_jpegls_init_state(&state);

  int ret, t;

  GetBitContext s;
  ret = init_get_bits8(&s, src, src_size);
  if (ret < 0)
  {
      return ret;
  }

  uint8_t *zero, *last, *cur;
  zero = calloc(h, 1);
  if (!zero)
  {
      return -1;
  }
  last = zero;
  cur = dst;

  int i;
  t = 0;
  for (i = 0; i < w; ++i) {
      ret = ls_decode_line(&state, &s, last, cur, t, h);
      if (ret < 0)
      {
          fprintf(stderr, "ls_decode_line error at col %d\n", i);
          free(zero);
          return ret;
      }
      t = last[0];
      last = cur;
      cur += h;
  }

  free(zero);

  return w * h;
}

enum {
  CORNER_TOP_LEFT,
  CORNER_BOT_LEFT,
  CORNER_BOT_RIGHT,
  CORNER_TOP_RIGHT,
  CORNER_COUNT,
};

static void interpolate_me(const s8 *me_x_vec[CORNER_COUNT], const s8 *me_y_vec[CORNER_COUNT], int half_range, int scale_log2, int block_size, int block_size_log2, int i, int j, s8 *x, s8 *y) {
  int step_total = block_size * 2;
  int step_base = 1;
  int step = 2;

  int x_left = i * step + step_base;
  int x_right = step_total - x_left;

  int y_top = j * step + step_base;
  int y_bot = step_total - y_top;

  int rshift_scale = (block_size_log2 + 1) * 2 + 2;

  int x_unscaled =
    ((int)*me_x_vec[CORNER_TOP_LEFT] - half_range) * x_left * y_top +
    ((int)*me_x_vec[CORNER_BOT_LEFT] - half_range) * x_left * y_bot +
    ((int)*me_x_vec[CORNER_BOT_RIGHT] - half_range) * x_right * y_bot +
    ((int)*me_x_vec[CORNER_TOP_RIGHT] - half_range) * x_right * y_top;
  *x = srshift_to_even(x_unscaled, rshift_scale) << scale_log2;

  int y_unscaled =
    ((int)*me_y_vec[CORNER_TOP_LEFT] - half_range) * x_left * y_top +
    ((int)*me_y_vec[CORNER_BOT_LEFT] - half_range) * x_left * y_bot +
    ((int)*me_y_vec[CORNER_BOT_RIGHT] - half_range) * x_right * y_bot +
    ((int)*me_y_vec[CORNER_TOP_RIGHT] - half_range) * x_right * y_top;
  *y = srshift_to_even(y_unscaled, rshift_scale) << scale_log2;
}

#define DO_DIFF_UNSIGNED() do { \
  if (RP_ENCODE_STATIC_LQ) { \
    *dst++ = (u8)((u8)(*ref++ << (8 - bpp)) + (u8)((s8)*cur++ << (8 - bpp))) >> (8 - bpp); \
  } else { \
    *dst++ = *ref + ((s8)*cur * (1 << (bpp_prev - bpp))); \
    ++ref, ++cur; \
  } \
} while (0)

#define DO_DIFF_SIGNED() do { \
  if (RP_ENCODE_STATIC_LQ) { \
    *dst++ = (((s8)*ref++ << (8 - bpp)) + ((s8)*cur++ << (8 - bpp))) >> (8 - bpp); \
  } else { \
    *dst++ = (s8)*ref + ((s8)*cur * (1 << (bpp_prev - bpp))); \
    ++ref, ++cur; \
  } \
} while (0)

#define DO_DIFF() do { \
  if (unsigned_signed == 0) { \
    DO_DIFF_UNSIGNED(); \
  } else { \
    DO_DIFF_SIGNED(); \
  } \
} while (0)

void diff_image(uint8_t *dst, const uint8_t *ref, const uint8_t *cur, int w, int h, int bpp, int bpp_prev UNUSED_ATTR, u8 unsigned_signed) {
  for (int i = 0; i < w; ++i) {
    for (int j = 0; j < h; ++j) {
      DO_DIFF();
    }
  }
}

void select_image(uint8_t *dst, const uint8_t *ref, const uint8_t *cur, const int8_t *me_x, int scale_log2, int dsx, int w, int h, int bpp, int bpp_prev UNUSED_ATTR, u8 unsigned_signed) {
  if (ntr_me_downscale)
    ++scale_log2;

  // u8 me_block_size_log2 = av_ceil_log2(ntr_me_block_size) + scale_log2;
  u8 me_block_size = (ntr_me_block_size << scale_log2) / dsx;
  // u8 me_block_size_mask = (1 << me_block_size_log2) - 1;

  u8 block_x_n = w / me_block_size;
  u8 block_y_n = h / me_block_size;
  u8 x_off = (w % me_block_size) >> 1;
  u8 y_off = (h % me_block_size) >> 1;

  // u8 me_bpp = av_ceil_log2(ntr_me_search_param * 2 + 1);
  // u8 me_bpp_half_range = (1 << me_bpp) >> 1;

  for (int i = 0; i < w; ++i) {
    int me_i = RP_MAX(0, i - x_off) / me_block_size;
    me_i = RP_MIN(me_i, block_x_n - 1);
    for (int j = 0; j < h; ++j) {
      int me_j = RP_MAX(0, j - y_off) / me_block_size;
      me_j = RP_MIN(me_j, block_y_n - 1);

      int x = me_x[me_i * block_y_n + me_j];

      if (x) {
        if (RP_ENCODE_STATIC_LQ) {
          ref++;
          *dst++ = *cur++;
        } else {
          ref++;
          if (unsigned_signed == 0) {
            *dst++ = *cur++;
          } else {
            *dst++ = (s8)*cur++;
          }
        }
      } else {
        DO_DIFF();
      }
    }
  }
}

void me_image(uint8_t *dst, const uint8_t *ref, const uint8_t *cur, const int8_t *me_x, const int8_t *me_y, int scale_log2, int dsx, int w, int h, int bpp) {
  if (ntr_me_downscale)
    ++scale_log2;

  u8 me_block_size_log2 = av_ceil_log2(ntr_me_block_size) + scale_log2;
  u8 me_block_size = (ntr_me_block_size << scale_log2) / dsx;
  // u8 me_block_size_mask = (1 << me_block_size_log2) - 1;

  u8 block_x_n = w / me_block_size;
  u8 block_y_n = h / me_block_size;
  u8 x_off = (w % me_block_size) >> 1;
  u8 y_off = (h % me_block_size) >> 1;

  u8 me_bpp = av_ceil_log2(ntr_me_search_param * 2 + 1);
  u8 me_bpp_half_range = (1 << me_bpp) >> 1;

  if (!ntr_me_interpolate) {
    for (int i = 0; i < w; ++i) {
      int me_i = RP_MAX(0, i - x_off) / me_block_size;
      me_i = RP_MIN(me_i, block_x_n - 1);
      for (int j = 0; j < h; ++j) {
        int me_j = RP_MAX(0, j - y_off) / me_block_size;
        me_j = RP_MIN(me_j, block_y_n - 1);

        int x = me_x[me_i * block_y_n + me_j];
        int y = me_y[me_i * block_y_n + me_j];

        if (!x && !y) {
          ref++;
          *dst++ = *cur++;
        } else {
          x = (int)(x - (s16)me_bpp_half_range) << scale_log2;
          y = (int)(y - (s16)me_bpp_half_range) << scale_log2;

          x = av_clip(x, -i, w - i - 1);
          y = av_clip(y, -j, h - j - 1);

          const u8 *ref_est = ref++ + (int)x * h + (int)y;
          *dst++ = (u8)((u8)(*ref_est << (8 - bpp)) + (u8)(*cur++ << (8 - bpp)) - 128) >> (8 - bpp);
        }
      }
    }
  } else {
    x_off += me_block_size >> 1;
    y_off += me_block_size >> 1;
    const s8 *me_x_col_vec[CORNER_COUNT];
    const s8 *me_y_col_vec[CORNER_COUNT];
    for (int i = 0; i < CORNER_COUNT; ++i) {
      me_x_col_vec[i] = me_x;
      me_y_col_vec[i] = me_y;
    }
    for (int i = 0; i < w; ++i) {
      int i_off = (i - x_off) % me_block_size;
      if (i_off == 0) {
        if (i < w - x_off - 1) {
          me_x_col_vec[CORNER_BOT_LEFT] += block_y_n;
          me_x_col_vec[CORNER_BOT_RIGHT] += block_y_n;

          me_y_col_vec[CORNER_BOT_LEFT] += block_y_n;
          me_y_col_vec[CORNER_BOT_RIGHT] += block_y_n;
        }
        if (i > x_off) {
          me_x_col_vec[CORNER_TOP_LEFT] += block_y_n;
          me_x_col_vec[CORNER_TOP_RIGHT] += block_y_n;

          me_y_col_vec[CORNER_TOP_LEFT] += block_y_n;
          me_y_col_vec[CORNER_TOP_RIGHT] += block_y_n;
        }
      }

      const s8 *me_x_vec[CORNER_COUNT];
      const s8 *me_y_vec[CORNER_COUNT];
      memcpy(me_x_vec, me_x_col_vec, sizeof(me_x_vec));
      memcpy(me_y_vec, me_y_col_vec, sizeof(me_y_vec));
      for (int j = 0; j < h; ++j) {
        int j_off = (j - y_off) % me_block_size;
        if (j_off == 0) {
          if (i == 0) {
          }
          if (j < h - y_off - 1) {
            ++me_x_vec[CORNER_BOT_LEFT];
            ++me_x_vec[CORNER_BOT_RIGHT];

            ++me_y_vec[CORNER_BOT_LEFT];
            ++me_y_vec[CORNER_BOT_RIGHT];
          }
          if (j > y_off) {
            ++me_x_vec[CORNER_TOP_LEFT];
            ++me_x_vec[CORNER_TOP_RIGHT];

            ++me_y_vec[CORNER_TOP_LEFT];
            ++me_y_vec[CORNER_TOP_RIGHT];
          }
        }
        s8 x, y;
        interpolate_me(me_x_vec, me_y_vec, me_bpp_half_range, scale_log2, me_block_size, me_block_size_log2, i_off, j_off, &x, &y);

        x = av_clip(x, -i, w - i - 1);
        y = av_clip(y, -j, h - j - 1);

        const u8 *ref_est = ref++ + (int)x * h + (int)y;
        *dst++ = (u8)((u8)(*ref_est << (8 - bpp)) + (u8)(*cur++ << (8 - bpp)) - 128) >> (8 - bpp);
      }
    }
  }
}

static uint8_t zstd_pred_med(uint8_t Rb, uint8_t Ra, uint8_t Rc) {
  uint8_t minx;
  uint8_t maxx;

  if (Rb > Ra) {
    minx = Ra;
    maxx = Rb;
  } else {
    maxx = Ra;
    minx = Rb;
  }
  if (Rc >= maxx)
    return minx;
  else if (Rc <= minx)
    return maxx;
  else
    return Ra + Rb - Rc;
}

int lz4_jls_decode(uint8_t *dst, int dst_x, int dst_y, const uint8_t *src, int src_size) {
  LZ4_streamDecode_t lz4StreamDecode_body;
  LZ4_streamDecode_t* lz4StreamDecode = &lz4StreamDecode_body;
  LZ4_setStreamDecode(lz4StreamDecode, NULL, 0);

  uint8_t (*pred_buf)[dst_x] = malloc(dst_x * 2);
  if (!pred_buf) {
    return -1;
  }
  uint8_t pred_buf_index = 0;

  for (int j = 0; j < dst_y; ++j) {
    uint8_t cmpBytes = 0;

    if (src_size < (int)sizeof(int)) {
      fprintf(stderr, "not enough input\n");
      free(pred_buf);
      return -1;
    }

    cmpBytes = *(const uint8_t *)src;
    src += sizeof(uint8_t);
    src_size -= sizeof(uint8_t);

    if (src_size < cmpBytes) {
      fprintf(stderr, "not enough input data at col %d: %d (%d)\n", j, cmpBytes, src_size);
      free(pred_buf);
      return -1;
    }

    uint8_t* const pred_buf0 = pred_buf[pred_buf_index];
    const int decBytes = LZ4_decompress_safe_continue(lz4StreamDecode, (const char *)src, (char *)pred_buf0, cmpBytes, dst_x);
    if(decBytes <= 0) {
      fprintf(stderr, "LZ4_decompress_safe_continue failed\n");
      free(pred_buf);
      return -1;
    }
    src += cmpBytes;
    src_size -= cmpBytes;

    uint8_t *dst_row = dst + j * dst_x;
    for (int i = 0; i < dst_x; ++i) {
      uint8_t Rb = j > 0 ? dst_row[i - dst_x] : 0, Ra = i > 0 ? dst_row[i - 1] : Rb, Rc = j > 0 && i > 0 ? dst_row[i - dst_x - 1] : Rb;
      uint8_t pred = zstd_pred_med(Rb, Ra, Rc);
      dst_row[i] = pred_buf0[i] + pred;
    }

    pred_buf_index = (pred_buf_index + 1) % 2;
  }

  free(pred_buf);

  return dst_x * dst_y;
}

int huff_jls_decode_2(uint8_t *dst, uint8_t *dst_2, int dst_x, int dst_y, const uint8_t *src, int src_size) {
  if (src_size < (int)sizeof(u32))
    return -1;

  int dst_size_1 = dst_x * dst_y, dst_size = dst_size_1;
  if (dst_2)
    dst_size *= 2;

  int rle_size = *(u32 *)src;
  if (!rle_size || rle_size < 0 || rle_size > RLE_MAX_COMPRESSED_SIZE(dst_size)) {
    fprintf(stderr, "rle_size error: %d (%d)\n", rle_size, RLE_MAX_COMPRESSED_SIZE(dst_size));
    return -1;
  }

  src += sizeof(u32);
  src_size -= sizeof(u32);

  uint8_t *rle_src = malloc(rle_size);
  if (!rle_src)
    return -1;

  int ret;
  ret = huffman_decode(rle_src, rle_size, src, src_size);

  if (ret < 0) {
    fprintf(stderr, "huffman_decode failed\n");
    return ret;
  }
  if (ret != rle_size) {
    fprintf(stderr, "huffman_decode mismatch %d (%d)\n", ret, rle_size);
    // return -1;
  }

  ret = rle_decode(dst, dst_size, rle_src, ret);
  if (ret < 0) {
    fprintf(stderr, "rle_decode failed: %d (%d)\n", ret, dst_size);
    free(rle_src);
    return ret;
  } else if (ret != dst_size) {
    fprintf(stderr, "rle_decode mismatch %d (%d %dx%d)\n", ret, dst_size, dst_x, dst_y);
    free(rle_src);
    return ret;
  }

  if (dst_2)
    memcpy(dst_2, dst + dst_size_1, dst_size_1);

  uint8_t *out_2[] = {dst, dst_2};

  for (int k = 0; k < (int)(sizeof(out_2) / sizeof(*out_2)) && out_2[k]; ++k) {
    for (int j = 0; j < dst_y; ++j) {
      uint8_t *dst_row = out_2[k] + j * dst_x;
      for (int i = 0; i < dst_x; ++i) {
        uint8_t Rb = j > 0 ? dst_row[i - dst_x] : 0, Ra = i > 0 ? dst_row[i - 1] : Rb, Rc = j > 0 && i > 0 ? dst_row[i - dst_x - 1] : Rb;
        uint8_t pred = zstd_pred_med(Rb, Ra, Rc);
        dst_row[i] = dst_row[i] + pred;
      }
    }
  }

  free(rle_src);
  return dst_size_1;
}

int huff_jls_decode(uint8_t *dst, int dst_x, int dst_y, const uint8_t *src, int src_size) {
    return huff_jls_decode_2(dst, 0, dst_x, dst_y, src, src_size);
}

int zstd_jls_decode(uint8_t *dst, int dst_x, int dst_y, const uint8_t *src, int src_size) {
  size_t ret;

  size_t ws_size = ZSTD_estimateDStreamSize_fromFrame(src, src_size);
  if (ZSTD_isError(ws_size)) {
    fprintf(stderr, "ZSTD_estimateDStreamSize_fromFrame failed: %d.\n", (int)ws_size);
    return -1;
  }

  void *ws = malloc(ws_size);

  ZSTD_DStream *dstream = ZSTD_initStaticDStream(ws, ws_size);
  if (!dstream) {
    fprintf(stderr, "ZSTD_initStaticDStream error\n");
    free(ws);
    return -1;
  }

  ret = ZSTD_initDStream(dstream);
  if (ZSTD_isError(ret)) {
    fprintf(stderr, "ZSTD_initDStream error: %d\n", (int)ret);
    free(ws);
    return -1;
  }

  ZSTD_inBuffer cur_input = {
    .src = src,
    .size = src_size,
  };

  uint8_t *pred_buf = malloc(dst_x);
  if (!pred_buf) {
    free(ws);
    return -1;
  }

  for (int j = 0; j < dst_y; ++j) {
    ZSTD_outBuffer cur_output = {
      .dst = pred_buf,
      .size = dst_x,
    };

    while (cur_output.pos < cur_output.size && cur_input.pos < cur_input.size) {
      ret = ZSTD_decompressStream(dstream, &cur_output, &cur_input);
      if (ZSTD_isError(ret)) {
        fprintf(stderr, "ZSTD_decompressStream error: %d\n", (int)ret);
        free(ws);
        free(pred_buf);
        return -1;
      }
    }

    if (cur_output.pos < cur_output.size && cur_input.pos >= cur_input.size) {
      fprintf(stderr, "not enough input\n");
      free(ws);
      free(pred_buf);
      return -1;
    }

    uint8_t *dst_row = dst + j * dst_x;
    for (int i = 0; i < dst_x; ++i) {
      uint8_t Rb = j > 0 ? dst_row[i - dst_x] : 0, Ra = i > 0 ? dst_row[i - 1] : Rb, Rc = j > 0 && i > 0 ? dst_row[i - dst_x - 1] : Rb;
      uint8_t pred = zstd_pred_med(Rb, Ra, Rc);
      dst_row[i] = pred_buf[i] + pred;
    }
  }

  free(ws);
  free(pred_buf);

  return dst_x * dst_y;
}

int bits_array_decode(uint8_t *dst, int dst_x, int dst_y, const uint8_t *src, int src_size) {
  int ret;
  GetBitContext s;
  ret = init_get_bits8(&s, src, src_size);
  if (ret < 0) {
    fprintf(stderr, "bits_array_decode init_get_bits8 error\n");
    return 0;
  }

  for (int i = 0; i < dst_x; ++i) {
    for (int j = 0; j < dst_y; ++j) {
      if (get_bits_left(&s) <= 0) {
        fprintf(stderr, "bits_array_decode not enough data\n");
        return 0;
      }
      *dst++ = get_bits1(&s);
    }
  }

  return dst_x * dst_y;
}

int receiving;
int handle_recv(uint8_t *buf, int size)
{
  // return 0;

  if (recv_state == RECV_STATE_HEADER) {
    if (leftover_size + size < (intptr_t)sizeof(struct rp_send_data_header)) {
      memcpy(buf_leftover + leftover_size, buf, size);
      leftover_size += size;
      return 0;
    }

    // fprintf(stderr, "Receiving with leftover %d, buf size: %d\n", leftover_size, size),

    memcpy(&send_header, buf_leftover, leftover_size);
    memcpy((char *)&send_header + leftover_size, buf, sizeof(struct rp_send_data_header) - leftover_size);
    buf += sizeof(struct rp_send_data_header) - leftover_size;
    size -= sizeof(struct rp_send_data_header) - leftover_size;

    if (send_header.type_data == RP_SEND_HEADER_TYPE_CONF) {
      struct rp_send_info_header send_info_header;
      memcpy(&send_info_header, &send_header, sizeof(struct rp_send_info_header));
      ntr_downscale_uv = send_info_header.downscale_uv;
      ntr_yuv_option = send_info_header.yuv_option;
      ntr_color_transform_hp = send_info_header.color_transform_hp;
      ntr_encoder_which = send_info_header.encoder_which;
      ntr_encode_split_image = send_info_header.encode_split_image;
      ntr_me_enabled = send_info_header.me_enabled;
      ntr_me_downscale = send_info_header.me_downscale;
      ntr_me_block_size = RP_ME_MIN_BLOCK_SIZE << send_info_header.me_block_size;
      ntr_me_search_param = send_info_header.me_search_param + RP_ME_MIN_SEARCH_PARAM;
      ntr_me_interpolate = send_info_header.me_interpolate;

      if (size)
        return handle_recv(buf, size);
      else
        return 0;
    }

    recv_state = RECV_STATE_DATA;
    recv_data_remain = send_header.data_size;
    leftover_size = 0;
    recv_buf_head = recv_buf;
  }

  if (recv_data_remain > RECV_BUF_SIZE) {
    fprintf(stderr, "too much for recv_buf %d\n", recv_data_remain);
    return -1;
  }

  if (recv_data_remain <= (uintptr_t)size) {
    memcpy(recv_buf_head, buf, recv_data_remain);
    recv_buf_head += recv_data_remain;
    // fprintf(stderr, "Done\n");
    buf += recv_data_remain;
    size -= recv_data_remain;
    recv_data_remain = 0;
    recv_state = RECV_STATE_HEADER;

    if (receiving) {
      int top_bot = send_header.top_bot;
      int p_frame = send_header.p_frame;
      int left_right = send_header.left_right;

      int pos = -1;
      int plane;
      int comp;

      int encoder_rgb = ntr_encoder_which >= RP_ENCODER_JLS_COUNT;
      int uv_comp = send_header.plane_type == 0 && send_header.plane_comp == UV_COMP;

      // fprintf(stderr, "Receiving: screen %d, frame_n = %d, left_right = %d, plane_type = %d, plane_comp = %d, bpp = %d, size = %d, done = %d\n", top_bot, send_header.frame_n, send_header.left_right, send_header.plane_type, send_header.plane_comp, send_header.bpp, send_header.data_size, send_header.data_end);

      if (uv_comp) {
        plane = U_COMP;
      } else {
        plane = send_header.plane_type == 0 ? send_header.plane_comp : send_header.plane_comp + COMP_COUNT;
        if (plane >= PLANE_COUNT) {
          fprintf(stderr, "plane_comp error\n");
          goto final;
        }
      }
      comp = plane < COMP_COUNT ? plane : COMP_COUNT;

      for (int i = 0; i < ENCODE_BUFFER_COUNT; ++i) {
        if (screen_frame_n[top_bot][i] == send_header.frame_n) {
          pos = i;
          break;
        }
      }
      if (pos < 0) {
        pos = screen_pos[top_bot]++;
        screen_pos[top_bot] %= ENCODE_BUFFER_COUNT;
        screen_frame_n[top_bot][pos] = send_header.frame_n;

        for (int k = 0; k < RP_SCREEN_SPLIT_COUNT; ++k) {
          if (encoder_rgb) {
            screen_recv_buf_head[top_bot][pos][Y_DATA][k] = 0;
            screen_recv_buf_head[top_bot][pos][U_DATA][k] = 0;
            screen_recv_buf_head[top_bot][pos][V_DATA][k] = 0;
            screen_recv_buf_head[top_bot][pos][ME_X_DATA][k] = 0;
            screen_recv_buf_head[top_bot][pos][ME_Y_DATA][k] = 0;

            screen_rgb_buf_head[top_bot][pos][k] = screen_rgb_buf[top_bot][pos][k];
          } else {
            screen_recv_buf_head[top_bot][pos][Y_DATA][k] = screen_recv_buf[top_bot][pos][Y_DATA][k];
            screen_recv_buf_head[top_bot][pos][U_DATA][k] = screen_recv_buf[top_bot][pos][U_DATA][k];
            screen_recv_buf_head[top_bot][pos][V_DATA][k] = screen_recv_buf[top_bot][pos][V_DATA][k];
            screen_recv_buf_head[top_bot][pos][ME_X_DATA][k] = screen_recv_buf[top_bot][pos][ME_X_DATA][k];
            screen_recv_buf_head[top_bot][pos][ME_Y_DATA][k] = screen_recv_buf[top_bot][pos][ME_Y_DATA][k];

            screen_rgb_buf_head[top_bot][pos][k] = 0;
          }
        }

        screen_recv_done[top_bot][pos][Y_DATA] = 0;
        screen_recv_done[top_bot][pos][U_DATA] = 0;
        screen_recv_done[top_bot][pos][V_DATA] = 0;
        screen_recv_done[top_bot][pos][ME_X_DATA] = 0;
        screen_recv_done[top_bot][pos][ME_Y_DATA] = 0;

        for (int j = 0; j < PLANE_COUNT; ++j) {
          for (int k = 0; k < 2; ++k) {
            screen_split_recv_done[top_bot][pos][j][k] = 0;
          }
        }

        screen_rgb_done[top_bot][pos] = 0;

        screen_done[top_bot][pos] = 0;
        for (int k = 0; k < RP_SCREEN_SPLIT_COUNT; ++k) {
          screen_data_size[top_bot][pos][k] = 0;
          screen_data_delay_between_packet[top_bot][pos][k] = 0;
          screen_data_last_packet_time[top_bot][pos][k] = iclock();
        }
        screen_buf_valid[top_bot][pos] = -1;
      }

      {
        uint32_t screen_data_packet_time = iclock();
        uint32_t screen_data_delay_from_last_packet = screen_data_packet_time - screen_data_last_packet_time[top_bot][pos][send_header.left_right];
        if (screen_data_delay_from_last_packet > screen_data_delay_between_packet[top_bot][pos][send_header.left_right]) {
          screen_data_delay_between_packet[top_bot][pos][send_header.left_right] = screen_data_delay_from_last_packet;
        }
        screen_data_last_packet_time[top_bot][pos][send_header.left_right] = screen_data_packet_time;
      }

      if (RP_HUFF_SHARE_STATS) {
        if (ntr_encode_split_image && comp < COMP_COUNT) {
          if (send_header.data_stats) {
            screen_data_size[top_bot][pos][RP_SCREEN_SPLIT_LEFT] += send_header.data_size - 256;
          } else {
            screen_data_size[top_bot][pos][RP_SCREEN_SPLIT_RIGHT] += send_header.data_size;

            if (screen_recv_buf_head[top_bot][pos][plane][left_right] == screen_recv_buf[top_bot][pos][plane][left_right]) {
              screen_recv_buf_head[top_bot][pos][plane][left_right] += 256;
            }
          }
        }
      } else {
        screen_data_size[top_bot][pos][send_header.left_right] += send_header.data_size;
      }

      if (encoder_rgb) {
        if (!screen_rgb_buf_head[top_bot][pos][left_right])
          goto final;
        if (recv_buf_head - recv_buf >
          (intptr_t)sizeof(screen_rgb_buf[top_bot][pos][left_right]) -
            (screen_rgb_buf_head[top_bot][pos][left_right] - screen_rgb_buf[top_bot][pos][left_right]
          )
        ) {
          screen_buf_valid[top_bot][pos] = 0;
          fprintf(stderr, "buffer overflow %d %d plane %d, size so far %d, size incoming %d\n", top_bot, send_header.frame_n, plane,
            (int)(screen_rgb_buf_head[top_bot][pos][left_right] - screen_rgb_buf[top_bot][pos][left_right]),
            (int)(recv_buf_head - recv_buf));
          goto final;
        }
        memcpy(screen_rgb_buf_head[top_bot][pos][left_right], recv_buf, recv_buf_head - recv_buf);
        screen_rgb_buf_head[top_bot][pos][left_right] += recv_buf_head - recv_buf;
      } else {
        if (!screen_recv_buf_head[top_bot][pos][plane][left_right])
          goto final;
        if (recv_buf_head - recv_buf >
          (intptr_t)sizeof(screen_recv_buf[top_bot][pos][plane][left_right]) -
            (screen_recv_buf_head[top_bot][pos][plane][left_right] - screen_recv_buf[top_bot][pos][plane][left_right]
          )
        ) {
          screen_buf_valid[top_bot][pos] = 0;
          fprintf(stderr, "buffer overflow %d %d plane %d, size so far %d, size incoming %d\n", top_bot, send_header.frame_n, plane,
            (int)(screen_recv_buf_head[top_bot][pos][plane][left_right] - screen_recv_buf[top_bot][pos][plane][left_right]),
            (int)(recv_buf_head - recv_buf));
          goto final;
        }
        memcpy(screen_recv_buf_head[top_bot][pos][plane][left_right], recv_buf, recv_buf_head - recv_buf);
        // if (comp < COMP_COUNT && (
        //   screen_recv_buf_head[top_bot][pos][plane][left_right] == screen_recv_buf[top_bot][pos][plane][left_right] ||
        //   screen_recv_buf_head[top_bot][pos][plane][left_right] == screen_recv_buf[top_bot][pos][plane][left_right] + 256
        // ))
        //   fprintf(stderr, "recv_buf %d %d %d %d: %d\n", top_bot, pos, plane, left_right, *(int *)recv_buf);
        screen_recv_buf_head[top_bot][pos][plane][left_right] += recv_buf_head - recv_buf;
      }

      if (!send_header.data_end)
        goto final;

      int split_image_count = ntr_encode_split_image ? RP_SCREEN_SPLIT_COUNT : 1;
      if (encoder_rgb) {
        ++screen_rgb_done[top_bot][pos];
        if (screen_rgb_done[top_bot][pos] < split_image_count)
          goto final;
      } else {
        // if (plane < COMP_COUNT)
        //   fprintf(stderr, "screen_recv_buf %d %d %d %d: %d %d\n", top_bot, pos, plane, left_right, *(int *)(screen_recv_buf[top_bot][pos][plane][left_right] + (send_header.data_stats ? 0 : 256)), send_header.data_stats);
        if (screen_split_recv_done[top_bot][pos][plane][left_right]) {
          fprintf(stderr, "screen_split_recv_done %d %d %d %d already set\n", top_bot, pos, plane, left_right);
          goto final;
        }
        screen_split_recv_done[top_bot][pos][plane][left_right] = 1;
        if (uv_comp) {
          ++screen_recv_done[top_bot][pos][V_COMP];
          ++screen_recv_done[top_bot][pos][U_COMP];
          if (screen_recv_done[top_bot][pos][V_COMP] < split_image_count || screen_recv_done[top_bot][pos][U_COMP] < split_image_count)
            goto final;
        } else {
          ++screen_recv_done[top_bot][pos][plane];
          if (screen_recv_done[top_bot][pos][plane] < split_image_count)
            goto final;
        }
      }
      // if (plane < COMP_COUNT)
      //   fprintf(stderr, "screen_recv_buf %d %d %d %d: %d %d\n", top_bot, pos, plane, !left_right, *(int *)(screen_recv_buf[top_bot][pos][plane][!left_right] + (send_header.data_stats ? 256 : 0)), send_header.data_stats);

      if (ntr_encode_split_image && comp < COMP_COUNT) {
        if (!(screen_split_recv_done[top_bot][pos][plane][!left_right] && screen_split_recv_done[top_bot][pos][plane][left_right])) {
          fprintf(stderr, "screen_split_recv_done error\n");
          goto final;
        }
        if (RP_HUFF_SHARE_STATS) {
          if (send_header.data_stats) {
            memcpy(screen_recv_buf[top_bot][pos][plane][!left_right], screen_recv_buf[top_bot][pos][plane][!left_right] + 256, sizeof(u32));
            memcpy(screen_recv_buf[top_bot][pos][plane][!left_right] + sizeof(u32), screen_recv_buf[top_bot][pos][plane][left_right] + sizeof(u32), 256);
          } else {
            memcpy(screen_recv_buf[top_bot][pos][plane][left_right], screen_recv_buf[top_bot][pos][plane][left_right] + 256, sizeof(u32));
            memcpy(screen_recv_buf[top_bot][pos][plane][left_right] + sizeof(u32), screen_recv_buf[top_bot][pos][plane][!left_right] + sizeof(u32), 256);
          }
        }
      }

      int w_orig_full = top_bot == SCREEN_TOP ? 400 : 320, w = w_orig_full;
      if (ntr_encode_split_image)
        w /= 2;
      int w_orig = w;
      int h = 240, h_orig = h;

      u8 me_block_size_log2 = av_ceil_log2(ntr_me_block_size);
      // u8 me_bpp = av_ceil_log2(ntr_me_search_param * 2 + 1);
      // u8 me_bpp_half_range = (1 << me_bpp) >> 1;

      int scale_log2_offset = ntr_me_downscale == 0 ? 0 : 1;
      int scale_log2 = 1 + scale_log2_offset;
      u8 block_size_log2 = me_block_size_log2 + scale_log2;

      int me_w = w >> block_size_log2;
      int me_w_full = w_orig_full >> block_size_log2;
      int me_h = h >> block_size_log2;

      int dsx = ntr_downscale_uv + 1;
      if (ntr_downscale_uv && (plane == U_DATA || plane == V_DATA)) {
        w = DSX_DIM(w, dsx);
        h = DSX_DIM(h, dsx);
      }

      if (plane == ME_X_DATA || plane == ME_Y_DATA) {
        w = me_w;
        h = me_h;
      }

      int bpp = send_header.bpp ? send_header.bpp : 8;

      // fprintf(stderr, "Decoding: screen %d, frame_n = %d, left_right = %d, stats = %d, plane = %d, comp = %d, w = %d, h = %d, bpp = %d, size = %d\n", top_bot, send_header.frame_n, send_header.left_right, send_header.data_stats, plane, comp, w, h, bpp,
      //   encoder_rgb ?
      //     (int)(screen_rgb_buf_head[top_bot][pos][left_right] - screen_rgb_buf[top_bot][pos][left_right]) :
      //     (int)(screen_recv_buf_head[top_bot][pos][plane][left_right] - screen_recv_buf[top_bot][pos][plane][left_right]));
      int ret = 0;
      if (encoder_rgb) {
        if (ntr_encoder_which == RP_ENCODER_IMAGE_ZERO) {
          ret = izDecodeImageRGB(screen_decoded[top_bot], screen_rgb_buf[top_bot][pos][RP_SCREEN_SPLIT_LEFT], h_orig, w_orig);
          // fprintf(stderr, "izDecodeImageRGB: %d (%d)\n", ret, (int)(screen_rgb_buf_head[top_bot][pos] - screen_rgb_buf[top_bot][pos]));
          ret = ret == screen_rgb_buf_head[top_bot][pos][RP_SCREEN_SPLIT_LEFT] - screen_rgb_buf[top_bot][pos][RP_SCREEN_SPLIT_LEFT];
          if (ret && ntr_encode_split_image) {
            ret = izDecodeImageRGB(screen_decoded[top_bot] + w_orig * h_orig, screen_rgb_buf[top_bot][pos][RP_SCREEN_SPLIT_RIGHT], h_orig, w_orig);
            ret = ret == screen_rgb_buf_head[top_bot][pos][RP_SCREEN_SPLIT_RIGHT] - screen_rgb_buf[top_bot][pos][RP_SCREEN_SPLIT_RIGHT];
          }
        } else {
          for (int k = 0; k < (ntr_encode_split_image ? RP_SCREEN_SPLIT_COUNT : 1); ++k) {
            struct jpeg_decompress_struct cinfo;
            struct jpeg_error_mgr jerr;
            cinfo.err = jpeg_std_error(&jerr);
            jpeg_create_decompress(&cinfo);
            jpeg_mem_src(&cinfo, screen_rgb_buf[top_bot][pos][k], screen_rgb_buf_head[top_bot][pos][k] - screen_rgb_buf[top_bot][pos][k]);
            ret = jpeg_read_header(&cinfo, TRUE);
            if (ret == JPEG_HEADER_OK) {
              jpeg_start_decompress(&cinfo);
              // fprintf(stderr, "jpeg_read_header: %d %d\n", (int)cinfo.output_width, (int)cinfo.output_height);
              if ((int)cinfo.output_width == h_orig && (int)cinfo.output_height == w_orig) {
                while (cinfo.output_scanline < cinfo.output_height) {
                  uint8_t *buffer = screen_decoded[top_bot] + w_orig * h_orig * k + cinfo.output_scanline * cinfo.output_width * 3;
                  jpeg_read_scanlines(&cinfo, &buffer, 1);
                }
                jpeg_finish_decompress(&cinfo);
                jpeg_destroy_decompress(&cinfo);
                ret = 1;
              } else {
                jpeg_destroy_decompress(&cinfo);
                ret = 0;
              }
            } else {
              jpeg_destroy_decompress(&cinfo);
              ret = 0;
            }
          }
        }
      } else if (send_header.bpp == 1) {
        if (plane != ME_X_DATA) {
          ret = 0;
        } else {
          for (int k = 0; k < (ntr_encode_split_image ? RP_SCREEN_SPLIT_COUNT : 1); ++k) {
            ret = bits_array_decode((u8 *)screen_me_buf[top_bot][pos][plane - COMP_COUNT] + w * h * k, w, h,
              screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k]) == w * h;
            if (!ret)
              break;
          }
        }
      } else if (ntr_encoder_which < RP_ENCODER_JLS_USE_LUT_COUNT) {
        for (int k = 0; k < (ntr_encode_split_image ? RP_SCREEN_SPLIT_COUNT : 1); ++k) {
          if (comp < COMP_COUNT) {
            ret = ffmpeg_jls_decode(screen_buf[top_bot][pos][comp] + w * h * k,
              h, w, h, screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k], bpp) == w * h;
          } else {
            ret = ffmpeg_jls_decode((u8 *)screen_me_buf[top_bot][pos][plane - COMP_COUNT] + w * h * k,
              h, w, h, screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k], bpp) == w * h;
          }
          if (!ret)
            break;
        }
      } else if (ntr_encoder_which == RP_ENCODER_LZ4_JLS) {
        for (int k = 0; k < (ntr_encode_split_image ? RP_SCREEN_SPLIT_COUNT : 1); ++k) {
          if (comp < COMP_COUNT) {
            ret = lz4_jls_decode(screen_buf[top_bot][pos][comp] + w * h * k,
              h, w, screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k]) == w * h;
          } else {
            ret = lz4_jls_decode((u8 *)screen_me_buf[top_bot][pos][plane - COMP_COUNT] + w * h * k,
              h, w, screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k]) == w * h;
          }
          if (!ret)
            break;
        }
      } else if (ntr_encoder_which == RP_ENCODER_HUFF_JLS) {
        for (int k = 0; k < (ntr_encode_split_image ? RP_SCREEN_SPLIT_COUNT : 1); ++k) {
          if (uv_comp) {
            ret = huff_jls_decode_2(screen_buf[top_bot][pos][U_COMP] + w * h * k, screen_buf[top_bot][pos][V_COMP] + w * h * k,
              h, w, screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k]) == w * h;
          } else if (comp < COMP_COUNT) {
            ret = huff_jls_decode(screen_buf[top_bot][pos][comp] + w * h * k,
              h, w, screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k]) == w * h;
          } else {
            ret = huff_jls_decode((u8 *)screen_me_buf[top_bot][pos][plane - COMP_COUNT] + w * h * k,
              h, w, screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k]) == w * h;
          }
          if (!ret)
            break;
        }
      } else if (ntr_encoder_which == RP_ENCODER_ZSTD_JLS) {
        for (int k = 0; k < (ntr_encode_split_image ? RP_SCREEN_SPLIT_COUNT : 1); ++k) {
          if (comp < COMP_COUNT) {
            ret = zstd_jls_decode(screen_buf[top_bot][pos][comp] + w * h * k,
              h, w, screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k]) == w * h;
          } else {
            ret = zstd_jls_decode((u8 *)screen_me_buf[top_bot][pos][plane - COMP_COUNT] + w * h * k,
              h, w, screen_recv_buf[top_bot][pos][plane][k], screen_recv_buf_head[top_bot][pos][plane][k] - screen_recv_buf[top_bot][pos][plane][k]) == w * h;
          }
          if (!ret)
            break;
        }
      } else {
        fprintf(stderr, "Unknown encoder\n");
        ret = 0;
      }
      if (ret
      ) {
        // fprintf(stderr, "success %d %d plane %d\n", top_bot, send_header.frame_n, plane);
        int frame_end = 1;
        if (encoder_rgb) {
          // done
        } else {
          if (uv_comp) {
            screen_bpp[top_bot][pos][V_COMP] = screen_bpp[top_bot][pos][U_COMP] = bpp;
          } else {
            if (comp < COMP_COUNT)
              screen_bpp[top_bot][pos][comp] = bpp;
          }

          for (int i = 0; i < (ntr_me_enabled == 1 && p_frame ? PLANE_COUNT : COMP_COUNT + (ntr_me_enabled == 3 && p_frame)); ++i) {
            if (screen_recv_done[top_bot][pos][i] < split_image_count) {
              frame_end = 0;
              break;
            }
          }
        }

        if (frame_end) {
          if (screen_buf_valid[top_bot][pos] == 0) {
            // TODO request key frame
            fprintf(stderr, "frame incomplete, requesting key frame\n");
          } else if ((send_header.frame_n - frame_n[top_bot]) % RP_IMAGE_FRAME_N_RANGE >= ENCODE_BUFFER_COUNT) {
            // TODO request key frame
            fprintf(stderr, "too many missing frames (%d, %d), requesting key frame\n", frame_n[top_bot], send_header.frame_n);
          } else {
            screen_buf_valid[top_bot][pos] = 1;
            // fprintf(stderr, "full frame %d %d (%d) pos %d\n", top_bot, send_header.frame_n, screen_frame_n[top_bot][pos], pos);
            if (!p_frame) {
              int w_orig_split = w_orig;
              w_orig = w_orig_full;
              frame_n[top_bot] = send_header.frame_n;
              if (encoder_rgb) {
                render_rgb_bpp(screen_decoded[top_bot], w_orig, h_orig, bpp);
              } else if (ntr_downscale_uv) {
                memcpy(image_me[top_bot][pos][Y_COMP], screen_buf[top_bot][pos][Y_COMP], w_orig * h_orig);
                memcpy(image_me[top_bot][pos][U_COMP], screen_buf[top_bot][pos][U_COMP], DSX_DIM(w_orig, dsx) * DSX_DIM(h_orig, dsx));
                memcpy(image_me[top_bot][pos][V_COMP], screen_buf[top_bot][pos][V_COMP], DSX_DIM(w_orig, dsx) * DSX_DIM(h_orig, dsx));

                upsampleXImage(upscaled_u_image, screen_buf[top_bot][pos][U_COMP], w_orig, h_orig, dsx);
                upsampleXImage(upscaled_v_image, screen_buf[top_bot][pos][V_COMP], w_orig, h_orig, dsx);
                render_yuv_to_rgb(screen_decoded[top_bot], screen_buf[top_bot][pos][Y_COMP], upscaled_u_image, upscaled_v_image,
                  w_orig, h_orig, screen_bpp[top_bot][pos][Y_COMP], screen_bpp[top_bot][pos][U_COMP], screen_bpp[top_bot][pos][V_COMP]
                );
              } else {
                memcpy(image_me[top_bot][pos][Y_COMP], screen_buf[top_bot][pos][Y_COMP], w_orig * h_orig);
                memcpy(image_me[top_bot][pos][U_COMP], screen_buf[top_bot][pos][U_COMP], w_orig * h_orig);
                memcpy(image_me[top_bot][pos][V_COMP], screen_buf[top_bot][pos][V_COMP], w_orig * h_orig);

                render_yuv_to_rgb(screen_decoded[top_bot], screen_buf[top_bot][pos][Y_COMP], screen_buf[top_bot][pos][U_COMP], screen_buf[top_bot][pos][V_COMP],
                  w_orig, h_orig, screen_bpp[top_bot][pos][Y_COMP], screen_bpp[top_bot][pos][U_COMP], screen_bpp[top_bot][pos][V_COMP]
                );
              }
              screen_done[top_bot][pos] = 1;
              screen_frame_n[top_bot][pos] += RP_IMAGE_FRAME_N_RANGE;
              handle_decode_frame_screen(&buffer_ctx[top_bot], screen_decoded[top_bot], top_bot,
                screen_data_size[top_bot][pos][RP_SCREEN_SPLIT_LEFT], screen_data_size[top_bot][pos][RP_SCREEN_SPLIT_RIGHT],
                screen_data_delay_between_packet[top_bot][pos][RP_SCREEN_SPLIT_LEFT], screen_data_delay_between_packet[top_bot][pos][RP_SCREEN_SPLIT_RIGHT]);
              // fprintf(stderr, "Displaying key frame: screen %d, frame_n = %d\n", top_bot, frame_n[top_bot]);
              w_orig = w_orig_split;
            }

            uint8_t frame_n_next = (frame_n[top_bot] + 1) % RP_IMAGE_FRAME_N_RANGE;
            int i_prev = -1;
            for (int i = 0; i < ENCODE_BUFFER_COUNT; ++i) {
              if (screen_frame_n[top_bot][i] == frame_n[top_bot] + RP_IMAGE_FRAME_N_RANGE
              ) {
                if (!screen_done[top_bot][i]) {
                  break;
                }
                i_prev = i;
                break;
              }
            }
            if (i_prev >= 0) {
              while (1) {
                int i;
                for (i = 0; i < ENCODE_BUFFER_COUNT; ++i) {
                  if (screen_frame_n[top_bot][i] == frame_n_next
                  ) {
                    if (screen_buf_valid[top_bot][i] <= 0) {
                      i = ENCODE_BUFFER_COUNT;
                      break;
                    }
                    // fprintf(stderr, "full next frame %d %d pos %d valid %d\n", top_bot, screen_frame_n[top_bot][i], i, screen_buf_valid[top_bot][i]);
                    int uv_unsigned_signed = !!(ntr_yuv_option & 0x2);
                    for (int k = 0; k < (ntr_encode_split_image ? RP_SCREEN_SPLIT_COUNT : 1); ++k) {
                      if (ntr_me_enabled == 1) {
                        int size_orig = w_orig * h_orig;
                        me_image(image_me[top_bot][i][Y_COMP] + size_orig * k,
                          image_me[top_bot][i_prev][Y_COMP] + size_orig * k,
                          screen_buf[top_bot][i][Y_COMP] + size_orig * k,
                          screen_me_buf[top_bot][i][ME_X_COMP] + me_w * me_h * k,
                          screen_me_buf[top_bot][i][ME_Y_COMP] + me_w * me_h * k,
                          1,
                          1,
                          w_orig,
                          h_orig,
                          screen_bpp[top_bot][i][Y_COMP]
                        );
                        if (ntr_downscale_uv)
                          size_orig = DSX_DIM(w_orig, dsx) * DSX_DIM(h_orig, dsx);
                        me_image(image_me[top_bot][i][U_COMP] + size_orig * k,
                          image_me[top_bot][i_prev][U_COMP] + size_orig * k,
                          screen_buf[top_bot][i][U_COMP] + size_orig * k,
                          screen_me_buf[top_bot][i][ME_X_COMP] + me_w * me_h * k,
                          screen_me_buf[top_bot][i][ME_Y_COMP] + me_w * me_h * k,
                          1,
                          dsx,
                          ntr_downscale_uv ? DSX_DIM(w_orig, dsx) : w_orig,
                          ntr_downscale_uv ? DSX_DIM(h_orig, dsx) : h_orig,
                          screen_bpp[top_bot][i][U_COMP]
                        );
                        me_image(image_me[top_bot][i][V_COMP] + size_orig * k,
                          image_me[top_bot][i_prev][V_COMP] + size_orig * k,
                          screen_buf[top_bot][i][V_COMP] + size_orig * k,
                          screen_me_buf[top_bot][i][ME_X_COMP] + me_w * me_h * k,
                          screen_me_buf[top_bot][i][ME_Y_COMP] + me_w * me_h * k,
                          1,
                          dsx,
                          ntr_downscale_uv ? DSX_DIM(w_orig, dsx) : w_orig,
                          ntr_downscale_uv ? DSX_DIM(h_orig, dsx) : h_orig,
                          screen_bpp[top_bot][i][V_COMP]
                        );
                      } else
                      if (ntr_me_enabled == 3)
                      {
                        int size_orig = w_orig * h_orig;
                        select_image(image_me[top_bot][i][Y_COMP] + size_orig * k,
                          image_me[top_bot][i_prev][Y_COMP] + size_orig * k,
                          screen_buf[top_bot][i][Y_COMP] + size_orig * k,
                          screen_me_buf[top_bot][i][ME_X_COMP] + me_w * me_h * k,
                          1,
                          1,
                          w_orig,
                          h_orig,
                          screen_bpp[top_bot][i][Y_COMP],
                          screen_bpp[top_bot][i_prev][Y_COMP],
                          0
                        );
                        if (ntr_downscale_uv)
                          size_orig = DSX_DIM(w_orig, dsx) * DSX_DIM(h_orig, dsx);
                        select_image(image_me[top_bot][i][U_COMP] + size_orig * k,
                          image_me[top_bot][i_prev][U_COMP] + size_orig * k,
                          screen_buf[top_bot][i][U_COMP] + size_orig * k,
                          screen_me_buf[top_bot][i][ME_X_COMP] + me_w * me_h * k,
                          1,
                          dsx,
                          ntr_downscale_uv ? DSX_DIM(w_orig, dsx) : w_orig,
                          ntr_downscale_uv ? DSX_DIM(h_orig, dsx) : h_orig,
                          screen_bpp[top_bot][i][U_COMP],
                          screen_bpp[top_bot][i_prev][U_COMP],
                          uv_unsigned_signed
                        );
                        select_image(image_me[top_bot][i][V_COMP] + size_orig * k,
                          image_me[top_bot][i_prev][V_COMP] + size_orig * k,
                          screen_buf[top_bot][i][V_COMP] + size_orig * k,
                          screen_me_buf[top_bot][i][ME_X_COMP] + me_w * me_h * k,
                          1,
                          dsx,
                          ntr_downscale_uv ? DSX_DIM(w_orig, dsx) : w_orig,
                          ntr_downscale_uv ? DSX_DIM(h_orig, dsx) : h_orig,
                          screen_bpp[top_bot][i][V_COMP],
                          screen_bpp[top_bot][i_prev][V_COMP],
                          uv_unsigned_signed
                        );
                      } else
                      {
                        int size_orig = w_orig * h_orig;
                        diff_image(image_me[top_bot][i][Y_COMP] + size_orig * k,
                          image_me[top_bot][i_prev][Y_COMP] + size_orig * k,
                          screen_buf[top_bot][i][Y_COMP] + size_orig * k,
                          w_orig,
                          h_orig,
                          screen_bpp[top_bot][i][Y_COMP],
                          screen_bpp[top_bot][i_prev][Y_COMP],
                          0
                        );
                        if (ntr_downscale_uv)
                          size_orig = DSX_DIM(w_orig, dsx) * DSX_DIM(h_orig, dsx);
                        diff_image(image_me[top_bot][i][U_COMP] + size_orig * k,
                          image_me[top_bot][i_prev][U_COMP] + size_orig * k,
                          screen_buf[top_bot][i][U_COMP] + size_orig * k,
                          ntr_downscale_uv ? DSX_DIM(w_orig, dsx) : w_orig,
                          ntr_downscale_uv ? DSX_DIM(h_orig, dsx) : h_orig,
                          screen_bpp[top_bot][i][U_COMP],
                          screen_bpp[top_bot][i_prev][U_COMP],
                          uv_unsigned_signed
                        );
                        diff_image(image_me[top_bot][i][V_COMP] + size_orig * k,
                          image_me[top_bot][i_prev][V_COMP] + size_orig * k,
                          screen_buf[top_bot][i][V_COMP] + size_orig * k,
                          ntr_downscale_uv ? DSX_DIM(w_orig, dsx) : w_orig,
                          ntr_downscale_uv ? DSX_DIM(h_orig, dsx) : h_orig,
                          screen_bpp[top_bot][i][V_COMP],
                          screen_bpp[top_bot][i_prev][V_COMP],
                          uv_unsigned_signed
                        );
                      }
                    }
                    u8 screen_bpp_cur[COMP_COUNT];
                    // if (!RP_ENCODE_STATIC_LQ) {
                      for (int c = 0; c < COMP_COUNT; ++c) {
                        screen_bpp_cur[c] = screen_bpp[top_bot][i][c];
                        screen_bpp[top_bot][i][c] = screen_bpp[top_bot][i_prev][c];
                      }
                    // }
                    w_orig = w_orig_full;
                    me_w = me_w_full;
                    if (ntr_downscale_uv) {
                      upsampleXImage(upscaled_u_image, image_me[top_bot][i][U_COMP], w_orig, h_orig, dsx);
                      upsampleXImage(upscaled_v_image, image_me[top_bot][i][V_COMP], w_orig, h_orig, dsx);
                      if (debug_view_plane == 0)
                        render_yuv_to_rgb(screen_decoded[top_bot], image_me[top_bot][i][Y_COMP], upscaled_u_image, upscaled_v_image,
                          w_orig, h_orig, screen_bpp[top_bot][i][Y_COMP], screen_bpp[top_bot][i][U_COMP], screen_bpp[top_bot][i][V_COMP]
                        );
                      else if (debug_view_plane == 2)
                        render_greyscale_to_comp3(screen_decoded[top_bot], upscaled_u_image, w_orig, h_orig, screen_bpp[top_bot][i][U_COMP]);
                      else if (debug_view_plane == 3)
                        render_greyscale_to_comp3(screen_decoded[top_bot], upscaled_v_image, w_orig, h_orig, screen_bpp[top_bot][i][V_COMP]);
                    } else {
                      if (debug_view_plane == 0)
                        render_yuv_to_rgb(screen_decoded[top_bot], image_me[top_bot][i][Y_COMP], image_me[top_bot][i][U_COMP], image_me[top_bot][i][V_COMP],
                          w_orig, h_orig, screen_bpp[top_bot][i][Y_COMP], screen_bpp[top_bot][i][U_COMP], screen_bpp[top_bot][i][V_COMP]
                        );
                      else if (debug_view_plane == 2)
                        render_greyscale_to_comp3(screen_decoded[top_bot], image_me[top_bot][i][U_COMP], w_orig, h_orig, screen_bpp[top_bot][i][U_COMP]);
                      else if (debug_view_plane == 3)
                        render_greyscale_to_comp3(screen_decoded[top_bot], image_me[top_bot][i][V_COMP], w_orig, h_orig, screen_bpp[top_bot][i][V_COMP]);
                    }
                    u8 me_bpp = ntr_me_enabled == 1 ? av_ceil_log2(ntr_me_search_param * 2 + 1) : 1;
                    if (debug_view_plane == 1)
                      render_greyscale_to_comp3(screen_decoded[top_bot], image_me[top_bot][i][Y_COMP], w_orig, h_orig, screen_bpp[top_bot][i][Y_COMP]);
                    else if (debug_view_plane == 4)
                      render_greyscale_upscale_to_comp3(screen_decoded[top_bot], w_orig, h_orig, (const u8 *)screen_me_buf[top_bot][i][ME_X_COMP], me_w, me_h, me_bpp);
                    else if (debug_view_plane == 5)
                      render_greyscale_upscale_to_comp3(screen_decoded[top_bot], w_orig, h_orig, (const u8 *)screen_me_buf[top_bot][i][ME_Y_COMP], me_w, me_h, me_bpp);
                    else if (debug_view_plane == 6)
                      render_greyscale_to_comp3(screen_decoded[top_bot], screen_buf[top_bot][i][Y_COMP], w_orig, h_orig, screen_bpp_cur[Y_COMP]);
                    else if (debug_view_plane == 7)
                      render_greyscale_upscale_to_comp3(screen_decoded[top_bot], w_orig, h_orig, screen_buf[top_bot][i][U_COMP], ntr_downscale_uv ? DSX_DIM(w_orig, dsx): w_orig, ntr_downscale_uv ? DSX_DIM(h_orig, dsx) : h_orig, screen_bpp_cur[U_COMP]);
                    else if (debug_view_plane == 8)
                      render_greyscale_upscale_to_comp3(screen_decoded[top_bot], w_orig, h_orig, screen_buf[top_bot][i][V_COMP], ntr_downscale_uv ? DSX_DIM(w_orig, dsx) : w_orig, ntr_downscale_uv ? DSX_DIM(h_orig, dsx) : h_orig, screen_bpp_cur[V_COMP]);
                    screen_done[top_bot][i] = 1;
                    handle_decode_frame_screen(&buffer_ctx[top_bot], screen_decoded[top_bot], top_bot,
                      screen_data_size[top_bot][pos][RP_SCREEN_SPLIT_LEFT], screen_data_size[top_bot][pos][RP_SCREEN_SPLIT_RIGHT],
                      screen_data_delay_between_packet[top_bot][pos][RP_SCREEN_SPLIT_LEFT], screen_data_delay_between_packet[top_bot][pos][RP_SCREEN_SPLIT_RIGHT]);
                    // fprintf(stderr, "Displaying p_frame: screen %d, frame_n = %d\n", top_bot, frame_n_next);
                    break;
                  }
                }
                if (i == ENCODE_BUFFER_COUNT) {
                  break;
                }

                screen_frame_n[top_bot][i] += RP_IMAGE_FRAME_N_RANGE;
                i_prev = i;
                frame_n[top_bot] = frame_n_next;
                frame_n_next = (frame_n[top_bot] + 1) % RP_IMAGE_FRAME_N_RANGE;
              }
            }
          }
        }
      } else {
        screen_buf_valid[top_bot][pos] = 0;
        screen_frame_n[top_bot][pos] = -1;
        fprintf(stderr, "fail %d %d (%d) plane %d\n", top_bot, pos, send_header.frame_n, plane);
        // char i = 0;
        // ikcp_send(kcp, &i, sizeof(i));
        // receiving = 0;
      }
    }

final:
    if (size) {
      return handle_recv(buf, size);
    }
    return 0;
  }

  // buf += size;
  memcpy(recv_buf_head, buf, size);
  recv_buf_head += size;
  recv_data_remain -= size;
  // size = 0;

  return 0;
}

SOCKET s;
struct sockaddr_in remoteAddr;
int udp_output(const char *buf, int len, ikcpcb *, void *)
{
  remoteAddr.sin_port = htons(8000);
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
  // fprintf(stderr, "udp_output: %d\n", len);
  return sendto(s, buf, len, 0, (struct sockaddr *)&remoteAddr, sizeof(remoteAddr));
}

IUINT32 kcpLastRecvMs = 0;
#define KCP_RECV_TIMEOUT_MS 2000
void receive_from_socket(SOCKET s)
{
  kcpLastRecvMs = iclock();

  while (running && !restart_kcp)
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
        IUINT32 diff = iclock() - kcpLastRecvMs;
        if (diff > KCP_RECV_TIMEOUT_MS)
        {
          // fprintf(stderr, "ikcp_recv timeout: %d\n", diff);
          break;
        }
      }
      continue;
    }
    kcpLastRecvMs = iclock();

    if ((ret = ikcp_input(kcp, (const char *)buf, ret)) < 0)
    {
      fprintf(stderr, "ikcp_input failed: %d\n", ret);
    }

    while ((ret = ikcp_recv(kcp, (char *)buf, sizeof(buf))) >= 0)
    {
      // fprintf(stderr, "ikcp_recv: %d\n", ret);
      if (handle_recv(buf, ret) < 0)
      {
        return;
      }
    }
  }
}

#define PACKET_SIZE 1448
#define KCP_SND_WND_SIZE 40
#define KCP_SOCKET_TIMEOUT 10
void *udp_recv_thread_func(void *)
{
  SDL_GL_MakeCurrent(win, glThreadContext);
  while (running)
  {
    restart_kcp = 0;

    kcp = ikcp_create(kcp_magic, 0);
    kcp->output = udp_output;
    ikcp_nodelay(kcp, 2, 10, 2, 1);
    ikcp_setmtu(kcp, PACKET_SIZE);
    ikcp_wndsize(kcp, KCP_SND_WND_SIZE, 0);
    // kcp->rx_minrto = 10;
    kcp->stream = 1;
    // fprintf(stderr, "new connection\n");
    for (int top_bot = 0; top_bot < SCREEN_COUNT; ++top_bot) {
      buffer_ctx[top_bot].updated = FBS_NOT_AVAIL;
      for (int i = 0; i < ENCODE_BUFFER_COUNT; ++i) {
        screen_frame_n[top_bot][i] = -1;
      }
      screen_pos[top_bot] = 0;
      frame_n[top_bot] = 0;
    }
    recv_state = RECV_STATE_HEADER;
    leftover_size = 0;
    receiving = 1;

    memset(frame_rate_tracker, 0, sizeof(frame_rate_tracker));
    memset(frame_size_tracker, 0, sizeof(frame_size_tracker));
    memset(delay_between_packet_tracker, 0, sizeof(delay_between_packet_tracker));

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

    Sleep(250);
  }
  SDL_GL_MakeCurrent(win, NULL);

  return 0;
}

#include "style.h"

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

  SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
  glThreadContext = SDL_GL_CreateContext(win);
  if (!glThreadContext)
  {
    fprintf(stderr, "SDL_GL_CreateContext (2): %s\n", SDL_GetError());
    return -1;
  }

  SDL_GL_MakeCurrent(win, glContext);

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
  izInitDecodeTable();

  for (int i = 0; i < SCREEN_COUNT; ++i) {
    glGenTextures(1, &buffer_ctx[i].gl_tex_id);
    pthread_mutex_init(&buffer_ctx[i].gl_tex_mutex, NULL);
  }
  gl_program = LoadProgram((const char *)vShaderStr, (const char *)fShaderStr);
  gl_position_loc = glGetAttribLocation(gl_program, "a_position");
  gl_tex_coord_loc = glGetAttribLocation(gl_program, "a_texCoord");
  gl_sampler_loc = glGetUniformLocation(gl_program, "s_texture");

  rpConfigSetDefault();

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

  SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED | ES_DISPLAY_REQUIRED);

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
  emscripten_set_main_loop_arg(MainLoop, (void *)ctx, 0, nk_true);
#else
  while (running)
    MainLoop((void *)ctx);
#endif

  SetThreadExecutionState(ES_CONTINUOUS);

  pthread_join(udp_recv_thread, NULL);
  pthread_join(menu_tcp_thread, NULL);
  pthread_join(nwm_tcp_thread, NULL);

  glDeleteProgram(gl_program);
  for (int i = 0; i < SCREEN_COUNT; ++i) {
    pthread_mutex_destroy(&buffer_ctx[i].gl_tex_mutex);
    glDeleteTextures(1, &buffer_ctx[i].gl_tex_id);
  }

  sock_cleanup();
  nk_sdl_shutdown();
  SDL_GL_DeleteContext(glThreadContext);
  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
