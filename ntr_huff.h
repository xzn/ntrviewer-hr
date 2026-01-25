#ifndef NTR_HUFF_H
#define NTR_HUFF_H

#include "const.h"

struct huff_tbl_t {
    uint8_t bits[17];
    uint8_t huffval[256];
};

#define HUFF_LOOKAHEAD 8
struct d_derived_tbl_t {
    int32_t maxcode[18];
    int32_t valoffset[18];
    struct huff_tbl_t *tbl;
    int lookup[1 << HUFF_LOOKAHEAD];
};

void gen_optimal_table(struct huff_tbl_t *htbl, long freq[257]);
int make_d_derived_tbl(struct huff_tbl_t *htbl, struct d_derived_tbl_t *dtbl);

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
    int unread_marker;
} bitread_working_state;

#define BITREAD_STATE_VARS            \
    register bit_buf_type get_buffer; \
    register int bits_left;           \
    bitread_working_state br_state

#define BITREAD_LOAD_STATE(cinfop, permstate)           \
    br_state.unread_marker = cinfop->unread_marker;     \
    br_state.next_input_byte = cinfop->next_input_byte; \
    br_state.bytes_in_buffer = cinfop->bytes_in_buffer; \
    get_buffer = permstate.get_buffer;                  \
    bits_left = permstate.bits_left;

#define BITREAD_SAVE_STATE(cinfop, permstate)           \
    cinfop->unread_marker = br_state.unread_marker;     \
    cinfop->next_input_byte = br_state.next_input_byte; \
    cinfop->bytes_in_buffer = br_state.bytes_in_buffer; \
    permstate.get_buffer = get_buffer;                  \
    permstate.bits_left = bits_left

#define CHECK_BIT_BUFFER(state, nbits, action)                              \
    {                                                                       \
        if (bits_left < (nbits)) {                                          \
            if (!fill_bit_buffer(&(state), get_buffer, bits_left, nbits)) { \
                action;                                                     \
            }                                                               \
            get_buffer = (state).get_buffer;                                \
            bits_left = (state).bits_left;                                  \
        }                                                                   \
    }

#define GET_BITS(nbits) \
    (((int)(get_buffer >> (bits_left -= (nbits)))) & ((1 << (nbits)) - 1))

#define PEEK_BITS(nbits) \
    (((int)(get_buffer >> (bits_left - (nbits)))) & ((1 << (nbits)) - 1))

#define DROP_BITS(nbits) \
    (bits_left -= (nbits))

#define HUFF_DECODE(result, state, htbl, failaction, slowlabel)                   \
    {                                                                             \
        register int nb, look;                                                    \
        if (bits_left < HUFF_LOOKAHEAD) {                                         \
            if (!fill_bit_buffer(&state, get_buffer, bits_left, 0)) {             \
                failaction;                                                       \
            }                                                                     \
            get_buffer = state.get_buffer;                                        \
            bits_left = state.bits_left;                                          \
            if (bits_left < HUFF_LOOKAHEAD) {                                     \
                nb = 1;                                                           \
                goto slowlabel;                                                   \
            }                                                                     \
        }                                                                         \
        look = PEEK_BITS(HUFF_LOOKAHEAD);                                         \
        if ((nb = (htbl->lookup[look] >> HUFF_LOOKAHEAD)) <= HUFF_LOOKAHEAD) {    \
            DROP_BITS(nb);                                                        \
            result = htbl->lookup[look] & ((1 << HUFF_LOOKAHEAD) - 1);            \
        } else {                                                                  \
        slowlabel:                                                                \
            if ((result =                                                         \
                     huff_decode(&state, get_buffer, bits_left, htbl, nb)) < 0) { \
                failaction;                                                       \
            }                                                                     \
            get_buffer = state.get_buffer;                                        \
            bits_left = state.bits_left;                                          \
        }                                                                         \
    }

#define NEG_1 ((unsigned int)-1)
#define HUFF_EXTEND(x, s) \
    ((x) + ((((x) - (1 << ((s) - 1))) >> 31) & (((NEG_1) << (s)) + 1)))

#define MIN_GET_BITS (BIT_BUF_SIZE - 7)

int huff_decode(bitread_working_state *state,
    bit_buf_type get_buffer, int bits_left,
    struct d_derived_tbl_t *htbl, int min_bits);

boolean fill_bit_buffer(bitread_working_state *state,
    bit_buf_type get_buffer, int bits_left,
    int nbits);

#endif
