#ifndef UI_COMPOSITOR_CSC_H
#define UI_COMPOSITOR_CSC_H

#define COBJMACROS
#define INITGUID
#include "const.h"
#include "rp_syn.h"
#include "ui_common_sdl.h"

#include <dxgi1_2.h>

#define CHECK_AND_RELEASE(p) do { \
    if (p) { \
        IUnknown_Release(p); \
        (p) = NULL; \
    } \
} while (0)

extern IDXGIAdapter1 *dxgi_adapter;
extern IDXGIFactory2 *dxgi_factory;

enum {
    SURFACE_UTIL_BG,
    SURFACE_UTIL_UI,
    SURFACE_UTIL_COUNT,
};

#define COMPAT_PRESENATTION_BUFFER_COUNT_PER_SCREEN (3)
#define PRESENATTION_BUFFER_COUNT_PER_SCREEN (8)

#include <stdbool.h>

#include <d3d11.h>
#include <d2d1_1.h>

#define IUNKNOWN_METHODS \
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, void **ppvObject); \
    STDMETHOD_(ULONG, AddRef)(THIS); \
    STDMETHOD_(ULONG, Release)(THIS);

typedef interface IPresentationFactory IPresentationFactory;
typedef interface IPresentationManager IPresentationManager;
typedef interface IPresentationBuffer IPresentationBuffer;
typedef interface IPresentationSurface IPresentationSurface;
typedef interface IPresentStatistics IPresentStatistics;
typedef interface IPresentStatusPresentStatistics IPresentStatusPresentStatistics;
typedef interface ICompositionFramePresentStatistics ICompositionFramePresentStatistics;
typedef interface IIndependentFlipFramePresentStatistics IIndependentFlipFramePresentStatistics;
typedef interface IDCompositionAffineTransform2DEffect IDCompositionAffineTransform2DEffect;
typedef interface IDCompositionAnimation IDCompositionAnimation;
typedef interface IDCompositionArithmeticCompositeEffect IDCompositionArithmeticCompositeEffect;
typedef interface IDCompositionBlendEffect IDCompositionBlendEffect;
typedef interface IDCompositionBrightnessEffect IDCompositionBrightnessEffect;
typedef interface IDCompositionClip IDCompositionClip;
typedef interface IDCompositionColorMatrixEffect IDCompositionColorMatrixEffect;
typedef interface IDCompositionCompositeEffect IDCompositionCompositeEffect;
typedef interface IDCompositionDevice IDCompositionDevice;
typedef interface IDCompositionEffect IDCompositionEffect;
typedef interface IDCompositionEffectGroup IDCompositionEffectGroup;
typedef interface IDCompositionFilterEffect IDCompositionFilterEffect;
typedef interface IDCompositionGaussianBlurEffect IDCompositionGaussianBlurEffect;
typedef interface IDCompositionHueRotationEffect IDCompositionHueRotationEffect;
typedef interface IDCompositionLinearTransferEffect IDCompositionLinearTransferEffect;
typedef interface IDCompositionMatrixTransform IDCompositionMatrixTransform;
typedef interface IDCompositionMatrixTransform3D IDCompositionMatrixTransform3D;
typedef interface IDCompositionRectangleClip IDCompositionRectangleClip;
typedef interface IDCompositionRotateTransform IDCompositionRotateTransform;
typedef interface IDCompositionRotateTransform3D IDCompositionRotateTransform3D;
typedef interface IDCompositionSaturationEffect IDCompositionSaturationEffect;
typedef interface IDCompositionScaleTransform IDCompositionScaleTransform;
typedef interface IDCompositionScaleTransform3D IDCompositionScaleTransform3D;
typedef interface IDCompositionShadowEffect IDCompositionShadowEffect;
typedef interface IDCompositionSkewTransform IDCompositionSkewTransform;
typedef interface IDCompositionSurface IDCompositionSurface;
typedef interface IDCompositionTableTransferEffect IDCompositionTableTransferEffect;
typedef interface IDCompositionTarget IDCompositionTarget;
typedef interface IDCompositionTransform IDCompositionTransform;
typedef interface IDCompositionTransform3D IDCompositionTransform3D;
typedef interface IDCompositionTranslateTransform IDCompositionTranslateTransform;
typedef interface IDCompositionTranslateTransform3D IDCompositionTranslateTransform3D;
typedef interface IDCompositionTurbulenceEffect IDCompositionTurbulenceEffect;
typedef interface IDCompositionVirtualSurface IDCompositionVirtualSurface;
typedef interface IDCompositionVisual IDCompositionVisual;
typedef interface IDCompositionDesktopDevice IDCompositionDesktopDevice;
typedef interface IDCompositionDevice2 IDCompositionDevice2;
typedef interface IDCompositionDeviceDebug IDCompositionDeviceDebug;
typedef interface IDCompositionSurfaceFactory IDCompositionSurfaceFactory;
typedef interface IDCompositionVisual2 IDCompositionVisual2;
typedef interface IDCompositionVisualDebug IDCompositionVisualDebug;
typedef interface IDCompositionDevice3 IDCompositionDevice3;
typedef interface IDCompositionVisual3 IDCompositionVisual3;

