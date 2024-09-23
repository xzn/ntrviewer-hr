#ifndef RP_DCOMP_H
#define RP_DCOMP_H

#define COBJMACROS
#define WIDL_using_Windows_System
#define WIDL_using_Windows_UI_Composition

#include <d3d11.h>
#include <d2d1_1.h>
#include "Presentation.h"
#include "dcomptypes.h"
#define COMPOSITIONSURFACE_READ 0x0001L
#define COMPOSITIONSURFACE_WRITE 0x0002L
#define COMPOSITIONSURFACE_ALL_ACCESS (COMPOSITIONSURFACE_READ | COMPOSITIONSURFACE_WRITE)

typedef STDMETHODIMP (*DCompositionCreateDevice3_t)(IUnknown *renderingDevice, REFIID iid, void **dcompositionDevice);
typedef STDMETHODIMP (*DCompositionCreateSurfaceHandle_t)(DWORD desiredAccess, SECURITY_ATTRIBUTES *securityAttributes, HANDLE *surfaceHandle);
typedef STDMETHODIMP (*DCompositionGetStatistics_t)(COMPOSITION_FRAME_ID frameId, COMPOSITION_FRAME_STATS *frameStats, UINT targetIdCount, COMPOSITION_TARGET_ID *targetIds, UINT *actualTargetIdCount);
typedef STDMETHODIMP (*DCompositionGetTargetStatistics_t)(COMPOSITION_FRAME_ID frameId, const COMPOSITION_TARGET_ID *targetId, COMPOSITION_TARGET_STATS *targetStats);
typedef STDMETHODIMP (*CreatePresentationFactory_t)(IUnknown  *d3dDevice, REFIID riid, void  **presentationFactory);

static DCompositionCreateDevice3_t DCompositionCreateDevice3;
static DCompositionCreateSurfaceHandle_t DCompositionCreateSurfaceHandle;
static DCompositionGetStatistics_t DCompositionGetStatistics;
static DCompositionGetTargetStatistics_t DCompositionGetTargetStatistics;
static CreatePresentationFactory_t pfn_CreatePresentationFactory;

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

    pfn_CreatePresentationFactory = (CreatePresentationFactory_t)(void *)GetProcAddress(dcomp, "CreatePresentationFactory");
    if (!pfn_CreatePresentationFactory) return -1;

    return 0;
}

const IID IID_IPresentationFactory = { 0x8fb37b58, 0x1d74, 0x4f64, { 0xa4, 0x9c, 0x1f, 0x97, 0xa8, 0x0a, 0x2e, 0xc0 } };
const IID IID_IPresentStatusPresentStatistics = { 0xc9ed2a41, 0x79cb, 0x435e, { 0x96, 0x4e, 0xc8, 0x55, 0x30, 0x55, 0x42, 0x0c } };
const IID IID_IIndependentFlipFramePresentStatistics = { 0x8c93be27, 0xad94, 0x4da0, { 0x8f, 0xd4, 0x24, 0x13, 0x13, 0x2d, 0x12, 0x4e } };
const IID IID_ICompositionFramePresentStatistics = { 0xab41d127, 0xc101, 0x4c0a, { 0x91, 0x1d, 0xf9, 0xf2, 0xe9, 0xd0, 0x8e, 0x64 } };

const IID IID_IDCompositionDevice = { 0xc37ea93a, 0xe7aa, 0x450d, { 0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3 } };
const IID IID_IDCompositionDevice3 = { 0x0987cb06, 0xf916, 0x48bf, { 0x8d, 0x35, 0xce, 0x76, 0x41, 0x78, 0x1b, 0xd9 } };
const IID IID_IDCompositionDesktopDevice = { 0x5f4633fe, 0x1e08, 0x4cb8, { 0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02 } };

