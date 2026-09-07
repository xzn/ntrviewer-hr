#include "ntr_common.h"
#include "const.h"

#include <stdlib.h>

#ifdef _WIN32
int socket_startup(void) {
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(2, 2), &wsa_data);
}
int socket_shutdown(void) {
    return WSACleanup();
}
#include <ipifcons.h>
#include <iptypes.h>
#define NTR_IP_NAME_LEN_MAX (16 + MAX_ADAPTER_DESCRIPTION_LENGTH + 4)
#else
#include <net/if.h>
#define NTR_IP_NAME_LEN_MAX (16 + IFNAMSIZ + 4)
#endif

int ntr_rp_port = 8001;
atomic_int ntr_rp_port_bound;
atomic_bool ntr_rp_port_changed;

struct ntr_rp_config_t ntr_rp_config;
void ntr_config_set_default(void) {
    ntr_rp_config = (struct ntr_rp_config_t){
        .top_screen_priority = 1,
        .screen_priority_factor = NTR_SCREEN_PRIORITY_FACTOR_DEFAULT,
        .jpeg_quality = 75,
        .bandwidth_limit = 16,
        .kcp_mode = KCP_MODE_ON_DELTA,
        .lossless_color = 1,
        .audio_enable = ntr_rp_config.audio_enable,
    };
    ntr_auto_q_reset_all();
}
bool ntr_top_screen_priority_prev;
int ntr_screen_priority_factor_prev;

char **ntr_auto_ip_list;
uint8_t **ntr_auto_ip_octet_list;
int ntr_auto_ip_count;

static void ntr_free_auto_ip_list(void) {
    if(ntr_auto_ip_count) {
        for (int i = 0; i < ntr_auto_ip_count; ++i) {
            free(ntr_auto_ip_list[i]);
            free(ntr_auto_ip_octet_list[i]);
        }
        free(ntr_auto_ip_list);
        free(ntr_auto_ip_octet_list);
        ntr_auto_ip_list = 0;
        ntr_auto_ip_octet_list = 0;
        ntr_auto_ip_count = 0;
    }
}

static int ntr_alloc_auto_ip_list(int count) {
    if (count) {
        ntr_auto_ip_list = malloc(sizeof(*ntr_auto_ip_list) * count);
        if (!ntr_auto_ip_list) {
            err_log("malloc ntr_auto_ip_list failed\n");
            goto fail;
        }
        memset(ntr_auto_ip_list, 0, sizeof(*ntr_auto_ip_list) * count);

        ntr_auto_ip_octet_list = malloc(sizeof(*ntr_auto_ip_octet_list) * count);
        if (!ntr_auto_ip_octet_list) {
            err_log("malloc ntr_auto_ip_list failed\n");
            goto fail;
        }
        memset(ntr_auto_ip_octet_list, 0, sizeof(*ntr_auto_ip_octet_list) * count);

        for (int i = 0; i < count; ++i) {
            ntr_auto_ip_list[i] = malloc(NTR_IP_NAME_LEN_MAX);
            if (!ntr_auto_ip_list[i]) {
                err_log("malloc ntr_auto_ip_list[i] failed\n");
                goto fail;
            }

            ntr_auto_ip_octet_list[i] = malloc(NTR_IP_OCTET_SIZE);
            if (!ntr_auto_ip_octet_list[i]) {
                err_log("malloc ntr_auto_ip_octet_list[i] failed\n");
                goto fail;
            }
        }
        ntr_auto_ip_count = count;
    }
    return 0;

fail:
    if (ntr_auto_ip_list) {
        for (int i = 0; i < count; ++i) {
            if (ntr_auto_ip_list[i]) {
                free(ntr_auto_ip_list[i]);
            }
        }

        free(ntr_auto_ip_list);
        ntr_auto_ip_list = 0;
    }

    if (ntr_auto_ip_octet_list) {
        for (int i = 0; i < count; ++i) {
            if (ntr_auto_ip_octet_list[i]) {
                free(ntr_auto_ip_octet_list[i]);
            }
        }

        free(ntr_auto_ip_octet_list);
        ntr_auto_ip_octet_list = 0;
    }

    return -1;
}

