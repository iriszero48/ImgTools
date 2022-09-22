#pragma region Header
// std
#include <execution>
#include <filesystem>
#include <queue>
#include <thread>
#include <span>

// 3rd
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

// project
#include "ItUtility.hpp"
#include "ItToolUI.hpp"
#include "ItLog.hpp"

// resource
#include "Changelog.h"
#include "ItEvent.hpp"
#include "LICENSE.h"
#include "resource.h"

#pragma endregion Header

#pragma region Var
// proc status
static bool IsProcessing = false;
static std::jthread ProcThread;

static auto CurrentGlobalId = []() -> Sequence<uint64_t>
{
	uint64_t i = 0;
	while (true)
	{
		if (i == 0)
			++i;

		co_yield i++;
	}
}();

#define ItToolList LutTool,                   \
				   LinearDodgeTool,           \
				   GenerateNormalTextureTool, \
				   NormalMapConvertorTool,    \
				   ColorBalanceTool,          \
				   HueSaturationTool,         \
				   Waifu2xTool,               \
				   RealsrTool

#define ItEventList DragDropFilesEvent,  \
					DragDropPresetEvent, \
					SaveSettingEvent,    \
					StartProcessEvent,   \
					EndProcessEvent,     \
					AlwaysEvent,         \
					LoadImageEvent

EventSystem<ItEventList> Events{};
#pragma endregion Var

class ImgTools
{
#pragma region ImgToolsType
	using IoPath = std::pair<std::filesystem::path, std::filesystem::path>;
	using PreviewItem = std::pair<std::filesystem::path, ImageView>;
	using ToolType = ToolListTypes<ItToolList>::ToolType;
	using ProcessorType = ToolListTypes<ItToolList>::ProcessorType;
#pragma endregion ImgToolsType

#pragma region ImgToolsStruct
	struct SettingData
	{
		Text::Language Language = Text::GlobalLanguage;
		ImVec4 ClearColor = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
		bool VSync = true;
		int FpsLimit = 60;
		Processor ExportProcessor = Processor::GPU;
		Processor PreviewProcessor = Processor::GPU;

		static std::string ToJson(const SettingData &data)
		{
			return nlohmann::json(data).dump(4);
		}

		NLOHMANN_DEFINE_TYPE_INTRUSIVE(SettingData, Language, ClearColor, VSync,
		                               FpsLimit, ExportProcessor, PreviewProcessor)
	};
#pragma endregion ImgToolsStruct

#pragma region ImgToolsStatus
	// window
	WNDCLASSEX wc{};
	HWND mainWnd{};
	HICON icon{};

	// data
	ImageView logo{};
	std::array<uint32_t, 4> version{};
	RcResource mainFontRes{};

	// show status
	[[maybe_unused]] bool showDemoWindows = true;
	bool showTools = true;
	bool showRaw = true;
	bool showPreview = true;
	bool showConsole = false;
	bool showSettings = false;
	[[maybe_unused]] bool showInfo = true;
	bool showLicense = false;
	bool showChangelog = false;
	bool showAbout = false;
	[[maybe_unused]] bool showDocument = false;

	// proc status
	ImageFormat imgFormat = ImageFormat::png;
	// bool needJoin = false;
	std::priority_queue<IoPath, std::vector<IoPath>, std::greater<>> procFiles{};
	std::atomic_int64_t processedCount = 0;
	int64_t totalCount = 0;
	float procStatus = 0.f;
	float procTimePreUpdate = 0.f;
	U8String curFile{};

	// ui status
	U8String inputPath{};
	U8String outputPath{};
	std::vector<ToolType> toolList{};
	bool done = false;

	// preview
	std::vector<PreviewItem> rawTextures{};
	std::optional<decltype(rawTextures)::size_type> currentPreviewIdx{};
	ImageView previewTexture;
	// std::filesystem::path previewPath{};
	bool needUpdate = false;

	// config
	SettingData settingData{};
	std::filesystem::path configPath{};
	std::u8string iniPath{};
	std::filesystem::path settingsPath{};
	std::filesystem::path sourceDirectoryPlaceholder{};
#pragma endregion ImgToolsStatus

#pragma region ImgToolsHelper
	static Image::ImageFile ProcessFile(const Image::ImageFile &img,
										std::vector<ToolType> &tools, const bool isPreview)
	{
		Image::ImageFile cur = img;

		std::vector<ProcessorType> processors{};
		for (auto &tool : tools)
		{
			if (auto val = std::visit(
					[&](auto &x) -> std::optional<ProcessorType>
					{
						x.IsPreview = isPreview;
						return x.Processor();
					},
					tool);
				val.has_value())
			{
				processors.push_back(*val);
			}
		}

		for (auto &proc : processors)
		{
			std::visit([&](auto &x)
					   { x.ImgRef(cur); },
					   proc);
			const auto [w, h] = std::visit(
				[](const auto &x) -> ImageTools::ImageSize
				{
					return x.GetOutputSize();
				},
				proc);
			auto buf = Image::ImageFile(w, h);

			Enumerable::Range<int64_t> rng(h);
			std::for_each(
				std::execution::par_unseq, rng.begin(), rng.end(),
				[&, w = w](const auto &hIdx)
				{
					for (int wIdx = 0; wIdx < w; ++wIdx)
					{
						buf.Set(hIdx, wIdx,
								std::visit([&](auto &x)
										   { return x(hIdx, wIdx); },
										   proc));
					}
				});

			cur = buf;
		}

		return cur;
	}

	static ImageView GPU(Dx11DevType *dev, Dx11DevCtxType *devCtx,
						 const ImageView &input, std::vector<ToolType> &tools, const bool isPreview)
	{
		ImageView imgPrev = input;
		for (auto &tool : tools)
		{
			if (auto out = std::visit(
					[&](auto &t)
					{
						t.IsPreview = isPreview;
						return t.GPU(dev, devCtx, imgPrev);
					},
					tool);
				out.has_value())
				imgPrev = std::move(*out);
		}
		devCtx->Flush();
		return imgPrev;
	}

