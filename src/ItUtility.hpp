#pragma once

#include <array>
#include <coroutine>
#include <filesystem>
#include <iostream>
#include <queue>
#include <random>
#include <ranges>
#include <span>
#include <string>
#include <variant>

#include <ShlObj.h>

#undef max
#undef min
#include <range/v3/all.hpp>

#include "Enum/Enum.hpp"
#include "String/String.hpp"

#include "Image.hpp"

#include "ItException.hpp"

#pragma region Helper
#define MakeStr(str) static constexpr auto String_##str = #str

[[maybe_unused]] static constexpr uint32_t MaxPathLengthW = 32767;
[[maybe_unused]] static constexpr uint32_t MaxPathLength8 = MaxPathLengthW * 3;

MakeEnum(Processor, CPU, GPU);
MakeEnum(ImageFormat, jpg, png, bmp, tga);

template <class... T>
struct Visitor : T...
{
    using T::operator()...;
};

template <typename... Args>
struct ToolListTypes
{
    using ToolType = std::variant<Args...>;
    using ProcessorType = std::variant<typename Args::ProcessorType...>;
};

struct U8String
{
    std::string Buf{};

    U8String() = default;

    explicit U8String(std::string buf) : Buf(std::move(buf)) {}

    [[nodiscard]] std::u8string_view GetView() const
    {
        return {reinterpret_cast<const char8_t *>(Buf.data()), Length()};
    }

    [[nodiscard]] std::filesystem::path GetPath() const { return GetView(); }
    [[nodiscard]] size_t Length() const { return Buf.length(); }
    [[nodiscard]] bool Empty() const { return Buf.empty(); }

    void Clear() { Buf.clear(); }

    void Set(const std::u8string_view &buf)
    {
        Buf = std::string_view(reinterpret_cast<const char *>(buf.data()),
                               buf.size());
    }

    friend bool operator==(const U8String &s1, const U8String &s2)
    {
        return s1.Buf == s2.Buf;
    }

    U8String &operator=(const std::filesystem::path &buf)
    {
        Set(buf.u8string());
        return *this;
    }
};

class SingleInstance
{
    HANDLE mutex{};

public:
    SingleInstance() = default;
    SingleInstance(const SingleInstance &) = delete;
    SingleInstance(SingleInstance &&) = delete;

    SingleInstance &operator=(const SingleInstance &) = delete;
    SingleInstance &operator=(SingleInstance &&) = delete;

    bool Ok()
    {
        mutex = CreateMutex(
            nullptr, true,
            LR"(=Zz,EKn@O8-GJ(lO$l^6IXWGMGrzU]3QaJ-Itcx2ODg.=0~!FcItcx2ODg.=2v=IF)");
        return GetLastError() != ERROR_ALREADY_EXISTS;
    }

    ~SingleInstance()
    {
        if (mutex)
            CloseHandle(mutex);
    }
};

class RcResource
{
    HGLOBAL res = nullptr;
    DWORD resSize = 0;

    const uint8_t *ptr = nullptr;

    std::string tip{};

public:
    RcResource() = default;

    RcResource(const wchar_t *name, const wchar_t *type, const std::string &tip = {})
        : tip(tip.empty() ? std::format("{:#x}", reinterpret_cast<uint64_t>(name)) : tip)
    {
        const auto mod = GetModuleHandle(nullptr);
        const HRSRC src = FindResource(mod, name, type);
        if (src == nullptr)
            throw Ex(WinApiException, "FindResource: {}: nullptr", tip);

        resSize = SizeofResource(mod, src);
        res = LoadResource(mod, src);
        if (res == nullptr)
            throw Ex(WinApiException, "LoadResource: {}: nullptr", tip);

        ptr = static_cast<const uint8_t *>(LockResource(res));
        if (ptr == nullptr)
            throw Ex(WinApiException, "LockResource: {}: nullptr", tip);
    }

