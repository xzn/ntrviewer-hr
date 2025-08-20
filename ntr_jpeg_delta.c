// Modified from libjpeg-turbo
// See jpeg_turbo/LICENSE.md for license

#include "ntr_jpeg_delta.h"

#include <stdlib.h>
#include <math.h>

#define DELTA_Q_COUNT 32
#define DELTA_Q_MAX 7.0f

#define DCTSIZE JPEG_DCTSIZE
#define DCTSIZE2 (DCTSIZE * DCTSIZE)

struct jhuff_tbl_t {
    uint8_t bits[17];
    uint8_t huffval[256];
};

#define SIZEOF_SIZE_T 8

#if SIZEOF_SIZE_T == 8 || defined(_WIN64)

typedef size_t bit_buf_type; /* type of bit-extraction buffer */
#define BIT_BUF_SIZE 64      /* size of buffer in bits */

#elif defined(__x86_64__) && defined(__ILP32__)

typedef unsigned long long bit_buf_type; /* type of bit-extraction buffer */
#define BIT_BUF_SIZE 64 /* size of buffer in bits */

#else

typedef unsigned long bit_buf_type; /* type of bit-extraction buffer */
#define BIT_BUF_SIZE 32 /* size of buffer in bits */

#endif

struct bitread_perm_state_t {
    bit_buf_type get_buffer;
    int bits_left;
};

#define FAST_FLOAT float
#define FLOAT_MULT_TYPE FAST_FLOAT

#define RP_NUM_QUANT_TBLS 2
#define RP_NUM_HUFF_TBLS 2
#define RP_NUM_JPEG_COMP 3
#define RP_MAX_SAMP_FACTOR 2

struct jpeg_comp_info_t {
    int dc_tbl_no;
    int ac_tbl_no;
    int quant_tbl_no;
    FLOAT_MULT_TYPE *dct_table;
    uint8_t *dct_log2_tbl;
};

#define HUFF_LOOKAHEAD 8
struct d_derived_tbl_t {
    int32_t maxcode[18];
    int32_t valoffset[18];
    struct jhuff_tbl_t *tbl;
    int lookup[1 << HUFF_LOOKAHEAD];
};

typedef short JCOEF;
typedef JCOEF JBLOCK[DCTSIZE2];
typedef JBLOCK *JBLOCKROW;
typedef JBLOCKROW *JBLOCKARRAY;
typedef JBLOCKARRAY *JBLOCKIMAGE;

// typedef unsigned char JSAMPLE;
typedef float JSAMPLE;
typedef JSAMPLE *JSAMPROW;
typedef JSAMPROW *JSAMPARRAY;
typedef JSAMPARRAY *JSAMPIMAGE;

typedef JCOEF *JCOEFPTR;

#define D_MAX_BLOCKS_IN_MCU (6)
struct jpeg_shared_t {
    int width, height;
    int h_samp_factor;
    int v_samp_factor;
    int rows_in_mcus;
    boolean is_top;
    int mcu_row;
    int quality[SCREEN_COUNT];
    int prev_quality[SCREEN_COUNT];

    struct jhuff_tbl_t dc_huff_tbl_ptrs[RP_NUM_HUFF_TBLS];
    struct jhuff_tbl_t ac_huff_tbl_ptrs[RP_NUM_HUFF_TBLS];
    struct d_derived_tbl_t dc_derived_tbls[RP_NUM_HUFF_TBLS];
    struct d_derived_tbl_t ac_derived_tbls[RP_NUM_HUFF_TBLS];
    struct d_derived_tbl_t *dc_cur_tbls[D_MAX_BLOCKS_IN_MCU];
    struct d_derived_tbl_t *ac_cur_tbls[D_MAX_BLOCKS_IN_MCU];
    struct jpeg_comp_info_t comp_infos[RP_NUM_JPEG_COMP];

    struct bitread_perm_state_t bitstate;
    int last_dc_val[RP_NUM_JPEG_COMP];
    int blocks_in_MCU;
    int MCU_membership[D_MAX_BLOCKS_IN_MCU];

    const uint8_t *next_input_byte;
    size_t bytes_in_buffer;
    uint8_t *out;
    int unread_marker;

    JBLOCK MCU_buffer_base[D_MAX_BLOCKS_IN_MCU];
    JBLOCKROW MCU_buffer[D_MAX_BLOCKS_IN_MCU];
    FLOAT_MULT_TYPE dct_table[RP_NUM_QUANT_TBLS][DCTSIZE2];
    uint8_t dct_log2_tbl[SCREEN_COUNT][RP_NUM_QUANT_TBLS][DCTSIZE2];

    uint8_t prev_dct_log2_tbl[SCREEN_COUNT][RP_NUM_QUANT_TBLS][DCTSIZE2];
    uint8_t prev_shifts[SCREEN_COUNT][RP_NUM_QUANT_TBLS][DCTSIZE2];

    int16_t prev[SCREEN_COUNT][SCREEN_WIDTH * SCREEN_HEIGHT0 * RP_NUM_JPEG_COMP];
};

static const float aanscalefactor[DCTSIZE] = {
    1.0,
    1.387039845,
    1.306562965,
    1.175875602,
    1.0,
    0.785694958,
    0.541196100,
    0.275899379,
};

static const uint8_t std_luminance_quant_tbl[DCTSIZE2] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113,
    92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
};

static const uint8_t std_chrominance_quant_tbl[DCTSIZE2] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
};

UNUSED
static void add_huff_table(struct jhuff_tbl_t *htblptr, const uint8_t *bits, const uint8_t *val)
{
    int nsymbols, len;

    /* Copy the number-of-symbols-of-each-code-length counts */
    memcpy(htblptr->bits, bits, sizeof(htblptr->bits));

    /* Validate the counts.  We do this here mainly so we can copy the right
     * number of symbols from the val[] array, without risking marching off
     * the end of memory.  jchuff.c will do a more thorough test later.
     */
    nsymbols = 0;
    for (len = 1; len <= 16; len++)
        nsymbols += bits[len];
    if (nsymbols < 1 || nsymbols > 256) {
        err_log("nsymbols out or range %d\n", nsymbols);
        exit(1);
    }

    memcpy(htblptr->huffval, val, nsymbols * sizeof(uint8_t));
    memset(&htblptr->huffval[nsymbols], 0, (256 - nsymbols) * sizeof(uint8_t));
}

static void jpeg_gen_optimal_table(struct jhuff_tbl_t *htbl, long freq[]);
static void fill_ac_freq(long freq[257], const uint8_t base[], int base_len)
{
    int dq_len = 0x10;
    int count = base_len + dq_len;
    for (int i = 0; i < base_len; ++i) {
        freq[base[i]] = count - i;
    }
    count -= base_len;
    for (int i = 0; i < dq_len; ++i) {
        freq[0x0b | (i << 4)] = count - i;
    }
};

