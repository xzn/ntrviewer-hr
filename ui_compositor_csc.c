#include "ui_compositor_csc.h"
#include "main.h"
#include "ui_main_nk.h"

#include "glad/glad_wgl.h"

IDXGIAdapter1 *dxgi_adapter;
IDXGIFactory2 *dxgi_factory;

IDXGISwapChain *dxgi_sc[SCREEN_COUNT];
ID3D11Device *d3d11device[SCREEN_COUNT];
ID3D11DeviceContext *d3d11device_context[SCREEN_COUNT];
IDXGIDevice *dxgi_device[SCREEN_COUNT];
IDXGIDevice2 *dxgi_device2[SCREEN_COUNT];
IDXGIAdapter1 *dxgi_adapter;
IDXGIFactory2 *dxgi_factory;
IPresentationFactory *presentation_factory[SCREEN_COUNT];
bool displayable_surface_support[SCREEN_COUNT];
HANDLE pres_man_lost_event[SCREEN_COUNT];
HANDLE pres_man_stat_avail_event[SCREEN_COUNT];
HANDLE pres_man_child_lost_event[SCREEN_COUNT];
HANDLE pres_man_child_stat_avail_event[SCREEN_COUNT];
IPresentationManager *presentation_manager[SCREEN_COUNT];
IPresentationManager *pres_man_child[SCREEN_COUNT];
IPresentationManager *pres_man_util[SCREEN_COUNT];
HANDLE composition_surface[SCREEN_COUNT];
IPresentationSurface *presentation_surface[SCREEN_COUNT];
RECT src_rect[SCREEN_COUNT];
HANDLE comp_surf_child[SCREEN_COUNT];
HANDLE comp_surf_util[SURFACE_UTIL_COUNT];
IPresentationSurface *pres_surf_child[SCREEN_COUNT];
IPresentationSurface *pres_surf_util[SURFACE_UTIL_COUNT];
RECT src_rect_child[SCREEN_COUNT];
RECT src_rect_util[SCREEN_COUNT];
IUnknown *dcomp_surface[SCREEN_COUNT];
IUnknown *dcomp_surf_child[SCREEN_COUNT];
IUnknown *dcomp_surf_util[SURFACE_UTIL_COUNT];
IDCompositionDesktopDevice *dcomp_desktop_device[SCREEN_COUNT];
IDCompositionDevice *dcomp_device1[SCREEN_COUNT];
IDCompositionDevice3 *dcomp_device[SCREEN_COUNT];
IDCompositionTarget *dcomp_target[SCREEN_COUNT];
IDCompositionVisual2 *dcomp_visual[SCREEN_COUNT];
IDCompositionVisual2 *dcomp_vis_child[SCREEN_COUNT];
IDCompositionVisual2 *dcomp_vis_util[SURFACE_UTIL_COUNT];
HANDLE gl_d3ddevice[SCREEN_COUNT];

DCompositionCreateDevice3_t DCompositionCreateDevice3;
DCompositionCreateSurfaceHandle_t DCompositionCreateSurfaceHandle;
DCompositionGetStatistics_t DCompositionGetStatistics;
DCompositionGetTargetStatistics_t DCompositionGetTargetStatistics;
CreatePresentationFactory_t pfn_CreatePresentationFactory;
DCompositionBoostCompositorClock_t DCompositionBoostCompositorClock;

static int dcomp_pfn_init(void) {
    HMODULE dcomp = GetModuleHandleA("dcomp.dll");
    if (!dcomp) dcomp = LoadLibraryA("dcomp.dll");
    if (!dcomp) return -1;

    DCompositionCreateDevice3 = (DCompositionCreateDevice3_t)(void *)GetProcAddress(dcomp, "DCompositionCreateDevice3");
    if (!DCompositionCreateDevice3) return -1;

    DCompositionCreateSurfaceHandle = (DCompositionCreateSurfaceHandle_t)(void *)GetProcAddress(dcomp, "DCompositionCreateSurfaceHandle");
    if (!DCompositionCreateSurfaceHandle) return -1;

    DCompositionGetStatistics = (DCompositionGetStatistics_t)(void *)GetProcAddress(dcomp, "DCompositionGetStatistics");
    if (!DCompositionGetStatistics) return -1;

    DCompositionGetTargetStatistics = (DCompositionGetTargetStatistics_t)(void *)GetProcAddress(dcomp, "DCompositionGetTargetStatistics");
    if (!DCompositionGetTargetStatistics) return -1;

    DCompositionBoostCompositorClock = (DCompositionBoostCompositorClock_t)(void *)GetProcAddress(dcomp, "DCompositionBoostCompositorClock");
    if (!DCompositionBoostCompositorClock) return -1;

    pfn_CreatePresentationFactory = (CreatePresentationFactory_t)(void *)GetProcAddress(dcomp, "CreatePresentationFactory");
    if (!pfn_CreatePresentationFactory) return -1;

    return 0;
}

#define COMPOSITIONOBJECT_READ 0x0001L
#define COMPOSITIONOBJECT_WRITE 0x0002L

#define COMPOSITIONOBJECT_ALL_ACCESS (COMPOSITIONOBJECT_READ | COMPOSITIONOBJECT_WRITE)

UNUSED static const D3D11_FEATURE D3D11_FEATURE_DISPLAYABLE = (D3D11_FEATURE)20;

UNUSED static const IID IID_IPresentationFactory = { 0x8fb37b58, 0x1d74, 0x4f64, { 0xa4, 0x9c, 0x1f, 0x97, 0xa8, 0x0a, 0x2e, 0xc0 } };
UNUSED static const IID IID_IPresentStatusPresentStatistics = { 0xc9ed2a41, 0x79cb, 0x435e, { 0x96, 0x4e, 0xc8, 0x55, 0x30, 0x55, 0x42, 0x0c } };
UNUSED static const IID IID_IIndependentFlipFramePresentStatistics = { 0x8c93be27, 0xad94, 0x4da0, { 0x8f, 0xd4, 0x24, 0x13, 0x13, 0x2d, 0x12, 0x4e } };
UNUSED static const IID IID_ICompositionFramePresentStatistics = { 0xab41d127, 0xc101, 0x4c0a, { 0x91, 0x1d, 0xf9, 0xf2, 0xe9, 0xd0, 0x8e, 0x64 } };

UNUSED static const IID IID_IDCompositionDevice = { 0xc37ea93a, 0xe7aa, 0x450d, { 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3 } };
UNUSED static const IID IID_IDCompositionDevice3 = { 0x0987cb06, 0xf916, 0x48bf, { 0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9 } };
UNUSED static const IID IID_IDCompositionDesktopDevice = { 0x5f4633fe, 0x1e08, 0x4cb8, { 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02 } };

#define PRESENT_STATS (1)

