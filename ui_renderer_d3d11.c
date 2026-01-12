#include "ui_compositor_csc.h"
#include "ui_common_sdl.h"
#include "ui_renderer_d3d11.h"
#include "ui_main_nk.h"
#include "main.h"
#include "placebo.h"
#include "rashader.h"
#include <libplacebo/d3d11.h>

static SDL_Window *sdl_win[SCREEN_COUNT];
static struct nk_context *nk_ctx;

#include "nuklear_d3d11.h"

#include <versionhelpers.h>
#include <math.h>

#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_INDEX_BUFFER 128 * 1024

static ID3D11BlendState *d3d_ui_bs[SCREEN_COUNT];
static ID3D11VertexShader *d3d_vs[SCREEN_COUNT];
static ID3D11VertexShader *d3d_data_vs[SCREEN_COUNT];
static ID3D11PixelShader *d3d_ps[SCREEN_COUNT];
static ID3D11PixelShader *d3d_ui_ps;
static ID3D11SamplerState *d3d_ss_point[SCREEN_COUNT];
static ID3D11SamplerState *d3d_ss_linear[SCREEN_COUNT];
static ID3D11RasterizerState *d3d_rs[SCREEN_COUNT];
static ID3D11RasterizerState *d3d_scissor_rs[SCREEN_COUNT];

enum {
    UPSCALING_DEFAULT_NONE = 0,
    UPSCALING_DEFAULT_COUNT,
};

#define PLACEBO_UI_INDEX(mode) (UPSCALING_DEFAULT_COUNT + mode)
#define PLACEBO_MODE(ui_index) (ui_index - PLACEBO_UI_INDEX(0))
#define IS_PLACEBO(ui_index) (PLACEBO_MODE(ui_index) >= 0 && PLACEBO_MODE(ui_index) < placebo_count)

static struct placebo_t *placebo;
static int placebo_count;
static struct placebo_render_t *placebo_render[SCREEN_COUNT][SCREEN_COUNT];
static int placebo_render_mode[SCREEN_COUNT][SCREEN_COUNT];

static pl_d3d11 pl_d3d11_dev[SCREEN_COUNT];
static pl_log pl_log_dev;

#define RASHADER_UI_INDEX(mode) (UPSCALING_DEFAULT_COUNT + placebo_count + mode)
#define RASHADER_MODE(ui_index) (ui_index - RASHADER_UI_INDEX(0))
#define IS_RASHADER(ui_index) (RASHADER_MODE(ui_index) >= 0 && RASHADER_MODE(ui_index) < rashader_count)

static struct rashader_t *rashader;
static int rashader_count;
static struct rashader_render_t *rashader_render[SCREEN_COUNT][SCREEN_COUNT];
static int rashader_render_mode[SCREEN_COUNT][SCREEN_COUNT];

static int d3d11_upscaling_init(void) {
    bool use_placebo = true;
    bool use_rashader = true;

    ui_upscaling_filter_count = UPSCALING_DEFAULT_COUNT;

    pl_log_dev = placebo_log_create();
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        pl_d3d11_dev[j] = pl_d3d11_create(pl_log_dev, pl_d3d11_params(
            .device = d3d11device[j],
        ));
        if (!pl_d3d11_dev[j]) {
            use_placebo = false;
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            placebo_render_mode[j][i] = -1;
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            rashader_render_mode[j][i] = -1;
        }
    }

    if (use_placebo) {
        placebo = placebo_load("placebo.json");
        if (placebo) {
            placebo_count = placebo_mode_count(placebo);
            ui_upscaling_filter_count += placebo_count;
        }
    }

    if (use_rashader) {
        rashader = rashader_load("rashader.json");
        if (rashader) {
            rashader_count = rashader_mode_count(rashader);
            ui_upscaling_filter_count += rashader_count;
        }
    }

    ui_upscaling_filter_options = malloc(ui_upscaling_filter_count * sizeof(*ui_upscaling_filter_options));
    if (!ui_upscaling_filter_options) {
        return -1;
    }

    ui_upscaling_filter_options[UPSCALING_DEFAULT_NONE] = NK_UPSCALE_TYPE_TEXT_NONE "None";

    for (int i = 0; i < placebo_count; ++i) {
        ui_upscaling_filter_options[PLACEBO_UI_INDEX(i)] = placebo_mode_name(placebo, i, NK_UPSCALE_TYPE_TEXT_PLACEBO);
    }

    for (int i = 0; i < rashader_count; ++i) {
        ui_upscaling_filter_options[RASHADER_UI_INDEX(i)] = rashader_mode_name(rashader, i, NK_UPSCALE_TYPE_TEXT_RASHADER);
    }

    ui_upscaling_selected = UPSCALING_DEFAULT_NONE;

    return 0;
}

static void d3d11_filter_chain_free(void *fc, void *);
static void d3d11_upscaling_close(void) {
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        for (int i = 0; i < SCREEN_COUNT; ++i) {
            if (placebo_render[j][i]) {
                placebo_render_close(placebo_render[j][i]);
                placebo_render[j][i] = 0;
            }
        }

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            if (rashader_render[j][i]) {
                rashader_render_close(rashader_render[j][i], d3d11_filter_chain_free, NULL);
                rashader_render[j][i] = 0;
            }
        }

        pl_d3d11_destroy(&pl_d3d11_dev[j]);
    }
    placebo_log_destroy(&pl_log_dev);

    if (placebo) {
        placebo_unload(placebo);
        placebo = 0;
    }
    placebo_count = 0;

    if (rashader) {
        rashader_unload(rashader);
        rashader = 0;
    }
    rashader_count = 0;

    if (ui_upscaling_filter_options) {
        free(ui_upscaling_filter_options);
        ui_upscaling_filter_options = 0;
    }
    ui_upscaling_filter_count = 0;
}

static const char *d3d_vs_src =
    "struct VSInput\n"
    "{\n"
    " uint vid : SV_VertexID;\n"
    "};\n"
    "struct VSOutput\n"
    "{\n"
    " float4 position: SV_Position;\n"
    " float2 uv: TEXCOORD;\n"
    "};\n"
    "VSOutput Main(VSInput input)\n"
    "{\n"
    " VSOutput output = (VSOutput)0;\n"
    " output.uv = float2((input.vid << 1) & 2, input.vid & 2);\n"
    " output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
    " return output;\n"
    "}\n";
static const char *d3d_data_vs_src =
    "struct VSInput\n"
    "{\n"
    " uint vid : SV_VertexID;\n"
    "};\n"
    "struct VSOutput\n"
    "{\n"
    " float4 position: SV_Position;\n"
    " float2 uv: TEXCOORD;\n"
    "};\n"
    "VSOutput Main(VSInput input)\n"
    "{\n"
    " VSOutput output = (VSOutput)0;\n"
    " output.uv = float2((input.vid << 1) & 2, input.vid & 2);\n"
    " output.position = float4(output.uv.yx * 2.0 + -1.0, 0.0, 1.0);\n"
    " return output;\n"
    "}\n";
#define d3d_ui_ps_src_0 \
    " if (any(color != float4(0.0, 0.0, 0.0, 0.0)))\n" \
    "  color = float4(color.rgb, 7.0 / 8.0);\n"
#define d3d_ps_src_use_0(src_0) \
    "SamplerState my_samp: register(s0);\n" \
    "Texture2D my_tex: register(t0);\n" \
    "struct PSInput\n" \
    "{\n" \
    " float4 position: SV_Position;\n" \
    " float2 uv: TEXCOORD;\n" \
    "};\n" \
    "struct PSOutput\n" \
    "{\n" \
    " float4 color: SV_Target0;\n" \
    "};\n" \
    "PSOutput Main(PSInput input)\n" \
    "{\n" \
    " PSOutput output = (PSOutput)0;\n" \
    " float4 color = my_tex.Sample(my_samp, input.uv);\n" \
    src_0 \
    " output.color = color;\n" \
    " return output;\n" \
    "}\n"
static const char *d3d_ps_src = d3d_ps_src_use_0("");
static const char *d3d_ui_ps_src = d3d_ps_src_use_0(d3d_ui_ps_src_0);

