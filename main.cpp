/*

Dec-14-2025

Main.cpp - Low Latency NV12 Capture -> GPU Convert -> Display

What the program does:
Displays the live stream of the screen using D3D12.

How to build:

F:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat

cl /EHsc /std:c++20 main.cpp /Fe:main.exe /I "F:\AI_Componets\Microsoft Visual Studio\vcpkg\installed\x64-windows\include" /link /LIBPATH:"F:\AI_Componets\Microsoft Visual Studio\vcpkg\installed\x64-windows\lib" mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib ole32.lib d3d12.lib dxgi.lib dxguid.lib

How to run:
main.exe



*/

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <d3d12video.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mfcaptureengine.h>
#include <wrl.h>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <string>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

using Microsoft::WRL::ComPtr;

// Configuration
const UINT WIDTH = 1920;
const UINT HEIGHT = 1080;
const UINT BUFFER_COUNT = 2; // For Flip Discard, 2 is standard.

// --- Global Resources ---
// D3D11 (Capture)
ComPtr<ID3D11Device> g_d3d11Device;
ComPtr<ID3D11DeviceContext> g_d3d11Context;
ComPtr<IMFDXGIDeviceManager> g_dxgiManager;
ComPtr<IMFSourceReader> g_reader;

// D3D12 (Process & Display)
ComPtr<ID3D12Device> g_d3d12Device;
ComPtr<ID3D12CommandQueue> g_directQueue;
ComPtr<ID3D12CommandQueue> g_videoQueue;
ComPtr<IDXGISwapChain3> g_swapChain;
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
ComPtr<ID3D12Resource> g_renderTargets[BUFFER_COUNT];
ComPtr<ID3D12CommandAllocator> g_directAllocator;
ComPtr<ID3D12GraphicsCommandList> g_directCommandList;
ComPtr<ID3D12CommandAllocator> g_videoAllocator;
ComPtr<ID3D12VideoProcessCommandList> g_videoCommandList;
ComPtr<ID3D12VideoDevice> g_videoDevice;
ComPtr<ID3D12VideoProcessor> g_videoProcessor;
ComPtr<ID3D12Resource> g_processedTexture; // Intermediate RGBA

// Sync
ComPtr<ID3D12Fence> g_fence;
HANDLE g_fenceEvent;
UINT64 g_fenceValue = 0;
UINT g_frameIndex = 0;
UINT g_rtvDescriptorSize = 0;

void ThrowIfFailed(HRESULT hr, const char* msg) {
    if (FAILED(hr)) {
        std::cerr << "Error: " << msg << " HR=" << std::hex << hr << std::endl;
        throw std::runtime_error(msg);
    }
}

// --- D3D11 Setup ---
void InitD3D11() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    ThrowIfFailed(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &g_d3d11Device, nullptr, &g_d3d11Context), "CreateD3D11Device");
    
    UINT token;
    ThrowIfFailed(MFCreateDXGIDeviceManager(&token, &g_dxgiManager), "MFCreateDXGIDeviceManager");
    ThrowIfFailed(g_dxgiManager->ResetDevice(g_d3d11Device.Get(), token), "ResetDevice");
}

// --- D3D12 Setup ---
void InitD3D12(HWND hwnd) {
    // 1. Device
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_d3d12Device)), "D3D12CreateDevice");

    // 2. Queues
    D3D12_COMMAND_QUEUE_DESC qDesc = {};
    qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(g_d3d12Device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&g_directQueue)), "CreateDirectQueue");

    qDesc.Type = D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS;
    ThrowIfFailed(g_d3d12Device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&g_videoQueue)), "CreateVideoQueue");

    // 3. SwapChain
    ComPtr<IDXGIFactory4> factory;
    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateFactory");

    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.BufferCount = BUFFER_COUNT;
    swapDesc.Width = WIDTH;
    swapDesc.Height = HEIGHT;
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(g_directQueue.Get(), hwnd, &swapDesc, nullptr, nullptr, &swapChain), "CreateSwapChain");
    ThrowIfFailed(swapChain.As(&g_swapChain), "AsSwapChain3");
    
    // LATENCY OPTIMIZATION: Set latency to 1
    g_swapChain->SetMaximumFrameLatency(1);
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    // 4. RTV Heap & Frame Buffers
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = BUFFER_COUNT;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(g_d3d12Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap)), "CreateRTVHeap");
    g_rtvDescriptorSize = g_d3d12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < BUFFER_COUNT; i++) {
        ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])), "GetBuffer");
        g_d3d12Device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += g_rtvDescriptorSize;
    }

    // 5. Allocators & Lists
    ThrowIfFailed(g_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_directAllocator)), "CreateDirectAllocator");
    ThrowIfFailed(g_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_directAllocator.Get(), nullptr, IID_PPV_ARGS(&g_directCommandList)), "CreateDirectList");
    g_directCommandList->Close();

    ThrowIfFailed(g_d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS, IID_PPV_ARGS(&g_videoAllocator)), "CreateVideoAllocator");
    ThrowIfFailed(g_d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS, g_videoAllocator.Get(), nullptr, IID_PPV_ARGS(&g_videoCommandList)), "CreateVideoList");
    g_videoCommandList->Close();

    // 6. Video Processor
    ThrowIfFailed(g_d3d12Device->QueryInterface(IID_PPV_ARGS(&g_videoDevice)), "QueryVideoDevice");

    D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC inDesc = {};
    inDesc.Format = DXGI_FORMAT_NV12;
    inDesc.ColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
    inDesc.SourceSizeRange = { WIDTH, HEIGHT, WIDTH, HEIGHT };
    inDesc.DestinationSizeRange = { WIDTH, HEIGHT, WIDTH, HEIGHT };
    inDesc.FrameRate = { 60, 1 };

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC outDesc = {};
    outDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    outDesc.ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

    ThrowIfFailed(g_videoDevice->CreateVideoProcessor(0, &outDesc, 1, &inDesc, IID_PPV_ARGS(&g_videoProcessor)), "CreateVideoProcessor");

    // 7. Intermediate Texture (RGBA)
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = WIDTH;
    texDesc.Height = HEIGHT;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
    ThrowIfFailed(g_d3d12Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&g_processedTexture)), "CreateIntermediateTexture");

    // 8. Fence
    ThrowIfFailed(g_d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)), "CreateFence");
    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

