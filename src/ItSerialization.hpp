#pragma once

#include <filesystem>
#include <string_view>

#include <nlohmann/json.hpp>

#include <imgui.h>

#include "File/File.hpp"
#include "String/String.hpp"

#include "ImageTools.hpp"
#include "ItConfig.hpp"
#include "ItText.hpp"
#include "ItTool.hpp"
#include "ItUtility.hpp"

MakeStr(data);
MakeStr(ext);
MakeStr(type);

#undef RGB
MakeEnum(_Language, English, Chinese);
MakeEnum(_NormalMapConvertFormat, RGB, DA);
MakeEnum(_ColorBalance_Range, Shadows, Midtones, Highlights);

inline nlohmann::json FilePacker(const std::filesystem::path &path)
{
    return nlohmann::json{
        {String_data, File::ReadAll(path)},
        {String_ext, path.extension().u8string()}};
}

inline std::filesystem::path FileUnpacker(const nlohmann::json &obj)
{
    const auto data = obj[String_data].get<std::string>();
    const auto ext = obj[String_ext].get<std::u8string>();

    const auto tmpPath =
        Config::TmpDir / String::FormatW("{}{}", UUID4(), ext);
    File::WriteAll(tmpPath, data);
    return tmpPath;
}

namespace nlohmann
{
    template <>
    struct adl_serializer<Text::Language>
    {
        static void to_json(json &j, const Text::Language &v)
        {
            j = Enum::ToString<_Language>(static_cast<_Language>(v));
        }

        static void from_json(const json &j, Text::Language &v)
        {
            v = static_cast<Text::Language>(Enum::FromString<_Language>(j.get<std::string>()));
        }
    };

    template <>
    struct adl_serializer<ImageTools::NormalMapConvert::Format>
    {
        static void to_json(json &j,
                            const ImageTools::NormalMapConvert::Format &v)
        {
            j = Enum::ToString<_NormalMapConvertFormat>(static_cast<_NormalMapConvertFormat>(v));
        }

        static void from_json(const json &j,
                              ImageTools::NormalMapConvert::Format &v)
        {
            v = static_cast<ImageTools::NormalMapConvert::Format>(
                Enum::FromString<_NormalMapConvertFormat>(j.get<std::string>()));
        }
    };

    template <>
    struct adl_serializer<ImVec4>
    {
        static void to_json(json &j, const ImVec4 &v)
        {
            const auto &[x, y, z, w] = v;
            j = std::array{x, y, z, w};
        }

        static void from_json(const json &j, ImVec4 &v)
        {
            const auto &[x, y, z, w] = j.get<std::array<decltype(ImVec4::x), 4>>();
            v = ImVec4(x, y, z, w);
        }
    };

    template <>
    struct adl_serializer<Processor>
    {
        static void to_json(json &j, const Processor &v) { j = Enum::ToString<Processor>(v); }

        static void from_json(const json &j, Processor &v)
        {
            v = Enum::FromString<Processor>(j.get<std::string>());
        }
    };

    template <>
    struct adl_serializer<ImageTools::ColorBalance::Range>
    {
        static void to_json(json &j, const ImageTools::ColorBalance::Range v)
        {
            j = Enum::ToString<_ColorBalance_Range>(static_cast<_ColorBalance_Range>(v));
        }

        static void from_json(const json &j, ImageTools::ColorBalance::Range &v)
        {
            v = static_cast<ImageTools::ColorBalance::Range>(
                Enum::FromString<_ColorBalance_Range>(j.get<std::string>()));
        }
    };

    // template <>
    // struct adl_serializer<std::u8string>
    // {
    // 	static void to_json(json &j, const std::u8string &v)
    // 	{
    // 		j = std::string_view(reinterpret_cast<const char *>(v.c_str()),
    // 							 v.length());
    // 	}
    //
    // 	static void from_json(const json &j, std::u8string &v)
    // 	{
    // 		const auto str = j.get<std::string>();
    // 		v = std::u8string_view(reinterpret_cast<const char8_t *>(str.c_str()),
    // 							   str.length());
    // 	}
    // };

    template <>
    struct adl_serializer<LinearDodgeType>
    {
        static void to_json(json &j, const LinearDodgeType &v)
        {
            j = Enum::ToString(v);
        }

        static void from_json(const json &j, LinearDodgeType &v)
        {
            v = Enum::FromString<LinearDodgeType>(j.get<std::string>());
        }
    };

    template <>
    struct adl_serializer<RealsrNcnnModel>
    {
        static void to_json(json &j, const RealsrNcnnModel &v)
        {
            j = Enum::ToString<RealsrNcnnModel>(v);
        }

        static void from_json(const json &j, RealsrNcnnModel &v)
        {
            v = Enum::FromString<RealsrNcnnModel>(j.get<std::string>());
        }
    };
}
