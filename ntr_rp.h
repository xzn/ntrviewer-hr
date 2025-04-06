#ifndef NTR_RP_H
#define NTR_RP_H

#include "const.h"
thread_ret_t udp_recv_thread_func(void *);
typedef struct {
    uint32_t hidPad;
    uint32_t touchScreenState;
    uint32_t circlePadState;
    uint32_t cppState;
    uint32_t interfaceButtons;
} input_redirection_frame_t;
void input_redirection_send_frame(input_redirection_frame_t *frame);

void rp_buffer_init(void);
void rp_buffer_destroy(void);

#ifdef __cplusplus
#include <atomic>
using namespace std;
#else
#include <stdatomic.h>
#endif
extern int frame_rate_decoded_tracker[SCREEN_COUNT];
extern int frame_rate_displayed_tracker[SCREEN_COUNT];
extern int frame_size_tracker[SCREEN_COUNT];
extern int delay_between_packet_tracker[SCREEN_COUNT];
extern int frame_fully_received_tracker;
extern int frame_lost_tracker;
extern atomic_bool kcp_active;
extern atomic_bool kcp_dq;
extern atomic_bool kcp_restart;

#include "ui_common_sdl.h"
#include "rp_syn.h"

#ifdef _WIN32
#include <d3d11.h>
#endif

struct rp_buffer_ctx_t {
#ifdef _WIN32
    ID3D11Texture2D *d3d_tex[SCREEN_COUNT];
    ID3D11ShaderResourceView *d3d_srv[SCREEN_COUNT];
    ID3D11Texture2D *d3d_tex_staging[SCREEN_COUNT];
    ID3D11Texture2D *d3d_tex_upscaled[SCREEN_COUNT];
    ID3D11RenderTargetView *d3d_rtv_upscaled[SCREEN_COUNT];
    ID3D11ShaderResourceView *d3d_srv_upscaled[SCREEN_COUNT];
    ID3D11ShaderResourceView *d3d_srv_upscaled_prev[SCREEN_COUNT]; // weak-ref
#endif
    GLuint gl_tex[SCREEN_COUNT];
    GLuint gl_tex_upscaled[SCREEN_COUNT];
    GLuint gl_tex_upscaled_prev[SCREEN_COUNT]; // weak-ref
    int width_upscaled[SCREEN_COUNT];
    int height_upscaled[SCREEN_COUNT];

    uint8_t screen_decoded[FBI_COUNT][SCREEN_HEIGHT0 * SCREEN_WIDTH * GL_CHANNELS_N];

    rp_lock_t status_lock;
    enum frame_buffer_status_t status;
    int index_display_2;
    int index_display;
    int index_ready_display_2;
    int index_ready_display;
    int index_decode;

    uint8_t *data_prev;
    int width_prev, height_prev;
    int win_width_prev, win_height_prev;
    int upscaling_selected_prev;
    view_mode_t view_mode_prev;

    event_t decode_updated_event;
};
extern struct rp_buffer_ctx_t rp_buffer_ctx[SCREEN_COUNT];
extern event_t decode_updated_event;
#endif
