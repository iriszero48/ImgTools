#pragma once

#include <cassert>
#include <filesystem>
#include <string>

#define U8 (const char *)u8""
#define NormU8(u8) (const char8_t *)(u8)

namespace Text
{
    enum class Language
    {
        English,
        Chinese
    };

    static auto GlobalLanguage = Language::Chinese;

    std::wstring ToWString(const char *txt)
    {
        return std::filesystem::path(NormU8(txt)).wstring();
    }

#define MakeFunc(func) inline const char *func()
#define MakeText(lang, txt)     \
    if (lang == GlobalLanguage) \
    return U8 txt

#define MakeEnText(txt) MakeText(Language::English, txt)
#define MakeCnText(txt)                  \
    MakeText(Language::Chinese, txt);    \
    assert((false, "invalid language")); \
    return "*undefined*"

    MakeFunc(InputPath)
    {
        MakeEnText("Input Path");
        MakeCnText("输入路径");
    }

    MakeFunc(OutputPath)
    {
        MakeEnText("Output Path");
        MakeCnText("输出路径");
    }

    MakeFunc(SelectSomething)
    {
        MakeEnText("Select...");
        MakeCnText("选择...");
    }

    MakeFunc(Export)
    {
        MakeEnText("Export");
        MakeCnText("导出");
    }

    MakeFunc(Start)
    {
        MakeEnText("Start");
        MakeCnText("开始");
    }

    MakeFunc(CurrentFile)
    {
        MakeEnText("Current File");
        MakeCnText("当前文件");
    }

    MakeFunc(Setting)
    {
        MakeEnText("Setting");
        MakeCnText("设置");
    }

    MakeFunc(BackgroundColor)
    {
        MakeEnText("Background Color");
        MakeCnText("背景颜色");
    }

    MakeFunc(RawImage)
    {
        MakeEnText("Raw Image");
        MakeCnText("原图");
    }

    MakeFunc(Preview)
    {
        MakeEnText("Preview");
        MakeCnText("预览");
    }

    MakeFunc(Finished)
    {
        MakeEnText("Finished");
        MakeCnText("已完成");
    }

    MakeFunc(File)
    {
        MakeEnText("File");
        MakeCnText("文件");
    }

    MakeFunc(Window)
    {
        MakeEnText("Window");
        MakeCnText("窗口");
    }

    MakeFunc(About)
    {
        MakeEnText("About");
        MakeCnText("关于");
    }

    MakeFunc(CubeFile)
    {
        MakeEnText("*.cube File");
        MakeCnText("*.cube文件");
    }

    MakeFunc(LinearDodgeColor)
    {
        MakeEnText("Linear Dodge(Color)");
        MakeCnText("线性添加(颜色)");
    }

    MakeFunc(Color)
    {
        MakeEnText("Color");
        MakeCnText("颜色");
    }

    MakeFunc(LinearDodgeImage)
    {
        MakeEnText("Linear Dodge(Image)");
        MakeCnText("线性添加(图片)");
    }

    MakeFunc(ImageFile)
    {
        MakeEnText("Image File");
        MakeCnText("图片文件");
    }

    MakeFunc(GenerateNormalTexture)
    {
        MakeEnText("Generate Normal Texture");
        MakeCnText("生成法线贴图");
    }

    MakeFunc(EditValue)
    {
        MakeEnText("EditValue");
        MakeCnText("编辑值");
    }

    MakeFunc(NormalMapFormatConvert)
    {
        MakeEnText("Normal Map Convert");
        MakeCnText("法线格式转换");
    }

    MakeFunc(InputFormat)
    {
        MakeEnText("Input Format");
        MakeCnText("输入格式");
    }

    MakeFunc(OutputFormat)
    {
        MakeEnText("Output Format");
        MakeCnText("输出格式");
    }

    MakeFunc(NowLoading)
    {
        MakeEnText("Now Loading...");
        MakeCnText("少女祈祷中...");
    }

    MakeFunc(Processor)
    {
        MakeEnText("Processor");
        MakeCnText("处理器");
    }

    MakeFunc(ProcessorPreview)
    {
        MakeEnText("Processor(Preview)");
        MakeCnText("处理器(预览)");
    }

    MakeFunc(ProcessorExport)
    {
        MakeEnText("Processor(Export)");
        MakeCnText("处理器(导出)");
    }

    MakeFunc(Error)
    {
        MakeEnText("Error");
        MakeCnText("错误");
    }

    MakeFunc(PathNotFound)
    {
        MakeEnText("Path Not Found");
        MakeCnText("路径不存在");
    }

    MakeFunc(Cancel)
    {
        MakeEnText("Cancel");
        MakeCnText("取消");
    }

    MakeFunc(Format)
    {
        MakeEnText("Format");
        MakeCnText("格式");
    }

    MakeFunc(ColorLookup)
    {
        MakeEnText("Color Lookup");
        MakeCnText("颜色查找");
    }

    MakeFunc(Range)
    {
        MakeEnText("Range");
        MakeCnText("范围");
    }

    MakeFunc(Shadows)
    {
        MakeEnText("Shadows");
        MakeCnText("阴影");
    }

