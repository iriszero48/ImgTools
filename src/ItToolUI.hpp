#pragma once

#include <any>
#include <filesystem>
#include <ranges>

#include "Convert/Convert.hpp"
#include "Enumerable/Enumerable.hpp"
#include "Function/Function.hpp"

#include "ItDirect3D.hpp"
#include "ItGui.hpp"
#include "ItLog.hpp"
#include "ItSerialization.hpp"
#include "ItText.hpp"
#include "ItTool.hpp"

#include <misc/cpp/imgui_stdlib.h>

#include "Shader_ColorBalance.h"
#include "Shader_GenerateNormalTexture.h"
#include "Shader_HueSaturation.h"
#include "Shader_LUT3D.h"
#include "Shader_LinearDodgeColor.h"
#include "Shader_LinearDodgeImage.h"
#include "Shader_LinearResize.h"
#include "Shader_NormalMapConvertorRGB2DA.h"

template <typename Impl>
struct ITool
{
    bool IsPreview = false;
    uint64_t GlobalId = 0;

    ITool() {}

    constexpr static const char *Id() { return typeid(Impl).name(); }

    constexpr static const char *Name() { return Impl::Name(); }

    void UI(bool &needUpdate) { return static_cast<Impl *>(this)->UI(); }

    [[nodiscard]] decltype(auto) Processor() const
    {
        return static_cast<Impl *>(this)->Processor();
    }

    [[nodiscard]] std::optional<ImageView>
    GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        return static_cast<Impl *>(this)->GPU(input);
    }

    nlohmann::json SaveData() { return static_cast<Impl *>(this)->SaveData(); }

    void LoadData(const nlohmann::json &data)
    {
        static_cast<Impl *>(this)->LoadData(data);
    }
};

#pragma region InitShader
#define InitShader(func, sh)                                                     \
    static std::unordered_map<Dx11DevType *,                                     \
                              Microsoft::WRL::ComPtr<ID3D11ComputeShader>>       \
        sh##Shaders = []() {                                                     \
            try                                                                  \
            {                                                                    \
                return std::unordered_map<                                       \
                    Dx11DevType *, Microsoft::WRL::ComPtr<ID3D11ComputeShader>>{ \
                    {D3D11Dev.Get(),                                             \
                     D3D11::CreateComputeShader(D3D11Dev.Get(), sh)},            \
                    {D3D11CSDev.Get(),                                           \
                     D3D11::CreateComputeShader(D3D11CSDev.Get(), sh)}};         \
            }                                                                    \
            catch (...)                                                          \
            {                                                                    \
                std::rethrow_if_nested(                                          \
                    Ex(ToolException, "[" #sh "] init shader failed"));          \
            }                                                                    \
            return std::unordered_map<                                           \
                Dx11DevType *, Microsoft::WRL::ComPtr<ID3D11ComputeShader>>{};   \
        }();                                                                     \
    const auto sh##Shader = sh##Shaders[dev].Get()
#pragma endregion InitShader

struct LutTool : ITool<LutTool>
{
    using ProcessorType = ImageTools::LUT;

    struct ToolData
    {
        U8String CubeFilePath{};
    } Data;

    LutTool()
    {
        static const auto DefaultCube = [&]() -> std::u8string
        {
            try
            {
                const auto path = Config::TmpDir / "Default64.cube";
                const RcResource lutDefault64(MAKEINTRESOURCE(LUT_DEFAULT_64), RT_RCDATA, "LUT_DEFAULT_64");
                File::WriteAll(path, lutDefault64.GetString());
                return path.u8string();
            }
            catch (...)
            {
                std::rethrow_if_nested(ToolException("init LutTool failed"));
            }
            return {};
        }();
        Data.CubeFilePath.Set(DefaultCube);
        Check();
    }

    [[nodiscard]] nlohmann::json SaveData() const
    {
        auto obj = nlohmann::json::object();
        if (Data.CubeFilePath.Empty())
        {
            obj[String_data] = nullptr;
        }
        else
        {
            obj[String_data] = FilePacker(Data.CubeFilePath.GetPath());
        }
        return obj;
    }

    void LoadData(const nlohmann::json &obj)
    {
        const auto &data = obj[String_data];

        try
        {
            Data.CubeFilePath = FileUnpacker(data).u8string();
        }
        catch (const std::exception &ex)
        {
            Data.CubeFilePath.Buf.clear();
            LogWarn("load data failed: {}", ex.what());
        }
        Check();
    }

    bool Valid = false;

    static const char *Name() { return Text::ColorLookup(); }

    bool Check()
    {
        Valid = false;
        if (!Data.CubeFilePath.Empty() && exists(Data.CubeFilePath.GetPath()))
            Valid = true;
        return Valid;
    }

    void UI(bool &needUpdate)
    {
        ImGui::InputText(Text::CubeFile(), &Data.CubeFilePath.Buf);
        if (ImGui::IsItemDeactivatedAfterEdit())
            if (Check())
                needUpdate = true;
        ImGui::SameLine();
        if (ImGui::Button(Text::SelectSomething()))
        {
            if (const auto tmp = Pick::PickFile({.Filter = L"3D Cube File\0*.cube\0", .Flags = OFN_FILEMUSTEXIST}).u8string();
                !tmp.empty())
                Data.CubeFilePath.Set(tmp);
            if (Check())
                needUpdate = true;
        }
        if (!Valid)
            ImGui::TextColored({1.f, 0.f, 0.f, 1.f}, "* %s", Text::InvalidPath());
    }

    [[nodiscard]] std::optional<ProcessorType> Processor() const
    {
        if (!Valid)
            return {};
        return ProcessorType(Data.CubeFilePath.GetView());
    }

    struct ShaderData
    {
        float MaxRGB[3];
        float MinRGB[3];
        float Size;
    };

    [[nodiscard]] std::optional<ImageView>
    GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        static Lut::CubeLut cube;

        if (!Valid)
            return {};

        if (static U8String path; path != Data.CubeFilePath)
        {
            path = Data.CubeFilePath;
            try
            {
                cube = Lut::CubeLut::FromCubeFile(Data.CubeFilePath.GetPath());
            }
            catch (...)
            {
                Valid = false;
                return {};
            }
        }

        InitShader("LutTool", g_LUT3D);

        const auto tex3d = D3D11::CreateTexture3d(dev, cube);

        const auto resBuf =
            D3D11::CreateTexture2dUavBuf(dev, input.Width, input.Height);
        const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

        const auto ss = D3D11::CreateSampler(dev);

        auto [ar, ag, ab] = cube.DomainMax;
        auto [ir, ig, ib] = cube.DomainMin;

        const float shaderData[7] = {
            ar, ag, ab, ir, ig, ib, static_cast<float>(cube.Length())};
        const auto dataBuf =
            D3D11::CreateStructuredBuffer(dev, sizeof(ShaderData), 1, shaderData);
        const auto dataSrv = D3D11::CreateBufferSRV(dev, dataBuf.Get());

        ID3D11ShaderResourceView *srvs[] = {input.SRV.Get(), tex3d.Get(),
                                            dataSrv.Get()};
        ID3D11UnorderedAccessView *uavs[] = {resBufUav.Get()};
        ID3D11SamplerState *sss[] = {ss.Get()};

        D3D11::RunComputeShader(devCtx, g_LUT3DShader, srvs, uavs, sss,
                                D3D11::GetThreadGroupNum(input.Width),
                                D3D11::GetThreadGroupNum(input.Height), 1);

        tex3d->Release();

        return ImageView(input, D3D11::CreateSrvFromTex(dev, resBuf.Get()));
    }
};

