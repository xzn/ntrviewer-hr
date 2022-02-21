#include <stdint.h>
#include <stdio.h>
#include <memory.h>
#include <limits.h>
#include "commondef.h"

typedef struct VLC
{
    int bits;
    int16_t (*table)[2];
    int table_size, table_allocated;
} VLC;

typedef struct VLCcode
{
    uint8_t bits;
    int16_t symbol;
    /** codeword, with the first bit-to-be-read in the msb
     * (even if intended for a little-endian bitstream reader) */
    uint32_t code;
} VLCcode;

#define LOCALBUF_ELEMS 1500

static inline int alloc_table(VLC *vlc, int size)
{
    int index = vlc->table_size;

    vlc->table_size += size;
    if (vlc->table_size > vlc->table_allocated)
    {
        vlc->table_allocated += (1 << vlc->bits);
        vlc->table = HR_REALLOC(vlc->table, vlc->table_allocated * sizeof(int16_t) * 2);
        if (!vlc->table)
        {
            vlc->table_allocated = 0;
            vlc->table_size = 0;
            return -1;
        }
        memset(vlc->table + vlc->table_allocated - (1 << vlc->bits), 0, sizeof(int16_t) * 2 << vlc->bits);
    }
    return index;
}

static inline int build_table(VLC *vlc, int table_nb_bits, int nb_codes, VLCcode *codes)
{
    int table_size, table_index, index, code_prefix, symbol, subtable_bits;
    int i, j, k, n, nb, inc;
    int16_t(*table)[2];
    uint32_t code;

    if (table_nb_bits > 30)
        return -1;
    table_size = 1 << table_nb_bits;
    table_index = alloc_table(vlc, table_size);
    ff_dlog(NULL, "new table index=%d size=%d\n", table_index, table_size);
    if (table_index < 0)
        return table_index;
    table = &vlc->table[table_index];

    /* first pass: map codes and compute auxiliary table sizes */
    for (i = 0; i < nb_codes; i++)
    {
        n = codes[i].bits;
        code = codes[i].code;
        symbol = codes[i].symbol;
        ff_dlog(NULL, "i=%d n=%d code=0x%" PRIx32 "\n", i, n, code);
        if (n <= table_nb_bits)
        {
            /* no need to add another table */
            j = code >> (32 - table_nb_bits);
            nb = 1 << (table_nb_bits - n);
            inc = 1;
            for (k = 0; k < nb; k++)
            {
                int bits = table[j][1];
                int oldsym = table[j][0];
                ff_dlog(NULL, "%4x: code=%d n=%d\n", j, i, n);
                if ((bits || oldsym) && (bits != n || oldsym != symbol))
                {
                    fprintf(stderr, "incorrect codes\n");
                    return -1;
                }
                table[j][1] = n; // bits
                table[j][0] = symbol;
                j += inc;
            }
        }
        else
        {
            /* fill auxiliary table recursively */
            n -= table_nb_bits;
            code_prefix = code >> (32 - table_nb_bits);
            subtable_bits = n;
            codes[i].bits = n;
            codes[i].code = code << table_nb_bits;
            for (k = i + 1; k < nb_codes; k++)
            {
                n = codes[k].bits - table_nb_bits;
                if (n <= 0)
                    break;
                code = codes[k].code;
                if (code >> (32 - table_nb_bits) != code_prefix)
                    break;
                codes[k].bits = n;
                codes[k].code = code << table_nb_bits;
                subtable_bits = FFMAX(subtable_bits, n);
            }
            subtable_bits = FFMIN(subtable_bits, table_nb_bits);
            j = code_prefix;
            table[j][1] = -subtable_bits;
            ff_dlog(NULL, "%4x: n=%d (subtable)\n",
                    j, codes[i].bits + table_nb_bits);
            index = build_table(vlc, subtable_bits, k - i, codes + i);
            if (index < 0)
                return index;
            /* note: realloc has been done, so reload tables */
            table = &vlc->table[table_index];
            table[j][0] = index; // code
            if (table[j][0] != index)
            {
                return -2;
            }
            i = k - 1;
        }
    }

    for (i = 0; i < table_size; i++)
    {
        if (table[i][1] == 0) // bits
            table[i][0] = -1; // codes
    }

    return table_index;
}

