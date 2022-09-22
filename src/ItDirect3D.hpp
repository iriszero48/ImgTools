#pragma once

#include <string>
#include <format>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <wrl.h>

#include "ItException.hpp"
#include "Image.hpp"
#include "CubeLUT.hpp"

using Dx11DevType = ID3D11Device;
using Dx11DevCtxType = ID3D11DeviceContext;

// dx11-imgui
static Microsoft::WRL::ComPtr<Dx11DevType> D3D11Dev = nullptr;
static Microsoft::WRL::ComPtr<Dx11DevCtxType> D3D11DevCtx = nullptr;
static Microsoft::WRL::ComPtr<IDXGISwapChain> D3D11SwapChain = nullptr;
static Microsoft::WRL::ComPtr<ID3D11RenderTargetView>
    D3D11MainRenderTargetView = nullptr;

// dx11-cs
static Microsoft::WRL::ComPtr<Dx11DevType> D3D11CSDev = nullptr;
static Microsoft::WRL::ComPtr<Dx11DevCtxType> D3D11CSDevCtx = nullptr;

struct ImageView
{
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> SRV;
    int Width;
    int Height;

    ImageView() : SRV(nullptr), Width(0), Height(0) {}

    ImageView(const Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &srv,
              const int w, const int h)
        : Width(w), Height(h)
    {
        srv.CopyTo(SRV.GetAddressOf());
    }

    ImageView(const ImageView &iv,
              const Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> &srv)
        : Width(iv.Width), Height(iv.Height)
    {
        srv.CopyTo(SRV.GetAddressOf());
    }

    ImageView(const ImageView &iv) : Width(iv.Width), Height(iv.Height)
    {
        iv.SRV.CopyTo(SRV.GetAddressOf());
    }

    ImageView(ImageView &&iv) noexcept
        : SRV(std::move(iv.SRV)), Width(iv.Width), Height(iv.Height) {}

    ImageView &operator=(const ImageView &iv)
    {
        Width = iv.Width;
        Height = iv.Height;
        iv.SRV.CopyTo(SRV.ReleaseAndGetAddressOf());
        return *this;
    }

    ImageView &operator=(ImageView &&iv) noexcept
    {
        Width = iv.Width;
        Height = iv.Height;
        SRV = std::move(iv.SRV);
        return *this;
    }
};

namespace D3D11
{
	inline std::string GetErrorString(const HRESULT hr)
    {
        switch (hr)
        {
        case D3D11_ERROR_FILE_NOT_FOUND:
            return "The file was not found";
        case D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS:
            return "There are too many unique instances of a particular type of state "
                   "object";
        case D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS:
            return "There are too many unique instances of a particular type of view "
                   "object";
        case D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD:
            return "The first call to ID3D11DeviceContext::Map after either "
                   "ID3D11Device::CreateDeferredContext or "
                   "ID3D11DeviceContext::FinishCommandList per Resource was not "
                   "D3D11_MAP_WRITE_DISCARD";
        case DXGI_ERROR_INVALID_CALL:
            return "The method call is invalid";
        case DXGI_ERROR_WAS_STILL_DRAWING:
            return "The previous blit operation that is transferring information to or "
                   "from this surface is incomplete";
        case E_FAIL:
            return "Attempted to create a device with the debug layer enabled and the "
                   "layer is not installed";
        case E_INVALIDARG:
            return "An invalid parameter was passed to the returning function";
        case E_OUTOFMEMORY:
            return "Direct3D could not allocate sufficient memory to complete the call";
        case E_NOTIMPL:
            return "The method call isn't implemented with the passed parameter "
                   "combination";
        case S_FALSE:
            return "Alternate success value, indicating a successful but nonstandard "
                   "completion (the precise meaning depends on context)";
        case S_OK:
            return "No error occurred";
        default:
            return std::format("Error code {:#x}", static_cast<uint32_t>(hr));
        }
    }

    template <int Size>
    Microsoft::WRL::ComPtr<ID3D11ComputeShader>
    CreateComputeShader(Dx11DevType *dev, const BYTE (&data)[Size])
    {
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader{};
        if (auto hr =
                dev->CreateComputeShader(data, Size, nullptr, shader.GetAddressOf());
            FAILED(hr))
            throw Ex(D3D11Exception, "CreateComputeShader: {}", GetErrorString(hr));
        return shader;
    }

