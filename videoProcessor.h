#pragma once
#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <iostream>

using Microsoft::WRL::ComPtr;

class VideoProcessorNV12Converter {
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, int width, int height);
    bool Convert(ID3D11Texture2D* inputBgra, ID3D11Texture2D** outputNv12);
    bool ConvertOld(ID3D11Texture2D* inputBgra, ID3D11Texture2D** outputNv12);
    bool Copy(ID3D11Texture2D* inputBgra, ID3D11Texture2D* outputBgra);
private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11VideoDevice> m_videoDevice;
    ComPtr<ID3D11VideoContext> m_videoContext;
    ComPtr<ID3D11VideoProcessorEnumerator> m_enumerator;
    ComPtr<ID3D11VideoProcessor> m_processor;
    int m_width = 0, m_height = 0;
};