static ID3DBlob *compile_shader(const char *src, const char *target)
{
    HRESULT hr;
    ID3DBlob *code;
    ID3DBlob *err_msg;
    hr = D3DCompile(src, strlen(src), NULL, NULL, NULL, "Main", target, D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &err_msg);
    if (hr) {
        err_log("D3DCompile failed: %d\n", (int)hr);
        if (err_msg) {
            err_log("%s\n", (const char *)err_msg->lpVtbl->GetBufferPointer(err_msg));
            IUnknown_Release(err_msg);
        }
        return NULL;
    }
    if (err_msg)
        IUnknown_Release(err_msg);
    return code;
}

static ID3D11VertexShader *load_vs(ID3D11Device *dev, const char *src, ID3DBlob **compiled)
{
    ID3DBlob *code = compile_shader(src, "vs_4_0");
    if (!code) {
        return NULL;
    }

    ID3D11VertexShader *vs;
    HRESULT hr;
    hr = ID3D11Device_CreateVertexShader(dev, code->lpVtbl->GetBufferPointer(code), code->lpVtbl->GetBufferSize(code), NULL, &vs);
    if (hr) {
        err_log("CreateVertexShader failed: %d\n", (int)hr);
        IUnknown_Release(code);
        return NULL;
    }
    if (compiled)
        *compiled = code;
    else
        IUnknown_Release(code);
    return vs;
}

static ID3D11PixelShader *load_ps(ID3D11Device *dev, const char *src)
{
    ID3DBlob *code = compile_shader(src, "ps_4_0");
    if (!code) {
        return NULL;
    }

    ID3D11PixelShader *ps;
    HRESULT hr;
    hr = ID3D11Device_CreatePixelShader(dev, code->lpVtbl->GetBufferPointer(code), code->lpVtbl->GetBufferSize(code), NULL, &ps);
    if (hr) {
        err_log("CreatePixelShader failed: %d\n", (int)hr);
        IUnknown_Release(code);
        return NULL;
    }
    IUnknown_Release(code);
    return ps;
}

static int d3d11_texs_update(struct rp_buffer_ctx_t *ctx, int ctx_top_bot, int width, int height) {
    int i = ctx_top_bot;

    if (ctx->d3d_tex_dims[i].width == width && ctx->d3d_tex_dims[i].height == height) {
        return 0;
    }

    ctx->d3d_tex_dims[i].width = ctx->d3d_tex_dims[i].height = 0;

    CHECK_AND_RELEASE(ctx->d3d_tex_staging[i]);
    CHECK_AND_RELEASE(ctx->d3d_srv[i]);
    CHECK_AND_RELEASE(ctx->d3d_tex[i]);

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = D3D_FORMAT;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Usage = D3D11_USAGE_DYNAMIC;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tex_desc.MiscFlags = 0;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &ctx->d3d_tex[i]);
    if (hr) {
        err_log("CreateTexture2D failed: %d\n", (int)hr);
        return -1;
    }

    hr = ID3D11Device_CreateShaderResourceView(d3d11device[i], (ID3D11Resource *)ctx->d3d_tex[i], NULL, &ctx->d3d_srv[i]);
    if (hr) {
        err_log("CreateShaderResourceView failed: %d\n", (int)hr);
        return -1;
    }

    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.CPUAccessFlags = 0;
    hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &ctx->d3d_tex_staging[i]);
    if (hr) {
        err_log("CreateTexture2D failed: %d\n", (int)hr);
        return -1;
    }

    ctx->d3d_tex_dims[i].width = width;
    ctx->d3d_tex_dims[i].height = height;

    return 0;
}

enum blur_pass_t {
    BLUR_PASS_H,
    BLUR_PASS_V,
    BLUR_PASS_COUNT,
};

static struct d3d_blur_t {
    double weights[UI_BLUR_RADIUS_MAX];
    double offsets[UI_BLUR_RADIUS_MAX];
    int radius;

    ID3D11PixelShader *ps[BLUR_PASS_COUNT];
    struct {
        ID3D11Texture2D *tex;
        ID3D11ShaderResourceView *srv;
        ID3D11RenderTargetView *rtv;
    } tex[SCREEN_COUNT][BLUR_PASS_COUNT];
    struct {
        int width;
        int height;
    } dims[SCREEN_COUNT];
} d3d_blur[SCREEN_COUNT];

static bool d3d_blur_prog_make(int ctx_top_bot, enum blur_pass_t pass) {
    int b = pass;

    int i = ctx_top_bot;
    struct d3d_blur_t *blur = &d3d_blur[i];

    CHECK_AND_RELEASE(blur->ps[b]);

    char prog_fs_src[8192];
    char line[256];

    strcpy(prog_fs_src,
        "SamplerState my_samp: register(s0);\n"
        "Texture2D my_tex: register(t0);\n"
        "struct PSInput\n"
        "{\n"
        " float4 position: SV_Position;\n"
        " float2 uv: TEXCOORD;\n"
        "};\n"
        "struct PSOutput\n"
        "{\n"
        " float4 color: SV_Target0;\n"
        "};\n");

    sprintf(line, "static const float2 DIRECTION = %s;\n", b == BLUR_PASS_H ? "float2(1.0, 0.0)" : "float2(0.0, 1.0)");
    strcat(prog_fs_src, line);

    sprintf(line, "static const int SAMPLE_COUNT = %d;\n", blur->radius);
    strcat(prog_fs_src, line);

    strcat(prog_fs_src, "static const float OFFSETS[] = {\n");
    for (int i = 0; i < blur->radius; ++i) {
        if (i)
            strcat(prog_fs_src, ",\n");;
        sprintf(line, "%lf", blur->offsets[i]);
        strcat(prog_fs_src, line);
    }
    strcat(prog_fs_src, "};\n");

    strcat(prog_fs_src, "static const float WEIGHTS[] = {\n");
    for (int i = 0; i < blur->radius; ++i) {
        if (i)
            strcat(prog_fs_src, ",\n");;
        sprintf(line, "%lf", blur->weights[i]);
        strcat(prog_fs_src, line);
    }
    strcat(prog_fs_src, "};\n");

    strcat(prog_fs_src,
        "PSOutput Main(PSInput input)\n"
        "{\n"
        " float4 result = float4(0.0, 0.0, 0.0, 0.0);\n"
        " uint width, height;\n"
        " my_tex.GetDimensions(width, height);\n"
        " float2 size = DIRECTION / float2(width, height);\n"
        " for (int i = 0; i < SAMPLE_COUNT; ++i)\n"
        " {\n"
        "  float2 offset = OFFSETS[i] * size;\n"
        "  float weight = WEIGHTS[i];\n"
        "  result += my_tex.Sample(my_samp, input.uv + offset) * weight;\n"
        " }\n"
        " PSOutput output = (PSOutput)0;\n"
        " output.color = result;\n"
        " return output;\n"
        "}\n");

    blur->ps[b] = load_ps(d3d11device[i], prog_fs_src);
    return !!blur->ps[b];
}

static bool d3d_blur_tex_init(int screen_top_bot, int ctx_top_bot, int width, int height) {
    int i = ctx_top_bot;
    struct d3d_blur_t *blur = &d3d_blur[i];

    if (blur->dims[screen_top_bot].width == width && blur->dims[screen_top_bot].height == height)
        return true;

    blur->dims[screen_top_bot].width = 0;
    blur->dims[screen_top_bot].height = 0;
    for (int b = 0; b < BLUR_PASS_COUNT; ++b) {
        CHECK_AND_RELEASE(blur->tex[screen_top_bot][b].tex);
        CHECK_AND_RELEASE(blur->tex[screen_top_bot][b].srv);
        CHECK_AND_RELEASE(blur->tex[screen_top_bot][b].rtv);
    }

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = D3D_FORMAT;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    tex_desc.MiscFlags = 0;
    tex_desc.CPUAccessFlags = 0;

    for (int b = 0; b < BLUR_PASS_COUNT; ++b) {
        HRESULT hr;

        hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &blur->tex[screen_top_bot][b].tex);
        if (hr) {
            err_log("CreateTexture2D failed: %d\n", (int)hr);
            goto fail;
        }

        hr = ID3D11Device_CreateShaderResourceView(d3d11device[i], (ID3D11Resource *)blur->tex[screen_top_bot][b].tex, NULL, &blur->tex[screen_top_bot][b].srv);
        if (hr) {
            err_log("CreateShaderResourceView failed: %d\n", (int)hr);
            goto fail;
        }

        hr = ID3D11Device_CreateRenderTargetView(d3d11device[i], (ID3D11Resource *)blur->tex[screen_top_bot][b].tex, NULL, &blur->tex[screen_top_bot][b].rtv);
        if (hr) {
            err_log("CreateRenderTargetView failed: %d\n", (int)hr);
            goto fail;
        }
    }

    blur->dims[screen_top_bot].width = width;
    blur->dims[screen_top_bot].height = height;
    return true;

