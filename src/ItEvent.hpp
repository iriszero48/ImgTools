#pragma once

#include <filesystem>
#include <optional>
#include <vector>
#include <variant>

#include "Thread/Thread.hpp"

template <typename Impl>
struct IEvent
{
    decltype(auto) GetArg()
    {
        return static_cast<Impl*>(this)->GetArg();
    }
};

struct DragDropFilesEvent : IEvent<DragDropFilesEvent>
{
    using ArgType = std::vector<std::filesystem::path>;

    ArgType Value;

    explicit DragDropFilesEvent(ArgType val) : Value(std::move(val)) {}

    ArgType& GetArg() { return Value; }
};

struct DragDropPresetEvent : IEvent<DragDropPresetEvent>
{
    using ArgType = std::filesystem::path;

    ArgType Value;

    explicit DragDropPresetEvent(ArgType val) : Value(std::move(val)) {}

    ArgType& GetArg() { return Value; }
};

struct SaveSettingEvent : IEvent<SaveSettingEvent>
{
	auto GetArg() { return std::nullopt; }
};

struct AlwaysEvent : IEvent<AlwaysEvent>
{
    auto GetArg() const { return std::nullopt; }
};

struct StartProcessEvent : IEvent<StartProcessEvent>
{
    auto GetArg() const { return std::nullopt; }
};

struct EndProcessEvent : IEvent<EndProcessEvent>
{
    auto GetArg() const { return std::nullopt; }
};

struct LoadImageEvent : IEvent<LoadImageEvent>
{
    using ArgType = std::vector<std::filesystem::path>;

    ArgType Paths;

    explicit LoadImageEvent(ArgType&& paths) : Paths(std::move(paths)) {}

    ArgType& GetArg() { return Paths; }
};

template <typename... EventTypes>
class EventSystem
{
    using EventType = std::variant<EventTypes...>;

    Thread::Channel<EventType> eventList;

public:
    void Emit(EventType event)
    {
        eventList.Write(std::move(event));
    }

    template <typename Handler>
    void Dispatch(Handler&& handler)
    {
        while (!eventList.Empty())
        {
            auto val = eventList.Read();
            std::visit(handler, val);
        }
    }
};
