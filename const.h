#ifndef CONST_H
#define CONST_H

#define UNUSED __attribute__((unused))

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#include <stdio.h>
#define err_log(f, ...) fprintf(stderr, "%s:%d:%s " f, __FILE__, __LINE__, __func__, ## __VA_ARGS__)

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define socket_valid(s) ((s) != INVALID_SOCKET)
#define socket_errno() WSAGetLastError()

#include <d3dcompiler.h>
#include "windows.h"
typedef HANDLE event_t;

UNUSED static void event_init(HANDLE *event) {
    *event = CreateEventA(NULL, FALSE, FALSE, NULL);
}

UNUSED static void event_close(HANDLE *event) {
    CloseHandle(*event);
    *event = NULL;
}

UNUSED static int event_wait(HANDLE *event, int to_ns) {
    DWORD res = WaitForSingleObject(*event, to_ns / 1000000);
    if (res == WAIT_TIMEOUT) {
        return ETIMEDOUT;
    }
    return (int)res;
}

UNUSED static void event_rel(HANDLE *event) {
    SetEvent(*event);
}
#include "rp_syn.h"
#else
typedef unsigned char boolean;
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
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

#define socket_valid(s) ((int)(s) >= 0)
#define socket_errno() errno
#define WSAEWOULDBLOCK EWOULDBLOCK
#define WSAETIMEDOUT ETIMEDOUT
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (-1)
#define closesocket close
#define WSAPOLLFD struct pollfd
#define WSAPoll poll
#define SD_BOTH SHUT_RDWR

UNUSED static void Sleep(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

#include "rp_syn.h"
typedef struct event_t {
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    int flag;
} event_t;

UNUSED static void event_init(struct event_t *event) {
    pthread_cond_init(&event->cond, &rp_cond_attr);
    pthread_mutex_init(&event->mutex, NULL);
    event->flag = 0;
}

UNUSED static void event_close(struct event_t *event) {
    pthread_cond_destroy(&event->cond);
    pthread_mutex_destroy(&event->mutex);
    event->flag = 0;
}

UNUSED static int event_wait(struct event_t *event, int to_ns) {
    pthread_mutex_lock(&event->mutex);
    while (!event->flag) {
#ifdef PTHREAD_SEM_NON_MONOTONIC_CLOCK
        struct timespec to = clock_abs_ns_from_now(to_ns);
#else
        struct timespec to = clock_monotonic_abs_ns_from_now(to_ns);
#endif
        int res = pthread_cond_timedwait(&event->cond, &event->mutex, &to);
        if (res) {
            pthread_mutex_unlock(&event->mutex);
            return res;
        }
    }
    event->flag = 0;
    pthread_mutex_unlock(&event->mutex);
    return 0;
}

UNUSED static void event_rel(struct event_t *event) {
    pthread_mutex_lock(&event->mutex);
    event->flag = 1;
    pthread_cond_signal(&event->cond);
    pthread_mutex_unlock(&event->mutex);
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
#define thread_set_cancel_state(b) ((void)0)
#else
#define thread_t pthread_t
typedef void *thread_ret_t;
#define thread_create(t, f, a) pthread_create(&(t), NULL, f, a)
#define thread_exit(n) pthread_exit((thread_ret_t)n)
#define thread_join(t) pthread_join(t, NULL)
#define thread_cancel(t) pthread_cancel(t)
#define thread_set_cancel_state(b) pthread_setcancelstate(b ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE, NULL)
#endif

#define REST_EVERY_MS 100

enum screen_t {
    SCREEN_TOP,
    SCREEN_BOT,
    SCREEN_COUNT,
};

enum frame_buffer_index_t
{
    FBI_DECODE,
    FBI_DECODE_PREV,
    FBI_READY_DISPLAY = FBI_DECODE_PREV,
    FBI_READY_DISPLAY_2,
    FBI_DISPLAY,
    FBI_DISPLAY_2,
    FBI_COUNT,
};

enum frame_buffer_status_t
{
    FBS_NOT_AVAIL = -1,
    FBS_NOT_UPDATED,
    FBS_UPDATED,
    FBS_UPDATED_2,
};

#define DIV_ROUND_UP(a, b) (((a) + (b) - 1) / (b))

#define ROUND_UP(a, b) (DIV_ROUND_UP(a, b) * b)

#include <SDL3/SDL.h>
#define SDL_WIN_FLAGS_DEFAULT (SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN)
#define WIN_TITLE "NTRViewer-HR"

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT0 400
#define SCREEN_HEIGHT1 320

#define WIN_WIDTH_DEFAULT (SCREEN_HEIGHT0 * 2)
#define WIN_HEIGHT_DEFAULT (SCREEN_WIDTH * 2 * 2)
#define WIN_WIDTH2_DEFAULT (SCREEN_HEIGHT1 * 2)
#define WIN_HEIGHT12_DEFAULT (SCREEN_WIDTH * 2)

#define JPEG_DCTSIZE 8

#define RGB_CHANNELS_N 3
#define GL_CHANNELS_N 4
#define TJ_FORMAT TJPF_RGBA
#define SDL_FORMAT SDL_PIXELFORMAT_RGBA32
#define D3D_FORMAT DXGI_FORMAT_R8G8B8A8_UNORM
#define VK_FORMAT VK_FORMAT_R8G8B8A8_UNORM
#define GL_INT_FORMAT GL_RGBA8
#define GL_FORMAT GL_RGBA

#define ui_font_scale_step_factor (32.0f)
#define ui_font_scale_epsilon (1.0f / ui_font_scale_step_factor)

#define NTR_SCREEN_PRIORITY_FACTOR_DEFAULT (2)
#define UI_BLUR_RADIUS_MAX (15)

typedef uint8_t byte;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#include "glad/glad.h"

#endif
