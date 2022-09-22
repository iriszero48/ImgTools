#pragma once

#include <source_location>

#include "Log/Log.hpp"
#include "StdIO/StdIO.hpp"
#include "String/String.hpp"
#include "Time/Time.hpp"

#include <boost/stacktrace/stacktrace.hpp>

struct LogMsg
{
    decltype(std::chrono::system_clock::now()) Time;
    decltype(std::this_thread::get_id()) Id;
    std::source_location Source;
    decltype(boost::stacktrace::stacktrace()) Stack;
    std::wstring Msg;

    static decltype(auto) LogTime(const decltype(Time) &time)
    {
        auto t = std::chrono::system_clock::to_time_t(time);
        tm local{};
        Time::Local(&local, &t);
        return String::FromStream(std::put_time(&local, "%F %X"));
    }

    static void LogExceptionImpl(std::vector<std::string> &out,
                                 const std::exception &e,
                                 const std::size_t level)
    {
        out.push_back(
            String::Format("{}>{}", level, e.what()));
        try
        {
            std::rethrow_if_nested(e);
        }
        catch (const std::exception &ex)
        {
            LogExceptionImpl(out, ex, level + 1);
        }
    }

    static std::string LogException(const std::exception &e)
    {
        std::vector<std::string> out{};
        LogExceptionImpl(out, e, 0);
        return String::Join(out.begin(), out.end(), "\n");
    }

    template <typename T, size_t S>
    static constexpr const char *GetFilename(const T (&str)[S])
    {
        for (size_t i = S - 1; i > 0; --i)
            if (str[i] == '/' || str[i] == '\\')
                return &str[i + 1];
        return str;
    }
};

static Logger<LogMsg> Log{};

#define ItLog(lv, ...)                                                          \
    if (Log.Level >= lv)                                                        \
    Log.Write<lv>(std::chrono::system_clock::now(), std::this_thread::get_id(), \
                  std::source_location::current(),                              \
                  boost::stacktrace::stacktrace(),                              \
                  String::FormatW(__VA_ARGS__))
#define LogNone(...) ItLog(LogLevel::None, __VA_ARGS__)
#define LogErr(...) ItLog(LogLevel::Error, __VA_ARGS__)
#define LogWarn(...) ItLog(LogLevel::Warn, __VA_ARGS__)
#define LogLog(...) ItLog(LogLevel::Log, __VA_ARGS__)
#define LogInfo(...) ItLog(LogLevel::Info, __VA_ARGS__)
#define LogDebug(...) ItLog(LogLevel::Debug, __VA_ARGS__)

static void LogHandle()
{
    const std::unordered_map<LogLevel, Console::Color> colorMap{
        {LogLevel::None, Console::Color::White},
        {LogLevel::Error, Console::Color::Red},
        {LogLevel::Warn, Console::Color::Yellow},
        {LogLevel::Log, Console::Color::White},
        {LogLevel::Info, Console::Color::Gray},
        {LogLevel::Debug, Console::Color::Blue}};

    while (true)
    {
        const auto [level, raw] = Log.Chan.Read();
        const auto &[time, id, src, stack, msg] = raw;

        auto out = String::FormatU8("[{}] [{}] [{}] [{}:{},{}] [{}] {}",
                                    Enum::ToString(level), LogMsg::LogTime(time),
                                    String::FromStream(id, std::hex),
                                    std::filesystem::path(src.file_name()).filename(), src.line(), src.column(),
                                    src.function_name(),
                                    msg);
        if (out.ends_with('\n'))
            out.erase(out.length() - 1);

        SetForegroundColor(colorMap.at(level));
        Console::WriteLine(std::string_view(reinterpret_cast<const char *>(out.c_str()), out.length()));

        if (level == LogLevel::Error)
            Console::WriteLine(stack);

        Console::WriteLine();

        if (level == LogLevel::None)
            break;
    }
}