#if 0
struct LinearDodgeColorTool : ITool<LinearDodgeColorTool>
{
	using ProcessorType = ImageTools::LinearDodgeColor;

	struct ToolData
	{
		std::array<float, 4> Color = {0, 0, 0, 0};

		NLOHMANN_DEFINE_TYPE_INTRUSIVE(ToolData, Color)
	} Data;

	nlohmann::json SaveData() const
	{
		return nlohmann::json::object({{String_data, Data}});
	}

	void LoadData(const nlohmann::json &obj) { obj[String_data].get_to(Data); }

	static const char *Name() { return Text::LinearDodgeColor(); }

	void UI(bool &needUpdate)
	{
		ImGui::ColorEdit4(Text::Color(), Data.Color.data());
		if (ImGui::IsItemEdited())
			needUpdate = true;
	}

	[[nodiscard]] std::optional<ProcessorType> Processor() const
	{
		return ProcessorType(
			Image::ColorRgba<uint8_t>(Data.Color[0] * 255., Data.Color[1] * 255.,
									  Data.Color[2] * 255., Data.Color[3] * 255.));
	}

	[[nodiscard]] std::optional<ImageView>
	GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
	{
		InitShader("LinearDodgeColorTool", g_LinearDodgeColor);

		const auto resBuf =
			D3D11::CreateTexture2dUavBuf(dev, input.Width, input.Height);
		const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

		const auto dataBuf = D3D11::CreateStructuredBuffer(dev, sizeof(float) * 4,
														   1, Data.Color.data());
		const auto dataSrv = D3D11::CreateBufferSRV(dev, dataBuf.Get());

		ID3D11ShaderResourceView *srvs[2] = {input.SRV.Get(), dataSrv.Get()};
		ID3D11UnorderedAccessView *uavs[] = {resBufUav.Get()};
		D3D11::RunComputeShader(devCtx, g_LinearDodgeColorShader, srvs, uavs,
								D3D11::GetThreadGroupNum(input.Width),
								D3D11::GetThreadGroupNum(input.Height), 1);

		return ImageView(input, D3D11::CreateSrvFromTex(dev, resBuf.Get()));
	}
};

struct LinearDodgeImageTool : ITool<LinearDodgeImageTool>
{
	using ProcessorType = ImageTools::LinearDodgeImage;

