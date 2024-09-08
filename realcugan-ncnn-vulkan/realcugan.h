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

class OutVkImageMat : public ncnn::VkImageMat {
public:
    OutVkImageMat() : ncnn::VkImageMat() {}

    void create_like(const RealCUGAN* cugan, const ncnn::VkMat& m, const ncnn::Option& opt);
    void create(const RealCUGAN* cugan, int _w, int _h, size_t _elemsize, int _elempack, const ncnn::Option& opt);
    void create(const RealCUGAN* cugan, int _w, int _h, int _c, size_t _elemsize, int _elempack, const ncnn::Option& opt);
    void release(const RealCUGAN* cugan);

    void create_handles(const RealCUGAN* cugan);
    void release_handles();

    size_t totalsize = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;

#ifdef _WIN32
    HANDLE memory = 0;
#else
    int memory = 0;
#endif
    GLuint gl_memory = 0;
    GLuint gl_texture = 0;
    bool dedicated = 0;

    VkSemaphore vk_sem = 0, vk_sem_next = 0;
#ifdef _WIN32
    HANDLE sem = 0, sem_next = 0;
#else
    int sem = 0, sem_next = 0;
#endif
    GLuint gl_sem = 0, gl_sem_next = 0;
    void create_sem(const RealCUGAN* cugan);
    void destroy_sem(const RealCUGAN* cugan);

    bool first_subseq = 0;
    ncnn::VkCompute *cmd = 0;
    VkFence fence = 0;
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

    int process(int index, const ncnn::Mat& inimage, ncnn::Mat& outimage) const;

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
    int noise = 0;
    int scale = 0;
    int tilesize = 0;
    int prepadding = 0;
    int syncgap = 0;

// private:
    ncnn::VulkanDevice* vkdev = 0;
    ncnn::Net net;
    ncnn::Pipeline* realcugan_preproc = 0;
    ncnn::Pipeline* realcugan_postproc = 0;
    ncnn::Pipeline* realcugan_4x_postproc = 0;
    ncnn::Layer* bicubic_2x = 0;
    ncnn::Layer* bicubic_3x = 0;
    ncnn::Layer* bicubic_4x = 0;
    bool tta_mode = 0;

    mutable std::vector<OutVkImageMat*> out_gpu_tex;
    bool support_ext_mem = 0;
    bool tiling_linear = 0;
};

#endif // REALCUGAN_H
