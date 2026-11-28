#include "Sha256.h"

#include "Fd.h"
#include "Runtime.h"

#include <fcntl.h>
#include <unistd.h>

#include <bit>
#include <cerrno>

namespace kode::remote
{

namespace
{

constexpr std::array<std::uint32_t, 64> rounds{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::size_t blockSize{64U};

} // namespace

void Sha256::block(const unsigned char *data) noexcept
{
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i{0U}; i < 16U; ++i) {
        w.at(i) = (std::uint32_t{data[i * 4U]} << 24U) | (std::uint32_t{data[(i * 4U) + 1U]} << 16U) |
                  (std::uint32_t{data[(i * 4U) + 2U]} << 8U) | std::uint32_t{data[(i * 4U) + 3U]};
    }
    for (std::size_t i{16U}; i < 64U; ++i) {
        const std::uint32_t s0{std::rotr(w.at(i - 15U), 7) ^ std::rotr(w.at(i - 15U), 18) ^ (w.at(i - 15U) >> 3U)};
        const std::uint32_t s1{std::rotr(w.at(i - 2U), 17) ^ std::rotr(w.at(i - 2U), 19) ^ (w.at(i - 2U) >> 10U)};
        w.at(i) = w.at(i - 16U) + s0 + w.at(i - 7U) + s1;
    }
    std::array<std::uint32_t, 8> v{m_state};
    for (std::size_t i{0U}; i < 64U; ++i) {
        const std::uint32_t s1{std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25)};
        const std::uint32_t choice{(v[4] & v[5]) ^ (~v[4] & v[6])};
        const std::uint32_t first{v[7] + s1 + choice + rounds.at(i) + w.at(i)};
        const std::uint32_t s0{std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22)};
        const std::uint32_t majority{(v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2])};
        const std::uint32_t second{s0 + majority};
        v = {first + second, v[0], v[1], v[2], v[3] + first, v[4], v[5], v[6]};
    }
    for (std::size_t i{0U}; i < 8U; ++i) {
        m_state.at(i) += v.at(i);
    }
}

void Sha256::add(std::string_view bytes) noexcept
{
    m_length += bytes.size();
    for (const char c : bytes) {
        m_buffer.at(m_buffered++) = static_cast<unsigned char>(c);
        if (m_buffered == blockSize) {
            block(m_buffer.data());
            m_buffered = 0U;
        }
    }
}

std::string Sha256::hex()
{
    const std::uint64_t bits{m_length * 8U};
    std::string padding(1, '\x80');
    const std::uint64_t used{(m_length + 1U) % blockSize};
    padding.append(used <= 56U ? 56U - used : 120U - used, '\0');
    for (std::int32_t shift{56}; shift >= 0; shift -= 8) {
        padding.push_back(static_cast<char>((bits >> static_cast<std::uint32_t>(shift)) & 0xFFU));
    }
    // add() counts the padding too; the length is already in it.
    add(padding);
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string out;
    for (const std::uint32_t word : m_state) {
        for (std::int32_t shift{28}; shift >= 0; shift -= 4) {
            out.push_back(digits[(word >> static_cast<std::uint32_t>(shift)) & 0x0FU]);
        }
    }
    return out;
}

std::expected<std::string, std::string> hashFile(const std::string &path)
{
    const Fd file{::open(path.c_str(), O_RDONLY | O_CLOEXEC)};
    if (!file.valid()) {
        return std::unexpected{systemError(path)};
    }
    Sha256 hash;
    std::string chunk(std::size_t{64} * 1024U, '\0');
    while (true) {
        const ssize_t count{::read(file.get(), chunk.data(), chunk.size())};
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            return std::unexpected{systemError(path)};
        }
        if (count == 0) {
            return hash.hex();
        }
        hash.add(std::string_view{chunk.data(), static_cast<std::size_t>(count)});
    }
}

} // namespace kode::remote
