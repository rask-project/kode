#include "Session.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>

namespace kode::remote
{

Session::Session(std::uint64_t key, Fd input, Fd output)
    : m_key{key}
    , m_input{std::move(input)}
    , m_output{std::move(output)}
{}

std::optional<std::vector<Frame>> Session::read()
{
    constexpr std::size_t chunkSize{std::size_t{64} * 1024U};
    std::array<char, chunkSize> chunk{};
    const ssize_t count{::read(m_input.get(), chunk.data(), chunk.size())};
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) {
        return std::vector<Frame>{};
    }
    if (count <= 0) {
        return std::nullopt;
    }
    m_reader.append(std::string_view{chunk.data(), static_cast<std::size_t>(count)});
    std::vector<Frame> frames;
    for (std::optional<Frame> frame{m_reader.next()}; frame; frame = m_reader.next()) {
        frames.emplace_back(std::move(*frame));
    }
    if (m_reader.failed()) {
        return std::nullopt;
    }
    return frames;
}

void Session::send(FrameType type, std::uint32_t channel, std::string_view payload)
{
    appendFrame(m_pending, type, channel, payload);
}

void Session::resetChannels()
{
    std::map<std::uint32_t, Incoming> incoming{std::exchange(m_incoming, {})};
    std::ranges::for_each(incoming, [](auto &entry) { entry.second.sink->reset(); });
    m_outgoing.clear();
}

bool Session::flush()
{
    while (hasPendingOutput()) {
        const ssize_t count{::write(outputFd(), m_pending.data() + m_written, m_pending.size() - m_written)};
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && errno == EAGAIN) {
            return true;
        }
        if (count <= 0) {
            return false;
        }
        m_written += static_cast<std::size_t>(count);
    }
    m_pending.clear();
    m_written = 0U;
    return true;
}

} // namespace kode::remote