int dxgi_init(void)
{
    HRESULT hr;

    if (!dxgi_factory) {
        hr = CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&dxgi_factory);
        if (hr) {
            err_log("CreateDXGIFactory1 failed: %d\n", (int)hr);
            return hr;
        }
    }

    CHECK_AND_RELEASE(dxgi_adapter);
    for (int i = 0;; ++i) {
        hr = IDXGIFactory2_EnumAdapters1(dxgi_factory, i, &dxgi_adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            err_log("EnumAdapters1 exhausted\n");
            dxgi_adapter = NULL;
            break;
        } else if (hr) {
            err_log("EnumAdapters1 failed: %d\n", (int)hr);
            return hr;
        }
        DXGI_ADAPTER_DESC1 adapter_desc;
        hr = IDXGIAdapter1_GetDesc1(dxgi_adapter, &adapter_desc);
        if (hr) {
            err_log("GetDesc1 failed: %d\n", (int)hr);
            return hr;
        }

        bool is_hardware = adapter_desc.Flags == DXGI_ADAPTER_FLAG_NONE;
        if (is_hardware) {
            if (is_renderer_sdl_ogl()) {
                D3D_FEATURE_LEVEL featureLevelSupported;
                ID3D11Device *dev;
                ID3D11DeviceContext *ctx;
                HANDLE gl_dev;

                hr = D3D11CreateDevice(
                    (IDXGIAdapter *)dxgi_adapter,
                    0, NULL,
                    D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                        D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
                    NULL, 0, D3D11_SDK_VERSION, &dev, &featureLevelSupported,
                    &ctx);
                if (hr) {
                    goto next;
                }

                gl_dev = wglDXOpenDeviceNV(dev);
                if (!gl_dev) {
                    CHECK_AND_RELEASE(dev);
                    CHECK_AND_RELEASE(ctx);
                    goto next;
                }

                wglDXCloseDeviceNV(gl_dev);
                CHECK_AND_RELEASE(dev);
                CHECK_AND_RELEASE(ctx);
            }
            err_log("using adapter %d\n", i);
            break;
        }

next:
        CHECK_AND_RELEASE(dxgi_adapter);
    }

    return 0;
}

void dxgi_close(void)
{
    CHECK_AND_RELEASE(dxgi_adapter);
    CHECK_AND_RELEASE(dxgi_factory);
}

static bool win_shared_prev;

static void composition_buffer_cleanup(int ctx_top_bot) {
    int i = ctx_top_bot;
    src_rect[i].bottom = src_rect[i].right = 0;
    for (int j = 0; j < SCREEN_COUNT; ++j) {
        // i here is tied to the respective GL context, where as in render_buffer_delete it's screen_top_bot
        // hence the reversed order of parameters
        if (is_renderer_sdl_ogl()) {
            struct render_buffer_t *b = &render_buffers[j][i];
            render_buffer_delete(b, j);
        }
        if (i == SCREEN_TOP) {
            src_rect_child[j].bottom = src_rect_child[j].right = 0;
        }
        for (int k = 0; k < PRESENATTION_BUFFER_COUNT_PER_SCREEN; ++k) {
            struct presentation_buffer_t *b = &presentation_buffers[i][j][k];
            presentation_buffer_delete(b);
        }
    }
    if (i == SCREEN_TOP) {
        if (is_renderer_sdl_ogl()) {
            struct render_buffer_t *b = &ui_render_buf;
            render_buffer_delete(b, i);
        }
        for (int k = 0; k < COMPAT_PRESENATTION_BUFFER_COUNT_PER_SCREEN; ++k) {
            struct presentation_buffer_t *b = &ui_pres_bufs[k];
            presentation_buffer_delete(b);
        }

        for (int j = 0; j < SCREEN_COUNT; ++j) {
            ui_win_height_drawable_prev[j] = ui_win_width_drawable_prev[j] = ui_ctx_height[j] = ui_ctx_width[j] = 0;
            src_rect[j].right = src_rect[j].bottom = 0;
            src_rect_child[j].right = src_rect_child[j].bottom = 0;
        }

        for (int j = 0; j < SURFACE_UTIL_COUNT; ++j)
            src_rect_util[j].right = src_rect_util[j].bottom = 0;
    }
}

static int presentation_render_reset(int win_shared, bool bg)
{
    int i = SCREEN_TOP;
    composition_buffer_cleanup(i);

    HRESULT hr;
    if (win_shared) {
        // set background color
        if (bg) {
            int j = SURFACE_UTIL_BG;
            hr = dcomp_visual[i]->lpVtbl->AddVisual(dcomp_visual[i], (IDCompositionVisual *)dcomp_vis_util[j], j == SURFACE_UTIL_BG ? TRUE : FALSE, NULL);
            if (hr) {
                err_log("AddVisual failed: %d\n", (int)hr);
                return hr;
            }
        }
        {
            D3D11_TEXTURE2D_DESC textureDesc = {};
            textureDesc.Width = 1;
            textureDesc.Height = 1;
            textureDesc.MipLevels = 1;
            textureDesc.ArraySize = 1;
            textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            textureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            textureDesc.CPUAccessFlags = 0;

            ID3D11Texture2D *tex;
            IPresentationBuffer *buf;

            hr = ID3D11Device_CreateTexture2D(d3d11device[i], &textureDesc, NULL, &tex);
            if (hr) {
                err_log("CreateTexture2D failed: %d\n", (int)hr);
                return hr;
            }

            hr = IPresentationManager_AddBufferFromResource(pres_man_util[SURFACE_UTIL_BG], (IUnknown *)tex, &buf);
            if (hr) {
                err_log("AddBufferFromResource failed: %d\n", (int)hr);
                return hr;
            }

            // comp_lock should have been held when calling this function
            ID3D11RenderTargetView *rtv;
            hr = ID3D11Device_CreateRenderTargetView(d3d11device[i], (ID3D11Resource *)tex, NULL, &rtv);
            if (hr) {
                err_log("CreateRenderTargetView failed: %d\n", (int)hr);
                return -1;
            }

            float clearColor[4];
            nk_color_fv(clearColor, nk_window_bgcolor);
            ID3D11DeviceContext_ClearRenderTargetView(d3d11device_context[i], rtv, clearColor);

            hr = IPresentationSurface_SetBuffer(pres_surf_util[SURFACE_UTIL_BG], buf);
            if (hr) {
                err_log("SetBuffer failed: %d\n", (int)hr);
                return -1;
            }

            RECT rect = {0, 0, 1, 1};
            hr = IPresentationSurface_SetSourceRect(pres_surf_util[SURFACE_UTIL_BG], &rect);

            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j) {
                hr = dcomp_vis_util[j]->lpVtbl->SetContent(dcomp_vis_util[j], dcomp_surf_util[j]);
                if (hr) {
                    err_log("SetContent failed: %d\n", (int)hr);
                    return hr;
                }
            }

            hr = IPresentationManager_Present(pres_man_util[SURFACE_UTIL_BG]);
            if (hr) {
                err_log("Present failed: %d\n", (int)hr);
                return -1;
            }

            IUnknown_Release(rtv);
            IUnknown_Release(tex);
            IUnknown_Release(buf);
        }

        for (int j = 0; j < SCREEN_COUNT; ++j) {
            hr = dcomp_vis_child[j]->lpVtbl->SetContent(dcomp_vis_child[j], dcomp_surf_child[j]);
            if (hr) {
                err_log("SetContent failed: %d\n", (int)hr);
                return hr;
            }
        }
    } else {
        if (bg) {
            int j = SURFACE_UTIL_BG;
            hr = dcomp_visual[i]->lpVtbl->RemoveVisual(dcomp_visual[i], (IDCompositionVisual *)dcomp_vis_util[j]);
            if (hr) {
                err_log("RemoveVisual failed: %d\n", (int)hr);
                return hr;
            }
        }

        for (int j = 0; j < SCREEN_COUNT; ++j) {
            hr = dcomp_vis_child[j]->lpVtbl->SetContent(dcomp_vis_child[j], NULL);
            if (hr) {
                err_log("SetContent failed: %d\n", (int)hr);
                return hr;
            }
        }

        for (int j = 0; j < SURFACE_UTIL_COUNT; ++j) {
            hr = dcomp_vis_util[j]->lpVtbl->SetContent(dcomp_vis_util[j], j == SURFACE_UTIL_UI ? dcomp_surf_util[j] : NULL);
            if (hr) {
                err_log("SetContent failed: %d\n", (int)hr);
                return hr;
            }
        }

        hr = dcomp_visual[i]->lpVtbl->SetContent(dcomp_visual[i], dcomp_surface[i]);
        if (hr) {
            err_log("SetContent failed: %d\n", (int)hr);
            return hr;
        }
    }

    for (int j = 0; j < SCREEN_COUNT; ++j) {
        hr = dcomp_device[j]->lpVtbl->Commit(dcomp_device[j]);
        if (hr) {
            err_log("Commit failed: %d\n", (int)hr);
            return hr;
        }
    }

    return 0;
}

