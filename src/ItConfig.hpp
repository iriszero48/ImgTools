#pragma once

#include <filesystem>

namespace Config
{
    static constexpr auto WindowTitle = L"ImgTools";
    static constexpr auto FontSize = 20.f;
    static const auto TmpDir = []
    {
	    try
	    {
            const auto p = std::filesystem::temp_directory_path() / "ImgTools";
            if (!exists(p))
                create_directory(p);
            return p;
	    }
	    catch (...)
	    {
            return std::filesystem::current_path();
	    }
    }();

    #define ItPresetExt "itpreset"
    static constexpr auto ItPresetFilter = L"preset file(*." ItPresetExt ")\0*." ItPresetExt "\0";
}; // namespace Config
