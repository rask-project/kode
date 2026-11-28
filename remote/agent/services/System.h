#pragma once

#include "../Service.h"

namespace kode::remote
{

// The agent itself: system/ping.
class System final : public Service
{
public:
    [[nodiscard]] std::string_view name() const override { return "system"; }
    [[nodiscard]] std::vector<std::string_view> capabilities() const override { return {}; }
    void
    handle(std::uint64_t session, std::string_view method, yyjson_val *params, Reply reply, const Channels &channels)
        override;
};

} // namespace kode::remote