int composition_swapchain_device_init(void)
{
    HRESULT hr;

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        D3D_FEATURE_LEVEL featureLevelSupported;

        hr = D3D11CreateDevice(
            (IDXGIAdapter *)dxgi_adapter,
            dxgi_adapter ? 0 : D3D_DRIVER_TYPE_HARDWARE, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS,
            NULL, 0, D3D11_SDK_VERSION, &d3d11device[i], &featureLevelSupported,
            &d3d11device_context[i]);
        if (hr) {
            err_log("D3D11CreateDevice failed: %d\n", (int)hr);
            return hr;
        }

        hr = ID3D11Device_QueryInterface(d3d11device[i], &IID_IDXGIDevice,
                                         (void **)&dxgi_device[i]);
        if (hr) {
            err_log("QueryInterface IDXGIDevice failed: %d\n", (int)hr);
            return hr;
        }

        hr = ID3D11Device_QueryInterface(d3d11device[i], &IID_IDXGIDevice2,
                                         (void **)&dxgi_device2[i]);
        if (hr) {
            err_log("QueryInterface IDXGIDevice2 failed: %d\n", (int)hr);
            return hr;
        }

        hr = pfn_CreatePresentationFactory((IUnknown *)d3d11device[i],
                                           &IID_IPresentationFactory,
                                           (void **)&presentation_factory[i]);
        if (hr) {
            err_log("CreatePresentationFactory failed: %d\n", (int)hr);
            return hr;
        }

        if (!(IPresentationFactory_IsPresentationSupportedWithIndependentFlip(
                  presentation_factory[i]) ||
              IPresentationFactory_IsPresentationSupported(
                  presentation_factory[i]))) {
            err_log("presentation not supported\n");
            return -1;
        }

        D3D11_FEATURE_DATA_DISPLAYABLE displayable_feature;
        hr = ID3D11Device_CheckFeatureSupport(
            d3d11device[i], D3D11_FEATURE_DISPLAYABLE, &displayable_feature,
            sizeof(displayable_feature));
        if (hr) {
            err_log("CheckFeatureSupport failed: %d\n", (int)hr);
            return hr;
        }
        displayable_surface_support[i] = displayable_feature.DisplayableTexture;

        hr = IPresentationFactory_CreatePresentationManager(
            presentation_factory[i], &presentation_manager[i]);
        if (hr) {
            err_log("CreatePresentationManager failed: %d\n", (int)hr);
            return hr;
        }

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SCREEN_COUNT; ++j) {
                hr = IPresentationFactory_CreatePresentationManager(
                    presentation_factory[i], &pres_man_child[j]);
                if (hr) {
                    err_log("CreatePresentationManager failed: %d\n", (int)hr);
                    return hr;
                }
            }

            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j) {
                hr = IPresentationFactory_CreatePresentationManager(
                    presentation_factory[i], &pres_man_util[j]);
                if (hr) {
                    err_log("CreatePresentationManager failed: %d\n", (int)hr);
                    return hr;
                }
            }
        }

        hr = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, NULL,
                                             &composition_surface[i]);
        if (hr) {
            err_log("DCompositionCreateSurfaceHandle failed: %d\n", (int)hr);
            return hr;
        }

        hr = IPresentationManager_CreatePresentationSurface(
            presentation_manager[i], composition_surface[i],
            &presentation_surface[i]);
        if (hr) {
            err_log("CreatePresentationSurface failed: %d\n", (int)hr);
            return hr;
        }

        hr = IPresentationSurface_SetAlphaMode(presentation_surface[i],
                                               DXGI_ALPHA_MODE_IGNORE);
        if (hr) {
            err_log("SetAlphaMode failed: %d\n", (int)hr);
            return hr;
        }

        hr = IPresentationSurface_SetColorSpace(
            presentation_surface[i], DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        if (hr) {
            err_log("SetColorSpace failed: %d\n", (int)hr);
            return hr;
        }

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SCREEN_COUNT; ++j) {
                hr = DCompositionCreateSurfaceHandle(
                    COMPOSITIONOBJECT_ALL_ACCESS, NULL, &comp_surf_child[j]);
                if (hr) {
                    err_log("DCompositionCreateSurfaceHandle failed: %d\n",
                            (int)hr);
                    return hr;
                }

                hr = IPresentationManager_CreatePresentationSurface(
                    pres_man_child[j], comp_surf_child[j], &pres_surf_child[j]);
                if (hr) {
                    err_log("CreatePresentationSurface failed: %d\n", (int)hr);
                    return hr;
                }

                hr = IPresentationSurface_SetAlphaMode(pres_surf_child[j],
                                                       DXGI_ALPHA_MODE_IGNORE);
                if (hr) {
                    err_log("SetAlphaMode failed: %d\n", (int)hr);
                    return hr;
                }

                hr = IPresentationSurface_SetColorSpace(
                    pres_surf_child[j],
                    DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
                if (hr) {
                    err_log("SetColorSpace failed: %d\n", (int)hr);
                    return hr;
                }
            }

            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j) {
                hr = DCompositionCreateSurfaceHandle(
                    COMPOSITIONOBJECT_ALL_ACCESS, NULL, &comp_surf_util[j]);
                if (hr) {
                    err_log("DCompositionCreateSurfaceHandle failed: %d\n",
                            (int)hr);
                    return hr;
                }

                hr = IPresentationManager_CreatePresentationSurface(
                    pres_man_util[j], comp_surf_util[j], &pres_surf_util[j]);
                if (hr) {
                    err_log("CreatePresentationSurface failed: %d\n", (int)hr);
                    return hr;
                }

                hr = IPresentationSurface_SetAlphaMode(
                    pres_surf_util[j], j == SURFACE_UTIL_UI
                                           ? DXGI_ALPHA_MODE_PREMULTIPLIED
                                           : DXGI_ALPHA_MODE_IGNORE);
                if (hr) {
                    err_log("SetAlphaMode failed: %d\n", (int)hr);
                    return hr;
                }

                hr = IPresentationSurface_SetColorSpace(
                    pres_surf_util[j], DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
                if (hr) {
                    err_log("SetColorSpace failed: %d\n", (int)hr);
                    return hr;
                }
            }
        }

        hr = dcomp_device1[i]->lpVtbl->CreateSurfaceFromHandle(
            dcomp_device1[i], composition_surface[i], &dcomp_surface[i]);
        if (hr) {
            err_log("CreateSurfaceFromHandle failed: %d\n", (int)hr);
            return hr;
        }

        hr = dcomp_visual[i]->lpVtbl->SetContent(dcomp_visual[i],
                                                 dcomp_surface[i]);
        if (hr) {
            err_log("SetContent failed: %d\n", (int)hr);
            return hr;
        }

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SCREEN_COUNT; ++j) {
                hr = dcomp_device1[i]->lpVtbl->CreateSurfaceFromHandle(
                    dcomp_device1[i], comp_surf_child[j], &dcomp_surf_child[j]);
                if (hr) {
                    err_log("CreateSurfaceFromHandle failed: %d\n", (int)hr);
                    return hr;
                }

                hr = dcomp_vis_child[j]->lpVtbl->SetContent(
                    dcomp_vis_child[j], dcomp_surf_child[j]);
                if (hr) {
                    err_log("SetContent failed: %d\n", (int)hr);
                    return hr;
                }
            }

            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j) {
                hr = dcomp_device1[i]->lpVtbl->CreateSurfaceFromHandle(
                    dcomp_device1[i], comp_surf_util[j], &dcomp_surf_util[j]);
                if (hr) {
                    err_log("CreateSurfaceFromHandle failed: %d\n", (int)hr);
                    return hr;
                }

                hr = dcomp_vis_util[j]->lpVtbl->SetContent(dcomp_vis_util[j],
                                                           dcomp_surf_util[j]);
                if (hr) {
                    err_log("SetContent failed: %d\n", (int)hr);
                    return hr;
                }
            }
        }

        if (PRESENT_STATS) {
            hr = IPresentationManager_EnablePresentStatisticsKind(
                presentation_manager[i], PresentStatisticsKind_CompositionFrame,
                true);
            if (hr) {
                err_log(
                    "EnablePresentStatisticsKind CompositionFrame failed: %d\n",
                    (int)hr);
                return hr;
            }

            hr = IPresentationManager_EnablePresentStatisticsKind(
                presentation_manager[i], PresentStatisticsKind_PresentStatus,
                true);
            if (hr) {
                err_log(
                    "EnablePresentStatisticsKind PresentStatus failed: %d\n",
                    (int)hr);
                return hr;
            }

            hr = IPresentationManager_EnablePresentStatisticsKind(
                presentation_manager[i],
                PresentStatisticsKind_IndependentFlipFrame, true);
            if (hr) {
                err_log("EnablePresentStatisticsKind IndependentFlipFrame "
                        "failed: %d\n",
                        (int)hr);
                return hr;
            }
        }

        hr = IPresentationManager_GetLostEvent(presentation_manager[i],
                                               &pres_man_lost_event[i]);
        if (hr) {
            err_log("GetLostEvent failed: %d\n", (int)hr);
            return hr;
        }

        hr = IPresentationManager_GetPresentStatisticsAvailableEvent(
            presentation_manager[i], &pres_man_stat_avail_event[i]);
        if (hr) {
            err_log("GetPresentStatisticsAvailableEvent failed: %d\n", (int)hr);
            return hr;
        }

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SCREEN_COUNT; ++j) {
                if (PRESENT_STATS) {
                    hr = IPresentationManager_EnablePresentStatisticsKind(
                        pres_man_child[j],
                        PresentStatisticsKind_CompositionFrame, true);
                    if (hr) {
                        err_log("EnablePresentStatisticsKind CompositionFrame "
                                "failed: %d\n",
                                (int)hr);
                        return hr;
                    }

                    hr = IPresentationManager_EnablePresentStatisticsKind(
                        pres_man_child[j], PresentStatisticsKind_PresentStatus,
                        true);
                    if (hr) {
                        err_log("EnablePresentStatisticsKind PresentStatus "
                                "failed: %d\n",
                                (int)hr);
                        return hr;
                    }

                    hr = IPresentationManager_EnablePresentStatisticsKind(
                        pres_man_child[j],
                        PresentStatisticsKind_IndependentFlipFrame, true);
                    if (hr) {
                        err_log("EnablePresentStatisticsKind "
                                "IndependentFlipFrame failed: %d\n",
                                (int)hr);
                        return hr;
                    }
                }

                hr = IPresentationManager_GetLostEvent(
                    pres_man_child[j], &pres_man_child_lost_event[j]);
                if (hr) {
                    err_log("GetLostEvent failed: %d\n", (int)hr);
                    return hr;
                }

                hr = IPresentationManager_GetPresentStatisticsAvailableEvent(
                    pres_man_child[j], &pres_man_child_stat_avail_event[j]);
                if (hr) {
                    err_log("GetPresentStatisticsAvailableEvent failed: %d\n",
                            (int)hr);
                    return hr;
                }
            }
        }

        if (is_renderer_sdl_ogl()) {
            gl_d3ddevice[i] = wglDXOpenDeviceNV(d3d11device[i]);
            if (!gl_d3ddevice[i]) {
                hr = GetLastError();
                err_log("wglDXOpenDeviceNV failed: %d\n", (int)hr);
                return hr;
            }
        }
    }

    presentation_render_reset(win_shared_prev, 0);

    return 0;
}

