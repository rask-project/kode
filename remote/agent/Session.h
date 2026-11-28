#pragma once

#include "../protocol/Frame.h"
#include "Fd.h"

#include <cstdint>
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

private:
    FrameReader m_reader;
    std::string m_pending;
    std::string m_id;
    std::size_t m_written{0U};
    std::uint64_t m_key{0U};
    Fd m_input;
    Fd m_output;
};

} // namespace kode::remote
