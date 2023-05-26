#include <stdint.h>

int ffmpeg_jls_encode(uint8_t *dst, int dst_size, const uint8_t *src, int src_x, int src_y, int src_line);
int ffmpeg_jls_decode(uint8_t *dst, int dst_x, int dst_y, int dst_line, const uint8_t *src, int src_size, int bpp);