static void std_huff_tables (struct jpeg_shared_t *shared)
/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */
{
    // static const uint8_t bits_dc_luminance[17] = {
    //     /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
    // static const uint8_t val_dc_luminance[] = {
    //     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

    // static const uint8_t bits_dc_chrominance[17] = {
    //     /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
    // static const uint8_t val_dc_chrominance[] = {
    //     0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

    // static const uint8_t bits_ac_luminance[17] = {
    //     /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d};
    static const uint8_t val_ac_luminance[] = {
        0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
        0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
        0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
        0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
        0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
        0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
        0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
        0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
        0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
        0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
        0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
        0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
        0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
        0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
        0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
        0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
        0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
        0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
        0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
        0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
        0xf9, 0xfa};

    // static const uint8_t bits_ac_chrominance[17] = {
    //     /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77};
    static const uint8_t val_ac_chrominance[] = {
        0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
        0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
        0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
        0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
        0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
        0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
        0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
        0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
        0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
        0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
        0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
        0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
        0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
        0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
        0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
        0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
        0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
        0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
        0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
        0xf9, 0xfa};

    // add_huff_table(&shared->dc_huff_tbl_ptrs[0], bits_dc_luminance,
    //                val_dc_luminance);
    // add_huff_table(&shared->ac_huff_tbl_ptrs[0], bits_ac_luminance,
    //                val_ac_luminance);
    // add_huff_table(&shared->dc_huff_tbl_ptrs[1], bits_dc_chrominance,
    //                val_dc_chrominance);
    // add_huff_table(&shared->ac_huff_tbl_ptrs[1], bits_ac_chrominance,
    //                val_ac_chrominance);

    long freq[257];

    {
        memset(freq, 0, sizeof(freq));
        long *dc_lum_freq = freq;
        for (int i = 0; i <= 12; ++i) {
            dc_lum_freq[i] = 12 + 1 - i;
        }
        jpeg_gen_optimal_table(&shared->dc_huff_tbl_ptrs[0], dc_lum_freq);
    }
    {
        memset(freq, 0, sizeof(freq));
        long *dc_chrom_freq = freq;
        for (int i = 0; i <= 12; ++i) {
            dc_chrom_freq[i] = 12 + 1 - i;
        }
        jpeg_gen_optimal_table(&shared->dc_huff_tbl_ptrs[1], dc_chrom_freq);
    }
    {
        memset(freq, 0, sizeof(freq));
        long *ac_lum_freq = freq;
        fill_ac_freq(ac_lum_freq, val_ac_luminance, sizeof(val_ac_luminance) / sizeof(*val_ac_luminance));
        jpeg_gen_optimal_table(&shared->ac_huff_tbl_ptrs[0], ac_lum_freq);
    }

    {
        memset(freq, 0, sizeof(freq));
        long *ac_chrom_freq = freq;
        fill_ac_freq(ac_chrom_freq, val_ac_chrominance, sizeof(val_ac_chrominance) / sizeof(*val_ac_chrominance));

        jpeg_gen_optimal_table(&shared->ac_huff_tbl_ptrs[1], ac_chrom_freq);
    }
}

static void jpeg_make_d_derived_tbl(boolean isDC, struct jhuff_tbl_t *htbl, struct d_derived_tbl_t *dtbl)
{
    int p, i, l, si, numsymbols;
    int lookbits, ctr;
    char huffsize[257];
    unsigned int huffcode[257];
    unsigned int code;

    /* Note that huffsize[] and huffcode[] are filled in code-length order,
     * paralleling the order of the symbols themselves in htbl->huffval[].
     */

    /* Find the input Huffman table */

    /* Allocate a workspace if we haven't already done so. */
    dtbl->tbl = htbl; /* fill in back link */

    /* Figure C.1: make table of Huffman code length for each symbol */

    p = 0;
    for (l = 1; l <= 16; l++) {
        i = (int)htbl->bits[l];
        if (i < 0 || p + i > 256) /* protect against table overrun */
            exit(2);
        while (i--)
            huffsize[p++] = (char)l;
    }
    huffsize[p] = 0;
    numsymbols = p;

    /* Figure C.2: generate the codes themselves */
    /* We also validate that the counts represent a legal Huffman code tree. */

    code = 0;
    si = huffsize[0];
    p = 0;
    while (huffsize[p]) {
        while (((int)huffsize[p]) == si) {
            huffcode[p++] = code;
            code++;
        }
        /* code is now 1 more than the last code used for codelength si; but
         * it must still fit in si bits, since no code is allowed to be all ones.
         */
        if (((int32_t)code) >= (((int32_t)1) << si))
            exit(2);
        code <<= 1;
        si++;
    }

    /* Figure F.15: generate decoding tables for bit-sequential decoding */

    p = 0;
    for (l = 1; l <= 16; l++) {
        if (htbl->bits[l]) {
            /* valoffset[l] = huffval[] index of 1st symbol of code length l,
             * minus the minimum code of length l
             */
            dtbl->valoffset[l] = (int32_t)p - (int32_t)huffcode[p];
            p += htbl->bits[l];
            dtbl->maxcode[l] = huffcode[p - 1]; /* maximum code of length l */
        } else {
            dtbl->maxcode[l] = -1; /* -1 if no codes of this length */
        }
    }
    dtbl->valoffset[17] = 0;
    dtbl->maxcode[17] = 0xFFFFFL; /* ensures jpeg_huff_decode terminates */

    /* Compute lookahead tables to speed up decoding.
     * First we set all the table entries to 0, indicating "too long";
     * then we iterate through the Huffman codes that are short enough and
     * fill in all the entries that correspond to bit sequences starting
     * with that code.
     */

    for (i = 0; i < (1 << HUFF_LOOKAHEAD); i++)
        dtbl->lookup[i] = (HUFF_LOOKAHEAD + 1) << HUFF_LOOKAHEAD;

    p = 0;
    for (l = 1; l <= HUFF_LOOKAHEAD; l++) {
        for (i = 1; i <= (int)htbl->bits[l]; i++, p++) {
            /* l = current code's length, p = its index in huffcode[] & huffval[]. */
            /* Generate left-justified code followed by all possible bit sequences */
            lookbits = huffcode[p] << (HUFF_LOOKAHEAD - l);
            for (ctr = 1 << (HUFF_LOOKAHEAD - l); ctr > 0; ctr--) {
                dtbl->lookup[lookbits] = (l << HUFF_LOOKAHEAD) | htbl->huffval[p];
                lookbits++;
            }
        }
    }

    /* Validate symbols as being reasonable.
     * For AC tables, we make no check, but accept all byte values 0..255.
     * For DC tables, we require the symbols to be in range 0..15 in lossy mode
     * and 0..16 in lossless mode.  (Tighter bounds could be applied depending on
     * the data depth and mode, but this is sufficient to ensure safe decoding.)
     */
    if (isDC) {
        for (i = 0; i < numsymbols; i++) {
            int sym = htbl->huffval[i];
            if (sym < 0 || sym > (1 ? 16 : 15))
                exit(2);
        }
    }
}

