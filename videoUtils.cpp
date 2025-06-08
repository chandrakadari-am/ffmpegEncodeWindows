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
#include "videoUtils.h"

void DumpD3D1TextureToFile(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture, const std::string& filename, bool isNVFormat) {
    std::string file;
    if (isNVFormat) {
        file = filename + ".yuv";
        SaveNV12TextureToFile(device, context, texture, file);
    }
    else {
        file = filename + ".bgra";
        SaveBGRATextureToFile(device, context, texture, file);
    }
}

void DumpVaSurfaceToFile(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height, const std::string& filename, bool isNVFormat) {
    std::string file;
    if (isNVFormat) {
        file = filename + ".yuv";
        DumpVaSurfaceToNV12File(vaDisplay, vaSurface, width, height, file);
    }
    else {
        file = filename + ".bgra";
        DumpVaSurfaceToBGRAFile(vaDisplay, vaSurface, width, height, file);
    }
}



void DumpD3D12ResourceToFile(ComPtr<ID3D12Device> d3d12Device, ComPtr<ID3D12GraphicsCommandList> d3d12CommandList, ComPtr<ID3D12CommandQueue> d3d12CommandQueue,
             ComPtr<ID3D12Resource> resourceD3D12, const std::string& filename, bool isNVFormat) {
    std::string file;
    if (isNVFormat) {
        file = filename + ".yuv";
        CopyNV12TextureToFile(d3d12Device, d3d12CommandList, d3d12CommandQueue, resourceD3D12, file);
    }
    else {
        file = filename + ".bgra";
        CopyD3D12BGRATextureToFile(d3d12Device, d3d12CommandList, d3d12CommandQueue, resourceD3D12, file);
    }
}

void DumpVaSurfaceToNV12File(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height, const std::string& filename) {
    VAImage vaImage;
    VAStatus va_status;

    va_status = vaSyncSurface(vaDisplay, vaSurface);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaSyncSurface failed after vaPutImage\n";
    }
    /*
    va_status = vaDeriveImage(vaDisplay, vaSurface, &vaImage);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to derive image from VA surface. Error: " << va_status << std::endl;
        return;
    }
    */
    //va_status = vaDeriveImage(vaDisplay, vaSurface, &vaImage);
    {
        VAImageFormat nv12Format = {};
        nv12Format.fourcc = VA_FOURCC_RGBX;//VA_FOURCC_NV12;
        nv12Format.byte_order = VA_LSB_FIRST;
        nv12Format.bits_per_pixel = 12; // NV12 is 12 bits per pixel (8 for Y + 4 for UV)
        nv12Format.depth = 8;           // 8 bits per component
        va_status = vaCreateImage(vaDisplay, &nv12Format, width, height, &vaImage);
        if (va_status != VA_STATUS_SUCCESS) {
            std::cerr << "DumpVaSurfaceToNV12File, vaCreateImage Failed Error: " << va_status << std::endl;
            return;
        }
    }

    va_status = vaGetImage(vaDisplay, vaSurface, 0, 0, width, height, vaImage.image_id);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "DumpVaSurfaceToNV12File, vaGetImage Failed Error: " << va_status << std::endl;
        return;
    }

    printf("Read: Y offset[0]: %u\n", vaImage.offsets[0]);
    printf("Read: U/V offsets[1]: %u, offsets[2]: %u\n", vaImage.offsets[1], vaImage.offsets[2]);

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

    for (int y = 1000; y < 1016; ++y) {
        if (y == 1000) printf("\n Y : ");
        printf("%02X ", yPlane[y]);
    }
    for (int y = 1000; y < 1016; ++y) {
        if (y == 1000) printf("\nUV : ");
        printf("%02X ", uvPlane[y]);
    }
    printf("\n");
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