	static Image::ImageFile ProcessFileGpu(Dx11DevType *dev,
										   Dx11DevCtxType *devCtx,
										   const Image::ImageFile &input,
										   std::vector<ToolType> &tools, const bool isPreview)
	{
		return D3D11::CreateOutTexture(
			dev, devCtx,
			GPU(dev, devCtx,
				D3D11::LoadTextureFromFile(D3D11CSDev.Get(),
										   Image::ImageFile(input)),
				tools, isPreview));
	}
#pragma endregion ImgToolsHelper

	static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam,
								  LPARAM lParam)
	{
		if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
			return true;

		switch (msg)
		{
		case WM_CREATE:
			DragAcceptFiles(hWnd, true);
			break;
		case WM_DROPFILES:
		{
			const auto drop = reinterpret_cast<HDROP>(wParam);
			const auto count = DragQueryFile(drop, 0xFFFFFFFF, nullptr, 0);
			std::vector<std::filesystem::path> paths{};
			std::filesystem::path presetPath{};
			for (UINT i = 0; i < count; i++)
			{
				std::wstring buf(MaxPathLengthW, 0);
				DragQueryFile(drop, i, buf.data(), MaxPathLengthW);
				if (auto p = std::filesystem::path(buf.c_str()); p.extension() == L"." ItPresetExt)
				{
					presetPath = std::move(p);
				}
				else
				{
					paths.push_back(std::move(p));
				}
			}
			if (!presetPath.empty())
				Events.Emit(DragDropPresetEvent(presetPath));

			Events.Emit(DragDropFilesEvent(paths));
			DragFinish(drop);

			break;
		}
		case WM_SIZE:
			if (D3D11Dev != nullptr && wParam != SIZE_MINIMIZED)
			{
				D3D11::CleanupRenderTarget();
				D3D11SwapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam),
											  DXGI_FORMAT_UNKNOWN, 0);
				D3D11::CreateRenderTarget();
			}
			return 0;
		case WM_SYSCOMMAND:
			if ((wParam & 0xfff0) == SC_KEYMENU)
				return 0;
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			if (IsProcessing)
				ProcThread.join();
			return 0;
		default:
			break;
		}
		return DefWindowProc(hWnd, msg, wParam, lParam);
	}

#pragma region Init
public:
	ImgTools(const ImgTools &it) = delete;
	ImgTools(ImgTools &&it) = delete;

private:
	void Init()
	{
		try
		{
			InitWindow();
			InitImgui();
			InitData();
		}
		catch (...)
		{
			std::throw_with_nested(Ex(Exception, "init failed"));
		}
	}

	void InitWindow()
	{
		CreateNewConsole(1024);
		SetConsoleOutputCP(65001);
		ShowWindow(GetConsoleWindow(), showConsole);

		LogLog(NormU8(Text::NowLoading()));

#if defined(DEBUG) || defined(_DEBUG)
		LogInfo(nlohmann::json::meta().dump(4));
#endif

		ImGui_ImplWin32_EnableDpiAwareness();

		icon = static_cast<HICON>(LoadImage(GetModuleHandle(nullptr),
											MAKEINTRESOURCE(MANICON), IMAGE_ICON,
											32, 32, 0));
		if (!icon)
			LogWarn("Load ICON failed.");
		wc = {sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
			  GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
			  Config::WindowTitle, icon};
		RegisterClassEx(&wc);

		mainWnd =
			CreateWindow(wc.lpszClassName, Config::WindowTitle, WS_OVERLAPPEDWINDOW,
						 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
						 nullptr, nullptr, wc.hInstance, nullptr);

		InitDirect3D11();

		ShowWindow(mainWnd, SW_SHOWDEFAULT);
		UpdateWindow(mainWnd);
	}

	void InitDirect3D11() const
	{
		try
		{
			D3D11::CreateDeviceD3D(mainWnd);
		}
		catch (...)
		{
			D3D11::CleanupDeviceD3D();
			UnregisterClass(wc.lpszClassName, wc.hInstance);
			std::throw_with_nested(Ex(ImgToolsException, "init dx11 error"));
		}
	}

	void InitImgui()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		configPath = GetAppData() / "ImgTools";
		if (!exists(configPath))
			create_directory(configPath);

		iniPath = (configPath / "config.ini").u8string();
		if (const auto fp = std::filesystem::path(iniPath); !exists(fp))
			std::ofstream(fp).close();

		InitImguiImplIniSettings();

		ImGui::StyleColorsDark();

		ImGuiIO &io = ImGui::GetIO();
		ImFontConfig fontConfig;
		fontConfig.FontDataOwnedByAtlas = false;
		mainFontRes = {MAKEINTRESOURCE(RHR_SC_REGULAR), RT_RCDATA, "RHR_SC_REGULAR"};
		io.Fonts->AddFontFromMemoryTTF((void *)mainFontRes.Get().data(), static_cast<int>(mainFontRes.Get().size()), Config::FontSize,
									   &fontConfig,
									   io.Fonts->GetGlyphRangesChineseFull());

		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigDockingWithShift = false;
		io.ConfigWindowsResizeFromEdges = true;

		ImGui_ImplWin32_Init(mainWnd);
		ImGui_ImplDX11_Init(D3D11Dev.Get(), D3D11DevCtx.Get());
	}

	void InitImguiImplIniSettings() const
	{
		ImGuiIO &io = ImGui::GetIO();
		io.IniFilename = nullptr;
		ImGui::LoadIniSettingsFromDisk(
			reinterpret_cast<const char *>(iniPath.c_str()));
	}

	void InitData()
	{
		version = ReadVersion(RcResource(MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION, "VS_VERSION_INFO"));

		const RcResource logoRes(MAKEINTRESOURCE(LOGOFILE), RT_RCDATA, "LOGOFILE");
		logo =
			D3D11::LoadTextureFromFile(D3D11Dev.Get(), Image::ImageFile(logoRes.Get().data(), static_cast<int>(logoRes.Get().size())));

		settingsPath = (configPath / "settings.json");

		try
		{
			if (exists(settingsPath))
				settingData = nlohmann::json::parse(File::ReadAll(settingsPath));
		}
		catch (const std::exception &ex)
		{
			LogWarn("read setting from file failed: {}", ex.what());
			std::filesystem::remove(settingsPath);
		}

		if (!exists(settingsPath))
		{
			const auto lang = GetUserLanguage();
			LogLog("get lang [{}]", lang);
			if (lang.starts_with(L"zh-"))
			{
				settingData.Language = Text::Language::Chinese;
			}
			else
			{
				settingData.Language = Text::Language::English;
			}
			File::WriteAll(settingsPath, SettingData::ToJson(settingData));
		}

		Text::GlobalLanguage = settingData.Language;

		sourceDirectoryPlaceholder = String::FormatW("<{}>", NormU8(Text::SourceDirectory()));
	}