int composition_swapchain_init(HWND hwnd[SCREEN_COUNT])
{
    if (dcomp_pfn_init() != 0) {
        err_log("dcomp_pfn_init failed\n");
        return -1;
    }

    HRESULT hr;

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        hr = DCompositionCreateDevice3((IUnknown *)dxgi_device[i],
                                       &IID_IDCompositionDesktopDevice,
                                       (void **)&dcomp_desktop_device[i]);
        if (hr) {
            err_log("DCompositionCreateDevice IDXGIDevice failed: %d\n",
                    (int)hr);
            return hr;
        }

        hr = dcomp_desktop_device[i]->lpVtbl->QueryInterface(
            dcomp_desktop_device[i], &IID_IDCompositionDevice3,
            (void **)&dcomp_device[i]);
        if (hr) {
            err_log("QueryInterface IDCompositionDevice3 failed: %d\n",
                    (int)hr);
            return hr;
        }

        hr = dcomp_device[i]->lpVtbl->QueryInterface(
            dcomp_device[i], &IID_IDCompositionDevice,
            (void **)&dcomp_device1[i]);
        if (hr) {
            err_log("QueryInterface IDCompositionDevice failed: %d\n", (int)hr);
            return hr;
        }

        hr = dcomp_device[i]->lpVtbl->CreateVisual(dcomp_device[i],
                                                   &dcomp_visual[i]);
        if (hr) {
            err_log("CreateVisual failed: %d\n", (int)hr);
            return hr;
        }

        hr = dcomp_desktop_device[i]->lpVtbl->CreateTargetForHwnd(
            dcomp_desktop_device[i], hwnd[i], TRUE, &dcomp_target[i]);
        if (hr) {
            err_log("CreateTargetForHwnd failed: %d\n", (int)hr);
            return hr;
        }

        hr = dcomp_target[i]->lpVtbl->SetRoot(
            dcomp_target[i], (IDCompositionVisual *)dcomp_visual[i]);
        if (hr) {
            err_log("SetRoot failed: %d\n", (int)hr);
            return hr;
        }

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SCREEN_COUNT; ++j) {
                hr = dcomp_device[i]->lpVtbl->CreateVisual(dcomp_device[i],
                                                           &dcomp_vis_child[j]);
                if (hr) {
                    err_log("CreateVisual failed: %d\n", (int)hr);
                    return hr;
                }

                hr = dcomp_visual[i]->lpVtbl->AddVisual(
                    dcomp_visual[i], (IDCompositionVisual *)dcomp_vis_child[j],
                    FALSE, NULL);
                if (hr) {
                    err_log("AddVisual failed: %d\n", (int)hr);
                    return hr;
                }
            }

            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j) {
                hr = dcomp_device[i]->lpVtbl->CreateVisual(dcomp_device[i],
                                                           &dcomp_vis_util[j]);
                if (hr) {
                    err_log("CreateVisual failed: %d\n", (int)hr);
                    return hr;
                }

                if (j == SURFACE_UTIL_BG)
                    continue;
                hr = dcomp_visual[i]->lpVtbl->AddVisual(
                    dcomp_visual[i], (IDCompositionVisual *)dcomp_vis_util[j],
                    j == SURFACE_UTIL_BG ? TRUE : FALSE, NULL);
                if (hr) {
                    err_log("AddVisual failed: %d\n", (int)hr);
                    return hr;
                }
            }
        }

        hr = dcomp_device[i]->lpVtbl->Commit(dcomp_device[i]);
        if (hr) {
            err_log("Commit failed: %d\n", (int)hr);
            return hr;
        }
    }

    hr = DCompositionBoostCompositorClock(TRUE);
    if (hr) {
        err_log("DCompositionBoostCompositorClock failed: %d\n", (int)hr);
    }

    return composition_swapchain_device_init();
}