bool SaveD3D12NV12TextureToFile(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    ID3D12CommandQueue* cmdQueue,
    ID3D12Resource* nv12Texture,
    UINT width,
    UINT height,
    const std::wstring& filePath
) {
    if (!device || !cmdList || !cmdQueue || !nv12Texture) return false;

    D3D12_RESOURCE_DESC texDesc = nv12Texture->GetDesc();

    UINT64 rowPitch = (width + 255) & ~255; // pitch is typically 256-aligned for NV12
    UINT64 yPlaneSize = rowPitch * height;
    UINT64 uvPlaneSize = rowPitch * (height / 2);
    UINT64 totalSize = yPlaneSize + uvPlaneSize;

    // Create readback buffer
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = totalSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer)
    );
    if (FAILED(hr)) {
        std::wcerr << L"Failed to create readback buffer\n";
        return false;
    }

    // Copy from NV12 texture to readback buffer
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readbackBuffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset = 0;
    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_NV12;
    dst.PlacedFootprint.Footprint.Width = width;
    dst.PlacedFootprint.Footprint.Height = height;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = nv12Texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Execute and wait
    cmdList->Close();
    ID3D12CommandList* cmdLists[] = { cmdList };
    cmdQueue->ExecuteCommandLists(1, cmdLists);

    // Sync
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceVal = 1;
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    cmdQueue->Signal(fence.Get(), fenceVal);
    fence->SetEventOnCompletion(fenceVal, fenceEvent);
    WaitForSingleObject(fenceEvent, INFINITE);
    CloseHandle(fenceEvent);

    // Map and write
    void* data = nullptr;
    D3D12_RANGE range = { 0, static_cast<SIZE_T>(totalSize) };
    hr = readbackBuffer->Map(0, &range, &data);
    if (FAILED(hr)) {
        std::wcerr << L"Failed to map readback buffer\n";
        return false;
    }

    // Save to file
    FILE* f = nullptr;
#ifdef _MSC_VER
    _wfopen_s(&f, filePath.c_str(), L"wb");
#else
    f = fopen(std::string(filePath.begin(), filePath.end()).c_str(), "wb");
#endif
    if (!f) {
        readbackBuffer->Unmap(0, nullptr);
        std::wcerr << L"Failed to open output file\n";
        return false;
    }

    // Y + UV
    fwrite(data, 1, static_cast<size_t>(totalSize), f);
    fclose(f);
    readbackBuffer->Unmap(0, nullptr);

    std::wcout << L"[OK] Saved NV12 frame to: " << filePath << std::endl;
    return true;
}

HRESULT SaveBGRATextureToFile(
    ID3D11Device* d3d11Device,
    ID3D11DeviceContext* d3d11Context,
    ID3D11Texture2D* texture,
    const std::string& filePath)
{
    if (!d3d11Device || !texture) return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);

    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        std::cerr << "Unsupported format. Expected DXGI_FORMAT_B8G8R8A8_UNORM\n";
        return E_FAIL;
    }

    // Create a staging texture for CPU readback
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTex;
    HRESULT hr = d3d11Device->CreateTexture2D(&stagingDesc, nullptr, &stagingTex);
    if (FAILED(hr)) return hr;

    // Copy the GPU texture to the staging texture
    d3d11Context->CopyResource(stagingTex.Get(), texture);

    // Map it to CPU memory
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = d3d11Context->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return hr;

    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile) {
        d3d11Context->Unmap(stagingTex.Get(), 0);
        return E_FAIL;
    }

    // Write each row, handling row pitch
    for (UINT y = 0; y < desc.Height; ++y) {
        const uint8_t* rowData = reinterpret_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch;
        outFile.write(reinterpret_cast<const char*>(rowData), desc.Width * 4);  // 4 bytes per pixel (BGRA)
    }

    outFile.close();
    d3d11Context->Unmap(stagingTex.Get(), 0);

    std::cout << "BGRA texture dumped to file: " << filePath << std::endl;
    return S_OK;
}


bool SaveNV12TextureToFile(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* nv12Texture, const std::string& filename) {
    if (!nv12Texture) return false;

    D3D11_TEXTURE2D_DESC desc = {};
    nv12Texture->GetDesc(&desc);

    // Create staging texture for CPU read
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> stagingTex;
    HRESULT hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTex);
    if (FAILED(hr)) {
        std::cerr << "Failed to create staging texture\n";
        return false;
    }

    // Copy GPU → CPU
    context->CopyResource(stagingTex.Get(), nv12Texture);

    // Map to access pixels
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = context->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        std::cerr << "Failed to map texture\n";
        return false;
    }

    // Write to file
    FILE* f = nullptr;
