// libFuzzer: bytes as they could arrive from a peer, in pieces sized by the
// input's first byte; each frame's payload parsed as the agent parses
// messages. Nothing may crash, leak or read out of bounds.

#include "../agent/Json.h"
#include "../protocol/Frame.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    if (size == 0U) {
        return 0;
    }
    const std::size_t piece{static_cast<std::size_t>(data[0]) + 1U};
    const std::string_view bytes{reinterpret_cast<const char *>(data + 1), size - 1U};
    kode::remote::FrameReader reader;
    for (std::size_t at{0U}; at < bytes.size() && !reader.failed(); at += piece) {
        reader.append(bytes.substr(at, piece));
        for (std::optional<kode::remote::Frame> frame{reader.next()}; frame; frame = reader.next()) {
            if (frame->type() == kode::remote::FrameType::Message) {
                const kode::remote::json::Doc doc{kode::remote::json::parse(frame->payload)};
                static_cast<void>(doc);
            }
        }
    }
    return 0;
}
