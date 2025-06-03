#include <iostream>
#include <string>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_2.h>
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


void FillVaSurfaceWithRed(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height) {
    VAImage image;

    VAImageFormat nv12Format = {};
    nv12Format.fourcc = VA_FOURCC_NV12;
    nv12Format.byte_order = VA_LSB_FIRST;
    nv12Format.bits_per_pixel = 12; // NV12 is 12 bits per pixel (8 for Y + 4 for UV)
    nv12Format.depth = 8;           // 8 bits per component

    // These masks are irrelevant for NV12 but must be zeroed for safety
    nv12Format.red_mask = 0;
    nv12Format.green_mask = 0;
    nv12Format.blue_mask = 0;
    nv12Format.alpha_mask = 0;
    memset(nv12Format.va_reserved, 0, sizeof(nv12Format.va_reserved));

    VAStatus va_status = vaCreateImage(vaDisplay, &nv12Format, width, height, &image);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to create VAImage with NV12 format. Status: " << va_status << "\n";
        return;
    }

    void* pData = nullptr;
    va_status = vaMapBuffer(vaDisplay, image.buf, &pData);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to map VA image buffer\n";
        vaDestroyImage(vaDisplay, image.image_id);
        return;
    }

    uint8_t* yPlane = static_cast<uint8_t*>(pData);
    uint8_t* uvPlane = yPlane + image.pitches[0] * height;

    // Fill with red (Y=76, U=84, V=255)
    for (int y = 0; y < height; ++y) {
        std::fill(yPlane + y* image.pitches[0], yPlane + y*image.pitches[0] + width, 76);
    }
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            uvPlane[y * image.pitches[1] + x * 2 + 0] = 84;   // U
            uvPlane[y * image.pitches[1] + x * 2 + 1] = 255;  // V
        }
    }

    vaUnmapBuffer(vaDisplay, image.buf);

    // Put image into surface
    va_status = vaPutImage(vaDisplay, vaSurface, image.image_id,
        0, 0, width, height,
        0, 0, width, height);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaPutImage failed\n";
    }

    va_status = vaSyncSurface(vaDisplay, vaSurface);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaSyncSurface failed after vaPutImage\n";
    }

    vaDestroyImage(vaDisplay, image.image_id);
}


void DumpVaSurfaceToNV12File(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height, const std::string& filename) {
    VAImage vaImage;
    VAStatus va_status;

    va_status = vaSyncSurface(vaDisplay, vaSurface);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaSyncSurface failed after vaPutImage\n";
    }

    va_status = vaDeriveImage(vaDisplay, vaSurface, &vaImage);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to derive image from VA surface. Error: " << va_status << std::endl;
        return;
    }

    // Map the image to access its data
    void* pData;
    va_status = vaMapBuffer(vaDisplay, vaImage.buf, &pData);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to map VA buffer. Error: " << va_status << std::endl;
        vaDestroyImage(vaDisplay, vaImage.image_id);
        return;
    }

    // Open the file for writing
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file for writing." << std::endl;
        vaUnmapBuffer(vaDisplay, vaImage.buf);
        vaDestroyImage(vaDisplay, vaImage.image_id);
        return;
    }

    // Write the Y plane
    uint8_t* yPlane = static_cast<uint8_t*>(pData);
    size_t ySize = vaImage.pitches[0] * height;

    // Write the UV plane
    uint8_t* uvPlane = yPlane + vaImage.pitches[0] * height;
    size_t uvSize = vaImage.pitches[1] * (height / 2);

    for (int y = 0; y < height; ++y) {
        file.write(reinterpret_cast<char*>(yPlane + y * vaImage.pitches[0]), width);
    }
    for (int y = 0; y < height / 2; ++y) {
        file.write(reinterpret_cast<char*>(uvPlane + y * vaImage.pitches[1]), width);
    }

    // Close the file
    file.close();

    // Unmap the buffer
    vaUnmapBuffer(vaDisplay, vaImage.buf);
    vaDestroyImage(vaDisplay, vaImage.image_id);
}




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

int ffmpegEncodeWin::FFMPEG_VAAPI_Debug() {


    UINT flags = 0;
    HRESULT hr = CreateDXGIFactory2(flags, __uuidof(IDXGIFactory2), (void**)&m_factory);
    if (FAILED(hr)) {
        std::cerr << "Failed to create CreateDXGIFactory2 device.\n";
        return -1;
    }
    m_factory->EnumAdapters1(0, &m_adapter);

    m_width = 1920;
    m_height = 1080;

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

    /*
        Initialize VAAPI;
    */
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


    // Create VA surface for dubug
    VASurfaceAttrib createSurfacesAttribList[3] = {};

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

    EncodedLoop();

    return 0;
}

int ffmpegEncodeWin::EncodedLoop(void)
{
    bool encodeFlag = true;

    for (int frameCount = 0; frameCount < 100; ++frameCount) {


        FillVaSurfaceWithRed(m_vaDisplay, vaSurfacesDebug, m_width, m_height);



        // verification - write captured frame to a file 
        if (frameCount == 1)
        {
            DumpVaSurfaceToNV12File(m_vaDisplay, vaSurfacesDebug,
                m_width, m_height, "C:\\proj\\prototype\\ScreenCaptureDX12VaApiShare\\ffmpegEncodeWindows\\frame_dump.nv12");

        }

        // encode captured frame, d3d11Texture maps to vaSurfaces[0] via D3D12 resource
        // somehow this is not happening - #TODO debug
        if (encodeFlag) {
            ScEncodeFrames(reinterpret_cast<void*>(static_cast<uintptr_t>(vaSurfacesDebug)), true);
        }

    }

    if (encodeFlag) {
        ScEncodeClose();
    }

    return 0;
}

int ffmpegEncodeWin::TestVaSurfaces(void) {
    VAStatus va_status;

    UINT flags = 0;
    HRESULT hr = CreateDXGIFactory2(flags, __uuidof(IDXGIFactory2), (void**)&m_factory);
    if (FAILED(hr)) {
        std::cerr << "Failed to create CreateDXGIFactory2 device.\n";
        return -1;
    }
    m_factory->EnumAdapters1(0, &m_adapter);

    m_width = 1920;
    m_height = 1080;

    /*
        Initialize VAAPI;
    */
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

    // Create VA surface for dubug
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


    CheckvaQueryConfigProfiles();

    FillVaSurfaceWithRed(m_vaDisplay, vaSurfacesDebug, m_width, m_height);

    // verification - write captured frame to a file 
    DumpVaSurfaceToNV12File(m_vaDisplay, vaSurfacesDebug,
        m_width, m_height, "frame_dump.nv12");

    return 0;
}

int main(int argc, char* argv[])
{

    ffmpegEncodeWin sc;

    int ret = 0;
    
    if (0) {
        ret = sc.FFMPEG_VAAPI_Debug();
    }
    else {
        ret = sc.TestVaSurfaces();
    }
    std::cout << "RET: " << ret << std::endl;

    return 0;
}

