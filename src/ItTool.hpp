#pragma once

#include <stb_image_resize.h>

#include <waifu2x-ncnn-vulkan/src/waifu2x.h>
#include <realsr-ncnn-vulkan/src/realsr.h>

#include "ImageTools.hpp"
#include "ItUtility.hpp"

#include "noise0_scale2_0x_model_param.h"
#include "noise1_scale2_0x_model_param.h"
#include "noise2_scale2_0x_model_param.h"
#include "noise3_scale2_0x_model_param.h"

#include "realsr_df2k_x4_param.h"
#include "realsr_df2k_jpeg_x4_param.h"

#include "resource.h"

template <typename... Tools>
class ToolCombine : public ImageTools::ITool<ToolCombine<Tools...>>
{
public:
    using ToolType = std::variant<Tools...>;

    ToolType Tool;

    explicit ToolCombine(ToolType tool) : Tool(std::move(tool)) {}

    void ImgRef(const Image::ImageFile &img)
    {
        std::visit([&](auto &t)
                   { t.ImgRef(img); },
                   Tool);
    }

    ImageTools::ImageSize GetOutputSize() const
    {
        return std::visit([](auto &t)
                          { return t.GetOutputSize(); },
                          Tool);
    }

    Image::ColorRgba<uint8_t> operator()(const int64_t row, const int64_t col)
    {
        return std::visit([&](auto &t)
                          { return t(row, col); },
                          Tool);
    }
};

class Waifu2xNcnn : public ImageTools::ITool<Waifu2xNcnn>
{
    int Noise = 3;
    int TileSize = -1;

    ImageTools::ImageSize OutputSize{};
    ncnn::Mat OutputBuffer{};
    Image::ImageFile Output{};

public:
    Waifu2xNcnn(const int noise, const int tileSize)
        : Noise(noise), TileSize(tileSize) {}

    void ImgRef(const Image::ImageFile &img)
    {
        _ImgRef = &img;

        Waifu2x waifu2x(ncnn::get_default_gpu_index(), false, 1);
        waifu2x.scale = 2;
        waifu2x.noise = Noise;
        waifu2x.prepadding = 18;
        waifu2x.tilesize = TileSize;
        if (TileSize == -1)
        {
            if (const auto heap = ncnn::get_gpu_device(ncnn::get_default_gpu_index())
                                      ->get_heap_budget();
                heap > 2600)
                waifu2x.tilesize = 400;
            else if (heap > 740)
                waifu2x.tilesize = 200;
            else if (heap > 250)
                waifu2x.tilesize = 100;
            else
                waifu2x.tilesize = 32;
        }

        static const RcResource CunetNoise0(MAKEINTRESOURCE(CUNET_NOISE0), RT_RCDATA, "CUNET_NOISE0");
        static const RcResource CunetNoise1(MAKEINTRESOURCE(CUNET_NOISE1), RT_RCDATA, "CUNET_NOISE1");
        static const RcResource CunetNoise2(MAKEINTRESOURCE(CUNET_NOISE2), RT_RCDATA, "CUNET_NOISE2");
        static const RcResource CunetNoise3(MAKEINTRESOURCE(CUNET_NOISE3), RT_RCDATA, "CUNET_NOISE3");

        switch (Noise)
        {
        case 0:
            waifu2x.load(reinterpret_cast<const char *>(noise0_scale2_0x_model_param),
                         CunetNoise0.Get().data());
            break;
        case 1:
            waifu2x.load(reinterpret_cast<const char *>(noise1_scale2_0x_model_param),
                         CunetNoise1.Get().data());
            break;
        case 2:
            waifu2x.load(reinterpret_cast<const char *>(noise2_scale2_0x_model_param),
                         CunetNoise2.Get().data());
            break;
        case 3:
            waifu2x.load(reinterpret_cast<const char *>(noise3_scale2_0x_model_param),
                         CunetNoise3.Get().data());
            break;
        default:
            assert((false, "invalid noise"));
        }

        const ncnn::Mat input(_ImgRef->Width(), _ImgRef->Height(),
                              (void *)_ImgRef->Data(), 4, 4);

        OutputSize = {_ImgRef->Width() * 2, _ImgRef->Height() * 2};
        OutputBuffer = {OutputSize.Width, OutputSize.Height, 4, 4};

        waifu2x.process(input, OutputBuffer);

        Output = {static_cast<uint8_t *>(OutputBuffer.data), OutputSize.Width,
                  OutputSize.Height, false};
    }

    [[nodiscard]] ImageTools::ImageSize GetOutputSize() const { return OutputSize; }

    Image::ColorRgba<uint8_t> operator()(const int64_t row, const int64_t col) const
    {
        return Output.At<uint8_t>(row, col);
    }

    Image::ImageFile &GetOutputImage() { return Output; }
};

MakeEnum(LinearDodgeType, Color, Image);