int ntr_selected_ip;
int ntr_selected_adapter;

char **ntr_adapter_list;
uint8_t **ntr_adapter_octet_list;
int ntr_adapter_count;

// taken from Boop's source code https://github.com/miltoncandelero/Boop
static uint8_t const known_mac_list[][3] = {
    { 0x00, 0x09, 0xBF }, { 0x00, 0x16, 0x56 }, { 0x00, 0x17, 0xAB }, { 0x00, 0x19, 0x1D }, { 0x00, 0x19, 0xFD },
    { 0x00, 0x1A, 0xE9 }, { 0x00, 0x1B, 0x7A }, { 0x00, 0x1B, 0xEA }, { 0x00, 0x1C, 0xBE }, { 0x00, 0x1D, 0xBC },
    { 0x00, 0x1E, 0x35 }, { 0x00, 0x1E, 0xA9 }, { 0x00, 0x1F, 0x32 }, { 0x00, 0x1F, 0xC5 }, { 0x00, 0x21, 0x47 },
    { 0x00, 0x21, 0xBD }, { 0x00, 0x22, 0x4C }, { 0x00, 0x22, 0xAA }, { 0x00, 0x22, 0xD7 }, { 0x00, 0x23, 0x31 },
    { 0x00, 0x23, 0xCC }, { 0x00, 0x24, 0x1E }, { 0x00, 0x24, 0x44 }, { 0x00, 0x24, 0xF3 }, { 0x00, 0x25, 0xA0 },
    { 0x00, 0x26, 0x59 }, { 0x00, 0x27, 0x09 }, { 0x04, 0x03, 0xD6 }, { 0x18, 0x2A, 0x7B }, { 0x2C, 0x10, 0xC1 },
    { 0x34, 0xAF, 0x2C }, { 0x40, 0xD2, 0x8A }, { 0x40, 0xF4, 0x07 }, { 0x58, 0x2F, 0x40 }, { 0x58, 0xBD, 0xA3 },
    { 0x5C, 0x52, 0x1E }, { 0x60, 0x6B, 0xFF }, { 0x64, 0xB5, 0xC6 }, { 0x78, 0xA2, 0xA0 }, { 0x7C, 0xBB, 0x8A },
    { 0x8C, 0x56, 0xC5 }, { 0x8C, 0xCD, 0xE8 }, { 0x98, 0xB6, 0xE9 }, { 0x9C, 0xE6, 0x35 }, { 0xA4, 0x38, 0xCC },
    { 0xA4, 0x5C, 0x27 }, { 0xA4, 0xC0, 0xE1 }, { 0xB8, 0x78, 0x26 }, { 0xB8, 0x8A, 0xEC }, { 0xB8, 0xAE, 0x6E },
    { 0xCC, 0x9E, 0x00 }, { 0xCC, 0xFB, 0x65 }, { 0xD8, 0x6B, 0xF7 }, { 0xDC, 0x68, 0xEB }, { 0xE0, 0x0C, 0x7F },
    { 0xE0, 0xE7, 0x51 }, { 0xE8, 0x4E, 0xCE }, { 0xEC, 0xC4, 0x0D }, { 0xE8, 0x4E, 0xCE }
};

static void ntr_free_adapter_list(void) {
    if(ntr_adapter_count) {
        for (int i = 0; i < ntr_adapter_count; ++i) {
            free(ntr_adapter_list[i]);
            free(ntr_adapter_octet_list[i]);
        }
        free(ntr_adapter_list);
        free(ntr_adapter_octet_list);
        ntr_adapter_list = 0;
        ntr_adapter_octet_list = 0;
        ntr_adapter_count = 0;
    }
}

