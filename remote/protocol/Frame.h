#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Shared by Kode and kode-remote: the standard library only.
namespace kode::remote
{

// frame = length:u32be type:u8 channel:u32be payload[length - 5]
enum class FrameType : std::uint8_t {
    // JSON-RPC 2.0, on the control channel only.
    Message = 0,
    // Bytes of an open channel.
    Data = 1,
    // The sender sends no more on the channel.
    Eof = 2,
    // payload u32be: how many more bytes the sender of this frame accepts.
    Credit = 3,
    // The channel is gone.
    Reset = 4,
};

inline constexpr std::uint32_t controlChannel{0U};
inline constexpr std::size_t headerSize{9U};
inline constexpr std::size_t maxMessageSize{std::size_t{16} * 1024U * 1024U};
inline constexpr std::size_t maxDataSize{std::size_t{256} * 1024U};

struct Frame {
    std::string payload;
    std::uint32_t channel{controlChannel};
    // A FrameType: checked by the reader.
    std::uint32_t kind{0U};

    [[nodiscard]] FrameType type() const noexcept { return static_cast<FrameType>(kind); }
};

void appendFrame(std::string &out, FrameType type, std::uint32_t channel, std::string_view payload);

// Frames out of a stream that arrives in pieces. A frame larger than its
// type allows, or of an unknown type, fails the stream: nothing is allocated
// for it and nothing more is read.
class FrameReader
{
public:
    void append(std::string_view bytes);
    [[nodiscard]] std::optional<Frame> next();
    [[nodiscard]] bool failed() const noexcept { return m_start == failedStart; }

private:
    static constexpr std::size_t failedStart{std::string::npos};

    std::string m_buffer;
    // Where the next frame starts; failedStart once the stream failed.
    std::size_t m_start{0U};
};

} // namespace kode::remote