#ifdef _MSC_VER
    fopen_s(&f, filename.c_str(), "wb");
#else
    f = fopen(filename.c_str(), "wb");
#endif
    if (!f) {
        context->Unmap(stagingTex.Get(), 0);
        std::cerr << "Failed to open output file\n";
        return false;
    }

    uint8_t* src = reinterpret_cast<uint8_t*>(mapped.pData);
    uint32_t pitch = mapped.RowPitch;
    int width = desc.Width;
    int height = desc.Height;

    // Write Y plane
    for (int y = 0; y < height; ++y) {
        fwrite(src + pitch * y, 1, width, f);
    }

    // Write interleaved UV plane (height / 2 lines)
    uint8_t* uv_start = src + pitch * height;
    for (int y = 0; y < height / 2; ++y) {
        fwrite(uv_start + pitch * y, 1, width, f); // width bytes = UV interleaved
    }

    fclose(f);
    context->Unmap(stagingTex.Get(), 0);

    std::cout << "[OK] Saved NV12 frame to " << filename << std::endl;
    return true;
}

HRESULT CopyD3D12BGRATextureToFile(
    ComPtr<ID3D12Device> d3d12Device,
    ComPtr<ID3D12GraphicsCommandList> d3d12CommandList,
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue,
    ComPtr<ID3D12Resource> texture,
    const std::string& outputFilePath)
{
    if (!d3d12Device || !d3d12CommandList || !d3d12CommandQueue || !texture) {
        return E_INVALIDARG;
    }

    D3D12_RESOURCE_DESC desc = texture->GetDesc();
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        std::wcerr << L"Unsupported format. Expected DXGI_FORMAT_B8G8R8A8_UNORM\n";
        return E_FAIL;
    }

    UINT width = static_cast<UINT>(desc.Width);
    UINT height = desc.Height;

    std::wcout << L"Dumping BGRA D3D12 texture: " << width << L"x" << height << std::endl;

    // Get footprint
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT64 totalBytes = 0;
    d3d12Device->GetCopyableFootprints(
        &desc,
        0,
        1,
        0,
        &footprint,
        nullptr,
        nullptr,
        &totalBytes
    );

    // Create readback buffer
    D3D12_RESOURCE_DESC readbackDesc = {};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = totalBytes;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    readbackDesc.SampleDesc.Count = 1;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = d3d12Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer)
    );
    if (FAILED(hr)) return hr;

    // Copy texture into readback buffer
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = texture.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = readbackBuffer.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;

    d3d12CommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    hr = d3d12CommandList->Close();
    if (FAILED(hr)) return hr;

    ID3D12CommandList* commandLists[] = { d3d12CommandList.Get() };
    d3d12CommandQueue->ExecuteCommandLists(1, commandLists);

    // Synchronize with fence
    ComPtr<ID3D12Fence> fence;
    hr = d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) return hr;

    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) return HRESULT_FROM_WIN32(GetLastError());

    UINT64 fenceValue = 1;
    d3d12CommandQueue->Signal(fence.Get(), fenceValue);

    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);

    // Map and write to file
    void* mappedData = nullptr;
    hr = readbackBuffer->Map(0, nullptr, &mappedData);
    if (FAILED(hr)) return hr;

    std::ofstream outFile(outputFilePath, std::ios::binary);
    if (!outFile) {
        readbackBuffer->Unmap(0, nullptr);
        return E_FAIL;
    }

    uint8_t* srcPtr = static_cast<uint8_t*>(mappedData);
    for (UINT y = 0; y < height; ++y) {
        outFile.write(reinterpret_cast<const char*>(srcPtr + y * footprint.Footprint.RowPitch), width * 4);
    }

    outFile.close();
    readbackBuffer->Unmap(0, nullptr);

    std::cout << "Saved BGRA texture to: " << outputFilePath << std::endl;
    return S_OK;
}

