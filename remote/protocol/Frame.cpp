#include "Frame.h"

namespace kode::remote
{

namespace
{

void appendU32(std::string &out, std::uint32_t value)
{
    out.push_back(static_cast<char>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<char>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<char>(value & 0xFFU));
}

std::uint32_t readU32(std::string_view bytes) noexcept
{
    const auto byte{[bytes](std::size_t index) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index]));
    }};
    return (byte(0) << 24U) | (byte(1) << 16U) | (byte(2) << 8U) | byte(3);
}

std::optional<std::size_t> payloadLimit(std::uint8_t type) noexcept
{
    switch (static_cast<FrameType>(type)) {
    case FrameType::Message:
        return maxMessageSize;
    case FrameType::Data:
        return maxDataSize;
    case FrameType::Credit:
        return std::size_t{4};
    case FrameType::Eof:
    case FrameType::Reset:
        return std::size_t{0};
    }
    return std::nullopt;
}

} // namespace

void appendFrame(std::string &out, FrameType type, std::uint32_t channel, std::string_view payload)
{
    appendU32(out, static_cast<std::uint32_t>(payload.size() + headerSize - 4U));
    out.push_back(static_cast<char>(type));
    appendU32(out, channel);
    out.append(payload);
}

void FrameReader::append(std::string_view bytes)
{
    if (failed()) {
        return;
    }
    // Drops what was read once it is most of the buffer.
    if (m_start > 0U && m_start * 2U >= m_buffer.size()) {
        m_buffer.erase(0, m_start);
        m_start = 0U;
    }
    m_buffer.append(bytes);
}

std::optional<Frame> FrameReader::next()
{
    if (failed()) {
        return std::nullopt;
    }
    const std::string_view pending{std::string_view{m_buffer}.substr(m_start)};
    if (pending.size() < headerSize) {
        return std::nullopt;
    }
    const std::uint32_t length{readU32(pending)};
    const auto type{static_cast<std::uint8_t>(pending[4])};
    const std::optional<std::size_t> limit{payloadLimit(type)};
    if (length < headerSize - 4U || !limit || length - (headerSize - 4U) > *limit) {
        m_buffer.clear();
        m_start = failedStart;
        return std::nullopt;
    }
    if (pending.size() < std::size_t{length} + 4U) {
        return std::nullopt;
    }
    Frame frame{
        .payload = std::string{pending.substr(headerSize, length - (headerSize - 4U))},
        .channel = readU32(pending.substr(5)),
        .kind = type,
    };
    m_start += std::size_t{length} + 4U;
    if (m_start == m_buffer.size()) {
        m_buffer.clear();
        m_start = 0U;
    }
    return frame;
}

} // namespace kode::remote