    std::span<const uint8_t> Get() const
    {
        return {ptr, resSize};
    }

    std::string_view GetString() const
    {
        return {reinterpret_cast<const char *>(ptr), resSize};
    }

    const std::string &GetTip() const
    {
        return tip;
    }

    ~RcResource()
    {
        FreeResource(res);
    }
};

template <typename T>
struct Sequence
{
    struct promise_type
    {
        T Value{};
        std::exception_ptr Except{};

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() { Except = std::current_exception(); }
        void return_void() {}

        Sequence get_return_object()
        {
            return Sequence(handle_type::from_promise(*this));
        }

        template <std::convertible_to<T> ValueType>
        std::suspend_always yield_value(ValueType &&val)
        {
            Value = std::forward<ValueType>(val);
            return {};
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    handle_type Handle;

    explicit Sequence(handle_type h) : Handle(h) {}
    Sequence(const Sequence &) = delete;
    ~Sequence() { Handle.destroy(); }
    Sequence &operator=(const Sequence &) = delete;

    Sequence(Sequence &&gen)
    {
        Handle = gen.Handle;
        hasValue = gen.hasValue;
    }

    Sequence &operator=(Sequence &&gen)
    {
        this->Handle = std::move(gen.Handle);
        this->hasValue = gen.hasValue;
        return *this;
    }

    explicit operator bool()
    {
        Cache();
        return !Handle.done();
    }

    T operator()()
    {
        Cache();
        hasValue = false;
        return std::move(Handle.promise().Value);
    }

private:
    bool hasValue = false;

    void Cache()
    {
        if (!hasValue)
        {
            Handle();
            if (Handle.promise().Except)
                std::rethrow_exception(Handle.promise().Except);

            hasValue = true;
        }
    }
};

template <std::movable T>
class Generator
{
public:
    struct promise_type
    {
        Generator<T> get_return_object()
        {
            return Generator{Handle::from_promise(*this)};
        }
        static std::suspend_always initial_suspend() noexcept
        {
            return {};
        }
        static std::suspend_always final_suspend() noexcept
        {
            return {};
        }

        template <std::convertible_to<T> ValueType>
        std::suspend_always yield_value(ValueType &&value) noexcept
        {
            current_value = std::move(value);
            return {};
        }
        void await_transform() = delete;
        [[noreturn]] static void unhandled_exception() { throw; }
        void return_void()
        {
            current_value.reset();
        }

        std::optional<T> current_value{};
    };

    using Handle = std::coroutine_handle<promise_type>;

    explicit Generator(const Handle handle) : m_coroutine{handle}
    {
    }

    Generator() = default;
    ~Generator()
    {
        if (m_coroutine)
        {
            m_coroutine.destroy();
        }
    }

    Generator(const Generator &) = delete;
    Generator &operator=(const Generator &) = delete;

    Generator(Generator &&other) noexcept : m_coroutine{other.m_coroutine}
    {
        other.m_coroutine = {};
    }
    Generator &operator=(Generator &&other) noexcept
    {
        if (this != &other)
        {
            if (m_coroutine)
            {
                m_coroutine.destroy();
            }
            m_coroutine = other.m_coroutine;
            other.m_coroutine = {};
        }
        return *this;
    }

    class Iter
    {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::int64_t;
        using pointer = const T *;
        using reference = const T &;

        Iter &operator++()
        {
            m_coroutine.resume();
            return *this;
        }

        Iter operator++(int)
        {
            Iter value{*this};
            ++(*this);
            return value;
        }

        const T &operator*() const
        {
            return *m_coroutine.promise().current_value;
        }

        bool operator==(std::default_sentinel_t) const
        {
            return !m_coroutine || !m_coroutine.promise().current_value.has_value();
        }

        bool operator==(const Iter &it) const
        {
            if (it.m_coroutine)
            {
                return m_coroutine.promise().current_value == it.m_coroutine.promise().current_value;
            }

            return !m_coroutine.promise().current_value.has_value();
        }