typedef struct { /* Bitreading working state within an MCU */
    /* Current data source location */
    /* We need a copy, rather than munging the original, in case of suspension */
    const uint8_t *next_input_byte; /* => next byte to read from source */
    size_t bytes_in_buffer;        /* # of bytes remaining in source buffer */
    /* Bit input buffer --- note these values are kept in register variables,
     * not in this struct, inside the inner loops.
     */
    bit_buf_type get_buffer; /* current bit-extraction buffer */
    int bits_left;           /* # of unused bits in it */
    /* Pointer needed by jpeg_fill_bit_buffer. */
    struct jpeg_shared_t *shared; /* back link to decompress master record */
} bitread_working_state;

#define BITREAD_STATE_VARS            \
    register bit_buf_type get_buffer; \
    register int bits_left;           \
    bitread_working_state br_state

#define BITREAD_LOAD_STATE(cinfop, permstate)           \
    br_state.shared = cinfop;                           \
    br_state.next_input_byte = cinfop->next_input_byte; \
    br_state.bytes_in_buffer = cinfop->bytes_in_buffer; \
    get_buffer = permstate.get_buffer;                  \
    bits_left = permstate.bits_left;

#define BITREAD_SAVE_STATE(cinfop, permstate)           \
    cinfop->next_input_byte = br_state.next_input_byte; \
    cinfop->bytes_in_buffer = br_state.bytes_in_buffer; \
    permstate.get_buffer = get_buffer;                  \
    permstate.bits_left = bits_left

#define CHECK_BIT_BUFFER(state, nbits, action)                                   \
    {                                                                            \
        if (bits_left < (nbits)) {                                               \
            if (!jpeg_fill_bit_buffer(&(state), get_buffer, bits_left, nbits)) { \
                action;                                                          \
            }                                                                    \
            get_buffer = (state).get_buffer;                                     \
            bits_left = (state).bits_left;                                       \
        }                                                                        \
    }

#define GET_BITS(nbits) \
    (((int)(get_buffer >> (bits_left -= (nbits)))) & ((1 << (nbits)) - 1))

#define PEEK_BITS(nbits) \
    (((int)(get_buffer >> (bits_left - (nbits)))) & ((1 << (nbits)) - 1))

#define DROP_BITS(nbits) \
    (bits_left -= (nbits))

#define HUFF_DECODE(result, state, htbl, failaction, slowlabel)                        \
    {                                                                                  \
        register int nb, look;                                                         \
        if (bits_left < HUFF_LOOKAHEAD) {                                              \
            if (!jpeg_fill_bit_buffer(&state, get_buffer, bits_left, 0)) {             \
                failaction;                                                            \
            }                                                                          \
            get_buffer = state.get_buffer;                                             \
            bits_left = state.bits_left;                                               \
            if (bits_left < HUFF_LOOKAHEAD) {                                          \
                nb = 1;                                                                \
                goto slowlabel;                                                        \
            }                                                                          \
        }                                                                              \
        look = PEEK_BITS(HUFF_LOOKAHEAD);                                              \
        if ((nb = (htbl->lookup[look] >> HUFF_LOOKAHEAD)) <= HUFF_LOOKAHEAD) {         \
            DROP_BITS(nb);                                                             \
            result = htbl->lookup[look] & ((1 << HUFF_LOOKAHEAD) - 1);                 \
        } else {                                                                       \
        slowlabel:                                                                     \
            if ((result =                                                              \
                     jpeg_huff_decode(&state, get_buffer, bits_left, htbl, nb)) < 0) { \
                failaction;                                                            \
            }                                                                          \
            get_buffer = state.get_buffer;                                             \
            bits_left = state.bits_left;                                               \
        }                                                                              \
    }

#define NEG_1 ((unsigned int)-1)
#define HUFF_EXTEND(x, s) \
    ((x) + ((((x) - (1 << ((s) - 1))) >> 31) & (((NEG_1) << (s)) + 1)))

#define MIN_GET_BITS (BIT_BUF_SIZE - 7)

static boolean jpeg_fill_bit_buffer(bitread_working_state *state,
                     register bit_buf_type get_buffer, register int bits_left,
                     int nbits)
/* Load up the bit buffer to a depth of at least nbits */
{
    /* Copy heavily used state fields into locals (hopefully registers) */
    register const uint8_t *next_input_byte = state->next_input_byte;
    register size_t bytes_in_buffer = state->bytes_in_buffer;
    struct jpeg_shared_t *shared = state->shared;

    /* Attempt to load at least MIN_GET_BITS bits into get_buffer. */
    /* (It is assumed that no request will be for more than that many bits.) */
    /* We fail to do so only if we hit a marker or are forced to suspend. */

    if (shared->unread_marker == 0) { /* cannot advance past a marker */
        while (bits_left < MIN_GET_BITS) {
            register int c;

            /* Attempt to read a byte */
            if (bytes_in_buffer == 0) {
                break;
                // err_log("input exhausted\n");
                // return FALSE;
            }
            bytes_in_buffer--;
            c = *next_input_byte++;

            /* If it's 0xFF, check and discard stuffed zero byte */
            if (FALSE && c == 0xFF) {
                // err_log("escape\n");
                /* Loop here to discard any padding FF's on terminating marker,
                 * so that we can save a valid unread_marker value.  NOTE: we will
                 * accept multiple FF's followed by a 0 as meaning a single FF data
                 * byte.  This data pattern is not valid according to the standard.
                 */
                do {
                    if (bytes_in_buffer == 0) {
                        err_log("input exhausted\n");
                        return FALSE;
                    }
                    bytes_in_buffer--;
                    c = *next_input_byte++;
                } while (c == 0xFF);

                if (c == 0) {
                    /* Found FF/00, which represents an FF data byte */
                    c = 0xFF;
                } else {
                    /* Oops, it's actually a marker indicating end of compressed data.
                     * Save the marker code for later use.
                     * Fine point: it might appear that we should save the marker into
                     * bitread working state, not straight into permanent state.  But
                     * once we have hit a marker, we cannot need to suspend within the
                     * current MCU, because we will read no more bytes from the data
                     * source.  So it is OK to update permanent state right away.
                     */
                    shared->unread_marker = c;
                    /* See if we need to insert some fake zero bits. */
                    goto no_more_bytes;
                }
            }

            /* OK, load c into get_buffer */
            get_buffer = (get_buffer << 8) | c;
            bits_left += 8;
        } /* end while */
    } else {
    no_more_bytes:
        /* We get here if we've read the marker that terminates the compressed
         * data segment.  There should be enough bits in the buffer register
         * to satisfy the request; if so, no problem.
         */
        if (nbits > bits_left) {
            /* Uh-oh.  Report corrupted data to user and stuff zeroes into
             * the data stream, so that we can produce some kind of image.
             * We use a nonvolatile flag to ensure that only one warning message
             * appears per data segment.
             */
            err_log("not enough data\n");
            /* Fill the buffer with zero bits */
            get_buffer <<= MIN_GET_BITS - bits_left;
            bits_left = MIN_GET_BITS;
        }
    }

    /* Unload the local registers */
    state->next_input_byte = next_input_byte;
    state->bytes_in_buffer = bytes_in_buffer;
    state->get_buffer = get_buffer;
    state->bits_left = bits_left;

    return TRUE;
}

