#include <iostream>
#include <string>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <d3d11_4.h>
#include <wrl.h>
#include <chrono>
#include <thread>
#include <vfw.h>
#include "ffmpegEncodeWin.h"
#include <vector>
#include <fstream>
#include <wincodec.h>
#include <wrl/client.h>
using namespace Microsoft::WRL;
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#include <va/va.h>
#include <va/va_enc_h264.h>
#include "videoUtils.h"

static void message_callback(void* user_context, const char* message)
{
    printf("mc: %s\n", message);
}

#define REPORT(hr, s) \
    do { \
        HRESULT _hrTemp = (hr); \
        if (FAILED(_hrTemp)) { \
            printf("[FAIL] Report: %s(%d), hr=0x%08x Failed on %s\n", __FILE__, __LINE__, static_cast<unsigned int>(_hrTemp), (s)); \
            return _hrTemp; \
        } else { \
            if (strlen(s) > 0) { \
                printf("[ OK ] Successful on %s\n", (s)); \
            } \
        } \
    } while (0)


ffmpegEncodeWin::ffmpegEncodeWin(void)
{
    // Initialize FFmpeg libraries
    avdevice_register_all();
    avformat_network_init();
}

ffmpegEncodeWin::~ffmpegEncodeWin()
{

}

void ffmpegEncodeWin::CheckvaQueryConfigProfiles() {
    int max_profiles = vaMaxNumProfiles(m_vaDisplay);
    std::vector<VAProfile> profiles(max_profiles);
    int actual_profiles = vaQueryConfigProfiles(m_vaDisplay, profiles.data(), &max_profiles);
    std::cout << "Supported VA Profiles: " << max_profiles << "\n";
    for (int i = 0; i < max_profiles; ++i) {
        std::cout << " - Profile: " << profiles[i] << "\n";
    }

    for (int i = 0; i < max_profiles; ++i) {
        VAProfile profile = profiles[i];

        int max_entrypoints = vaMaxNumEntrypoints(m_vaDisplay);
        std::vector<VAEntrypoint> entrypoints(max_entrypoints);
        int actual_entrypoints = vaQueryConfigEntrypoints(m_vaDisplay, profile, entrypoints.data(), &max_entrypoints);

        for (int j = 0; j < max_entrypoints; ++j) {
            std::cout << "   - Entrypoint: " << entrypoints[j] << "\n";
        }
    }
}

