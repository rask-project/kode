#include "EventLoop.h"

#include <poll.h>

#include <algorithm>
#include <cerrno>
#include <limits>

namespace kode::remote
{

void EventLoop::watch(int fd, Events events, Ready ready)
{
    m_watchers.insert_or_assign(fd, Watcher{.events = std::move(events), .ready = std::move(ready)});
}

void EventLoop::unwatch(int fd)
{
    m_watchers.erase(fd);
}

void EventLoop::quit() noexcept
{
    m_watchers.clear();
    m_timers.clear();
}

std::uint64_t EventLoop::startTimer(std::chrono::milliseconds delay, std::function<void()> callback)
{
    const std::uint64_t id{m_nextTimer++};
    m_timers.emplace_back(
        Timer{.callback = std::move(callback), .due = std::chrono::steady_clock::now() + delay, .id = id}
    );
    return id;
}

void EventLoop::stopTimer(std::uint64_t id)
{
    std::erase_if(m_timers, [id](const Timer &timer) { return timer.id == id; });
}

int EventLoop::timeout() const
{
    if (m_timers.empty()) {
        return -1;
    }
    const auto next{std::ranges::min(m_timers, {}, &Timer::due).due};
    const auto wait{std::chrono::ceil<std::chrono::milliseconds>(next - std::chrono::steady_clock::now()).count()};
    return static_cast<int>(std::clamp<long long>(wait, 0, std::numeric_limits<int>::max()));
}

void EventLoop::runTimers()
{
    const auto now{std::chrono::steady_clock::now()};
    std::vector<std::uint64_t> due;
    std::ranges::for_each(m_timers, [&due, now](const Timer &timer) {
        if (timer.due <= now) {
            due.emplace_back(timer.id);
        }
    });
    for (const std::uint64_t id : due) {
        const auto timer{std::ranges::find(m_timers, id, &Timer::id)};
        if (timer == m_timers.end()) {
            continue;
        }
        const std::function<void()> callback{std::move(timer->callback)};
        m_timers.erase(timer);
        callback();
    }
}

void EventLoop::run()
{
    std::vector<pollfd> fds;
    while (!m_watchers.empty() || !m_timers.empty()) {
        fds.clear();
        std::ranges::for_each(m_watchers, [&fds](const auto &entry) {
            fds.emplace_back(pollfd{.fd = entry.first, .events = entry.second.events(), .revents = 0});
        });
        const int ready{::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout())};
        if (ready < 0 && errno != EINTR) {
            return;
        }
        for (const pollfd &fd : fds) {
            if (fd.revents == 0) {
                continue;
            }
            const auto watcher{m_watchers.find(fd.fd)};
            if (watcher == m_watchers.end()) {
                continue;
            }
            // A copy: the callback may unwatch its own descriptor.
            const Ready callback{watcher->second.ready};
            callback(fd.revents);
        }
        runTimers();
    }
}

} // namespace kode::remote
