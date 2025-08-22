#ifndef NTR_HB_H
#define NTR_HB_H

#include "const.h"
#include <stdatomic.h>

struct tcp_thread_arg {
    atomic_int *work_state;
    atomic_int *work_req_state;
    atomic_bool *remote_play;
    short port;
};

extern atomic_int menu_work_state, nwm_work_state;
extern atomic_int menu_work_req_state, nwm_work_req_state;
extern atomic_bool menu_remote_play;

enum connection_state_t
{
    CONNECTION_STATE_DISCONNECTED,
    CONNECTION_STATE_CONNECTED,
    CONNECTION_STATE_COUNT,
};
enum connection_req_state_t
{
    CONNECTION_REQ_STATE_NONE,
    CONNECTION_REQ_STATE_CONNECTING,
    CONNECTION_REQ_STATE_DISCONNECTING,
    CONNECTION_REQ_STATE_COUNT,
};

thread_ret_t tcp_thread_func(void *arg);

#endif