    inline Microsoft::WRL::ComPtr<ID3D11ComputeShader>
    CreateComputeShader(Dx11DevType *dev, const std::string_view &code,
                        const LPCSTR functionName)
    {
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;

        constexpr DWORD shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
        constexpr D3D_SHADER_MACRO defines[] = {{"USE_STRUCTURED_BUFFERS", "1"}, {nullptr, nullptr}};

        const LPCSTR profile =
            (dev->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0) ? "cs_5_0" : "cs_4_0";

        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
        Microsoft::WRL::ComPtr<ID3DBlob> blob = nullptr;

        auto hr = D3DCompile(code.data(), code.size(), nullptr, defines, nullptr,
                             functionName, profile, shaderFlags, NULL, blob.GetAddressOf(), errorBlob.GetAddressOf());
        if (FAILED(hr))
            if (errorBlob)
                throw Ex(
                    D3D11Exception, "CreateComputeShader: {}: {}",
                    GetErrorString(hr),
                    std::string_view(static_cast<char *>(errorBlob->GetBufferPointer())));

        hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                      nullptr, shader.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "CreateComputeShader: {}", GetErrorString(hr));

        return shader;
    }

    inline Microsoft::WRL::ComPtr<ID3D11ComputeShader>
    CreateComputeShader(Dx11DevType *dev, const LPCWSTR srcFile,
                        const LPCSTR functionName)
    {
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;

        constexpr DWORD shaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
        constexpr D3D_SHADER_MACRO defines[] = {{"USE_STRUCTURED_BUFFERS", "1"}, {nullptr, nullptr}};

        const LPCSTR profile =
            (dev->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0) ? "cs_5_0" : "cs_4_0";

        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
        Microsoft::WRL::ComPtr<ID3DBlob> blob = nullptr;

        HRESULT hr = D3DCompileFromFile(
            srcFile, defines, nullptr, functionName, profile, shaderFlags, NULL,
            blob.GetAddressOf(), errorBlob.GetAddressOf());
        if (FAILED(hr))
            if (errorBlob)
                throw Ex(
                    D3D11Exception, "D3DCompileFromFile: {}: {}",
                    GetErrorString(hr),
                    std::string_view(static_cast<char *>(errorBlob->GetBufferPointer())));

        hr = dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                      nullptr, shader.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "CreateComputeShader: {}", GetErrorString(hr));

        return shader;
    }

    inline Microsoft::WRL::ComPtr<ID3D11Buffer>
    CreateStructuredBuffer(Dx11DevType *dev, const UINT elementSize,
                           const UINT count, const void *data)
    {
        Microsoft::WRL::ComPtr<ID3D11Buffer> buf;

        D3D11_BUFFER_DESC desc;
        ZeroMemory(&desc, sizeof(D3D11_BUFFER_DESC));
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        desc.ByteWidth = elementSize * count;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = elementSize;

        HRESULT hr;
        if (data)
        {
            D3D11_SUBRESOURCE_DATA initData{};
            initData.pSysMem = data;
            hr = dev->CreateBuffer(&desc, &initData, buf.GetAddressOf());
        }
        else
            hr = dev->CreateBuffer(&desc, nullptr, buf.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "CreateBuffer: {}", GetErrorString(hr));

        return buf;
    }

    inline Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
    CreateBufferSRV(Dx11DevType *dev, ID3D11Buffer *buffer)
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;

        D3D11_BUFFER_DESC descBuf;
        ZeroMemory(&descBuf, sizeof(descBuf));
        buffer->GetDesc(&descBuf);

        D3D11_SHADER_RESOURCE_VIEW_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        desc.BufferEx.FirstElement = 0;

        if (descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
        {
            desc.Format = DXGI_FORMAT_R32_TYPELESS;
            desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            desc.BufferEx.NumElements = descBuf.ByteWidth / 4;
        }
        else if (descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
        {
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.BufferEx.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;
        }
        else
        {
            throw Ex(D3D11Exception, "E_INVALIDARG");
        }

        if (const auto hr =
                dev->CreateShaderResourceView(buffer, &desc, srv.GetAddressOf());
            FAILED(hr))
            throw Ex(D3D11Exception, "CreateShaderResourceView: {}",
                     GetErrorString(hr));

        return srv;
    }

    inline Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
    CreateBufferUAV(Dx11DevType *dev, ID3D11Buffer *buffer)
    {
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;

        D3D11_BUFFER_DESC descBuf;
        ZeroMemory(&descBuf, sizeof(descBuf));
        buffer->GetDesc(&descBuf);

        D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = 0;

        if (descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
        {
            desc.Format = DXGI_FORMAT_R32_TYPELESS;
            desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            desc.Buffer.NumElements = descBuf.ByteWidth / 4;
        }
        else if (descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
        {
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.Buffer.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;
        }
        else
        {
            throw Ex(D3D11Exception, "CreateBufferSRV: E_INVALIDARG");
        }

        if (const auto hr =
                dev->CreateUnorderedAccessView(buffer, &desc, uav.GetAddressOf());
            FAILED(hr))
            throw Ex(D3D11Exception, "CreateBufferUAV: CreateUnorderedAccessView: {}",
                     GetErrorString(hr));

        return uav;
    }

    inline Microsoft::WRL::ComPtr<ID3D11Texture2D>
    CreateTexture2dUavBuf(Dx11DevType *dev, const UINT width, const UINT height)
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> buf;

        D3D11_TEXTURE2D_DESC texDesc;
        ZeroMemory(&texDesc, sizeof(D3D11_TEXTURE2D_DESC));
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.CPUAccessFlags = 0;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        texDesc.MiscFlags = 0;

        if (const auto hr =
                dev->CreateTexture2D(&texDesc, nullptr, buf.GetAddressOf());
            FAILED(hr))
            throw Ex(D3D11Exception, "CreateTexture2D: {}", GetErrorString(hr));

        return buf;
    }

    inline void RunComputeShader(Dx11DevCtxType *devCtx, ID3D11ComputeShader *shader,
                                 ID3D11ShaderResourceView **srvs, const UINT srvN,
                                 ID3D11UnorderedAccessView **uavs, const UINT uavN,
                                 ID3D11SamplerState **sss, const UINT ssN,
                                 ID3D11Buffer **cbs, const UINT cbN, const UINT x,
                                 const UINT y, const UINT z)
    {
        devCtx->CSSetShader(shader, nullptr, 0);
        devCtx->CSSetShaderResources(0, srvN, srvs);
        devCtx->CSSetUnorderedAccessViews(0, uavN, uavs, nullptr);
        devCtx->CSSetSamplers(0, ssN, sss);
        devCtx->CSSetConstantBuffers(0, cbN, cbs);

        devCtx->Dispatch(x, y, z);

        devCtx->CSSetShader(nullptr, nullptr, 0);
        ID3D11UnorderedAccessView *nullUav[1] = {nullptr};
        devCtx->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);
        ID3D11ShaderResourceView *nullSrv[1] = {nullptr};
        devCtx->CSSetShaderResources(0, 1, nullSrv);
        ID3D11Buffer *nullCb[1] = {nullptr};
        devCtx->CSSetConstantBuffers(0, 1, nullCb);
        ID3D11SamplerState *nullSs[1] = {nullptr};
        devCtx->CSSetSamplers(0, 1, nullSs);
    }

    template <std::size_t SrvN, std::size_t UavN>
    void RunComputeShader(Dx11DevCtxType *devCtx, ID3D11ComputeShader *shader,
                          ID3D11ShaderResourceView *(&srvs)[SrvN],
                          ID3D11UnorderedAccessView *(&uavs)[UavN], const UINT x,
                          const UINT y, const UINT z)
    {
        RunComputeShader(devCtx, shader, srvs, SrvN, uavs, UavN, nullptr, 0, nullptr,
                         0, x, y, z);
    }

    template <std::size_t SrvN, std::size_t UavN, std::size_t SsN>
    void RunComputeShader(Dx11DevCtxType *devCtx, ID3D11ComputeShader *shader,
                          ID3D11ShaderResourceView *(&srvs)[SrvN],
                          ID3D11UnorderedAccessView *(&uavs)[UavN],
                          ID3D11SamplerState *(&sss)[SsN], const UINT x,
                          const UINT y, const UINT z)
    {
        RunComputeShader(devCtx, shader, srvs, SrvN, uavs, UavN, sss, SsN, nullptr, 0,
                         x, y, z);
    }

    template <std::size_t SrvN, std::size_t UavN, std::size_t SsN, std::size_t CbN>
    void RunComputeShader(Dx11DevCtxType *devCtx, ID3D11ComputeShader *shader,
                          ID3D11ShaderResourceView *(&srvs)[SrvN],
                          ID3D11UnorderedAccessView *(&uavs)[UavN],
                          ID3D11SamplerState *(&sss)[SsN], ID3D11Buffer *cbs[CbN],
                          const UINT x, const UINT y, const UINT z)
    {
        RunComputeShader(devCtx, shader, srvs, SrvN, uavs, UavN, sss, SsN, cbs, CbN,
                         x, y, z);
    }

    inline Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>
    CreateTexture2dUav(Dx11DevType *dev, ID3D11Texture2D *tex)
    {
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> buf;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
        ZeroMemory(&uavDesc, sizeof(D3D11_UNORDERED_ACCESS_VIEW_DESC));
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;

        if (const auto hr =
                dev->CreateUnorderedAccessView(tex, &uavDesc, buf.GetAddressOf());
            FAILED(hr))
            throw Ex(D3D11Exception, "CreateUnorderedAccessView: {}",
                     GetErrorString(hr));

        return buf;
    }

    inline Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
    CreateSrvFromTex(Dx11DevType *dev, ID3D11Texture2D *tex)
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> buf;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;

        if (const auto hr =
                dev->CreateShaderResourceView(tex, &srvDesc, buf.GetAddressOf());
            FAILED(hr))
            throw Ex(D3D11Exception, "CreateShaderResourceView: {}",
                     GetErrorString(hr));

        return buf;
    }

    inline Microsoft::WRL::ComPtr<ID3D11SamplerState> CreateSampler(Dx11DevType *dev)
    {
        Microsoft::WRL::ComPtr<ID3D11SamplerState> ss;

        D3D11_SAMPLER_DESC desc{};
        ZeroMemory(&desc, sizeof(D3D11_SAMPLER_DESC));
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

        if (const auto hr = dev->CreateSamplerState(&desc, ss.GetAddressOf());
            FAILED(hr))
            throw Ex(D3D11Exception, "CreateSamplerState: {}", GetErrorString(hr));

        return ss;
    }

    static void CreateRenderTarget()
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;

        auto hr =
            D3D11SwapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
        if (FAILED(hr))
            throw Ex(D3D11Exception, "GetBuffer: {}", GetErrorString(hr));

        hr = D3D11Dev->CreateRenderTargetView(
            backBuffer.Get(), nullptr, D3D11MainRenderTargetView.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "CreateRenderTargetView: {}", GetErrorString(hr));
    }

    static void CreateDeviceD3D(const HWND wnd)
    {
        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = wnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(DEBUG) || defined(_DEBUG)
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevel;
        constexpr D3D_FEATURE_LEVEL featureLevelArray[2] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_0,
        };
        auto hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            D3D11SwapChain.GetAddressOf(), D3D11Dev.GetAddressOf(), &featureLevel,
            D3D11DevCtx.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "D3D11CreateDeviceAndSwapChain: {}",
                     GetErrorString(hr));

        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               createDeviceFlags, featureLevelArray, 2,
                               D3D11_SDK_VERSION, D3D11CSDev.GetAddressOf(),
                               &featureLevel, D3D11CSDevCtx.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "D3D11CreateDevice: {}", GetErrorString(hr));

        CreateRenderTarget();
    }

    static void CleanupRenderTarget() { D3D11MainRenderTargetView.Reset(); }

    static void CleanupDeviceD3D()
    {
        CleanupRenderTarget();
        D3D11SwapChain.Reset();
        D3D11DevCtx.Reset();
        D3D11Dev.Reset();

        D3D11CSDevCtx.Reset();
        D3D11CSDev.Reset();
    }

    constexpr int GetThreadGroupNum(const int size)
    {
        constexpr auto groupSize = 32;
        return size / groupSize + (size % groupSize ? 1 : 0);
    }

    inline Image::ImageFile CreateOutTexture(Dx11DevType *dev, Dx11DevCtxType *devCtx,
                                             const ImageView &texture)
    {
        Microsoft::WRL::ComPtr<ID3D11Resource> res;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        texture.SRV->GetResource(res.GetAddressOf());
        auto hr = res->QueryInterface(tex.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "QueryInterface: {}", GetErrorString(hr));

        D3D11_TEXTURE2D_DESC texDesc;
        tex->GetDesc(&texDesc);
        texDesc.Usage = D3D11_USAGE_STAGING;
        texDesc.BindFlags = 0;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        texDesc.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> des;
        hr = dev->CreateTexture2D(&texDesc, nullptr, des.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "CreateTexture2D: {}", GetErrorString(hr));

        devCtx->CopyResource(des.Get(), tex.Get());

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = devCtx->Map(des.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr))
            throw Ex(D3D11Exception, "Map: {}", GetErrorString(hr));

        constexpr auto comp = 4;
        const int w = mapped.RowPitch / comp;
        const int h = mapped.DepthPitch / mapped.RowPitch;

        Image::ImageFile img(texture.Width, texture.Height);
        if (w == img.Width() && h == img.Height())
        {
            std::copy_n(static_cast<uint8_t *>(mapped.pData), img.Size(), img.Data());
        }
        else
        {
            for (auto i = 0; i < texture.Height; ++i)
            {
                std::copy_n(static_cast<uint8_t *>(mapped.pData) + static_cast<uint32_t>(i) * 4 * w,
                            img.Width() * 4, img.Data() + static_cast<int64_t>(i) * img.Width() * 4);
            }
        }

        devCtx->Unmap(des.Get(), 0);

        return img;
    }

    inline ImageView LoadTextureFromFile(Dx11DevType *dev, const Image::ImageFile &img)
    {
        if (img.Empty())
            throw Ex(D3D11Exception, "img.Empty()");

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> outSrv;

        D3D11_TEXTURE2D_DESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.Width = img.Width();
        desc.Height = img.Height();
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        D3D11_SUBRESOURCE_DATA subResource{};
        subResource.pSysMem = img.Data();
        subResource.SysMemPitch = desc.Width * 4;
        subResource.SysMemSlicePitch = 0;
        auto hr = dev->CreateTexture2D(&desc, &subResource, texture.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "CreateTexture2D: {}", GetErrorString(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        hr = dev->CreateShaderResourceView(texture.Get(), &srvDesc,
                                           outSrv.GetAddressOf());
        if (FAILED(hr))
            throw Ex(D3D11Exception, "CreateShaderResourceView: {}",
                     GetErrorString(hr));

        return {outSrv, img.Width(), img.Height()};
    }

    inline Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
    CreateTexture3d(Dx11DevType *dev, const Lut::CubeLut &cube)
    {
        auto &tab = cube.GetTable();
        ID3D11ShaderResourceView *outSrv = nullptr;
        const auto *data = reinterpret_cast<const float *>(
            std::visit([](auto &x)
                       { return x.GetRawData().data(); },
                       tab));
        D3D11_TEXTURE3D_DESC desc;
        ZeroMemory(&desc, sizeof(D3D11_TEXTURE3D_DESC));
        desc.Width = static_cast<UINT>(cube.Length());
        desc.Height = static_cast<UINT>(cube.Length());
        desc.Depth = static_cast<UINT>(cube.Length());
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture3D> texture = nullptr;
        D3D11_SUBRESOURCE_DATA subResource;
        ZeroMemory(&subResource, sizeof(D3D11_SUBRESOURCE_DATA));
        subResource.pSysMem = data;
        subResource.SysMemPitch = desc.Width * sizeof(float) * 3;
        subResource.SysMemSlicePitch = static_cast<unsigned long long>(desc.Height) *
                                       desc.Width * sizeof(float) * 3;
        auto hr = dev->CreateTexture3D(&desc, &subResource, texture.GetAddressOf());
        if (FAILED(hr))
            throw Ex(ToolException, "CreateTexture3D: {}", GetErrorString(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
        srvDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = desc.MipLevels;
        srvDesc.Texture3D.MostDetailedMip = 0;
        hr = dev->CreateShaderResourceView(texture.Get(), &srvDesc, &outSrv);
        if (FAILED(hr))
            throw Ex(ToolException, "CreateShaderResourceView: {}", GetErrorString(hr));

        return outSrv;
    }
}
