#include "iz_c.h"
#include "encoder.h"

namespace IZ {

void initEncodeTable();

}

#ifdef __cplusplus
extern "C" {
#endif

void izInitEncodeTable() {
    IZ::initEncodeTable();
}

int izEncodeImageRGB(struct BitCoderPtrs *dst, const uint8_t *src, int width, int height, int pitch) {
    IZ::ConstImage<> image;
    image.setWidth(width);
    image.setHeight(height);
    image.setSamplesPerLine(pitch);
    image.setData(src);

    IZ::ImageEncoder<> ic;
    ic.begin((unsigned char *)dst->p);
    ic.p_end = dst->p_end;
    ic.flush = dst->flush;
    ic.user = dst->user;
    return ic.encodeImagePixels(image);
}

#ifdef __cplusplus
}
#endif
