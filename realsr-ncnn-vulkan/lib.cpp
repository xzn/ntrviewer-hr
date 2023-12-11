// realsr implemented with ncnn library

#include <stdio.h>
#include <algorithm>
#include <queue>
#include <vector>
#include <clocale>

// ncnn
#include "cpu.h"
#include "gpu.h"
#include "platform.h"

#include "realsr.h"

#include "filesystem_utils.h"

static std::vector<RealSR*> realsr;
static int use_gpu_count;
static const int scale = 4;

extern "C" int realsr_create()
{
    std::vector<int> tilesize;
    path_t model = PATHSTR("models-DF2K_JPEG");
    std::vector<int> gpuid;
    int tta_mode = 0;

    if (tilesize.size() != (gpuid.empty() ? 1 : gpuid.size()) && !tilesize.empty())
    {
        fprintf(stderr, "invalid tilesize argument\n");
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

    if (model.find(PATHSTR("models-DF2K")) != path_t::npos
        || model.find(PATHSTR("models-DF2K_JPEG")) != path_t::npos)
    {
        prepadding = 10;
    }
    else
    {
        fprintf(stderr, "unknown model dir type\n");
        return -1;
    }

#if _WIN32
    wchar_t parampath[256];
    wchar_t modelpath[256];
    if (scale == 4)
    {
        swprintf(parampath, 256, L"%ls/x4.param", model.c_str());
        swprintf(modelpath, 256, L"%ls/x4.bin", model.c_str());
    }
#else
    char parampath[256];
    char modelpath[256];
    if (scale == 4)
    {
        sprintf(parampath, "%s/x4.param", model.c_str());
        sprintf(modelpath, "%s/x4.bin", model.c_str());
    }
#endif

    path_t paramfullpath = sanitize_filepath(parampath);
    path_t modelfullpath = sanitize_filepath(modelpath);

    ncnn::create_gpu_instance();

    if (gpuid.empty())
    {
        gpuid.push_back(ncnn::get_default_gpu_index());
    }

    use_gpu_count = (int)gpuid.size();

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

    for (int i=0; i<use_gpu_count; i++)
    {
        if (tilesize[i] != 0)
            continue;

        if (gpuid[i] == -1)
        {
            // cpu only
            tilesize[i] = 200;
            continue;
        }

        uint32_t heap_budget = ncnn::get_gpu_device(gpuid[i])->get_heap_budget();

        // more fine-grained tilesize policy here
        if (model.find(PATHSTR("models-DF2K")) != path_t::npos
            || model.find(PATHSTR("models-DF2K_JPEG")) != path_t::npos)
        {
            if (heap_budget > 1900)
                tilesize[i] = 200;
            else if (heap_budget > 550)
                tilesize[i] = 100;
            else if (heap_budget > 190)
                tilesize[i] = 64;
            else
                tilesize[i] = 32;
        }
    }

    realsr.resize(use_gpu_count, nullptr);

    for (int i=0; i<use_gpu_count; i++)
    {
        realsr[i] = new RealSR(gpuid[i], tta_mode);

        realsr[i]->load(paramfullpath, modelfullpath);

        realsr[i]->scale = scale;
        realsr[i]->tilesize = tilesize[i];
        realsr[i]->prepadding = prepadding;
    }

    return 0;
}

extern "C" void realsr_run(int w, int h, int c, const unsigned char *indata, unsigned char *outdata)
{
    ncnn::Mat inimage = ncnn::Mat(w, h, (void*)indata, (size_t)c, c);
    ncnn::Mat outimage = ncnn::Mat(w * scale, h * scale, (void*)outdata, (size_t)c, c);
    realsr[0]->process(inimage, outimage);
}

extern "C" void realsr_destroy()
{
    for (int i=0; i<use_gpu_count; i++)
    {
        delete realsr[i];
    }
    use_gpu_count = 0;
    realsr.clear();

    ncnn::destroy_gpu_instance();
}