class LinearDodge : public ImageTools::ITool<LinearDodge>
{
public:
    using ColorProc = ImageTools::LinearDodgeColor;
    using ImageProc = ImageTools::LinearDodgeImage;
    using ProcType = std::variant<ColorProc, ImageProc>;

private:
    ProcType proc;

public:
    LinearDodge(const float color[4]) : proc(ColorProc(Image::FloatToUint8({color[0], color[1], color[2], color[3]}))) {}
    LinearDodge(const std::filesystem::path &param) : proc(ImageProc(param)) {}

    void ImgRef(const Image::ImageFile &img)
    {
        std::visit([&](auto &p)
                   { p.ImgRef(img); },
                   proc);
    }

    [[nodiscard]] ImageTools::ImageSize GetOutputSize() const
    {
        return std::visit([](auto &p)
                          { return p.GetOutputSize(); },
                          proc);
    }

    Image::ColorRgba<uint8_t> operator()(const int64_t row, const int64_t col)
    {
        return std::visit([&](auto &p)
                          { return p(row, col); },
                          proc);
    }
};

MakeEnum(RealsrNcnnModel, DF2K_X4, DF2K_JPEG_X4);

class RealsrNcnn : public ImageTools::ITool<RealsrNcnn>
{
    ImageTools::ImageSize OutputSize{};
    ncnn::Mat OutputBuffer{};
    Image::ImageFile Output{};

    RealsrNcnnModel Model = RealsrNcnnModel::DF2K_JPEG_X4;
    bool UseTta = false;

public:
    RealsrNcnn(const RealsrNcnnModel model, const bool useTta) : Model(model), UseTta(useTta) {}

    void ImgRef(const Image::ImageFile &img)
    {
        _ImgRef = &img;

        const auto gpu = ncnn::get_default_gpu_index();
        RealSR realsr(gpu, UseTta);

        if (const auto heap = ncnn::get_gpu_device(gpu)->get_heap_budget(); heap > 1900)
            realsr.tilesize = 200;
        else if (heap > 550)
            realsr.tilesize = 100;
        else if (heap > 190)
            realsr.tilesize = 64;
        else
            realsr.tilesize = 32;

        realsr.scale = 4;
        realsr.prepadding = 10;

        static const RcResource Df2k(MAKEINTRESOURCE(DF2K), RT_RCDATA, "DF2K");
        static const RcResource Df2k_Jpeg(MAKEINTRESOURCE(DF2K_JPEG), RT_RCDATA, "DF2K_JPEG");

        if (Model == RealsrNcnnModel::DF2K_X4)
        {
            realsr.load(reinterpret_cast<const char *>(realsr_df2k_x4_param), Df2k.Get().data());
        }
        else if (Model == RealsrNcnnModel::DF2K_JPEG_X4)
        {
            realsr.load(reinterpret_cast<const char *>(realsr_df2k_jpeg_x4_param), Df2k_Jpeg.Get().data());
        }
        else
        {
            assert((false, "invalid realsr model"));
        }

        const ncnn::Mat input(_ImgRef->Width(), _ImgRef->Height(),
                              (void *)_ImgRef->Data(), 4, 4);

        OutputSize = {_ImgRef->Width() * 4, _ImgRef->Height() * 4};
        OutputBuffer = {OutputSize.Width, OutputSize.Height, 4, 4};

        realsr.process(input, OutputBuffer);

        Output = {static_cast<uint8_t *>(OutputBuffer.data), OutputSize.Width,
                  OutputSize.Height, false};
    }

    [[nodiscard]] const ImageTools::ImageSize &GetOutputSize() const { return OutputSize; }

    Image::ColorRgba<uint8_t> operator()(const int64_t row, const int64_t col) const
    {
        return Output.At<uint8_t>(row, col);
    }

    Image::ImageFile &GetOutputImage() { return Output; }
};

class StbResize : public ImageTools::ITool<StbResize>
{
    int scale = 1;
    ImageTools::ImageSize OutputSize{};
    Image::ImageFile OutputImage{};

public:
    explicit StbResize(const int scale) : scale(scale) {}

    void ImgRef(const Image::ImageFile &img)
    {
        _ImgRef = &img;

        OutputSize = {_ImgRef->Width() * scale, _ImgRef->Height() * scale};
        OutputImage = {OutputSize.Width, OutputSize.Height};

        stbir_resize_uint8(_ImgRef->Data(), _ImgRef->Width(), _ImgRef->Height(),
                           0, OutputImage.Data(), OutputSize.Width, OutputSize.Height,
                           0, 4);
    }

    [[nodiscard]] const ImageTools::ImageSize &GetOutputSize() const { return OutputSize; }

    Image::ColorRgba<uint8_t> operator()(const int64_t row, const int64_t col) const
    {
        return OutputImage.At<uint8_t>(row, col);
    }

    Image::ImageFile &GetOutputImage() { return OutputImage; }
};
