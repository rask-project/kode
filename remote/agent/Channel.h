#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kode::remote
{

// What each side may send on a channel before the other grants more (CREDIT).
inline constexpr std::size_t initialCredit{std::size_t{1024} * 1024U};

// Bytes the agent sends on a channel of its own, read as the client's credit allows.
class Source
{
public:
    Source() = default;
    Source(const Source &) = delete;
    Source &operator=(const Source &) = delete;
    Source(Source &&) = delete;
    Source &operator=(Source &&) = delete;
    virtual ~Source() = default;

    // At most `limit` bytes; none (nullopt) at the end; an error resets the
    // channel. Empty: nothing for now (Hooks::pump when there is).
    [[nodiscard]] virtual std::expected<std::optional<std::string>, std::string> next(std::size_t limit) = 0;
};

// Bytes the client sends on a channel it opened.
class Sink
{
public:
    Sink() = default;
    Sink(const Sink &) = delete;
    Sink &operator=(const Sink &) = delete;
    Sink(Sink &&) = delete;
    Sink &operator=(Sink &&) = delete;
    virtual ~Sink() = default;

    // False: the sink failed (it answered); the channel is reset.
    [[nodiscard]] virtual bool write(std::string_view bytes) = 0;
    // The client sent everything (EOF).
    virtual void end() = 0;
    // The client gave up, or the session ended.
    virtual void reset() = 0;
};

// A request's way to its session's channels.
struct Channels {
    // A new channel of the agent's, fed by `source`; returns its id.
    std::function<std::uint32_t(std::unique_ptr<Source> source)> open;
    // The client's channel `id` goes to `sink`; false when it is not one the client may open.
    std::function<bool(std::uint32_t id, std::unique_ptr<Sink> sink)> accept;
};

} // namespace kode::remote
