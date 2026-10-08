#include "main.h"
#include "ntr_audio.h"

int ntr_audio_danger_ms;
int ntr_audio_delay_ms;
static int audio_danger_ms;
static int audio_delay_ms;

static int audio_danger_frames;
static int audio_soft_danger_frames;
static int audio_delay_frames;
static int audio_soft_delay_frames;

// sdl device stream, opened lazily on the first audio frame
static SDL_AudioStream *audio_stream;
static volatile int audio_shutting_down;
static bool audio_primed; // playback starts only once the jitter buffer fills
static int64_t audio_primed_time;
static int audio_soft_reprime;
#define AUDIO_PRIME_FRAMES_SOFT_STEP (2)
#define AUDIO_PRIME_FRAMES_SOFT_STEP_FULL (AUDIO_PRIME_FRAMES_SOFT_STEP * 2)
static int audio_soft_skip;
#define AUDIO_PRIME_FRAMES_SOFT_STEP_SKIP (AUDIO_PRIME_FRAMES_SOFT_STEP + audio_soft_skip)
#define AUDIO_PRIME_ELAPSE_LOTHRES (4)
#define AUDIO_PRIME_ELAPSE_HITHRES (16)
#define AUDIO_PRIME_ELAPSE_SOTHRES (8)

static bool audio_have_last;
static uint8_t audio_last_seq;

static const uint8_t silence[RP_AUDIO_FRAME_BYTES] = {0};

// frames held before playback starts, cushion for wifi dips (up to ~313 ms)
#define AUDIO_PRIME_FRAMES_MIN (16)
#define AUDIO_PRIME_FRAMES_STEP (8)
#define AUDIO_PRIME_FRAMES_MAX (64)
static int audio_prime_frames = AUDIO_PRIME_FRAMES_MIN;
// queue cap so latency cannot grow unbounded (~939 ms)
#define AUDIO_MAX_QUEUED_FRAMES (192)
// max silence frames per gap, larger gaps just resync
#define AUDIO_MAX_GAP_FILL (8)

#define AUDIO_PRIME_DEBUG_INFO (0)

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

static void frame_audio_fade(uint8_t *frame, double start, double end)
{
    int16_t *samples = (int16_t *)frame;
    for (int s_i = 0; s_i < RP_AUDIO_FRAME_SAMPLES; ++s_i) {
        double fact = (end - start) * ((double)s_i / (RP_AUDIO_FRAME_SAMPLES - 1)) + start;
        for (int c_i = 0; c_i < RP_AUDIO_CHANNELS; ++c_i) {
            *samples++ *= fact;
        }
    }
}