#pragma endregion Init

#pragma region Update
	void Update()
	{
		UpdateImgui();
		UpdateUi();
		UpdateRender();
		UpdateEvent();
		UpdatePreview();
	}

	void UpdatePreview()
	{
		if (!needUpdate)
			return;

		const auto t1 = std::chrono::high_resolution_clock::now();

		if (currentPreviewIdx.has_value())
		{
			if (settingData.PreviewProcessor == Processor::GPU)
			{
				previewTexture =
					GPU(D3D11Dev.Get(), D3D11DevCtx.Get(), rawTextures[*currentPreviewIdx].second, toolList, true);
				D3D11DevCtx->Flush();
			}
			else
			{
				previewTexture = D3D11::LoadTextureFromFile(
					D3D11Dev.Get(),
					ProcessFile(Image::ImageFile(rawTextures[*currentPreviewIdx].first), toolList, true));
			}
		}

		const auto t2 = std::chrono::high_resolution_clock::now();
		procTimePreUpdate =
			std::chrono::duration_cast<std::chrono::duration<float, std::micro>>(
				t2 - t1)
				.count();

		needUpdate = false;
	}

	static void UpdateImgui()
	{
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
	}

	void UpdateRender() const
	{
		ImGui::Render();

		const auto col = settingData.ClearColor;
		const float clearColorWithAlpha[4] = {col.x * col.w, col.y * col.w,
											  col.z * col.w, col.w};
		D3D11DevCtx->OMSetRenderTargets(1, D3D11MainRenderTargetView.GetAddressOf(),
										nullptr);
		D3D11DevCtx->ClearRenderTargetView(D3D11MainRenderTargetView.Get(),
										   clearColorWithAlpha);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}

		D3D11SwapChain->Present(settingData.VSync, 0);
	}

	void UpdateUi()
	{
#if defined(DEBUG) | defined(_DEBUG)
		if (showDemoWindows)
			ImGui::ShowDemoWindow(&showDemoWindows);
#endif

		ShowMainWindow();
#if 0
		ShowInfo();
#endif
		ShowSetting();
		ShowRaw();
		ShowPreview();
		ShowImageTools();
		ShowTopMenu();
	}

	void ShowMainWindow() const
	{
		const auto *viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

		ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0, 0, 0, 0));

		constexpr ImGuiWindowFlags windowFlags =
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoNavFocus |
			ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground;

		MakeStr(MainDock);

		if (ImGui::Begin(String_MainDock, nullptr, windowFlags))
		{
			const auto drawList = ImGui::GetWindowDrawList();

			const auto footerHeight = ImGui::GetTextLineHeightWithSpacing() +
									  ImGui::GetStyle().FramePadding.y;
			const auto dockSpaceSize =
				ImVec2(ImGui::GetContentRegionAvail().x,
					   ImGui::GetContentRegionAvail().y - footerHeight);

			ImGui::DockSpace(ImGui::GetID(String_MainDock), dockSpaceSize);

			const ImRect rect(ImGui::GetWindowPos() + ImVec2(0, dockSpaceSize.y),
							  ImGui::GetWindowPos() + dockSpaceSize +
								  ImVec2(0, footerHeight));
			drawList->AddRectFilled(rect.Min, rect.Max,
									ImGui::GetColorU32(ImGuiCol_MenuBarBg));
			drawList->AddLine(rect.Min, rect.Min + ImVec2(rect.GetWidth(), 0),
							  ImGui::GetColorU32(ImGuiCol_Border));

			ImGui::SetCursorPosX(8);

			ImGui::Text("Framerate: %s FPS", Convert::ToString(ImGui::GetIO().Framerate, std::chars_format::fixed, 1)->c_str());

			ImGui::SameLine();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();
			ImGui::Text("Update Time: %s ms", Convert::ToString(procTimePreUpdate / 1000.f, std::chars_format::fixed, 1)->c_str());

			ImGui::SameLine();
			ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
			ImGui::SameLine();
			if (IsProcessing)
			{
				ImGui::Text("%c %s",
							"|/-\\"[static_cast<int>(ImGui::GetTime() / 0.25 * 2.) % 4],
							curFile.Buf.c_str());
			}
		}
		ImGui::End();
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
	}

	std::vector<std::filesystem::path> ParsePathFromPaths(const std::u8string_view &buf)
	{
		return buf
			| std::views::split(u8';')
			| std::views::filter(std::ranges::size)
			| std::views::transform(To<std::filesystem::path>())
			| to_vector();
	}

	void CheckOutputPath(const std::u8string_view &buf)
	{
		CheckOutputPath(ParsePathFromPaths(buf));
	}

	std::filesystem::path GetExtension() const
	{
		return Enum::ToString(imgFormat);
	}

	void
	CheckOutputPath(const std::vector<std::filesystem::path> &buf)
	{
		if (buf.empty())
			return;

		auto output = outputPath.GetPath();

		if (buf.size() == 1)
		{
			if (const auto &head = buf[0]; exists(head))
			{
				if (output == sourceDirectoryPlaceholder)
				{
					if (is_directory(head))
					{
						outputPath = output;
					}
					else
					{
						outputPath = output.replace_filename(String::FormatW("{}.out.{}", output.stem(), GetExtension()));
					}
				}
				else if (exists(output))
				{
					if (is_regular_file(head) && is_directory(output))
					{
						output /= String::FormatW("{}.out.{}", head.stem(), GetExtension());
						outputPath = output;
					}
					else if (is_directory(head) && is_regular_file(output))
					{
						outputPath = output.remove_filename();
					}
				}
			}
		}
		else
		{
			if (exists(output) && is_regular_file(output))
				outputPath = output.remove_filename();
		}
	}

	void UpdateEvent()
	{
		Events.Emit(AlwaysEvent{});

		Events.Dispatch(Visitor
		{
			[&](DragDropFilesEvent &ev)
			{
				SetInputPath(ev.GetArg());
			},
			[&](DragDropPresetEvent &ev)
			{
				LoadPreset(ev.GetArg());
			},
			[&](SaveSettingEvent &)
			{
				File::WriteAll(settingsPath, SettingData::ToJson(settingData));
			},
			[&](StartProcessEvent &)
			{
				auto paths = ParsePathFromPaths(inputPath.GetView());
				CheckOutputPath(paths);

				const auto output = outputPath.GetPath();
				if (paths.empty())
				{
				}
				else if (paths.size() == 1)
				{
					if (const auto &path = paths[0]; is_regular_file(path))
					{
						procFiles.emplace(path, output);
					}
					else if (is_directory(path))
					{
						for (const auto &file : GetFilesFromPaths(paths))
						{
							procFiles.emplace(file, std::filesystem::path(output / file.filename()).replace_extension(GetExtension()));
						}
					}
				}
				else
				{
					for (const auto &file : GetFilesFromPaths(paths))
					{
						if (output == sourceDirectoryPlaceholder)
						{
							procFiles.emplace(file, std::filesystem::path(file).replace_filename(String::FormatW("{}.out.{}", file.stem(), GetExtension())));
						}
						else
						{
							procFiles.emplace(file, output / String::FormatW("{}.{}", file.stem(), GetExtension()));
						}
					}
				}

				IsProcessing = true;
				processedCount.store(0);
				totalCount = static_cast<int64_t>(procFiles.size());

				static const auto ProcHandle = [&](const std::stop_token &tk)
				{
					while (!procFiles.empty())
					{
						if (tk.stop_requested())
						{
							procFiles = {};
							break;
						}

						const auto& [in, outRaw] = procFiles.top();
						auto out = std::filesystem::path(outRaw);
						out.replace_extension(GetExtension());
						LogInfo(R"("{}" => "{}")", in, out);

						try
						{
							curFile = in.u8string();
							if (!exists(out.parent_path()))
								create_directories(out.parent_path());
							if (settingData.ExportProcessor == Processor::GPU)
								ProcessFileGpu(D3D11CSDev.Get(), D3D11CSDevCtx.Get(),
											   Image::ImageFile(in), toolList, false)
									.Save(out);
							else
								ProcessFile(Image::ImageFile(in), toolList, false).Save(out);
						}
						catch (const std::exception &ex)
						{
							LogErr("[ProcThread] processor error:\n{}",
								   LogMsg::LogException(ex));
						}

						++processedCount;
						procFiles.pop();
					}
					curFile.Set(NormU8(Text::Finished()));
					totalCount = 0;
					procStatus = 0.f;
					IsProcessing = false;
					Events.Emit(EndProcessEvent{});
				};

				ProcThread = std::jthread(ProcHandle);
			},
			[&](EndProcessEvent &)
			{
				ProcThread.join();
			},
			[&](AlwaysEvent &)
			{
				if (ImGui::GetIO().WantSaveIniSettings)
				{
					ImGui::SaveIniSettingsToDisk(ToImString(iniPath).c_str());
					ImGui::GetIO().WantSaveIniSettings = false;
				}
			},
			[&](LoadImageEvent& ev)
			{
				SetInputPath(ev.GetArg());
			}
		});
	}

