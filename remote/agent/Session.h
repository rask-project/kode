#pragma once

#include "../protocol/Frame.h"
#include "Channel.h"
#include "Fd.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kode::remote
{

// One client's connection: frames read from `input`, written to `output`
// (none for a socket: `input` both ways). Both non-blocking; what cannot be
// written now waits for the descriptor to take it.
class Session
{
public:
    Session(std::uint64_t key, Fd input, Fd output);

    [[nodiscard]] std::uint64_t key() const noexcept { return m_key; }
    [[nodiscard]] int inputFd() const noexcept { return m_input.get(); }
    [[nodiscard]] int outputFd() const noexcept { return m_output.valid() ? m_output.get() : m_input.get(); }
    [[nodiscard]] bool separateOutput() const noexcept { return m_output.valid(); }
    [[nodiscard]] bool hasPendingOutput() const noexcept { return m_written < m_pending.size(); }
    // Initialized: it has an id.
    [[nodiscard]] bool initialized() const noexcept { return !m_id.empty(); }
    [[nodiscard]] const std::string &id() const noexcept { return m_id; }
    void initialize(std::string id) { m_id = std::move(id); }

    // The frames that arrived; none when the client is gone or broke the protocol.
    [[nodiscard]] std::optional<std::vector<Frame>> read();
    void send(FrameType type, std::uint32_t channel, std::string_view payload);
    // False when the client is gone.
    [[nodiscard]] bool flush();
    [[nodiscard]] std::size_t pendingOutput() const noexcept { return m_pending.size() - m_written; }

    struct Outgoing {
        std::unique_ptr<Source> source;
        // What the client still accepts.
        std::size_t credit{initialCredit};
    };
    struct Incoming {
        std::unique_ptr<Sink> sink;
        // What the client may still send before it is granted more.
        std::size_t window{initialCredit};
    };
    [[nodiscard]] std::map<std::uint32_t, Outgoing> &outgoing() noexcept { return m_outgoing; }
    [[nodiscard]] std::map<std::uint32_t, Incoming> &incoming() noexcept { return m_incoming; }
    // An id for a channel of the agent's (even).
    [[nodiscard]] std::uint32_t nextChannel() noexcept { return static_cast<std::uint32_t>(m_nextChannel += 2U); }
    // The session ends: every sink is told, then dropped with the sources.
    void resetChannels();

private:
    FrameReader m_reader;
    std::map<std::uint32_t, Outgoing> m_outgoing;
    std::map<std::uint32_t, Incoming> m_incoming;
    std::string m_pending;
    std::string m_id;
    std::size_t m_written{0U};
    std::uint64_t m_key{0U};
    // The last channel id of the agent's (even, as a word: no padding).
    std::uint64_t m_nextChannel{0U};
    Fd m_input;
    Fd m_output;
};

} // namespace kode::remote
