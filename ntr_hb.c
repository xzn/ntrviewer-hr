#include "ntr_hb.h"
#include "main.h"
#include "ntr_common.h"

#include <stdlib.h>

#define HEART_BEAT_EVERY_MS 250

#define TCP_MAGIC 0x12345678
#define TCP_ARGS_COUNT 16

struct tcp_packet_hdr {
    uint32_t magic;
    uint32_t seq;
    uint32_t type;
    uint32_t cmd;
    uint32_t args[TCP_ARGS_COUNT];

    uint32_t data_len;
};

atomic_int menu_work_state, nwm_work_state;
atomic_int menu_work_req_state, nwm_work_req_state;
atomic_bool menu_remote_play;

static int socket_close(SOCKET sock)
{
    int status = 0;

    status = shutdown(sock, SD_BOTH);
    if (status != 0) {
        err_log("socket shutdown failed: %d\n", socket_errno());
    }
    status = closesocket(sock);

    return status;
}

static SOCKET tcp_connect(int port, uint32_t addr)
{
    struct sockaddr_in servaddr = {0};
    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (!socket_valid(sockfd))
    {
        err_log("socket creation failed: %d\n", socket_errno());
        return INVALID_SOCKET;
    }

    if (!socket_set_nonblock(sockfd, 1))
    {
        err_log("socket_set_nonblock failed: %d\n", socket_errno());
        closesocket(sockfd);
        return INVALID_SOCKET;
    }

    servaddr.sin_family = AF_INET;
    char ip_addr_buf[16];
    snprintf(
        ip_addr_buf, sizeof(ip_addr_buf),
        "%d.%d.%d.%d",
        (int)((uint8_t *)&addr)[0],
        (int)((uint8_t *)&addr)[1],
        (int)((uint8_t *)&addr)[2],
        (int)((uint8_t *)&addr)[3]);
    servaddr.sin_addr.s_addr = htonl(__builtin_bswap32(addr));
    servaddr.sin_port = htons(port);

    err_log("connecting to %s:%d ...\n", ip_addr_buf, port);
    int ret = connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    if (ret != 0 && socket_errno() != WSAEWOULDBLOCK && socket_errno() != EINPROGRESS)
    {
        err_log("connection failed: %d\n", socket_errno());
        socket_close(sockfd);
        return INVALID_SOCKET;
    }

    fd_set fdset;
    struct timeval tv;
    FD_ZERO(&fdset);
    FD_SET(sockfd, &fdset);
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    if (select(sockfd + 1, NULL, &fdset, NULL, &tv) == 1)
    {
        int so_error;
        socklen_t len = sizeof(so_error);

        getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char *)&so_error, &len);

        if (so_error == 0)
        {
            err_log("connected\n");
            return sockfd;
        }
        err_log("connection failed: %d\n", so_error);
    }

    closesocket(sockfd);
    err_log("connection timeout\n");
    return INVALID_SOCKET;
}

static int tcp_send(SOCKET sockfd, char *buf, int size)
{
    int ret, pos = 0;
    int tmpsize = size;

    while (program_running && tmpsize) {
        if ((ret = send(sockfd, &buf[pos], tmpsize, 0)) < 0) {
            if (socket_errno() == WSAEWOULDBLOCK) {
                if (socket_poll(sockfd)) {
                    continue;
                } else {
                    if (program_running)
                        err_log("socket poll failed: %d\n", socket_errno());
                    return -1;
                }
            }
            return ret;
        }
        pos += ret;
        tmpsize -= ret;
    }

    return size;
}

static int tcp_recv(SOCKET sockfd, char *buf, int size)
{
    int ret, pos = 0;
    int tmpsize = size;

    while (program_running && tmpsize) {
        if ((ret = recv(sockfd, &buf[pos], tmpsize, 0)) <= 0) {
            if (ret < 0) {
                if (socket_errno() == WSAEWOULDBLOCK) {
                    if (pos) {
                        if (socket_poll(sockfd)) {
                            continue;
                        } else {
                            if (program_running)
                                err_log("socket poll failed: %d\n", socket_errno());
                            return -1;
                        }
                    } else {
                        return 0;
                    }
                }
            }
            return ret;
        }
        pos += ret;
        tmpsize -= ret;
    }

    return size;
}

