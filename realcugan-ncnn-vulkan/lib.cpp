// realcugan implemented with ncnn library

#include <stdio.h>
#include <algorithm>
#include <queue>
#include <vector>
#include <clocale>
#include <mutex>
#include <memory>

#if 0
#include "cpu.h"
#include "gpu.h"
#include "platform.h"
#else
#include "ncnn/cpu.h"
#include "ncnn/gpu.h"
#include "ncnn/platform.h"
#endif

#include "realcugan.h"
#include "lib.h"

#include "filesystem_utils.h"

static RealCUGAN* realcugan;
static std::vector<std::unique_ptr<std::mutex>> realcugan_locks;
static const int scale = 2;

extern "C" int realcugan_create()
{
    int noise = -1;
    std::vector<int> tilesize;
    path_t model = PATHSTR("models-se");
    std::vector<int> gpuid;
    int syncgap = 3;
    // int syncgap = 0;
    int tta_mode = 0;

    if (noise < -1 || noise > 3)
    {
        fprintf(stderr, "invalid noise argument\n");
        return -1;
    }

    if (!(scale == 1 || scale == 2 || scale == 3 || scale == 4))
    {
        fprintf(stderr, "invalid scale argument\n");
        return -1;
    }

    if (tilesize.size() != (gpuid.empty() ? 1 : gpuid.size()) && !tilesize.empty())
    {
        fprintf(stderr, "invalid tilesize argument\n");
        return -1;
    }

    if (!(syncgap == 0 || syncgap == 1 || syncgap == 2 || syncgap == 3))
    {
        fprintf(stderr, "invalid syncgap argument\n");
        return -1;
    }

    for (int i=0; i<(int)tilesize.size(); i++)
    {
        if (tilesize[i] != 0 && tilesize[i] < 32)
        {
            fprintf(stderr, "invalid tilesize argument\n");
            return -1;
        }
    }

    int prepadding = 0;

    if (model.find(PATHSTR("models-se")) != path_t::npos
        || model.find(PATHSTR("models-nose")) != path_t::npos
        || model.find(PATHSTR("models-pro")) != path_t::npos)
    {
        if (scale == 2)
        {
            prepadding = 18;
        }
        if (scale == 3)
        {
            prepadding = 14;
        }
        if (scale == 4)
        {
            prepadding = 19;
        }
    }
    else
    {
        fprintf(stderr, "unknown model dir type\n");
        return -1;
    }

    if (model.find(PATHSTR("models-nose")) != path_t::npos)
    {
        // force syncgap off for nose models
        syncgap = 0;
    }

#if _WIN32
    wchar_t parampath[256];
    wchar_t modelpath[256];
    if (noise == -1)
    {
        swprintf(parampath, 256, L"%ls/up%dx-conservative.param", model.c_str(), scale);
        swprintf(modelpath, 256, L"%ls/up%dx-conservative.bin", model.c_str(), scale);
    }
    else if (noise == 0)
    {
        swprintf(parampath, 256, L"%ls/up%dx-no-denoise.param", model.c_str(), scale);
        swprintf(modelpath, 256, L"%ls/up%dx-no-denoise.bin", model.c_str(), scale);
    }
    else
    {
        swprintf(parampath, 256, L"%ls/up%dx-denoise%dx.param", model.c_str(), scale, noise);
        swprintf(modelpath, 256, L"%ls/up%dx-denoise%dx.bin", model.c_str(), scale, noise);
    }
#else
    char parampath[256];
    char modelpath[256];
    if (noise == -1)
    {
        sprintf(parampath, "%s/up%dx-conservative.param", model.c_str(), scale);
        sprintf(modelpath, "%s/up%dx-conservative.bin", model.c_str(), scale);
    }
    else if (noise == 0)
    {
        sprintf(parampath, "%s/up%dx-no-denoise.param", model.c_str(), scale);
        sprintf(modelpath, "%s/up%dx-no-denoise.bin", model.c_str(), scale);
    }
    else
    {
        sprintf(parampath, "%s/up%dx-denoise%dx.param", model.c_str(), scale, noise);
        sprintf(modelpath, "%s/up%dx-denoise%dx.bin", model.c_str(), scale, noise);
    }
#endif

    path_t paramfullpath = sanitize_filepath(parampath);
    path_t modelfullpath = sanitize_filepath(modelpath);

    ncnn::create_gpu_instance();

    if (gpuid.empty())
    {
        gpuid.push_back(ncnn::get_default_gpu_index());
    }

    int use_gpu_count = (int)gpuid.size();

    if (tilesize.empty())
    {
        tilesize.resize(use_gpu_count, 0);
    }

    int gpu_count = ncnn::get_gpu_count();
    for (int i=0; i<use_gpu_count; i++)
    {
        if (gpuid[i] < -1 || gpuid[i] >= gpu_count)
        {
            fprintf(stderr, "invalid gpu device\n");

            ncnn::destroy_gpu_instance();
            return -1;
        }
    }

    int i = 0;

    for (; i<use_gpu_count; i++)
    {
        if (tilesize[i] != 0)
            continue;

        if (gpuid[i] == -1)
        {
            // cpu only
            tilesize[i] = 400;
            continue;
        }

        uint32_t heap_budget = ncnn::get_gpu_device(gpuid[i])->get_heap_budget();

        // more fine-grained tilesize policy here
        if (model.find(PATHSTR("models-nose")) != path_t::npos || model.find(PATHSTR("models-se")) != path_t::npos || model.find(PATHSTR("models-pro")) != path_t::npos)
        {
            if (scale == 2)
            {
                if (heap_budget > 1300)
                    tilesize[i] = 400;
                else if (heap_budget > 800)
                    tilesize[i] = 300;
                else if (heap_budget > 400)
                    tilesize[i] = 200;
                else if (heap_budget > 200)
                    tilesize[i] = 100;
                else
                    tilesize[i] = 32;
            }
            if (scale == 3)
            {
                if (heap_budget > 3300)
                    tilesize[i] = 400;
                else if (heap_budget > 1900)
                    tilesize[i] = 300;
                else if (heap_budget > 950)
                    tilesize[i] = 200;
                else if (heap_budget > 320)
                    tilesize[i] = 100;
                else
                    tilesize[i] = 32;
            }
            if (scale == 4)
            {
                if (heap_budget > 1690)
                    tilesize[i] = 400;
                else if (heap_budget > 980)
                    tilesize[i] = 300;
                else if (heap_budget > 530)
                    tilesize[i] = 200;
                else if (heap_budget > 240)
                    tilesize[i] = 100;
                else
                    tilesize[i] = 32;
            }
        }
        break;
    }

    if (i == use_gpu_count) {
        fprintf(stderr, "no suitable gpu device\n");

        ncnn::destroy_gpu_instance();
        return -1;
    }

    if (realcugan) {
        delete realcugan;
    }
    realcugan = new RealCUGAN(gpuid[i], tta_mode);
    realcugan->load(paramfullpath, modelfullpath);
    realcugan->noise = noise;
    realcugan->scale = scale;
    realcugan->tilesize = tilesize[i];
    realcugan->prepadding = prepadding;
    realcugan->syncgap = syncgap;

    return 0;
}

