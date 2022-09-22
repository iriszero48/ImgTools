#pragma once

// #define UseOpenCV
#define UseStb

#include <climits>
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <sstream>

#ifdef UseOpenCV
#include <opencv2/opencv.hpp>
#endif

extern "C"
{
#include <stb_image.h>
#include <stb_image_write.h>
}

namespace Image
{
#ifdef UseOpenCV
    namespace __Detail
    {
        template <typename T>
        constexpr auto ColorFloatTypeToCvType()
        {
            if constexpr (sizeof(T) * CHAR_BIT == 16)
                return CV_16FC4;
            else if constexpr (sizeof(T) * CHAR_BIT == 32)
                return CV_32FC4;
            else if constexpr (sizeof(T) * CHAR_BIT == 64)
                return CV_64FC4;
            else
                static_assert(false, "unknow float bit");
        }

        template <typename T>
        struct ColorTypeToCvType
        {
        };
        template <>
        struct ColorTypeToCvType<uint8_t>
        {
            constexpr auto operator()() { return CV_8UC4; }
        };
        template <>
        struct ColorTypeToCvType<int8_t>
        {
            constexpr auto operator()() { return CV_8SC4; }
        };
        template <>
        struct ColorTypeToCvType<uint16_t>
        {
            constexpr auto operator()() { return CV_16UC4; }
        };
        template <>
        struct ColorTypeToCvType<int16_t>
        {
            constexpr auto operator()() { return CV_16SC4; }
        };
        template <>
        struct ColorTypeToCvType<int32_t>
        {
            constexpr auto operator()() { return CV_32SC4; }
        };
        template <>
        struct ColorTypeToCvType<float>
        {
            constexpr auto operator()() { return ColorFloatTypeToCvType<float>(); }
        };
        template <>
        struct ColorTypeToCvType<double>
        {
            constexpr auto operator()() { return ColorFloatTypeToCvType<double>(); }
        };
        template <>
        struct ColorTypeToCvType<long double>
        {
            constexpr auto operator()() { return ColorFloatTypeToCvType<long double>(); }
        };
    }

    class ImageFile
    {
    private:
        cv::Mat data{};

    public:
        ImageFile() {}

        ImageFile(const std::filesystem::path &file)
        {
            LoadFile(file);
        }

        template <typename T>
        static ImageFile Create(const int64_t width, const int64_t height)
        {
            ImageFile img{};
            img.Data() = cv::Mat((int)height, (int)width, __Detail::ColorTypeToCvType<T>{}());

            return img;
        }

        void LoadFile(std::vector<char> &data)
        {
            data = cv::imdecode(data, cv::IMREAD_UNCHANGED);
        }

        void LoadFile(const std::filesystem::path &file)
        {
            try
            {
                data = cv::imread(file.string(), cv::IMREAD_UNCHANGED);
            }
            catch (...)
            {
                std::vector<char> fileData{};

                std::ifstream fs(file, std::ios::in | std::ios::binary);
                if (!fs)
                    throw std::runtime_error("open image file failed");

                fs.read(fileData.data(), std::filesystem::file_size(file));
                fs.close();

                LoadFile(fileData);
            }
        }

        void Save(const std::filesystem::path &path)
        {
            try
            {
                data = cv::imwrite(path.string(), data);
            }
            catch (...)
            {
                std::vector<uint8_t> fileData{};
                cv::imencode(path.extension().string(), data, fileData);

                std::ofstream fs(path, std::ios::out | std::ios::binary);
                if (!fs)
                    throw std::runtime_error("open image file failed");

                fs.write((const char *)fileData.data(), fileData.size());
                fs.close();
            }
        }

        cv::Mat &Data() { return data; }
        int Width() const { return data.cols; }
        int Height() const { return data.rows; }