static int tcp_send_packet_header(SOCKET s, uint32_t seq, uint32_t type, uint32_t cmd, uint32_t *argv, int argc, uint32_t data_len)
{
    struct tcp_packet_hdr packet;
    packet.magic = TCP_MAGIC;
    packet.seq = seq;
    packet.type = type;
    packet.cmd = cmd;
    for (int i = 0; i < TCP_ARGS_COUNT; ++i) {
        if (i < argc) {
            packet.args[i] = argv[i];
        } else {
            packet.args[i] = 0;
        }
    }
    packet.data_len = data_len;

    char *buf = (char *)&packet;
    int size = sizeof(packet);
    return tcp_send(s, buf, size);
}

static bool rp_send_need_update;
static uint32_t rp_send_last_us;
static struct ntr_rp_config_t rp_config_last;
static int rp_port_last;

#include "ui_main_nk.h"

nk_bool ntr_auto_reconnect = nk_true;
nk_bool ntr_auto_update_params = nk_true;

thread_ret_t tcp_thread_func(void *arg)
{
    struct tcp_thread_arg *t = (struct tcp_thread_arg *)arg;

#define RESET_SOCKET() do { \
    socket_close(sockfd); \
    sockfd = INVALID_SOCKET; \
    *(t->work_state) = CONNECTION_STATE_DISCONNECTED; \
    *(t->work_req_state) = CONNECTION_REQ_STATE_NONE; \
    if (t->remote_play) { \
        *(t->remote_play) = 0; \
    } \
    err_log("disconnected\n"); \
} while (0)

    SOCKET sockfd = INVALID_SOCKET;
    int packet_seq = 0;
    while (program_running)
    {
        uint32_t ip_octet_incoming;
        uint32_t ip_octet;

        rp_lock_wait(ui_nk_lock);
        ip_octet_incoming = t->remote_play && ntr_auto_reconnect ? *(uint32_t *)ntr_ip_octet_incoming : 0;
        ip_octet = *(uint32_t *)ntr_ip_octet;
        rp_lock_rel(ui_nk_lock);

        if (
            *(t->work_state) == CONNECTION_STATE_DISCONNECTED &&
            (
                *(t->work_req_state) == CONNECTION_REQ_STATE_CONNECTING ||
                (ip_octet_incoming && ip_octet_incoming == ip_octet)
            )
        )
        {
            *(t->work_req_state) = CONNECTION_REQ_STATE_NONE;
            sockfd = tcp_connect(t->port, ip_octet);
            if (!socket_valid(sockfd))
            {
                if (t->remote_play)
                {
                    *(t->remote_play) = 0;
                }
                continue;
            }

            packet_seq = 0;
            *(t->work_state) = CONNECTION_STATE_CONNECTED;
        }
        else if (*(t->work_state) == CONNECTION_STATE_CONNECTED)
        {
            if (*(t->work_req_state) == CONNECTION_REQ_STATE_DISCONNECTING) {
                RESET_SOCKET();
                continue;
            }

            Sleep(HEART_BEAT_EVERY_MS);

            struct tcp_packet_hdr header = {0};
            char *buf = (char *)&header;
            int size = sizeof(header);
            int ret;
            if ((ret = tcp_recv(sockfd, buf, size)) < 0 || !program_running)
            {
                if (program_running)
                    err_log("tcp recv error: %d\n", socket_errno());
                RESET_SOCKET();
                continue;
            }
            if (ret)
            {
                if (header.magic != TCP_MAGIC)
                {
                    if (program_running)
                        err_log("broken protocol\n");
                    RESET_SOCKET();
                    continue;
                }
                if (header.cmd == 0)
                {
                    // err_log("heartbeat packet: size %d\n", header.data_len);
                    if (header.data_len)
                    {
                        char *buf = malloc(header.data_len + 1);
                        if ((ret = tcp_recv(sockfd, buf, header.data_len)) < 0)
                        {
                            if (program_running)
                                err_log("heart beat recv error: %d\n", socket_errno());
                            free(buf);
                            RESET_SOCKET();
                            continue;
                        }
                        if (ret)
                        {
                            buf[header.data_len] = 0;
                            fprintf(stderr, "%s", buf);
                        }
                        free(buf);
                    }
                }
                else if (header.data_len)
                {
                    err_log("unhandled packet type %d: size %d\n", header.cmd, header.data_len);
                    char *buf = malloc(header.data_len);
                    if ((ret = tcp_recv(sockfd, buf, header.data_len)) < 0)
                    {
                        if (program_running)
                            err_log("tcp recv error: %d\n", socket_errno());
                        free(buf);
                        RESET_SOCKET();
                        continue;
                    }
                    free(buf);
                }
            }

            ret = tcp_send_packet_header(sockfd, packet_seq, 0, 0, 0, 0, 0);
            if (ret < 0)
            {
                if (program_running)
                    err_log("heart beat send failed: %d\n", socket_errno());
                RESET_SOCKET();
                continue;
            }
            ++packet_seq;

            const uint32_t rp_send_next_us = iclock();
            const bool rp_send_update =
                memcmp(&rp_config_last, &ntr_rp_config, sizeof(struct ntr_rp_config_t)) ||
                rp_port_last != ntr_rp_port_bound;
            rp_send_need_update = rp_send_need_update || rp_send_update;
            const bool rp_send_wait_timeout = (int32_t)(rp_send_next_us - rp_send_last_us) > 1000000;
            if (t->remote_play && (*(t->remote_play) || rp_send_update || rp_send_wait_timeout))
            {
                if (!*(t->remote_play)) {
                    rp_send_last_us = rp_send_next_us;
                    memcpy(&rp_config_last, &ntr_rp_config, sizeof(struct ntr_rp_config_t));
                    rp_port_last = ntr_rp_port_bound;
                    if (!rp_send_wait_timeout || !rp_send_need_update || !ntr_auto_update_params)
                        continue;
                    rp_send_need_update = false;
                }

                *(t->remote_play) = 0;

                uint32_t args[] = {
                    ((uint32_t)rp_config_last.top_screen_priority << 8) | (uint32_t)rp_config_last.screen_priority_factor,
                    (uint32_t)rp_config_last.jpeg_quality,
                    (uint32_t)rp_config_last.bandwidth_limit * 128 * 1024,
                    1404036572 /* guarding magic */,
                    (uint32_t)rp_port_last |
                        (rp_config_last.kcp_mode ? (uint32_t)(1 << 30) : (uint32_t)0) |
                        (rp_config_last.kcp_mode == 2 ? (uint32_t)(1 << 31) : (uint32_t)0)};

                ret = tcp_send_packet_header(
                    sockfd, packet_seq, 0, 901,
                    args, sizeof(args) / sizeof(*args), 0);

                if (ret < 0)
                {
                    if (program_running)
                        err_log("remote play send failed: %d\n", socket_errno());
                    RESET_SOCKET();
                    continue;
                }
                ++packet_seq;

                rp_lock_wait(ui_nk_lock);
                switch (ui_view_mode) {
                    case VIEW_MODE_TOP:
                    case VIEW_MODE_BOT:
                        if (rp_config_last.screen_priority_factor) {
                            ui_view_mode = VIEW_MODE_TOP_BOT;
                        }
                        // fall-through
                    case VIEW_MODE_TOP_BOT:
                    case VIEW_MODE_SEPARATE:
                        if (!rp_config_last.screen_priority_factor) {
                            ui_view_mode = rp_config_last.top_screen_priority ? VIEW_MODE_TOP : VIEW_MODE_BOT;
                        }
                        break;
                }
                rp_lock_rel(ui_nk_lock);
            }
        }
        else
        {
            Sleep(REST_EVERY_MS);
        }
    }
    return 0;

#undef RESET_SOCKET
}