fail:
    return false;
}

static ID3D11ShaderResourceView *d3d_blur_tex(ID3D11ShaderResourceView *srv, int width, int height, int screen_top_bot, int ctx_top_bot) {
    int i = ctx_top_bot;
    struct d3d_blur_t *blur = &d3d_blur[i];

    if (!srv)
        goto end;

    const int radius_prev = calculate_blur_weights_and_offsets(blur->weights, blur->offsets);
    if (radius_prev != blur->radius) {
        for (int b = 0; b < BLUR_PASS_COUNT; ++b) {
            CHECK_AND_RELEASE(blur->ps[b]);
        }
        blur->radius = radius_prev;
    }

    d3d_blur_tex_init(screen_top_bot, i, width, height);
    for (int bb = 0; bb < ui_blur_iter + 1; ++bb) {
        for (int b = 0; b < BLUR_PASS_COUNT; ++b) {
            if (!blur->ps[b]) {
                if (!d3d_blur_prog_make(i, b))
                    return 0;
            }

            ID3D11DeviceContext_IASetPrimitiveTopology(d3d11device_context[i], D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11DeviceContext_IASetInputLayout(d3d11device_context[i], NULL);
            ID3D11DeviceContext_OMSetBlendState(d3d11device_context[i], d3d_ui_bs[i], NULL, 0xffffffff);
            ID3D11DeviceContext_PSSetShader(d3d11device_context[i], blur->ps[b], NULL, 0);
            ID3D11DeviceContext_PSSetSamplers(d3d11device_context[i], 0, 1, &d3d_ss_linear[i]);
            ID3D11DeviceContext_RSSetState(d3d11device_context[i], d3d_rs[i]);
            ID3D11DeviceContext_PSSetShaderResources(d3d11device_context[i], 0, 1,
                b ? &blur->tex[screen_top_bot][b - 1].srv :
                bb ? &blur->tex[screen_top_bot][BLUR_PASS_COUNT - 1].srv :
                &srv);
            ID3D11DeviceContext_OMSetRenderTargets(d3d11device_context[i], 1, &blur->tex[screen_top_bot][b].rtv, NULL);
            D3D11_VIEWPORT vp = { .Width = width, .Height = height };
            ID3D11DeviceContext_RSSetViewports(d3d11device_context[i], 1, &vp);
            ID3D11DeviceContext_VSSetShader(d3d11device_context[i], d3d_vs[i], NULL, 0);
            ID3D11DeviceContext_Draw(d3d11device_context[i], 3, 0);
            ID3D11ShaderResourceView *srv_null = NULL;
            ID3D11DeviceContext_PSSetShaderResources(d3d11device_context[i], 0, 1, &srv_null);
            ID3D11RenderTargetView *rtv_null = NULL;
            ID3D11DeviceContext_OMSetRenderTargets(d3d11device_context[i], 1, &rtv_null, NULL);
        }
    }

end:
    return blur->tex[screen_top_bot][BLUR_PASS_COUNT - 1].srv;
}

static void d3d_blur_tex_close(int ctx_top_bot) {
    struct d3d_blur_t *blur = &d3d_blur[ctx_top_bot];
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        for (int b = 0; b < BLUR_PASS_COUNT; ++b) {
            CHECK_AND_RELEASE(blur->tex[i][b].tex);
            CHECK_AND_RELEASE(blur->tex[i][b].rtv);
            CHECK_AND_RELEASE(blur->tex[i][b].srv);
        }
    }
    for (int b = 0; b < BLUR_PASS_COUNT; ++b)
        CHECK_AND_RELEASE(blur->ps[b]);
    memset(blur, 0, sizeof(*blur));
}

static int d3d11_init(void) {
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        HRESULT hr;

        d3d_vs[j] = load_vs(d3d11device[j], d3d_vs_src, NULL);
        if (!d3d_vs[j]) {
            return -1;
        }
        d3d_data_vs[j] = load_vs(d3d11device[j], d3d_data_vs_src, NULL);
        if (!d3d_data_vs[j]) {
            return -1;
        }
        d3d_ps[j] = load_ps(d3d11device[j], d3d_ps_src);
        if (!d3d_ps[j]) {
            return -1;
        }
        if (j == SCREEN_TOP) {
            d3d_ui_ps = load_ps(d3d11device[j], d3d_ui_ps_src);
            if (!d3d_ui_ps) {
                return -1;
            }
        }

        D3D11_BLEND_DESC blend_desc = {
            .RenderTarget = {
                {
                    .BlendEnable = FALSE,
                    .SrcBlend = D3D11_BLEND_ONE,
                    .DestBlend = D3D11_BLEND_ZERO,
                    .BlendOp = D3D11_BLEND_OP_ADD,
                    .SrcBlendAlpha = D3D11_BLEND_ONE,
                    .DestBlendAlpha = D3D11_BLEND_ZERO,
                    .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                    .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
                }}};
        hr = ID3D11Device_CreateBlendState(d3d11device[j], &blend_desc, &d3d_ui_bs[j]);
        if (hr) {
            err_log("CreateBlendState failed: %d\n", (int)hr);
            return -1;
        }

        D3D11_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        hr = ID3D11Device_CreateSamplerState(d3d11device[j], &sampler_desc, &d3d_ss_point[j]);
        if (hr) {
            err_log("CreateSamplerState failed: %d\n", (int)hr);
            return -1;
        }

        sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        hr = ID3D11Device_CreateSamplerState(d3d11device[j], &sampler_desc, &d3d_ss_linear[j]);
        if (hr) {
            err_log("CreateSamplerState failed: %d\n", (int)hr);
            return -1;
        }

        D3D11_RASTERIZER_DESC rast_desc = {};
        rast_desc.FillMode = D3D11_FILL_SOLID;
        rast_desc.CullMode = D3D11_CULL_NONE;
        hr = ID3D11Device_CreateRasterizerState(d3d11device[j], &rast_desc, &d3d_rs[j]);
        if (hr) {
            err_log("CreateRasterizerState failed: %d\n", (int)hr);
            return -1;
        }

        rast_desc.ScissorEnable = TRUE;
        hr = ID3D11Device_CreateRasterizerState(d3d11device[j], &rast_desc, &d3d_scissor_rs[j]);
        if (hr) {
            err_log("CreateRasterizerState failed: %d\n", (int)hr);
            return -1;
        }
    }

    return 0;
}

static void d3d11_close(void)
{
    d3d11_ui_close();

    for (int j = 0; j < SCREEN_COUNT; ++j) {
        d3d_blur_tex_close(j);

        for (int i = 0; i < SCREEN_COUNT; ++i) {
            CHECK_AND_RELEASE(rp_buffer_ctx[j].d3d_rtv_upscaled[i]);
            CHECK_AND_RELEASE(rp_buffer_ctx[j].d3d_srv_upscaled[i]);
            CHECK_AND_RELEASE(rp_buffer_ctx[j].d3d_tex_upscaled[i]);
            CHECK_AND_RELEASE(rp_buffer_ctx[j].d3d_tex_staging[i]);
            CHECK_AND_RELEASE(rp_buffer_ctx[j].d3d_srv[i]);
            CHECK_AND_RELEASE(rp_buffer_ctx[j].d3d_tex[i]);
        }

        CHECK_AND_RELEASE(d3d_ui_bs[j]);
        CHECK_AND_RELEASE(d3d_scissor_rs[j]);
        CHECK_AND_RELEASE(d3d_rs[j]);
        CHECK_AND_RELEASE(d3d_ss_point[j]);
        CHECK_AND_RELEASE(d3d_ss_linear[j]);
        CHECK_AND_RELEASE(d3d_vs[j]);
        CHECK_AND_RELEASE(d3d_data_vs[j]);
        CHECK_AND_RELEASE(d3d_ps[j]);
        if (j == SCREEN_TOP) {
            CHECK_AND_RELEASE(d3d_ui_ps);
        }
    }
}