enum DCOMPOSITION_BITMAP_INTERPOLATION_MODE {
    DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR = 0,
    DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR = 1,
    DCOMPOSITION_BITMAP_INTERPOLATION_MODE_INHERIT = 0xffffffff
};

enum DCOMPOSITION_BORDER_MODE {
    DCOMPOSITION_BORDER_MODE_SOFT = 0,
    DCOMPOSITION_BORDER_MODE_HARD = 1,
    DCOMPOSITION_BORDER_MODE_INHERIT = 0xffffffff
};

enum DCOMPOSITION_COMPOSITE_MODE {
    DCOMPOSITION_COMPOSITE_MODE_SOURCE_OVER = 0,
    DCOMPOSITION_COMPOSITE_MODE_DESTINATION_INVERT = 1,
    DCOMPOSITION_COMPOSITE_MODE_MIN_BLEND = 2,
    DCOMPOSITION_COMPOSITE_MODE_INHERIT = 0xffffffff
};

enum DCOMPOSITION_BACKFACE_VISIBILITY {
    DCOMPOSITION_BACKFACE_VISIBILITY_VISIBLE = 0,
    DCOMPOSITION_BACKFACE_VISIBILITY_HIDDEN = 1,
    DCOMPOSITION_BACKFACE_VISIBILITY_INHERIT = 0xffffffff
};

enum DCOMPOSITION_OPACITY_MODE {
    DCOMPOSITION_OPACITY_MODE_LAYER = 0,
    DCOMPOSITION_OPACITY_MODE_MULTIPLY = 1,
    DCOMPOSITION_OPACITY_MODE_INHERIT = 0xffffffff
};

typedef struct D3D11_FEATURE_DATA_DISPLAYABLE {
    BOOL DisplayableTexture;
    D3D11_SHARED_RESOURCE_TIER SharedResourceTier;
} D3D11_FEATURE_DATA_DISPLAYABLE;

typedef struct SystemInterruptTime {
    UINT64 value;
} SystemInterruptTime;

typedef enum PresentStatisticsKind {
    PresentStatisticsKind_PresentStatus = 1,
    PresentStatisticsKind_CompositionFrame = 2,
    PresentStatisticsKind_IndependentFlipFrame = 3
} PresentStatisticsKind;

typedef struct {
    LARGE_INTEGER lastFrameTime;
    DXGI_RATIONAL currentCompositionRate;
    LARGE_INTEGER currentTime;
    LARGE_INTEGER timeFrequency;
    LARGE_INTEGER nextEstimatedFrameTime;
} DCOMPOSITION_FRAME_STATISTICS;

typedef struct PresentationTransform {
    float M11;
    float M12;
    float M21;
    float M22;
    float M31;
    float M32;
} PresentationTransform;

typedef ULONG64 COMPOSITION_FRAME_ID;