typedef interface IDCompositionAffineTransform2DEffect   IDCompositionAffineTransform2DEffect;
typedef interface IDCompositionAnimation                 IDCompositionAnimation;
typedef interface IDCompositionArithmeticCompositeEffect IDCompositionArithmeticCompositeEffect;
typedef interface IDCompositionBlendEffect               IDCompositionBlendEffect;
typedef interface IDCompositionBrightnessEffect          IDCompositionBrightnessEffect;
typedef interface IDCompositionClip                      IDCompositionClip;
typedef interface IDCompositionColorMatrixEffect         IDCompositionColorMatrixEffect;
typedef interface IDCompositionCompositeEffect           IDCompositionCompositeEffect;
typedef interface IDCompositionDevice                    IDCompositionDevice;
typedef interface IDCompositionEffect                    IDCompositionEffect;
typedef interface IDCompositionEffectGroup               IDCompositionEffectGroup;
typedef interface IDCompositionFilterEffect              IDCompositionFilterEffect;
typedef interface IDCompositionGaussianBlurEffect        IDCompositionGaussianBlurEffect;
typedef interface IDCompositionHueRotationEffect         IDCompositionHueRotationEffect;
typedef interface IDCompositionLinearTransferEffect      IDCompositionLinearTransferEffect;
typedef interface IDCompositionMatrixTransform           IDCompositionMatrixTransform;
typedef interface IDCompositionMatrixTransform3D         IDCompositionMatrixTransform3D;
typedef interface IDCompositionRectangleClip             IDCompositionRectangleClip;
typedef interface IDCompositionRotateTransform           IDCompositionRotateTransform;
typedef interface IDCompositionRotateTransform3D         IDCompositionRotateTransform3D;
typedef interface IDCompositionSaturationEffect          IDCompositionSaturationEffect;
typedef interface IDCompositionScaleTransform            IDCompositionScaleTransform;
typedef interface IDCompositionScaleTransform3D          IDCompositionScaleTransform3D;
typedef interface IDCompositionShadowEffect              IDCompositionShadowEffect;
typedef interface IDCompositionSkewTransform             IDCompositionSkewTransform;
typedef interface IDCompositionSurface                   IDCompositionSurface;
typedef interface IDCompositionTableTransferEffect       IDCompositionTableTransferEffect;
typedef interface IDCompositionTarget                    IDCompositionTarget;
typedef interface IDCompositionTransform                 IDCompositionTransform;
typedef interface IDCompositionTransform3D               IDCompositionTransform3D;
typedef interface IDCompositionTranslateTransform        IDCompositionTranslateTransform;
typedef interface IDCompositionTranslateTransform3D      IDCompositionTranslateTransform3D;
typedef interface IDCompositionTurbulenceEffect          IDCompositionTurbulenceEffect;
typedef interface IDCompositionVirtualSurface            IDCompositionVirtualSurface;
typedef interface IDCompositionVisual                    IDCompositionVisual;

typedef interface IDCompositionDesktopDevice  IDCompositionDesktopDevice;
typedef interface IDCompositionDevice2        IDCompositionDevice2;
typedef interface IDCompositionDeviceDebug    IDCompositionDeviceDebug;
typedef interface IDCompositionSurfaceFactory IDCompositionSurfaceFactory;
typedef interface IDCompositionVisual2        IDCompositionVisual2;
typedef interface IDCompositionVisualDebug    IDCompositionVisualDebug;

typedef interface IDCompositionDevice3 IDCompositionDevice3;
typedef interface IDCompositionVisual3 IDCompositionVisual3;

#define IUNKNOWN_METHODS \
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject); \
    STDMETHOD_(ULONG, AddRef)(THIS); \
    STDMETHOD_(ULONG, Release)(THIS);

