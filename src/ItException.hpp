#pragma once

#include <stdexcept>
#include <format>

#include <boost/stacktrace/stacktrace.hpp>

class Exception : public std::runtime_error
{
public:
    template <typename... Args>
    explicit Exception(const std::string_view &fmt, Args &&...args)
        : std::runtime_error(std::vformat(
              fmt, std::make_format_args(std::forward<Args>(args)...))) {}
};

#define MakeException(name, base) \
    class name : public base      \
    {                             \
        using base::base;         \
    }

MakeException(WinApiException, Exception);

MakeException(D3D11Exception, Exception);

MakeException(ToolException, Exception);

MakeException(LibZipException, Exception);

MakeException(ImgToolsException, Exception);

#define Ex(ex, ...)                                                   \
    ex("[{}:{}] [{}] [{}] {}",                                        \
       std::filesystem::path(__FILE__).filename().string(), __LINE__, \
       __FUNCTION__,                                                  \
       #ex,                                                           \
       std::format(__VA_ARGS__), "\n", boost::stacktrace::to_string(boost::stacktrace::stacktrace()))
