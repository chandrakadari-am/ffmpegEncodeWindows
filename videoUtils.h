#pragma once
#include <iostream>
#include <string>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include <chrono>
#include <thread>
#include <vfw.h>

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

void DumpVaSurfaceToNV12File(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height, const std::string& filename);
void FillVaSurfaceWithRed(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height);
void FillVaSurfaceWithRedOld(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height);
bool DumpVaSurfaceToFile(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height, const std::string& filename);
void FillVaSurfaceWithRGBA(VADisplay vaDisplay, VASurfaceID vaSurface, int width, int height);
void fillBGRAWithRed(uint8_t* buffer, int width, int height, int pitch);
bool SaveNV12TextureToFile(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* nv12Texture, const std::string& filename);
bool SaveD3D12NV12TextureToFile(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    ID3D12CommandQueue* cmdQueue,
    ID3D12Resource* nv12Texture,
    UINT width,
    UINT height,
    const std::wstring& filePath
);
bool CopyVaSurfaceManual(VADisplay vaDisplay, VASurfaceID src, VASurfaceID dst, int width, int height);