static inline int vlc_common_init(VLC *vlc, int nb_bits, int nb_codes, VLCcode **buf)
{
    vlc->bits = nb_bits;
    vlc->table_size = 0;
    vlc->table = NULL;
    vlc->table_allocated = 0;
    if (nb_codes > LOCALBUF_ELEMS)
    {
        *buf = HR_MALLOC(nb_codes * sizeof(VLCcode));
        if (!*buf)
            return -1;
    }

    return 0;
}

static inline int vlc_common_end(VLC *vlc, int nb_bits, int nb_codes, VLCcode *codes, VLCcode localbuf[LOCALBUF_ELEMS])
{
    int ret = build_table(vlc, nb_bits, nb_codes, codes);

    if (codes != localbuf)
        HR_FREE(codes);
    if (ret < 0)
    {
        HR_FREE(vlc->table);
        return ret;
    }
    return 0;
}

#define GET_DATA(v, table, i, wrap, size)                       \
    {                                                           \
        const uint8_t *ptr = (const uint8_t *)table + i * wrap; \
        switch (size)                                           \
        {                                                       \
        case 1:                                                 \
            v = *(const uint8_t *)ptr;                          \
            break;                                              \
        case 2:                                                 \
            v = *(const uint16_t *)ptr;                         \
            break;                                              \
        case 4:                                                 \
        default:                                                \
            av_assert1(size == 4);                              \
            v = *(const uint32_t *)ptr;                         \
            break;                                              \
        }                                                       \
    }

static inline int ff_init_vlc_from_lengths(VLC *vlc, int nb_bits, int nb_codes,
                                           const int8_t *lens, int lens_wrap,
                                           const void *symbols, int symbols_wrap, int symbols_size)
{
    VLCcode *localbuf = HR_MALLOC(sizeof(VLCcode) * LOCALBUF_ELEMS), *buf = localbuf;
    uint64_t code;
    int ret, j, len_max = FFMIN(32, 3 * nb_bits);

    ret = vlc_common_init(vlc, nb_bits, nb_codes, &buf);
    if (ret < 0)
    {
        goto fail;
    }

    j = code = 0;
    for (int i = 0; i < nb_codes; i++, lens += lens_wrap)
    {
        int len = *lens;
        if (len > 0)
        {
            unsigned sym;

            buf[j].bits = len;
            if (symbols)
                GET_DATA(sym, symbols, i, symbols_wrap, symbols_size)
            else
                sym = i;
            buf[j].symbol = sym;
            buf[j++].code = code;
        }
        else if (len < 0)
        {
            len = -len;
        }
        else
            continue;
        if (len > len_max || code & ((1U << (32 - len)) - 1))
        {
            fprintf(stderr, "Invalid VLC (length %u)\n", len);
            ret = -1;
            goto fail;
        }
        code += 1U << (32 - len);
        if (code > UINT32_MAX + 1ULL)
        {
            fprintf(stderr, "Overdetermined VLC tree\n");
            ret = -1;
            goto fail;
        }
    }
    ret = vlc_common_end(vlc, nb_bits, j, buf, localbuf);

fail:
    if (buf != localbuf)
        HR_FREE(buf);
    HR_FREE(localbuf);
    return ret;
}

static inline void ff_free_vlc(VLC *vlc)
{
    HR_FREE(vlc->table);
}

typedef struct GetBitContext
{
    const uint8_t *buffer, *buffer_end;
    int index;
    int size_in_bits;
    int size_in_bits_plus8;
} GetBitContext;

#define OPEN_READER_NOSIZE(name, gb)         \
    unsigned int name##_index = (gb)->index; \
    unsigned int name##_cache

#define OPEN_READER(name, gb)     \
    OPEN_READER_NOSIZE(name, gb); \
    unsigned int name##_size_plus8 = (gb)->size_in_bits_plus8

