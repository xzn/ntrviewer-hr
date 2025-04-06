#include "rashader.h"

struct mtl_ctx_t {
    void *queue;
};

void *mtl_filter_chain_create(libra_shader_preset_t *preset, void *ctx);
void mtl_filter_chain_free(void *fc, void *ctx);

bool mtl_filter_chain_frame(struct rashader_render_t *render, void *ctx, void *upload_evt, void *libra_evt, uint64_t upload_val, uint64_t libra_val, void *src, void *img);

void init_local_network_access(void);

extern PFN_filter_chain_set_param mtl_filter_chain_set_param;