void DumpVaSurfaceToBGRAFile(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height, std::string& filename) {
    VAImage image;
    if (vaDeriveImage(vaDisplay, vaSurface, &image) != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to derive VA image\n";
        return;
    }

    void* pBuffer = nullptr;
    if (vaMapBuffer(vaDisplay, image.buf, &pBuffer) != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to map VA image buffer\n";
        vaDestroyImage(vaDisplay, image.image_id);
        return;
    }

    // image.data points to the surface pixel data in RGB32 format (usually BGRA)
    uint8_t* srcData = (uint8_t*)pBuffer + image.offsets[0];
    int pitch = image.pitches[0];  // bytes per row

    // Open file for writing
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        std::cerr << "Failed to open output file\n";
        vaUnmapBuffer(vaDisplay, image.buf);
        vaDestroyImage(vaDisplay, image.image_id);
        return;
    }

    for (int y = 1000; y < 1016; ++y) {
        if (y == 1000) printf("\n BGRA : ");
        printf("%02X ", srcData[y]);
    }

    printf("\n");

    // Write row by row because pitch may be larger than width * 4
    for (int y = 0; y < height; ++y) {
        outFile.write(reinterpret_cast<const char*>(srcData + y * pitch), width * 4);
    }

    std::cout << "Saved BGRA dump to " << filename << std::endl;

    vaUnmapBuffer(vaDisplay, image.buf);
    vaDestroyImage(vaDisplay, image.image_id);
}



void fillBGRAWithRed(uint8_t* buffer, int width, int height, int pitch) {
    if (!buffer) return;

    for (int y = 0; y < height; ++y) {
        uint8_t* row = buffer + y * pitch;
        for (int x = 0; x < width; ++x) {
            int offset = x * 4;
            row[offset + 0] = 0;   // Blue
            row[offset + 1] = 0;   // Green
            row[offset + 2] = 255; // Red
            row[offset + 3] = 255; // Alpha
        }
    }
}

void FillVaSurfaceWithRGBA(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height) {

    VAImage image;
    VAImageFormat rgbaFormat = {};
    rgbaFormat.fourcc = VA_FOURCC_RGBA;
    rgbaFormat.byte_order = VA_LSB_FIRST;
    rgbaFormat.bits_per_pixel = 32; // 8 bits per channel × 4 channels (R, G, B, A)
    rgbaFormat.depth = 8;           // 8 bits per channel

    // Set channel masks assuming little-endian memory layout
    rgbaFormat.red_mask = 0x000000FF; // R in lowest byte
    rgbaFormat.green_mask = 0x0000FF00; // G in second byte
    rgbaFormat.blue_mask = 0x00FF0000; // B in third byte
    rgbaFormat.alpha_mask = 0xFF000000; // A in fourth byte

    memset(rgbaFormat.va_reserved, 0, sizeof(rgbaFormat.va_reserved));

    // Create image
    VAStatus va_status = vaCreateImage(vaDisplay, &rgbaFormat, width, height, &image);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to create VAImage with RGBA format. Status: " << va_status << "\n";
        return;
    }

    // Map buffer
    void* pData = nullptr;
    va_status = vaMapBuffer(vaDisplay, image.buf, &pData);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to map VA image buffer\n";
        vaDestroyImage(vaDisplay, image.image_id);
        return;
    }


    uint8_t* bgra_data = (uint8_t*)pData + image.offsets[0];
    int pitch = image.pitches[0];

    fillBGRAWithRed(bgra_data, width, height, pitch);


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

