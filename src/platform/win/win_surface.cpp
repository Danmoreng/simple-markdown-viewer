#include "platform/win/win_surface.h"

#include <array>
#include <cstdint>
#include <sstream>
#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>

// Suppress warnings from Skia headers
#pragma warning(push)
#pragma warning(disable: 4201)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DBackendSurface.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
#pragma warning(pop)

namespace mdviewer::win {

namespace {

constexpr UINT kBufferCount = 3;
constexpr DXGI_FORMAT kSwapChainFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr UINT kSwapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

std::string HResultMessage(const char* operation, HRESULT result) {
    std::ostringstream message;
    message << operation << " failed with HRESULT 0x" << std::hex
            << static_cast<unsigned long>(result);
    return message.str();
}

template <typename Function>
bool CheckHResult(Function&& operation, const char* label, std::string& error) {
    const HRESULT result = operation();
    if (SUCCEEDED(result)) {
        return true;
    }
    error = HResultMessage(label, result);
    return false;
}

void ReportGpuFallback(const std::string& reason) {
    const std::string diagnostic =
        "Simple Markdown Viewer: Direct3D rendering unavailable; using raster fallback: " +
        reason + "\n";
    OutputDebugStringA(diagnostic.c_str());
}

} // namespace

struct WinSurface::Impl {
    HWND window = nullptr;
    int width = 0;
    int height = 0;
    UINT bufferIndex = 0;
    std::uint64_t nextFenceValue = 1;
    HANDLE fenceEvent = nullptr;
    HANDLE frameLatencyWaitable = nullptr;
    bool gpuAttempted = false;
    bool gpuActive = false;

    gr_cp<IDXGIFactory4> factory;
    gr_cp<IDXGIAdapter1> adapter;
    gr_cp<ID3D12Device> device;
    gr_cp<ID3D12CommandQueue> queue;
    sk_sp<GrDirectContext> context;
    gr_cp<IDXGISwapChain3> swapChain;
    gr_cp<ID3D12Fence> fence;
    std::array<gr_cp<ID3D12Resource>, kBufferCount> buffers;
    std::array<sk_sp<SkSurface>, kBufferCount> gpuSurfaces;
    std::array<std::uint64_t, kBufferCount> bufferFenceValues{};
    sk_sp<SkSurface> surface;

    bool create_device(std::string& error) {
        if (!CheckHResult(
                [&] { return CreateDXGIFactory1(IID_PPV_ARGS(&factory)); },
                "CreateDXGIFactory1",
                error) ||
            !CheckHResult(
                [&] {
                    // A null adapter lets Windows and the installed driver choose
                    // the default device instead of imposing an app-side GPU policy.
                    return D3D12CreateDevice(
                        nullptr,
                        D3D_FEATURE_LEVEL_11_0,
                        IID_PPV_ARGS(&device));
                },
                "D3D12CreateDevice",
                error)) {
            return false;
        }

        const LUID adapterLuid = device->GetAdapterLuid();
        if (!CheckHResult(
                [&] {
                    return factory->EnumAdapterByLuid(
                        adapterLuid,
                        IID_PPV_ARGS(&adapter));
                },
                "IDXGIFactory4::EnumAdapterByLuid",
                error)) {
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC queueDescription{};
        queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDescription.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDescription.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (!CheckHResult(
                [&] {
                    return device->CreateCommandQueue(
                        &queueDescription,
                        IID_PPV_ARGS(&queue));
                },
                "ID3D12Device::CreateCommandQueue",
                error)) {
            return false;
        }

        GrD3DBackendContext backendContext;
        backendContext.fAdapter = adapter;
        backendContext.fDevice = device;
        backendContext.fQueue = queue;
        context = GrDirectContexts::MakeD3D(backendContext);
        if (!context) {
            error = "Skia could not create a Ganesh Direct3D context.";
            return false;
        }

        if (!CheckHResult(
                [&] {
                    return device->CreateFence(
                        0,
                        D3D12_FENCE_FLAG_NONE,
                        IID_PPV_ARGS(&fence));
                },
                "ID3D12Device::CreateFence",
                error)) {
            return false;
        }
        fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent) {
            error = "Could not create the Direct3D fence event.";
            return false;
        }
        return true;
    }