typedef struct COMPOSITION_FRAME_STATS {
    UINT64 startTime;
    UINT64 targetTime;
    UINT64 framePeriod;
} COMPOSITION_FRAME_STATS;

typedef struct COMPOSITION_TARGET_ID {
    LUID displayAdapterLuid;
    LUID renderAdapterLuid;
    UINT vidPnSourceId;
    UINT vidPnTargetId;
    UINT uniqueId;
} COMPOSITION_TARGET_ID;

typedef struct COMPOSITION_STATS {
    UINT presentCount;
    UINT refreshCount;
    UINT virtualRefreshCount;
    UINT64 time;
} COMPOSITION_STATS;

typedef struct COMPOSITION_TARGET_STATS {
    UINT outstandingPresents;
    UINT64 presentTime;
    UINT64 vblankDuration;

    COMPOSITION_STATS presentedStats;
    COMPOSITION_STATS completedStats;
} COMPOSITION_TARGET_STATS;

#undef INTERFACE
#define INTERFACE IPresentationFactory
DECLARE_INTERFACE(IPresentationFactory)
{
    IUNKNOWN_METHODS

    STDMETHOD_(boolean, IsPresentationSupported)(THIS);
    STDMETHOD_(boolean, IsPresentationSupportedWithIndependentFlip)(THIS);
    STDMETHOD(CreatePresentationManager)(THIS_ IPresentationManager **ppPresentationManager);
};