	struct ToolData
	{
		U8String ImagePath;

		NLOHMANN_DEFINE_TYPE_INTRUSIVE(ToolData, ImagePath)
	} Data;

	nlohmann::json SaveData() const
	{
		auto obj = nlohmann::json::object();
		if (Data.ImagePath.Empty())
		{
			obj[String_data] = nullptr;
		}
		else
		{
			const auto path = Data.ImagePath.GetPath();
			obj[String_data] = File::ReadAll(path);
			obj[String_ext] = path.extension().u8string();
		}
		return obj;
	}

	void LoadData(const nlohmann::json &obj)
	{
		const auto data = obj[String_data].get<std::string>();
		const auto ext = obj[String_ext].get<std::u8string>();

		const auto tmpPath =
			Config::TmpDir / String::FormatW("{}{}", UUID4(), ext);
		File::WriteAll(tmpPath, data);
		Data.ImagePath = tmpPath.u8string();
		Valid = true;
	}

	bool Valid = false;

	static const char *Name() { return Text::LinearDodgeImage(); }

	bool Check()
	{
		Valid = false;
		if (!Data.ImagePath.Empty() && exists(Data.ImagePath.GetPath()))
			Valid = true;
		return Valid;
	}

	void UI(bool &needUpdate)
	{
		ImGui::InputText(Text::ImageFile(), &Data.ImagePath.Buf);
		if (ImGui::IsItemEdited())
			if (Check())
				needUpdate = true;
		ImGui::SameLine();
		if (ImGui::Button(Text::SelectSomething()))
		{
			if (const auto p = Pick::PickFile(); !p.empty())
				Data.ImagePath.Set(p.u8string());
			if (Check())
				needUpdate = true;
		}
		if (!Valid)
			ImGui::TextColored({1.f, 0.f, 0.f, 1.f}, "* %s", Text::InvalidPath());
	}

	[[nodiscard]] std::optional<ProcessorType> Processor() const
	{
		if (!Valid)
			return {};
		return ProcessorType(Data.ImagePath.GetPath());
	}

	[[nodiscard]] std::optional<ImageView>
	GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
	{
		static std::filesystem::path refPath{};
		static Image::ImageFile ref{};

		if (!Valid)
			return {};

		if (Data.ImagePath.GetPath() != refPath)
		{
			ref = Image::ImageFile(Data.ImagePath.GetPath());
			if (ref.Empty())
			{
				Valid = false;
				return {};
			}
			refPath = Data.ImagePath.GetPath();
		}

		InitShader("LinearDodgeImageTool", g_LinearDodgeImage);

		const auto resBuf =
			D3D11::CreateTexture2dUavBuf(dev, input.Width, input.Height);
		const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

		const auto refSrv = D3D11::LoadTextureFromFile(dev, ref);

		ID3D11ShaderResourceView *srvs[2] = {input.SRV.Get(), refSrv.SRV.Get()};
		ID3D11UnorderedAccessView *uavs[] = {resBufUav.Get()};
		D3D11::RunComputeShader(devCtx, g_LinearDodgeImageShader, srvs, uavs,
								D3D11::GetThreadGroupNum(input.Width),
								D3D11::GetThreadGroupNum(input.Height), 1);

		return ImageView(input, D3D11::CreateSrvFromTex(dev, resBuf.Get()));
	}
};
#endif

struct LinearDodgeTool : ITool<LinearDodgeTool>
{
    using ProcessorType = LinearDodge;

    struct ToolData
    {
        LinearDodgeType Type = LinearDodgeType::Color;
        float Color[4] = {0, 0, 0, 0};
        U8String ImagePath{};
    } Data;

    [[nodiscard]] nlohmann::json SaveData() const
    {
        auto obj = nlohmann::json::object();
        obj[String_type] = Enum::ToString(Data.Type);
        if (Data.Type == LinearDodgeType::Color)
        {
            obj[String_data] = Data.Color;
        }
        else if (Data.Type == LinearDodgeType::Image)
        {
            if (Data.ImagePath.Empty())
            {
                obj[String_data] = nullptr;
            }
            else
            {
                obj[String_data] = FilePacker(Data.ImagePath.GetPath());
            }
        }
        else
        {
            assert((false, "Invalid LinearDodgeType"));
        }
        return obj;
    }

    void LoadData(const nlohmann::json &obj)
    {
        Data.Type = obj[String_type].get<LinearDodgeType>();
        const auto &data = obj[String_data];

        if (Data.Type == LinearDodgeType::Color)
        {
            data.get_to(Data.Color);
        }
        else if (Data.Type == LinearDodgeType::Image)
        {
            try
            {
                Data.ImagePath = FileUnpacker(data).u8string();
            }
            catch (const std::exception &ex)
            {
                Data.ImagePath.Clear();
                LogWarn("load data failed: {}", ex.what());
            }
            Check();
        }
        else
        {
            assert((false, "Invalid LinearDodgeType"));
        }
    }

