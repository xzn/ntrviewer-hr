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

static SOCKET tcp_connect(int port)
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
        (int)ntr_ip_octet[0],
        (int)ntr_ip_octet[1],
        (int)ntr_ip_octet[2],
        (int)ntr_ip_octet[3]);
    uint32_t addr =
        (ntr_ip_octet[0] << 24) |
        (ntr_ip_octet[1] << 16) |
        (ntr_ip_octet[2] << 8) |
        ntr_ip_octet[3];
    servaddr.sin_addr.s_addr = htonl(addr);
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
        if (*(t->work_state) == CONNECTION_STATE_DISCONNECTED && *(t->work_req_state) == CONNECTION_REQ_STATE_CONNECTING)
        {
            *(t->work_req_state) = CONNECTION_REQ_STATE_NONE;
            sockfd = tcp_connect(t->port);
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

            if (t->remote_play && *(t->remote_play))
            {
                *(t->remote_play) = 0;

                uint32_t args[] = {
                    ((uint32_t)ntr_rp_config.top_screen_priority << 8) | (uint32_t)ntr_rp_config.screen_priority_factor,
                    (uint32_t)ntr_rp_config.jpeg_quality,
                    (uint32_t)ntr_rp_config.bandwidth_limit * 128 * 1024,
                    1404036572 /* guarding magic */,
                    (uint32_t)ntr_rp_port_bound |
                        (ntr_rp_config.kcp_mode ? (uint32_t)(1 << 30) : (uint32_t)0) |
                        (ntr_rp_config.kcp_mode == 2 ? (uint32_t)(1 << 31) : (uint32_t)0)};

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