static int jpeg_huff_decode(bitread_working_state *state,
                 register bit_buf_type get_buffer, register int bits_left,
                 struct d_derived_tbl_t *htbl, int min_bits)
{
    register int l = min_bits;
    register int32_t code;

    /* HUFF_DECODE has determined that the code is at least min_bits */
    /* bits long, so fetch that many bits in one swoop. */

    CHECK_BIT_BUFFER(*state, l, return -1);
    code = GET_BITS(l);

    /* Collect the rest of the Huffman code one bit at a time. */
    /* This is per Figure F.16. */

    while (code > htbl->maxcode[l]) {
        code <<= 1;
        CHECK_BIT_BUFFER(*state, 1, return -1);
        code |= GET_BITS(1);
        l++;
    }

    /* Unload the local registers */
    state->get_buffer = get_buffer;
    state->bits_left = bits_left;

    /* With garbage input we may reach the sentinel value l = 17. */

    if (l > 16) {
        err_log("huff decode error\n");
        return 0; /* fake a zero as the safest result */
    }

    return htbl->tbl->huffval[(int)(code + htbl->valoffset[l])];
}

static const int jpeg_natural_order[DCTSIZE2 + 16] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
    63, 63, 63, 63, 63, 63, 63, 63, /* extra entries for safety in decoder */
    63, 63, 63, 63, 63, 63, 63, 63};

// static int coef_fix(int s, int m) {
//     if (s >= (1 << m)) {
//         s -= (1 << (m + 1)) - 1;
//     } else if (s <= -(1 << m)) {
//         s += (1 << (m + 1)) - 1;
//     }
//     return s;
// }

UNUSED
static boolean coef_check(int s, int m) {
    if (s >= (1 << m) || s <= -(1 << m)) {
        err_log("s %d, m %d\n", s, m);
        return TRUE;
    }
    return FALSE;
}

static void prev_shift(int16_t *prev, uint8_t shift, int dir) {
    if (dir > 0) {
        *prev <<= shift;
    } else if (dir < 0) {
        if (*prev < 0) {
            *prev = -(-*prev >> shift);
        } else {
            *prev >>= shift;
        }
    }
}

static const uint8_t MAX_COEF_BITS = 8 + 2;
static boolean decode_mcu(struct jpeg_shared_t *shared, JBLOCKROW *MCU_data, int16_t *prev)
{
    int dir = shared->prev_quality[shared->is_top] >= 0 ? shared->quality[shared->is_top] - shared->prev_quality[shared->is_top] : 0;
    BITREAD_STATE_VARS;
    int blkn;
    int state[RP_NUM_JPEG_COMP];
    /* Outer loop handles each block in the MCU */

    /* Load up working state */
    BITREAD_LOAD_STATE(shared, shared->bitstate);
    memcpy(state, shared->last_dc_val, sizeof(state));

    for (blkn = 0; blkn < shared->blocks_in_MCU; blkn++) {
        struct jpeg_comp_info_t *info = &shared->comp_infos[shared->MCU_membership[blkn]];

        int16_t *prev_block = prev + blkn * DCTSIZE2;

        JBLOCKROW block = MCU_data ? MCU_data[blkn] : NULL;
        struct d_derived_tbl_t *dctbl = shared->dc_cur_tbls[blkn];
        struct d_derived_tbl_t *actbl = shared->ac_cur_tbls[blkn];
        register int s, k, r;

        /* Decode a single block's worth of coefficients */

        /* Section F.2.2.1: decode the DC coefficient difference */
        HUFF_DECODE(s, br_state, dctbl, return FALSE, label1);
        if (s) {
            CHECK_BIT_BUFFER(br_state, s, return FALSE);
            r = GET_BITS(s);
            s = HUFF_EXTEND(r, s);
        }

        /* Convert DC difference to actual value, update last_dc_val */
        int ci = shared->MCU_membership[blkn];
        /* Certain malformed JPEG images produce repeated DC coefficient
            * differences of 2047 or -2047, which causes state.last_dc_val[ci] to
            * grow until it overflows or underflows a 32-bit signed integer.  This
            * behavior is, to the best of our understanding, innocuous, and it is
            * unclear how to work around it without potentially affecting
            * performance.  Thus, we (hopefully temporarily) suppress UBSan integer
            * overflow errors for this function and decode_mcu_fast().
            */
        s += state[ci];
        state[ci] = s;
        if (block) {
            /* Output the DC coefficient (assumes jpeg_natural_order[0] = 0) */
            prev_shift(&prev_block[0], shared->prev_shifts[shared->is_top][info->quant_tbl_no][0], dir);
            s += prev_block[0];
            prev_block[0] = s;
            s <<= info->dct_log2_tbl[0];
            (*block)[0] = (JCOEF)s;
        }

        /* Section F.2.2.2: decode the AC coefficients */
        /* Since zeroes are skipped, output area must be cleared beforehand */
        for (k = 1; k < DCTSIZE2; k++) {
            HUFF_DECODE(s, br_state, actbl, return FALSE, label2);

            r = s >> 4;
            s &= 15;

            if (s) {
                for (int l = k; l < k + r; ++l) {
                    if (l >= DCTSIZE2) {
                        err_log("mcu err\n");
                        return FALSE;
                    }
                    prev_shift(&prev_block[l], shared->prev_shifts[shared->is_top][info->quant_tbl_no][jpeg_natural_order[l]], dir);
                    (*block)[jpeg_natural_order[l]] = prev_block[l] << info->dct_log2_tbl[jpeg_natural_order[l]];
                }

                k += r;
                CHECK_BIT_BUFFER(br_state, s, return FALSE);
                r = GET_BITS(s);
                s = HUFF_EXTEND(r, s);
                /* Output coefficient in natural (dezigzagged) order.
                    * Note: the extra entries in jpeg_natural_order[] will save us
                    * if k >= DCTSIZE2, which could happen if the data is corrupted.
                    */
                if (k >= DCTSIZE2) {
                    err_log("mcu err\n");
                    return FALSE;
                }
                prev_shift(&prev_block[k], shared->prev_shifts[shared->is_top][info->quant_tbl_no][jpeg_natural_order[k]], dir);
                s += prev_block[k];
                prev_block[k] = s;
                s <<= info->dct_log2_tbl[jpeg_natural_order[k]];
                (*block)[jpeg_natural_order[k]] = (JCOEF)s;
            } else {
                for (int l = k; l < (r == 15 ? k + 16 : DCTSIZE2); ++l) {
                    if (l >= DCTSIZE2) {
                        err_log("mcu err\n");
                        return FALSE;
                    }
                    prev_shift(&prev_block[l], shared->prev_shifts[shared->is_top][info->quant_tbl_no][jpeg_natural_order[l]], dir);
                    (*block)[jpeg_natural_order[l]] = prev_block[l] << info->dct_log2_tbl[jpeg_natural_order[l]];
                }

                if (r != 15) {
                    // err_log("r %d\n", r);
                    break;
                }
                k += 15;
                // err_log("many zeroes\n");
            }
        }

    }

    /* Completed MCU, so update state */
    BITREAD_SAVE_STATE(shared, shared->bitstate);
    memcpy(shared->last_dc_val, state, sizeof(state));
    return TRUE;
}