    static const char *Name() { return Text::LinearDodge(); }

    bool Valid = false;

    bool Check()
    {
        Valid = false;
        if (!Data.ImagePath.Empty() && exists(Data.ImagePath.GetPath()))
            Valid = true;
        return Valid;
    }

    void UI(bool &needUpdate)
    {
        needUpdate |= ImGui::RadioButton(Text::LinearDodgeColor(), reinterpret_cast<int *>(&Data.Type), static_cast<int>(LinearDodgeType::Color));
        ImGui::SameLine();
        needUpdate |= ImGui::RadioButton(Text::LinearDodgeImage(), reinterpret_cast<int *>(&Data.Type), static_cast<int>(LinearDodgeType::Image));
        ImGui::Separator();

        if (Data.Type == LinearDodgeType::Color)
        {
            needUpdate |= ImGui::ColorEdit4(Text::Color(), Data.Color);
        }
        else if (Data.Type == LinearDodgeType::Image)
        {
            ImGui::InputText(Text::ImageFile(), &Data.ImagePath.Buf);
            if (ImGui::IsItemDeactivatedAfterEdit())
                if (Check())
                    needUpdate = true;
            ImGui::SameLine();
            if (ImGui::Button(Text::SelectSomething()))
            {
                if (const auto p = Pick::PickFile({.Flags = OFN_FILEMUSTEXIST}); !p.empty())
                    Data.ImagePath.Set(p.u8string());
                if (Check())
                    needUpdate = true;
            }
            if (!Valid)
                ImGui::TextColored({1.f, 0.f, 0.f, 1.f}, "* %s", Text::InvalidPath());
        }
        else
        {
            assert((false, "Invalid LinearDodgeType"));
        }
    }

    [[nodiscard]] std::optional<ProcessorType> Processor() const
    {
        if (Data.Type == LinearDodgeType::Color)
        {
            return ProcessorType(Data.Color);
        }
        else if (Data.Type == LinearDodgeType::Image)
        {
            if (!Valid)
                return {};

            return ProcessorType(Data.ImagePath.GetPath());
        }
        else
        {
            assert((false, "Invalid LinearDodgeType"));
        }

        return {};
    }

    [[nodiscard]] std::optional<ImageView> GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        if (Data.Type == LinearDodgeType::Color)
        {
            InitShader("LinearDodgeColorTool", g_LinearDodgeColor);

            const auto resBuf =
                D3D11::CreateTexture2dUavBuf(dev, input.Width, input.Height);
            const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

            const auto dataBuf = D3D11::CreateStructuredBuffer(dev, sizeof(float) * 4,
                                                               1, &Data.Color);
            const auto dataSrv = D3D11::CreateBufferSRV(dev, dataBuf.Get());

            ID3D11ShaderResourceView *srvs[2] = {input.SRV.Get(), dataSrv.Get()};
            ID3D11UnorderedAccessView *uavs[] = {resBufUav.Get()};
            D3D11::RunComputeShader(devCtx, g_LinearDodgeColorShader, srvs, uavs,
                                    D3D11::GetThreadGroupNum(input.Width),
                                    D3D11::GetThreadGroupNum(input.Height), 1);

            return ImageView(input, D3D11::CreateSrvFromTex(dev, resBuf.Get()));
        }
        else if (Data.Type == LinearDodgeType::Image)
        {
            static std::filesystem::path refPath{};
            static Image::ImageFile ref{};

            if (!Valid)
                return {};

            if (Data.ImagePath.GetPath() != refPath)
            {
                ref = Image::ImageFile(Data.ImagePath.GetPath());
                if (ref.Empty())
                {
                    Valid = false;
                    return {};
                }
                refPath = Data.ImagePath.GetPath();
            }

            InitShader("LinearDodgeImageTool", g_LinearDodgeImage);

            const auto resBuf =
                D3D11::CreateTexture2dUavBuf(dev, input.Width, input.Height);
            const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

            const auto refSrv = D3D11::LoadTextureFromFile(dev, ref);

            ID3D11ShaderResourceView *srvs[2] = {input.SRV.Get(), refSrv.SRV.Get()};
            ID3D11UnorderedAccessView *uavs[] = {resBufUav.Get()};
            D3D11::RunComputeShader(devCtx, g_LinearDodgeImageShader, srvs, uavs,
                                    D3D11::GetThreadGroupNum(input.Width),
                                    D3D11::GetThreadGroupNum(input.Height), 1);

            return ImageView(input, D3D11::CreateSrvFromTex(dev, resBuf.Get()));
        }
        else
        {
            assert((false, "Invalid LinearDodgeType"));
        }

        return {};
    }
};

struct GenerateNormalTextureTool : ITool<GenerateNormalTextureTool>
{
    using ProcessorType = ImageTools::GenerateNormalTexture;