#undef INTERFACE
#define INTERFACE IDCompositionDevice
DECLARE_INTERFACE(IDCompositionDevice)
{
    IUNKNOWN_METHODS
    STDMETHOD(Commit)(THIS);
    STDMETHOD(WaitForCommitCompletion)(THIS);
    STDMETHOD(GetFrameStatistics)(THIS_ DCOMPOSITION_FRAME_STATISTICS *statistics);
    STDMETHOD(CreateTargetForHwnd)(THIS_ HWND hwnd, BOOL topmost, IDCompositionTarget **target);
    STDMETHOD(CreateVisual)(THIS_ IDCompositionVisual **visual);
    STDMETHOD(CreateSurface)(THIS_ UINT width, UINT height, DXGI_FORMAT pixelFormat, DXGI_ALPHA_MODE alphaMode, IDCompositionSurface **surface);
    STDMETHOD(CreateVirtualSurface)(THIS_ UINT initialWidth, UINT initialHeight, DXGI_FORMAT pixelFormat, DXGI_ALPHA_MODE alphaMode, IDCompositionVirtualSurface **virtualSurface);
    STDMETHOD(CreateSurfaceFromHandle)(THIS_ HANDLE handle, IUnknown **surface);
    STDMETHOD(CreateSurfaceFromHwnd)(THIS_ HWND hwnd, IUnknown **surface);
    STDMETHOD(CreateTranslateTransform)(THIS_ IDCompositionTranslateTransform **translateTransform);
    STDMETHOD(CreateScaleTransform)(THIS_ IDCompositionScaleTransform **scaleTransform);
    STDMETHOD(CreateRotateTransform)(THIS_ IDCompositionRotateTransform **rotateTransform);
    STDMETHOD(CreateSkewTransform)(THIS_ IDCompositionSkewTransform **skewTransform);
    STDMETHOD(CreateMatrixTransform)(THIS_ IDCompositionMatrixTransform **matrixTransform);
    STDMETHOD(CreateTransformGroup)(THIS_ IDCompositionTransform **transforms, UINT elements, IDCompositionTransform **transformGroup);
    STDMETHOD(CreateTranslateTransform3D)(THIS_ IDCompositionTranslateTransform3D **translateTransform3D);
    STDMETHOD(CreateScaleTransform3D)(THIS_ IDCompositionScaleTransform3D **scaleTransform3D);
    STDMETHOD(CreateRotateTransform3D)(THIS_ IDCompositionRotateTransform3D **rotateTransform3D);
    STDMETHOD(CreateMatrixTransform3D)(THIS_ IDCompositionMatrixTransform3D **matrixTransform3D);
    STDMETHOD(CreateTransform3DGroup)(THIS_ IDCompositionTransform3D **transforms3D, UINT elements, IDCompositionTransform3D **transform3DGroup);
    STDMETHOD(CreateEffectGroup)(THIS_ IDCompositionEffectGroup **effectGroup);
    STDMETHOD(CreateRectangleClip)(THIS_ IDCompositionRectangleClip **clip);
    STDMETHOD(CreateAnimation)(THIS_ IDCompositionAnimation **animation);
    STDMETHOD(CheckDeviceState)(THIS_ BOOL *pfValid);
};

#define IDCompositionVisual_METHODS \
    STDMETHOD(SetOffsetX1)(THIS_ IDCompositionAnimation*); \
    STDMETHOD(SetOffsetX2)(THIS_ float); \
    STDMETHOD(SetOffsetY1)(THIS_ IDCompositionAnimation*); \
    STDMETHOD(SetOffsetY2)(THIS_ float); \
    STDMETHOD(SetTransform1)(THIS_ IDCompositionTransform*); \
    STDMETHOD(SetTransform2)(THIS_ D2D_MATRIX_3X2_F*); \
    STDMETHOD(SetTransformParent)(THIS_ IDCompositionVisual*); \
    STDMETHOD(SetEffect)(THIS_ IDCompositionEffect*); \
    STDMETHOD(SetBitmapInterpolationMode)(THIS_ enum DCOMPOSITION_BITMAP_INTERPOLATION_MODE); \
    STDMETHOD(SetBorderMode)(THIS_ enum DCOMPOSITION_BORDER_MODE); \
    STDMETHOD(SetClip1)(THIS_ IDCompositionClip*); \
    STDMETHOD(SetClip2)(THIS_ D2D_RECT_F*); \
    STDMETHOD(SetContent)(THIS_ IUnknown*); \
    STDMETHOD(AddVisual)(THIS_ IDCompositionVisual*,BOOL,IDCompositionVisual*); \
    STDMETHOD(RemoveVisual)(THIS_ IDCompositionVisual*); \
    STDMETHOD(RemoveAllVisuals)(THIS); \
    STDMETHOD(SetCompositeMode)(THIS_ enum DCOMPOSITION_COMPOSITE_MODE);

#undef INTERFACE
#define INTERFACE IDCompositionVisual
DECLARE_INTERFACE(IDCompositionVisual)
{
    IUNKNOWN_METHODS
    IDCompositionVisual_METHODS
};

#undef INTERFACE
#define INTERFACE IDCompositionTarget
DECLARE_INTERFACE(IDCompositionTarget)
{
    IUNKNOWN_METHODS
    STDMETHOD(SetRoot)(THIS_ IDCompositionVisual*);
};