void composition_swapchain_device_close(void)
{
    for (int i = 0; i < SCREEN_COUNT; ++i) {
        if (is_renderer_sdl_ogl()) {
            if (gl_d3ddevice[i]) {
                wglDXCloseDeviceNV(gl_d3ddevice[i]);
                gl_d3ddevice[i] = NULL;
            }
        }

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SCREEN_COUNT; ++j) {
                if (pres_man_child_stat_avail_event[i]) {
                    CloseHandle(pres_man_child_stat_avail_event[i]);
                    pres_man_child_stat_avail_event[i] = NULL;
                }

                if (pres_man_child_lost_event[i]) {
                    CloseHandle(pres_man_child_lost_event[i]);
                    pres_man_child_lost_event[i] = NULL;
                }
            }
        }

        if (pres_man_stat_avail_event[i]) {
            CloseHandle(pres_man_stat_avail_event[i]);
            pres_man_stat_avail_event[i] = NULL;
        }

        if (pres_man_lost_event[i]) {
            CloseHandle(pres_man_lost_event[i]);
            pres_man_lost_event[i] = NULL;
        }

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j)
                CHECK_AND_RELEASE(dcomp_surf_util[i]);

            for (int j = 0; j < SCREEN_COUNT; ++j)
                CHECK_AND_RELEASE(dcomp_surf_child[i]);
        }

        CHECK_AND_RELEASE(dcomp_surface[i]);

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j)
                CHECK_AND_RELEASE(pres_surf_util[i]);

            for (int j = 0; j < SCREEN_COUNT; ++j)
                CHECK_AND_RELEASE(pres_surf_child[i]);
        }

        CHECK_AND_RELEASE(presentation_surface[i]);

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j)
                if (comp_surf_util[i]) {
                    CloseHandle(comp_surf_util[i]);
                    comp_surf_util[i] = NULL;
                }

            for (int j = 0; j < SCREEN_COUNT; ++j)
                if (comp_surf_child[i]) {
                    CloseHandle(comp_surf_child[i]);
                    comp_surf_child[i] = NULL;
                }
        }

        if (composition_surface[i]) {
            CloseHandle(composition_surface[i]);
            composition_surface[i] = NULL;
        }

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j)
                CHECK_AND_RELEASE(pres_man_util[j]);

            for (int j = 0; j < SCREEN_COUNT; ++j)
                CHECK_AND_RELEASE(pres_man_child[i]);
        }

        CHECK_AND_RELEASE(presentation_manager[i]);

        displayable_surface_support[i] = false;

        CHECK_AND_RELEASE(presentation_factory[i]);

        CHECK_AND_RELEASE(dxgi_device2[i]);

        CHECK_AND_RELEASE(dxgi_device[i]);

        CHECK_AND_RELEASE(d3d11device_context[i]);

        CHECK_AND_RELEASE(d3d11device[i]);
    }
}

void composition_swapchain_close(void)
{
    composition_swapchain_device_close();

    for (int i = 0; i < SCREEN_COUNT; ++i) {
        CHECK_AND_RELEASE(dcomp_target[i]);

        if (i == SCREEN_TOP) {
            for (int j = 0; j < SURFACE_UTIL_COUNT; ++j)
                CHECK_AND_RELEASE(dcomp_vis_util[i]);

            for (int j = 0; j < SCREEN_COUNT; ++j)
                CHECK_AND_RELEASE(dcomp_vis_child[i]);
        }

        CHECK_AND_RELEASE(dcomp_visual[i]);

        CHECK_AND_RELEASE(dcomp_device1[i]);

        CHECK_AND_RELEASE(dcomp_device[i]);

        CHECK_AND_RELEASE(dcomp_desktop_device[i]);
    }
}

rp_lock_t comp_lock;
bool sc_fail[SCREEN_COUNT];
int ui_win_width_drawable_prev[SCREEN_COUNT], ui_win_height_drawable_prev[SCREEN_COUNT];
bool ui_compositing;

void ui_compositor_csc_main(int screen_top_bot, int ctx_top_bot, bool win_shared)
{
    int i = ctx_top_bot;
    int p = win_shared ? screen_top_bot : i;

    if (i == SCREEN_TOP) {
        rp_lock_wait(comp_lock);
        if (__atomic_load_n(&win_shared_prev, __ATOMIC_ACQUIRE) != win_shared) {
            if (screen_top_bot == SCREEN_TOP) {
                if (presentation_render_reset(win_shared, 1)) {
                    sc_fail[p] = 1;
                    return;
                }
                __atomic_store_n(&win_shared_prev, win_shared, __ATOMIC_RELEASE);
            } else {
                sc_fail[p] = 1;
                return;
            }
        }
    }

    if (win_shared) {
        int ctx_left;
        int ctx_top;

        int ctx_width;
        int ctx_height;

        const int width = screen_top_bot == SCREEN_TOP ? SCREEN_HEIGHT0 : SCREEN_HEIGHT1;
        const int height = SCREEN_WIDTH;

        ctx_height = (double)ui_win_height_drawable[i] / 2;
        if ((double)ui_win_width_drawable[i] / width * height > ctx_height) {
            ctx_width = (double)ctx_height / height * width;
            ctx_left = (double)(ui_win_width_drawable[i] - ctx_width) / 2;
            ctx_top = 0;
        } else {
            ctx_height = (double)ui_win_width_drawable[i] / width * height;
            ctx_left = 0;
            ctx_width = ui_win_width_drawable[i];
            ctx_top = (double)ui_win_height_drawable[i] / 2 - ctx_height;
        }

        if (screen_top_bot != SCREEN_TOP) {
            ctx_top = (double)ui_win_height_drawable[i] / 2;
        }

        ctx_width = NK_MAX(ctx_width, 1);
        ctx_height = NK_MAX(ctx_height, 1);

        if (ui_win_width_drawable_prev[screen_top_bot] != ui_win_width_drawable[i] || ui_win_height_drawable_prev[screen_top_bot] != ui_win_height_drawable[i]) {
            HRESULT hr;

            hr = dcomp_vis_child[screen_top_bot]->lpVtbl->SetOffsetX2(dcomp_vis_child[screen_top_bot], (FLOAT)ctx_left);
            if (hr) {
                err_log("SetOffsetX failed: %d\n", (int)hr);
                sc_fail[p] = 1;
                return;
            }

            hr = dcomp_vis_child[screen_top_bot]->lpVtbl->SetOffsetY2(dcomp_vis_child[screen_top_bot], (FLOAT)ctx_top);
            if (hr) {
                err_log("SetOffsetY failed: %d\n", (int)hr);
                sc_fail[p] = 1;
                return;
            }

            D2D_MATRIX_3X2_F bg_trans_mat = {.m = {{(FLOAT)ui_win_width_drawable[i], 0.0f}, {0.0f, (FLOAT)ui_win_height_drawable[i]}, {0.0f, 0.0f}}};
            hr = dcomp_vis_util[SURFACE_UTIL_BG]->lpVtbl->SetTransform2(dcomp_vis_util[SURFACE_UTIL_BG], &bg_trans_mat);
            if (hr) {
                err_log("SetTransform failed: %d\n", (int)hr);
                sc_fail[p] = 1;
                return;
            }

            hr = dcomp_device[SCREEN_TOP]->lpVtbl->Commit(dcomp_device[SCREEN_TOP]);
            if (hr) {
                err_log("Commit failed: %d\n", (int)hr);
                sc_fail[p] = 1;
                return;
            }

            ui_win_width_drawable_prev[screen_top_bot] = ui_win_width_drawable[i];
            ui_win_height_drawable_prev[screen_top_bot] = ui_win_height_drawable[i];

            ui_ctx_width[p] = ctx_width;
            ui_ctx_height[p] = ctx_height;
        }
    } else {
        if (ui_win_width_drawable_prev[screen_top_bot] != ui_win_width_drawable[i] || ui_win_height_drawable_prev[screen_top_bot] != ui_win_height_drawable[i]) {
            if (i == SCREEN_TOP) {
                HRESULT hr;

                hr = dcomp_device[i]->lpVtbl->Commit(dcomp_device[i]);
                if (hr) {
                    err_log("Commit failed: %d\n", (int)hr);
                    sc_fail[p] = 1;
                    return;
                }
            }
            ui_ctx_width[p] = ui_win_width_drawable_prev[screen_top_bot] = ui_win_width_drawable[i];
            ui_ctx_height[p] = ui_win_height_drawable_prev[screen_top_bot] = ui_win_height_drawable[i];
        }
    }
}