#if 0
	void ShowInfo()
	{
		if (showInfo)
		{
			constexpr ImGuiWindowFlags windowFlags =
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
				ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
				ImGuiWindowFlags_NoMove;

			constexpr auto pad = 10.0f;
			ImVec2 pos = ImGui::GetMainViewport()->WorkPos;
			pos.x = pos.x + pad;
			pos.y = pos.y + pad;
			ImGui::SetNextWindowPos(pos, ImGuiCond_Always);

			ImGui::SetNextWindowBgAlpha(0.35f);
			if (ImGui::Begin("Info", &showInfo, windowFlags))
			{
				ImGui::Text("Framerate  : %.1f FPS", ImGui::GetIO().Framerate);
				ImGui::Text("Update Time: %.1f ms", procTimePreUpdate / 1000.f);
			}

			ImGui::End();
		}
	}
#endif

	void ShowSetting()
	{
		if (showSettings)
		{
			ImGui::Begin(Text::Setting(), &showSettings);

			bool wantToSaveSetting = false;

			static const char *lang[]{Text::English(), Text::ChineseSimplified()};
			ImGui::Combo(Text::Language_(),
						 reinterpret_cast<std::underlying_type_t<Text::Language> *>(&settingData.Language), lang, 2);
			if (ImGui::IsItemEdited())
			{
				wantToSaveSetting = true;
				Text::GlobalLanguage = settingData.Language;
			}

			ImGui::ColorEdit3(Text::BackgroundColor(),
							  reinterpret_cast<float *>(&settingData.ClearColor));
			if (ImGui::IsItemEdited())
				wantToSaveSetting = true;

			wantToSaveSetting |=
				ImGui::Checkbox(Text::VerticalSynchronization(), &settingData.VSync);

			if (settingData.VSync) ImGui::BeginDisabled();

			const auto fmt = settingData.FpsLimit > 360 ? Text::Unlocked() : "%d";
			wantToSaveSetting |= ImGui::SliderInt(Text::FpsLimit(), &settingData.FpsLimit, 10, 361, fmt);
			
			if (settingData.VSync) ImGui::EndDisabled();

			ImGui::Separator();
			ImGui::Text("%s", Text::ProcessorPreview());
			ImGui::SameLine();
			// ImGui::RadioButton(U8 "CPU##ProcessorPreview",
			//				   reinterpret_cast<int *>(&settingData.PreviewProcessor),
			//				   std::to_underlying(Processor::CPU));
			GUI::EnumRadioButton("CPU##ProcessorPreview", settingData.PreviewProcessor, Processor::CPU);
			if (ImGui::IsItemEdited())
			{
				needUpdate = true;
				wantToSaveSetting = true;
			}
			ImGui::SameLine();
			ImGui::RadioButton("GPU##ProcessorPreview",
							   reinterpret_cast<int *>(&settingData.PreviewProcessor),
							   static_cast<int>(Processor::GPU));
			if (ImGui::IsItemEdited())
			{
				needUpdate = true;
				wantToSaveSetting = true;
			}

			ImGui::Separator();
			ImGui::Text("%s", Text::ProcessorExport());
			ImGui::SameLine();
			ImGui::RadioButton(U8 "CPU##ProcessorExport",
							   reinterpret_cast<int *>(&settingData.ExportProcessor),
							   static_cast<int>(Processor::CPU));
			if (ImGui::IsItemEdited())
				wantToSaveSetting = true;
			ImGui::SameLine();
			ImGui::RadioButton(U8 "GPU##ProcessorExport",
							   reinterpret_cast<int *>(&settingData.ExportProcessor),
							   static_cast<int>(Processor::GPU));
			if (ImGui::IsItemEdited())
				wantToSaveSetting = true;

			if (ImGui::Button(Text::ResetSettings()))
			{
				settingData = {};
				needUpdate = true;
				wantToSaveSetting = true;
			}

			if (wantToSaveSetting)
				Events.Emit(SaveSettingEvent{});

			ImGui::End();
		}
	}

	void ShowRaw()
	{
		if (showRaw)
		{
			ImGui::Begin(Text::RawImage(), &showRaw);

			if (const auto &[x, y] = ImGui::GetContentRegionAvail(); x == 0 || y == 0)
				ImGui::SetWindowSize(ImVec2(400, 400));

			if (ImGui::BeginChild("RawList", ImVec2(0, 0), true))
			{
				const auto size = ImGui::GetContentRegionAvail();

				const auto isProcessing = IsProcessing;

				if (isProcessing) ImGui::BeginDisabled();
				for (size_t i = 0; i < rawTextures.size(); ++i)
				{
					const auto &[path, tex] = rawTextures[i];
					ImGui::PushID(static_cast<int>(i));

					const auto destW = size.x - ImGui::GetStyle().FramePadding.x * 2.f;
					const auto destH = static_cast<float>(tex.Height) * destW / static_cast<float>(tex.Width);

					if (ImGui::ImageButton(tex.SRV.Get(), ImVec2(destW, destH)))
					{
						currentPreviewIdx = i;
						needUpdate = true;
					}

					ImGui::PopID();
				}
				if (isProcessing) ImGui::EndDisabled();
			}

			ImGui::EndChild();

#if 0
			const auto imgW = rawTexture.Width;
			const auto imgH = rawTexture.Height;
			ImGui::Text("%d x %d", imgW, imgH);
			if (rawTexture.Height && previewTexture.Height)
			{
				const auto size = ImGui::GetContentRegionAvail();
				auto h = size.y;
				auto w = h * imgW / imgH;
				if (w > size.x)
				{
					w = size.x;
					h = w * imgH / imgW;
				}
				if (w == 0 || h == 0)
					ImGui::SetWindowSize(ImVec2(400, 400));
				ImGui::Image(rawTexture.SRV.Get(), ImVec2(w, h));
			}
#endif
			ImGui::End();
		}
	}

	void ShowPreview()
	{
		if (showPreview)
		{
			ImGui::Begin(Text::Preview(), &showPreview);

			if (currentPreviewIdx.has_value())
			{
				const auto imgW = previewTexture.Width;
				const auto imgH = previewTexture.Height;

				const auto &[path, tex] = rawTextures[currentPreviewIdx.value()];
				ImGui::Text("%s", ToImString(path).c_str());
				ImGui::Text("%d x %d => %d x %d", tex.Width, tex.Height, imgW, imgH);

				if (previewTexture.Height && previewTexture.Width)
				{
					const auto size = ImGui::GetContentRegionAvail();
					auto h = size.y;
					auto w = h * static_cast<float>(imgW) / static_cast<float>(imgH);
					if (w > size.x)
					{
						w = size.x;
						h = w * static_cast<float>(imgH) / static_cast<float>(imgW);
					}
					if (w <= 0.0001f || h <= 0.0001f)
						ImGui::SetWindowSize(ImVec2(400, 400));
					ImGui::Image(previewTexture.SRV.Get(), ImVec2(w, h));
				}
			}

			ImGui::End();
		}
	}