#define IDCompositionDevice2_METHODS \
    STDMETHOD(Commit)(THIS); \
    STDMETHOD(WaitForCommitCompletion)(THIS); \
    STDMETHOD(GetFrameStatistics)(THIS_ DCOMPOSITION_FRAME_STATISTICS *statistics); \
    STDMETHOD(CreateVisual)(THIS_ IDCompositionVisual2 **visual); \
    STDMETHOD(CreateSurfaceFactory)(THIS_ IUnknown *renderingDevice, IDCompositionSurfaceFactory **surfaceFactory); \
    STDMETHOD(CreateSurface)(THIS_ UINT width, UINT height, DXGI_FORMAT pixelFormat, DXGI_ALPHA_MODE alphaMode, IDCompositionSurface **surface); \
    STDMETHOD(CreateVirtualSurface)(THIS_ UINT initialWidth, UINT initialHeight, DXGI_FORMAT pixelFormat, DXGI_ALPHA_MODE alphaMode, IDCompositionVirtualSurface **virtualSurface); \
    STDMETHOD(CreateTranslateTransform)(THIS_ IDCompositionTranslateTransform **translateTransform); \
    STDMETHOD(CreateScaleTransform)(THIS_ IDCompositionScaleTransform **scaleTransform); \
    STDMETHOD(CreateRotateTransform)(THIS_ IDCompositionRotateTransform **rotateTransform); \
    STDMETHOD(CreateSkewTransform)(THIS_ IDCompositionSkewTransform **skewTransform); \
    STDMETHOD(CreateMatrixTransform)(THIS_ IDCompositionMatrixTransform **matrixTransform); \
    STDMETHOD(CreateTransformGroup)(THIS_ IDCompositionTransform **transforms, UINT elements, IDCompositionTransform **transformGroup); \
    STDMETHOD(CreateTranslateTransform3D)(THIS_ IDCompositionTranslateTransform3D **translateTransform3D); \
    STDMETHOD(CreateScaleTransform3D)(THIS_ IDCompositionScaleTransform3D **scaleTransform3D); \
    STDMETHOD(CreateRotateTransform3D)(THIS_ IDCompositionRotateTransform3D **rotateTransform3D); \
    STDMETHOD(CreateMatrixTransform3D)(THIS_ IDCompositionMatrixTransform3D **matrixTransform3D); \
    STDMETHOD(CreateTransform3DGroup)(THIS_ IDCompositionTransform3D **transforms3D, UINT elements, IDCompositionTransform3D **transform3DGroup); \
    STDMETHOD(CreateEffectGroup)(THIS_ IDCompositionEffectGroup **effectGroup); \
    STDMETHOD(CreateRectangleClip)(THIS_ IDCompositionRectangleClip **clip); \
    STDMETHOD(CreateAnimation)(THIS_ IDCompositionAnimation **animation);

#undef INTERFACE
#define INTERFACE IDCompositionDesktopDevice
DECLARE_INTERFACE(IDCompositionDesktopDevice)
{
    IUNKNOWN_METHODS
    IDCompositionDevice2_METHODS
    STDMETHOD(CreateTargetForHwnd)(THIS_ HWND hwnd, BOOL topmost, IDCompositionTarget **target);
    STDMETHOD(CreateSurfaceFromHandle)(THIS_ HANDLE handle, IUnknown **surface);
    STDMETHOD(CreateSurfaceFromHwnd)(THIS_ HWND hwnd, IUnknown **surface);
};

#undef INTERFACE
#define INTERFACE IDCompositionVisual2
DECLARE_INTERFACE(IDCompositionVisual2)
{
    IUNKNOWN_METHODS
    IDCompositionVisual_METHODS
    STDMETHOD(SetOpacityMode)(THIS_ enum DCOMPOSITION_OPACITY_MODE mode);
    STDMETHOD(SetBackFaceVisibility)(THIS_ enum DCOMPOSITION_BACKFACE_VISIBILITY visibility);
};

#undef INTERFACE
#define INTERFACE IDCompositionDevice3
DECLARE_INTERFACE(IDCompositionDevice3)
{
    IUNKNOWN_METHODS
    IDCompositionDevice2_METHODS
    STDMETHOD(CreateGaussianBlurEffect)(THIS_ IDCompositionGaussianBlurEffect **gaussianBlurEffect);
    STDMETHOD(CreateBrightnessEffect)(THIS_ IDCompositionBrightnessEffect **brightnessEffect);
    STDMETHOD(CreateColorMatrixEffect)(THIS_ IDCompositionColorMatrixEffect **colorMatrixEffect);
    STDMETHOD(CreateShadowEffect)(THIS_ IDCompositionShadowEffect **shadowEffect);
    STDMETHOD(CreateHueRotationEffect)(THIS_ IDCompositionHueRotationEffect **hueRotationEffect);
    STDMETHOD(CreateSaturationEffect)(THIS_ IDCompositionSaturationEffect **saturationEffect);
    STDMETHOD(CreateTurbulenceEffect)(THIS_ IDCompositionTurbulenceEffect **turbulenceEffect);
    STDMETHOD(CreateLinearTransferEffect)(THIS_ IDCompositionLinearTransferEffect **linearTransferEffect);
    STDMETHOD(CreateTableTransferEffect)(THIS_ IDCompositionTableTransferEffect **tableTransferEffect);
    STDMETHOD(CreateCompositeEffect)(THIS_ IDCompositionCompositeEffect **compositeEffect);
    STDMETHOD(CreateBlendEffect)(THIS_ IDCompositionBlendEffect **blendEffect);
    STDMETHOD(CreateArithmeticCompositeEffect)(THIS_ IDCompositionArithmeticCompositeEffect **arithmeticCompositeEffect);
    STDMETHOD(CreateAffineTransform2DEffect)(THIS_ IDCompositionAffineTransform2DEffect **affineTransform2dEffect);
};