// --- Capture Setup ---
void InitCapture() {
    ThrowIfFailed(MFStartup(MF_VERSION), "MFStartup");

    IMFAttributes* pAttributes = nullptr;
    MFCreateAttributes(&pAttributes, 1);
    pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    ThrowIfFailed(MFEnumDeviceSources(pAttributes, &ppDevices, &count), "EnumDevices");
    if (count == 0) throw std::runtime_error("No capture devices found");

    IMFMediaSource* pSource = nullptr;
    // Simple logic: pick first USB 3.0 device or fallback to 0
    for (UINT i = 0; i < count; i++) {
        WCHAR* name = nullptr;
        ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, nullptr);
        if (wcsstr(name, L"USB3.0")) {
            ppDevices[i]->ActivateObject(__uuidof(IMFMediaSource), (void**)&pSource);
        }
        CoTaskMemFree(name);
        if (pSource) break;
    }
    if (!pSource) ppDevices[0]->ActivateObject(__uuidof(IMFMediaSource), (void**)&pSource);

    for (UINT i=0; i<count; i++) ppDevices[i]->Release();
    pAttributes->Release();

    // Source Reader
    IMFAttributes* pReaderAttrs = nullptr;
    MFCreateAttributes(&pReaderAttrs, 1);
    pReaderAttrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, g_dxgiManager.Get());
    
    // LATENCY OPTIMIZATION: Enable Low Latency mode
    pReaderAttrs->SetUINT32(MF_LOW_LATENCY, TRUE);
    pReaderAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

    ThrowIfFailed(MFCreateSourceReaderFromMediaSource(pSource, pReaderAttrs, &g_reader), "CreateSourceReader");
    pSource->Release();
    pReaderAttrs->Release();

    // Select Format
    DWORD idx = 0;
    while (true) {
        ComPtr<IMFMediaType> pType;
        if (FAILED(g_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, idx, &pType))) break;
        
        GUID subtype;
        UINT w, h;
        pType->GetGUID(MF_MT_SUBTYPE, &subtype);
        MFGetAttributeSize(pType.Get(), MF_MT_FRAME_SIZE, &w, &h);

        if (IsEqualGUID(subtype, MFVideoFormat_NV12) && w == WIDTH && h == HEIGHT) {
            ThrowIfFailed(g_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType.Get()), "SetMediaType");
            std::cout << "Capture Initialized: 1920x1080 NV12" << std::endl;
            return;
        }
        idx++;
    }
    throw std::runtime_error("Could not find 1080p NV12 format");
}

// --- Main Loop Functions ---