#pragma region UpdateImageTools
	template <typename T>
	void ShowImageToolsImplAddImplSelectImplSingle(std::vector<ToolType> &list, bool &update)
	{
		if (ImGui::Selectable(T::Name()))
		{
			list.push_back(T{});
			update = true;
		}
	}

	template <typename... Args>
	void ShowImageToolsImplAddImplSelect(std::vector<ToolType> &list, bool &update)
	{
		(ShowImageToolsImplAddImplSelectImplSingle<Args>(list, update), ...);
	}

	template <typename T>
	void LoadPresetImplIdMatcherImplMatch(std::vector<ToolType> &list, const std::string_view &id, bool &update)
	{
		if (!update && id == T::Id())
		{
			list.push_back(T{});
			update = true;
		}
	}

	template <typename... Args>
	void LoadPresetImplIdMatcher(std::vector<ToolType> &list, const std::string_view &id)
	{
		bool updated = false;
		(LoadPresetImplIdMatcherImplMatch<Args>(list, id, updated), ...);

		if (!updated)
			throw Ex(ImgToolsException, "unknown preset id: {}", id);
	}

	void ShowImageTools()
	{
		if (showTools)
		{
			ImGui::Begin(Text::Tools(), &showTools);

			ShowImageToolsImplInput();
			ShowImageToolsImplOutput();

			ImGui::Separator();
			if (ImGui::BeginTabBar("Processor",
								   ImGuiTabBarFlags_AutoSelectNewTabs |
									   ImGuiTabBarFlags_Reorderable |
									   ImGuiTabBarFlags_FittingPolicyResizeDown))
			{
				ShowImageToolsImplExport();
				ShowImageToolsImplAdd();
				ShowImageToolsImplTab();

				ImGui::EndTabBar();
			}

			static bool dg = false;
			if (ImGui::IsMouseDragging(0) && !dg)
				dg = true;

			if (dg && !ImGui::IsMouseDragging(0))
			{
				dg = false;

				const auto g = ImGui::GetCurrentContext();
				const auto window = g->CurrentWindow;
				const auto id = window->GetID("Processor");
				const auto tabBar = g->TabBars.GetOrAddByKey(id);
				std::unordered_map<uint64_t, uint64_t> mapped{};
				for (auto &t : tabBar->Tabs)
				{
					std::u8string_view tabName(reinterpret_cast<const char8_t *>(tabBar->GetTabName(&t)));
					if (const auto pos = tabName.find(u8"###"); pos != std::u8string_view::npos)
					{
						const auto tabIdStr = tabName.substr(pos + 3);
						const auto tabId = Convert::FromString<uint64_t>(std::string_view(reinterpret_cast<const char *>(tabIdStr.data()), tabIdStr.length())).value();
						mapped.emplace(tabId, tabBar->GetTabOrder(&t));
					}
				}

				std::vector<ToolType> tools(toolList.size());

				for (auto &i : toolList)
				{
					tools[mapped.at(std::visit([](const auto &t)
											   { return t.GlobalId; },
											   i))] = i;
				}

				toolList = tools;
				needUpdate = true;
			}

			ImGui::End();
		}
	}

	void ShowImageToolsImplInput()
	{
		ImGui::InputText(Text::InputPath(), &inputPath.Buf);
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			Events.Emit(LoadImageEvent(ParsePathFromPaths(inputPath.GetView())));
		}
		ImGui::SameLine();
		if (ImGui::Button(reinterpret_cast<const char *>(
				String::FormatU8("{}##input{}", NormU8(Text::SelectSomething()),
								 __FUNCTION__)
					.c_str())))
		{
			auto paths = ParsePathFromPaths(Pick::PickFileAndFolder({ .Flags = OFN_ENABLEHOOK | OFN_EXPLORER | OFN_NOVALIDATE | OFN_ALLOWMULTISELECT }).u8string());
			if (paths.size() > 1)
			{
				const auto basePath = paths[0].parent_path();
				for (size_t i = 1; i < paths.size(); ++i)
				{
					paths[i] = basePath / paths[i];
				}
			}
			Events.Emit(LoadImageEvent(std::move(paths)));
		}
	}

	static bool IsImage(const std::filesystem::path &file)
	{
		static std::unordered_set<std::string> extensions{
			".jpg", ".jpeg", ".jpe", ".png", ".tga", ".bmp",
			".psd", ".gif", ".hdr", ".pic", ".ppm", ".pgm"};
		return extensions.contains(String::ToLower(file.extension().u8string()));
	}

	//void SetInputPath(const std::filesystem::path &buf)
	//{
	//	SetInputPathImpl(ParsePathFromPaths(buf.u8string()));
	//}
	//
	//void SetInputPath(const std::vector<std::filesystem::path> &buf)
	//{
	//	SetInputPathImpl(buf);
	//}

	void SetInputPath(const std::vector<std::filesystem::path> &paths)
	{
		if (paths.empty())
			return;

		currentPreviewIdx.reset();
		rawTextures.clear();
		rawTextures.shrink_to_fit();
		D3D11DevCtx->Flush();

		inputPath = JoinPaths(paths);

		if (paths.size() == 1)
		{
			const std::filesystem::path& path = paths[0];
			if (!exists(path))
				return;

			if (is_directory(path))
			{
				outputPath = path.parent_path() / String::FormatW("{}.out", path.filename());
			}
			else
			{
				outputPath = path.parent_path() / String::FormatW("{}.out.{}", path.stem(), GetExtension());
			}
		}
		else
		{
			outputPath = sourceDirectoryPlaceholder;
		}

		auto files = GetFilesFromPaths(paths) | to_vector();

		std::ranges::sort(files);

		for (const auto &file : files)
		{
			if (IsImage(file))
			{
				try
				{
					rawTextures.emplace_back(file, D3D11::LoadTextureFromFile(D3D11Dev.Get(), Image::ImageFile(file)));
				}
				catch (const std::exception &ex)
				{
					LogErr(ex.what());
					GUI::ShowError(String::FormatW("{}: {}", file, ex.what()), mainWnd);
				}
			}
			else
			{
				LogWarn("ignore: {}", file);
			}
		}

		if (!rawTextures.empty())
			currentPreviewIdx = 0;

		needUpdate = true;
	}

	void ReSetOutputPathExtension()
	{
		if (const auto p = inputPath.GetPath(); exists(p) && is_regular_file(p))
			outputPath = outputPath.GetPath().replace_extension(GetExtension());
	}

	void ShowImageToolsImplOutput()
	{
		ImGui::InputText(Text::OutputPath(), &outputPath.Buf);
		ImGui::SameLine();
		if (ImGui::Button(reinterpret_cast<const char *>(
				String::FormatU8("{}##output", NormU8(Text::SelectSomething()))
					.c_str())))
		{
			if (const auto buf = Pick::PickFileAndFolder({}); !buf.empty())
				outputPath = buf;
			ReSetOutputPathExtension();
		}
	}

	void ShowImageToolsImplExport()
	{
		if (ImGui::BeginTabItem(
				Text::Export(), nullptr,
				ImGuiTabItemFlags_Trailing |
					ImGuiTabItemFlags_NoCloseWithMiddleMouseButton))
		{
			ImGui::Text("%s", curFile.Buf.c_str());
			if (totalCount)
				procStatus = static_cast<float>(processedCount.load()) / static_cast<float>(totalCount);
			ImGui::ProgressBar(procStatus);

			ImGui::BeginDisabled(IsProcessing);
			{
				if (GUI::EnumCombo(Text::Format(), imgFormat))
					ReSetOutputPathExtension();
			}
			ImGui::EndDisabled();

			if (!IsProcessing && ImGui::Button(Text::Start()))
			{
				Events.Emit(StartProcessEvent{});
			}

			if (IsProcessing && ImGui::Button(Text::Cancel()))
				ProcThread.request_stop();

			ImGui::EndTabItem();
		}
	}

	void ShowImageToolsImplAdd()
	{
		if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing |
										  ImGuiTabItemFlags_NoTooltip))
		{
			ImGui::OpenPopup("AddMenu");
		}

		if (ImGui::BeginPopup("AddMenu"))
		{
			ShowImageToolsImplAddImplSelect<ItToolList>(toolList, needUpdate);

			ImGui::EndPopup();
		}
	}

	void ShowImageToolsImplTab()
	{
		for (std::size_t i = 0; i < toolList.size();)
		{
			bool open = true;
			std::visit(
				[&](auto &x)
				{
					if (x.GlobalId == 0)
						x.GlobalId = CurrentGlobalId();

					const auto label = std::format("{}. {}###{}", i + 1, x.Name(), x.GlobalId);
					if (ImGui::BeginTabItem(label.c_str(), &open))
					{
						const auto ps = IsProcessing;
						if (ps)
							ImGui::BeginDisabled();
						try
						{
							x.UI(needUpdate);
						}
						catch (...)
						{
							std::rethrow_if_nested(Ex(
								ImgToolsException,
								R"(render tool[index = {}, name = "{}"] error)", i, x.Name()));
						}

						if (ps)
							ImGui::EndDisabled();

						ImGui::EndTabItem();
					}
				},
				toolList[i]);

			if (!open)
			{
				toolList.erase(toolList.begin() + static_cast<decltype(toolList.begin())::difference_type>(i));
				needUpdate = true;
			}
			else
			{
				i++;
			}
		}
	}