static int ntr_alloc_adapter_list(int count) {
    if (count) {
        ntr_adapter_list = malloc(sizeof(*ntr_adapter_list) * count);
        if (!ntr_adapter_list) {
            err_log("malloc ntr_adapter_list failed\n");
            goto fail;
        }
        memset(ntr_adapter_list, 0, sizeof(*ntr_adapter_list) * count);

        ntr_adapter_octet_list = malloc(sizeof(*ntr_adapter_octet_list) * count);
        if (!ntr_adapter_octet_list) {
            err_log("malloc ntr_adapter_octet_list failed\n");
            goto fail;
        }
        memset(ntr_adapter_octet_list, 0, sizeof(*ntr_adapter_octet_list) * count);

        for (int i = 0; i < count; ++i) {
            ntr_adapter_list[i] = malloc(NTR_IP_NAME_LEN_MAX);
            if (!ntr_adapter_list[i]) {
                err_log("malloc ntr_adapter_list[i] failed\n");
                goto fail;
            }

            ntr_adapter_octet_list[i] = malloc(NTR_IP_OCTET_SIZE);
            if (!ntr_adapter_octet_list[i]) {
                err_log("malloc ntr_adapter_octet_list[i] failed\n");
                goto fail;
            }
        }
        ntr_adapter_count = count;
    }
    return 0;

fail:
    if (ntr_adapter_list) {
        for (int i = 0; i < count; ++i) {
            if (ntr_adapter_list[i]) {
                free(ntr_adapter_list[i]);
            }
        }

        free(ntr_adapter_list);
        ntr_adapter_list = 0;
    }

    if (ntr_adapter_octet_list) {
        for (int i = 0; i < count; ++i) {
            if (ntr_adapter_octet_list[i]) {
                free(ntr_adapter_octet_list[i]);
            }
        }

        free(ntr_adapter_octet_list);
        ntr_adapter_octet_list = 0;
    }

    return -1;
}

atomic_uint_fast8_t ntr_ip_octet[NTR_IP_OCTET_SIZE];
atomic_uint_fast8_t ntr_ip_octet_incoming[NTR_IP_OCTET_SIZE];

static bool ntr_get_auto_ip_from_ip_octet(uint32_t a) {
    for (int i = 0; i < ntr_auto_ip_count; ++i) {
        uint32_t b = *(uint32_t *)ntr_auto_ip_octet_list[i];
        if (a == b) {
            ntr_selected_ip = i;
            return true;
        }
    }
    return false;
}

static uint32_t ntr_get_identical_bits_prefix(uint32_t a, uint32_t b) {
    return ~__builtin_bswap32(a ^ b);
}

void ntr_try_auto_select_adapter(void) {
    uint32_t incoming = *(uint32_t *)ntr_ip_octet_incoming;
    if (incoming && ntr_get_auto_ip_from_ip_octet(incoming))
        *(uint32_t *)ntr_ip_octet = incoming;

    ntr_selected_adapter = 0;
    uint32_t count = 0;
    for (int i = NTR_ADAPTER_PRE_COUNT; i < ntr_adapter_count - NTR_ADAPTER_POST_COUNT; ++i) {
        uint32_t bits = ntr_get_identical_bits_prefix(*(uint32_t *)ntr_ip_octet, *(uint32_t *)ntr_adapter_octet_list[i]);
        if (bits > count) {
            count = bits;
            ntr_selected_adapter = i;
        }
    }

    ntr_rp_port_changed = 1;
    kcp_restart = 1;
}

#define NTR_AUTO_IP_PRE_COUNT (1)

#ifdef _WIN32
#include <iphlpapi.h>

static PMIB_IPNETTABLE ip_net_buf = 0;
static ULONG ip_net_buf_size = 0;