static int d3d11_renderer_init(void) {
    if (dxgi_init())
        return -1;

    if (is_renderer_csc()) {
        if (composition_swapchain_init(ui_hwnd)) {
            return -1;
        }
        ui_compositing = 1;
    } else {
        HRESULT hr;
        for (int i = 0; i < SCREEN_COUNT; ++i) {
            D3D_FEATURE_LEVEL featureLevelSupported;
            hr = D3D11CreateDevice(
                (IDXGIAdapter *)dxgi_adapter,
                dxgi_adapter ? 0 : D3D_DRIVER_TYPE_HARDWARE,
                NULL,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                NULL,
                0,
                D3D11_SDK_VERSION,
                &d3d11device[i],
                &featureLevelSupported,
                &d3d11device_context[i]);
            if (hr) {
                err_log("D3D11CreateDevice failed: %d\n", (int)hr);
                return -1;
            }

            DXGI_SWAP_CHAIN_DESC sc_desc = {};
            sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sc_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sc_desc.SampleDesc.Count = 1;
            sc_desc.BufferCount = COMPAT_PRESENATTION_BUFFER_COUNT_PER_SCREEN;
            sc_desc.OutputWindow = ui_hwnd[i];
            sc_desc.Windowed = TRUE;
            sc_desc.SwapEffect = IsWindows10OrGreater() ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

            hr = IDXGIFactory2_CreateSwapChain(dxgi_factory, (IUnknown *)d3d11device[i], &sc_desc, &dxgi_sc[i]);
            if (hr) {
                err_log("CreateSwapChain failed: %d\n", (int)hr);
                return -1;
            }
        }
    }

    nk_ctx = nk_d3d11_init(d3d11device[SCREEN_TOP], 1, 1, MAX_VERTEX_BUFFER, MAX_INDEX_BUFFER);
    if (!nk_ctx)
        return -1;

    if (d3d11_init()) {
        return -1;
    }

    if (d3d11_upscaling_init())
        return -1;

    err_log("d3d11 %s\n", is_renderer_csc() ? "composition swapchain" : "");

    return 0;
}

static void d3d11_renderer_destroy(void) {
    d3d11_upscaling_close();

    d3d11_close();

    if (nk_ctx) {
        nk_d3d11_shutdown();
        nk_ctx = NULL;
    }

    if (is_renderer_csc()) {
        ui_compositing = 0;
        composition_swapchain_close();
    } else {
        for (int i = 0; i < SCREEN_COUNT; ++i) {
            CHECK_AND_RELEASE(dxgi_sc[i]);
            CHECK_AND_RELEASE(d3d11device_context[i]);
            CHECK_AND_RELEASE(d3d11device[i]);
        }
    }

    dxgi_close();
}

int ui_renderer_d3d11_init(void) {
    if (sdl_win_init(sdl_win, 0)) {
        return -1;
    }

    for (int i = 0; i < SCREEN_COUNT; ++i)
        ui_sdl_win[i] = sdl_win[i];

    sdl_set_wminfo();

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        ui_win_width_drawable[i] = 1;
        ui_win_height_drawable[i] = 1;
        ui_win_scale[i] = 1.0f;
    }

    if (d3d11_renderer_init()) {
        return -1;
    }

    ui_nk_ctx = nk_ctx;

    return 0;
}

void ui_renderer_d3d11_destroy(void) {
    ui_nk_ctx = NULL;

    d3d11_renderer_destroy();

    sdl_reset_wminfo();

    for (int i = 0; i < SCREEN_COUNT; ++i)
        ui_sdl_win[i] = NULL;

    sdl_win_destroy(sdl_win);
}

static struct ID3D11RenderTargetView *d3d_rtv[SCREEN_COUNT]; // Non-owning

static ID3D11Texture2D *sc_tex[SCREEN_COUNT];
static ID3D11RenderTargetView *sc_rtv[SCREEN_COUNT];

static struct presentation_buffer_t *d3d_pres_buf[SCREEN_COUNT];

void ui_renderer_d3d11_main(int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, bool win_shared, float bg[GL_CHANNELS_N]) {
    int i = ctx_top_bot;
    HRESULT hr;

    int p = win_shared ? screen_top_bot : i;
    sc_fail[p] = 0;

    if (is_renderer_csc()) {
        ui_compositor_csc_main(screen_top_bot, i, win_shared);
        if (sc_fail[p]) {
            return;
        }

        struct presentation_buffer_t *bufs = presentation_buffers[i][screen_top_bot];
        int index_sc;
        if (presentation_buffer_get(bufs, p, win_shared, COMPAT_PRESENATTION_BUFFER_COUNT_PER_SCREEN, ui_ctx_width_drawable[p], ui_ctx_height_drawable[p], &index_sc) != 0) {
            sc_fail[p] = 1;
            return;
        }
        d3d_pres_buf[p] = &bufs[index_sc];
        d3d_rtv[p] = d3d_pres_buf[p]->rtv;
    } else {
        if (i == SCREEN_TOP)
            rp_lock_wait(comp_lock);

        if (ui_ctx_width_drawable[i] != ui_win_width_drawable[i] || ui_ctx_height_drawable[i] != ui_win_height_drawable[i]) {
            ui_ctx_width_drawable[i] = ui_win_width_drawable[i];
            ui_ctx_height_drawable[i] = ui_win_height_drawable[i];
            hr = IDXGISwapChain_ResizeBuffers(dxgi_sc[i], 0, 0, 0, 0, 0);
            if (hr) {
                err_log("ResizeBuffers failed: %d\n", (int)hr);
                sc_fail[p] = 1;
                return;
            }

            if (i == SCREEN_TOP) {
                if (nk_d3d11_resize(d3d11device_context[i], ui_ctx_width_drawable[i], ui_ctx_height_drawable[i], ui_win_scale[i])) {
                    err_log("nk_d3d11_resize failed\n");
                    sc_fail[p] = 1;
                    return;
                }
            }
        }

        hr = IDXGISwapChain_GetBuffer(dxgi_sc[i], 0, &IID_ID3D11Texture2D, (void **)&sc_tex[i]);
        if (hr) {
            err_log("GetBuffer failed: %d\n", (int)hr);
            sc_fail[p] = 1;
            return;
        }

        hr = ID3D11Device_CreateRenderTargetView(d3d11device[i], (ID3D11Resource *)sc_tex[i], NULL, &sc_rtv[i]);
        if (hr) {
            err_log("CreateRenderTargetView failed: %d\n", (int)hr);
            IUnknown_Release(sc_tex[i]);
            sc_fail[p] = 1;
            return;
        }

        d3d_rtv[i] = sc_rtv[i];
    }

    ID3D11DeviceContext_ClearRenderTargetView(d3d11device_context[i], d3d_rtv[p], bg);
    if (view_mode == VIEW_MODE_TOP_BOT && !win_shared) {
        draw_screen(&rp_buffer_ctx[SCREEN_TOP], SCREEN_HEIGHT0, SCREEN_WIDTH, SCREEN_TOP, i, view_mode, 0);
        draw_screen(&rp_buffer_ctx[SCREEN_BOT], SCREEN_HEIGHT1, SCREEN_WIDTH, SCREEN_BOT, i, view_mode, 0);
    } else if (view_mode == VIEW_MODE_BOT) {
        draw_screen(&rp_buffer_ctx[SCREEN_BOT], SCREEN_HEIGHT1, SCREEN_WIDTH, SCREEN_BOT, i, view_mode, win_shared);
    } else {
        if (!draw_screen(&rp_buffer_ctx[screen_top_bot], screen_top_bot == SCREEN_TOP ? SCREEN_HEIGHT0 : SCREEN_HEIGHT1, SCREEN_WIDTH, screen_top_bot, i, view_mode, win_shared)) {
            ID3D11DeviceContext_ClearRenderTargetView(d3d11device_context[i], d3d_rtv[p], bg);
        }
    }
}

