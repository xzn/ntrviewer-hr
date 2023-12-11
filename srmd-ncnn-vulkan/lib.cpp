// srmd implemented with ncnn library

#include <stdio.h>
#include <algorithm>
#include <queue>
#include <vector>
#include <clocale>

// ncnn
#include "cpu.h"
#include "gpu.h"
#include "platform.h"

#include "srmd.h"

#include "filesystem_utils.h"

static std::vector<SRMD*> srmd;
static int use_gpu_count;
static const int scale = 2;

extern "C" int srmd_create()
{
    int noise = 3;
    std::vector<int> tilesize;
    path_t model = PATHSTR("models-srmd");
    std::vector<int> gpuid;
    int tta_mode = 0;

    if (noise < -1 || noise > 10 || scale < 2 || scale > 4)
    {
        fprintf(stderr, "invalid noise or scale argument\n");
        return -1;
    }

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

    if (model.find(PATHSTR("models-srmd")) != path_t::npos)
    {
        prepadding = 12;
    }
    else
    {
        fprintf(stderr, "unknown model dir type\n");
        return -1;
    }

#if _WIN32
    wchar_t parampath[256];
    wchar_t modelpath[256];
    if (noise == -1)
    {
        swprintf(parampath, 256, L"%ls/srmdnf_x%d.param", model.c_str(), scale);
        swprintf(modelpath, 256, L"%ls/srmdnf_x%d.bin", model.c_str(), scale);
    }
    else
    {
        swprintf(parampath, 256, L"%ls/srmd_x%d.param", model.c_str(), scale);
        swprintf(modelpath, 256, L"%ls/srmd_x%d.bin", model.c_str(), scale);
    }
#else
    char parampath[256];
    char modelpath[256];
    if (noise == -1)
    {
        sprintf(parampath, "%s/srmdnf_x%d.param", model.c_str(), scale);
        sprintf(modelpath, "%s/srmdnf_x%d.bin", model.c_str(), scale);
    }
    else
    {
        sprintf(parampath, "%s/srmd_x%d.param", model.c_str(), scale);
        sprintf(modelpath, "%s/srmd_x%d.bin", model.c_str(), scale);
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

        uint32_t heap_budget = ncnn::get_gpu_device(gpuid[i])->get_heap_budget();

        // more fine-grained tilesize policy here
        if (model.find(PATHSTR("models-srmd")) != path_t::npos)
        {
            if (heap_budget > 2600)
                tilesize[i] = 400;
            else if (heap_budget > 740)
                tilesize[i] = 200;
            else if (heap_budget > 250)
                tilesize[i] = 100;
            else
                tilesize[i] = 32;
        }
    }

    srmd.resize(use_gpu_count, nullptr);

    for (int i=0; i<use_gpu_count; i++)
    {
        srmd[i] = new SRMD(gpuid[i], tta_mode);

        srmd[i]->load(paramfullpath, modelfullpath);

        srmd[i]->noise = noise;
        srmd[i]->scale = scale;
        srmd[i]->tilesize = tilesize[i];
        srmd[i]->prepadding = prepadding;
    }

    return 0;
}

extern "C" void srmd_run(int w, int h, int c, const unsigned char *indata, unsigned char *outdata)
{
    ncnn::Mat inimage = ncnn::Mat(w, h, (void*)indata, (size_t)c, c);
    ncnn::Mat outimage = ncnn::Mat(w * scale, h * scale, (void*)outdata, (size_t)c, c);
    srmd[0]->process(inimage, outimage);
}

extern "C" void srmd_destroy()
{
    for (int i=0; i<use_gpu_count; i++)
    {
        delete srmd[i];
    }
    use_gpu_count = 0;
    srmd.clear();

    ncnn::destroy_gpu_instance();
}
