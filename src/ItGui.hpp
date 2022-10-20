#pragma once

#include <Windows.h>

#include <imgui.h>
#include <imgui_internal.h>

#include "ItUtility.hpp"
#include "ItText.hpp"
#include "ItException.hpp"

#include "String/String.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace Pick
{
    using HookParam = std::tuple<std::wstring, HWND>;
#define CaseErr(ce) \
    case ce:        \
        throw Ex(WinApiException, "GetOpenFileName: " #ce)
#define ThrowLastError()\
    switch (CommDlgExtendedError())\
    {\
        CaseErr(CDERR_DIALOGFAILURE);\
        CaseErr(CDERR_FINDRESFAILURE);\
        CaseErr(CDERR_INITIALIZATION);\
        CaseErr(CDERR_LOADRESFAILURE);\
        CaseErr(CDERR_LOADSTRFAILURE);\
        CaseErr(CDERR_LOCKRESFAILURE);\
        CaseErr(CDERR_MEMALLOCFAILURE);\
        CaseErr(CDERR_MEMLOCKFAILURE);\
        CaseErr(CDERR_NOHINSTANCE);\
        CaseErr(CDERR_NOHOOK);\
        CaseErr(CDERR_NOTEMPLATE);\
        CaseErr(CDERR_STRUCTSIZE);\
        CaseErr(FNERR_BUFFERTOOSMALL);\
        CaseErr(FNERR_INVALIDFILENAME);\
        CaseErr(FNERR_SUBCLASSFAILURE);\
            default: break;\
    }

    struct Params
    {
        std::wstring_view Filter = L"Any File\0*\0";
        std::wstring_view Title = L"Select...";
        LPOFNHOOKPROC Hook = nullptr;
        DWORD Flags = 0;
    };

    template <bool IsSaveDlg = false>
    std::filesystem::path PickBase(const Params& params)
    {
        const auto filename = std::make_unique<wchar_t[]>(MaxPathLengthW);

        OPENFILENAME ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = nullptr;
        ofn.lpstrFilter = params.Filter.data();
        ofn.lpstrFile = filename.get();
        ofn.nMaxFile = MaxPathLengthW;
        ofn.lpstrTitle = params.Title.data();
        ofn.lpfnHook = params.Hook;
        ofn.Flags = params.Flags;

        if constexpr (IsSaveDlg)
        {
	        if (GetSaveFileName(&ofn))
                return filename.get();
        }
        else
        {
            if (GetOpenFileName(&ofn))
                return filename.get();
        }

        ThrowLastError();
        return {};
#undef CaseErr
    }

    struct PickFileAndFolderParams
    {
        std::wstring_view Filter = L"Any File\0*\0";
        std::wstring_view Title = L"Select...";
        DWORD Flags = OFN_ENABLEHOOK | OFN_EXPLORER | OFN_NOVALIDATE;
    };

    inline std::filesystem::path PickFileAndFolder(const PickFileAndFolderParams& params)
    {
        return PickBase<false>(
            {
	            params.Filter, params.Title,
	            [](HWND curWnd, UINT message, WPARAM, LPARAM lParam) -> UINT_PTR
	            {
	                if (message == WM_NOTIFY)
	                {
	                    if ((&reinterpret_cast<OFNOTIFY*>(lParam)->hdr)->code == CDN_SELCHANGE)
	                    {
	                        const auto parentWnd = GetParent(curWnd);
	                        auto param = HookParam(L"FolderView", nullptr);

	                        EnumChildWindows(
	                            parentWnd,
	                            [](HWND childWnd, LPARAM lParam) -> BOOL
	                            {
	                                auto& [name, wnd] = *reinterpret_cast<HookParam*>(lParam);
	                                const auto nameLen = GetWindowTextLength(childWnd);

	                                if (static_cast<std::size_t>(nameLen) != name.length())
	                                    return true;

	                                std::wstring buf(nameLen, 0);
	                                GetWindowText(childWnd, buf.data(), nameLen + 1);

	                                if (name == buf)
	                                {
	                                    wnd = childWnd;
	                                    return false;
	                                }

	                                return true;
	                            },
	                            reinterpret_cast<LPARAM>(&param));

	                        if (auto& [_, viewWnd] = param; std::get<1>(param))
	                        {
	                            std::wstring res{};

	                            std::vector<std::wstring> buffs{};
	                            int index = -1;
	                            while (-1 != (index = ListView_GetNextItem(
	                                              viewWnd, index, LVNI_ALL | LVNI_SELECTED)))
	                            {
	                                std::vector<wchar_t> buf(MaxPathLengthW, 0);
	                                ListView_GetItemText(viewWnd, index, 0, buf.data(), static_cast<int>(buf.size()));
	                                buffs.emplace_back(buf.data());
	                            }

	                            if (buffs.empty())
	                            {
	                            }
	                            else if (buffs.size() == 1)
	                            {
	                                res = buffs[0];
	                            }
	                            else
	                            {
	                                for (const auto& buf : buffs)
	                                {
	                                    res.append(buf);
	                                    res.append(1, L';');
	                                }
	                            }
	                            CommDlg_OpenSave_SetControlText(parentWnd, edt1, res.c_str());
	                        }
	                    }
	                }
	                return 0;
	            },
	            params.Flags
            });
    }

    inline std::filesystem::path PickFile(const Params& params)
    {
        return PickBase<>(params);
    }

    inline std::filesystem::path SaveFile(const Params& params)
    {
        return PickBase<true>(params);
    }
} // namespace Pick

namespace GUI
{
#define MacroToString(x) #x
#define MacroLine MacroToString(__LINE__)
#define AutoMark(cs) std::format("{}##" MacroLine, cs).c_str()

    inline void ShowError(
        const std::filesystem::path &msg, const std::filesystem::path &title, const HWND wnd = nullptr)
    {
        MessageBox(wnd, msg.native().data(), title.native().data(), MB_ICONSTOP | MB_TASKMODAL);
    }

    inline void ShowError(
        const std::filesystem::path &msg, const HWND wnd = nullptr)
    {
        ShowError(msg, NormU8(Text::Error()), wnd);
    }

    inline void RawTextU8(const std::u8string_view &str)
    {
        const auto *ptr = reinterpret_cast<const char *>(str.data());
        ImGui::TextUnformatted(ptr, ptr + str.size());
    }

    template <size_t Size>
    void RawTextU8(const uint8_t (&data)[Size])
    {
        RawTextU8({reinterpret_cast<const char8_t *>(data), Size});
    }

    //#define EnumCombe_(str, value, enumName) ImGui::Combo(str, reinterpret_cast<int *>(&value),                           \
//                                                     Func::Array(Enum##enumName())                                   \
//                                                         .Map([](const auto &x) { return EnumToString(x).c_str(); }) \
//                                                         .ToVector()                                                 \
//                                                         .data(),                                                    \
//                                                     (int)Enum##enumName().size())

    template <typename EnumType>
    bool EnumCombo(const char *str, EnumType &enumVal)
    {
        bool changed = false;

        if (ImGui::BeginCombo(str, std::string(Enum::ToString(enumVal)).c_str()))
        {
            for (auto v : Enum::Values<EnumType>())
            {
                const auto selected = (enumVal == v);
                if (ImGui::Selectable(std::string(Enum::ToString(v)).c_str(), selected))
                {
                    enumVal = v;
                    changed = true;
                }

                if (selected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }

    inline void DoubleClickToEdit()
    {
        if (ImGui::IsItemActivated() && ImGui::IsMouseDoubleClicked(0))
            ImGui::SetKeyboardFocusHere(-1);
    }

    template <typename T>
    bool EnumRadioButton(const char* str, T &enumVal, const T currentVal)
    {
        return ImGui::RadioButton(str, reinterpret_cast<std::underlying_type_t<T> *>(&enumVal), std::to_underlying(currentVal));
    }
} // namespace GUI