int ffmpegEncodeWin::CreateSurfaces() {
    /*
    Initialize VAAPI;
    */
    static int flag = 0;
    HRESULT hr;
    DXGI_ADAPTER_DESC descva = {};
    VAStatus va_status;
    if (flag == 0) {
        hr = m_adapter->GetDesc(&descva);
        if (FAILED(hr))
        {
            std::cerr << "Failed to get desc from adapter. HRESULT: " << std::hex << hr << std::endl;
            return -1;
        }
        m_vaDisplay = vaGetDisplayWin32(&descva.AdapterLuid);
        if (!m_vaDisplay)
        {
            std::cerr << "vaGetDisplayWin32 failed to create a VADisplay." << std::endl;
            return -1;
        }

        int major_ver, minor_ver;
        va_status = vaInitialize(m_vaDisplay, &major_ver, &minor_ver);
        if (va_status != VA_STATUS_SUCCESS)
        {
            std::cerr << "Failed to vaInitialize. va_status: " << std::hex << va_status << std::endl;
            return -1;
        }
        std::cout << "vaInitialize successful va_status: " << std::hex << va_status << std::endl;

        // Create VA Surfaces
        vaSetErrorCallback(m_vaDisplay, message_callback, 0);
        vaSetInfoCallback(m_vaDisplay, message_callback, 0);
        flag++;
    }
    VASurfaceAttrib createSurfacesAttribList1[3] = {};
    HANDLE vaHandles[] = { m_renderSharedHandle };

    createSurfacesAttribList1[0].type = VASurfaceAttribMemoryType;
    createSurfacesAttribList1[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    createSurfacesAttribList1[0].value.type = VAGenericValueTypeInteger;
    createSurfacesAttribList1[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE;

    createSurfacesAttribList1[1].type = VASurfaceAttribExternalBufferDescriptor;
    createSurfacesAttribList1[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
    createSurfacesAttribList1[1].value.type = VAGenericValueTypePointer;
    createSurfacesAttribList1[1].value.value.p = vaHandles;//static_cast<void*>(vaHandles);

    createSurfacesAttribList1[2].type = VASurfaceAttribPixelFormat;
    createSurfacesAttribList1[2].flags = VA_SURFACE_ATTRIB_SETTABLE;
    createSurfacesAttribList1[2].value.type = VAGenericValueTypeInteger;
    createSurfacesAttribList1[2].value.value.i = vaDescFmt;

    va_status = vaCreateSurfaces(m_vaDisplay, vaSurfaceFmt, m_width, m_height, &m_VASurfaceNV12New, 1, createSurfacesAttribList1, _countof(createSurfacesAttribList1));
     if (va_status != VA_STATUS_SUCCESS)
    {
        std::cerr << "[FAIL] Failed to vaCreateSurfaces. va_status: " << std::hex << va_status << std::endl;
        return -2;
    }
    std::cout << "[ OK ] vaCreateSurfaces " << " successful" << std::hex << va_status << std::endl;


    return 0;
}

HRESULT ffmpegEncodeWin::InitializeD3D11Interop() {
    if (initialized) return S_OK;

    isNVFormat = true;
    if (isNVFormat) {
        // NV12 Fprmat settings
        vaSurfaceFmt = VA_RT_FORMAT_YUV420;
        vaDescFmt = VA_FOURCC_NV12;
        dxgiD3D11TextureFmt = DXGI_FORMAT_NV12;
        std::cout << "NV12 Formats: VA_RT_FORMAT_YUV420: " << std::dec << vaSurfaceFmt << "  VA_FOURCC_NV12: " << vaDescFmt << "  DXGI_FORMAT_NV12: " << dxgiD3D11TextureFmt << std::endl;
        std::cout << "NV12 Formats: VA_RT_FORMAT_YUV420: 0x" << std::hex << vaSurfaceFmt << "  VA_FOURCC_NV12: 0x" << vaDescFmt << "  DXGI_FORMAT_NV12: 0x" << dxgiD3D11TextureFmt << std::endl;
    }
    else {
        // BGRA format settings
        vaSurfaceFmt = VA_RT_FORMAT_RGB32;
        vaDescFmt = VA_FOURCC_BGRA;
        dxgiD3D11TextureFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
        std::cout << "BGRA Formats: VA_RT_FORMAT_RGB32: " << std::dec << vaSurfaceFmt << "  VA_FOURCC_BGRA: " << vaDescFmt << "  DXGI_FORMAT_B8G8R8A8_UNORM: " << dxgiD3D11TextureFmt << std::endl;
        std::cout << "BGRA Formats: VA_RT_FORMAT_RGB32: 0x" << std::hex << vaSurfaceFmt << "  VA_FOURCC_BGRA: 0x" << vaDescFmt << "  DXGI_FORMAT_B8G8R8A8_UNORM: 0x" << dxgiD3D11TextureFmt << std::endl;
    }

    UINT flags = 0;
    HRESULT hr;
    hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) return hr;

    // Enumerate adapter
    hr = m_factory->EnumAdapters1(0, &m_adapter);
    if (FAILED(hr)) return hr;


    // Create D3D11 device for duplication
    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    hr = D3D11CreateDevice(m_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &d3d11Device, &featureLevel, &d3d11Context);
    if (FAILED(hr)) return hr;

    // Get DXGI output (first output of first adapter)
    hr = m_adapter->EnumOutputs(0, &dxgiOutput);
    if (FAILED(hr)) return hr;


    hr = dxgiOutput.As(&dxgiOutput1);
    if (FAILED(hr)) return hr;

    // Create duplication
    hr = dxgiOutput1->DuplicateOutput(d3d11Device.Get(), &outputDuplication);
    if (FAILED(hr)) return hr;

    hr = D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device));
    if (FAILED(hr)) {
        return hr;
    }

    if (!d3d12Device) {
        std::cerr << "Failed to create D3D12 device\n";
        return hr;
    }

    // Create command allocator
    d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

    // Create command list
    d3d12Device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)
    );

    // Create command queue
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    hr = (d3d12Device)->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    if (FAILED(hr)) {
        std::cerr << "Failed to create D3D12 command queue\n";
        return hr;
    }

    // acquire a frame to get screen co-ordinates
    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
    hr = outputDuplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);
    if (FAILED(hr)) {
        std::cout << "ERROR >>>>>>> " << std::endl;
        return -1;
    }

    // Get D3D11 texture
    desktopResource.As(&acquiredTexture);
    // Get texture desc
    D3D11_TEXTURE2D_DESC desc = {};
    acquiredTexture->GetDesc(&desc);
    m_width = desc.Width;
    m_height = desc.Height;
    outputDuplication->ReleaseFrame();

    if (!converter.Initialize(d3d11Device.Get(), d3d11Context.Get(), m_width, m_height)) {
        std::cerr << "Init failed\n";
        hr = -1;
        return hr;
    }


    initialized = true;
    return S_OK;
}