void ui_compositor_csc_present(int ctx_top_bot)
{
    int i = ctx_top_bot;
    if (i == SCREEN_TOP) {
        rp_lock_rel(comp_lock);
    }
}

struct presentation_buffer_t presentation_buffers[SCREEN_COUNT][SCREEN_COUNT][PRESENATTION_BUFFER_COUNT_PER_SCREEN], ui_pres_bufs[COMPAT_PRESENATTION_BUFFER_COUNT_PER_SCREEN];
struct render_buffer_t render_buffers[SCREEN_COUNT][SCREEN_COUNT], ui_render_buf;

int presentation_buffer_delete(struct presentation_buffer_t *b)
{
    if (b->buf_avail_event) {
        CloseHandle(b->buf_avail_event);
        b->buf_avail_event = NULL;
    }

    if (b->buf) {
        IPresentationBuffer_Release(b->buf);
        b->buf = NULL;
    }

    if (b->rtv) {
        ID3D11RenderTargetView_Release(b->rtv);
        b->rtv = NULL;
    }

    if (b->tex) {
        ID3D11Texture2D_Release(b->tex);
        b->tex = NULL;
    }

    b->width = 0;
    b->height = 0;

    return 0;
}

int presentation_buffer_gen(struct presentation_buffer_t *b, int ctx_top_bot, int win_shared, int width, int height)
{
    int j = ctx_top_bot;
    if (win_shared) {
        ctx_top_bot = SCREEN_TOP;
    }
    int i = ctx_top_bot;

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (is_renderer_d3d11())
        tex_desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    tex_desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    if (win_shared >= 0 && displayable_surface_support[i])
        tex_desc.MiscFlags |= D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE;
    tex_desc.CPUAccessFlags = 0;

    HRESULT hr;

    hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &b->tex);
    if (hr) {
        err_log("CreateTexture2D failed: %d\n", (int)hr);
        goto fail;
    }

    if (is_renderer_d3d11()) {
        hr = ID3D11Device_CreateRenderTargetView(d3d11device[i], (ID3D11Resource *)b->tex, NULL, &b->rtv);
        if (hr) {
            err_log("CreateRenderTargetView failed: %d\n", (int)hr);
            return -1;
        }
    }

    hr = IPresentationManager_AddBufferFromResource(
        win_shared >= 0 ? win_shared ? pres_man_child[j] : presentation_manager[j] : pres_man_util[j],
        (IUnknown *)b->tex, &b->buf);
    if (hr) {
        err_log("AddBufferFromResource failed: %d\n", (int)hr);
        goto fail;
    }

    hr = IPresentationBuffer_GetAvailableEvent(b->buf, &b->buf_avail_event);
    if (hr) {
        err_log("GetAvailableEvent failed: %d\n", (int)hr);
        goto fail;
    }

    b->width = width;
    b->height = height;

    return 0;

fail:
    presentation_buffer_delete(b);
    return -1;
}

#define PRINT_PRESENT_STATS (0)

static void pres_man_proc_stat(int ctx_top_bot, int win_shared)
{
    HRESULT hr;

    IPresentStatistics *pres_stat;
    hr = IPresentationManager_GetNextPresentStatistics(
        win_shared ? pres_man_child[ctx_top_bot] : presentation_manager[ctx_top_bot],
        &pres_stat);
    if (hr) {
        err_log("GetNextPresentStatistics failed: %d\n", (int)hr);
        return;
    }

    PresentStatisticsKind pres_kind = IPresentStatistics_GetKind(pres_stat);
    UINT64 pres_id = IPresentStatistics_GetPresentId(pres_stat);
    if (PRINT_PRESENT_STATS)
        err_log("Present stat %d %llu %d\n", ctx_top_bot, (unsigned long long)pres_id, (int)pres_kind);

    switch (pres_kind) {
        case PresentStatisticsKind_PresentStatus: break; {
            IPresentStatusPresentStatistics *pres_stat_stat;
            hr = IPresentStatistics_QueryInterface(pres_stat, &IID_IPresentStatusPresentStatistics, (void **)&pres_stat_stat);
            if (hr) {
                err_log("QueryInterface IPresentStatusPresentStatistics failed: %d\n", (int)hr);
                goto done;
            }

            CompositionFrameId comp_frame_id = IPresentStatusPresentStatistics_GetCompositionFrameId(pres_stat_stat);
            PresentStatus pres_status = IPresentStatusPresentStatistics_GetPresentStatus(pres_stat_stat);

            if (PRINT_PRESENT_STATS)
                err_log("Present status stat %llu %d\n", (unsigned long long)comp_frame_id, (int)pres_status);

            IPresentStatusPresentStatistics_Release(pres_stat_stat);
            break;
        }

        case PresentStatisticsKind_CompositionFrame: break; {
            ICompositionFramePresentStatistics *comp_stat;
            hr = IPresentStatistics_QueryInterface(pres_stat, &IID_ICompositionFramePresentStatistics, (void **)&comp_stat);
            if (hr) {
                err_log("QueryInterface ICompositionFramePresentStatistics failed: %d\n", (int)hr);
                goto done;
            }

            CompositionFrameId comp_frame_id = ICompositionFramePresentStatistics_GetCompositionFrameId(comp_stat);

            UINT disp_inst_count;
            const CompositionFrameDisplayInstance *disp_insts;
            ICompositionFramePresentStatistics_GetDisplayInstanceArray(comp_stat, &disp_inst_count, &disp_insts);

            if (PRINT_PRESENT_STATS)
                err_log("Comp stat %llu %d\n", (unsigned long long)comp_frame_id, (int)disp_inst_count);

            COMPOSITION_FRAME_STATS comp_frame_stats;
            UINT target_count;
            hr = DCompositionGetStatistics(comp_frame_id, &comp_frame_stats, 0, NULL, &target_count);
            if (hr) {
                err_log("DCompositionGetStatistics failed: %d\n", (int)hr);
                goto comp_frame_done;
            } else {
                COMPOSITION_TARGET_ID comp_target_ids[target_count];
                hr = DCompositionGetStatistics(comp_frame_id, &comp_frame_stats, target_count, comp_target_ids, &target_count);
                if (hr) {
                    err_log("DCompositionGetStatistics failed: %d\n", (int)hr);
                    goto comp_frame_done;
                }

                for (int i = 0; i < (int)target_count; ++i) {
                    COMPOSITION_TARGET_STATS comp_target_stats;
                    hr = DCompositionGetTargetStatistics(comp_frame_id, &comp_target_ids[i], &comp_target_stats);
                    if (hr) {
                        err_log("DCompositionGetTargetStatistics failed: %d\n", (int)hr);
                        goto comp_frame_done;
                    }

                    if (PRINT_PRESENT_STATS)
                        if (comp_target_stats.presentTime)
                            err_log("Comp target stat %llu %llu\n", (unsigned long long)comp_target_stats.presentTime, (unsigned long long)comp_target_stats.vblankDuration);
                }
            }

comp_frame_done:
            ICompositionFramePresentStatistics_Release(comp_stat);
            break;
        }

        case PresentStatisticsKind_IndependentFlipFrame: {
            IIndependentFlipFramePresentStatistics *iflip_stat;
            hr = IPresentStatistics_QueryInterface(pres_stat, &IID_IIndependentFlipFramePresentStatistics, (void **)&iflip_stat);
            if (hr) {
                err_log("QueryInterface IIndependentFlipFramePresentStatistics failed: %d\n", (int)hr);
                goto done;
            }

            SystemInterruptTime disp_time;
            IIndependentFlipFramePresentStatistics_GetDisplayedTime(iflip_stat, &disp_time);

            SystemInterruptTime pres_dura;
            IIndependentFlipFramePresentStatistics_GetPresentDuration(iflip_stat, &pres_dura);

            if (PRINT_PRESENT_STATS)
                err_log("I flip frame stat %llu %llu\n", (unsigned long long)disp_time.value, (unsigned long long)pres_dura.value);

            IIndependentFlipFramePresentStatistics_Release(iflip_stat);
            break;
        }

        default:
    }

done:
    IPresentStatistics_Release(pres_stat);
}

