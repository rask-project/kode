#pragma once

#include "../Service.h"

namespace kode::remote
{

// Finding files on the host (docs/REMOTE.md, section 5): search/files lists
// a folder's files, as FileSearch does here (.git, build, cmake-build-* and
// node_modules left out, links to folders not followed, hidden ones on
// request), on a channel: one path relative to the folder per line, walked
// as the client takes them. Scoring stays in Kode.
class Search final : public Service
{
public:
    [[nodiscard]] std::string_view name() const override { return "search"; }
    [[nodiscard]] std::vector<std::string_view> capabilities() const override { return {}; }
    void
    handle(std::uint64_t session, std::string_view method, yyjson_val *params, Reply reply, const Channels &channels)
        override;
};

} // namespace kode::remote
