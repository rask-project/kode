#pragma once

#include "../Service.h"

namespace kode::remote
{

// The host's files (docs/REMOTE.md, section 5): fs/stat, fs/list, fs/read
// (the bytes on a channel of the agent's) and fs/write (the bytes on the
// client's channel; atomic, checked against the version the client
// expects, keeping the file's mode and owner and writing through a link);
// what the sidebar does to files: fs/create, fs/rename and fs/copy (a
// name taken is refused: error::exists), fs/remove and fs/trash (the
// freedesktop Trash on Linux, ~/.Trash on macOS).
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
    static void create(yyjson_val *params, Reply reply);
    static void rename(yyjson_val *params, Reply reply);
    static void copy(yyjson_val *params, Reply reply);
    static void remove(yyjson_val *params, Reply reply);
    static void trash(yyjson_val *params, Reply reply);
};

} // namespace kode::remote