    struct ToolData
    {
        float Bias = 50.;
        bool InvertR = false;
        bool InvertG = false;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ToolData, Bias, InvertR, InvertG)
    } Data;

    [[nodiscard]] nlohmann::json SaveData() const
    {
        return nlohmann::json::object({{String_data, Data}});
    }

    void LoadData(const nlohmann::json &obj) { obj[String_data].get_to(Data); }

    static const char *Name() { return Text::GenerateNormalTexture(); }

    void UI(bool &needUpdate)
    {
        needUpdate |= ImGui::SliderFloat(U8 "bias", &Data.Bias, 0.0f, 100.0f, "%.3f");
        GUI::DoubleClickToEdit();

        needUpdate |= ImGui::Checkbox(U8 "invert R", &Data.InvertR);
        needUpdate |= ImGui::Checkbox(U8 "invert G", &Data.InvertG);
    }

    [[nodiscard]] std::optional<ProcessorType> Processor() const
    {
        return ProcessorType(Data.Bias, Data.InvertR, Data.InvertG);
    }

    struct ShaderData
    {
        float Bias;
        uint32_t InvertR;
        uint32_t InvertG;
        float Width;
        float Height;
    };

    [[nodiscard]] std::optional<ImageView>
    GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        InitShader("GenerateNormalTextureTool", g_GenerateNormalTexture);

        const auto resBuf =
            D3D11::CreateTexture2dUavBuf(dev, input.Width, input.Height);
        const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

        const ShaderData data{Data.Bias, Data.InvertR, Data.InvertG,
                              static_cast<float>(input.Width),
                              static_cast<float>(input.Height)};
        const auto dataBuf =
            D3D11::CreateStructuredBuffer(dev, sizeof(ShaderData), 1, &data);
        const auto dataSrv = D3D11::CreateBufferSRV(dev, dataBuf.Get());

        const auto sam = D3D11::CreateSampler(dev);

        ID3D11ShaderResourceView *srvs[2] = {input.SRV.Get(), dataSrv.Get()};
        ID3D11UnorderedAccessView *uavs[1] = {resBufUav.Get()};
        ID3D11SamplerState *sss[1] = {sam.Get()};

        D3D11::RunComputeShader(devCtx, g_GenerateNormalTextureShader, srvs, uavs,
                                sss, D3D11::GetThreadGroupNum(input.Width),
                                D3D11::GetThreadGroupNum(input.Height), 1);

        return ImageView(input, D3D11::CreateSrvFromTex(dev, resBuf.Get()));
    }
};

struct NormalMapConvertorTool : ITool<NormalMapConvertorTool>
{
    using ProcessorType = ImageTools::NormalMapConvert;

    struct ToolData
    {
        ProcessorType::Format InputType = ProcessorType::Format::RGB;
        ProcessorType::Format OutputType = ProcessorType::Format::DA;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ToolData, InputType, OutputType)
    } Data;

    [[nodiscard]] nlohmann::json SaveData() const
    {
        return nlohmann::json::object({{String_data, Data}});
    }

    void LoadData(const nlohmann::json &obj) { obj[String_data].get_to(Data); }

    static const char *Name() { return Text::NormalMapFormatConvert(); }

    void UI(bool &needUpdate)
    {
        ImGui::Text("%s", Text::InputFormat());
        ImGui::SameLine();
        ImGui::RadioButton(U8 "RGB##in", reinterpret_cast<int *>(&Data.InputType),
                           0);
        if (ImGui::IsItemEdited())
            needUpdate = true;
#if 0
		ImGui::SameLine();
		ImGui::RadioButton(U8 "DA##in", reinterpret_cast<int*>(&Data.InputType),
			1);
		if (ImGui::IsItemEdited())
			needUpdate = true;
#endif

        ImGui::Text("%s", Text::OutputFormat());
        ImGui::SameLine();
#if 0
		ImGui::RadioButton(U8 "RGB##out", reinterpret_cast<int*>(&Data.OutputType),
			0);
		if (ImGui::IsItemEdited())
			needUpdate = true;
		ImGui::SameLine();
#endif
        ImGui::RadioButton(U8 "DA##out", reinterpret_cast<int *>(&Data.OutputType),
                           1);
        if (ImGui::IsItemEdited())
            needUpdate = true;
    }

    [[nodiscard]] std::optional<ProcessorType> Processor() const
    {
        if (Data.InputType == Data.OutputType)
            return {};
        return ProcessorType(Data.InputType, Data.OutputType);
    }

    [[nodiscard]] std::optional<ImageView>
    GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        if (Data.InputType == Data.OutputType)
            return {};

        InitShader("NormalMapConvertorTool", g_NormalMapConvertorRGB2DA);

        const auto resBuf =
            D3D11::CreateTexture2dUavBuf(dev, input.Width, input.Height);
        const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

        ID3D11ShaderResourceView *srvs[] = {input.SRV.Get()};
        ID3D11UnorderedAccessView *uavs[] = {resBufUav.Get()};

        if (Data.InputType == ProcessorType::Format::RGB &&
            Data.OutputType == ProcessorType::Format::DA)
        {
            D3D11::RunComputeShader(devCtx, g_NormalMapConvertorRGB2DAShader, srvs,
                                    uavs, D3D11::GetThreadGroupNum(input.Width),
                                    D3D11::GetThreadGroupNum(input.Height), 1);
        }
        else
        {
            throw Ex(ToolException, "[DA->RGB] not impl");
        }

        return ImageView(input, D3D11::CreateSrvFromTex(dev, resBuf.Get()));
    }
};

