#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace kode::remote
{

// One thread's loop over file descriptors (poll(2)) and timers, running
// while it has any. Callbacks may watch, unwatch, start and stop anything,
// themselves included.
class EventLoop
{
public:
    // The poll(2) events wanted now (asked before each wait).
    using Events = std::function<short()>;
    using Ready = std::function<void(short revents)>;

    void watch(int fd, Events events, Ready ready);
    void unwatch(int fd);
    std::uint64_t startTimer(std::chrono::milliseconds delay, std::function<void()> callback);
    void stopTimer(std::uint64_t id);
    void run();
    // Drops every watch and timer: run() returns.
    void quit() noexcept;

private:
    struct Watcher {
        Events events;
        Ready ready;
    };
    struct Timer {
        std::function<void()> callback;
        std::chrono::steady_clock::time_point due;
        std::uint64_t id{0U};
    };

    void runTimers();
    [[nodiscard]] int timeout() const;

    std::map<int, Watcher> m_watchers;
    std::vector<Timer> m_timers;
    std::uint64_t m_nextTimer{1U};
};

} // namespace kode::remote