#define DEQUANTIZE(coef, quantval) (((FAST_FLOAT)(coef)) * (quantval))

static JSAMPLE range_limit(float in) {
    // int out = roundf(in);
    // return out > 255 ? 255 : out < 0 ? 0 : out;
    return in;
}

static void jpeg_idct_float(
    struct jpeg_comp_info_t *compptr,
    JCOEFPTR coef_block,
    JSAMPARRAY output_buf)
{
    FAST_FLOAT tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    FAST_FLOAT tmp10, tmp11, tmp12, tmp13;
    FAST_FLOAT z5, z10, z11, z12, z13;
    JCOEFPTR inptr;
    FLOAT_MULT_TYPE *quantptr;
    FAST_FLOAT *wsptr;
    JSAMPROW outptr;
    int ctr;
    FAST_FLOAT workspace[DCTSIZE2]; /* buffers data between passes */
#define _0_125 ((FLOAT_MULT_TYPE)0.125)

    /* Pass 1: process columns from input, store into work array. */

    inptr = coef_block;
    quantptr = (FLOAT_MULT_TYPE *)compptr->dct_table;
    wsptr = workspace;
    for (ctr = DCTSIZE; ctr > 0; ctr--) {
        /* Due to quantization, we will usually find that many of the input
         * coefficients are zero, especially the AC terms.  We can exploit this
         * by short-circuiting the IDCT calculation for any column in which all
         * the AC terms are zero.  In that case each output is equal to the
         * DC coefficient (with scale factor as needed).
         * With typical images and quantization tables, half or more of the
         * column DCT calculations can be simplified this way.
         */

        if (inptr[DCTSIZE * 1] == 0 && inptr[DCTSIZE * 2] == 0 &&
            inptr[DCTSIZE * 3] == 0 && inptr[DCTSIZE * 4] == 0 &&
            inptr[DCTSIZE * 5] == 0 && inptr[DCTSIZE * 6] == 0 &&
            inptr[DCTSIZE * 7] == 0) {
            /* AC terms all zero */
            FAST_FLOAT dcval = DEQUANTIZE(inptr[DCTSIZE * 0],
                                          quantptr[DCTSIZE * 0] * _0_125);

            wsptr[DCTSIZE * 0] = dcval;
            wsptr[DCTSIZE * 1] = dcval;
            wsptr[DCTSIZE * 2] = dcval;
            wsptr[DCTSIZE * 3] = dcval;
            wsptr[DCTSIZE * 4] = dcval;
            wsptr[DCTSIZE * 5] = dcval;
            wsptr[DCTSIZE * 6] = dcval;
            wsptr[DCTSIZE * 7] = dcval;

            inptr++; /* advance pointers to next column */
            quantptr++;
            wsptr++;
            continue;
        }

        /* Even part */

        tmp0 = DEQUANTIZE(inptr[DCTSIZE * 0], quantptr[DCTSIZE * 0] * _0_125);
        tmp1 = DEQUANTIZE(inptr[DCTSIZE * 2], quantptr[DCTSIZE * 2] * _0_125);
        tmp2 = DEQUANTIZE(inptr[DCTSIZE * 4], quantptr[DCTSIZE * 4] * _0_125);
        tmp3 = DEQUANTIZE(inptr[DCTSIZE * 6], quantptr[DCTSIZE * 6] * _0_125);

        tmp10 = tmp0 + tmp2; /* phase 3 */
        tmp11 = tmp0 - tmp2;

        tmp13 = tmp1 + tmp3;                                       /* phases 5-3 */
        tmp12 = (tmp1 - tmp3) * ((FAST_FLOAT)1.414213562) - tmp13; /* 2*c4 */

        tmp0 = tmp10 + tmp13; /* phase 2 */
        tmp3 = tmp10 - tmp13;
        tmp1 = tmp11 + tmp12;
        tmp2 = tmp11 - tmp12;

        /* Odd part */

        tmp4 = DEQUANTIZE(inptr[DCTSIZE * 1], quantptr[DCTSIZE * 1] * _0_125);
        tmp5 = DEQUANTIZE(inptr[DCTSIZE * 3], quantptr[DCTSIZE * 3] * _0_125);
        tmp6 = DEQUANTIZE(inptr[DCTSIZE * 5], quantptr[DCTSIZE * 5] * _0_125);
        tmp7 = DEQUANTIZE(inptr[DCTSIZE * 7], quantptr[DCTSIZE * 7] * _0_125);

        z13 = tmp6 + tmp5; /* phase 6 */
        z10 = tmp6 - tmp5;
        z11 = tmp4 + tmp7;
        z12 = tmp4 - tmp7;

        tmp7 = z11 + z13;                                /* phase 5 */
        tmp11 = (z11 - z13) * ((FAST_FLOAT)1.414213562); /* 2*c4 */

        z5 = (z10 + z12) * ((FAST_FLOAT)1.847759065); /* 2*c2 */
        tmp10 = z5 - z12 * ((FAST_FLOAT)1.082392200); /* 2*(c2-c6) */
        tmp12 = z5 - z10 * ((FAST_FLOAT)2.613125930); /* 2*(c2+c6) */

        tmp6 = tmp12 - tmp7; /* phase 2 */
        tmp5 = tmp11 - tmp6;
        tmp4 = tmp10 - tmp5;

        wsptr[DCTSIZE * 0] = tmp0 + tmp7;
        wsptr[DCTSIZE * 7] = tmp0 - tmp7;
        wsptr[DCTSIZE * 1] = tmp1 + tmp6;
        wsptr[DCTSIZE * 6] = tmp1 - tmp6;
        wsptr[DCTSIZE * 2] = tmp2 + tmp5;
        wsptr[DCTSIZE * 5] = tmp2 - tmp5;
        wsptr[DCTSIZE * 3] = tmp3 + tmp4;
        wsptr[DCTSIZE * 4] = tmp3 - tmp4;

        inptr++; /* advance pointers to next column */
        quantptr++;
        wsptr++;
    }

    /* Pass 2: process rows from work array, store into output array. */

    wsptr = workspace;
    for (ctr = 0; ctr < DCTSIZE; ctr++) {
        outptr = output_buf[ctr];
        /* Rows of zeroes can be exploited in the same way as we did with columns.
         * However, the column calculation has created many nonzero AC terms, so
         * the simplification applies less often (typically 5% to 10% of the time).
         * And testing floats for zero is relatively expensive, so we don't bother.
         */

        /* Even part */

        /* Apply signed->unsigned and prepare float->int conversion */
        z5 = wsptr[0] + ((FAST_FLOAT)127.5 + (FAST_FLOAT)0.5);
        tmp10 = z5 + wsptr[4];
        tmp11 = z5 - wsptr[4];

        tmp13 = wsptr[2] + wsptr[6];
        tmp12 = (wsptr[2] - wsptr[6]) * ((FAST_FLOAT)1.414213562) - tmp13;

        tmp0 = tmp10 + tmp13;
        tmp3 = tmp10 - tmp13;
        tmp1 = tmp11 + tmp12;
        tmp2 = tmp11 - tmp12;

        /* Odd part */

        z13 = wsptr[5] + wsptr[3];
        z10 = wsptr[5] - wsptr[3];
        z11 = wsptr[1] + wsptr[7];
        z12 = wsptr[1] - wsptr[7];

        tmp7 = z11 + z13;
        tmp11 = (z11 - z13) * ((FAST_FLOAT)1.414213562);

        z5 = (z10 + z12) * ((FAST_FLOAT)1.847759065); /* 2*c2 */
        tmp10 = z5 - z12 * ((FAST_FLOAT)1.082392200); /* 2*(c2-c6) */
        tmp12 = z5 - z10 * ((FAST_FLOAT)2.613125930); /* 2*(c2+c6) */

        tmp6 = tmp12 - tmp7;
        tmp5 = tmp11 - tmp6;
        tmp4 = tmp10 - tmp5;

        /* Final output stage: float->int conversion and range-limit */

        outptr[0] = range_limit(tmp0 + tmp7);
        outptr[7] = range_limit(tmp0 - tmp7);
        outptr[1] = range_limit(tmp1 + tmp6);
        outptr[6] = range_limit(tmp1 - tmp6);
        outptr[2] = range_limit(tmp2 + tmp5);
        outptr[5] = range_limit(tmp2 - tmp5);
        outptr[3] = range_limit(tmp3 + tmp4);
        outptr[4] = range_limit(tmp3 - tmp4);

        wsptr += DCTSIZE; /* advance pointer to next row */
    }
}