    bool create_swap_chain(std::string& error) {
        DXGI_SWAP_CHAIN_DESC1 description{};
        description.Width = static_cast<UINT>(width);
        description.Height = static_cast<UINT>(height);
        description.Format = kSwapChainFormat;
        description.Stereo = FALSE;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = kBufferCount;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        description.Flags = kSwapChainFlags;

        gr_cp<IDXGISwapChain1> initialSwapChain;
        if (!CheckHResult(
                [&] {
                    return factory->CreateSwapChainForHwnd(
                        queue.get(),
                        window,
                        &description,
                        nullptr,
                        nullptr,
                        &initialSwapChain);
                },
                "IDXGIFactory4::CreateSwapChainForHwnd",
                error) ||
            !CheckHResult(
                [&] {
                    return initialSwapChain->QueryInterface(
                        IID_PPV_ARGS(&swapChain));
                },
                "IDXGISwapChain1::QueryInterface",
                error)) {
            return false;
        }

        factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        gr_cp<IDXGISwapChain2> swapChain2;
        if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain2)))) {
            swapChain2->SetMaximumFrameLatency(1);
            frameLatencyWaitable = swapChain2->GetFrameLatencyWaitableObject();
        }
        return create_gpu_surfaces(error);
    }

    bool create_gpu_surfaces(std::string& error) {
        for (UINT index = 0; index < kBufferCount; ++index) {
            if (!CheckHResult(
                    [&] {
                        return swapChain->GetBuffer(
                            index,
                            IID_PPV_ARGS(&buffers[index]));
                    },
                    "IDXGISwapChain3::GetBuffer",
                    error)) {
                return false;
            }

            GrD3DTextureResourceInfo resourceInfo(
                buffers[index].get(),
                nullptr,
                D3D12_RESOURCE_STATE_PRESENT,
                kSwapChainFormat,
                1,
                1,
                0);
            const GrBackendRenderTarget renderTarget =
                GrBackendRenderTargets::MakeD3D(width, height, resourceInfo);
            gpuSurfaces[index] = SkSurfaces::WrapBackendRenderTarget(
                context.get(),
                renderTarget,
                kTopLeft_GrSurfaceOrigin,
                kBGRA_8888_SkColorType,
                nullptr,
                nullptr);
            if (!gpuSurfaces[index]) {
                error = "Skia could not wrap a Direct3D swap-chain buffer.";
                return false;
            }
        }
        bufferIndex = swapChain->GetCurrentBackBufferIndex();
        return true;
    }

    bool wait_for_fence(std::uint64_t value) {
        if (value == 0 || fence->GetCompletedValue() >= value) {
            return true;
        }
        if (FAILED(fence->SetEventOnCompletion(value, fenceEvent))) {
            return false;
        }
        return WaitForSingleObject(fenceEvent, 2000) == WAIT_OBJECT_0;
    }

    bool wait_for_idle() {
        const std::uint64_t value = nextFenceValue++;
        if (FAILED(queue->Signal(fence.get(), value))) {
            return false;
        }
        return wait_for_fence(value);
    }

    void release_gpu_surfaces() {
        surface.reset();
        for (auto& gpuSurface : gpuSurfaces) {
            gpuSurface.reset();
        }
        for (auto& buffer : buffers) {
            buffer.reset();
        }
    }

    bool resize_gpu(int newWidth, int newHeight, std::string& error) {
        if (newWidth == width && newHeight == height) {
            return true;
        }
        if (!wait_for_idle()) {
            error = "Direct3D could not synchronize before resizing.";
            return false;
        }

        context->flush();
        context->submit(GrSyncCpu::kYes);
        release_gpu_surfaces();
        width = newWidth;
        height = newHeight;
        bufferFenceValues.fill(0);
        if (!CheckHResult(
                [&] {
                    return swapChain->ResizeBuffers(
                        kBufferCount,
                        static_cast<UINT>(width),
                        static_cast<UINT>(height),
                        kSwapChainFormat,
                        kSwapChainFlags);
                },
                "IDXGISwapChain3::ResizeBuffers",
                error)) {
            return false;
        }
        return create_gpu_surfaces(error);
    }

    bool initialize_gpu(HWND hwnd, int initialWidth, int initialHeight, std::string& error) {
        window = hwnd;
        width = initialWidth;
        height = initialHeight;
        if (!create_device(error) || !create_swap_chain(error)) {
            return false;
        }
        gpuActive = true;
        return true;
    }

    bool begin_gpu_frame(
        int newWidth,
        int newHeight,
        bool interactiveResize,
        std::string& error) {
        if (!resize_gpu(newWidth, newHeight, error)) {
            return false;
        }
        if (frameLatencyWaitable && !interactiveResize) {
            WaitForSingleObject(frameLatencyWaitable, 16);
        }
        bufferIndex = swapChain->GetCurrentBackBufferIndex();
        if (!wait_for_fence(bufferFenceValues[bufferIndex])) {
            error = "Direct3D timed out while waiting for a swap-chain buffer.";
            return false;
        }
        surface = gpuSurfaces[bufferIndex];
        return surface != nullptr;
    }

    bool present_gpu(bool interactiveResize, std::string& error) {
        if (!surface || !context || !swapChain) {
            error = "Direct3D has no active frame to present.";
            return false;
        }

        GrFlushInfo flushInfo;
        context->flush(
            surface.get(),
            SkSurfaces::BackendSurfaceAccess::kPresent,
            flushInfo);
        if (!context->submit()) {
            error = "Skia could not submit the Direct3D frame.";
            return false;
        }
        // During a live sidebar resize, avoid holding the UI thread until the
        // next vertical blank. Windowed flip-model presentation is still
        // composed by DWM, while newer drag frames can replace older ones.
        const HRESULT presentResult = swapChain->Present(interactiveResize ? 0 : 1, 0);
        if (FAILED(presentResult)) {
            error = HResultMessage("IDXGISwapChain3::Present", presentResult);
            return false;
        }
        const std::uint64_t fenceValue = nextFenceValue++;
        if (FAILED(queue->Signal(fence.get(), fenceValue))) {
            error = "Direct3D could not signal the frame fence.";
            return false;
        }
        bufferFenceValues[bufferIndex] = fenceValue;
        return true;
    }

    void shutdown_gpu() {
        if (context && queue && fence) {
            wait_for_idle();
        }
        release_gpu_surfaces();
        if (context) {
            context->flush();
            context->submit(GrSyncCpu::kYes);
            context->abandonContext();
        }
        if (frameLatencyWaitable) {
            CloseHandle(frameLatencyWaitable);
        }
        frameLatencyWaitable = nullptr;
        swapChain.reset();
        context.reset();
        fence.reset();
        queue.reset();
        device.reset();
        adapter.reset();
        factory.reset();
        if (fenceEvent) {
            CloseHandle(fenceEvent);
        }
        fenceEvent = nullptr;
        bufferFenceValues.fill(0);
        nextFenceValue = 1;
        gpuActive = false;
        width = 0;
        height = 0;
        window = nullptr;
    }

    bool ensure_raster_size(int newWidth, int newHeight) {
        if (surface && surface->width() == newWidth && surface->height() == newHeight) {
            return true;
        }

        const SkImageInfo info = SkImageInfo::MakeN32Premul(newWidth, newHeight);
        surface = SkSurfaces::Raster(info);
        width = newWidth;
        height = newHeight;
        return surface != nullptr;
    }

    bool ensure_size(HWND hwnd, bool acquireGpuFrame, bool interactiveResize = false) {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        const int newWidth = rect.right - rect.left;
        const int newHeight = rect.bottom - rect.top;
        if (newWidth <= 0 || newHeight <= 0) {
            surface.reset();
            return false;
        }

        if (!gpuAttempted) {
            gpuAttempted = true;
            std::string error;
            if (!initialize_gpu(hwnd, newWidth, newHeight, error)) {
                shutdown_gpu();
                ReportGpuFallback(error);
            }
        }

        if (gpuActive) {
            std::string error;
            if (acquireGpuFrame &&
                begin_gpu_frame(newWidth, newHeight, interactiveResize, error)) {
                return true;
            }
            if (!acquireGpuFrame && resize_gpu(newWidth, newHeight, error)) {
                bufferIndex = swapChain->GetCurrentBackBufferIndex();
                surface = gpuSurfaces[bufferIndex];
                return surface != nullptr;
            }
            shutdown_gpu();
            ReportGpuFallback(error);
        }
        return ensure_raster_size(newWidth, newHeight);
    }

    void present_raster(HWND hwnd) {
        PAINTSTRUCT ps{};
        if (!surface) {
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return;
        }

        SkPixmap pixmap;
        if (!surface->peekPixels(&pixmap)) {
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return;
        }

        HDC hdc = BeginPaint(hwnd, &ps);

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pixmap.width();
        bmi.bmiHeader.biHeight = -pixmap.height();
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        StretchDIBits(
            hdc,
            0,
            0,
            pixmap.width(),
            pixmap.height(),
            0,
            0,
            pixmap.width(),
            pixmap.height(),
            pixmap.addr(),
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);

        EndPaint(hwnd, &ps);
    }
};