int presentation_buffer_get(struct presentation_buffer_t *bufs, int ctx_top_bot, int win_shared, int count_max, int width, int height, int *index)
{
    HANDLE events[count_max];
    int j = ctx_top_bot;
    for (int i = 0; i < count_max; ++i) {
        if (!bufs[i].tex) {
            struct presentation_buffer_t *b = &bufs[i];
            if (presentation_buffer_gen(b, j, win_shared, width, height) != 0) {
                return -1;
            }
            *index = i;
            return 0;
        }

        events[i] = bufs[i].buf_avail_event;
    }

    DWORD res;
    if (win_shared >= 0) {
        enum {
            pres_man_event_lost,
            pres_man_event_stat_avail,
            pres_man_event_count,
        };
        HANDLE pres_man_events[pres_man_event_count];
        if (win_shared) {
            pres_man_events[pres_man_event_lost] = pres_man_child_lost_event[j];
            pres_man_events[pres_man_event_stat_avail] = pres_man_child_stat_avail_event[j];
        } else {
            pres_man_events[pres_man_event_lost] = pres_man_lost_event[j];
            pres_man_events[pres_man_event_stat_avail] = pres_man_stat_avail_event[j];
        }

        while (1) {
            res = WaitForMultipleObjects(pres_man_event_count, pres_man_events, FALSE, 0);
            if (res == WAIT_TIMEOUT) {
                break;
            }

            if (res == WAIT_FAILED) {
                err_log("WaitForMultipleObjects failed: %d\n", (int)GetLastError());
                return -1;
            }

            res -= WAIT_OBJECT_0;
            if (res == pres_man_event_lost) {
                err_log("Presentation manager lost\n");
                ui_compositing = 0;
                return -1;
            }

            if (res == pres_man_event_stat_avail) {
                pres_man_proc_stat(j, win_shared);
                continue;
            }

            break;
        }
    }

    res = WaitForMultipleObjects(count_max, events, FALSE, INFINITE);
    if (res - WAIT_OBJECT_0 >= (DWORD)count_max) {
        return -1;
    }
    res -= WAIT_OBJECT_0;

    int i = res;
    if (bufs[i].width != width || bufs[i].height != height) {
        struct presentation_buffer_t *b = &bufs[i];
        presentation_buffer_delete(b);
        if (presentation_buffer_gen(b, j, win_shared, width, height) != 0) {
            return -1;
        }
    }
    *index = i;
    return 0;
}

int presentation_buffer_present(struct presentation_buffer_t *buf, int ctx_top_bot, int screen_top_bot, int win_shared, int width, int height)
{
    HRESULT hr;

    const int i = ctx_top_bot;
    const int j = screen_top_bot;

    hr = IPresentationSurface_SetBuffer(
        win_shared ? pres_surf_child[j] : presentation_surface[i],
        buf->buf);
    if (hr) {
        err_log("SetBuffer failed: %d\n", (int)hr);
        return -1;
    }

    RECT *rect;
    rect = win_shared ? &src_rect_child[j] : &src_rect[i];
    if (rect->right != width || rect->bottom != height) {
        rect->right = width;
        rect->bottom = height;
        hr = IPresentationSurface_SetSourceRect(
            win_shared ? pres_surf_child[j] : presentation_surface[i],
            rect);
        if (hr) {
            err_log("SetSourceRect failed: %d\n", (int)hr);
            return hr;
        }
    }

    hr = IPresentationManager_Present(win_shared ? pres_man_child[j] : presentation_manager[i]);
    if (hr) {
        err_log("Present %d %d failed: %d\n", win_shared, win_shared ? j : i, (int)hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED) {
            hr = ID3D11Device_GetDeviceRemovedReason(d3d11device[i]);
            err_log("GetDeviceRemovedReason: %d\n", (int)hr);
        }
        ui_compositing = 0;
        return -1;
    }

    return 0;
}

int ui_buffer_present(struct presentation_buffer_t *buf, int width, int height)
{
    int j = SURFACE_UTIL_UI;
    int i = SCREEN_TOP;
    HRESULT hr;

    hr = IPresentationSurface_SetBuffer(pres_surf_util[j], buf->buf);
    if (hr) {
        err_log("SetBuffer failed: %d\n", (int)hr);
        return -1;
    }

    RECT *rect;
    rect = &src_rect_util[j];
    if (rect->right != width || rect->bottom != height) {
        rect->right = width;
        rect->bottom = height;
        hr = IPresentationSurface_SetSourceRect(pres_surf_util[j], rect);
        if (hr) {
            err_log("SetSourceRect failed: %d\n", (int)hr);
            return hr;
        }
    }

    hr = IPresentationManager_Present(pres_man_util[j]);
    if (hr) {
        err_log("Present failed: %d\n", (int)hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED) {
            hr = ID3D11Device_GetDeviceRemovedReason(d3d11device[i]);
            err_log("GetDeviceRemovedReason: %d\n", (int)hr);
        }
        ui_compositing = 0;
        return -1;
    }

    return 0;
}

static bool hide_nk_windows;