static void upsample(JSAMPLE (*out)[DCTSIZE * RP_MAX_SAMP_FACTOR][DCTSIZE * RP_MAX_SAMP_FACTOR][RP_NUM_JPEG_COMP], int c, int h_samp, int v_samp, const JSAMPROW *in) {
    if (h_samp == 1 && v_samp == 1) {
        for (int j = 0; j < DCTSIZE; ++j) {
            for (int i = 0; i < DCTSIZE; ++i) {
                (*out)[j][i][c] = in[j][i];
            }
        }
    } else if (h_samp == 2 && v_samp == 1) {
        for (int j = 0; j < DCTSIZE; ++j) {
            for (int i = 0; i < DCTSIZE * h_samp; ++i) {
                JSAMPLE a = in[j][MAX((i - 1) / h_samp, 0)];
                JSAMPLE b = in[j][MIN((i + 1) / h_samp, DCTSIZE - 1)];
                (*out)[j][i][c] = i % h_samp ? a * 0.75f + b * 0.25f : a * 0.25f + b * 0.75f;
            }
        }
    } else if (h_samp == 2 && v_samp == 2) {
        for (int j = 0; j < DCTSIZE * v_samp; ++j) {
            for (int i = 0; i < DCTSIZE * h_samp; ++i) {
                JSAMPLE aa = in[MAX((j - 1) / v_samp, 0)][MAX((i - 1) / h_samp, 0)];
                JSAMPLE ab = in[MAX((j - 1) / v_samp, 0)][MIN((i + 1) / h_samp, DCTSIZE - 1)];

                JSAMPLE ba = in[MIN((j + 1) / v_samp, DCTSIZE - 1)][MAX((i - 1) / h_samp, 0)];
                JSAMPLE bb = in[MIN((j + 1) / v_samp, DCTSIZE - 1)][MIN((i + 1) / h_samp, DCTSIZE - 1)];

                JSAMPLE a = i % h_samp ? aa * 0.75f + ab * 0.25f : aa * 0.25f + ab * 0.75f;
                JSAMPLE b = i % h_samp ? ba * 0.75f + bb * 0.25f : ba * 0.25f + bb * 0.75f;

                (*out)[j][i][c] = j % v_samp ? a * 0.75f + b * 0.25f : a * 0.25f + b * 0.75f;
            }
        }
    } else {
        err_log("upsample err samp factor\n");
    }
}

static uint8_t range_limit_i(JSAMPLE in) {
    int out = roundf(in);
    return out > 255 ? 255 : out < 0 ? 0 : out;
}

static void ycc_rgb_convert(
    uint8_t out[GL_CHANNELS_N],
    JSAMPLE in[RP_NUM_JPEG_COMP])
{
    JSAMPLE y = in[0];
    JSAMPLE cb = in[1];
    JSAMPLE cr = in[2];
#define R_I 0
#define G_I 1
#define B_I 2
#define A_I 3
    /* Range-limiting is essential due to noise introduced by DCT losses. */
    out[R_I] = range_limit_i(y + 1.40200f * (cr - 128.0f));
    out[G_I] = range_limit_i(y - 0.34414f * (cb - 128.0f) - 0.71414f * (cr - 128.0f));
    out[B_I] = range_limit_i(y + 1.77200f * (cb - 128.0f));
    /* Set unused byte to _MAXJSAMPLE so it can be interpreted as an */
    /* opaque alpha channel value */
    out[A_I] = 255;
}

