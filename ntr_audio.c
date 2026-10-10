#include "main.h"
#include "ntr_audio.h"
#include <math.h>

#define AUDIO_DBG (1)

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

#define AUDIO_FRAMES_MAX_COUNT (256)

enum audio_frame_processed_t {
    AUDIO_FRAME_PROCESSED_FADE_IN = (1 << 0),
    AUDIO_FRAME_PROCESSED_FADE_OUT = (1 << 1),
};

static rp_lock_t audio_frames_lock;
static volatile bool audio_shutting_down;

static struct audio_frame_t {
    bool avail;
    bool silence;
    bool reprime;
    enum audio_frame_processed_t processed;
    uint8_t frame[RP_AUDIO_FRAME_BYTES];
} audio_frames[AUDIO_FRAMES_MAX_COUNT];
static int audio_frames_head, audio_frames_tail;
static bool audio_state_primed;
static int64_t audio_primed_time;

#define AUDIO_PRIME_COUNT_MIN (16)
#define AUDIO_PRIME_COUNT_MIN (16)
#define AUDIO_PRIME_COUNT_STEP (8)
#define AUDIO_PRIME_COUNT_MAX (64)
static int audio_prime_count = AUDIO_PRIME_COUNT_MIN;

#define AUDIO_PRIME_ELAPSE_LOTHRES (4)
#define AUDIO_PRIME_ELAPSE_HITHRES (16)
#define AUDIO_PRIME_ELAPSE_SOTHRES (8)

static void audio_frame_fade(uint8_t *frame, double a, double b)
{
    int16_t *samples = (int16_t *)frame;
    _Static_assert(sizeof(*samples) == RP_AUDIO_SAMPLE_BYTES);
    for (int s_i = 0; s_i < RP_AUDIO_FRAME_SAMPLES; ++s_i) {
        double fact = (b - a) * ((double)s_i / (RP_AUDIO_FRAME_SAMPLES - 1)) + a;
        for (int c_i = 0; c_i < RP_AUDIO_CHANNELS; ++c_i) {
            double s = *samples * fact;
            *samples = trunc(s);
            samples++;
        }
    }
}

static void audio_frame_fade_in_step(uint8_t *frame, int i, int n)
{
    double a = (double)i / n;
    double b = a + 1.0 / n;
    audio_frame_fade(frame, a, b);
}

static void audio_frame_fade_out_step(uint8_t *frame, int i, int n)
{
    double start = (double)(n - i) / n;
    double end = start - 1.0 / n;
    audio_frame_fade(frame, start, end);
}

static struct audio_frame_t *ntr_audio_put_frame(int i)
{
    return &audio_frames[(audio_frames_head + i + AUDIO_FRAMES_MAX_COUNT) % AUDIO_FRAMES_MAX_COUNT];
}

static void ntr_audio_advance_frame_head(int i)
{
    audio_frames_head = (audio_frames_head + i) % AUDIO_FRAMES_MAX_COUNT;
}

static struct audio_frame_t *ntr_audio_peek_frame(int i)
{
    return &audio_frames[(audio_frames_tail + i + AUDIO_FRAMES_MAX_COUNT) % AUDIO_FRAMES_MAX_COUNT];
}

static void ntr_audio_advance_frame_tail(int i)
{
    audio_frames_tail = (audio_frames_tail + i) % AUDIO_FRAMES_MAX_COUNT;
}

static struct audio_frame_t *ntr_audio_next_frame(void)
{
    struct audio_frame_t *frame = ntr_audio_peek_frame(0);
    ntr_audio_advance_frame_tail(1);
    return frame;
}

static void audio_stream_put_frame(SDL_AudioStream *stream, uint8_t* frame)
{
    SDL_PutAudioStreamData(stream, frame, RP_AUDIO_FRAME_BYTES);
}

static void audio_stream_put_silence_frames(SDL_AudioStream *stream, int n)
{
    static const uint8_t silence[RP_AUDIO_FRAME_BYTES] = { 0 };
    for (int i = 0; i < n; ++i)
        SDL_PutAudioStreamData(stream, silence, RP_AUDIO_FRAME_BYTES);
}

static void ntr_audio_reset_frame(struct audio_frame_t *frame)
{
    frame->avail = false;
    frame->reprime = false;
    frame->processed = 0;
}

static void audio_stream_put_audio_frames(SDL_AudioStream *stream, int n)
{
    for (int i = 0; i < n; ++i) {
        struct audio_frame_t *frame = ntr_audio_next_frame();
        if (frame->silence)
            audio_stream_put_silence_frames(stream, 1);
        else
            audio_stream_put_frame(stream, frame->frame);

        ntr_audio_reset_frame(frame);
    }
}

