#ifndef NTR_AUDIO_H
#define NTR_AUDIO_H

#include <stdint.h>

// NTR-HR+ audio over the plain-udp video stream, old viewers drop it.
// hdr[0]=seq, hdr[1]=flags, hdr[2]=type, hdr[3]=format; payload s16 le stereo
#define RP_AUDIO_HDR_TYPE      (4)
#define RP_AUDIO_FMT_PCM16     (0)

#define RP_AUDIO_SAMPLE_RATE   (32728)
#define RP_AUDIO_CHANNELS      (2)
#define RP_AUDIO_FRAME_SAMPLES (160)
#define RP_AUDIO_FRAME_BYTES   (RP_AUDIO_FRAME_SAMPLES * RP_AUDIO_CHANNELS * 2) // 640

// call after the recv thread has stopped
void ntr_audio_shutdown(void);

// payload = consecutive pcm16 frames, oldest first; seq is the newest frame's
void ntr_audio_handle_packet(const uint8_t *pcm, int size, uint8_t fmt, uint8_t seq);

#endif // NTR_AUDIO_H
