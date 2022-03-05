#include <stdint.h>

#define RP_DATA_TOP_BOT ((uint32_t)1 << 0)
#define RP_DATA_Y_UV ((uint32_t)1 << 1)
#define RP_DATA_FRAME_DELTA ((uint32_t)1 << 2)
#define RP_DATA_PREVIOUS_FRAME_DELTA ((uint32_t)1 << 3)
#define RP_DATA_SELECT_FRAME_DELTA ((uint32_t)1 << 4)
#define RP_DATA_HUFFMAN ((uint32_t)1 << 5)
#define RP_DATA_RLE ((uint32_t)1 << 6)
#define RP_DATA_YUV_LQ ((uint32_t)1 << 7)
#define RP_DATA_INTERLACE ((uint32_t)1 << 8)
#define RP_DATA_INTERLACE_EVEN_ODD ((uint32_t)1 << 9)
#define RP_DATA_DOWNSAMPLE ((uint32_t)1 << 10)
#define RP_DATA_DOWNSAMPLE2 ((uint32_t)1 << 11)

#define BITS_PER_BYTE 8
#define ENCODE_SELECT_MASK_X_SCALE 1
#define ENCODE_SELECT_MASK_Y_SCALE 8
#define ENCODE_SELECT_MASK_STRIDE(h) \
    (((h) + (ENCODE_SELECT_MASK_Y_SCALE * BITS_PER_BYTE) - 1) / (ENCODE_SELECT_MASK_Y_SCALE * BITS_PER_BYTE))
#define ENCODE_SELECT_MASK_SIZE(w, h) \
    (ENCODE_SELECT_MASK_STRIDE(h) * (((w) + ENCODE_SELECT_MASK_X_SCALE - 1) / ENCODE_SELECT_MASK_X_SCALE))

#define ENCODE_UPSAMPLE_CARRY_SIZE(w, h) (((h) + BITS_PER_BYTE - 1) / BITS_PER_BYTE * (w))

typedef struct _DataHeader
{
    uint32_t flags;
    uint32_t len;
    uint32_t id;
    uint32_t uncompressed_len;
} DataHeader;

uint8_t *frame_decode(DataHeader header, uint8_t *data, int data_size, uint8_t *data2, int data2_size, int adata);
void frame_decode_init(int interlaced);
void frame_decode_destroy(void);
void yadif_start(void);
void yadif_stop(void);
