#include "startup_image.h"

#include <exception>
#include <limits>
#include <sstream>
#include <vector>

#include <Windows.h>
#include <wincodec.h>

namespace fearvr {
namespace {

using Microsoft::WRL::ComPtr;

std::string HresultText(const char* operation, HRESULT result) {
    std::ostringstream detail;
    detail << operation << " failed with HRESULT=0x"
           << std::hex << static_cast<std::uint32_t>(result);
    return detail.str();
}

class ComApartment {
public:
    ComApartment() noexcept
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool Available() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

    [[nodiscard]] HRESULT Result() const noexcept {
        return result_;
    }

private:
    HRESULT result_;
};

} // namespace

bool StartupImage::Load(
    ID3D11Device* device,
    const std::filesystem::path& path,
    std::string& error) noexcept {
    texture_.Reset();
    view_.Reset();
    width_ = 0;
    height_ = 0;
    error.clear();

    try {
        if (device == nullptr || path.empty()) {
            error = "startup image device or path is empty";
            return false;
        }

        const ComApartment apartment;
        if (!apartment.Available()) {
            error = HresultText("CoInitializeEx", apartment.Result());
            return false;
        }

        ComPtr<IWICImagingFactory> factory;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
        if (FAILED(result)) {
            error = HresultText("CoCreateInstance(WIC)", result);
            return false;
        }

        ComPtr<IWICBitmapDecoder> decoder;
        result = factory->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            decoder.ReleaseAndGetAddressOf());
        if (FAILED(result)) {
            error = HresultText("CreateDecoderFromFilename", result);
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        result = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
        if (FAILED(result)) {
            error = HresultText("IWICBitmapDecoder::GetFrame", result);
            return false;
        }

        UINT width = 0;
        UINT height = 0;
        result = frame->GetSize(&width, &height);
        constexpr UINT kMaximumDimension = 8192;
        if (FAILED(result) || width == 0 || height == 0 ||
            width > kMaximumDimension || height > kMaximumDimension) {
            error = FAILED(result)
                ? HresultText("IWICBitmapFrameDecode::GetSize", result)
                : "startup image dimensions are invalid or exceed 8192";
            return false;
        }

        ComPtr<IWICFormatConverter> converter;
        result = factory->CreateFormatConverter(
            converter.ReleaseAndGetAddressOf());
        if (FAILED(result)) {
            error = HresultText("IWICImagingFactory::CreateFormatConverter", result);
            return false;
        }
        result = converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(result)) {
            error = HresultText("IWICFormatConverter::Initialize", result);
            return false;
        }

        constexpr std::size_t kBytesPerPixel = 4;
        const std::size_t rowPitch =
            static_cast<std::size_t>(width) * kBytesPerPixel;
        if (rowPitch > (std::numeric_limits<UINT>::max)() ||
            static_cast<std::size_t>(height) >
                (std::numeric_limits<std::size_t>::max)() / rowPitch) {
            error = "startup image byte count overflow";
            return false;
        }
        const std::size_t byteCount =
            rowPitch * static_cast<std::size_t>(height);
        if (byteCount > (std::numeric_limits<UINT>::max)()) {
            error = "startup image exceeds WIC copy capacity";
            return false;
        }
        std::vector<std::uint8_t> pixels(byteCount);
        result = converter->CopyPixels(
            nullptr,
            static_cast<UINT>(rowPitch),
            static_cast<UINT>(byteCount),
            pixels.data());
        if (FAILED(result)) {
            error = HresultText("IWICBitmapSource::CopyPixels", result);
            return false;
        }

        D3D11_TEXTURE2D_DESC textureDescription{};
        textureDescription.Width = width;
        textureDescription.Height = height;
        textureDescription.MipLevels = 1;
        textureDescription.ArraySize = 1;
        textureDescription.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        textureDescription.SampleDesc.Count = 1;
        textureDescription.Usage = D3D11_USAGE_IMMUTABLE;
        textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = pixels.data();
        initialData.SysMemPitch = static_cast<UINT>(rowPitch);
        result = device->CreateTexture2D(
            &textureDescription,
            &initialData,
            texture_.ReleaseAndGetAddressOf());
        if (FAILED(result)) {
            error = HresultText("CreateTexture2D(startup image)", result);
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
        viewDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDescription.Texture2D.MostDetailedMip = 0;
        viewDescription.Texture2D.MipLevels = 1;
        result = device->CreateShaderResourceView(
            texture_.Get(),
            &viewDescription,
            view_.ReleaseAndGetAddressOf());
        if (FAILED(result)) {
            error = HresultText("CreateShaderResourceView(startup image)", result);
            texture_.Reset();
            return false;
        }

        width_ = width;
        height_ = height;
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "unknown startup image load failure";
    }

    texture_.Reset();
    view_.Reset();
    width_ = 0;
    height_ = 0;
    return false;
}

bool StartupImage::Available() const noexcept {
    return view_.Get() != nullptr;
}

ID3D11ShaderResourceView* StartupImage::View() const noexcept {
    return view_.Get();
}

std::uint32_t StartupImage::Width() const noexcept {
    return width_;
}

std::uint32_t StartupImage::Height() const noexcept {
    return height_;
}

} // namespace fearvr