WinSurface::WinSurface() : impl_(std::make_unique<Impl>()) {}

WinSurface::~WinSurface() {
    Shutdown();
}

bool WinSurface::EnsureSize(HWND hwnd) {
    return impl_ && impl_->ensure_size(hwnd, false);
}

bool WinSurface::BeginFrame(HWND hwnd, bool interactiveResize) {
    return impl_ && impl_->ensure_size(hwnd, true, interactiveResize);
}

void WinSurface::Present(HWND hwnd, bool interactiveResize) {
    if (!impl_) {
        return;
    }
    if (impl_->gpuActive) {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        std::string error;
        if (!impl_->present_gpu(interactiveResize, error)) {
            impl_->shutdown_gpu();
            ReportGpuFallback(error);
            impl_->ensure_size(hwnd, false);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }
    impl_->present_raster(hwnd);
}

void WinSurface::Shutdown() {
    if (!impl_) {
        return;
    }
    impl_->shutdown_gpu();
    impl_->surface.reset();
}

SkSurface* WinSurface::get() const {
    return impl_ ? impl_->surface.get() : nullptr;
}

SkSurface* WinSurface::operator->() const {
    return get();
}

WinSurface::operator bool() const {
    return get() != nullptr;
}

bool WinSurface::IsGpuBacked() const {
    return impl_ && impl_->gpuActive;
}

} // namespace mdviewer::win
