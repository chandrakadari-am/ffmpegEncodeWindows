#pragma once
#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <iostream>
#include "videoProcessor.h"

using Microsoft::WRL::ComPtr;

bool VideoProcessorNV12Converter::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, int width, int height) {
    m_device = device;
    m_context = context;
    m_width = width;
    m_height = height;

    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&m_videoDevice));
    hr |= context->QueryInterface(IID_PPV_ARGS(&m_videoContext));
    if (FAILED(hr)) {
        std::cerr << "Failed to get video device/context\n";
        return false;
    }

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
    contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    contentDesc.InputWidth = width;
    contentDesc.InputHeight = height;
    contentDesc.OutputWidth = width;
    contentDesc.OutputHeight = height;
    contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

    hr = m_videoDevice->CreateVideoProcessorEnumerator(&contentDesc, &m_enumerator);
    if (FAILED(hr)) {
        std::cerr << "Failed to create video processor enumerator\n";
        return false;
    }

    hr = m_videoDevice->CreateVideoProcessor(m_enumerator.Get(), 0, &m_processor);
    if (FAILED(hr)) {
        std::cerr << "Failed to create video processor\n";
        return false;
    }

    return true;
}

bool VideoProcessorNV12Converter::Convert(ID3D11Texture2D* inputBgra, ID3D11Texture2D** outputNv12) {
    if (!inputBgra || !outputNv12) return false;

    HRESULT hr;

    // Create NV12 output texture
    D3D11_TEXTURE2D_DESC nv12Desc = {};
    nv12Desc.Width = m_width;
    nv12Desc.Height = m_height;
    nv12Desc.MipLevels = 1;
    nv12Desc.ArraySize = 1;
    nv12Desc.Format = DXGI_FORMAT_NV12;
    nv12Desc.SampleDesc.Count = 1;
    nv12Desc.Usage = D3D11_USAGE_DEFAULT;
    nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> nv12Tex;
    hr = m_device->CreateTexture2D(&nv12Desc, nullptr, &nv12Tex);
    if (FAILED(hr)) {
        std::cerr << "Failed to create NV12 output texture\n";
        return false;
    }

    // Input view
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc = {};
    inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputDesc.Texture2D.MipSlice = 0;
    inputDesc.Texture2D.ArraySlice = 0;

    hr = m_videoDevice->CreateVideoProcessorInputView(inputBgra, m_enumerator.Get(), &inputDesc, &inputView);
    if (FAILED(hr)) {
        std::cerr << "Failed to create input view\n";
        return false;
    }

    // Output view
    ComPtr<ID3D11VideoProcessorOutputView> outputView;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc = {};
    outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputDesc.Texture2D.MipSlice = 0;

    hr = m_videoDevice->CreateVideoProcessorOutputView(nv12Tex.Get(), m_enumerator.Get(), &outputDesc, &outputView);
    if (FAILED(hr)) {
        std::cerr << "Failed to create output view\n";
        return false;
    }

    // Stream
    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();

    hr = m_videoContext->VideoProcessorBlt(m_processor.Get(), outputView.Get(), 0, 1, &stream);
    if (FAILED(hr)) {
        std::cerr << "VideoProcessorBlt failed\n";
        return false;
    }

    // 🔽 Wait for GPU to finish processing
    ComPtr<ID3D11Query> query;
    D3D11_QUERY_DESC queryDesc = {};
    queryDesc.Query = D3D11_QUERY_EVENT;
    hr = m_device->CreateQuery(&queryDesc, &query);
    if (FAILED(hr)) {
        std::cerr << "Failed to create GPU sync query\n";
        return false;
    }

    m_context->End(query.Get());

    // Busy-wait until GPU signals it's done
    while (S_OK != m_context->GetData(query.Get(), nullptr, 0, 0)) {
        // Optionally sleep to avoid CPU spin
        Sleep(2);
    }

    *outputNv12 = nv12Tex.Detach();
    return true;
}

bool VideoProcessorNV12Converter::Copy(ID3D11Texture2D* inputBgra, ID3D11Texture2D* outputBgra) {
    if (!inputBgra || !outputBgra) return false;

    // Just copy the entire resource from input to output
    m_context->CopyResource(outputBgra, inputBgra);

    // Optionally flush the context to ensure copy completion
    m_context->Flush();

    return true;
}

bool VideoProcessorNV12Converter::ConvertOld(ID3D11Texture2D* inputBgra, ID3D11Texture2D** outputNv12) {
    if (!inputBgra || !outputNv12) return false;

    HRESULT hr;

    // Create NV12 output texture
    D3D11_TEXTURE2D_DESC nv12Desc = {};
    nv12Desc.Width = m_width;
    nv12Desc.Height = m_height;
    nv12Desc.MipLevels = 1;
    nv12Desc.ArraySize = 1;
    nv12Desc.Format = DXGI_FORMAT_NV12;
    nv12Desc.SampleDesc.Count = 1;
    nv12Desc.Usage = D3D11_USAGE_DEFAULT;
    nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> nv12Tex;
    hr = m_device->CreateTexture2D(&nv12Desc, nullptr, &nv12Tex);
    if (FAILED(hr)) {
        std::cerr << "Failed to create NV12 output texture\n";
        return false;
    }

    // Input view
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc = {};
    inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputDesc.Texture2D.MipSlice = 0;
    inputDesc.Texture2D.ArraySlice = 0;

    hr = m_videoDevice->CreateVideoProcessorInputView(inputBgra, m_enumerator.Get(), &inputDesc, &inputView);
    if (FAILED(hr)) {
        std::cerr << "Failed to create input view\n";
        return false;
    }

    // Output view
    ComPtr<ID3D11VideoProcessorOutputView> outputView;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc = {};
    outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputDesc.Texture2D.MipSlice = 0;

    hr = m_videoDevice->CreateVideoProcessorOutputView(nv12Tex.Get(), m_enumerator.Get(), &outputDesc, &outputView);
    if (FAILED(hr)) {
        std::cerr << "Failed to create output view\n";
        return false;
    }

    // Stream
    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();

    hr = m_videoContext->VideoProcessorBlt(m_processor.Get(), outputView.Get(), 0, 1, &stream);
    if (FAILED(hr)) {
        std::cerr << "VideoProcessorBlt failed\n";
        return false;
    }

    *outputNv12 = nv12Tex.Detach();
    return true;
}
