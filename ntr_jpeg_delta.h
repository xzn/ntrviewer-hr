#ifndef NTR_JPEG_DELTA_H
#define NTR_JPEG_DELTA_H

#include "const.h"

int decode_jpeg_delta(uint8_t *out, const uint8_t *in, int in_size, int rows_in_mcus, int l_h_samp, int l_v_samp, int quality);

#endif