static void ntr_audio_put(const uint8_t *frame)
{
    int64_t audio_primed_elapsed = iclock64() - audio_primed_time;
    audio_primed_elapsed /= 1000000; // us to s

    // latency hit the cap: flush and re-prime instead of holding max lag forever
    int audio_queued = SDL_GetAudioStreamQueued(audio_stream);
    if (audio_queued < 0)
        audio_queued = 0;
    int frames_queued = audio_queued / RP_AUDIO_FRAME_BYTES;
    bool max_lat = frames_queued > AUDIO_MAX_QUEUED_FRAMES;
    bool no_data = !frames_queued && SDL_GetAudioStreamAvailable(audio_stream) <= 0;
    if (AUDIO_PRIME_DEBUG_INFO) {
        if (max_lat)
            err_log("Max latency hit, re-priming\n");
        if (no_data)
            err_log("Audio data empty, re-priming\n");
    }
    if ((max_lat || no_data) && audio_primed) {
        SDL_ClearAudioStream(audio_stream);
        SDL_PauseAudioStreamDevice(audio_stream);
        audio_primed = false;

        if (AUDIO_PRIME_DEBUG_INFO)
            err_log("Audio prime elapsed: %d\n", (int)audio_primed_elapsed);
        if (audio_primed_elapsed < AUDIO_PRIME_ELAPSE_LOTHRES && audio_prime_frames < AUDIO_PRIME_FRAMES_MAX) {
            audio_prime_frames += AUDIO_PRIME_FRAMES_STEP;
            if (AUDIO_PRIME_DEBUG_INFO)
                err_log("Audio prime frames increased: %d\n", audio_prime_frames);
        } else if (audio_primed_elapsed >= AUDIO_PRIME_ELAPSE_HITHRES && audio_prime_frames > AUDIO_PRIME_FRAMES_MIN) {
            audio_prime_frames -= AUDIO_PRIME_FRAMES_STEP;
            if (AUDIO_PRIME_DEBUG_INFO)
                err_log("Audio prime frames decreased: %d\n", audio_prime_frames);
        }

        audio_soft_skip = 0;
        audio_soft_reprime = AUDIO_PRIME_FRAMES_SOFT_STEP;
        audio_primed_time = iclock64();
    }

    if (audio_primed && !audio_soft_reprime &&
        (
            (audio_primed_elapsed >= AUDIO_PRIME_ELAPSE_SOTHRES && frames_queued >= AUDIO_PRIME_FRAMES_MIN + MIN(AUDIO_PRIME_FRAMES_STEP, audio_soft_danger_frames)) ||
            (audio_primed_elapsed < AUDIO_PRIME_ELAPSE_LOTHRES && frames_queued >= audio_prime_frames + MAX(AUDIO_PRIME_FRAMES_STEP, audio_soft_delay_frames - audio_soft_danger_frames))
        )
    ) {
        audio_soft_skip = MIN(MAX(1, audio_soft_danger_frames / 2), frames_queued - AUDIO_PRIME_FRAMES_MIN);
        if (AUDIO_PRIME_DEBUG_INFO)
            err_log("Audio frames soft skip: %d\n", audio_soft_skip);
        audio_soft_reprime = audio_soft_skip + AUDIO_PRIME_FRAMES_SOFT_STEP_FULL;

        if (
            frames_queued - audio_soft_skip <= audio_prime_frames - AUDIO_PRIME_FRAMES_STEP &&
            audio_prime_frames > AUDIO_PRIME_FRAMES_MIN
        )
            audio_prime_frames -= AUDIO_PRIME_FRAMES_STEP;

        audio_primed_time = iclock64();

        if (AUDIO_PRIME_DEBUG_INFO)
            err_log("Audio prime frames (soft) decreased: %d\n", audio_prime_frames);
    }

    if (audio_soft_reprime) {
        if (audio_soft_reprime > AUDIO_PRIME_FRAMES_SOFT_STEP_SKIP) {
            uint8_t fade[RP_AUDIO_FRAME_BYTES];
            memcpy(fade, frame, RP_AUDIO_FRAME_BYTES);
            double start = ((double)audio_soft_reprime - AUDIO_PRIME_FRAMES_SOFT_STEP_SKIP) / AUDIO_PRIME_FRAMES_SOFT_STEP;
            double end = start - 1.0 / AUDIO_PRIME_FRAMES_SOFT_STEP;
            frame_audio_fade(fade, start, end);

            if (!SDL_PutAudioStreamData(audio_stream, fade, RP_AUDIO_FRAME_BYTES))
                err_log("SDL_PutAudioStreamData: %s\n", SDL_GetError());
        } else if (audio_soft_reprime <= AUDIO_PRIME_FRAMES_SOFT_STEP) {
            uint8_t fade[RP_AUDIO_FRAME_BYTES];
            memcpy(fade, frame, RP_AUDIO_FRAME_BYTES);
            double start = ((double)AUDIO_PRIME_FRAMES_SOFT_STEP - audio_soft_reprime) / AUDIO_PRIME_FRAMES_SOFT_STEP;
            double end = start + 1.0 / AUDIO_PRIME_FRAMES_SOFT_STEP;
            frame_audio_fade(fade, start, end);

            if (!SDL_PutAudioStreamData(audio_stream, fade, RP_AUDIO_FRAME_BYTES))
                err_log("SDL_PutAudioStreamData: %s\n", SDL_GetError());
        }
        --audio_soft_reprime;
    } else {
        if (!SDL_PutAudioStreamData(audio_stream, frame, RP_AUDIO_FRAME_BYTES))
            err_log("SDL_PutAudioStreamData: %s\n", SDL_GetError());

        int curr_audio_delay_ms = audio_queued
            * RP_AUDIO_FRAME_SAMPLES / RP_AUDIO_FRAME_BYTES * 1000 / RP_AUDIO_SAMPLE_RATE;

        if (!audio_danger_ms || curr_audio_delay_ms < audio_danger_ms) {
            audio_danger_ms = curr_audio_delay_ms;
            audio_danger_frames = frames_queued;
        }

        if (curr_audio_delay_ms > audio_delay_ms) {
            audio_delay_ms = curr_audio_delay_ms;
            audio_delay_frames = frames_queued;
        }
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
        if (diff < -32) {
            // large forward jump past the +/-127 window: resync after a long stall
            ntr_audio_put(fdata);
            audio_last_seq = fseq;
            continue;
        }
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

    // log buffer depth every 256 packets
    static unsigned ntr_audio_pkt_count;
    if (!(ntr_audio_pkt_count % 256)) {
        if (AUDIO_PRIME_DEBUG_INFO)
            err_log("audio: pkt#%u frames=%d queued=%d\n",
                ntr_audio_pkt_count, nframes, SDL_GetAudioStreamQueued(audio_stream));

        audio_soft_danger_frames = (audio_soft_danger_frames + audio_danger_frames) / 2;
        audio_danger_frames = 0;
        audio_soft_delay_frames = (audio_soft_delay_frames + audio_delay_frames) / 2;
        audio_delay_frames = 0;
        ntr_audio_danger_ms = audio_danger_ms;
        audio_danger_ms = 0;
        ntr_audio_delay_ms = audio_delay_ms;
        audio_delay_ms = 0;
    }
    ntr_audio_pkt_count++;

    // start playback only once primed
    if (!audio_primed &&
        SDL_GetAudioStreamQueued(audio_stream) >= RP_AUDIO_FRAME_BYTES * audio_prime_frames) {
        if (!SDL_ResumeAudioStreamDevice(audio_stream))
            err_log("SDL_ResumeAudioStreamDevice: %s\n", SDL_GetError());
        audio_primed = true;
        audio_primed_time = iclock64();
    }
}

void ntr_audio_reset(void)
{
    audio_danger_ms = ntr_audio_danger_ms = 0;
    audio_delay_ms = ntr_audio_delay_ms = 0;
    audio_soft_skip = 0;
    audio_soft_reprime = AUDIO_PRIME_FRAMES_SOFT_STEP;
    // drop stale jitter state so a reconnect re-primes from scratch
    audio_have_last = false;
    audio_last_seq = 0;
    audio_primed = false;
    audio_prime_frames = AUDIO_PRIME_FRAMES_MIN;
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
    audio_danger_ms = ntr_audio_danger_ms = 0;
    audio_delay_ms = ntr_audio_delay_ms = 0;
    audio_soft_skip = 0;
    audio_soft_reprime = 0;
}
