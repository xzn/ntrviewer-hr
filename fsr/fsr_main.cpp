#include <cstdio>
#include <string>
#include <vector>

#include "fsr_main.h"

#include "image_utils.h"

static void runFSR(struct Extent outputExtent, uint32_t fsrProgramEASU, uint32_t fsrProgramRCAS, uint32_t fsrData_vbo, uint32_t inputImage, uint32_t outputImage) {
    uint32_t displayWidth = outputExtent.width;
    uint32_t displayHeight = outputExtent.height;

    static const int threadGroupWorkRegionDim = 16;
    int dispatchX = (displayWidth + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
    int dispatchY = (displayHeight + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;


    // binding point constants in the shaders
    const int inFSRDataPos = 0;
    const int inFSRInputTexture = 1;
    const int inFSROutputTexture = 2;

    { // run FSR EASU
        glUseProgram(fsrProgramEASU);

        // connect the input uniform data
        glBindBufferBase(GL_UNIFORM_BUFFER, inFSRDataPos, fsrData_vbo);

        // bind the input image to a texture unit
        glActiveTexture(GL_TEXTURE0 + inFSRInputTexture);
        glBindTexture(GL_TEXTURE_2D, inputImage);

        // connect the output image
        glBindImageTexture(inFSROutputTexture, outputImage, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

        glDispatchCompute(dispatchX, dispatchY, 1);
        // glFinish();
    }

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    {
        // FSR RCAS
        // connect the input uniform data
        glBindBufferBase(GL_UNIFORM_BUFFER, inFSRDataPos, fsrData_vbo);

        // connect the previous image's output as input
        glActiveTexture(GL_TEXTURE0 + inFSRInputTexture);
        glBindTexture(GL_TEXTURE_2D, outputImage);

        // connect the output image which is the same as the input image
        glBindImageTexture(inFSROutputTexture, outputImage, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

        glUseProgram(fsrProgramRCAS);
        glDispatchCompute(dispatchX, dispatchY, 1);
        // glFinish();
    }
}

static void runBilinear(struct FSRConstants fsrData, uint32_t bilinearProgram, int32_t fsrData_vbo, uint32_t inputImage, uint32_t outputImage) {
    uint32_t displayWidth = fsrData.output.width;
    uint32_t displayHeight = fsrData.output.height;

    static const int threadGroupWorkRegionDim = 16;
    int dispatchX = (displayWidth + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
    int dispatchY = (displayHeight + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;


    // binding point constants in the shaders
    const int inFSRDataPos = 0;
    const int inFSRInputTexture = 1;
    const int inFSROutputTexture = 2;

    { // run FSR EASU
        glUseProgram(bilinearProgram);

        // connect the input uniform data
        glBindBufferBase(GL_UNIFORM_BUFFER, inFSRDataPos, fsrData_vbo);

        // bind the input image to a texture unit
        glActiveTexture(GL_TEXTURE0 + inFSRInputTexture);
        glBindTexture(GL_TEXTURE_2D, inputImage);

        // connect the output image
        glBindImageTexture(inFSROutputTexture, outputImage, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

        glDispatchCompute(dispatchX, dispatchY, 1);
        glFinish();
    }
}

uint32_t createOutputImage(struct FSRConstants fsrData) {
    uint32_t outputImage = 0;
    glGenTextures(1, &outputImage);
    glBindTexture(GL_TEXTURE_2D, outputImage);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, fsrData.output.width, fsrData.output.height);
    glBindTexture(GL_TEXTURE_2D, 0);

    return outputImage;
}

static void glfw_error_callback(int error, const char* description) {
        fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

static void on_gl_error(GLenum source, GLenum type, GLuint id, GLenum severity,
                          GLsizei length, const GLchar* message, const void *userParam) {

    printf("-> %s\n", message);
}

struct fsr_last_t {
    uint32_t out_w, out_h;
    GLuint vbo, out_image;
    float rcasAtt;
};

std::vector<struct fsr_last_t> fsr_last;

uint32_t fsrProgramEASU, fsrProgramRCAS;

extern "C" GLuint fsr_main(int index, GLuint inputTexture, uint32_t in_w, uint32_t in_h, uint32_t out_w, uint32_t out_h, float rcasAtt) {
    const std::string baseDir = "fsr/";

    if (!fsrProgramEASU)
        fsrProgramEASU = createFSRComputeProgramEAUS(baseDir);
    if (!fsrProgramRCAS)
        fsrProgramRCAS = createFSRComputeProgramRCAS(baseDir);
    // uint32_t bilinearProgram = createBilinearComputeProgram(baseDir);

    int min_size = index + 1;
    if (min_size > fsr_last.size()) {
        fsr_last.resize(min_size);
    }

    struct fsr_last_t *l = &fsr_last[index];

    struct Extent outputExtent = { out_w, out_h };
    if (l->out_w != out_w || l->out_h != out_h || l->rcasAtt != rcasAtt) {
        struct FSRConstants fsrData = {};

        fsrData.input = { in_w, in_h };
        fsrData.output = outputExtent;

        prepareFSR(&fsrData, rcasAtt);

        // upload the FSR constants, this contains the EASU and RCAS constants in a single uniform
        if (l->vbo)
            glDeleteBuffers(1, &l->vbo);

        {
            glGenBuffers(1, &l->vbo);
            glBindBuffer(GL_ARRAY_BUFFER, l->vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(fsrData), &fsrData, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        if (l->out_image) {
            glDeleteTextures(1, &l->out_image);
        }
        l->out_image = createOutputImage(fsrData);
    }

    // runBilinear(fsrData, bilinearProgram, fsrData_vbo, inputTexture, outputImage);
    runFSR(outputExtent, fsrProgramEASU, fsrProgramRCAS, l->vbo, inputTexture, l->out_image);

    return l->out_image;
}
