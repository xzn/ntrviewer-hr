#include <stdint.h>

#define RP_DATA_TOP ((uint32_t)1 << 0)
#define RP_DATA_Y ((uint32_t)1 << 1)
#define RP_DATA_DS ((uint32_t)1 << 2)
#define RP_DATA_FD ((uint32_t)1 << 3)
#define RP_DATA_PFD ((uint32_t)1 << 4)
#define RP_DATA_SPFD ((uint32_t)1 << 5)
#define RP_DATA_RLE ((uint32_t)1 << 6)
#define RP_DATA_LQ ((uint32_t)1 << 7)
#define RP_DATA_IL ((uint32_t)1 << 8)
#define RP_DATA_ILO ((uint32_t)1 << 9)

#define BITS_PER_BYTE 8
#define ENCODE_SELECT_MASK_X_SCALE 1
#define ENCODE_SELECT_MASK_Y_SCALE 8
#define ENCODE_SELECT_MASK_SIZE(w, h)                                                                       \
    (h + (ENCODE_SELECT_MASK_Y_SCALE * BITS_PER_BYTE) - 1) / (ENCODE_SELECT_MASK_Y_SCALE * BITS_PER_BYTE) * \
        (w + ENCODE_SELECT_MASK_X_SCALE - 1) / ENCODE_SELECT_MASK_X_SCALE

typedef struct _DataHeader
{
    uint32_t flags;
    uint32_t len;
    uint32_t id;
    uint32_t uncompressed_len;
} DataHeader;

uint8_t *frame_decode(DataHeader header, uint8_t *data, int data_size, uint8_t *data2, int data2_size);
void frame_decode_init();
void frame_decode_destroy();
