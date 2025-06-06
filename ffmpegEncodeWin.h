#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <iostream>
#include <va/va.h>
#include <va/va_win32.h>
#include "sc_encoder_if.h"
#include "videoProcessor.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavdevice/avdevice.h>
#include <libavutil/hwcontext.h>
}

using namespace Microsoft::WRL;
using Microsoft::WRL::ComPtr;

inline std::string HrToString(HRESULT hr)
{
	char s_str[64] = {};
	sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
	return std::string(s_str);
}

class VAException : public std::runtime_error
{
public:
	VAException(VAStatus vas) : std::runtime_error(VAStatusToString(vas)), m_vas(vas) {}
	VAStatus Error() const { return m_vas; }
private:
	inline std::string VAStatusToString(VAStatus vas)
	{
		char s_str[64] = {};
		sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(vas));
		return std::string(s_str);
	}
	const VAStatus m_vas;
};

class HrException : public std::runtime_error
{
public:
	HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
	HRESULT Error() const { return m_hr; }
private:
	const HRESULT m_hr;
};

#define SAFE_RELEASE(p) if (p) (p)->Release()

inline void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw HrException(hr);
	}
}

inline void ThrowIfFailed(VAStatus va_status, const char* func)
{
	if (va_status != VA_STATUS_SUCCESS)
	{
		printf("%s:%s (%d) failed with VAStatus %x,exit\n", __func__, func, __LINE__, va_status);   \
			throw VAException(va_status);
	}
}

class ffmpegEncodeWin {

public:
	ffmpegEncodeWin(void);
	~ffmpegEncodeWin();
	int FFMPEG_VAAPI_Debug();
	int EncodedLoop(void);
	int CreateSurfaces();
	int CreateFactory();
	ComPtr<ID3D12Resource> CaptureScreenD3D12(ComPtr<ID3D12Device> d3d12Device, ComPtr<ID3D12CommandQueue> commandQueue);
	HRESULT InitializeD3D11Interop();
private:
	void CheckvaQueryConfigProfiles();

	ComPtr<ID3D11Device> d3d11Device;
	ComPtr<ID3D11DeviceContext> d3d11Context;
	ComPtr<IDXGIOutputDuplication> outputDuplication;
	bool initialized = false;
	ComPtr<ID3D12Device> d3d12Device;
	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ComPtr<ID3D12GraphicsCommandList> commandList;
	// Create a staging texture (CPU-readable) or shared handle for D3D12
	//ComPtr<ID3D12Resource> d3d12Texture;
	ComPtr<ID3D11Texture2D> sharedTextureD3D11;
	ComPtr<ID3D12Resource> sharedTextureD3D12;
	ComPtr<ID3D11Texture2D> acquiredTexture;
	ComPtr<IDXGIResource> desktopResource;
	IDXGIResource* dxgiResource = nullptr;

	ComPtr<IDXGIFactory4> m_factory;
	ComPtr<IDXGIAdapter1> m_adapter;

	VADisplay m_vaDisplay = { };
	VASurfaceID vaSurfacesSrc = { };
	VASurfaceID vaSurfacesDebug = { };

	VASurfaceID m_vaRGBASurface = 0;
	VASurfaceID m_VASurfaceNV12 = 0;
	VASurfaceID m_VASurfaceNV12New = 0;
	HANDLE m_renderSharedHandle = { nullptr };

	int m_width;
	int m_height;

	VideoProcessorNV12Converter converter;
};