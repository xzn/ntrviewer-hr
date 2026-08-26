#include "const.h"
#include "ntr_audio.h"

// sdl device stream, opened lazily on the first audio frame
static SDL_AudioStream *audio_stream;
static volatile int audio_shutting_down;

// queue cap so latency cannot grow unbounded (~117 ms)
#define AUDIO_MAX_QUEUED_FRAMES (24)

static bool ntr_audio_open(void)
{
    if (audio_stream)
        return true;
    if (audio_shutting_down)
        return false;

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = RP_AUDIO_CHANNELS;
    spec.freq = RP_AUDIO_SAMPLE_RATE;

    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!audio_stream) {
        err_log("SDL_OpenAudioDeviceStream: %s\n", SDL_GetError());
        return false;
    }
    if (!SDL_ResumeAudioStreamDevice(audio_stream)) {
        err_log("SDL_ResumeAudioStreamDevice: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;
        return false;
    }
    return true;
}

void ntr_audio_handle_packet(const uint8_t *pcm, int size, uint8_t fmt)
{
    if (audio_shutting_down)
        return;
    if (fmt != RP_AUDIO_FMT_PCM16)
        return; // unknown format
    if (size < RP_AUDIO_FRAME_BYTES)
        return; // truncated frame

    if (!ntr_audio_open())
        return;

    // drop when the queue is too deep
    int queued = SDL_GetAudioStreamQueued(audio_stream);
    if (queued > RP_AUDIO_FRAME_BYTES * AUDIO_MAX_QUEUED_FRAMES)
        return;

    if (!SDL_PutAudioStreamData(audio_stream, pcm, RP_AUDIO_FRAME_BYTES))
        err_log("SDL_PutAudioStreamData: %s\n", SDL_GetError());
}

void ntr_audio_shutdown(void)
{
    audio_shutting_down = 1;
    if (audio_stream) {
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;
    }
    audio_shutting_down = 0;
}
