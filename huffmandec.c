#include "huffmancodec.h"
#include "commondef.h"
#include "bitdec.h"

#include <memory.h>

typedef struct HuffEntry
{
    uint8_t len;
    uint16_t sym;
} HuffmanEntry;

static inline int build_huff(const uint8_t *src, VLC *vlc, int *fsym, unsigned nb_elems)
{
    int i;
    HuffmanEntry *he = HR_MALLOC(sizeof(HuffmanEntry) * 1024);
    uint8_t *bits = HR_MALLOC(sizeof(uint8_t) * 1024);
    uint16_t *codes_count = HR_MALLOC(sizeof(uint16_t) * 33);
    memset(codes_count, 0, sizeof(uint16_t) * 33);

    *fsym = -1;
    for (i = 0; i < nb_elems; i++)
    {
        if (src[i] == 0)
        {
            *fsym = i;
            return 0;
        }
        else if (src[i] == 255)
        {
            bits[i] = 0;
        }
        else if (src[i] <= 32)
        {
            bits[i] = src[i];
        }
        else
            return -2;

        codes_count[bits[i]]++;
    }
    if (codes_count[0] == nb_elems)
        return -3;

    /* For Ut Video, longer codes are to the left of the tree and
     * for codes with the same length the symbol is descending from
     * left to right. So after the next loop --codes_count[i] will
     * be the index of the first (lowest) symbol of length i when
     * indexed by the position in the tree with left nodes being first. */
    for (int i = 31; i >= 0; i--)
        codes_count[i] += codes_count[i + 1];

    for (unsigned i = 0; i < nb_elems; i++)
        he[--codes_count[bits[i]]] = (HuffmanEntry){bits[i], i};

#define VLC_BITS 11
    int ret = ff_init_vlc_from_lengths(vlc, VLC_BITS, codes_count[0], &he[0].len, sizeof(*he), &he[0].sym, sizeof(*he), 2);

    HR_FREE(he);
    HR_FREE(bits);
    HR_FREE(codes_count);

    return ret;
}

int huffman_decode(uint8_t *dst, int dst_size, const uint8_t *src, int src_size)
{
    VLC vlc;
    int fsym, pix;
    int ret;
    GetBitContext gb;
    const uint8_t *src_end = src + src_size, *dst_end = dst + dst_size, *dst_begin = dst;

    if (src_size < 256)
    {
        return -1;
    }

    if ((ret = build_huff(src, &vlc, &fsym, 256)) < 0)
        return ret;

    src += 256;

    init_get_bits(&gb, src, (src_end - src) * 8);

    while (dst != dst_end)
    {
        pix = get_vlc2(&gb, vlc.table, VLC_BITS, 3);
        if (pix < 0)
        {
            fprintf(stderr, "Decoding error\n");
            goto fail;
        }
        *dst++ = pix;

        if (get_bits_left(&gb) < 0)
        {
            fprintf(stderr, "Slice decoding ran out of bits at %d\n", (int)(dst - dst_begin));
            goto fail;
        }
    }
    if (get_bits_left(&gb) > 32)
    {
        fprintf(stderr, "%d bits left after decoding slice\n", get_bits_left(&gb));
        goto fail;
    }

    ff_free_vlc(&vlc);
    return dst_size;

fail:
    ff_free_vlc(&vlc);
    return -1;
}