static int placebo_upscaling_update(int selected, int ctx_top_bot, int screen_top_bot) {
    int i = ctx_top_bot;

    int render_mode = -1;
    bool reset_mode = 0;
    if (selected >= 0) {
        render_mode = selected;
    } else {
        reset_mode = 1;
    }

    if (
        placebo_render[i][screen_top_bot] && (
            placebo_render_mode[i][screen_top_bot] != render_mode ||
            reset_mode
        )
    ) {
        placebo_render_close(placebo_render[i][screen_top_bot]);
        placebo_render[i][screen_top_bot] = 0;
    }

    if (!reset_mode && !placebo_render[i][screen_top_bot] && render_mode >= 0 && placebo) {
        placebo_render[i][screen_top_bot] = placebo_render_init(placebo, render_mode, pl_d3d11_dev[i]->gpu, pl_log_dev);
        if (!placebo_render[i][screen_top_bot]) {
            err_log("placebo_render_init failed\n");
            goto fail;
        }

        placebo_render_mode[i][screen_top_bot] = render_mode;
    }

fail:
    return reset_mode;
}

static void *d3d11_filter_chain_create(libra_shader_preset_t *preset, void *dev) {
    struct filter_chain_d3d11_opt_t opt = {
        .version = libra_instance_api_version(),
    };
    libra_d3d11_filter_chain_t out;
    libra_error_t err = libra_d3d11_filter_chain_create(preset, dev, &opt, &out);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
        return NULL;
    }
    return out;
}

static void d3d11_filter_chain_free(void *fc, void *) {
    libra_error_t err = libra_d3d11_filter_chain_free((libra_d3d11_filter_chain_t *)fc);
    if (err) {
        libra_error_print(err);
        libra_error_free(&err);
    }
}

static int rashader_upscaling_update(int selected, int ctx_top_bot, int screen_top_bot) {
    int i = ctx_top_bot;

    int render_mode = -1;
    bool reset_mode = 0;
    if (selected >= 0) {
        render_mode = selected;
    } else {
        reset_mode = 1;
    }

    if (
        rashader_render[i][screen_top_bot] && (
            rashader_render_mode[i][screen_top_bot] != render_mode ||
            reset_mode
        )
    ) {
        rashader_render_close(rashader_render[i][screen_top_bot], d3d11_filter_chain_free, NULL);
        rashader_render[i][screen_top_bot] = 0;
    }

    static libra_preset_ctx_t ctx = 0;
    if (!reset_mode && !rashader_render[i][screen_top_bot] && render_mode >= 0) {
        libra_error_t err = libra_preset_ctx_create(&ctx);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            ctx = 0;
            goto fail;
        }
        err = libra_preset_ctx_set_runtime(&ctx, LIBRA_PRESET_CTX_RUNTIME_D3D11);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            goto fail;
        }

        rashader_render[i][screen_top_bot] = rashader_render_init(rashader, render_mode, &ctx, d3d11_filter_chain_create, d3d11device[i], (PFN_filter_chain_set_param)libra_d3d11_filter_chain_set_param);
        if (!rashader_render[i][screen_top_bot]) {
            err_log("rashader_render_init failed\n");
            goto fail;
        }

        rashader_render_mode[i][screen_top_bot] = render_mode;
    }

fail:
    if (ctx)
        libra_preset_ctx_free(&ctx);
    return reset_mode;
}

static int ctx_width[SCREEN_COUNT];
static int ctx_height[SCREEN_COUNT];
static int win_width_drawable[SCREEN_COUNT];
static int win_height_drawable[SCREEN_COUNT];

static void d3d11_draw_screen(int ctx_top_bot, ID3D11ShaderResourceView *in_srv)
{
    int i = ctx_top_bot;

    ID3D11DeviceContext_IASetPrimitiveTopology(d3d11device_context[i], D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetInputLayout(d3d11device_context[i], NULL);
    ID3D11DeviceContext_OMSetBlendState(d3d11device_context[i], d3d_ui_bs[i], NULL, 0xffffffff);
    ID3D11DeviceContext_PSSetShader(d3d11device_context[i], d3d_ps[i], NULL, 0);
    ID3D11DeviceContext_PSSetSamplers(d3d11device_context[i], 0, 1, &d3d_ss_linear[i]);
    ID3D11DeviceContext_PSSetShaderResources(d3d11device_context[i], 0, 1, &in_srv);
    ID3D11DeviceContext_Draw(d3d11device_context[i], 3, 0);
    ID3D11ShaderResourceView *ptr_null = NULL;
    ID3D11DeviceContext_PSSetShaderResources(d3d11device_context[i], 0, 1, &ptr_null);
}

static bool d3d11_recreate_upscaled_resource(struct rp_buffer_ctx_t *ctx, int ctx_top_bot, int ctx_width, int ctx_height) {
    int i = ctx_top_bot;

    HRESULT hr;

    if (ctx->width_upscaled[i] != ctx_height || ctx->height_upscaled[i] != ctx_width || !ctx->d3d_srv_upscaled_prev[i]) {
        CHECK_AND_RELEASE(ctx->d3d_rtv_upscaled[i]);
        CHECK_AND_RELEASE(ctx->d3d_srv_upscaled[i]);
        CHECK_AND_RELEASE(ctx->d3d_tex_upscaled[i]);

        D3D11_TEXTURE2D_DESC tex_desc = {};
        tex_desc.Width = ctx_height;
        tex_desc.Height = ctx_width;
        tex_desc.MipLevels = 1;
        tex_desc.ArraySize = 1;
        tex_desc.Format = D3D_FORMAT;
        tex_desc.SampleDesc.Count = 1;
        tex_desc.SampleDesc.Quality = 0;
        tex_desc.Usage = D3D11_USAGE_DEFAULT;
        tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        tex_desc.MiscFlags = 0;
        tex_desc.CPUAccessFlags = 0;

        hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &ctx->d3d_tex_upscaled[i]);
        if (hr) {
            err_log("CreateTexture2D failed: %d\n", (int)hr);
            goto fail;
        }

        hr = ID3D11Device_CreateShaderResourceView(d3d11device[i], (ID3D11Resource *)ctx->d3d_tex_upscaled[i], NULL, &ctx->d3d_srv_upscaled[i]);
        if (hr) {
            err_log("CreateShaderResourceView failed: %d\n", (int)hr);
            goto fail;
        }

        hr = ID3D11Device_CreateRenderTargetView(d3d11device[i], (ID3D11Resource *)ctx->d3d_tex_upscaled[i], NULL, &ctx->d3d_rtv_upscaled[i]);
        if (hr) {
            err_log("CreateRenderTargetView failed: %d\n", (int)hr);
            goto fail;
        }

        ctx->width_upscaled[i] = ctx_height;
        ctx->height_upscaled[i] = ctx_width;
    }

    return true;

fail:
    return false;
}

static void d3d_blur_tex_draw(ID3D11ShaderResourceView *srv, int ctx_top_bot, ID3D11RenderTargetView *rtv,
    int left, int top, int width, int height,
    int ctx_left, int ctx_top, int ctx_width, int ctx_height)
{
    int i = ctx_top_bot;

    if (!ui_blur_iter)
        return;

    ID3D11DeviceContext_OMSetRenderTargets(d3d11device_context[i], 1, &rtv, NULL);
    D3D11_VIEWPORT vp = { .TopLeftX = left, .TopLeftY = top, .Width = width, .Height = height };
    ID3D11DeviceContext_RSSetViewports(d3d11device_context[i], 1, &vp);
    ID3D11DeviceContext_VSSetShader(d3d11device_context[i], d3d_data_vs[i], NULL, 0);
    ID3D11DeviceContext_RSSetState(d3d11device_context[i], d3d_scissor_rs[i]);
    D3D11_RECT rect = { .left = ctx_left, .top = ctx_top, .right = ctx_left + ctx_width, .bottom = ctx_top + ctx_height };
    ID3D11DeviceContext_RSSetScissorRects(d3d11device_context[i], 1, &rect);

    d3d11_draw_screen(i, srv);

    ID3D11DeviceContext_RSSetScissorRects(d3d11device_context[i], 0, NULL);
}

