#include "const.h"
#include "ntr_audio.h"

// sdl device stream, opened lazily on the first audio frame
static SDL_AudioStream *audio_stream;
static volatile int audio_shutting_down;
static bool audio_primed; // playback starts only once the jitter buffer fills

static bool audio_have_last;
static uint8_t audio_last_seq;

// frames held before playback starts, cushion for wifi dips (~117 ms)
#define AUDIO_PRIME_FRAMES (24)
// queue cap so latency cannot grow unbounded (~391 ms)
#define AUDIO_MAX_QUEUED_FRAMES (80)
// max silence frames per gap, larger gaps just resync
#define AUDIO_MAX_GAP_FILL (8)

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
    // resume happens once primed
    return true;
}

static void ntr_audio_put(const uint8_t *frame)
{
    if (SDL_GetAudioStreamQueued(audio_stream) <= RP_AUDIO_FRAME_BYTES * AUDIO_MAX_QUEUED_FRAMES) {
        if (!SDL_PutAudioStreamData(audio_stream, frame, RP_AUDIO_FRAME_BYTES))
            err_log("SDL_PutAudioStreamData: %s\n", SDL_GetError());
    }
}

void ntr_audio_handle_packet(const uint8_t *pcm, int size, uint8_t fmt, uint8_t seq)
{
    if (audio_shutting_down)
        return;
    if (fmt != RP_AUDIO_FMT_PCM16)
        return; // unknown format
    int nframes = size / RP_AUDIO_FRAME_BYTES;
    if (nframes < 1)
        return; // no whole frame

    if (!ntr_audio_open())
        return;

    static const uint8_t silence[RP_AUDIO_FRAME_BYTES] = {0};

    // frames are oldest first, seq is the newest frame's
    for (int i = 0; i < nframes; i++) {
        uint8_t fseq = (uint8_t)(seq - (uint8_t)(nframes - 1) + (uint8_t)i);
        const uint8_t *fdata = pcm + (size_t)i * RP_AUDIO_FRAME_BYTES;

        if (!audio_have_last) {
            ntr_audio_put(fdata);
            audio_last_seq = fseq;
            audio_have_last = true;
            continue;
        }

        int diff = (int8_t)(fseq - audio_last_seq); // wrap-safe distance
        if (diff <= 0)
            continue; // duplicate or older

        if (diff > 1) {
            int missing = diff - 1;
            if (missing <= AUDIO_MAX_GAP_FILL)
                for (int k = 0; k < missing; k++)
                    ntr_audio_put(silence);
        }
        ntr_audio_put(fdata);
        audio_last_seq = fseq;
    }

    // log buffer depth every 200 packets
    static unsigned ntr_audio_pkt_count;
    if (ntr_audio_pkt_count == 0 || (ntr_audio_pkt_count % 200) == 0)
        err_log("audio: pkt#%u frames=%d queued=%d\n",
                ntr_audio_pkt_count, nframes, SDL_GetAudioStreamQueued(audio_stream));
    ntr_audio_pkt_count++;

    // start playback only once primed
    if (!audio_primed &&
        SDL_GetAudioStreamQueued(audio_stream) >= RP_AUDIO_FRAME_BYTES * AUDIO_PRIME_FRAMES) {
        if (!SDL_ResumeAudioStreamDevice(audio_stream))
            err_log("SDL_ResumeAudioStreamDevice: %s\n", SDL_GetError());
        audio_primed = true;
    }
}

void ntr_audio_reset(void)
{
    // drop stale jitter state so a reconnect re-primes from scratch
    audio_have_last = false;
    audio_last_seq = 0;
    audio_primed = false;
    if (audio_stream) {
        SDL_ClearAudioStream(audio_stream);
        SDL_PauseAudioStreamDevice(audio_stream);
    }
}

void ntr_audio_shutdown(void)
{
    audio_shutting_down = 1;
    if (audio_stream) {
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;
    }
    audio_primed = false;
    audio_have_last = false;
    audio_shutting_down = 0;
}