ComPtr<ID3D12Resource> ffmpegEncodeWin::CaptureScreenD3D12(ComPtr<ID3D12Device> d3d12Device, ComPtr<ID3D12CommandQueue> commandQueue) {

    std::cout << "CaptureScreenD3D12 =========== " << std::endl;

    // Create a shared texture in D3D11
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = m_width;        // capture width
    texDesc.Height = m_height;      // capture height
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = dxgiD3D11TextureFmt;  // NV12 format for video frames
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;//D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;//0;                  // NV12 is typically not bindable as render target
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;//D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;//D3D11_RESOURCE_MISC_SHARED;  // Enable sharing!
    texDesc.CPUAccessFlags = 0;

    HRESULT hr = d3d11Device->CreateTexture2D(&texDesc, nullptr, &sharedTextureD3D11);
    if (FAILED(hr)) {
        std::cout << "ERROR >>>>>>> " << std::endl;
        return nullptr;
    }

    // Get the shared handle from the D3D11 texture
    hr = sharedTextureD3D11->QueryInterface(__uuidof(IDXGIResource), (void**)&dxgiResource);
    if (FAILED(hr)) {
        std::cout << "ERROR >>>>>>> " << std::endl;
        return nullptr;
    }

    hr = dxgiResource->GetSharedHandle(&m_renderSharedHandle);
    dxgiResource->Release();
    if (FAILED(hr) || m_renderSharedHandle == nullptr) {
        std::cout << "ERROR >>>>>>> " << std::endl;
        return nullptr;
    }

    // Open the shared handle in D3D12 device
    //hr = d3d12Device->OpenSharedHandle(m_renderSharedHandle, __uuidof(ID3D12Resource), (void**)&sharedTextureD3D12);
    hr = d3d12Device->OpenSharedHandle(m_renderSharedHandle, __uuidof(ID3D12Resource), (void**)sharedResourceD3D12.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to open shared handle\n";
        return nullptr;
    }
    if (FAILED(hr)) {
        std::cout << "ERROR >>>>>>> " << std::endl;
        return nullptr;
    }

    if (FAILED(hr)) {
        std::cerr << "Failed to get IDXGIKeyedMutex from sharedTextureD3D12\n";
        return nullptr;
    }



    return sharedResourceD3D12;
}


int ffmpegEncodeWin::FFMPEG_VAAPI_Debug() {

    InitializeD3D11Interop();
    CaptureScreenD3D12(d3d12Device, commandQueue);
    CreateSurfaces();

    // Config and init ffmpeg encoder
    encIfConfig cfg;
    cfg.width = m_width;
    cfg.height = m_height;
    cfg.bitrate = 1000000;
    cfg.framerate = 30;
    cfg.codecIndex = 0;
    cfg.qualityModeIndex = 0;
    cfg.osVersion = AM_SC_OS_VERSION_WINDOWS;
    cfg.deviceType = AV_HWDEVICE_TYPE_VAAPI;
    strcpy_s(cfg.hwDeviceTypeName, sizeof(cfg.hwDeviceTypeName), "vaapi");
    ScEncoderConfigIF(&cfg);

    //vaSyncSurface(m_vaDisplay, m_VASurfaceNV12New);
    //DumpVaSurfaceToNV12File(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height, "vaSufaceDump_before.yuv");
    //FillVaSurfaceWithRed(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height);
    //DumpVaSurfaceToNV12File(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height, "vaSufaceDump_after.yuv");
    //vaSyncSurface(m_vaDisplay, m_VASurfaceNV12New);

    EncodedLoop();

    return 0;
}