#pragma endregion UpdateImageTools

#pragma region UpdateTopMenu
	void ShowTopMenu()
	{
		if (ImGui::BeginMainMenuBar())
		{
			ShowTopMenuImplFile();
			ShowTopMenuImplWindow();
			ShowTopMenuImplHelp();

			ImGui::EndMainMenuBar();
		}
	}

	MakeStr(id);
	MakeStr(value);
	MakeStr(ver);

	void LoadPreset(const std::filesystem::path &path)
	{
		try
		{
			const auto data = nlohmann::json::parse(File::ReadAll(path));
			const auto &tools = data[String_data];

			std::vector<ToolType> list;
			for (const auto &tool : tools)
			{
				const auto id = tool[String_id].get<std::string>();
				const auto &val = tool[String_value];

				LoadPresetImplIdMatcher<ItToolList>(list, id);
				std::visit([&](auto &x)
						   { x.LoadData(val); },
						   *list.rbegin());
			}

			toolList.clear();
			toolList.shrink_to_fit();
			toolList = list;
			needUpdate = true;
		}
		catch (const std::exception &ex)
		{
			LogErr(ex.what());
			GUI::ShowError(String::ToWString(ex.what()));
		}
	}

	void SavePreset(const std::filesystem::path &path) const
	{
		std::vector<nlohmann::json> tools{};
		for (const auto &i : toolList)
		{
			auto id = std::visit([](const auto &x)
								 { return x.Id(); },
								 i);
			auto tool = nlohmann::json::object({{String_id, id}});
			std::visit([&](const auto &x)
					   { tool[String_value] = x.SaveData(); },
					   i);
			tools.push_back(tool);
		}
		File::WriteAll(
			path,
			nlohmann::json::object({{String_ver, version}, {String_data, tools}}).dump(4));
	}

	void ShowTopMenuImplFile()
	{
		if (ImGui::BeginMenu(Text::File()))
		{
			if (ImGui::MenuItem(Text::OpenPreset(), "CTRL+O"))
			{
				if (const auto p = Pick::PickFile({Config::ItPresetFilter,
												  Text::ToWString(Text::OpenPreset()), nullptr, OFN_FILEMUSTEXIST});
					!p.empty())
				{
					if (exists(p))
					{
						LoadPreset(p);
						needUpdate = true;
					}
					else
					{
						LogErr("file '{}' not found", p);
						MessageBox(mainWnd,
								   String::FormatW(Text::File_NotFound(), p).c_str(),
								   String::ToWString(NormU8(Text::Error())).c_str(),
								   MB_ICONSTOP | MB_TASKMODAL);
					}
				}
			}

			if (ImGui::MenuItem(Text::SavePreset(), "CTRL+S"))
			{
				if (const auto p = Pick::SaveFile({ Config::ItPresetFilter,
												  Text::ToWString(Text::OpenPreset()) });
					!p.empty())
				{
					const auto pp = p.parent_path();
					const auto ps = p.stem();
					SavePreset(pp / String::FormatW("{}." ItPresetExt, ps));
				}
			}

			ImGui::Separator();
			if (ImGui::MenuItem(Text::Settings(), "CTRL+,", showSettings))
				showSettings = !showSettings;

			ImGui::Separator();
			if (ImGui::MenuItem(Text::Exit(), "ALT+F4"))
				done = true;

			ImGui::EndMenu();
		}
	}

	void ShowTopMenuImplWindow()
	{
		if (ImGui::BeginMenu(Text::Window()))
		{
			if (ImGui::MenuItem(Text::Tools(), nullptr, showTools))
				showTools = !showTools;

			if (ImGui::MenuItem(Text::RawImage(), nullptr, showRaw))
				showRaw = !showRaw;

			if (ImGui::MenuItem(Text::Preview(), nullptr, showPreview))
				showPreview = !showPreview;

			if (ImGui::MenuItem(Text::Console(), nullptr, showConsole))
			{
				showConsole = !showConsole;
				ShowWindow(GetConsoleWindow(), showConsole);
			}

			ImGui::EndMenu();
		}
	}

	void ShowTopMenuImplHelp()
	{
		if (ImGui::BeginMenu(Text::About()))
		{
#if 0
			if (ImGui::MenuItem(Text::Document(), nullptr, showDocument))
				showDocument = !showDocument;
#endif
			if (ImGui::MenuItem(Text::License(), nullptr, showLicense))
				showLicense = !showLicense;
			if (ImGui::MenuItem(Text::Changelog(), nullptr, showChangelog))
				showChangelog = !showChangelog;
			if (ImGui::MenuItem(Text::About(), nullptr, showAbout))
				showAbout = !showAbout;

			ImGui::EndMenu();
		}
		ShowTopMenuImplHelpImplDocument();
		ShowTopMenuImplHelpImplLicense();
		ShowTopMenuImplHelpImplChangelog();
		ShowTopMenuImplHelpImplAbout();
	}

	void ShowTopMenuImplHelpImplLicense()
	{
		if (!showLicense)
			return;

		ImGui::Begin(Text::License(), &showLicense);

		GUI::RawTextU8(LICENSE_txt);

		ImGui::End();
	}

	void ShowTopMenuImplHelpImplChangelog()
	{
		if (!showChangelog)
			return;

		ImGui::Begin(Text::Changelog(), &showChangelog);

		GUI::RawTextU8(Changelog);

		ImGui::End();
	}

	void ShowTopMenuImplHelpImplAbout()
	{
		if (!showAbout)
			return;

		ImGui::Begin(Text::About(), &showAbout);

		ImGui::Image(logo.SRV.Get(), ImVec2(static_cast<float>(logo.Height), static_cast<float>(logo.Width)));

		ImGui::Text(R"(ImgTools)");
		ImGui::Text(R"()");
		ImGui::Text(R"(Copyright (c) 2022 iriszero(ih@iriszero.cc))");
		ImGui::Text(R"(Version %d.%d.%d.%d, Build %s %s)", version[0], version[1],
					version[2], version[3], __DATE__, __TIME__);

		ImGui::End();
	}

	void ShowTopMenuImplHelpImplDocument()
	{
		if (!showDocument)
			return;

		ImGui::Begin(Text::Document(), &showDocument);
		ImGui::Text(R"(
Helper

no help!)");
		ImGui::End();
	}