static void get_ip_map_mac(void) {
    if (ip_net_buf) {
        free(ip_net_buf);
        ip_net_buf = 0;
        ip_net_buf_size = 0;
    }

    ip_net_buf_size = 0;
    if (GetIpNetTable(NULL, &ip_net_buf_size, TRUE) == ERROR_INSUFFICIENT_BUFFER) {
        ip_net_buf = malloc(ip_net_buf_size);
        if (!ip_net_buf) {
            ip_net_buf_size = 0;
            err_log("malloc ip_net_buf failed\n");
            return;
        }
        ULONG ret = GetIpNetTable(ip_net_buf, &ip_net_buf_size, TRUE);
        if (ret == NO_ERROR) {
            return;
        } else {
            err_log("GetIpNetTable failed: %d\n", (int)ret);
            free(ip_net_buf);
            ip_net_buf = 0;
            ip_net_buf_size = 0;
        }
    } else {
        ip_net_buf_size = 0;
    }
}

static int match_mac(UCHAR *mac) {
    // err_log("%02x-%02x-%02x\n", (int)mac[0], (int)mac[1], (int)mac[2]);
    for (unsigned i = 0; i < sizeof(known_mac_list) / sizeof(*known_mac_list); ++i) {
        if (memcmp(mac, known_mac_list[i], 3) == 0)
            return 1;
    }
    return 0;
}

void ntr_detect_3ds_ip(void) {
    get_ip_map_mac();

    int detected_ip_count = 0;
    int *map_index = 0;
    if (ip_net_buf_size) {
        map_index = malloc(ip_net_buf->dwNumEntries * sizeof(*map_index));
        if (!map_index) {
            err_log("malloc map_index failed\n");
            goto fail;
        }
        for (unsigned i = 0; i < ip_net_buf->dwNumEntries; ++i) {
            PMIB_IPNETROW entry = &ip_net_buf->table[i];
            if (entry->dwType != MIB_IPNET_TYPE_INVALID) {
                if (entry->dwPhysAddrLen == NTR_MAC_SIZE) {
                    if (match_mac(entry->bPhysAddr)) {
                        map_index[detected_ip_count] = i;
                        ++detected_ip_count;
                    }
                }
            }
        }
    }

    ntr_free_auto_ip_list();
    if (ntr_alloc_auto_ip_list(detected_ip_count + NTR_AUTO_IP_PRE_COUNT)) {
        goto fail;
    }

    if (detected_ip_count) {
        strcpy(ntr_auto_ip_list[0], "");
    } else {
        strcpy(ntr_auto_ip_list[0], "None Detected");
    }
    memset(ntr_auto_ip_octet_list[0], 0, NTR_IP_OCTET_SIZE);

    for (int i = 0; i < detected_ip_count; ++i) {
        PMIB_IPNETROW entry = &ip_net_buf->table[map_index[i]];
        uint8_t *octets = (uint8_t *)&entry->dwAddr;
        sprintf(ntr_auto_ip_list[i + NTR_AUTO_IP_PRE_COUNT], "%d.%d.%d.%d", (int)octets[0], (int)octets[1], (int)octets[2], (int)octets[3]);
        memcpy(ntr_auto_ip_octet_list[i + NTR_AUTO_IP_PRE_COUNT], &entry->dwAddr, NTR_IP_OCTET_SIZE);
    }
    free(map_index);

    ntr_selected_ip = detected_ip_count ? NTR_AUTO_IP_PRE_COUNT : 0;
    memcpy(ntr_ip_octet, ntr_auto_ip_octet_list[ntr_selected_ip], NTR_IP_OCTET_SIZE);

    return;

fail:
    return;
}

static PIP_ADAPTER_INFO adapter_info_list;
static ULONG adapter_info_list_size;

static uint32_t parse_ip_address(const char *ip) {
    return inet_addr(ip);
}

static int get_adapter_count(void) {
    int count = 0;
    if (adapter_info_list && adapter_info_list_size) {
        PIP_ADAPTER_INFO next = adapter_info_list;
        while (next) {
            if (next->Type == MIB_IF_TYPE_ETHERNET || next->Type == IF_TYPE_IEEE80211) {
                PIP_ADDR_STRING ip = &next->IpAddressList;
                while (ip) {
                    if (parse_ip_address(ip->IpAddress.String) != 0)
                    ++count;
                    ip = ip->Next;
                }
            }
            next = next->Next;
        }
    }
    return count;
}