struct ColorBalanceTool : ITool<ColorBalanceTool>
{
    using ProcessorType = ImageTools::ColorBalance;

    struct ToolData
    {
        ProcessorType::Range Range = ProcessorType::Range::Midtones;

        float CyanRed = 0;
        float MagentaGreen = 0;
        float YellowBlue = 0;

        bool PreserveLuminosity = true;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ToolData, Range, CyanRed, MagentaGreen,
                                       YellowBlue, PreserveLuminosity)
    } Data;

    [[nodiscard]] nlohmann::json SaveData() const
    {
        return nlohmann::json::object({{String_data, Data}});
    }

    void LoadData(const nlohmann::json &obj) { obj[String_data].get_to(Data); }

    static const char *Name() { return Text::ColorBalance(); }

    void UI(bool &needUpdate)
    {
        ImGui::Text("%s:", Text::Range());
        ImGui::SameLine();
        needUpdate |= ImGui::RadioButton(
            Text::Shadows(), reinterpret_cast<int *>(&Data.Range),
            static_cast<int>(decltype(Data.Range)::Shadows));
        ImGui::SameLine();
        needUpdate |= ImGui::RadioButton(
            Text::Midtones(), reinterpret_cast<int *>(&Data.Range),
            static_cast<int>(decltype(Data.Range)::Midtones));
        ImGui::SameLine();
        needUpdate |= ImGui::RadioButton(
            Text::Highlights(), reinterpret_cast<int *>(&Data.Range),
            static_cast<int>(decltype(Data.Range)::Highlights));

        ImGui::Text("%s", Text::Cyan());
        ImGui::SameLine();
        needUpdate |=
            ImGui::SliderFloat(Text::Red(), &Data.CyanRed, -100., 100., "%.1f");
        GUI::DoubleClickToEdit();

        ImGui::Text("%s", Text::Magenta());
        ImGui::SameLine();
        needUpdate |= ImGui::SliderFloat(Text::Green(), &Data.MagentaGreen, -100.,
                                         100., "%.1f");
        GUI::DoubleClickToEdit();

        ImGui::Text("%s", Text::Yellow());
        ImGui::SameLine();
        needUpdate |=
            ImGui::SliderFloat(Text::Blue(), &Data.YellowBlue, -100., 100., "%.1f");
        GUI::DoubleClickToEdit();

        needUpdate |=
            ImGui::Checkbox(Text::PreserveLuminosity(), &Data.PreserveLuminosity);
    }

    [[nodiscard]] std::optional<ProcessorType> Processor() const
    {
        return ProcessorType(Data.Range, Data.CyanRed, Data.MagentaGreen,
                             Data.YellowBlue, Data.PreserveLuminosity);
    }

    struct ShaderData
    {
        int Range = 0;
        float CyanRed = 0;
        float MagentaGreen = 0;
        float YellowBlue = 0;
        int PreserveLuminosity = 0;
    };

    [[nodiscard]] std::optional<ImageView>
    GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        InitShader("ColorBalanceTool", g_ColorBalance);

        const auto resBuf =
            D3D11::CreateTexture2dUavBuf(dev, input.Width, input.Height);
        const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

        ShaderData shaderData;

        shaderData.Range = static_cast<int>(Data.Range);
        shaderData.CyanRed = Data.CyanRed;
        shaderData.MagentaGreen = Data.MagentaGreen;
        shaderData.YellowBlue = Data.YellowBlue;
        shaderData.PreserveLuminosity = Data.PreserveLuminosity;

        const auto dataBuf =
            D3D11::CreateStructuredBuffer(dev, sizeof(ShaderData), 1, &shaderData);
        const auto dataSrv = D3D11::CreateBufferSRV(dev, dataBuf.Get());

        ID3D11ShaderResourceView *srvs[] = {input.SRV.Get(), dataSrv.Get()};
        ID3D11UnorderedAccessView *uavs[] = {resBufUav.Get()};

        D3D11::RunComputeShader(devCtx, g_ColorBalanceShader, srvs, uavs,
                                D3D11::GetThreadGroupNum(input.Width),
                                D3D11::GetThreadGroupNum(input.Height), 1);

        return ImageView(input, D3D11::CreateSrvFromTex(dev, resBuf.Get()));
    }
};

struct HueSaturationTool : ITool<HueSaturationTool>
{
    using ProcessorType = ImageTools::HueSaturation;

