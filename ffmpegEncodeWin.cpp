#include <iostream>
#include <string>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_2.h>
//#include <d3d11_1.h>
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

int ffmpegEncodeWin::CreateFactory() {
    UINT flags = 0;
    HRESULT hr = CreateDXGIFactory2(flags, __uuidof(IDXGIFactory2), (void**)&m_factory);
    if (FAILED(hr)) {
        std::cerr << "Failed to create CreateDXGIFactory2 device.\n";
        return -1;
    }
    m_factory->EnumAdapters1(0, &m_adapter);
    return 0;
}

int ffmpegEncodeWin::CreateSurfaces() {
    /*
    Initialize VAAPI;
    */
    HRESULT hr;
    DXGI_ADAPTER_DESC descva = {};
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
    VAStatus va_status = vaInitialize(m_vaDisplay, &major_ver, &minor_ver);
    if (va_status != VA_STATUS_SUCCESS)
    {
        std::cerr << "Failed to vaInitialize. va_status: " << std::hex << va_status << std::endl;
        return -1;
    }
    std::cout << "vaInitialize successful va_status: " << std::hex << va_status << std::endl;

    // Create VA Surfaces
    vaSetErrorCallback(m_vaDisplay, message_callback, 0);
    vaSetInfoCallback(m_vaDisplay, message_callback, 0);

    VASurfaceAttrib createSurfacesAttribList[2] = {};
    createSurfacesAttribList[0].type = VASurfaceAttribMemoryType;
    createSurfacesAttribList[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    createSurfacesAttribList[0].value.type = VAGenericValueTypeInteger;
    createSurfacesAttribList[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_VA;

    createSurfacesAttribList[1].type = VASurfaceAttribPixelFormat;
    createSurfacesAttribList[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
    createSurfacesAttribList[1].value.type = VAGenericValueTypeInteger;
    createSurfacesAttribList[1].value.value.i = VA_FOURCC_NV12;

    va_status = vaCreateSurfaces(m_vaDisplay, VA_RT_FORMAT_YUV420, m_width, m_height, &vaSurfacesDebug, 1, createSurfacesAttribList, 2);
    if (va_status != VA_STATUS_SUCCESS)
    {
        vaTerminate(m_vaDisplay);
        std::cerr << "[FAIL] Failed to vaCreateSurfaces - vaSurfacesDebug. va_status: " << std::hex << va_status << std::endl;
        return -3;
    }
    std::cout << "[ OK ] vaCreateSurfaces vaSurfacesDebug successful va_status: " << std::hex << va_status << std::endl;


    VASurfaceAttrib createSurfacesAttribList1[3] = {};
    HANDLE vaHandles[] = { m_renderSharedHandle };

    createSurfacesAttribList1[0].type = VASurfaceAttribMemoryType;
    createSurfacesAttribList1[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
    createSurfacesAttribList1[0].value.type = VAGenericValueTypeInteger;
    createSurfacesAttribList1[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE;

    createSurfacesAttribList1[1].type = VASurfaceAttribExternalBufferDescriptor;
    createSurfacesAttribList1[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
    createSurfacesAttribList1[1].value.type = VAGenericValueTypePointer;
    createSurfacesAttribList1[1].value.value.p = static_cast<void*>(vaHandles);

    createSurfacesAttribList1[2].type = VASurfaceAttribPixelFormat;
    createSurfacesAttribList1[2].flags = VA_SURFACE_ATTRIB_SETTABLE;
    createSurfacesAttribList1[2].value.type = VAGenericValueTypeInteger;
    createSurfacesAttribList1[2].value.value.i = VA_FOURCC_NV12;

    va_status = vaCreateSurfaces(m_vaDisplay, VA_RT_FORMAT_YUV420, m_width, m_height, &m_VASurfaceNV12New, 1, createSurfacesAttribList1, _countof(createSurfacesAttribList1));
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

    m_width = 1920;
    m_height = 1200;

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
    ComPtr<IDXGIOutput> dxgiOutput;
    hr = m_adapter->EnumOutputs(0, &dxgiOutput);
    if (FAILED(hr)) return hr;

    ComPtr<IDXGIOutput1> dxgiOutput1;
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
    texDesc.Format = DXGI_FORMAT_NV12;  // NV12 format for video frames
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;//0;                  // NV12 is typically not bindable as render target
    texDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;//D3D11_RESOURCE_MISC_SHARED;  // Enable sharing!

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
    hr = d3d12Device->OpenSharedHandle(m_renderSharedHandle, __uuidof(ID3D12Resource), (void**)sharedTextureD3D12.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "Failed to open shared handle\n";
        return nullptr;
    }
    if (FAILED(hr)) {
        std::cout << "ERROR >>>>>>> " << std::endl;
        return nullptr;
    }
    ComPtr<IDXGIKeyedMutex> keyedMutex11;
    hr = sharedTextureD3D11.As(&keyedMutex11);

    if (FAILED(hr)) {
        std::cerr << "Failed to get IDXGIKeyedMutex from sharedTextureD3D12\n";
        return nullptr;
    }


/*
        // Try to acquire next frame
        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        hr = outputDuplication->AcquireNextFrame(1000, &frameInfo, &desktopResource);
        if (FAILED(hr)) {
            std::cout << "ERROR >>>>>>> " << std::endl;
            return nullptr;
        }

        // Get D3D11 texture
        desktopResource.As(&acquiredTexture);


        if (SUCCEEDED(hr)) {
            keyedMutex11->AcquireSync(0, 500);  // Acquire D3D11 access
            
            // ... write/copy frame to sharedTextureD3D11
            if (converter.Convert(acquiredTexture.Get(), &sharedTextureD3D11)) {
                d3d11Context->Flush();   // Push to GPU

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

                SaveNV12TextureToFile(d3d11Device.Get(), d3d11Context.Get(), sharedTextureD3D11.Get(), "d3d11_frame_dump.yuv");
            }
            else {
                std::cerr << "Frame conversion failed\n";
            }

            keyedMutex11->ReleaseSync(1);            // Release D3D11 access, signal D3D12 access
            keyedMutex11->AcquireSync(1, 500);

            hr = CopyNV12TextureToFile(d3d12Device, commandList, commandQueue, sharedTextureD3D12, L"d3d12_nv12_dump.yuv");


            keyedMutex11->ReleaseSync(0);



        outputDuplication->ReleaseFrame();
 */

    return sharedTextureD3D12;
}


int ffmpegEncodeWin::FFMPEG_VAAPI_Debug() {

    // workout how to get frame size before creating D3D11 Textures and shared resources
    // set it for now
    m_width = 1920;
    m_height = 1200;


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

    vaSyncSurface(m_vaDisplay, m_VASurfaceNV12New);
    DumpVaSurfaceToNV12File(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height, "vaSufaceDump_before.yuv");
    FillVaSurfaceWithRed(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height);
    DumpVaSurfaceToNV12File(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height, "vaSufaceDump_after.yuv");
    vaSyncSurface(m_vaDisplay, m_VASurfaceNV12New);

    EncodedLoop();

    return 0;
}

int ffmpegEncodeWin::EncodedLoop(void)
{
    HRESULT hr;
    bool encodeFlag = true;

    for (int frameCount = 0; frameCount < 1; ++frameCount) {

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

        // convert and write frame to sharedTextureD3D11
        if (converter.Convert(acquiredTexture.Get(), &sharedTextureD3D11)) {
            d3d11Context->Flush();   // Push to GPU

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

            SaveNV12TextureToFile(d3d11Device.Get(), d3d11Context.Get(), sharedTextureD3D11.Get(), "d3d11_frame_dump.yuv");

            // encode captured frame, d3d11Texture maps to vaSurfaces via D3D12 resource
            // somehow this is not happening - #TODO debug
            if (encodeFlag) {
                if (frameCount == 0) {

                    DumpVaSurfaceToNV12File(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height, "vaSufaceDump.yuv");
                }
                vaSyncSurface(m_vaDisplay, m_VASurfaceNV12New);
                ScEncodeFrames(reinterpret_cast<void*>(static_cast<uintptr_t>(m_VASurfaceNV12New)), true);
                DumpVaSurfaceToNV12File(m_vaDisplay, m_VASurfaceNV12New, m_width, m_height, "vaSufaceDump_after_enc.yuv");

                if (frameCount == 0) {
                    hr = CopyNV12TextureToFile(d3d12Device, commandList, commandQueue, sharedTextureD3D12, L"d3d12_nv12_dump.yuv");
                }
            }
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