static void update_adapter_list(void) {
    ntr_free_adapter_list();

    int count = get_adapter_count();

    if (ntr_alloc_adapter_list(count + NTR_ADAPTER_EXTRA_COUNT)) {
        goto fail;
    }

    strcpy(ntr_adapter_list[0], "0.0.0.0 (Any)");
    memset(ntr_adapter_octet_list[0], 0, NTR_IP_OCTET_SIZE);

    if (adapter_info_list && adapter_info_list_size) {
        PIP_ADAPTER_INFO next = adapter_info_list;
        for (int i = 0; i < count && next;) {
            if (next->Type == MIB_IF_TYPE_ETHERNET || next->Type == IF_TYPE_IEEE80211) {
                PIP_ADDR_STRING ip = &next->IpAddressList;
                while (ip) {
                    int addr;
                    if ((addr = parse_ip_address(ip->IpAddress.String)) != 0) {
                        snprintf(ntr_adapter_list[i + NTR_ADAPTER_PRE_COUNT], NTR_IP_NAME_LEN_MAX, "%s %s", ip->IpAddress.String, next->Description);
                        memcpy(ntr_adapter_octet_list[i + NTR_ADAPTER_PRE_COUNT], &addr, NTR_IP_OCTET_SIZE);
                        ++i;
                    }
                    ip = ip->Next;
                }
            }
            next = next->Next;
        }
    }

    strcpy(ntr_adapter_list[NTR_ADAPTER_PRE_COUNT + count + NTR_ADAPTER_POST_AUTO], "Auto-Select");
    memset(ntr_adapter_octet_list[NTR_ADAPTER_PRE_COUNT + count + NTR_ADAPTER_POST_AUTO], 0, NTR_IP_OCTET_SIZE);

    strcpy(ntr_adapter_list[NTR_ADAPTER_PRE_COUNT + count + NTR_ADAPTER_POST_REFRESH], "Refresh List");
    memset(ntr_adapter_octet_list[NTR_ADAPTER_PRE_COUNT + count + NTR_ADAPTER_POST_REFRESH], 0, NTR_IP_OCTET_SIZE);

    ntr_try_auto_select_adapter();

    return;

fail:
    return;
}

void ntr_get_adapter_list(void) {
    if (adapter_info_list) {
        free(adapter_info_list);
        adapter_info_list = 0;
        adapter_info_list_size = 0;
    }

    ULONG ret = GetAdaptersInfo(adapter_info_list, &adapter_info_list_size);
    if (ret == ERROR_BUFFER_OVERFLOW) {
        adapter_info_list = malloc(adapter_info_list_size);
        if (!adapter_info_list) {
            err_log("malloc adapter_info_list failed\n");
            goto fail;
        }
        ret = GetAdaptersInfo(adapter_info_list, &adapter_info_list_size);
        if (ret == ERROR_SUCCESS) {
        } else {
            err_log("GetAdaptersInfo failed: %d\n", (int)ret);
            free(adapter_info_list);
            adapter_info_list = 0;
            adapter_info_list_size = 0;
        }
    } else if (ret != ERROR_SUCCESS) {
        err_log("GetAdaptersInfo failed: %d\n", (int)ret);
        adapter_info_list_size = 0;
    }

    update_adapter_list();
    return;

fail:
    return;
}
#else

struct ip_map_mac_t {
    uint8_t ip_bytes[NTR_IP_OCTET_SIZE];
    uint8_t mac_bytes[NTR_MAC_SIZE];
};

static struct ip_map_mac_t *ip_net_buf = 0;
static size_t ip_net_buf_count = 0;

static void clear_ip_map_mac(void) {
    if (ip_net_buf) {
        free(ip_net_buf);
        ip_net_buf = 0;
        ip_net_buf_count = 0;
    }
}

