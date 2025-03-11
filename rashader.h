#ifndef RASHADER_H
#define RASHADER_H

#ifdef __cplusplus
extern "C" {
#endif

#define LIBRA_RUNTIME_D3D11
#define LIBRA_RUNTIME_OPENGL
#include <librashader.h>

#include <stdint.h>

struct rashader_t;
struct rashader_t *rashader_load(const char *filename);
void rashader_unload(struct rashader_t *rashader);

size_t rashader_mode_count(struct rashader_t *rashader);
const char *rashader_mode_name(struct rashader_t *rashader, size_t index, const char *prefix);

typedef void *(*PFN_filter_chain_create)(libra_shader_preset_t *, void *);
typedef void (*PFN_filter_chain_free)(void *);

struct rashader_render_t;
struct rashader_render_t *rashader_render_init(struct rashader_t *rashader, size_t index, libra_preset_ctx_t *ctx, PFN_filter_chain_create fcc_fn, void *user);
void rashader_render_close(struct rashader_render_t *render, PFN_filter_chain_free fcf_fn);
void *rashader_render_chain(struct rashader_render_t *render);

#ifdef __cplusplus
}
#endif

#endif