        bool operator!=(const Iter &it) const
        {
            return !operator==(it);
        }

        Iter() = default;
        explicit Iter(const Handle coroutine) : m_coroutine{coroutine} {}

    private:
        Handle m_coroutine{};
    };

    Iter begin()
    {
        if (m_coroutine)
        {
            m_coroutine.resume();
        }
        return Iter{m_coroutine};
    }

    Iter end()
    {
        return {};
    }

private:
    Handle m_coroutine;
};

const std::filesystem::path &GetAppData()
{
    static const auto Path = []()
    {
        wchar_t *buf;
        SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &buf);
        return std::filesystem::path(buf);
    }();
    return Path;
}

inline bool IsFilePath(const U8String &path)
{
    return std::filesystem::is_regular_file(path.GetView());
}

inline bool IsFolderPath(const U8String &path)
{
    return std::filesystem::is_directory(path.GetView());
}

inline bool IsExist(const U8String &path)
{
    return std::filesystem::exists(path.GetView());
}

inline void AdjustConsoleBuffer(const int16_t minLength)
{
    CONSOLE_SCREEN_BUFFER_INFO conInfo;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &conInfo);
    if (conInfo.dwSize.Y < minLength)
        conInfo.dwSize.Y = minLength;
    SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), conInfo.dwSize);
}

inline bool ReleaseConsole()
{
    bool result = true;
    FILE *fp;

    if (freopen_s(&fp, "NUL:", "r", stdin) != 0)
        result = false;
    else
        setvbuf(stdin, nullptr, _IONBF, 0);

    if (freopen_s(&fp, "NUL:", "w", stdout) != 0)
        result = false;
    else
        setvbuf(stdout, nullptr, _IONBF, 0);

    if (freopen_s(&fp, "NUL:", "w", stderr) != 0)
        result = false;
    else
        setvbuf(stderr, nullptr, _IONBF, 0);

    if (!FreeConsole())
        result = false;

    return result;
}

inline bool RedirectConsoleIO()
{
    bool result = true;
    FILE *fp;

    if (GetStdHandle(STD_INPUT_HANDLE) != INVALID_HANDLE_VALUE)
    {
        if (freopen_s(&fp, "CONIN$", "r", stdin) != 0)
            result = false;
        else
            setvbuf(stdin, NULL, _IONBF, 0);
    }

    if (GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE)
    {
        if (freopen_s(&fp, "CONOUT$", "w", stdout) != 0)
            result = false;
        else
            setvbuf(stdout, NULL, _IONBF, 0);
    }

    if (GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE)
    {
        if (freopen_s(&fp, "CONOUT$", "w", stderr) != 0)
            result = false;
        else
            setvbuf(stderr, NULL, _IONBF, 0);
    }

    std::ios::sync_with_stdio(true);

    std::wcout.clear();
    std::cout.clear();
    std::wcerr.clear();
    std::cerr.clear();
    std::wcin.clear();
    std::cin.clear();

    return result;
}

inline bool CreateNewConsole(const int16_t minLength)
{
    bool result = false;

    ReleaseConsole();

    if (AllocConsole())
    {
        AdjustConsoleBuffer(minLength);
        result = RedirectConsoleIO();
    }

    return result;
}

inline std::string Hex(const uint8_t ch)
{
    std::string buf{'0', 0, 0};
    std::to_chars(buf.data() + 1, buf.data() + buf.length(), ch, 16);
    buf.erase(buf.begin() + (buf[2] == 0 ? 2 : 0));
    return buf;
}