void ui_renderer_d3d11_draw(struct rp_buffer_ctx_t *ctx, uint8_t *data, int width, int height, int screen_top_bot, int ctx_top_bot, view_mode_t view_mode, int win_shared) {
    int i = ctx_top_bot;

    int ctx_left;
    int ctx_top;
    if (win_shared)
        draw_screen_get_dims_win_shared(screen_top_bot, i, width, height, &ctx_left, &ctx_top, &ctx_width[screen_top_bot], &ctx_height[screen_top_bot]);
    else
        draw_screen_get_dims_lite(screen_top_bot, i, view_mode, width, height, &ctx_left, &ctx_top, &ctx_width[screen_top_bot], &ctx_height[screen_top_bot]);
    ctx_left *= ui_win_scale[i];
    ctx_top *= ui_win_scale[i];
    ctx_width[screen_top_bot] *= ui_win_scale[i];
    ctx_height[screen_top_bot] *= ui_win_scale[i];

    win_width_drawable[screen_top_bot] = ui_win_width_drawable[i];
    win_height_drawable[screen_top_bot] = ui_win_height_drawable[i];

    int p = win_shared ? screen_top_bot : i;

    if (d3d11_texs_update(ctx, i, height, width) != 0) {
        return;
    }

    int upscaling_selected = ui_upscaling_selected;
    bool upscaled = upscaling_selected != UPSCALING_DEFAULT_NONE;

    bool need_tex_update = ctx->upscaling_selected_prev != upscaling_selected ||
        ctx->width_prev != ctx_width[screen_top_bot] || ctx->height_prev != ctx_height[screen_top_bot] ||
        ctx->win_width_prev != win_width_drawable[screen_top_bot] || ctx->win_height_prev != win_height_drawable[screen_top_bot] ||
        ctx->view_mode_prev != view_mode;

    int blur_left;
    int blur_top;
    int blur_width;
    int blur_height;
    int blur_ctx_left;
    int blur_ctx_top;
    int blur_ctx_width;
    int blur_ctx_height;
    draw_screen_get_blur_dims_win_shared(win_shared ? screen_top_bot : screen_top_bot, i, view_mode, win_shared, width, height,
        &blur_left, &blur_top, &blur_width, &blur_height, &blur_ctx_left, &blur_ctx_top, &blur_ctx_width, &blur_ctx_height);

    ID3D11ShaderResourceView *srv = ctx->d3d_srv[i];
    if (!data) {
        if (upscaled) {
            if (need_tex_update || !ctx->d3d_srv_upscaled_prev[i]) {
                data = ctx->data_prev;
            } else {
                srv = ctx->d3d_srv_upscaled_prev[i];
            }
        } else {
            ctx->d3d_srv_upscaled_prev[i] = 0;
        }
    }

    if (data) {
        D3D11_MAPPED_SUBRESOURCE tex_mapped = {};
        HRESULT hr = ID3D11DeviceContext_Map(d3d11device_context[i], (ID3D11Resource *)ctx->d3d_tex[i], 0, D3D11_MAP_WRITE_DISCARD, 0, &tex_mapped);
        if (hr) {
            err_log("Map failed: %d", (int)hr);
            return;
        }
        for (int i = 0; i < width; ++i) {
            memcpy(tex_mapped.pData + i * tex_mapped.RowPitch, data + i * height * GL_CHANNELS_N, height * GL_CHANNELS_N);
        }

        ID3D11DeviceContext_Unmap(d3d11device_context[i], (ID3D11Resource *)ctx->d3d_tex[i], 0);

        d3d_blur_tex_draw(d3d_blur_tex(srv, height, width, screen_top_bot, i), i,
            d3d_rtv[is_renderer_csc() ? p : i],
            blur_left, blur_top, blur_width, blur_height,
            blur_ctx_left, blur_ctx_top, blur_ctx_width, blur_ctx_height);

        if (!IS_PLACEBO(upscaling_selected)) {
            placebo_upscaling_update(-1, i, screen_top_bot);
        }

        if (!IS_RASHADER(upscaling_selected)) {
            rashader_upscaling_update(-1, i, screen_top_bot);
        }

        pl_tex in_tex = NULL;
        pl_tex out_tex = NULL;
        if (IS_PLACEBO(upscaling_selected)) {
            int reset_mode = placebo_upscaling_update(PLACEBO_MODE(upscaling_selected), i, screen_top_bot);
            if (placebo_render[i][screen_top_bot]) {
                ID3D11DeviceContext_CopyResource(d3d11device_context[i], (ID3D11Resource *)ctx->d3d_tex_staging[i], (ID3D11Resource *)ctx->d3d_tex[i]);

                struct pl_d3d11_wrap_params in_tex_pars = { .tex = (ID3D11Resource *)ctx->d3d_tex_staging[i] };
                in_tex = pl_d3d11_wrap(pl_d3d11_dev[i]->gpu, &in_tex_pars);
                if (!in_tex) {
                    goto placebo_fail;
                }

                if (!d3d11_recreate_upscaled_resource(ctx, i, ctx_width[screen_top_bot], ctx_height[screen_top_bot])) {
                    goto placebo_fail;
                }

                struct pl_d3d11_wrap_params out_tex_pars = { .tex = (ID3D11Resource *)ctx->d3d_tex_upscaled[i] };
                out_tex = pl_d3d11_wrap(pl_d3d11_dev[i]->gpu, &out_tex_pars);
                if (!out_tex) {
                    goto placebo_fail;
                }
                bool ret = placebo_render_run(placebo_render[i][screen_top_bot], in_tex, out_tex, 0, 0) != NULL;
                if (!ret) {
                    goto placebo_fail;
                }
                srv = ctx->d3d_srv_upscaled_prev[i] = ctx->d3d_srv_upscaled[i];
            } else if (!reset_mode) {
placebo_fail:
                err_log("placebo render failed\n");

                CHECK_AND_RELEASE(ctx->d3d_rtv_upscaled[i]);
                CHECK_AND_RELEASE(ctx->d3d_srv_upscaled[i]);
                CHECK_AND_RELEASE(ctx->d3d_tex_upscaled[i]);

                ui_upscaling_selected = UPSCALING_DEFAULT_NONE;
            }
        }
        if (in_tex)
            pl_tex_destroy(pl_d3d11_dev[i]->gpu, &in_tex);
        if (out_tex)
            pl_tex_destroy(pl_d3d11_dev[i]->gpu, &out_tex);

        if (IS_RASHADER(upscaling_selected)) {
            int reset_mode = rashader_upscaling_update(RASHADER_MODE(upscaling_selected), i, screen_top_bot);
            if (rashader_render[i][screen_top_bot]) {
                libra_d3d11_filter_chain_t *chain = rashader_render_chain(rashader_render[i][screen_top_bot]);

                if (!d3d11_recreate_upscaled_resource(ctx, i, ctx_width[screen_top_bot], ctx_height[screen_top_bot])) {
                    goto rashader_fail;
                }

                libra_error_t err = libra_d3d11_filter_chain_frame(chain, d3d11device_context[i], 1, ctx->d3d_srv[i], ctx->d3d_rtv_upscaled[i], NULL, NULL, NULL);
                if (err) {
                    libra_error_print(err);
                    libra_error_free(&err);
                    goto rashader_fail;
                }
                srv = ctx->d3d_srv_upscaled_prev[i] = ctx->d3d_srv_upscaled[i];
            } else if (!reset_mode) {
rashader_fail:
                err_log("rashader render failed\n");

                CHECK_AND_RELEASE(ctx->d3d_rtv_upscaled[i]);
                CHECK_AND_RELEASE(ctx->d3d_srv_upscaled[i]);
                CHECK_AND_RELEASE(ctx->d3d_tex_upscaled[i]);

                ui_upscaling_selected = UPSCALING_DEFAULT_NONE;
            }
        }

        if (ui_upscaling_selected == UPSCALING_DEFAULT_NONE)
            ctx->d3d_srv_upscaled_prev[i] = 0;
    } else { // !data
        d3d_blur_tex_draw(d3d_blur_tex(NULL, height, width, screen_top_bot, i), i,
            d3d_rtv[is_renderer_csc() ? p : i],
            blur_left, blur_top, blur_width, blur_height,
            blur_ctx_left, blur_ctx_top, blur_ctx_width, blur_ctx_height);
    }

    ID3D11DeviceContext_OMSetRenderTargets(d3d11device_context[i], 1, &d3d_rtv[is_renderer_csc() ? p : i], NULL);
    D3D11_VIEWPORT vp = { .TopLeftX = ctx_left, .TopLeftY = ctx_top, .Width = ctx_width[screen_top_bot], .Height = ctx_height[screen_top_bot] };
    ID3D11DeviceContext_RSSetViewports(d3d11device_context[i], 1, &vp);
    ID3D11DeviceContext_VSSetShader(d3d11device_context[i], d3d_data_vs[i], NULL, 0);
    ID3D11DeviceContext_RSSetState(d3d11device_context[i], d3d_rs[i]);

    d3d11_draw_screen(i, srv);

    ctx->width_prev = ctx_width[screen_top_bot];
    ctx->height_prev = ctx_height[screen_top_bot];
    ctx->win_width_prev = win_width_drawable[screen_top_bot];
    ctx->win_height_prev = win_height_drawable[screen_top_bot];
    ctx->view_mode_prev = view_mode;
    ctx->upscaling_selected_prev = upscaling_selected;
}

