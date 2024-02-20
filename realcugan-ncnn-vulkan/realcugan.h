// realcugan implemented with ncnn library

#ifndef REALCUGAN_H
#define REALCUGAN_H

#include <string>

// ncnn
#if 0
#include "net.h"
#include "gpu.h"
#include "layer.h"
#else
#include "ncnn/net.h"
#include "ncnn/gpu.h"
#include "ncnn/layer.h"
#endif

#include "glad/glad.h"

class FeatureCache;

class RealCUGAN;

class OutVkMat : public ncnn::VkMat {
public:
    OutVkMat() : ncnn::VkMat(), totalsize(0), memory(0), gl_memory(0), gl_buffer(0), gl_texture(0) {}

    void create(const RealCUGAN* cugan, int _w, int _h, size_t _elemsize, int _elempack);
    void create(const RealCUGAN* cugan, int _w, int _h, int _c, size_t _elemsize, int _elempack);
    void release(const RealCUGAN* cugan);

    void create_handles(const RealCUGAN* cugan);
    void release_handles();

    size_t totalsize;
#ifdef _WIN32
    HANDLE memory;
#else
    int memory;
#endif
    GLuint gl_memory;
    GLuint gl_buffer;
    GLuint gl_texture;
};

class OutVkImageMat : public ncnn::VkImageMat {
public:
    OutVkImageMat() : ncnn::VkImageMat(), totalsize(0), width(0), height(0), depth(0), memory(0), gl_memory(0), gl_texture(0) {}

    void create_like(const RealCUGAN* cugan, const ncnn::VkMat& m, const ncnn::Option& opt);
    void create(const RealCUGAN* cugan, int _w, int _h, size_t _elemsize, int _elempack, const ncnn::Option& opt);
    void create(const RealCUGAN* cugan, int _w, int _h, int _c, size_t _elemsize, int _elempack, const ncnn::Option& opt);
    void release(const RealCUGAN* cugan);

    void create_handles(const RealCUGAN* cugan);
    void release_handles();

    size_t totalsize;
    uint32_t width;
    uint32_t height;
    uint32_t depth;

#ifdef _WIN32
    HANDLE memory;
#else
    int memory;
#endif
    GLuint gl_memory;
    GLuint gl_texture;
};

class RealCUGAN
{
public:
    RealCUGAN(int gpuid, bool tta_mode = false, int num_threads = 1);
    ~RealCUGAN();

#if _WIN32
    int load(const std::wstring& parampath, const std::wstring& modelpath);
#else
    int load(const std::string& parampath, const std::string& modelpath);
#endif

    int process(const ncnn::Mat& inimage, ncnn::Mat& outimage) const;

    int process_cpu(const ncnn::Mat& inimage, ncnn::Mat& outimage) const;

    int process_se(const ncnn::Mat& inimage, ncnn::Mat& outimage) const;

    int process_cpu_se(const ncnn::Mat& inimage, ncnn::Mat& outimage) const;

    int process_se_rough(const ncnn::Mat& inimage, ncnn::Mat& outimage) const;

    int process_cpu_se_rough(const ncnn::Mat& inimage, ncnn::Mat& outimage) const;

    int process_se_very_rough(const ncnn::Mat& inimage, ncnn::Mat& outimage) const;

    int process_cpu_se_very_rough(const ncnn::Mat& inimage, ncnn::Mat& outimage) const;

protected:
    int process_se_stage0(const ncnn::Mat& inimage, const std::vector<std::string>& names, const std::vector<std::string>& outnames, const ncnn::Option& opt, FeatureCache& cache) const;
    int process_se_stage2(const ncnn::Mat& inimage, const std::vector<std::string>& names, ncnn::Mat& outimage, const ncnn::Option& opt, FeatureCache& cache) const;
    int process_se_sync_gap(const ncnn::Mat& inimage, const std::vector<std::string>& names, const ncnn::Option& opt, FeatureCache& cache) const;

    int process_se_very_rough_stage0(const ncnn::Mat& inimage, const std::vector<std::string>& names, const std::vector<std::string>& outnames, const ncnn::Option& opt, FeatureCache& cache) const;
    int process_se_very_rough_sync_gap(const ncnn::Mat& inimage, const std::vector<std::string>& names, const ncnn::Option& opt, FeatureCache& cache) const;

    int process_cpu_se_stage0(const ncnn::Mat& inimage, const std::vector<std::string>& names, const std::vector<std::string>& outnames, FeatureCache& cache) const;
    int process_cpu_se_stage2(const ncnn::Mat& inimage, const std::vector<std::string>& names, ncnn::Mat& outimage, FeatureCache& cache) const;
    int process_cpu_se_sync_gap(const ncnn::Mat& inimage, const std::vector<std::string>& names, FeatureCache& cache) const;

    int process_cpu_se_very_rough_stage0(const ncnn::Mat& inimage, const std::vector<std::string>& names, const std::vector<std::string>& outnames, FeatureCache& cache) const;
    int process_cpu_se_very_rough_sync_gap(const ncnn::Mat& inimage, const std::vector<std::string>& names, FeatureCache& cache) const;

public:
    // realcugan parameters
    int noise;
    int scale;
    int tilesize;
    int prepadding;
    int syncgap;

// private:
    ncnn::VulkanDevice* vkdev;
    ncnn::Net net;
    ncnn::Pipeline* realcugan_preproc;
    ncnn::Pipeline* realcugan_postproc;
    ncnn::Pipeline* realcugan_4x_postproc;
    ncnn::Layer* bicubic_2x;
    ncnn::Layer* bicubic_3x;
    ncnn::Layer* bicubic_4x;
    bool tta_mode;

    // OutVkMat* out_gpu;
    ncnn::VkMat* out_gpu_buf;
    OutVkImageMat* out_gpu_tex;
    bool support_ext_mem;
};

#endif // REALCUGAN_H