static std::string UUID4()
{
    union Data
    {
        uint8_t Buf16[16];
        uint32_t Buf4[4];
    } data{};

    std::random_device rd{};
    const std::uniform_int_distribution<uint32_t> dist{};
    for (auto &x : data.Buf4)
        x = dist(rd);

    data.Buf16[8] &= 0xBF;
    data.Buf16[8] |= 0x80;

    data.Buf16[6] &= 0x4F;
    data.Buf16[6] |= 0x40;

    std::string str = "00000000-0000-0000-0000-000000000000";

    for (size_t i = 0, index = 0; i < 36; ++i)
    {
        if (i == 8 || i == 13 || i == 18 || i == 23)
            continue;

        const auto hex = Hex(data.Buf16[index]);
        str[i] = hex[0];
        str[++i] = hex[1];
        index++;
    }

    return str;
}

inline std::vector<std::filesystem::path> GetFiles(const std::filesystem::path &path)
{
    std::vector<std::filesystem::path> res{};
    for (const auto &p : std::filesystem::directory_iterator(path))
    {
        if (p.is_regular_file())
            res.emplace_back(p);
    }
    return res;
}

inline std::array<uint32_t, 4> ReadVersion(const RcResource &res)
{
    VS_FIXEDFILEINFO *pvFileInfo{};
    UINT uiFileInfoLen;

    if (!VerQueryValue(res.Get().data(), L"\\",
                       reinterpret_cast<void **>(&pvFileInfo), &uiFileInfoLen))
        throw Ex(ImgToolsException,
                 "VerQueryValue: {}: false", res.GetTip());

    return std::array<uint32_t, 4>{
        HIWORD(pvFileInfo->dwFileVersionMS),
        LOWORD(pvFileInfo->dwFileVersionMS),
        HIWORD(pvFileInfo->dwFileVersionLS),
        LOWORD(pvFileInfo->dwFileVersionLS)};
}

inline std::wstring GetUserLanguage()
{
    ULONG num = 0;
    ULONG bufLen = 0;

    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, nullptr, &bufLen))
    {
        throw Ex(ImgToolsException, "GetUserPreferredUILanguages: get buffre length failed");
    }

    const auto buf = std::make_unique<wchar_t[]>(bufLen);

    if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &num, buf.get(), &bufLen))
    {
        throw Ex(ImgToolsException, "GetUserPreferredUILanguages: get lang failed");
    }

    return std::wstring(buf.get());
}

inline std::string ToImString(const std::filesystem::path &str)
{
    const auto s = str.u8string();
    return std::string(reinterpret_cast<const char *>(str.u8string().c_str()), s.length());
}

template <std::ranges::range R>
static Generator<std::filesystem::path> GetFilesFromPaths(R &&paths)
{
    for (const std::filesystem::path path : paths)
    {
        if (is_regular_file(path))
        {
            co_yield path;
        }
        else if (is_directory(path))
        {
            for (const auto &f : std::filesystem::directory_iterator(path))
            {
                if (f.is_regular_file())
                    co_yield f.path();
            }
        }
    }

    co_return;
}

struct to_vector_adapter
{
    struct closure
    {
        template <std::ranges::input_range R>
        constexpr auto operator()(R &&r) const
        {
            auto r_common = r | std::views::common;
            std::vector<std::ranges::range_value_t<R>> v;

            // if we can get a size, reserve that much
            if constexpr (requires { std::ranges::size(r); })
            {
                v.reserve(std::ranges::size(r));
            }

            v.insert(v.begin(), r_common.begin(), r_common.end());

            return v;
        }
    };

    constexpr auto operator()() const -> closure
    {
        return closure{};
    }

    template <std::ranges::range R>
    constexpr auto operator()(R &&r)
    {
        return closure{}(r);
    }
};

inline to_vector_adapter to_vector;

template <std::ranges::range R>
constexpr auto operator|(R &&r, to_vector_adapter::closure const &a)
{
    return a(std::forward<R>(r));
}

template <typename Cont>
struct To
{
    struct Adapter
    {
        template <std::ranges::input_range R>
        constexpr auto operator()(R &&r) const
        {
            auto rng = r | std::views::common;
            return Cont{rng.begin(), rng.end()};
        }
    };