static ID3D11Texture2D *d3d_ui_tex;
static ID3D11RenderTargetView *d3d_ui_rtv;
static ID3D11ShaderResourceView *d3d_ui_srv;

int d3d11_ui_init(void)
{
    int i = SCREEN_TOP;

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = ui_win_width_drawable[i];
    tex_desc.Height = ui_win_height_drawable[i];
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    tex_desc.MiscFlags = 0;
    tex_desc.CPUAccessFlags = 0;

    HRESULT hr;
    hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &d3d_ui_tex);
    if (hr) {
        err_log("CreateTexture2D failed: %d\n", (int)hr);
        return -1;
    } else {
        hr = ID3D11Device_CreateRenderTargetView(d3d11device[i], (ID3D11Resource *)d3d_ui_tex, NULL, &d3d_ui_rtv);
        if (hr) {
            err_log("CreateRenderTargetView failed: %d\n", (int)hr);
            CHECK_AND_RELEASE(d3d_ui_tex);
            return -1;
        } else {
            hr = ID3D11Device_CreateShaderResourceView(d3d11device[i], (ID3D11Resource *)d3d_ui_tex, NULL, &d3d_ui_srv);
            if (hr) {
                err_log("CreateShaderResourceView failed: %d\n", (int)hr);
                CHECK_AND_RELEASE(d3d_ui_rtv);
                CHECK_AND_RELEASE(d3d_ui_tex);
                return -1;
            } else {
                if (nk_d3d11_resize(d3d11device_context[i], ui_win_width_drawable[i], ui_win_height_drawable[i], ui_win_scale[i])) {
                    err_log("nk_d3d11_resize failed\n");
                    CHECK_AND_RELEASE(d3d_ui_srv);
                    CHECK_AND_RELEASE(d3d_ui_rtv);
                    CHECK_AND_RELEASE(d3d_ui_tex);
                    return -1;
                }
            }
        }
    }

    return 0;
}

void d3d11_ui_close(void)
{
    CHECK_AND_RELEASE(d3d_ui_srv);
    CHECK_AND_RELEASE(d3d_ui_rtv);
    CHECK_AND_RELEASE(d3d_ui_tex);
}