        template <typename T>
        ColorRgba<T> At(const int64_t row, const int64_t col) const
        {
            const auto &bgra = data.at<cv::Vec4b>((int)row, (int)col);
            return ColorRgba<T>(bgra[RIdx], bgra[GIdx], bgra[BIdx], bgra[AIdx]);
        }

        template <typename T>
        void Set(const int64_t row, const int64_t col, const ColorRgba<T> &val)
        {
            // auto& bgra = data.at<cv::Vec4b>((int)row, (int)col);
            bgra[RIdx] = (uint8_t)val.R;
            bgra[GIdx] = (uint8_t)val.G;
            bgra[BIdx] = (uint8_t)val.B;
            bgra[AIdx] = (uint8_t)val.A;
        }

        struct MapParams
        {
            ColorRgba<uint8_t> &Color;
            int64_t row;
            int64_t col;
            const Image::ImageFile &img;
        };

    private:
        static const auto RIdx = 2;
        static const auto GIdx = 1;
        static const auto BIdx = 0;
        static const auto AIdx = 3;
    };
#endif

    namespace __Detail
    {
        template <typename... Args>
        std::string StringCombine(Args &&...args)
        {
            std::stringstream ss;
            (ss << ... << args);
            return ss.str();
        }
    }

    class Exception : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

#define __Image_Ex__(...) Exception(__Detail::StringCombine(                       \
    "[", std::filesystem::path(__FILE__).filename().string(), ":", __LINE__, "] ", \
    "[", __FUNCTION__, "] ",                                                       \
    "[Exception] ",                                                                \
    __VA_ARGS__))

    template <typename T = double>
    struct ColorHsl
    {
        T H;
        T S;
        T L;

        ColorHsl() : H(0), S(0), L(0) {}

        ColorHsl(const T v) : H(v), S(v), L(v) {}

        ColorHsl(const T h, const T s, const T l) : H(h), S(s), L(l) {}

        ColorHsl(const T (&arr)[3]) : H(arr[0]), S(arr[1]), L(arr[2]) {}
    };

    template <typename T = uint8_t>
    struct ColorRgb
    {
        T R;
        T G;
        T B;

        ColorRgb() : R(0), G(0), B(0) {}

        ColorRgb(const T v) : R(v), G(v), B(v) {}

        ColorRgb(const T r, const T g, const T b) : R(r), G(g), B(b) {}

        ColorRgb(const T (&arr)[3]) : R(arr[0]), G(arr[1]), B(arr[2]) {}

        template<typename DstT>
        ColorRgb<DstT> StaticCast()
        {
            return ColorRgb<DstT>(static_cast<DstT>(R), static_cast<DstT>(G), static_cast<DstT>(B));
        }
    };

    template <typename T = uint8_t>
    struct ColorRgba
    {
        T R;
        T G;
        T B;
        T A;

        ColorRgba() : R(0), G(0), B(0), A(0) {}

        ColorRgba(const T v) : R(v), G(v), B(v), A(v) {}
        ColorRgba(const T v, const T a) : R(v), G(v), B(v), A(a) {}

        ColorRgba(const T r, const T g, const T b, const T a) : R(r), G(g), B(b), A(a) {}

        ColorRgba(const T (&arr)[4]) : R(arr[0]), G(arr[1]), B(arr[2]), A(arr[3]) {}

        ColorRgba(const ColorRgb<T>& rgb, const T a) : R(rgb.R), G(rgb.G), B(rgb.B), A(a) {}

#undef RGB
        ColorRgb<T> RGB()
        {
            return {R, G, B};
        }

        template<typename DstT>
        ColorRgba<DstT> StaticCast()
        {
            return ColorRgba<DstT>(static_cast<DstT>(R), static_cast<DstT>(G), static_cast<DstT>(B), static_cast<DstT>(A));
        }
    };