    struct ToolData
    {
        float Hue = 0;
        float Saturation = 0;
        float Lightness = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ToolData, Hue, Saturation, Lightness)
    } Data;

    [[nodiscard]] nlohmann::json SaveData() const
    {
        return nlohmann::json::object({{String_data, Data}});
    }

    void LoadData(const nlohmann::json &obj) { obj[String_data].get_to(Data); }

    static const char *Name() { return Text::HueSaturation(); }

    void UI(bool &needUpdate)
    {
        needUpdate |=
            ImGui::SliderFloat(Text::Hue(), &Data.Hue, -180., 180., "%.1f");
        GUI::DoubleClickToEdit();

        needUpdate |= ImGui::SliderFloat(Text::Saturation(), &Data.Saturation,
                                         -100., 100., "%.1f");
        GUI::DoubleClickToEdit();

        needUpdate |= ImGui::SliderFloat(Text::Lightness(), &Data.Lightness, -100.,
                                         100., "%.1f");
        GUI::DoubleClickToEdit();
    }

    [[nodiscard]] std::optional<ProcessorType> Processor() const
    {
        return ProcessorType(Data.Hue, Data.Saturation, Data.Lightness);
    }

    [[nodiscard]] std::optional<ImageView>
    GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        InitShader("HueSaturationTool", g_HueSaturation);

        const auto resBuf =
            D3D11::CreateTexture2dUavBuf(dev, input.Width, input.Height);
        const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

        const auto dataBuf =
            D3D11::CreateStructuredBuffer(dev, sizeof(ToolData), 1, &Data);
        const auto dataSrv = D3D11::CreateBufferSRV(dev, dataBuf.Get());

        ID3D11ShaderResourceView *srvs[] = {input.SRV.Get(), dataSrv.Get()};
        ID3D11UnorderedAccessView *uavs[] = {resBufUav.Get()};

        D3D11::RunComputeShader(devCtx, g_HueSaturationShader, srvs, uavs,
                                D3D11::GetThreadGroupNum(input.Width),
                                D3D11::GetThreadGroupNum(input.Height), 1);

        return ImageView(input, D3D11::CreateSrvFromTex(dev, resBuf.Get()));
    }
};

struct Waifu2xTool : ITool<Waifu2xTool>
{
    using ProcessorType = ToolCombine<Waifu2xNcnn, StbResize>;

    static constexpr std::array TileValues{-1, 64, 100, 128, 240,
                                           256, 384, 432, 480, 512};

    struct ToolData
    {
        bool Preview = false;
        int Noise = 0;
        int TileSizeIdx = 0;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ToolData, Preview, Noise, TileSizeIdx)
    } Data;

    [[nodiscard]] nlohmann::json SaveData() const
    {
        return nlohmann::json::object({{String_data, Data}});
    }

    void LoadData(const nlohmann::json &obj) { obj[String_data].get_to(Data); }

    constexpr static const char *Name() { return "Waifu2x(CUnet)"; }

    void UI(bool &needUpdate)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 204.f / 255.f, 0.f, 1.f));
        needUpdate |= ImGui::Checkbox(Text::Preview(), &Data.Preview);
        ImGui::PopStyleColor();

        ImGui::Text("%s", Text::DenoiseLevel());

        for (const auto &i : Enumerable::Range(4))
        {
            ImGui::SameLine();
            needUpdate |= ImGui::RadioButton(Convert::ToString(i)->c_str(), &Data.Noise, i);
        }

        static const auto Desc =
            Func::Array(TileValues)
                .Choose([](const auto x)
                        { return Convert::ToString(x); })
                .Set(0, Text::Auto())
                .ToArray<TileValues.size()>();
        static const auto DescCStr =
            Desc | std::views::transform([](const auto &x)
                                         { return x.c_str(); });
        static const auto DescStr =
            Func::Array<const char *>(DescCStr.begin(), DescCStr.end())
                .ToArray<Desc.size()>();
        needUpdate |= ImGui::Combo(Text::TileSize(), &Data.TileSizeIdx,
                                   DescStr.data(), static_cast<int>(DescStr.size()));
    }

    [[nodiscard]] std::optional<ProcessorType> Processor() const
    {
        if (!Data.Preview && IsPreview)
            return ProcessorType(StbResize(2));

        return ProcessorType(Waifu2xNcnn(Data.Noise, TileValues[Data.TileSizeIdx]));
    }

    struct ShaderData
    {
        float Width;
        float Height;
    };

    [[nodiscard]] std::optional<ImageView>
    GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        InitShader("LinearResize", g_LinearResize);

        if (!Data.Preview && IsPreview)
        {
            const auto outW = input.Width * 2;
            const auto outH = input.Height * 2;

            const ShaderData data{
                static_cast<float>(outW),
                static_cast<float>(outH)};

            const auto resBuf = D3D11::CreateTexture2dUavBuf(dev, outW, outH);
            const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

            const auto dataBuf =
                D3D11::CreateStructuredBuffer(dev, sizeof(ShaderData), 1, &data);
            const auto dataSrv = D3D11::CreateBufferSRV(dev, dataBuf.Get());

            const auto sam = D3D11::CreateSampler(dev);

            ID3D11ShaderResourceView *srvs[2] = {input.SRV.Get(), dataSrv.Get()};
            ID3D11UnorderedAccessView *uavs[1] = {resBufUav.Get()};
            ID3D11SamplerState *sss[1] = {sam.Get()};

            D3D11::RunComputeShader(devCtx, g_LinearResizeShader, srvs, uavs,
                                    sss, D3D11::GetThreadGroupNum(outW),
                                    D3D11::GetThreadGroupNum(outH), 1);

            return ImageView(D3D11::CreateSrvFromTex(dev, resBuf.Get()), outW, outH);
        }

        Waifu2xNcnn proc(Data.Noise, TileValues[Data.TileSizeIdx]);
        proc.ImgRef(D3D11::CreateOutTexture(dev, devCtx, input));
        return D3D11::LoadTextureFromFile(dev, proc.GetOutputImage());
    }
};