HRESULT ffmpegEncodeWin::ConfigFences(void) {
    HRESULT hr;
    // SDK needed: Windows 10 SDK 10.0.14393+

    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    fenceValue = 1;

    // Check for D3D11 fence support
    D3D11_FEATURE_DATA_D3D11_OPTIONS5 featureOptions5 = {};
    ComPtr<ID3D11DeviceContext1> d3d11Context1;
    d3d11Context.As(&d3d11Context1);

    bool isSharedFenceSupported = false; // or false, if you want to disable on unknown systems

    //hr = d3d11Device->CheckFeatureSupport(
    //    D3D11_FEATURE_D3D11_OPTIONS5,
    //    &featureOptions5,
    //    sizeof(featureOptions5)
    //);

    if (SUCCEEDED(d3d11Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS5, &featureOptions5, sizeof(featureOptions5)))) {
        isSharedFenceSupported = true;
    }

    if (!isSharedFenceSupported) {
        std::cerr << "WARNING: Assuming shared fences are unsupported.\n";
    }

    // Create D3D12 fence
    hr = d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&sharedFence));
    if (FAILED(hr)) {
        std::cerr << "ERROR: Failed to create D3D12 fence\n";
        return hr;
    }

    // Export D3D12 fence to shared handle
    HANDLE sharedFenceHandle = nullptr;
    hr = d3d12Device->CreateSharedHandle(
        sharedFence.Get(),
        nullptr,
        GENERIC_ALL,
        nullptr,
        &sharedFenceHandle
    );
    if (FAILED(hr)) {
        std::cerr << "ERROR: Failed to create shared fence handle\n";
        return hr;
    }

    hr = d3d11Device.As(&d3d11Device5);
    if (FAILED(hr)) {
        std::cerr << "Failed to get ID3D11Device5 interface.\n";
        return hr;
    }

    hr = d3d11Device5->OpenSharedFence(sharedFenceHandle, IID_PPV_ARGS(&d3d11Fence));
    CloseHandle(sharedFenceHandle); // Safe to close after import
    if (FAILED(hr)) {
        std::cerr << "OpenSharedFence failed: " << std::hex << hr << std::endl;
        return hr;
    }

    hr = d3d11Context.As(&d3d11Context4);
    if (FAILED(hr)) {
        std::cerr << "Failed to query ID3D11DeviceContext4.\n";
        return hr;
    }
}