static int consume_data(struct jpeg_shared_t *shared)
{
    uint32_t MCU_col_num; /* index of current MCU within row */
    int yoffset;

    JSAMPLE working[DCTSIZE * RP_MAX_SAMP_FACTOR][DCTSIZE * RP_MAX_SAMP_FACTOR][RP_NUM_JPEG_COMP];

    /* Loop to process one whole iMCU row */
    int mcu_cols = div_round_up(shared->width, shared->h_samp_factor * DCTSIZE);
    int16_t *prev = shared->prev[shared->is_top];
    for (yoffset = 0; yoffset < shared->rows_in_mcus;
         yoffset++) {
        for (MCU_col_num = 0; (int)MCU_col_num < mcu_cols;
             MCU_col_num++) {
            memset(shared->MCU_buffer_base, 0, sizeof(shared->MCU_buffer_base));

            int16_t *prev_mcu = prev + (((yoffset + shared->mcu_row) * mcu_cols + MCU_col_num) * shared->blocks_in_MCU) * DCTSIZE2;
            if (!decode_mcu(shared, shared->MCU_buffer, prev_mcu)) {
                err_log("mcu decode err at %d (%d) %d\n", (int)yoffset, (int)shared->rows_in_mcus, (int)MCU_col_num);
                return -1;
            } else {
                for (int i = 0; i < shared->blocks_in_MCU; ++i) {
                    JSAMPLE output_buf_base[DCTSIZE][DCTSIZE];
                    JSAMPROW output_buf[DCTSIZE];
                    for (int j = 0; j < DCTSIZE; ++j) {
                        output_buf[j] = output_buf_base[j];
                    }
                    jpeg_idct_float(&shared->comp_infos[shared->MCU_membership[i]], shared->MCU_buffer[i][0], output_buf);
                    int c = shared->MCU_membership[i];
                    if (c == 0) {
                        int w_b = i % shared->h_samp_factor;
                        int h_b = i / shared->h_samp_factor;

                        for (int j = 0; j < DCTSIZE; ++j) {
                            for (int i = 0; i < DCTSIZE; ++i) {
                                working[j + h_b * DCTSIZE][i + w_b * DCTSIZE][c] = output_buf[j][i];
                            }
                        }
                    } else {
                        upsample(&working, c, shared->h_samp_factor, shared->v_samp_factor, output_buf);
                    }
                }

                int b = yoffset * shared->width * shared->v_samp_factor * DCTSIZE * GL_CHANNELS_N + MCU_col_num * shared->h_samp_factor * DCTSIZE * GL_CHANNELS_N;
                for (int j = 0; j < DCTSIZE * shared->v_samp_factor; ++j) {
                    for (int i = 0; i < DCTSIZE * shared->h_samp_factor; ++i) {
                        int a = b + j * shared->width * GL_CHANNELS_N + i * GL_CHANNELS_N;

                        if ((int)MCU_col_num * shared->h_samp_factor * DCTSIZE + i >= shared->width) {
                            continue;
                        }
                        if ((int)yoffset * shared->v_samp_factor * DCTSIZE + j >= shared->height) {
                            continue;
                        }

                        ycc_rgb_convert(&shared->out[a], working[j][i]);
                    }
                }
            }
        }
    }
    return 0;
}

static void init_dct_table(const uint8_t in[DCTSIZE2], float out[DCTSIZE2], uint8_t log2_out[DCTSIZE2], int quality) {
    float q = DELTA_Q_MAX / (float)DELTA_Q_COUNT * (float)quality;
    for (int j = 0; j < DCTSIZE; ++j) {
        for (int i = 0; i < DCTSIZE; ++i) {
            int k = j * DCTSIZE + i;

            float l = log2f((float)in[k]);
            int v = (int)roundf(MAX(l - q, 0.0f));

            out[k] = aanscalefactor[i] * aanscalefactor[j];
            log2_out[k] = v;

            // err_log("%d v %d\n", k, v);
        }
    }
}

static void jpeg_gen_optimal_table(struct jhuff_tbl_t *htbl, long freq[])
{
#define MAX_CLEN 32            /* assumed maximum initial code length */
    uint8_t bits[MAX_CLEN + 1];  /* bits[k] = # of symbols with code length k */
    int bit_pos[MAX_CLEN + 1]; /* # of symbols with smaller code length */
    int codesize[257];         /* codesize[k] = code length of symbol k */
    int nz_index[257];         /* index of nonzero symbol in the original freq
                                  array */
    int others[257];           /* next symbol in current branch of tree */
    int c1, c2;
    int p, i, j;
    int num_nz_symbols;
    long v, v2;

    /* This algorithm is explained in section K.2 of the JPEG standard */

    memset(bits, 0, sizeof(bits));
    memset(codesize, 0, sizeof(codesize));
    for (i = 0; i < 257; i++)
        others[i] = -1; /* init links to empty */

    freq[256] = 1; /* make sure 256 has a nonzero count */
    /* Including the pseudo-symbol 256 in the Huffman procedure guarantees
     * that no real symbol is given code-value of all ones, because 256
     * will be placed last in the largest codeword category.
     */

    /* Group nonzero frequencies together so we can more easily find the
     * smallest.
     */
    num_nz_symbols = 0;
    for (i = 0; i < 257; i++) {
        if (freq[i]) {
            nz_index[num_nz_symbols] = i;
            freq[num_nz_symbols] = freq[i];
            num_nz_symbols++;
        }
    }

    /* Huffman's basic algorithm to assign optimal code lengths to symbols */

    for (;;) {
        /* Find the two smallest nonzero frequencies; set c1, c2 = their symbols */
        /* In case of ties, take the larger symbol number.  Since we have grouped
         * the nonzero symbols together, checking for zero symbols is not
         * necessary.
         */
        c1 = -1;
        c2 = -1;
        v = 1000000000L;
        v2 = 1000000000L;
        for (i = 0; i < num_nz_symbols; i++) {
            if (freq[i] <= v2) {
                if (freq[i] <= v) {
                    c2 = c1;
                    v2 = v;
                    v = freq[i];
                    c1 = i;
                } else {
                    v2 = freq[i];
                    c2 = i;
                }
            }
        }

        /* Done if we've merged everything into one frequency */
        if (c2 < 0)
            break;

        /* Else merge the two counts/trees */
        freq[c1] += freq[c2];
        /* Set the frequency to a very high value instead of zero, so we don't have
         * to check for zero values.
         */
        freq[c2] = 1000000001L;

        /* Increment the codesize of everything in c1's tree branch */
        codesize[c1]++;
        while (others[c1] >= 0) {
            c1 = others[c1];
            codesize[c1]++;
        }

        others[c1] = c2; /* chain c2 onto c1's tree branch */

        /* Increment the codesize of everything in c2's tree branch */
        codesize[c2]++;
        while (others[c2] >= 0) {
            c2 = others[c2];
            codesize[c2]++;
        }
    }

    /* Now count the number of symbols of each code length */
    for (i = 0; i < num_nz_symbols; i++) {
        /* The JPEG standard seems to think that this can't happen, */
        /* but I'm paranoid... */
        if (codesize[i] > MAX_CLEN) {
            err_log("codesize error\n");
            exit(2);
        }

        bits[codesize[i]]++;
    }

    /* Count the number of symbols with a length smaller than i bits, so we can
     * construct the symbol table more efficiently.  Note that this includes the
     * pseudo-symbol 256, but since it is the last symbol, it will not affect the
     * table.
     */
    p = 0;
    for (i = 1; i <= MAX_CLEN; i++) {
        bit_pos[i] = p;
        p += bits[i];
    }

    /* JPEG doesn't allow symbols with code lengths over 16 bits, so if the pure
     * Huffman procedure assigned any such lengths, we must adjust the coding.
     * Here is what Rec. ITU-T T.81 | ISO/IEC 10918-1 says about how this next
     * bit works: Since symbols are paired for the longest Huffman code, the
     * symbols are removed from this length category two at a time.  The prefix
     * for the pair (which is one bit shorter) is allocated to one of the pair;
     * then, skipping the BITS entry for that prefix length, a code word from the
     * next shortest nonzero BITS entry is converted into a prefix for two code
     * words one bit longer.
     */

    for (i = MAX_CLEN; i > 16; i--) {
        while (bits[i] > 0) {
            j = i - 2; /* find length of new prefix to be used */
            while (bits[j] == 0)
                j--;

            bits[i] -= 2;     /* remove two symbols */
            bits[i - 1]++;    /* one goes in this length */
            bits[j + 1] += 2; /* two new symbols in this length */
            bits[j]--;        /* symbol of this length is now a prefix */
        }
    }

    /* Remove the count for the pseudo-symbol 256 from the largest codelength */
    while (bits[i] == 0) /* find largest codelength still in use */
        i--;
    bits[i]--;

    /* Return final symbol counts (only for lengths 0..16) */
    memcpy(htbl->bits, bits, sizeof(htbl->bits));

    /* Return a list of the symbols sorted by code length */
    /* It's not real clear to me why we don't need to consider the codelength
     * changes made above, but Rec. ITU-T T.81 | ISO/IEC 10918-1 seems to think
     * this works.
     */
    for (i = 0; i < num_nz_symbols - 1; i++) {
        htbl->huffval[bit_pos[codesize[i]]] = (uint8_t)nz_index[i];
        bit_pos[codesize[i]]++;
    }
}