    MakeFunc(Midtones)
    {
        MakeEnText("Midtones");
        MakeCnText("中间调");
    }

    MakeFunc(Highlights)
    {
        MakeEnText("Highlights");
        MakeCnText("高亮");
    }

    MakeFunc(Cyan)
    {
        MakeEnText("Cyan");
        MakeCnText("青色");
    }

    MakeFunc(Red)
    {
        MakeEnText("Red");
        MakeCnText("红色");
    }

    MakeFunc(Magenta)
    {
        MakeEnText("Magenta");
        MakeCnText("品红");
    }

    MakeFunc(Green)
    {
        MakeEnText("Green");
        MakeCnText("绿色");
    }

    MakeFunc(Yellow)
    {
        MakeEnText("Yellow");
        MakeCnText("黄色");
    }

    MakeFunc(Blue)
    {
        MakeEnText("Blue");
        MakeCnText("蓝色");
    }

    MakeFunc(PreserveLuminosity)
    {
        MakeEnText("Preserve Luminosity");
        MakeCnText("保持明度");
    }

    MakeFunc(ColorBalance)
    {
        MakeEnText("ColorBalance");
        MakeCnText("色彩平衡");
    }

    MakeFunc(HueSaturation)
    {
        MakeEnText("Hue/Saturation");
        MakeCnText("色相/饱和度");
    }

    MakeFunc(Hue)
    {
        MakeEnText("Hue");
        MakeCnText("色相");
    }

    MakeFunc(Saturation)
    {
        MakeEnText("Saturation");
        MakeCnText("饱和度");
    }

    MakeFunc(Lightness)
    {
        MakeEnText("Lightness");
        MakeCnText("明度");
    }

    MakeFunc(DenoiseLevel)
    {
        MakeEnText("Denoise Level");
        MakeCnText("降噪等级");
    }

    MakeFunc(TileSize)
    {
        MakeEnText("Tile Size");
        MakeCnText("拆分尺寸");
    }

    MakeFunc(Auto)
    {
        MakeEnText("Auto");
        MakeCnText("自动");
    }

    MakeFunc(Language_)
    {
        MakeEnText("Language");
        MakeCnText("语言");
    }

    MakeFunc(ChineseSimplified)
    {
        MakeEnText("中文（简体）");
        MakeCnText("中文（简体）");
    }

    MakeFunc(English)
    {
        MakeEnText("English");
        MakeCnText("English");
    }

    MakeFunc(VerticalSynchronization)
    {
        MakeEnText("Vertical Synchronization");
        MakeCnText("垂直同步");
    }

    MakeFunc(InvalidPath)
    {
        MakeEnText("Invalid Path");
        MakeCnText("无效路径");
    }

    MakeFunc(Settings)
    {
        MakeEnText("Settings");
        MakeCnText("设置");
    }

    MakeFunc(Exit)
    {
        MakeEnText("Exit");
        MakeCnText("退出");
    }

    MakeFunc(Tools)
    {
        MakeEnText("Tools");
        MakeCnText("工具");
    }

    MakeFunc(Console)
    {
        MakeEnText("Console");
        MakeCnText("控制台");
    }

    MakeFunc(Document)
    {
        MakeEnText("Document");
        MakeCnText("文档");
    }

    MakeFunc(License)
    {
        MakeEnText("License");
        MakeCnText("许可");
    }

    MakeFunc(Changelog)
    {
        MakeEnText("Changelog");
        MakeCnText("变更日志");
    }

    MakeFunc(ResetSettings)
    {
        MakeEnText("Reset Settings");
        MakeCnText("重置设置");
    }

    MakeFunc(File_NotFound)
    {
        MakeEnText("File '{}' not found");
        MakeCnText(R"("{}"未找到)");
    }

    MakeFunc(Preset)
    {
        MakeEnText("Preset");
        MakeCnText("预设");
    }

    MakeFunc(OpenPreset)
    {
        MakeEnText("Open Preset");
        MakeCnText("打开预设");
    }

    MakeFunc(SavePreset)
    {
        MakeEnText("Save Preset");
        MakeCnText("保存预设");
    }

    MakeFunc(LinearDodge)
    {
        MakeEnText("Linear Dodge");
        MakeCnText("线性添加");
    }

    MakeFunc(Preview_NotRecommended)
    {
        MakeEnText("Preview(not recommended)");
        MakeCnText("预览(不推荐)");
    }

    MakeFunc(SourceDirectory)
    {
        MakeEnText("Source Directory");
        MakeCnText("源目录");
    }

    MakeFunc(FpsLimit)
    {
        MakeEnText("FPS Limit");
        MakeCnText("FPS限制");
    }

    MakeFunc(Unlocked)
    {
        MakeEnText("Unlocked");
        MakeCnText("无限制");
    }

    MakeFunc(Base64ToImage)
    {
        MakeEnText("Base64 To Image");
        MakeCnText("Base64 -> 图片");
    }

    MakeFunc(Decode)
    {
        MakeEnText("Decode");
        MakeCnText("解码");
    }

    MakeFunc(Reset)
    {
        MakeEnText("Reset");
        MakeCnText("重置");
    }

#undef MakeFunc
#undef MakeText

#undef MakeEnText
#undef MakeCnText
}