static int match_mac(uint8_t *mac)
{
    for (unsigned i = 0; i < sizeof(known_mac_list) / sizeof(*known_mac_list); ++i) {
        if (memcmp(mac, known_mac_list[i], 3) == 0)
            return 1;
    }
    return 0;
}

static void get_ntr_detect_3ds_ip(void) {
    unsigned detected_ip_count = 0;
    unsigned *map_index = 0;
    if (ip_net_buf_count) {
        map_index = malloc(ip_net_buf_count * sizeof(*map_index));
        for (unsigned i = 0; i < ip_net_buf_count; ++i) {
            if (match_mac(ip_net_buf[i].mac_bytes)) {
                map_index[detected_ip_count] = i;
                ++detected_ip_count;
            }
        }
    }

    ntr_free_auto_ip_list();
    ntr_alloc_auto_ip_list(detected_ip_count + NTR_AUTO_IP_PRE_COUNT);

    if (detected_ip_count) {
        strcpy(ntr_auto_ip_list[0], "");
    } else {
        strcpy(ntr_auto_ip_list[0], "None Detected");
    }
    memset(ntr_auto_ip_octet_list[0], 0, NTR_IP_OCTET_SIZE);

    for (unsigned i = 0; i < detected_ip_count; ++i) {
        struct ip_map_mac_t *b = &ip_net_buf[map_index[i]];
        sprintf(ntr_auto_ip_list[i + NTR_AUTO_IP_PRE_COUNT], "%d.%d.%d.%d",
                (int)b->ip_bytes[0],
                (int)b->ip_bytes[1],
                (int)b->ip_bytes[2],
                (int)b->ip_bytes[3]);
        memcpy(ntr_auto_ip_octet_list[i + NTR_AUTO_IP_PRE_COUNT], b->ip_bytes, NTR_IP_OCTET_SIZE);
    }
    free(map_index);

    ntr_selected_ip = detected_ip_count ? NTR_AUTO_IP_PRE_COUNT : 0;
    memcpy(ntr_ip_octet, ntr_auto_ip_octet_list[ntr_selected_ip], NTR_IP_OCTET_SIZE);
}

#ifdef __APPLE__
#include "ui_main_nk.h"

#define READ_END 0
#define WRITE_END 1
static bool detecting_3ds;

void *do_ntr_detect_3ds_ip(void *) {
    pid_t pid;
    int fd[2];

    if (pipe(fd) < 0) {
        goto fail;
    }
    pid = fork();
    if (pid == -1) {
        goto fail_fork;
    }
    if (pid == 0) {
        // child
        if (close(fd[READ_END]) == -1) {
            goto fail_child_close;
        }
        if (dup2(fd[WRITE_END], STDOUT_FILENO) == -1) {
            goto fail_child_close;
        }
        if (close(fd[WRITE_END]) == -1) {
            goto fail_child;
        }
#define ARP_CMD "arp"
#define ARP_ARGS "-n", "-a", NULL
        execlp(ARP_CMD, ARP_ARGS);
        err_log("execlp %s failed: %d\n", ARP_CMD, errno);
        exit(-2);

fail_child_close:
        close(fd[WRITE_END]);
fail_child:
        exit(-1);
    } else {
        // parent
        if (close(fd[WRITE_END]) == -1) {
            goto fail_parent;
        }

        FILE *file = fdopen(fd[READ_END], "r");
        if (!file) {
            goto fail_parent;
        }

        clear_ip_map_mac();

        int count = 0;
        char *line = NULL;
        size_t size = 0;
        ssize_t nread = 0;
        while ((nread = getline(&line, &size, file)) != -1) {
            char *next_tok = line;
            char *tok = NULL;
#define ARP_IP_FIELD_I (1)
#define ARP_MAC_FIELD_I (3)
#define ARP_END_FIELD_I (ARP_MAC_FIELD_I + 1)

            char *ip = NULL;
            char *mac = NULL;
            for (int i = 0; i < ARP_END_FIELD_I; ++i) {
                tok = strsep(&next_tok, " ");
                if (i == ARP_IP_FIELD_I) {
                    ip = tok;
                } else if (i == ARP_MAC_FIELD_I) {
                    mac = tok;
                }
            }
            if (!ip || !mac) {
                continue;
            }

            int next_count = count + 1;
            ip_net_buf = realloc(ip_net_buf, next_count * sizeof(struct ip_map_mac_t));
            struct ip_map_mac_t *b = &ip_net_buf[count];
            sscanf(ip, "(%hhu.%hhu.%hhu.%hhu)",
                    &b->ip_bytes[0],
                    &b->ip_bytes[1],
                    &b->ip_bytes[2],
                    &b->ip_bytes[3]);
            sscanf(mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                    &b->mac_bytes[0],
                    &b->mac_bytes[1],
                    &b->mac_bytes[2],
                    &b->mac_bytes[3],
                    &b->mac_bytes[4],
                    &b->mac_bytes[5]);
            count = next_count;
        }
        ip_net_buf_count = count;
        free(line);

fail_parent:
        close(fd[READ_END]);
        goto fail;
    }

fail_fork:
    close(fd[READ_END]);
    close(fd[WRITE_END]);
fail:
    rp_lock_wait(ui_nk_lock);
    get_ntr_detect_3ds_ip();
    ntr_get_adapter_list();
    rp_lock_rel(ui_nk_lock);

    __atomic_clear(&detecting_3ds, __ATOMIC_RELAXED);

    pthread_exit(0);
}