bool DumpVaSurfaceToFile(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height, const std::string& filename) {
    VAImage image;
    VAStatus va_status;

    VAImageFormat rgbaFormat = {};
    rgbaFormat.fourcc = VA_FOURCC_RGBA;
    rgbaFormat.byte_order = VA_LSB_FIRST;
    rgbaFormat.bits_per_pixel = 32; // 8 bits per channel × 4 channels (R, G, B, A)
    rgbaFormat.depth = 8;           // 8 bits per channel

    // Set channel masks assuming little-endian memory layout
    rgbaFormat.red_mask = 0x000000FF; // R in lowest byte
    rgbaFormat.green_mask = 0x0000FF00; // G in second byte
    rgbaFormat.blue_mask = 0x00FF0000; // B in third byte
    rgbaFormat.alpha_mask = 0xFF000000; // A in fourth byte

    memset(rgbaFormat.va_reserved, 0, sizeof(rgbaFormat.va_reserved));

    // Create image
    va_status = vaCreateImage(vaDisplay, &rgbaFormat, width, height, &image);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "Failed to create VAImage with RGBA format. Status: " << va_status << "\n";
        return false;
    }

    // Derive image from the surface
    va_status = vaGetImage(vaDisplay, vaSurface, 0, 0, width, height, image.image_id);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaGetImage failed: " << va_status << std::endl;
        return false;
    }

    // Map image buffer
    void* pData = nullptr;
    va_status = vaMapBuffer(vaDisplay, image.buf, &pData);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaMapBuffer failed: " << va_status << std::endl;
        vaDestroyImage(vaDisplay, image.image_id);
        return false;
    }

    // Open output file
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        std::cerr << "Failed to open file for writing: " << filename << std::endl;
        vaUnmapBuffer(vaDisplay, image.buf);
        vaDestroyImage(vaDisplay, image.image_id);
        return false;
    }

    // Dump raw pixel data (assumes 1 plane and 4 bytes per pixel)
    uint8_t* data = (uint8_t*)pData + image.offsets[0];
    int pitch = image.pitches[0];

    for (int y = 0; y < height; ++y) {
        outFile.write(reinterpret_cast<char*>(data + y * pitch), width * 4); // 4 bytes/pixel for RGBA
    }

    outFile.close();

    vaUnmapBuffer(vaDisplay, image.buf);
    vaDestroyImage(vaDisplay, image.image_id);

    std::cout << "Surface dumped to: " << filename << std::endl;
    return true;
}

void FillVaSurfaceWithRed(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height) {
    VAImage image;
    VAStatus va_status = vaDeriveImage(vaDisplay, vaSurface, &image);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaDeriveImage failed\n";
        return;
    }
    static int cnt = 0;
    void* pData = nullptr;
    va_status = vaMapBuffer(vaDisplay, image.buf, &pData);
    if (va_status != VA_STATUS_SUCCESS || !pData) {
        std::cerr << "vaMapBuffer failed\n";
        vaDestroyImage(vaDisplay, image.image_id);
        return;
    }

    printf("Image Size: %dx%d\n", width, height);
    printf("Write: Y pitches[0]: %u\n", image.pitches[0]);
    printf("Write: U/V pitches[1]: %u, pitches[2]: %u\n", image.pitches[1], image.pitches[2]);
    printf("Write: Y offset[0]: %u\n", image.offsets[0]);
    printf("Write: U/V offsets[1]: %u, offsets[2]: %u\n", image.offsets[1], image.offsets[2]);


    uint8_t* yPlane = static_cast<uint8_t*>(pData) + image.offsets[0];
    uint8_t* uvPlane = static_cast<uint8_t*>(pData) + image.offsets[1];

    for (int y = 0; y < height; ++y) {
        memset(yPlane + y * image.pitches[0], 76, width);  // Y = 76 (red)
    }

    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width; x += 2) {
            uint8_t* uv = uvPlane + y * image.pitches[1] + x;
            if (x < (width / 2)) {
                uv[0] = (cnt)&255;  //84 // U
                uv[1] = 255;  // V
            }
            else {
                uv[0] = 240;   // U
                uv[1] = (cnt) & 255;  //110;  // V
            }
        }
    }
    cnt+=2;

    vaUnmapBuffer(vaDisplay, image.buf);
    vaDestroyImage(vaDisplay, image.image_id);

    va_status = vaSyncSurface(vaDisplay, vaSurface);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaSyncSurface failed\n";
    }
}


void FillVaSurfaceWithRedOld(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height) {
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

    //VAStatus va_status = vaCreateImage(vaDisplay, &nv12Format, width, height, &image);
    VAStatus va_status = vaDeriveImage(vaDisplay, vaSurface, &image);
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


    printf("Write1: Y pitches[0]: %u\n", image.pitches[0]);
    printf("Write1: U/V pitches[1]: %u, pitches[2]: %u\n", image.pitches[1], image.pitches[2]);
    printf("Write1: Y offset[0]: %u\n", image.offsets[0]);
    printf("Write1: U/V offsets[1]: %u, offsets[2]: %u\n", image.offsets[1], image.offsets[2]);



    uint8_t* yPlane = static_cast<uint8_t*>(pData);
    uint8_t* uvPlane = yPlane + image.pitches[0] * height;

    // Fill with red (Y=76, U=84, V=255)
    //for (int y = 0; y < height; ++y) {
    //    std::fill(yPlane + y* image.pitches[0], yPlane + y*image.pitches[0] + width, 76);
    //}
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            yPlane[y * image.pitches[0] + x] = 76;   // Y
        }
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