#undef max
#undef min
    template <typename T>
    ColorHsl<T> RgbToHsl(const ColorRgb<T> &color)
    {
        const auto [r, g, b] = color;
        const auto rgb = {r, g, b};
        const auto cMax = std::max_element(rgb.begin(), rgb.end());
        const auto cMin = std::min_element(rgb.begin(), rgb.end());
        const auto delta = *cMax - *cMin;

        T h{};
        if (delta == 0)
            h = 0;
        else
        {
            h = static_cast<T>(60.);
            switch (std::distance(rgb.begin(), cMax))
            {
            case 0:
                static const auto mod = [](const T lhs, const T rhs)
                { return std::fmod(rhs + std::fmod(lhs, rhs), rhs); };
                h *= mod((g - b) / delta, 6.);
                break;
            case 1:
                h *= static_cast<T>((b - r) / delta + 2.);
                break;
            case 2:
                h *= static_cast<T>((r - g) / delta + 4.);
                break;
            default:
                assert(true);
                break;
            }
        }

        T l = static_cast<T>((*cMax + *cMin) / 2.);
        T s = delta == 0 ? 0 : delta / (1 - std::abs(2 * l - 1));

        return {h, s, l};
    }

    template <typename T>
    ColorRgb<T> HslToRgb(const ColorHsl<T> &color)
    {
        const auto [h, s, l] = color;

        const T c = static_cast<T>((1. - std::abs(2. * l - 1.)) * s);
        const T x = static_cast<T>(c * (1. - std::abs(std::fmod(h / 60., 2.) - 1.)));
        const T m = static_cast<T>(l - c / 2.);

        ColorRgb<T> res;

        if (0. <= h && h < 60.)
        {
            res = {c, x, 0};
        }
        else if (60. <= h && h < 120.)
        {
            res = {x, c, 0};
        }
        else if (120. <= h && h < 180.)
        {
            res = {0, c, x};
        }
        else if (180. <= h && h < 240.)
        {
            res = {0, x, c};
        }
        else if (240. <= h && h < 300.)
        {
            res = {x, 0, c};
        }
        else if (300. <= h && h < 360.)
        {
            res = {c, 0, x};
        }
        else
        {
            assert(true);
        }

        res.R += m;
        res.G += m;
        res.B += m;

        return res;
    }

    inline ColorRgb<uint8_t> FloatToUint8(const ColorRgb<float>& color)
    {
        return ColorRgb{
            static_cast<uint8_t>(std::clamp(std::round(color.R * 255.f), 0.f, 255.f)),
                static_cast<uint8_t>(std::clamp(std::round(color.G * 255.f), 0.f, 255.f)),
                static_cast<uint8_t>(std::clamp(std::round(color.B * 255.f), 0.f, 255.f))};
    }

    inline ColorRgba<uint8_t> FloatToUint8(const ColorRgba<float>& color)
    {
        return ColorRgba{
                static_cast<uint8_t>(std::clamp(std::round(color.R * 255.f), 0.f, 255.f)),
                static_cast<uint8_t>(std::clamp(std::round(color.G * 255.f), 0.f, 255.f)),
                static_cast<uint8_t>(std::clamp(std::round(color.B * 255.f), 0.f, 255.f)),
                static_cast<uint8_t>(std::clamp(std::round(color.A * 255.f), 0.f, 255.f))};
    }