extern "C" GLuint realcugan_run(int index, int w, int h, int c, const unsigned char *indata, unsigned char *outdata, GLuint *gl_sem, GLuint *gl_sem_next, bool *dim3, bool *success)
{
    ncnn::Mat inimage = ncnn::Mat(w, h, (void*)indata, (size_t)c, c);
    ncnn::Mat outimage = ncnn::Mat(w * scale, h * scale, (void*)outdata, (size_t)c, c);

    if (index + 1 > realcugan_locks.size()) {
        realcugan_locks.resize(index + 1);
    }

    if (!realcugan_locks[index]) {
        realcugan_locks[index] = std::make_unique<std::mutex>();
    }

    realcugan_locks[index]->lock();
    if (realcugan->process(index, inimage, outimage) != 0) {
        *success = false;
        return 0;
    }
    realcugan_locks[index]->unlock();

    OutVkImageMat *out = realcugan->out_gpu_tex[index];
    GLuint tex = out->gl_texture;
    *gl_sem = out->gl_sem;
    *gl_sem_next = out->gl_sem_next;
    *dim3 = out->depth > 1;
    *success = true;
    return tex;
}

extern "C" void realcugan_next(int index)
{
    if (!realcugan || index >= realcugan->out_gpu_tex.size()) {
        return;
    }
    OutVkImageMat *out = realcugan->out_gpu_tex[index];
    if (!out || !out->first_subseq) {
        return;
    }

    if (out->need_wait) {
        realcugan_locks[index]->lock();
        if (out->need_wait) {
            VkResult ret = ncnn::vkWaitForFences(realcugan->vkdev->vkdevice(), 1, &out->fence, VK_TRUE, (uint64_t)-1);
            if (ret != VK_SUCCESS) {
                NCNN_LOGE("vkWaitForFences failed %d", ret);
            }
            out->need_wait = false;
        }
        realcugan_locks[index]->unlock();
    }
}

extern "C" void realcugan_destroy()
{
    if (realcugan) {
        for (int i = 0; i < realcugan->out_gpu_tex.size(); ++i) {
            realcugan_next(i);
        }
        delete realcugan;
        realcugan = nullptr;
    }

    ncnn::destroy_gpu_instance();
}
