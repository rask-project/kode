#pragma once

#include "../Service.h"

namespace kode::remote
{

// The host's files (docs/REMOTE.md, section 5): fs/stat, fs/list, fs/read
// (the bytes on a channel of the agent's) and fs/write (the bytes on the
// client's channel; atomic, checked against the version the client
// expects, keeping the file's mode and owner and writing through a link).
class Fs final : public Service
{
public:
    [[nodiscard]] std::string_view name() const override { return "fs"; }
    [[nodiscard]] std::vector<std::string_view> capabilities() const override { return {}; }
    void
    handle(std::uint64_t session, std::string_view method, yyjson_val *params, Reply reply, const Channels &channels)
        override;

private:
    static void stat(yyjson_val *params, Reply reply);
    static void list(yyjson_val *params, Reply reply);
    static void read(yyjson_val *params, Reply reply, const Channels &channels);
    static void write(yyjson_val *params, Reply reply, const Channels &channels);
};

} // namespace kode::remote