#ifdef UseStb
    class ImageFile
    {
        uint8_t *data = nullptr;
        int width = 0;
        int height = 0;
        bool autoFree = true;

    public:
        ImageFile() = default;

        ImageFile(const std::filesystem::path &file)
        {
            LoadFile(file);
        }

        ImageFile(const uint8_t *fileData, const int size)
        {
            data = stbi_load_from_memory(fileData, size, &width, &height, nullptr, 4);
        }

        ImageFile(uint8_t *data, const int width, const int height, const bool autoFree = true) : data(data), width(width), height(height), autoFree(autoFree) {}

        ImageFile(const int width, const int height) : width(width), height(height)
        {
            data = new uint8_t[static_cast<size_t>(width) * height * 4];
        }

        ImageFile(const ImageFile &img)
        {
            width = img.width;
            height = img.height;
            autoFree = true;
            data = new uint8_t[img.Size()];
            std::copy_n(img.data, img.Size(), data);
        }

        ImageFile(ImageFile &&img) noexcept
        {
            width = img.width;
            height = img.height;
            data = img.data;
            autoFree = img.autoFree;
            img.width = 0;
            img.height = 0;
            img.data = nullptr;
        }

        ImageFile &operator=(const ImageFile &img)
        {
            this->Clear();
            this->width = img.width;
            this->height = img.height;
            this->autoFree = true;
            this->data = new uint8_t[img.Size()];
            std::copy_n(img.data, img.Size(), this->data);
            return *this;
        }

        ImageFile &operator=(ImageFile &&img) noexcept
        {
            Clear();
            width = img.width;
            height = img.height;
            data = img.data;
            autoFree = img.autoFree;
            img.width = 0;
            img.height = 0;
            img.data = nullptr;
            return *this;
        }

        ~ImageFile()
        {
            Clear();
        }

        ImageFile Clone() const
        {
            ImageFile buf(width, height);
            std::copy_n(data, Size(), buf.Data());
            return buf;
        }

        void Clear()
        {
            if (autoFree && data != nullptr)
                stbi_image_free(data);
            data = nullptr;
            width = 0;
            height = 0;
        }

        void LoadFile(const std::filesystem::path &file)
        {
            data = stbi_load(reinterpret_cast<const char *>(file.u8string().c_str()), &width, &height, nullptr, 4);
            if (!data)
                throw __Image_Ex__("invalid data: \"", stbi_failure_reason(), "\"");
        }

        void Save(const std::filesystem::path &path) const
        {
            const auto ext = path.extension();
#define MakeData (const char *)path.u8string().c_str(), width, height, 4, data
            int ret;
            if (ext == ".png")
                ret = stbi_write_png(MakeData, width * 4);
            else if (ext == ".jpg" || ext == ".jpeg" || ext == ".jpe")
                ret = stbi_write_jpg(MakeData, 100);
            else if (ext == ".bmp")
                ret = stbi_write_bmp(MakeData);
            else if (ext == ".tga")
                ret = stbi_write_tga(MakeData);
            else
                throw __Image_Ex__("Unsupported image format");

            if (!ret)
                throw __Image_Ex__("ret ", ret, ": ", stbi_failure_reason());

            if (!exists(path))
                throw __Image_Ex__("save failed");
#undef MakeData
        }

        [[nodiscard]] const uint8_t *Data() const { return data; }
        [[nodiscard]] uint8_t *Data() { return data; }
        [[nodiscard]] int Width() const { return width; }
        [[nodiscard]] int Height() const { return height; }
        [[nodiscard]] size_t Size() const { return static_cast<size_t>(width) * height * 4; }

        template <typename T>
        [[nodiscard]] ColorRgba<T> At(const int64_t row, const int64_t col) const
        {
            const auto *p = data + (row * width + col) * 4;
            return ColorRgba<T>(p[0], p[1], p[2], p[3]);
        }

        template <typename T>
        void Set(const int64_t row, const int64_t col, const ColorRgba<T> &val)
        {
            auto *bgra = data + (row * width + col) * 4;
            bgra[RIdx] = static_cast<uint8_t>(val.R);
            bgra[GIdx] = static_cast<uint8_t>(val.G);
            bgra[BIdx] = static_cast<uint8_t>(val.B);
            bgra[AIdx] = static_cast<uint8_t>(val.A);
        }

        [[nodiscard]] bool Empty() const
        {
            return data == nullptr || width == 0 || height == 0;
        }

    private:
        static constexpr auto RIdx = 0;
        static constexpr auto GIdx = 1;
        static constexpr auto BIdx = 2;
        static constexpr auto AIdx = 3;
    };
#endif
}