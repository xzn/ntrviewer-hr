#include "ntr_huff.h"

#include <stdlib.h>

void gen_optimal_table(struct huff_tbl_t *htbl, long freq[257])
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

int make_d_derived_tbl(struct huff_tbl_t *htbl, struct d_derived_tbl_t *dtbl)
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

    return numsymbols;
}

int huff_decode(bitread_working_state *state,
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

boolean fill_bit_buffer(bitread_working_state *state,
                     register bit_buf_type get_buffer, register int bits_left,
                     int nbits)
/* Load up the bit buffer to a depth of at least nbits */
{
    /* Copy heavily used state fields into locals (hopefully registers) */
    register const uint8_t *next_input_byte = state->next_input_byte;
    register size_t bytes_in_buffer = state->bytes_in_buffer;

    /* Attempt to load at least MIN_GET_BITS bits into get_buffer. */
    /* (It is assumed that no request will be for more than that many bits.) */
    /* We fail to do so only if we hit a marker or are forced to suspend. */

    if (state->unread_marker == 0) { /* cannot advance past a marker */
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
                    state->unread_marker = c;
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