#undef INTERFACE
#define INTERFACE IPresentationManager
DECLARE_INTERFACE(IPresentationManager)
{
    IUNKNOWN_METHODS

    STDMETHOD(AddBufferFromResource)(THIS_ IUnknown *resource, IPresentationBuffer **presentationBuffer);
    STDMETHOD(CreatePresentationSurface)(THIS_ HANDLE compositionSurfaceHandle, IPresentationSurface **presentationSurface);
    STDMETHOD_(UINT64, GetNextPresentId)(IPresentationManager * This);
    STDMETHOD(SetTargetTime)(THIS_ SystemInterruptTime targetTime);
    STDMETHOD(SetPreferredPresentDuration)(THIS_ SystemInterruptTime preferredDuration, SystemInterruptTime deviationTolerance);
    STDMETHOD(ForceVSyncInterrupt)(THIS_ boolean forceVsyncInterrupt);
    STDMETHOD(Present)(IPresentationManager * This);
    STDMETHOD(GetPresentRetiringFence)(THIS_ REFIID riid, void **fence);
    STDMETHOD(CancelPresentsFrom)(THIS_ UINT64 presentIdToCancelFrom);
    STDMETHOD(GetLostEvent)(THIS_ HANDLE *lostEventHandle);
    STDMETHOD(GetPresentStatisticsAvailableEvent)(THIS_ HANDLE *presentStatisticsAvailableEventHandle);
    STDMETHOD(EnablePresentStatisticsKind)(THIS_ PresentStatisticsKind presentStatisticsKind, boolean enabled);
    STDMETHOD(GetNextPresentStatistics)(THIS_ IPresentStatistics **nextPresentStatistics);
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
#define INTERFACE IDCompositionVisual2
DECLARE_INTERFACE(IDCompositionVisual2)
{
    IUNKNOWN_METHODS
    IDCompositionVisual_METHODS
    STDMETHOD(SetOpacityMode)(THIS_ enum DCOMPOSITION_OPACITY_MODE mode);
    STDMETHOD(SetBackFaceVisibility)(THIS_ enum DCOMPOSITION_BACKFACE_VISIBILITY visibility);
};

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

#undef INTERFACE
#define INTERFACE IDCompositionTarget
DECLARE_INTERFACE(IDCompositionTarget)
{
    IUNKNOWN_METHODS
    STDMETHOD(SetRoot)(THIS_ IDCompositionVisual*);
};

#undef INTERFACE
#define INTERFACE IPresentationSurface
DECLARE_INTERFACE(IPresentationSurface)
{
    IUNKNOWN_METHODS
    STDMETHOD_(void, SetTag)(THIS_ UINT_PTR tag);
    STDMETHOD(SetBuffer)(THIS_ IPresentationBuffer *presentationBuffer);
    STDMETHOD(SetColorSpace)(THIS_ DXGI_COLOR_SPACE_TYPE colorSpace);
    STDMETHOD(SetAlphaMode)(THIS_ DXGI_ALPHA_MODE alphaMode);
    STDMETHOD(SetSourceRect)(THIS_ const RECT *sourceRect);
    STDMETHOD(SetTransform)(THIS_ PresentationTransform *transform);
    STDMETHOD(RestrictToOutput)(THIS_ IUnknown *output);
    STDMETHOD(SetDisableReadback)(THIS_ boolean value);
    STDMETHOD(SetLetterboxingMargins)(THIS_ float leftLetterboxSize, float topLetterboxSize, float rightLetterboxSize, float bottomLetterboxSize);
};

#undef INTERFACE
#define INTERFACE IPresentationBuffer
DECLARE_INTERFACE(IPresentationBuffer)
{
    IUNKNOWN_METHODS

    STDMETHOD(GetAvailableEvent)(THIS_ HANDLE *availableEventHandle);
    STDMETHOD(IsAvailable)(THIS_ boolean *isAvailable);
};

#ifdef COBJMACROS

#define IPresentationFactory_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IPresentationFactory_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IPresentationFactory_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IPresentationFactory_IsPresentationSupported(This) \
    ((This)->lpVtbl->IsPresentationSupported(This))

#define IPresentationFactory_IsPresentationSupportedWithIndependentFlip(This) \
    ((This)->lpVtbl->IsPresentationSupportedWithIndependentFlip(This))

#define IPresentationFactory_CreatePresentationManager(This, ppPresentationManager) \
    ((This)->lpVtbl->CreatePresentationManager(This, ppPresentationManager))

#define IPresentationManager_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) )

#define IPresentationManager_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) )

#define IPresentationManager_Release(This)	\
    ( (This)->lpVtbl -> Release(This) )

#define IPresentationManager_AddBufferFromResource(This, resource, presentationBuffer) \
    ((This)->lpVtbl->AddBufferFromResource(This, resource, presentationBuffer))

#define IPresentationManager_CreatePresentationSurface(This, compositionSurfaceHandle, presentationSurface) \
    ((This)->lpVtbl->CreatePresentationSurface(This, compositionSurfaceHandle, presentationSurface))

#define IPresentationManager_GetNextPresentId(This) \
    ((This)->lpVtbl->GetNextPresentId(This))

#define IPresentationManager_SetTargetTime(This, targetTime) \
    ((This)->lpVtbl->SetTargetTime(This, targetTime))

#define IPresentationManager_SetPreferredPresentDuration(This, preferredDuration, deviationTolerance) \
    ((This)->lpVtbl->SetPreferredPresentDuration(This, preferredDuration, deviationTolerance))

#define IPresentationManager_ForceVSyncInterrupt(This, forceVsyncInterrupt) \
    ((This)->lpVtbl->ForceVSyncInterrupt(This, forceVsyncInterrupt))

#define IPresentationManager_Present(This) \
    ((This)->lpVtbl->Present(This))

#define IPresentationManager_GetPresentRetiringFence(This, riid, fence) \
    ((This)->lpVtbl->GetPresentRetiringFence(This, riid, fence))

#define IPresentationManager_CancelPresentsFrom(This, presentIdToCancelFrom) \
    ((This)->lpVtbl->CancelPresentsFrom(This, presentIdToCancelFrom))

#define IPresentationManager_GetLostEvent(This, lostEventHandle) \
    ((This)->lpVtbl->GetLostEvent(This, lostEventHandle))

#define IPresentationManager_GetPresentStatisticsAvailableEvent(This, presentStatisticsAvailableEventHandle) \
    ((This)->lpVtbl->GetPresentStatisticsAvailableEvent(This, presentStatisticsAvailableEventHandle))

#define IPresentationManager_EnablePresentStatisticsKind(This, presentStatisticsKind, enabled) \
    ((This)->lpVtbl->EnablePresentStatisticsKind(This, presentStatisticsKind, enabled))

#define IPresentationManager_GetNextPresentStatistics(This, nextPresentStatistics) \
    ((This)->lpVtbl->GetNextPresentStatistics(This, nextPresentStatistics))

#define IPresentationSurface_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IPresentationSurface_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IPresentationSurface_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IPresentationSurface_SetTag(This, tag) \
    ((This)->lpVtbl->SetTag(This, tag))

#define IPresentationSurface_SetBuffer(This, presentationBuffer) \
    ((This)->lpVtbl->SetBuffer(This, presentationBuffer))

#define IPresentationSurface_SetColorSpace(This, colorSpace) \
    ((This)->lpVtbl->SetColorSpace(This, colorSpace))

#define IPresentationSurface_SetAlphaMode(This, alphaMode) \
    ((This)->lpVtbl->SetAlphaMode(This, alphaMode))

#define IPresentationSurface_SetSourceRect(This, sourceRect) \
    ((This)->lpVtbl->SetSourceRect(This, sourceRect))

#define IPresentationSurface_SetTransform(This, transform) \
    ((This)->lpVtbl->SetTransform(This, transform))

#define IPresentationSurface_RestrictToOutput(This, output) \
    ((This)->lpVtbl->RestrictToOutput(This, output))

#define IPresentationSurface_SetDisableReadback(This, value) \
    ((This)->lpVtbl->SetDisableReadback(This, value))

#define IPresentationSurface_SetLetterboxingMargins(This, leftLetterboxSize, topLetterboxSize, rightLetterboxSize, bottomLetterboxSize) \
    ((This)->lpVtbl->SetLetterboxingMargins(This, leftLetterboxSize, topLetterboxSize, rightLetterboxSize, bottomLetterboxSize))

#define IPresentationBuffer_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IPresentationBuffer_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IPresentationBuffer_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IPresentationBuffer_GetAvailableEvent(This, availableEventHandle) \
    ((This)->lpVtbl->GetAvailableEvent(This, availableEventHandle))

#define IPresentationBuffer_IsAvailable(This, isAvailable) \
    ((This)->lpVtbl->IsAvailable(This, isAvailable))

#endif

#define IPRESENTSTATISTICS_METHODS \
    STDMETHOD_(UINT64, GetPresentId)(THIS); \
    STDMETHOD_(PresentStatisticsKind, GetKind)(THIS);

#undef INTERFACE
#define INTERFACE IPresentStatistics
DECLARE_INTERFACE(IPresentStatistics)
{
    IUNKNOWN_METHODS
    IPRESENTSTATISTICS_METHODS
};

#ifdef COBJMACROS

#define IPresentStatistics_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IPresentStatistics_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IPresentStatistics_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IPresentStatistics_GetPresentId(This) \
    ((This)->lpVtbl->GetPresentId(This))

#define IPresentStatistics_GetKind(This) \
    ((This)->lpVtbl->GetKind(This))

#endif

typedef UINT64 CompositionFrameId;
typedef enum PresentStatus {
    PresentStatus_Queued = 0,
    PresentStatus_Skipped = 1,
    PresentStatus_Canceled = 2
} PresentStatus;

#undef INTERFACE
#define INTERFACE IPresentStatusPresentStatistics
DECLARE_INTERFACE(IPresentStatusPresentStatistics)
{
    IUNKNOWN_METHODS
    IPRESENTSTATISTICS_METHODS

    STDMETHOD_(CompositionFrameId, GetCompositionFrameId)(THIS);
    STDMETHOD_(PresentStatus, GetPresentStatus)(THIS);
};

#ifdef COBJMACROS

#define IPresentStatusPresentStatistics_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IPresentStatusPresentStatistics_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IPresentStatusPresentStatistics_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IPresentStatusPresentStatistics_GetPresentId(This) \
    ((This)->lpVtbl->GetPresentId(This))

#define IPresentStatusPresentStatistics_GetKind(This) \
    ((This)->lpVtbl->GetKind(This))

#define IPresentStatusPresentStatistics_GetCompositionFrameId(This) \
    ((This)->lpVtbl->GetCompositionFrameId(This))

#define IPresentStatusPresentStatistics_GetPresentStatus(This) \
    ((This)->lpVtbl->GetPresentStatus(This))

#endif

typedef enum CompositionFrameInstanceKind {
    CompositionFrameInstanceKind_ComposedOnScreen = 0,
    CompositionFrameInstanceKind_ScanoutOnScreen = 1,
    CompositionFrameInstanceKind_ComposedToIntermediate = 2
} CompositionFrameInstanceKind;

typedef struct CompositionFrameDisplayInstance {
    LUID displayAdapterLUID;
    UINT displayVidPnSourceId;
    UINT displayUniqueId;
    LUID renderAdapterLUID;
    CompositionFrameInstanceKind instanceKind;
    PresentationTransform finalTransform;
    boolean requiredCrossAdapterCopy;
    DXGI_COLOR_SPACE_TYPE colorSpace;
} CompositionFrameDisplayInstance;

#undef INTERFACE
#define INTERFACE ICompositionFramePresentStatistics
DECLARE_INTERFACE(ICompositionFramePresentStatistics)
{
    IUNKNOWN_METHODS
    IPRESENTSTATISTICS_METHODS

    STDMETHOD_(UINT_PTR, GetContentTag)(THIS);
    STDMETHOD_(CompositionFrameId, GetCompositionFrameId)(THIS);
    STDMETHOD(GetDisplayInstanceArray)(THIS_ UINT *displayInstanceArrayCount, const CompositionFrameDisplayInstance **displayInstanceArray);
};

#ifdef COBJMACROS

#define ICompositionFramePresentStatistics_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define ICompositionFramePresentStatistics_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define ICompositionFramePresentStatistics_Release(This) \
    ((This)->lpVtbl->Release(This))

#define ICompositionFramePresentStatistics_GetPresentId(This) \
    ((This)->lpVtbl->GetPresentId(This))

#define ICompositionFramePresentStatistics_GetKind(This) \
    ((This)->lpVtbl->GetKind(This))

#define ICompositionFramePresentStatistics_GetContentTag(This) \
    ((This)->lpVtbl->GetContentTag(This))

#define ICompositionFramePresentStatistics_GetCompositionFrameId(This) \
    ((This)->lpVtbl->GetCompositionFrameId(This))

#define ICompositionFramePresentStatistics_GetDisplayInstanceArray(This, displayInstanceArrayCount, displayInstanceArray) \
    ((This)->lpVtbl->GetDisplayInstanceArray(This, displayInstanceArrayCount, displayInstanceArray))

#endif

#undef INTERFACE
#define INTERFACE IIndependentFlipFramePresentStatistics
DECLARE_INTERFACE(IIndependentFlipFramePresentStatistics)
{
    IUNKNOWN_METHODS
    IPRESENTSTATISTICS_METHODS

    STDMETHOD_(LUID, GetOutputAdapterLUID)(THIS);
    STDMETHOD_(UINT, GetOutputVidPnSourceId)(THIS);
    STDMETHOD_(UINT_PTR, GetContentTag)(THIS);
    STDMETHOD(GetDisplayedTime)(THIS_ SystemInterruptTime *displayedTime);
    STDMETHOD(GetPresentDuration)(THIS_ SystemInterruptTime *presentDuration);
};

#ifdef COBJMACROS

#define IIndependentFlipFramePresentStatistics_QueryInterface(This, riid, ppvObject) \
    ((This)->lpVtbl->QueryInterface(This, riid, ppvObject))

#define IIndependentFlipFramePresentStatistics_AddRef(This) \
    ((This)->lpVtbl->AddRef(This))

#define IIndependentFlipFramePresentStatistics_Release(This) \
    ((This)->lpVtbl->Release(This))

#define IIndependentFlipFramePresentStatistics_GetPresentId(This) \
    ((This)->lpVtbl->GetPresentId(This))

#define IIndependentFlipFramePresentStatistics_GetKind(This) \
    ((This)->lpVtbl->GetKind(This))

#define IIndependentFlipFramePresentStatistics_GetOutputAdapterLUID(This) \
    ((This)->lpVtbl->GetOutputAdapterLUID(This))

#define IIndependentFlipFramePresentStatistics_GetOutputVidPnSourceId(This) \
    ((This)->lpVtbl->GetOutputVidPnSourceId(This))

#define IIndependentFlipFramePresentStatistics_GetContentTag(This) \
    ((This)->lpVtbl->GetContentTag(This))

#define IIndependentFlipFramePresentStatistics_GetDisplayedTime(This, displayedTime) \
    ((This)->lpVtbl->GetDisplayedTime(This, displayedTime))

#define IIndependentFlipFramePresentStatistics_GetPresentDuration(This, presentDuration) \
    ((This)->lpVtbl->GetPresentDuration(This, presentDuration))

#endif

extern IDXGISwapChain *dxgi_sc[SCREEN_COUNT];
extern ID3D11Device *d3d11device[SCREEN_COUNT];
extern ID3D11DeviceContext *d3d11device_context[SCREEN_COUNT];
extern IDXGIDevice *dxgi_device[SCREEN_COUNT];
extern IDXGIDevice2 *dxgi_device2[SCREEN_COUNT];
extern IDXGIAdapter1 *dxgi_adapter;
extern IDXGIFactory2 *dxgi_factory;
extern IPresentationFactory *presentation_factory[SCREEN_COUNT];
extern bool displayable_surface_support[SCREEN_COUNT];
extern HANDLE pres_man_lost_event[SCREEN_COUNT];
extern HANDLE pres_man_stat_avail_event[SCREEN_COUNT];
extern HANDLE pres_man_child_lost_event[SCREEN_COUNT];
extern HANDLE pres_man_child_stat_avail_event[SCREEN_COUNT];
extern IPresentationManager *presentation_manager[SCREEN_COUNT];
extern IPresentationManager *pres_man_child[SCREEN_COUNT];
extern IPresentationManager *pres_man_util[SCREEN_COUNT];
extern HANDLE composition_surface[SCREEN_COUNT];
extern IPresentationSurface *presentation_surface[SCREEN_COUNT];
extern RECT src_rect[SCREEN_COUNT];
// For ctx_top_bot == SCREEN_TOP and view_mode == VIEW_MODE_TOP_BOT
extern HANDLE comp_surf_child[SCREEN_COUNT];
extern HANDLE comp_surf_util[SURFACE_UTIL_COUNT];
extern IPresentationSurface *pres_surf_child[SCREEN_COUNT];
extern IPresentationSurface *pres_surf_util[SURFACE_UTIL_COUNT];
extern RECT src_rect_child[SCREEN_COUNT];
extern RECT src_rect_util[SCREEN_COUNT];
extern IUnknown *dcomp_surface[SCREEN_COUNT];
extern IUnknown *dcomp_surf_child[SCREEN_COUNT];
extern IUnknown *dcomp_surf_util[SURFACE_UTIL_COUNT];
extern IDCompositionDesktopDevice *dcomp_desktop_device[SCREEN_COUNT];
extern IDCompositionDevice *dcomp_device1[SCREEN_COUNT];
extern IDCompositionDevice3 *dcomp_device[SCREEN_COUNT];
extern IDCompositionTarget *dcomp_target[SCREEN_COUNT];
extern IDCompositionVisual2 *dcomp_visual[SCREEN_COUNT];
extern IDCompositionVisual2 *dcomp_vis_child[SCREEN_COUNT];
extern IDCompositionVisual2 *dcomp_vis_util[SURFACE_UTIL_COUNT];
extern HANDLE gl_d3ddevice[SCREEN_COUNT];

typedef STDMETHODIMP (*DCompositionCreateDevice3_t)(IUnknown *renderingDevice, REFIID iid, void **dcompositionDevice);
typedef STDMETHODIMP (*DCompositionCreateSurfaceHandle_t)(DWORD desiredAccess, SECURITY_ATTRIBUTES *securityAttributes, HANDLE *surfaceHandle);
typedef STDMETHODIMP (*DCompositionGetStatistics_t)(COMPOSITION_FRAME_ID frameId, COMPOSITION_FRAME_STATS *frameStats, UINT targetIdCount, COMPOSITION_TARGET_ID *targetIds, UINT *actualTargetIdCount);
typedef STDMETHODIMP (*DCompositionGetTargetStatistics_t)(COMPOSITION_FRAME_ID frameId, const COMPOSITION_TARGET_ID *targetId, COMPOSITION_TARGET_STATS *targetStats);
typedef STDMETHODIMP (*CreatePresentationFactory_t)(IUnknown *d3dDevice, REFIID riid, void **presentationFactory);
typedef STDMETHODIMP (*DCompositionBoostCompositorClock_t)(BOOL enable);

extern DCompositionCreateDevice3_t DCompositionCreateDevice3;
extern DCompositionCreateSurfaceHandle_t DCompositionCreateSurfaceHandle;
extern DCompositionGetStatistics_t DCompositionGetStatistics;
extern DCompositionGetTargetStatistics_t DCompositionGetTargetStatistics;
extern CreatePresentationFactory_t pfn_CreatePresentationFactory;
extern DCompositionBoostCompositorClock_t DCompositionBoostCompositorClock;

int dxgi_init(void);
void dxgi_close(void);
int composition_swapchain_device_init(void);
int composition_swapchain_init(HWND hwnd[SCREEN_COUNT]);
void composition_swapchain_device_close(void);
void composition_swapchain_close(void);

extern rp_lock_t comp_lock;
extern bool sc_fail[SCREEN_COUNT];

extern int ui_win_width_drawable_prev[SCREEN_COUNT], ui_win_height_drawable_prev[SCREEN_COUNT];
extern bool ui_compositing;

void ui_compositor_csc_main(int screen_top_bot, int ctx_top_bot, bool win_shared);
void ui_compositor_csc_present(int ctx_top_bot);

struct presentation_buffer_t {
    ID3D11Texture2D *tex;
    ID3D11RenderTargetView *rtv;
    IPresentationBuffer *buf;
    HANDLE buf_avail_event;
    int width;
    int height;
};
extern struct presentation_buffer_t presentation_buffers[SCREEN_COUNT][SCREEN_COUNT][PRESENATTION_BUFFER_COUNT_PER_SCREEN], ui_pres_bufs[COMPAT_PRESENATTION_BUFFER_COUNT_PER_SCREEN];

struct render_buffer_t {
    ID3D11Texture2D *tex;
    GLuint gl_tex;
    HANDLE gl_handle;
    int width;
    int height;
};
extern struct render_buffer_t render_buffers[SCREEN_COUNT][SCREEN_COUNT], ui_render_buf;

int presentation_buffer_delete(struct presentation_buffer_t *b);
int presentation_buffer_gen(struct presentation_buffer_t *b, int ctx_top_bot, int win_shared, int width, int height);
int presentation_buffer_get(struct presentation_buffer_t *bufs, int ctx_top_bot, int win_shared, int count_max, int width, int height, int *index);

int update_hide_ui(void);

// D3D11
int presentation_buffer_present(struct presentation_buffer_t *buf, int ctx_top_bot, int screen_top_bot, int win_shared, int width, int height);
int ui_buffer_present(struct presentation_buffer_t *buf, int width, int height);

// OGL
int render_buffer_delete(struct render_buffer_t *b, int ctx_top_bot);
int render_buffer_gen(struct render_buffer_t *b, int ctx_top_bot, int width, int height);
int render_buffer_get(struct render_buffer_t *b, int ctx_top_bot, int width, int height, GLuint *tex, HANDLE *handle);
GLuint ui_render_tex_get(int width, int height);
int ui_tex_present(int count_max);
int presentation_tex_present(int ctx_top_bot, int screen_top_bot, int win_shared, int count_max);

#endif