#include <windows.system.h>

typedef IDispatcherQueue *PDISPATCHERQUEUE;
typedef IDispatcherQueueController *PDISPATCHERQUEUECONTROLLER;

enum DISPATCHERQUEUE_THREAD_APARTMENTTYPE {
    DQTAT_COM_NONE = 0,
    DQTAT_COM_ASTA = 1,
    DQTAT_COM_STA  = 2
};

enum DISPATCHERQUEUE_THREAD_TYPE {
    DQTYPE_THREAD_DEDICATED = 1,
    DQTYPE_THREAD_CURRENT   = 2
};

struct DispatcherQueueOptions {
    DWORD                                     dwSize;
    enum DISPATCHERQUEUE_THREAD_TYPE          threadType;
    enum DISPATCHERQUEUE_THREAD_APARTMENTTYPE apartmentType;
};

EXTERN_C HRESULT WINAPI CreateDispatcherQueueController(struct DispatcherQueueOptions options, PDISPATCHERQUEUECONTROLLER *dispatcherQueueController);

#include <windows.ui.composition.h>

typedef interface IDesktopWindowTarget IDesktopWindowTarget;
const WCHAR InterfaceName_Windows_UI_Composition_Desktop_IDesktopWindowTarget[] = L"Windows.UI.Composition.Desktop.IDesktopWindowTarget";
typedef struct IDesktopWindowTargetVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IDesktopWindowTarget *This,
        REFIID riid,
        void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(IDesktopWindowTarget *This);
    ULONG (STDMETHODCALLTYPE *Release)(IDesktopWindowTarget *This);
    HRESULT (STDMETHODCALLTYPE *GetIids)(IDesktopWindowTarget *This,
        ULONG *iidCount,
        IID **iids);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(IDesktopWindowTarget *This,
        HSTRING *className);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(IDesktopWindowTarget *This,
        TrustLevel *trustLevel);
    HRESULT (STDMETHODCALLTYPE *get_IsTopmost)(IDesktopWindowTarget *This,
        boolean *value);
} IDesktopWindowTargetVtbl;

interface IDesktopWindowTarget
{
    CONST_VTBL struct IDesktopWindowTargetVtbl *lpVtbl;
};

#define IDesktopWindowTarget_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))
#define IDesktopWindowTarget_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))
#define IDesktopWindowTarget_Release(This) \
    ((This)->lpVtbl->Release(This))
#define IDesktopWindowTarget_GetIids(This, iidCount, iids) \
    ((This)->lpVtbl->GetIids(This, iidCount, iids))
#define IDesktopWindowTarget_GetRuntimeClassName(This, className) \
    ((This)->lpVtbl->GetRuntimeClassName(This, className))
#define IDesktopWindowTarget_GetTrustLevel(This, trustLevel) \
    ((This)->lpVtbl->GetTrustLevel(This, trustLevel))
#define IDesktopWindowTarget_get_IsTopmost(This, value) \
    ((This)->lpVtbl->get_IsTopmost(This, value))
const IID IID_IDesktopWindowTarget = { 0x6329d6ca, 0x3366, 0x490e, { 0x9d, 0xb3, 0x25, 0x31, 0x29, 0x29, 0xac, 0x51 } };

#undef INTERFACE
#define INTERFACE ICompositorDesktopInterop
typedef interface INTERFACE INTERFACE;
DECLARE_INTERFACE(ICompositorDesktopInterop)
{
    IUNKNOWN_METHODS
    STDMETHOD(CreateDesktopWindowTarget)(THIS_ HWND hwndTarget, BOOL isTopmost, IDesktopWindowTarget **result);
    STDMETHOD(EnsureOnThread)(THIS_ DWORD threadId);
};
const IID IID_ICompositorDesktopInterop = { 0x29e691fa, 0x4567, 0x4dca, { 0xb3, 0x19, 0xd0, 0xf2, 0x07, 0xeb, 0x68, 0x07 } };

#include <windows.ui.composition.interop.h>

#endif