#pragma endregion UpdateTopMenu

#pragma endregion Update

public:
	ImgTools() { Init(); }

	void Run()
	{
		decltype(std::chrono::high_resolution_clock::now()) lastFrameTime{};
		while (!done)
		{
			const auto unlocked = settingData.FpsLimit > 360;
			if (!unlocked) lastFrameTime = std::chrono::high_resolution_clock::now();

			MSG msg;
			while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				if (msg.message == WM_QUIT)
					done = true;
			}
			if (done)
				break;

			Update();

			if (!unlocked) 
				if (const auto exp = lastFrameTime + std::chrono::milliseconds(static_cast<int64_t>(1. / settingData.FpsLimit * 1000.));
					exp > std::chrono::high_resolution_clock::now()) std::this_thread::sleep_until(exp);
		}

		std::error_code ec;
		remove_all(Config::TmpDir, ec);
		if (ec)
			throw Ex(ImgToolsException, "remove_all: {}", ec.message());
	}

	~ImgTools()
	{
		ImGui_ImplDX11_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		D3D11::CleanupDeviceD3D();
		DestroyWindow(mainWnd);
		UnregisterClass(wc.lpszClassName, wc.hInstance);
	}
};

static SingleInstance AppInstance{};

int WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPSTR lpCmdLine,
	_In_ int nShowCmd)
{
	if (!AppInstance.Ok())
	{
		GUI::ShowError(L"Already running.", nullptr);
		exit(EXIT_FAILURE);
	}
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	std::thread logThread(LogHandle);

#if !(defined(DEBUG) || defined(_DEBUG))
	try
#endif
	{
		ImgTools{}.Run();
		LogNone("See you next time.");
	}
#if !(defined(DEBUG) || defined(_DEBUG))
	catch (const std::exception &e)
	{
		LogErr("Run error:\n{}", LogMsg::LogException(e));
		LogNone("Opss.");
		ShowWindow(GetConsoleWindow(), true);
		system("pause");
	}
#endif
	logThread.join();
	return 0;
}