#define NTR_AUDIO_FADE_FRAMES_COUNT (2)

#define NTR_AUDIO_REPRIME_THRES (NTR_AUDIO_FADE_FRAMES_COUNT)

static bool ntr_audio_frame_cont(struct audio_frame_t *frame)
{
    return frame->avail && !frame->reprime;
}

static bool ntr_audio_frame_data_cont(struct audio_frame_t *frame)
{
    return ntr_audio_frame_cont(frame) && frame->silence;
}

static void ntr_audio_fade_in(int i)
{
    for (int f = 0; f < NTR_AUDIO_FADE_FRAMES_COUNT; ++f) {
        struct audio_frame_t *frame = ntr_audio_peek_frame(i + f);
        if (ntr_audio_frame_data_cont(frame))
            break;
        if (!(frame->processed & AUDIO_FRAME_PROCESSED_FADE_IN)) {
            audio_frame_fade_in_step(frame->frame, f, NTR_AUDIO_FADE_FRAMES_COUNT);
            frame->processed |= AUDIO_FRAME_PROCESSED_FADE_IN;
        }
    }
}

static void ntr_audio_fade_out(int i)
{
    for (int f = -1; f >= -NTR_AUDIO_FADE_FRAMES_COUNT; --f) {
        struct audio_frame_t *frame = ntr_audio_peek_frame(i + f);
        if (ntr_audio_frame_data_cont(frame))
            break;
        if (!(frame->processed & AUDIO_FRAME_PROCESSED_FADE_OUT)) {
            audio_frame_fade_out_step(frame->frame, NTR_AUDIO_FADE_FRAMES_COUNT + f, NTR_AUDIO_FADE_FRAMES_COUNT);
            frame->processed |= AUDIO_FRAME_PROCESSED_FADE_OUT;
        }
    }
}

static int ntr_audio_get_frames_avail(void)
{
    int frames_avail = 0;
    for (int i = 0; i < AUDIO_FRAMES_MAX_COUNT; ++i) {
        struct audio_frame_t *frame = ntr_audio_peek_frame(i);
        if (ntr_audio_frame_cont(frame))
            frames_avail = i + 1;
        else
            break;
    }
    return frames_avail;
}

static void audio_stream_skip_audio_frames(int n)
{
    ntr_audio_fade_out(0);
    for (int i = 0; i < n; ++i) {
        struct audio_frame_t *frame = ntr_audio_next_frame();
        ntr_audio_reset_frame(frame);
    }
    ntr_audio_fade_in(0);
}