static void init_prev_shifts(struct jpeg_shared_t *shared) {
    for (int qi = 0; qi < RP_NUM_QUANT_TBLS; ++qi) {
        for (int i = 0; i < DCTSIZE2; ++i) {
            shared->prev_shifts[shared->is_top][qi][i] = shared->quality[shared->is_top] > shared->prev_quality[shared->is_top] ?
                shared->prev_dct_log2_tbl[shared->is_top][qi][i] - shared->dct_log2_tbl[shared->is_top][qi][i] :
                shared->dct_log2_tbl[shared->is_top][qi][i] - shared->prev_dct_log2_tbl[shared->is_top][qi][i];
            if (shared->prev_shifts[shared->is_top][qi][i] > MAX_COEF_BITS)
                err_log("prev_shifts[%d][%d][%d] err %d\n", (int)shared->is_top, qi, i, (int)shared->prev_shifts[shared->is_top][qi][i]);
        }
    }
}

static struct jpeg_shared_t jpeg_shared;
int decode_jpeg_delta(uint8_t *out, const uint8_t *in, int in_size, int rows_in_mcus, int l_h_samp, int l_v_samp, int quality, boolean is_top, int mcu_row, int width, int height) {
    // err_log("size %d, quality %d\n", in_size, quality);
    struct jpeg_shared_t *shared = &jpeg_shared;
    shared->out = out;
    shared->width = width;
    shared->height = height;
    shared->h_samp_factor = l_h_samp;
    shared->v_samp_factor = l_v_samp;
    shared->rows_in_mcus = rows_in_mcus;
    shared->is_top = is_top;
    shared->mcu_row = mcu_row;
    boolean need_prev_shifts = mcu_row == 0;
    if (need_prev_shifts) {
        shared->prev_quality[shared->is_top] = shared->quality[shared->is_top];
        // err_log("%d prev_quality %d, quality %d\n", shared->is_top, shared->prev_quality[shared->is_top], quality);
    }
    shared->quality[shared->is_top] = quality;

    std_huff_tables(shared);
    if (need_prev_shifts) {
        memcpy(shared->prev_dct_log2_tbl[shared->is_top], shared->dct_log2_tbl[shared->is_top], sizeof(shared->prev_dct_log2_tbl[shared->is_top]));
    }
    init_dct_table(std_luminance_quant_tbl, shared->dct_table[0], shared->dct_log2_tbl[shared->is_top][0], quality);
    init_dct_table(std_chrominance_quant_tbl, shared->dct_table[1], shared->dct_log2_tbl[shared->is_top][1], quality);
    if (need_prev_shifts && shared->prev_quality[shared->is_top] >= 0) {
        init_prev_shifts(shared);
    }

    memset(&shared->bitstate, 0, sizeof(shared->bitstate));

    shared->blocks_in_MCU = l_h_samp * l_v_samp + 2;
    for (int i = 0; i < shared->blocks_in_MCU; ++i) {
        shared->MCU_membership[i] = i < l_h_samp * l_v_samp ? 0 : i - l_h_samp * l_v_samp + 1;
    }

    for (int c = 0; c < RP_NUM_JPEG_COMP; ++c) {
        struct jpeg_comp_info_t *info = &shared->comp_infos[c];
        int dctbl = info->dc_tbl_no = c == 0 ? 0 : 1;
        int actbl = info->ac_tbl_no = c == 0 ? 0 : 1;

        jpeg_make_d_derived_tbl(TRUE, &shared->dc_huff_tbl_ptrs[dctbl], &shared->dc_derived_tbls[dctbl]);
        jpeg_make_d_derived_tbl(FALSE, &shared->ac_huff_tbl_ptrs[actbl], &shared->ac_derived_tbls[actbl]);
        info->quant_tbl_no = c == 0 ? 0 : 1;
        info->dct_table = shared->dct_table[info->quant_tbl_no];
        info->dct_log2_tbl = shared->dct_log2_tbl[shared->is_top][info->quant_tbl_no];

        shared->last_dc_val[c] = 0;
    }

    for (int blkn = 0; blkn < shared->blocks_in_MCU; blkn++) {
        int ci = shared->MCU_membership[blkn];
        struct jpeg_comp_info_t *info = &shared->comp_infos[ci];
        /* Precalculate which table to use for each block */
        shared->dc_cur_tbls[blkn] = &shared->dc_derived_tbls[info->dc_tbl_no];
        shared->ac_cur_tbls[blkn] = &shared->ac_derived_tbls[info->ac_tbl_no];

        shared->MCU_buffer[blkn] = &shared->MCU_buffer_base[blkn];
    }

    shared->next_input_byte = in;
    shared->bytes_in_buffer = in_size;
    shared->unread_marker = 0;

    if (consume_data(shared) < 0) {
        return -1;
    }
    if (shared->bytes_in_buffer > 2)
        err_log("extra data %d\n", (int)shared->bytes_in_buffer);

    return 0;
}

void reset_jpeg_delta(void) {
    struct jpeg_shared_t *shared = &jpeg_shared;
    memset(shared, 0, sizeof(*shared));

    for (int s = 0; s < SCREEN_COUNT; ++s) {
        shared->prev_quality[s] = shared->quality[s] = -1;
    }
}