struct RealsrTool : ITool<RealsrTool>
{
    using ProcessorType = ToolCombine<RealsrNcnn, StbResize>;

    struct ToolData
    {
        bool Preview = false;
        RealsrNcnnModel Model = RealsrNcnnModel::DF2K_JPEG_X4;
        bool UseTta = false;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ToolData, Preview, Model, UseTta)
    } Data;

    [[nodiscard]] nlohmann::json SaveData() const
    {
        return nlohmann::json::object({{String_data, Data}});
    }

    void LoadData(const nlohmann::json &obj) { obj[String_data].get_to(Data); }

    constexpr static const char *Name() { return "RealSR"; }

    void UI(bool &needUpdate)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 204.f / 255.f, 0.f, 1.f));
        needUpdate |= ImGui::Checkbox(Text::Preview(), &Data.Preview);
        ImGui::PopStyleColor();

        needUpdate |= GUI::EnumCombo("Model", Data.Model);
    }

    [[nodiscard]] std::optional<ProcessorType> Processor() const
    {
        if (!Data.Preview && IsPreview)
            return ProcessorType(StbResize(4));

        return ProcessorType(RealsrNcnn(Data.Model, Data.UseTta));
    }

    struct ShaderData
    {
        float Width;
        float Height;
    };

    [[nodiscard]] std::optional<ImageView>
    GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        InitShader("LinearResize", g_LinearResize);

        if (!Data.Preview && IsPreview)
        {
            const auto outW = input.Width * 4;
            const auto outH = input.Height * 4;

            ShaderData data{
                static_cast<float>(outW),
                static_cast<float>(outH)};

            const auto resBuf = D3D11::CreateTexture2dUavBuf(dev, outW, outH);
            const auto resBufUav = D3D11::CreateTexture2dUav(dev, resBuf.Get());

            const auto dataBuf =
                D3D11::CreateStructuredBuffer(dev, sizeof(ShaderData), 1, &data);
            const auto dataSrv = D3D11::CreateBufferSRV(dev, dataBuf.Get());

            const auto sam = D3D11::CreateSampler(dev);

            ID3D11ShaderResourceView *srvs[2] = {input.SRV.Get(), dataSrv.Get()};
            ID3D11UnorderedAccessView *uavs[1] = {resBufUav.Get()};
            ID3D11SamplerState *sss[1] = {sam.Get()};

            D3D11::RunComputeShader(devCtx, g_LinearResizeShader, srvs, uavs,
                                    sss, D3D11::GetThreadGroupNum(outW),
                                    D3D11::GetThreadGroupNum(outH), 1);

            return ImageView(D3D11::CreateSrvFromTex(dev, resBuf.Get()), outW, outH);
        }

        RealsrNcnn proc(Data.Model, Data.UseTta);
        proc.ImgRef(D3D11::CreateOutTexture(dev, devCtx, input));
        return D3D11::LoadTextureFromFile(dev, proc.GetOutputImage());
    }
};

struct CustomTool : ITool<CustomTool>
{
    using ProcessorType = std::any;

    struct ToolData
    {
        U8String Shader{};

        // NLOHMANN_DEFINE_TYPE_INTRUSIVE(ToolData, Shader)
    } Data;

    nlohmann::json SaveData() const
    {
        // return nlohmann::json::object({{String_data, Data}});
    }

    void LoadData(const nlohmann::json &obj)
    {
        // obj[String_data].get_to(Data);
    }

    constexpr static const char *Name() { return "RealSR"; }

    void UI(bool &needUpdate)
    {
        ImGui::InputTextMultiline("HLSL", &Data.Shader.Buf);
    }

    [[nodiscard]] std::optional<ProcessorType> Processor() const
    {
        return std::nullopt;
    }

    [[nodiscard]] std::optional<ImageView>
    GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx, const ImageView &input)
    {
        return std::nullopt;
    }
};

#undef InitShader