static void ntr_audio_handle_prime(SDL_AudioStream *stream, int frames_needed);
static void ntr_audio_handle_play(SDL_AudioStream *stream, int frames_needed)
{
    int frames_avail = ntr_audio_get_frames_avail();

    // frames_avail >= NTR_AUDIO_REPRIME_THRES

    int frames_process = MIN(frames_needed, frames_avail);

    int frames_remain = frames_avail - frames_process;

    int64_t audio_primed_elapsed = iclock64() - audio_primed_time;
    audio_primed_elapsed /= 1000000; // us to s

    if (
        (
            audio_primed_elapsed >= AUDIO_PRIME_ELAPSE_SOTHRES &&
            frames_avail >= AUDIO_PRIME_COUNT_MIN + MIN(AUDIO_PRIME_COUNT_STEP, audio_soft_danger_frames) &&
            audio_soft_danger_frames / 2 >= audio_soft_delay_frames - audio_soft_danger_frames
        ) ||
        (
            audio_primed_elapsed < AUDIO_PRIME_ELAPSE_LOTHRES &&
            frames_avail >= audio_prime_count + MAX(AUDIO_PRIME_COUNT_STEP, audio_soft_delay_frames - audio_soft_danger_frames)
        )
    ) {
        int audio_skip = MIN(MAX(1, audio_soft_danger_frames / 2), frames_avail - AUDIO_PRIME_COUNT_MIN);
        if (AUDIO_DBG)
            err_log("Audio frames skip: %d\n", audio_skip);

        if (
            frames_avail - audio_skip <= audio_prime_count - AUDIO_PRIME_COUNT_STEP &&
            audio_prime_count > AUDIO_PRIME_COUNT_MIN
        )
            audio_prime_count -= AUDIO_PRIME_COUNT_STEP;

        audio_primed_time = iclock64();

        if (AUDIO_DBG)
            err_log("Audio prime frames (soft) decreased: %d\n", audio_prime_count);

        audio_stream_skip_audio_frames(audio_skip);

        return ntr_audio_handle_play(stream, frames_needed);
    }

    if (frames_remain < NTR_AUDIO_REPRIME_THRES) {
        ntr_audio_peek_frame(frames_avail)->reprime = true;
        frames_process = frames_avail - NTR_AUDIO_REPRIME_THRES;
        ntr_audio_fade_out(frames_avail);

        audio_state_primed = false;

        if (AUDIO_DBG)
            err_log("Audio prime elapsed: %d\n", (int)audio_primed_elapsed);
        if (audio_primed_elapsed < AUDIO_PRIME_ELAPSE_LOTHRES && audio_prime_count < AUDIO_PRIME_COUNT_MAX) {
            audio_prime_count += AUDIO_PRIME_COUNT_STEP;
            if (AUDIO_DBG)
                err_log("Audio prime frames increased: %d\n", audio_prime_count);
        } else if (audio_primed_elapsed >= AUDIO_PRIME_ELAPSE_HITHRES && audio_prime_count > AUDIO_PRIME_COUNT_MIN) {
            audio_prime_count -= AUDIO_PRIME_COUNT_STEP;
            if (AUDIO_DBG)
                err_log("Audio prime frames decreased: %d\n", audio_prime_count);
        }
    }

    for (int i = 1; i < NTR_AUDIO_REPRIME_THRES + frames_process; ++i) {
        struct audio_frame_t *frame_prev = ntr_audio_peek_frame(i - 1);
        struct audio_frame_t *frame = ntr_audio_peek_frame(i);

        if (frame_prev->silence && !frame->silence) {
            frame_prev->processed |= AUDIO_FRAME_PROCESSED_FADE_IN;
            ntr_audio_fade_in(i);
        } if (!frame_prev->silence && frame->silence) {
            ntr_audio_fade_out(i);
            frame->processed |= AUDIO_FRAME_PROCESSED_FADE_OUT;
        }
    }

    audio_stream_put_audio_frames(stream, frames_process);
    frames_needed -= frames_process;

    if (!audio_state_primed) {
        audio_stream_put_audio_frames(stream, NTR_AUDIO_REPRIME_THRES);
        frames_needed -= NTR_AUDIO_REPRIME_THRES;

        ntr_audio_handle_prime(stream, frames_needed);
    }
}
static void ntr_audio_handle_prime(SDL_AudioStream *stream, int frames_needed)
{
    int prime_count = audio_prime_count;
    prime_count = MAX(prime_count, NTR_AUDIO_REPRIME_THRES);

    ntr_audio_peek_frame(0)->reprime = false;
    int frames_avail = ntr_audio_get_frames_avail();

    if (frames_avail < prime_count) {
        audio_stream_put_silence_frames(stream, frames_needed);
        frames_needed = 0;
    } else {
        ntr_audio_fade_in(0);

        audio_state_primed = true;
        audio_primed_time = iclock64();

        ntr_audio_handle_play(stream, frames_needed);
    }
}

#define AUDIO_QUEUED ((audio_frames_head - audio_frames_tail + AUDIO_FRAMES_MAX_COUNT) % AUDIO_FRAMES_MAX_COUNT)

static void SDLCALL ntr_sdl_audio_stream_cb(UNUSED void *userdata, SDL_AudioStream *stream, int additional_amount, UNUSED int total_amount)
{
    if (!additional_amount)
        return;

    if (audio_shutting_down)
        return;

    int frames_needed = (additional_amount + RP_AUDIO_FRAME_BYTES - 1) / RP_AUDIO_FRAME_BYTES;

    rp_lock_wait(audio_frames_lock);

    int audio_queued = AUDIO_QUEUED;

    if (audio_state_primed)
        ntr_audio_handle_play(stream, frames_needed);
    else
        ntr_audio_handle_prime(stream, frames_needed);
    rp_lock_rel(audio_frames_lock);

    int audio_queued_ms = audio_queued
        * RP_AUDIO_FRAME_SAMPLES * 1000 / RP_AUDIO_SAMPLE_RATE;

    if (audio_queued_ms && (!audio_danger_ms || audio_queued_ms < audio_danger_ms)) {
        audio_danger_ms = audio_queued_ms;
        audio_danger_frames = audio_queued;
    }

    if (audio_queued_ms > audio_delay_ms) {
        audio_delay_ms = audio_queued_ms;
        audio_delay_frames = audio_queued;
    }
}