#define AV_RB32(x)                                 \
    (((uint32_t)((const uint8_t *)(x))[0] << 24) | \
     (((const uint8_t *)(x))[1] << 16) |           \
     (((const uint8_t *)(x))[2] << 8) |            \
     ((const uint8_t *)(x))[3])

#define UPDATE_CACHE_BE(name, gb) name##_cache = \
                                      AV_RB32((gb)->buffer + (name##_index >> 3)) << (name##_index & 7)

#define UPDATE_CACHE(name, gb) UPDATE_CACHE_BE(name, gb)

#define SKIP_COUNTER(name, gb, num) \
    name##_index = FFMIN(name##_size_plus8, name##_index + (num))
#define LAST_SKIP_BITS(name, gb, num) SKIP_COUNTER(name, gb, num)
#define NEG_USR32(a, s) (((uint32_t)(a)) >> (32 - (s)))
#define SHOW_UBITS_BE(name, gb, num) NEG_USR32(name##_cache, num)
#define SHOW_UBITS(name, gb, num) SHOW_UBITS_BE(name, gb, num)

#define SKIP_CACHE(name, gb, num) name##_cache <<= (num)
#define SKIP_BITS(name, gb, num)     \
    do                               \
    {                                \
        SKIP_CACHE(name, gb, num);   \
        SKIP_COUNTER(name, gb, num); \
    } while (0)

#define GET_VLC(code, name, gb, table, bits, max_depth)       \
    do                                                        \
    {                                                         \
        int n, nb_bits;                                       \
        unsigned int index;                                   \
                                                              \
        index = SHOW_UBITS(name, gb, bits);                   \
        code = table[index][0];                               \
        n = table[index][1];                                  \
                                                              \
        if (max_depth > 1 && n < 0)                           \
        {                                                     \
            LAST_SKIP_BITS(name, gb, bits);                   \
            UPDATE_CACHE(name, gb);                           \
                                                              \
            nb_bits = -n;                                     \
                                                              \
            index = SHOW_UBITS(name, gb, nb_bits) + code;     \
            code = table[index][0];                           \
            n = table[index][1];                              \
            if (max_depth > 2 && n < 0)                       \
            {                                                 \
                LAST_SKIP_BITS(name, gb, nb_bits);            \
                UPDATE_CACHE(name, gb);                       \
                                                              \
                nb_bits = -n;                                 \
                                                              \
                index = SHOW_UBITS(name, gb, nb_bits) + code; \
                code = table[index][0];                       \
                n = table[index][1];                          \
            }                                                 \
        }                                                     \
        SKIP_BITS(name, gb, n);                               \
    } while (0)

#define CLOSE_READER(name, gb) (gb)->index = name##_index

static inline int get_vlc2(GetBitContext *s, int16_t (*table)[2],
                           int bits, int max_depth)
{
    int code;

    OPEN_READER(re, s);
    UPDATE_CACHE(re, s);

    GET_VLC(code, re, s, table, bits, max_depth);

    CLOSE_READER(re, s);

    return code;
}

static inline int get_bits_count(const GetBitContext *s)
{
    return s->index;
}

static inline int get_bits_left(GetBitContext *gb)
{
    return gb->size_in_bits - get_bits_count(gb);
}

#define AV_INPUT_BUFFER_PADDING_SIZE 64

static inline int init_get_bits(GetBitContext *s, const uint8_t *buffer, int bit_size)
{
    int buffer_size;
    int ret = 0;

    if (bit_size >= INT_MAX - FFMAX(7, AV_INPUT_BUFFER_PADDING_SIZE * 8) || bit_size < 0 || !buffer)
    {
        bit_size = 0;
        buffer = NULL;
        ret = -1;
    }

    buffer_size = (bit_size + 7) >> 3;

    s->buffer = buffer;
    s->size_in_bits = bit_size;
    s->size_in_bits_plus8 = bit_size + 8;
    s->buffer_end = buffer + buffer_size;
    s->index = 0;

    return ret;
}