int update_hide_ui(void)
{
    int i = SCREEN_TOP;
    if (hide_nk_windows != ui_hide_nk_windows) {
        HRESULT hr;
        int j = SURFACE_UTIL_UI;
        if (ui_hide_nk_windows) {
            hr = dcomp_visual[i]->lpVtbl->RemoveVisual(dcomp_visual[i], (IDCompositionVisual *)dcomp_vis_util[j]);
            if (hr) {
                err_log("RemoveVisual failed: %d\n", (int)hr);
                return hr;
            }
        } else {
            hr = dcomp_visual[i]->lpVtbl->AddVisual(dcomp_visual[i], (IDCompositionVisual *)dcomp_vis_util[j], j == SURFACE_UTIL_BG ? TRUE : FALSE, NULL);
            if (hr) {
                err_log("AddVisual failed: %d\n", (int)hr);
                return hr;
            }
        }
        hr = dcomp_vis_util[j]->lpVtbl->SetContent(dcomp_vis_util[j], ui_hide_nk_windows ? NULL : dcomp_surf_util[j]);
        if (hr) {
            err_log("SetContent failed: %d\n", (int)hr);
            return hr;
        }
        hr = dcomp_device[i]->lpVtbl->Commit(dcomp_device[i]);
        if (hr) {
            err_log("Commit failed: %d\n", (int)hr);
            return hr;
        }

        hide_nk_windows = ui_hide_nk_windows;
    }
    return 0;
}

int render_buffer_delete(struct render_buffer_t *b, int ctx_top_bot)
{
    if (b->gl_handle) {
        // In case of gl device lost (e.g. graphics driver reset) this will crash on NVIDIA.
        wglDXUnregisterObjectNV(gl_d3ddevice[ctx_top_bot], b->gl_handle);
        b->gl_handle = NULL;
    }

    if (b->gl_tex) {
        glDeleteRenderbuffers(1, &b->gl_tex);
        b->gl_tex = 0;
    }

    if (b->tex) {
        ID3D11Texture2D_Release(b->tex);
        b->tex = NULL;
    }

    b->width = 0;
    b->height = 0;

    return 0;
}

int render_buffer_gen(struct render_buffer_t *b, int ctx_top_bot, int width, int height)
{
    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.SampleDesc.Quality = 0;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    tex_desc.MiscFlags = 0;
    tex_desc.CPUAccessFlags = 0;

    HRESULT hr;

    int i = ctx_top_bot;

    hr = ID3D11Device_CreateTexture2D(d3d11device[i], &tex_desc, NULL, &b->tex);
    if (hr) {
        err_log("CreateTexture2D failed: %d\n", (int)hr);
        goto fail;
    }

    glGenRenderbuffers(1, &b->gl_tex);
    b->gl_handle = wglDXRegisterObjectNV(gl_d3ddevice[i], b->tex, b->gl_tex, GL_RENDERBUFFER, WGL_ACCESS_WRITE_DISCARD_NV);
    if (!b->gl_handle) {
        err_log("wglDXRegisterObjectNV failed: %d\n", (int)GetLastError());
        goto fail;
    }

    b->width = width;
    b->height = height;

    return 0;

fail:
    render_buffer_delete(b, i);
    return -1;
}

int render_buffer_get(struct render_buffer_t *b, int ctx_top_bot, int width, int height, GLuint *tex, HANDLE *handle)
{
    int i = ctx_top_bot;

    if (b->width != width || b->height != height) {
        if (render_buffer_delete(b, i) != 0) {
            return -1;
        }

        if (render_buffer_gen(b, i, width, height) != 0) {
            return -1;
        }
    }

    *tex = b->gl_tex;
    *handle = b->gl_handle;

    return 0;
}

static GLuint ui_render_tex;
static int ui_render_width, ui_render_height;
GLuint ui_render_tex_get(int width, int height)
{
    if (ui_render_width == width && ui_render_height == height) {
        return ui_render_tex;
    }

    if (ui_render_tex) {
        glDeleteTextures(1, &ui_render_tex);
        ui_render_tex = 0;
        ui_render_width = 0;
        ui_render_height = 0;
    }

    glGenTextures(1, &ui_render_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ui_render_tex);
    glTexImage2D(
        GL_TEXTURE_2D, 0,
        GL_INT_FORMAT,
        width,
        height, 0,
        GL_FORMAT, GL_UNSIGNED_BYTE,
        0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    ui_render_width = width;
    ui_render_height = height;
    return ui_render_tex;
}

int ui_tex_present(int count_max)
{
    struct render_buffer_t *b = &ui_render_buf;

    int i = SCREEN_TOP;
    int j = SURFACE_UTIL_UI;
    int index_sc;
    struct presentation_buffer_t *bufs = ui_pres_bufs;
    if (presentation_buffer_get(bufs, j, -1, count_max, b->width, b->height, &index_sc) != 0) {
        return -1;
    }

    ID3D11DeviceContext_CopyResource(d3d11device_context[i], (ID3D11Resource *)bufs[index_sc].tex, (ID3D11Resource *)b->tex);

    HRESULT hr;

    hr = IPresentationSurface_SetBuffer(pres_surf_util[j], bufs[index_sc].buf);
    if (hr) {
        err_log("SetBuffer failed: %d\n", (int)hr);
        return -1;
    }

    RECT *rect;
    rect = &src_rect_util[j];
    if (rect->right != b->width || rect->bottom != b->height) {
        rect->right = b->width;
        rect->bottom = b->height;
        hr = IPresentationSurface_SetSourceRect(pres_surf_util[j], rect);
        if (hr) {
            err_log("SetSourceRect failed: %d\n", (int)hr);
            return hr;
        }
    }

    hr = IPresentationManager_Present(pres_man_util[j]);
    if (hr) {
        err_log("Present failed: %d\n", (int)hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED) {
            hr = ID3D11Device_GetDeviceRemovedReason(d3d11device[i]);
            err_log("GetDeviceRemovedReason: %d\n", (int)hr);
        }
        ui_compositing = 0;
        return -1;
    }

    return 0;
}

int presentation_tex_present(int ctx_top_bot, int screen_top_bot, int win_shared, int count_max)
{
    int i = ctx_top_bot;

    struct render_buffer_t *b = &render_buffers[i][screen_top_bot];

    int index_sc;
    struct presentation_buffer_t *bufs = presentation_buffers[i][screen_top_bot];
    if (presentation_buffer_get(bufs, win_shared ? screen_top_bot : i, win_shared, count_max, b->width, b->height, &index_sc) != 0) {
        return -1;
    }

    ID3D11DeviceContext_CopyResource(d3d11device_context[i], (ID3D11Resource *)bufs[index_sc].tex, (ID3D11Resource *)b->tex);

    HRESULT hr;

    hr = IPresentationSurface_SetBuffer(
        win_shared ? pres_surf_child[screen_top_bot] : presentation_surface[i],
        bufs[index_sc].buf);
    if (hr) {
        err_log("SetBuffer failed: %d\n", (int)hr);
        return -1;
    }

    RECT *rect;
    rect = win_shared ? &src_rect_child[screen_top_bot] : &src_rect[i];
    if (rect->right != b->width || rect->bottom != b->height) {
        rect->right = b->width;
        rect->bottom = b->height;
        hr = IPresentationSurface_SetSourceRect(
            win_shared ? pres_surf_child[screen_top_bot] : presentation_surface[i],
            rect);
        if (hr) {
            err_log("SetSourceRect failed: %d\n", (int)hr);
            return hr;
        }
    }

    // err_log("%d %llu\n", win_shared ? screen_top_bot : i, (unsigned long long)IPresentationManager_GetNextPresentId(win_shared ? pres_man_child[screen_top_bot] : presentation_manager[i]));
    hr = IPresentationManager_Present(win_shared ? pres_man_child[screen_top_bot] : presentation_manager[i]);
    if (hr) {
        err_log("Present %d %d failed: %d\n", win_shared, win_shared ? screen_top_bot : i, (int)hr);
        if (hr == DXGI_ERROR_DEVICE_REMOVED) {
            hr = ID3D11Device_GetDeviceRemovedReason(d3d11device[i]);
            err_log("GetDeviceRemovedReason: %d\n", (int)hr);
        }
        ui_compositing = 0;
        return -1;
    }

    return 0;
}
