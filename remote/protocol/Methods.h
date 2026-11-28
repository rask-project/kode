#pragma once

#include <cstdint>
#include <string_view>

// The JSON-RPC methods and error codes Kode and kode-remote share.
namespace kode::remote
{

inline constexpr std::int32_t protocolVersion{1};

namespace method
{
inline constexpr std::string_view initialize{"initialize"};
inline constexpr std::string_view cancel{"$/cancel"};
inline constexpr std::string_view ping{"system/ping"};
} // namespace method

namespace error
{
inline constexpr std::int32_t parseError{-32700};
inline constexpr std::int32_t invalidRequest{-32600};
inline constexpr std::int32_t methodNotFound{-32601};
inline constexpr std::int32_t invalidParams{-32602};
inline constexpr std::int32_t internalError{-32603};
inline constexpr std::int32_t notInitialized{-32002};
// Kode's own: the connection went away before the answer came.
inline constexpr std::int32_t disconnected{-32000};
} // namespace error

} // namespace kode::remote