void ntr_detect_3ds_ip(void) {
    if (__atomic_test_and_set(&detecting_3ds, __ATOMIC_RELAXED)) {
        return;
    }

    pthread_t thread;
    if (pthread_create(&thread, NULL, do_ntr_detect_3ds_ip, NULL) != 0) {
        err_log("pthread_create failed\n");
        __atomic_clear(&detecting_3ds, __ATOMIC_RELAXED);
    }
}
#else
// Taken from stackexchange
// https://codereview.stackexchange.com/a/58107
#include <stdio.h>

#define xstr(s) str(s)
#define str(s) #s

#define ARP_CACHE "/proc/net/arp"
#define ARP_LINE_FORMAT \
    "%" xstr(ARP_STRING_LEN) "s %*s %*s " \
    "%" xstr(ARP_STRING_LEN) "s %*s " \
    "%" xstr(ARP_STRING_LEN) "s"

static void get_ip_map_mac(void)
{
    clear_ip_map_mac();

    FILE *arp_cache = fopen(ARP_CACHE, "r");
    if (!arp_cache)
        return;

    char *line = NULL;
    size_t size = 0;
    ssize_t nread = 0;

    // ignore the first line, which contains the header
    if ((nread = getline(&line, &size, arp_cache)) == -1)
        goto final;

    int count = 0;
    while ((nread = getline(&line, &size, arp_cache)) != -1) {
        char *next_tok = line;
        char *tok = NULL;
#define ARP_IP_FIELD_I (0)
#define ARP_MAC_FIELD_I (3)
#define ARP_END_FIELD_I (ARP_MAC_FIELD_I + 1)

        char *ip = NULL;
        char *mac = NULL;
        for (int i = 0; i < ARP_END_FIELD_I; ++i) {
            while ((tok = strsep(&next_tok, " ")) && strlen(tok) == 0) {}
            if (!tok)
                break;
            if (i == ARP_IP_FIELD_I) {
                ip = tok;
            } else if (i == ARP_MAC_FIELD_I) {
                mac = tok;
            }
        }

        if (!ip || !mac) {
            continue;
        }
        int next_count = count + 1;
        ip_net_buf = realloc(ip_net_buf, next_count * sizeof(struct ip_map_mac_t));
        struct ip_map_mac_t *b = &ip_net_buf[count];
        sscanf(ip, "%hhu.%hhu.%hhu.%hhu",
                &b->ip_bytes[0],
                &b->ip_bytes[1],
                &b->ip_bytes[2],
                &b->ip_bytes[3]);
        sscanf(mac, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                &b->mac_bytes[0],
                &b->mac_bytes[1],
                &b->mac_bytes[2],
                &b->mac_bytes[3],
                &b->mac_bytes[4],
                &b->mac_bytes[5]);
        count = next_count;
    }
    ip_net_buf_count = count;

final:
    fclose(arp_cache);
    return;
}

