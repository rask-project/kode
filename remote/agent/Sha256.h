#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace kode::remote
{

// SHA-256 (FIPS 180-4), fed in pieces.
class Sha256
{
public:
    void add(std::string_view bytes) noexcept;
    // The digest as lowercase hex; the object is spent.
    [[nodiscard]] std::string hex();

private:
    void block(const unsigned char *data) noexcept;

    std::array<std::uint32_t, 8>
        m_state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<unsigned char, 64> m_buffer{};
    std::uint64_t m_length{0U};
    std::uint64_t m_buffered{0U};
};

// The SHA-256 of a file's bytes, as hex.
[[nodiscard]] std::expected<std::string, std::string> hashFile(const std::string &path);

} // namespace kode::remote