    constexpr auto operator()() const -> Adapter
    {
        return Adapter{};
    }

    template <std::ranges::range R>
    constexpr auto operator()(R &&r)
    {
        return Adapter{}(r);
    }
};

template <typename Cont, std::ranges::range R>
constexpr auto operator|(R &&r, typename To<typename Cont>::Adapter const &a)
{
    return a(std::forward<R>(r));
}

inline std::u8string JoinPaths(const std::vector<std::filesystem::path> &paths)
{
    // return paths | ranges::views::transform([](const auto &x)
    //                                         { return x.u8string(); }) |
    //        ranges::views::join(u8";") | ranges::to<std::u8string>();
    return String::JoinU8(paths.begin(), paths.end(), u8";");
}

#if 0
namespace rg = std::ranges;

template<rg::input_range R> requires rg::view<R>
class custom_take_view : public rg::view_interface<custom_take_view<R>>
{
private:
    R                                         base_{};
    //std::iter_difference_t<rg::iterator_t<R>> count_{};
    rg::iterator_t<R>                         iter_{ std::begin(base_) };
public:
    custom_take_view() = default;

    constexpr custom_take_view(R base//, std::iter_difference_t<rg::iterator_t<R>> count
    )
        : base_(base)
        //, count_(count)
        , iter_(std::begin(base_))
    {}

    constexpr R base() const&
    {
        return base_;
    }
    constexpr R base()&&
    {
        return std::move(base_);
    }

    constexpr auto begin() const
    {
        return iter_;
    }
    constexpr auto end() const
    {
        //return std::next(iter_, count_);
        return std::next(iter_, 1);
    }

    constexpr auto size() const requires rg::sized_range<const R>
    {
        const auto s = rg::size(base_);
        //const auto c = static_cast<decltype(s)>(count_);
        //return s < c ? 0 : s - c;
        return s;
    }
};

template<class R>
custom_take_view(R&& base, std::iter_difference_t<rg::iterator_t<R>>)
->custom_take_view<rg::views::all_t<R>>;

namespace details
{
    struct custom_take_range_adaptor_closure
    {
        constexpr custom_take_range_adaptor_closure()
        {}

        template <rg::viewable_range R>
        constexpr auto operator()(R&& r) const
        {
            return custom_take_view(std::forward<R>(r), count_);
        }
    };

    struct custom_take_range_adaptor
    {
        template<rg::viewable_range R>
        constexpr auto operator () (R&& r, std::iter_difference_t<rg::iterator_t<R>> count)
        {
            return custom_take_view(std::forward<R>(r), count);
        }

        constexpr auto operator () ()
        {
            return custom_take_range_adaptor_closure();
        }
    };

    template <rg::viewable_range R>
    constexpr auto operator | (R&& r, custom_take_range_adaptor_closure const& a)
    {
        return a(std::forward<R>(r));
    }
}

namespace views
{
    details::custom_take_range_adaptor custom_take;
}
#endif

std::vector<std::uint8_t> Base64Decode(std::string str)
{
    static const std::string Table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    const auto ret = std::ranges::remove_if(str, [](char c)
    {
        return !(Table.contains(c) || c == '=');
    });
    str.erase(ret.begin(), ret.end());

    if (str.length() % 4 != 0)
    {
        throw Ex(ImgToolsException, "str.length() % 4 != 0");
    }

    return str
        | ranges::views::chunk(4)
        | ranges::views::transform([](const auto &x)
        {
            static const auto find = [](char c)
            {
                return c == '=' ? 0 : Table.find(c);
            };
            std::uint8_t a = find(x[0]);
            std::uint8_t b = find(x[1]);
            std::uint8_t c = find(x[2]);
            std::uint8_t d = find(x[3]);

            return std::array{(a << 2) | (b >> 4), (b << 4) | (c >> 2), (c << 6) | d};
        })
        | ranges::views::join
        | ranges::to<std::vector<std::uint8_t>>();
}