void ntr_detect_3ds_ip(void)
{
    get_ip_map_mac();
    get_ntr_detect_3ds_ip();
}
#endif

// Taken from stackoverflow
// https://stackoverflow.com/a/12131131
#include <ifaddrs.h>
#include <netdb.h>

void ntr_get_adapter_list(void)
{
    ntr_free_adapter_list();

    int count = 0;

    struct ifaddrs *ifaddr = 0, *ifa;
    if (getifaddrs(&ifaddr) != -1) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL)
                continue;

            if (!(ifa->ifa_flags & IFF_RUNNING)) {
                continue;
            }

            if (ifa->ifa_addr->sa_family != AF_INET) {
                continue;
            }

            char host[NI_MAXHOST] = {0};
            int s = getnameinfo(
                ifa->ifa_addr,
                sizeof(struct sockaddr_in),
                host,
                NI_MAXHOST,
                NULL,
                0,
                NI_NUMERICHOST);

            if (s == 0)
                ++count;
        }
    }

    ntr_alloc_adapter_list(count + NTR_ADAPTER_EXTRA_COUNT);

    strcpy(ntr_adapter_list[0], "0.0.0.0 (Any)");
    memset(ntr_adapter_octet_list[0], 0, NTR_IP_OCTET_SIZE);

    if (count) {
        int i = NTR_ADAPTER_PRE_COUNT;
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL)
                continue;

            if (!(ifa->ifa_flags & IFF_RUNNING)) {
                continue;
            }

            if (ifa->ifa_addr->sa_family != AF_INET) {
                continue;
            }

            char host[NI_MAXHOST] = {0};
            int s = getnameinfo(
                ifa->ifa_addr,
                sizeof(struct sockaddr_in),
                host,
                NI_MAXHOST,
                NULL,
                0,
                NI_NUMERICHOST);

            if (s == 0) {
                sscanf(host, "%hhu.%hhu.%hhu.%hhu",
                       &ntr_adapter_octet_list[i][0],
                       &ntr_adapter_octet_list[i][1],
                       &ntr_adapter_octet_list[i][2],
                       &ntr_adapter_octet_list[i][3]);
                snprintf(
                    ntr_adapter_list[i],
                    NTR_IP_NAME_LEN_MAX,
                    "%d.%d.%d.%d %s",
                    (int)ntr_adapter_octet_list[i][0],
                    (int)ntr_adapter_octet_list[i][1],
                    (int)ntr_adapter_octet_list[i][2],
                    (int)ntr_adapter_octet_list[i][3],
                    ifa->ifa_name
                );

                ++i;
            }
        }
    }

    if (ifaddr) {
        freeifaddrs(ifaddr);
    }

    strcpy(ntr_adapter_list[NTR_ADAPTER_PRE_COUNT + count + NTR_ADAPTER_POST_AUTO], "Auto-Select");
    memset(ntr_adapter_octet_list[NTR_ADAPTER_PRE_COUNT + count + NTR_ADAPTER_POST_AUTO], 0, NTR_IP_OCTET_SIZE);

    strcpy(ntr_adapter_list[NTR_ADAPTER_PRE_COUNT + count + NTR_ADAPTER_POST_REFRESH], "Refresh List");
    memset(ntr_adapter_octet_list[NTR_ADAPTER_PRE_COUNT + count + NTR_ADAPTER_POST_REFRESH], 0, NTR_IP_OCTET_SIZE);

    ntr_try_auto_select_adapter();
}
#endif