bool CopyVaSurfaceManual(VADisplay vaDisplay, VASurfaceID src, VASurfaceID dst, int width, int height) {
    VAStatus va_status;
    VAImage image;
    void* data = nullptr;

    VAImageFormat nv12Format = {};
    nv12Format.fourcc = VA_FOURCC_NV12;
    nv12Format.byte_order = VA_LSB_FIRST;
    nv12Format.bits_per_pixel = 12;
    nv12Format.depth = 8;

    va_status = vaCreateImage(vaDisplay, &nv12Format, width, height, &image);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaCreateImage failed\n";
        return false;
    }

    va_status = vaGetImage(vaDisplay, src, 0, 0, width, height, image.image_id);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaGetImage failed\n";
        vaDestroyImage(vaDisplay, image.image_id);
        return false;
    }

    va_status = vaMapBuffer(vaDisplay, image.buf, &data);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaMapBuffer failed\n";
        vaDestroyImage(vaDisplay, image.image_id);
        return false;
    }

    va_status = vaUnmapBuffer(vaDisplay, image.buf);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaUnmapBuffer failed\n";
        vaDestroyImage(vaDisplay, image.image_id);
        return false;
    }

    // Now push to destination surface
    va_status = vaPutImage(vaDisplay, dst, image.image_id,
        0, 0, width, height,
        0, 0, width, height);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaPutImage failed in copy\n";
    }

    vaDestroyImage(vaDisplay, image.image_id);
    return va_status == VA_STATUS_SUCCESS;
}


bool CopyVaSurfaceManual11(VADisplay vaDisplay, VASurfaceID src, VASurfaceID dst, int width, int height) {
    VAStatus va_status;
    VAImage image;
    void* data = nullptr;

    va_status = vaDeriveImage(vaDisplay, src, &image);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaDeriveImage failed\n";
        return false;
    }

    va_status = vaMapBuffer(vaDisplay, image.buf, &data);
    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaMapBuffer failed\n";
        vaDestroyImage(vaDisplay, image.image_id);
        return false;
    }

    // Put it into destination surface
    va_status = vaPutImage(
        vaDisplay, dst, image.image_id,
        0, 0, width, height,
        0, 0, width, height);

    vaUnmapBuffer(vaDisplay, image.buf);
    vaDestroyImage(vaDisplay, image.image_id);

    if (va_status != VA_STATUS_SUCCESS) {
        std::cerr << "vaPutImage failed in copy\n";
        return false;
    }

    return true;
}

