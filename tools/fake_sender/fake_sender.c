// fake_sender: a/v test source for ntrviewer-hr
// usage: fake_sender [ip] [quality] [fps] [audio]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef int socklen_t;
#  define CLOSESOCK closesocket
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <time.h>
   typedef int SOCKET;
#  define INVALID_SOCKET (-1)
#  define CLOSESOCK close
#endif

#include <turbojpeg.h>
#include "ntr_audio.h"

// ntr plain-udp video header, matches ntr_rp.c
#define NTR_VIDEO_PORT        8001
#define NTR_PACKET_SIZE       1448
#define NTR_DATA_HDR_SIZE     4
#define NTR_PACKET_DATA_SIZE  (NTR_PACKET_SIZE - NTR_DATA_HDR_SIZE) // 1444
#define NTR_MAX_PACKET_COUNT  240
#define NTR_HDR_ENDBIT        0x10
#define NTR_FMT_VIDEO         0x02
// encoded (portrait) framebuffer dims
#define NTR_TOP_ENC_W  240
#define NTR_TOP_ENC_H  400
#define NTR_BOT_ENC_W  240
#define NTR_BOT_ENC_H  320

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

// test pattern: corner markers (TL red, TR green, BL blue, BR yellow), sweeping bar
static void gen_pattern(uint8_t *rgb, int w, int h, int frame, int screen_top) {
    const int c = 40;
    const int bar = (frame * 4) % w;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t *p = rgb + (size_t)(y * w + x) * 3;
            p[0] = (uint8_t)(x * 255 / (w - 1));
            p[1] = (uint8_t)(y * 255 / (h - 1));
            p[2] = screen_top ? 70 : 190;
            if (x - bar < 3 && bar - x < 3) { p[0] = p[1] = p[2] = 255; }
            if      (x <  c && y <  c)         { p[0]=255; p[1]=0;   p[2]=0;   }
            else if (x >= w-c && y <  c)       { p[0]=0;   p[1]=255; p[2]=0;   }
            else if (x <  c && y >= h-c)       { p[0]=0;   p[1]=0;   p[2]=255; }
            else if (x >= w-c && y >= h-c)     { p[0]=255; p[1]=255; p[2]=0;   }
        }
    }
}

// 180-degree flip, matches the viewer rotation
static void flip_rgb_180(uint8_t *rgb, int w, int h) {
    size_t n = (size_t)w * h;
    for (size_t i = 0; i < n / 2; ++i) {
        uint8_t *a = rgb + i * 3;
        uint8_t *b = rgb + (n - 1 - i) * 3;
        for (int k = 0; k < 3; ++k) { uint8_t t = a[k]; a[k] = b[k]; b[k] = t; }
    }
}

// fragment a jpeg into plain-udp packets
static int send_frame(SOCKET s, const struct sockaddr_in *dst, uint8_t frame_id,
                      int screen_top, const uint8_t *jpeg, size_t jpeg_size) {
    uint8_t pkt[NTR_PACKET_SIZE];
    size_t off = 0;
    int idx = 0;
    while (off < jpeg_size) {
        size_t chunk = jpeg_size - off;
        if (chunk > NTR_PACKET_DATA_SIZE) chunk = NTR_PACKET_DATA_SIZE;
        int last = (off + chunk >= jpeg_size);
        if (idx >= NTR_MAX_PACKET_COUNT) {
            fprintf(stderr, "frame too large (%zu bytes) - lower quality\n", jpeg_size);
            return -1;
        }
        pkt[0] = frame_id;
        pkt[1] = (uint8_t)((screen_top ? 1 : 0) | (last ? NTR_HDR_ENDBIT : 0));
        pkt[2] = NTR_FMT_VIDEO;
        pkt[3] = (uint8_t)idx;
        memcpy(pkt + NTR_DATA_HDR_SIZE, jpeg + off, chunk);
        int n = (int)(NTR_DATA_HDR_SIZE + chunk);
        if (sendto(s, (const char *)pkt, n, 0, (const struct sockaddr *)dst, sizeof(*dst)) != n)
            return -1;
        off += chunk;
        ++idx;
    }
    return 0;
}

