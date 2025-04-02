#include "ui_renderer_metal.h"

void *mtl_filter_chain_create(libra_shader_preset_t *preset, void *ctx) {
    struct filter_chain_mtl_opt_t opt = {
        .version = libra_instance_api_version(),
    };
    struct mtl_ctx_t *mtl_ctx = ctx;
    libra_mtl_filter_chain_t out;
    id<MTLCommandQueue> queue = mtl_ctx->queue;
    if (!queue) {
        return NULL;
    }
    libra_error_t err = libra_mtl_filter_chain_create(preset, queue, &opt, &out);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
        return NULL;
    }

    return out;
}

void mtl_filter_chain_free(void *fc, void *) {
    libra_error_t err = libra_mtl_filter_chain_free((libra_mtl_filter_chain_t *)fc);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
    }
}

PFN_filter_chain_set_param mtl_filter_chain_set_param = (PFN_filter_chain_set_param)libra_mtl_filter_chain_set_param;

bool mtl_filter_chain_frame(struct rashader_render_t *render, void *ctx, void *upload_evt, void *libra_evt, uint64_t upload_val, uint64_t libra_val, void *src, void *img) {
    struct mtl_ctx_t *mtl_ctx = ctx;
    id<MTLCommandQueue> queue = mtl_ctx->queue;
    id<MTLSharedEvent> upload = upload_evt;
    id<MTLSharedEvent> libra = libra_evt;
    id<MTLTexture> src_tex = src;
    id<MTLTexture> img_tex = img;

    @autoreleasepool {
        id<MTLCommandBuffer> cmd = [queue commandBuffer];
        if (!cmd)
            return false;
        if (upload)
            [cmd encodeWaitForEvent:upload value:upload_val];

        libra_mtl_filter_chain_t *chain = rashader_render_chain(render);
        frame_mtl_opt_t opt = { .version = libra_instance_api_version() };
        libra_mtl_filter_chain_frame(chain, cmd, 1, src_tex, img_tex, NULL, NULL, &opt);

        [cmd encodeSignalEvent:libra value:libra_val];
        [cmd commit];
    }
    return true;
}