int ffmpegEncodeWin::EncodedLoop(void)
{
    HRESULT hr;
    bool encodeFlag = true;

    ConfigFences();

    {
        DXGI_ADAPTER_DESC desc11 = {}, desc12 = {};
        ComPtr<IDXGIDevice> dxgiDevice11;
        d3d11Device->QueryInterface(IID_PPV_ARGS(&dxgiDevice11));

        ComPtr<IDXGIAdapter> adapter11;
        dxgiDevice11->GetAdapter(&adapter11);
        adapter11->GetDesc(&desc11);

        m_adapter->GetDesc(&desc12);  // m_adapter used for D3D12

        std::wcout << L"D3D11 Adapter LUID: " << desc11.AdapterLuid.HighPart << L"-" << desc11.AdapterLuid.LowPart << std::endl;
        std::wcout << L"D3D12 Adapter LUID: " << desc12.AdapterLuid.HighPart << L"-" << desc12.AdapterLuid.LowPart << std::endl;


        ComPtr<IDXGIResource> dxgiRes11;
        sharedTextureD3D11->QueryInterface(IID_PPV_ARGS(&dxgiRes11));

        HANDLE handle11 = nullptr;
        dxgiRes11->GetSharedHandle(&handle11);

        ComPtr<IDXGIResource> dxgiRes12;
        sharedResourceD3D12->QueryInterface(IID_PPV_ARGS(&dxgiRes12));

        D3D12_RESOURCE_DESC desc = sharedResourceD3D12->GetDesc();
        std::cout << "D3D12 Width: " << std::dec << desc.Width << ", Height: " << desc.Height << ", Format: " << desc.Format << std::endl;

        //auto gpuVA = sharedResorceD3D12->GetGPUVirtualAddress();
        //std::cout << "D3D12 GPU virtual address: 0x" << std::hex << gpuVA << std::dec << std::endl;

        //HANDLE handle12 = nullptr;
        //dxgiRes12->GetSharedHandle(&handle12);

        std::cout << "D3D11 Shared Handle: " << handle11 << std::endl;
        //std::cout << "D3D12 Shared Handle: " << handle12 << std::endl;



        D3D11_TEXTURE2D_DESC desc11_1 = {};
        sharedTextureD3D11->GetDesc(&desc11_1);

        D3D12_RESOURCE_DESC desc12_1 = sharedResourceD3D12->GetDesc();

        // Print and compare
        std::cout << "D3D11 Format: " << desc11_1.Format << ", Width: " << desc11_1.Width << ", Height: " << desc11_1.Height << std::endl;
        std::cout << "D3D12 Format: " << desc12_1.Format << ", Width: " << desc12_1.Width << ", Height: " << desc12_1.Height << std::endl;


    }

    int dumpFrameCount = 3;
    for (int frameCount = 0; frameCount < 5; ++frameCount) {

        std::cout << "Capturing frame +++++++ " << frameCount << std::endl;

        // Try to acquire next frame
        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        hr = outputDuplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);
        if (FAILED(hr)) {
            std::cout << "ERROR >>>>>>> " << std::endl;
            continue;
        }

        // Get D3D11 texture
        desktopResource.As(&acquiredTexture);

        bool convStatus;
        // convert and write frame to sharedTextureD3D11
        if (isNVFormat) {
            convStatus = converter.Convert(acquiredTexture.Get(), &sharedTextureD3D11);
        }
        else {
            convStatus = converter.Copy(acquiredTexture.Get(), sharedTextureD3D11.Get());
        }
        if (convStatus) {
            fenceValue++;  // Increment for next frame
            d3d11Context->Flush();   // Push to GPU
            /*
            // Wait for GPU to finish processing
            ID3D11Query* query = nullptr;
            D3D11_QUERY_DESC qdesc = {};
            qdesc.Query = D3D11_QUERY_EVENT;
            d3d11Device->CreateQuery(&qdesc, &query);

            d3d11Context->End(query);  // Signal event
            while (S_OK != d3d11Context->GetData(query, nullptr, 0, 0)) {
                std::cout << "Frame conversion is going on\n";
                Sleep(1); // Wait until the GPU is done
            }
            query->Release();
            */
            // Get texture desc
            D3D11_TEXTURE2D_DESC desc = {};
            sharedTextureD3D11->GetDesc(&desc);
            std::cout << "sharedTextureD3D11 Format : " << desc.Format << " " <<  std::endl;
            D3D12_RESOURCE_DESC d3d12Desc = sharedResourceD3D12->GetDesc();
            std::cout << "sharedResourceD3D12 Format: " << d3d12Desc.Format << " " << std::endl;


            // Signal the shared fence from D3D11
            hr = d3d11Context4->Signal(d3d11Fence.Get(), fenceValue);
            if (FAILED(hr)) {
                std::cerr << "Failed to signal D3D11 fence.\n";
                continue;
            }

            std::cout << "Fence Values: " << sharedFence->GetCompletedValue() << " " << fenceValue << std::endl;
            // Wait in D3D12 for fenceValue
            if (sharedFence->GetCompletedValue() < fenceValue) {
                hr = sharedFence->SetEventOnCompletion(fenceValue, fenceEvent);
                if (FAILED(hr)) {
                    std::cerr << "ERROR: SetEventOnCompletion failed\n";
                }
                WaitForSingleObject(fenceEvent, INFINITE);
            }
            if (frameCount == dumpFrameCount) {
                DumpD3D1TextureToFile(d3d11Device.Get(), d3d11Context.Get(), sharedTextureD3D11.Get(), "d3d11_frame_dump", isNVFormat);
            }

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = sharedResourceD3D12.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;  // or current state
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);


            // D3D12 shared resource access starts here
            // encode captured frame, d3d11Texture maps to vaSurfaces via D3D12 resource
            // somehow this is not happening - #TODO debug
            vaSyncSurface(m_vaDisplay, m_VASurfaceNV12New);
            if (encodeFlag) {
                if (frameCount == dumpFrameCount) {
                    DumpVaSurfaceToFile(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height, "vaSufaceDump", isNVFormat);
                }

                ScEncodeFrames(reinterpret_cast<void*>(static_cast<uintptr_t>(m_VASurfaceNV12New)), true);

                if (frameCount == dumpFrameCount) {
                    DumpVaSurfaceToFile(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height, "vaSufaceDump_after_enc", isNVFormat);
                    DumpD3D12ResourceToFile(d3d12Device, commandList, commandQueue, sharedResourceD3D12, "d3d12_frame_dump", isNVFormat);
                }
            }
            // D3D12 shared resource access ends here
        }
        else {
            std::cerr << "Frame conversion failed\n";
        }
        outputDuplication->ReleaseFrame();
    } 

    if (encodeFlag) {
        ScEncodeClose();
    }

    return 0;
}

int main(int argc, char* argv[])
{





    ffmpegEncodeWin sc;

    int ret = 0;
    
    ret = sc.FFMPEG_VAAPI_Debug();

    std::cout << "RET: " << ret << std::endl;

    return 0;
}