// send the audio frames owed since the last call, 440/587 Hz test tone
static void send_audio(SOCKET s, const struct sockaddr_in *dst, int fps) {
    static double accum = 0.0;            // samples owed since last call
    static double phase_l = 0.0, phase_r = 0.0;
    static uint8_t seq = 0;
    const double two_pi = 6.283185307179586;
    const double step_l = two_pi * 440.0 / RP_AUDIO_SAMPLE_RATE;
    const double step_r = two_pi * 587.0 / RP_AUDIO_SAMPLE_RATE;
    const double amp = 0.15 * 32767.0;

    accum += (double)RP_AUDIO_SAMPLE_RATE / fps;
    while (accum >= RP_AUDIO_FRAME_SAMPLES) {
        uint8_t pkt[NTR_DATA_HDR_SIZE + RP_AUDIO_FRAME_BYTES];
        pkt[0] = seq++;
        pkt[1] = 0;
        pkt[2] = RP_AUDIO_HDR_TYPE;
        pkt[3] = RP_AUDIO_FMT_PCM16;
        int16_t *pcm = (int16_t *)(pkt + NTR_DATA_HDR_SIZE);
        for (int i = 0; i < RP_AUDIO_FRAME_SAMPLES; ++i) {
            pcm[i * 2 + 0] = (int16_t)(amp * sin(phase_l));
            pcm[i * 2 + 1] = (int16_t)(amp * sin(phase_r));
            phase_l += step_l; if (phase_l >= two_pi) phase_l -= two_pi;
            phase_r += step_r; if (phase_r >= two_pi) phase_r -= two_pi;
        }
        int n = (int)sizeof(pkt);
        sendto(s, (const char *)pkt, n, 0, (const struct sockaddr *)dst, sizeof(*dst));
        accum -= RP_AUDIO_FRAME_SAMPLES;
    }
}

int main(int argc, char **argv) {
    const char *ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int quality    = (argc > 2) ? atoi(argv[2]) : 80;
    int fps        = (argc > 3) ? atoi(argv[3]) : 30;
    int audio_on   = (argc > 4) ? atoi(argv[4]) : 1;
    if (fps < 1) fps = 30;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "WSAStartup failed\n"); return 1; }
#endif

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) { fprintf(stderr, "socket failed\n"); return 1; }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(NTR_VIDEO_PORT);
    if (inet_pton(AF_INET, ip, &dst.sin_addr) != 1) { fprintf(stderr, "bad ip: %s\n", ip); return 1; }

    tjhandle tj = tjInitCompress();
    if (!tj) { fprintf(stderr, "tjInitCompress failed\n"); return 1; }

    static uint8_t rgb_top[NTR_TOP_ENC_W * NTR_TOP_ENC_H * 3];
    static uint8_t rgb_bot[NTR_BOT_ENC_W * NTR_BOT_ENC_H * 3];

    printf("fake_sender -> %s:%d  quality=%d fps=%d audio=%s  (Ctrl+C to stop)\n",
           ip, NTR_VIDEO_PORT, quality, fps, audio_on ? "on" : "off");

    uint8_t frame_id = 0;
    for (;;) {
        struct { uint8_t *rgb; int w, h, top; } screens[2] = {
            { rgb_top, NTR_TOP_ENC_W, NTR_TOP_ENC_H, 1 },
            { rgb_bot, NTR_BOT_ENC_W, NTR_BOT_ENC_H, 0 },
        };
        for (int i = 0; i < 2; ++i) {
            gen_pattern(screens[i].rgb, screens[i].w, screens[i].h, frame_id, screens[i].top);
            flip_rgb_180(screens[i].rgb, screens[i].w, screens[i].h);
            unsigned char *jpeg = NULL;
            unsigned long  jpeg_size = 0;
            if (tjCompress2(tj, screens[i].rgb, screens[i].w, 0, screens[i].h, TJPF_RGB,
                            &jpeg, &jpeg_size, TJSAMP_420, quality, 0) != 0) {
                fprintf(stderr, "tjCompress2: %s\n", tjGetErrorStr());
                continue;
            }
            send_frame(s, &dst, frame_id, screens[i].top, jpeg, jpeg_size);
            tjFree(jpeg);
        }
        if (audio_on)
            send_audio(s, &dst, fps);
        ++frame_id;
        sleep_ms(1000 / fps);
    }

    tjDestroy(tj);
    CLOSESOCK(s);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