HRESULT CopyNV12TextureToFile(
    ComPtr<ID3D12Device> d3d12Device,
    ComPtr<ID3D12GraphicsCommandList> d3d12CommandList,
    ComPtr<ID3D12CommandQueue> d3d12CommandQueue,
    ComPtr<ID3D12Resource> sharedTextureD3D12,
    const std::string& outputFilePath
) {
    if (!d3d12Device || !d3d12CommandList || !d3d12CommandQueue || !sharedTextureD3D12) {
        return E_INVALIDARG;
    }

    D3D12_RESOURCE_DESC texDesc = sharedTextureD3D12->GetDesc();
    UINT width = static_cast<UINT>(texDesc.Width);
    UINT height = texDesc.Height;

    std::cout << "D3D12 Shared Frame SIZE: " << width << " x " << height << std::endl;

    // Two subresources: 0 = Y, 1 = UV
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[2];
    UINT64 rowSizesInBytes[2];
    UINT numRows[2];
    UINT64 totalBytes = 0;

    d3d12Device->GetCopyableFootprints(
        &texDesc,
        0,
        2,
        0,
        footprints,
        numRows,
        rowSizesInBytes,
        &totalBytes
    );

    // Create readback buffer for both planes
    D3D12_RESOURCE_DESC readbackDesc = {};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = totalBytes;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    readbackDesc.SampleDesc.Count = 1;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    ComPtr<ID3D12Resource> readbackBuffer;
    HRESULT hr = d3d12Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &readbackDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&readbackBuffer)
    );
    if (FAILED(hr)) return hr;

    // Copy both subresources: Y and UV
    for (UINT plane = 0; plane < 2; ++plane) {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = sharedTextureD3D12.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = plane;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = readbackBuffer.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprints[plane];

        d3d12CommandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    hr = d3d12CommandList->Close();
    if (FAILED(hr)) return hr;

    ID3D12CommandList* commandLists[] = { d3d12CommandList.Get() };
    d3d12CommandQueue->ExecuteCommandLists(1, commandLists);

    // Synchronize with fence
    ComPtr<ID3D12Fence> fence;
    UINT64 fenceValue = 1;
    hr = d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) return hr;

    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) return HRESULT_FROM_WIN32(GetLastError());

    hr = d3d12CommandQueue->Signal(fence.Get(), fenceValue);
    if (FAILED(hr)) {
        CloseHandle(fenceEvent);
        return hr;
    }

    if (fence->GetCompletedValue() < fenceValue) {
        fence->SetEventOnCompletion(fenceValue, fenceEvent);
        WaitForSingleObject(fenceEvent, INFINITE);
    }
    CloseHandle(fenceEvent);

    // Map and write both planes
    void* mappedData = nullptr;
    hr = readbackBuffer->Map(0, nullptr, &mappedData);
    if (FAILED(hr)) return hr;

    std::ofstream outFile(outputFilePath, std::ios::binary);
    if (!outFile) {
        readbackBuffer->Unmap(0, nullptr);
        return E_FAIL;
    }

    // Write Y plane
    const uint8_t* yPlane = reinterpret_cast<uint8_t*>(mappedData) + footprints[0].Offset;
    for (UINT y = 0; y < height; ++y) {
        outFile.write(reinterpret_cast<const char*>(yPlane + y * footprints[0].Footprint.RowPitch), width);
    }

    // Write UV plane (interleaved, half height)
    const uint8_t* uvPlane = reinterpret_cast<uint8_t*>(mappedData) + footprints[1].Offset;
    for (UINT y = 0; y < height / 2; ++y) {
        outFile.write(reinterpret_cast<const char*>(uvPlane + y * footprints[1].Footprint.RowPitch), width);
    }

    outFile.close();
    readbackBuffer->Unmap(0, nullptr);

    std::cout << "NV12 texture copied and saved to file: " << outputFilePath << std::endl;
    return S_OK;
}




/*


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

    / *
        Initialize VAAPI;
    * /
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
createSurfacesAttribList[1].value.value.i = VA_FOURCC_BGRX;//VA_FOURCC_NV12;

va_status = vaCreateSurfaces(m_vaDisplay, VA_RT_FORMAT_RGB32/ *VA_RT_FORMAT_YUV420 * /, m_width, m_height, &vaSurfacesDebug, 1, createSurfacesAttribList, 2);
if (va_status != VA_STATUS_SUCCESS)
{
    vaTerminate(m_vaDisplay);
    std::cerr << "[FAIL] Failed to vaCreateSurfaces - vaSurfacesDebug. va_status: " << std::hex << va_status << std::endl;
    return -3;
}
std::cout << "[ OK ] vaCreateSurfaces vaSurfacesDebug successful va_status: " << std::hex << va_status << std::endl;


CheckvaQueryConfigProfiles();

FillVaSurfaceWithRGBA(m_vaDisplay, vaSurfacesDebug, m_width, m_height);
//FillVaSurfaceWithRed(m_vaDisplay, vaSurfacesDebug, m_width, m_height);


// verification - write captured frame to a file 
//DumpVaSurfaceToNV12File(m_vaDisplay, vaSurfacesDebug, m_width, m_height, "frame_dump.nv12");
DumpVaSurfaceToFile(m_vaDisplay, vaSurfacesDebug, m_width, m_height, "frame_dump.rgba");

return 0;
}


*/