void ProcessAndRender() {
    // LATENCY OPTIMIZATION: Wait for SwapChain (Back Buffer) availability immediately.
    // This prevents us from processing frames that we can't display yet.
    HANDLE waitableObj = g_swapChain->GetFrameLatencyWaitableObject();
    WaitForSingleObjectEx(waitableObj, 1000, true);

    ComPtr<IMFSample> pSample;
    DWORD streamIndex, flags;
    LONGLONG stamp;
    
    // 1. Capture
    HRESULT hr = g_reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &stamp, &pSample);
    if (FAILED(hr)) return;
    if (!pSample) return;

    ComPtr<IMFMediaBuffer> pBuffer;
    pSample->GetBufferByIndex(0, &pBuffer);
    ComPtr<IMFDXGIBuffer> dxgiBuffer;
    if (FAILED(pBuffer.As(&dxgiBuffer))) return;

    ComPtr<ID3D11Texture2D> d3d11Texture;
    dxgiBuffer->GetResource(IID_PPV_ARGS(&d3d11Texture));

    // 2. Interop (D3D11 -> D3D12) - Cached
    static ComPtr<ID3D11Texture2D> g_sharedTex11;
    static ComPtr<ID3D12Resource> g_sharedTex12;
    static HANDLE g_hShared = nullptr;

    if (!g_sharedTex11) {
        // Create the persistent shared buffer
        D3D11_TEXTURE2D_DESC desc = {};
        d3d11Texture->GetDesc(&desc);
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED; 
        
        if (FAILED(g_d3d11Device->CreateTexture2D(&desc, nullptr, &g_sharedTex11))) return;
        
        ComPtr<IDXGIResource> dxgiRes;
        g_sharedTex11.As(&dxgiRes);
        dxgiRes->GetSharedHandle(&g_hShared);
        
        if (FAILED(g_d3d12Device->OpenSharedHandle(g_hShared, IID_PPV_ARGS(&g_sharedTex12)))) return;
    }

    // Always copy new frame to the persistent shared buffer
    g_d3d11Context->CopyResource(g_sharedTex11.Get(), d3d11Texture.Get());
    g_d3d11Context->Flush();

    // 3. Video Process (NV12 -> RGBA)
    g_videoAllocator->Reset();
    g_videoCommandList->Reset(g_videoAllocator.Get());

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = g_sharedTex12.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = g_processedTexture.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    g_videoCommandList->ResourceBarrier(2, barriers);

    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS inputArgs = {};
    inputArgs.InputStream[0].pTexture2D = g_sharedTex12.Get();
    inputArgs.InputStream[0].Subresource = 0;
    inputArgs.Transform.SourceRectangle = {0,0,(LONG)WIDTH,(LONG)HEIGHT};
    inputArgs.Transform.DestinationRectangle = {0,0,(LONG)WIDTH,(LONG)HEIGHT};

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS outputArgs = {};
    outputArgs.OutputStream[0].pTexture2D = g_processedTexture.Get();
    outputArgs.TargetRectangle = {0,0,(LONG)WIDTH,(LONG)HEIGHT};

    g_videoCommandList->ProcessFrames(g_videoProcessor.Get(), &outputArgs, 1, &inputArgs);

    // Transition back for Direct Queue usage
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    g_videoCommandList->ResourceBarrier(2, barriers);

    g_videoCommandList->Close();
    ID3D12CommandList* videoLists[] = { g_videoCommandList.Get() };
    g_videoQueue->ExecuteCommandLists(1, videoLists);

    // Sync: Signal Fence from Video Queue
    g_fenceValue++;
    g_videoQueue->Signal(g_fence.Get(), g_fenceValue);

    // 4. Display (RGBA -> BackBuffer)
    g_directQueue->Wait(g_fence.Get(), g_fenceValue); // Wait for Video Queue
    
    g_directAllocator->Reset();
    g_directCommandList->Reset(g_directAllocator.Get(), nullptr);

    // Transition RGBA to COPY_SOURCE
    D3D12_RESOURCE_BARRIER copyBarriers[2] = {};
    copyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyBarriers[0].Transition.pResource = g_processedTexture.Get();
    copyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    copyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    copyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    // Transition BackBuffer to COPY_DEST
    copyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyBarriers[1].Transition.pResource = g_renderTargets[g_frameIndex].Get();
    copyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    copyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    copyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    g_directCommandList->ResourceBarrier(2, copyBarriers);

    g_directCommandList->CopyResource(g_renderTargets[g_frameIndex].Get(), g_processedTexture.Get());

    // Transition BackBuffer to PRESENT, RGBA back to COMMON
    copyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    copyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    copyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    copyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_directCommandList->ResourceBarrier(2, copyBarriers);

    g_directCommandList->Close();
    ID3D12CommandList* directLists[] = { g_directCommandList.Get() };
    g_directQueue->ExecuteCommandLists(1, directLists);

    // LATENCY OPTIMIZATION: Present(0, 0) tells DX to present immediately (tearing allowed if windowed/supported)
    // or next VSync if not.
    g_swapChain->Present(0, 0);
    
    // We do NOT wait for CPU fence here anymore because SetMaximumFrameLatency(1) + WaitableObj handles the throttling.
    // If the GPU is slow, WaitableObject will block at the start of next frame.
    // If the GPU is fast, we proceed immediately.
    
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

int main() {
    try {
        // Window
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WindowProc, 0, 0, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, "D3D12Live", nullptr };
        RegisterClassEx(&wc);
        HWND hwnd = CreateWindow(wc.lpszClassName, "D3D12 Live Streamer", WS_OVERLAPPEDWINDOW, 100, 100, WIDTH, HEIGHT, nullptr, nullptr, wc.hInstance, nullptr);
        
        InitD3D11();
        InitD3D12(hwnd);
        InitCapture();

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        MSG msg = {};
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            } else {
                ProcessAndRender();
            }
        }
    } catch (const std::exception& e) {
        MessageBox(nullptr, e.what(), "Error", MB_ICONERROR);
    }
    MFShutdown();
    return 0;
}
