#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <string>
#include <map>
#include <vector>

typedef uint32_t AU1;

struct Extent {
    uint32_t width;
    uint32_t height;
};

struct FSRConstants {
    AU1 const0[4];
    AU1 const1[4];
    AU1 const2[4];
    AU1 const3[4];

    AU1 const0RCAS[4];

    struct Extent input;
    struct Extent output;
    AU1 Sample[4]; // unused
};

void prepareFSR(FSRConstants* fsrData, float rcasAttenuation);

uint32_t createFSRComputeProgramEAUS(const std::string& baseDir);
uint32_t createFSRComputeProgramRCAS(const std::string& baseDir);
uint32_t createBilinearComputeProgram(const std::string& baseDir);

#endif /* IMAGE_UTILS_H */