static bool ntr_audio_open(void)
{
    if (audio_shutting_down)
        return false;

    if (audio_stream)
        return true;

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = RP_AUDIO_CHANNELS;
    spec.freq = RP_AUDIO_SAMPLE_RATE;

    audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!audio_stream) {
        err_log("SDL_OpenAudioDeviceStream: %s\n", SDL_GetError());
        return false;
    }

    if (!SDL_SetAudioStreamGetCallback(audio_stream, ntr_sdl_audio_stream_cb, NULL)) {
        err_log("SDL_SetAudioStreamGetCallback: %s\n", SDL_GetError());
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;
        return false;
    }

    rp_lock_init(audio_frames_lock);

    SDL_ResumeAudioStreamDevice(audio_stream);

    return true;
}

static struct audio_frame_t *ntr_audio_put(const uint8_t *fdata, int i)
{
    for (int h = 0; h < i; ++h) {
        struct audio_frame_t *frame = ntr_audio_put_frame(h);
        if (!frame->avail) {
            frame->avail = true;
            frame->silence = true;
        }
    }

    struct audio_frame_t *frame = ntr_audio_put_frame(i);
    if (!frame->avail || (frame->silence && !frame->processed)) {
        frame->avail = true;
        frame->silence = false;
        memcpy(frame->frame, fdata, RP_AUDIO_FRAME_BYTES);
    }

    return frame;
}

static struct audio_frame_t *ntr_audio_push(const uint8_t *fdata)
{
    struct audio_frame_t *frame = ntr_audio_put(fdata, 0);
    ntr_audio_advance_frame_head(1);
    return frame;
}

static bool audio_have_last;
static uint8_t audio_last_seq;
void ntr_audio_handle_packet(const uint8_t *pcm, int size, uint8_t fmt, uint8_t seq)
{
    if (fmt != RP_AUDIO_FMT_PCM16)
        return; // unknown format
    int nframes = size / RP_AUDIO_FRAME_BYTES;
    if (nframes < 1)
        return; // no whole frame

    if (!ntr_audio_open())
        return;

    rp_lock_wait(audio_frames_lock);

    // frames are oldest first, seq is the newest frame's
    for (int i = 0; i < nframes; i++) {
        uint8_t fseq = (uint8_t)(seq - (uint8_t)(nframes - 1) + (uint8_t)i);
        const uint8_t *fdata = pcm + (size_t)i * RP_AUDIO_FRAME_BYTES;

        if (!audio_have_last) {
            ntr_audio_push(fdata);
            audio_last_seq = fseq;
            audio_have_last = true;
            continue;
        }

        int diff = (int8_t)(fseq - (audio_last_seq + 1)); // wrap-safe distance
        if (diff < -32) {
            // large forward jump past the +/-127 window: resync after a long stall
            struct audio_frame_t *frame = ntr_audio_push(fdata);
            frame->reprime = true;
            audio_last_seq = fseq;
            continue;
        }

        ntr_audio_put(fdata, diff);
        if (diff >= 0) {
            ntr_audio_advance_frame_head(diff + 1);
            audio_last_seq = fseq;
        }
    }

    int audio_queued = AUDIO_QUEUED;

    rp_lock_rel(audio_frames_lock);

    // log buffer depth every 256 packets
    static unsigned ntr_audio_pkt_count;
    if (!(ntr_audio_pkt_count % 256)) {
        if (AUDIO_DBG)
            err_log("audio: pkt#%u frames=%d queued=%d\n",
                ntr_audio_pkt_count, nframes, audio_queued);

        ntr_audio_danger_ms = audio_danger_ms;
        audio_danger_ms = 0;
        ntr_audio_delay_ms = audio_delay_ms;
        audio_delay_ms = 0;

        audio_soft_danger_frames = (audio_soft_danger_frames + audio_danger_frames) / 2;
        audio_danger_frames = 0;
        audio_soft_delay_frames = (audio_soft_delay_frames + audio_delay_frames) / 2;
        audio_delay_frames = 0;
    }
    ntr_audio_pkt_count++;
}

void ntr_audio_reset(void)
{
    audio_shutting_down = false;
    audio_have_last = false;
    audio_last_seq = 0;

    audio_state_primed = false;

    audio_frames_head = audio_frames_tail = 0;
    audio_prime_count = AUDIO_PRIME_COUNT_MIN;

    for (int i = 0; i < AUDIO_FRAMES_MAX_COUNT; ++i)
        ntr_audio_reset_frame(&audio_frames[i]);

    audio_danger_ms = ntr_audio_danger_ms = 0;
    audio_delay_ms = ntr_audio_delay_ms = 0;
}

void ntr_audio_shutdown(void)
{
    audio_shutting_down = true;
    if (audio_stream) {
        SDL_DestroyAudioStream(audio_stream);
        audio_stream = NULL;
    }
}
