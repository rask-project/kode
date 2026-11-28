#pragma once

#include <unistd.h>

#include <cstdint>
#include <utility>

namespace kode::remote
{

// An owned file descriptor, closed when it goes. Kept in 64 bits: beside
// the 8-byte members it sits with, no padding.
class Fd
{
public:
    Fd() noexcept = default;
    explicit Fd(int fd) noexcept : m_fd{fd} {}
    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
    Fd(Fd &&other) noexcept : m_fd{std::exchange(other.m_fd, -1)} {}
    Fd &operator=(Fd &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_fd = std::exchange(other.m_fd, -1);
        }
        return *this;
    }
    ~Fd() { reset(); }

    [[nodiscard]] int get() const noexcept { return static_cast<int>(m_fd); }
    [[nodiscard]] bool valid() const noexcept { return m_fd >= 0; }
    void reset() noexcept
    {
        if (m_fd >= 0) {
            ::close(static_cast<int>(m_fd));
            m_fd = -1;
        }
    }

private:
    std::int64_t m_fd{-1};
};

} // namespace kode::remote