void ui_renderer_d3d11_present(int screen_top_bot, int ctx_top_bot, bool win_shared) {
    int i = ctx_top_bot;
    int p = win_shared ? screen_top_bot : i;

    if (is_renderer_csc()) {
        if (!sc_fail[p]) {
            if (p == SCREEN_TOP) {
                int width = ui_win_width_drawable_prev[p];
                int height = ui_win_height_drawable_prev[p];
                struct presentation_buffer_t *bufs = ui_pres_bufs;
                int j = SURFACE_UTIL_UI;
                int index_sc;
                if (presentation_buffer_get(bufs, j, -1, COMPAT_PRESENATTION_BUFFER_COUNT_PER_SCREEN, width, height, &index_sc) != 0) {
                    sc_fail[p] = 1;
                    goto fail;
                }
                struct presentation_buffer_t *buf = &bufs[index_sc];
                ID3D11DeviceContext_OMSetRenderTargets(d3d11device_context[i], 1, &d3d_ui_rtv, NULL);
                float clearColor[GL_CHANNELS_N] = {};
                ID3D11DeviceContext_ClearRenderTargetView(d3d11device_context[i], d3d_ui_rtv, clearColor);
                nk_d3d11_render(d3d11device_context[i], NK_ANTI_ALIASING_OFF, ui_win_scale[i]);
                nk_gui_next = 0;

                ID3D11DeviceContext_IASetPrimitiveTopology(d3d11device_context[i], D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                ID3D11DeviceContext_IASetInputLayout(d3d11device_context[i], NULL);
                ID3D11DeviceContext_OMSetRenderTargets(d3d11device_context[i], 1, &buf->rtv, NULL);
                ID3D11DeviceContext_OMSetBlendState(d3d11device_context[i], d3d_ui_bs[i], NULL, 0xffffffff);
                ID3D11DeviceContext_VSSetShader(d3d11device_context[i], d3d_vs[i], NULL, 0);
                ID3D11DeviceContext_PSSetShader(d3d11device_context[i], d3d_ui_ps, NULL, 0);
                ID3D11DeviceContext_PSSetShaderResources(d3d11device_context[i], 0, 1, &d3d_ui_srv);
                ID3D11DeviceContext_PSSetSamplers(d3d11device_context[i], 0, 1, &d3d_ss_point[i]);

                D3D11_VIEWPORT vp = {.Width = width, .Height = height};
                ID3D11DeviceContext_RSSetViewports(d3d11device_context[i], 1, &vp);
                ID3D11DeviceContext_RSSetState(d3d11device_context[i], d3d_rs[i]);
                ID3D11DeviceContext_Draw(d3d11device_context[i], 3, 0);

                ID3D11DeviceContext_OMSetRenderTargets(d3d11device_context[i], 0, NULL, NULL);
                ID3D11ShaderResourceView *ptr_null = NULL;
                ID3D11DeviceContext_PSSetShaderResources(d3d11device_context[i], 0, 1, &ptr_null);

                if (update_hide_ui()) {
                    sc_fail[p] = 1;
                    goto fail;
                }

                if (!ui_hide_nk_windows && ui_buffer_present(buf, width, height)) {
                    sc_fail[p] = 1;
                    goto fail;
                }
            }
            if (presentation_buffer_present(d3d_pres_buf[p], i, screen_top_bot, win_shared, ui_ctx_width_drawable[p], ui_ctx_height_drawable[p])) {
            }
        }
fail:
        ui_compositor_csc_present(i);
    } else {
        if (!sc_fail[p]) {
            HRESULT hr;

            if (p == SCREEN_TOP) {
                ID3D11DeviceContext_OMSetRenderTargets(d3d11device_context[i], 1, &d3d_rtv[i], NULL);
                nk_d3d11_render(d3d11device_context[i], NK_ANTI_ALIASING_OFF, ui_win_scale[i]);
                nk_gui_next = 0;
            }
            hr = IDXGISwapChain_Present(dxgi_sc[i], 1, 0);
            if (hr) {
                err_log("Present failed: %d\n", (int)hr);
            }

            IUnknown_Release(sc_rtv[i]);
            IUnknown_Release(sc_tex[i]);
        }
        if (i == SCREEN_TOP)
            rp_lock_rel(comp_lock);
    }

    if (sc_fail[p]) {
        Sleep(REST_EVERY_MS);
        sc_fail[p] = 0;
    }
}

void ui_renderer_d3d11_gen_cursor(stbi_t *image, const unsigned char *base, int width, int height, int channels, float scale) {
    if (channels != GL_CHANNELS_N) {
        return;
    }
    bool fail = true;

    int i = SCREEN_TOP;
    int screen_top_bot = i;

    int target_width = roundf(width * scale);
    int target_height = roundf(height * scale);

    image->image = malloc(target_width * target_height * channels);
    if (!image->image) {
        return;
    }

    rp_lock_wait(comp_lock);

    unsigned char *base2 = NULL;
    unsigned char *image_base2 = NULL;

    base2 = malloc(width * height * GL_CHANNELS_N);
    if (!base2) {
        goto fail;
    }

    image_base2 = malloc(target_width * target_height * GL_CHANNELS_N);
    if (!image_base2) {
        goto fail;
    }

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            int base2_i = (y * width + x) * GL_CHANNELS_N;
            int base_i = (y * width + x) * channels;
            base2[base2_i + 2] = base2[base2_i] = ((int)base[base_i] + (int)base[base_i + 1] + (int)base[base_i + 2]) / 3;
            base2[base2_i + 1] = base[base_i + 3];
            base2[base2_i + 3] = UCHAR_MAX;
        }
    }

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = target_width;
    tex_desc.Height = target_height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = D3D_FORMAT;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    tex_desc.MiscFlags = 0;
    tex_desc.CPUAccessFlags = 0;
    HRESULT hr;

    ID3D11Texture2D *tex = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11Texture2D *staging = NULL;
    ID3D11Texture2D *in_tex = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11Texture2D *in_staging = NULL;

    hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &tex);
    if (hr) {
        err_log("CreateTexture2D failed: %d\n", (int)hr);
        goto fail;
    }

    hr = ID3D11Device_CreateRenderTargetView(d3d11device[i], (ID3D11Resource *)tex, NULL, &rtv);
    if (hr) {
        err_log("CreateRenderTargetView failed: %d\n", (int)hr);
        goto fail;
    }

    tex_desc.Usage = D3D11_USAGE_STAGING;
    tex_desc.BindFlags = 0;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &staging);
    if (hr) {
        err_log("CreateTexture2D failed: %d\n", (int)hr);
        goto fail;
    }

    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.Usage = D3D11_USAGE_DYNAMIC;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &in_tex);
    if (hr) {
        err_log("CreateTexture2D failed: %d\n", (int)hr);
        goto fail;
    }

    hr = ID3D11Device_CreateShaderResourceView(d3d11device[i], (ID3D11Resource *)in_tex, NULL, &srv);
    if (hr) {
        err_log("CreateShaderResourceView failed: %d\n", (int)hr);
        goto fail;
    }

    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.CPUAccessFlags = 0;

    hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &in_staging);
    if (hr) {
        err_log("CreateTexture2D failed: %d\n", (int)hr);
        goto fail;
    }

    D3D11_MAPPED_SUBRESOURCE tex_mapped = {};
    hr = ID3D11DeviceContext_Map(d3d11device_context[i], (ID3D11Resource *)in_tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &tex_mapped);
    if (hr) {
        err_log("Map failed: %d", (int)hr);
        goto fail;
    }
    for (int i = 0; i < height; ++i) {
        memcpy(tex_mapped.pData + i * tex_mapped.RowPitch, base2 + i * width * GL_CHANNELS_N, width * GL_CHANNELS_N);
    }
    ID3D11DeviceContext_Unmap(d3d11device_context[i], (ID3D11Resource *)in_tex, 0);

    int upscaling_selected = ui_upscaling_selected;

    if (
        IS_PLACEBO(upscaling_selected) &&
        placebo_upscaling_update(PLACEBO_MODE(upscaling_selected), i, screen_top_bot) == 0 &&
        placebo_render[i][screen_top_bot]
    ) {
        int fail = true;
        pl_tex pl_in_tex = NULL;
        pl_tex pl_out_tex = NULL;
        ID3D11DeviceContext_CopyResource(d3d11device_context[i], (ID3D11Resource *)in_staging, (ID3D11Resource *)in_tex);

        struct pl_d3d11_wrap_params in_tex_pars = { .tex = (ID3D11Resource *)in_staging };
        pl_in_tex = pl_d3d11_wrap(pl_d3d11_dev[i]->gpu, &in_tex_pars);
        if (!pl_in_tex) {
            goto placebo_fail;
        }

        struct pl_d3d11_wrap_params out_tex_pars = { .tex = (ID3D11Resource *)tex };
        pl_out_tex = pl_d3d11_wrap(pl_d3d11_dev[i]->gpu, &out_tex_pars);
        if (!pl_out_tex) {
            goto placebo_fail;
        }
        bool ret = placebo_render_run(placebo_render[i][screen_top_bot], pl_in_tex, pl_out_tex, 0, 0) != NULL;
        if (!ret) {
            goto placebo_fail;
        }

        fail = false;
placebo_fail:
        if (pl_in_tex)
            pl_tex_destroy(pl_d3d11_dev[i]->gpu, &pl_in_tex);
        if (pl_out_tex)
            pl_tex_destroy(pl_d3d11_dev[i]->gpu, &pl_out_tex);

        if (fail)
            goto no_upscale;
    } else if (
        IS_RASHADER(upscaling_selected) &&
        rashader_upscaling_update(RASHADER_MODE(upscaling_selected), i, screen_top_bot) == 0 &&
        rashader_render[i][screen_top_bot]
    ) {
        fail = true;
        libra_d3d11_filter_chain_t *chain = rashader_render_chain(rashader_render[i][screen_top_bot]);

        libra_error_t err = libra_d3d11_filter_chain_frame(chain, d3d11device_context[i], 1, srv, rtv, NULL, NULL, NULL);
        if (err) {
            libra_error_print(err);
            libra_error_free(&err);
            goto rashader_fail;
        }

        fail = false;

rashader_fail:
        if (fail)
            goto no_upscale;
    } else {
no_upscale:
        ID3D11DeviceContext_OMSetRenderTargets(d3d11device_context[i], 1, &rtv, NULL);
        D3D11_VIEWPORT vp = { .Width = target_width, .Height = target_height };
        ID3D11DeviceContext_RSSetViewports(d3d11device_context[i], 1, &vp);
        ID3D11DeviceContext_VSSetShader(d3d11device_context[i], d3d_vs[i], NULL, 0);
        ID3D11DeviceContext_RSSetState(d3d11device_context[i], d3d_rs[i]);

        d3d11_draw_screen(i, srv);
    }
    ID3D11DeviceContext_CopyResource(d3d11device_context[i], (ID3D11Resource *)staging, (ID3D11Resource *)tex);
    tex_mapped = (D3D11_MAPPED_SUBRESOURCE){};
    hr = ID3D11DeviceContext_Map(d3d11device_context[i], (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &tex_mapped);
    if (hr) {
        err_log("Map failed: %d\n", (int)hr);
        goto fail;
    }
    for (int i = 0; i < target_height; ++i) {
        memcpy(image_base2 + i * target_width * GL_CHANNELS_N, tex_mapped.pData + i * tex_mapped.RowPitch, target_width * GL_CHANNELS_N);
    }
    ID3D11DeviceContext_Unmap(d3d11device_context[i], (ID3D11Resource *)staging, 0);

    fail = false;
    for (int x = 0; x < target_width; ++x) {
        for (int y = 0; y < target_height; ++y) {
            int image_i = (y * target_width + x) * channels;
            int image2_i = (y * target_width + x) * GL_CHANNELS_N;
            image->image[image_i + 2] = image->image[image_i + 1] = image->image[image_i] =
                ((int)image_base2[image2_i] + (int)image_base2[image2_i + 2]) / 2;
            image->image[image_i + 3] = image_base2[image2_i + 1];
        }
    }

    image->width = target_width;
    image->height = target_height;
    image->channels = channels;


fail:
    CHECK_AND_RELEASE(in_staging);
    CHECK_AND_RELEASE(srv);
    CHECK_AND_RELEASE(in_tex);
    CHECK_AND_RELEASE(staging);
    CHECK_AND_RELEASE(rtv);
    CHECK_AND_RELEASE(tex);

    if (image_base2) {
        free(image_base2);
    }

    if (base2) {
        free(base2);
    }

    if (fail) {
        free(image->image);
        image->image = 0;
    }

    rp_lock_rel(comp_lock);
}
