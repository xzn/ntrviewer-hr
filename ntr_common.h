#ifndef NTR_COMMON_H
#define NTR_COMMON_H

#define SOCKET_POLL_INTERVAL_MS 250
#define SOCKET_RESET_INTERVAL_MS 2000

#include "nuklear/nuklear.h"

#include "main.h"
#include "ntr_rp.h"
#include <stdatomic.h>

int socket_startup(void);
int socket_shutdown(void);

UNUSED static bool socket_set_nonblock(SOCKET s, bool nb)
{
#ifdef _WIN32
    u_long opt = nb;
    if (ioctlsocket(s, FIONBIO, &opt)) {
        return false;
    }
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    flags = nb ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
    if (fcntl(s, F_SETFL, flags) != 0) {
        return false;
    }
#endif
    return true;
}

UNUSED static bool socket_poll(SOCKET s)
{
    while (program_running && !kcp_restart) {
        WSAPOLLFD pollfd = {
            .fd = s,
            .events = POLLIN,
            .revents = 0,
        };
        int res = WSAPoll(&pollfd, 1, SOCKET_POLL_INTERVAL_MS);
        if (res < 0) {
            return false;
        }
        else if (res > 0) {
            if (pollfd.revents & POLLIN) {
                return true;
            }
        }
    }
    return false;
}

#define NTR_IP_OCTET_SIZE (4)
#define NTR_MAC_SIZE (6)

extern atomic_uint_fast8_t ntr_ip_octet[NTR_IP_OCTET_SIZE];
extern atomic_uint_fast8_t ntr_ip_octet_incoming[NTR_IP_OCTET_SIZE];

extern int ntr_rp_port;
extern atomic_int ntr_rp_port_bound;
extern atomic_bool ntr_rp_port_changed;

enum ntr_kcp_mode_t {
    KCP_MODE_NONE,
    KCP_MODE_ON,
    KCP_MODE_ON_DELTA,
    KCP_MODE_COUNT,

    KCP_MODE_L_NONE = KCP_MODE_COUNT,
    KCP_MODE_L_ON,
    KCP_MODE_L_ON_DELTA,
};
struct ntr_rp_config_t {
    nk_bool top_screen_priority;
    int screen_priority_factor;
    int jpeg_quality;
    int bandwidth_limit;
    int kcp_mode;
    int lossless_color;
};

extern struct ntr_rp_config_t ntr_rp_config;
void ntr_config_set_default(void);

extern bool ntr_top_screen_priority_prev;
extern int ntr_screen_priority_factor_prev;

extern char **ntr_auto_ip_list;
extern uint8_t **ntr_auto_ip_octet_list;
extern int ntr_auto_ip_count;

extern int ntr_selected_ip;
extern int ntr_selected_adapter;

extern char **ntr_adapter_list;
extern uint8_t **ntr_adapter_octet_list;
extern int ntr_adapter_count;

enum {
    NTR_ADAPTER_PRE_ANY,
    NTR_ADAPTER_PRE_COUNT,
};

enum {
    NTR_ADAPTER_POST_AUTO,
    NTR_ADAPTER_POST_REFRESH,
    NTR_ADAPTER_POST_COUNT,
};

#define NTR_ADAPTER_EXTRA_COUNT (NTR_ADAPTER_PRE_COUNT + NTR_ADAPTER_POST_COUNT)

void ntr_try_auto_select_adapter(void);
void ntr_detect_3ds_ip(void);
void ntr_get_adapter_list(void);

extern nk_bool ntr_stats_overlay;
extern nk_bool ntr_auto_reconnect;
extern nk_bool ntr_auto_update_params;

#endif
