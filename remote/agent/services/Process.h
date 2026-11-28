#pragma once

#include "../Fd.h"
#include "../Service.h"

#include <sys/types.h>

#include <map>
#include <string>
#include <vector>

namespace kode::remote
{

// Programs run on the host (docs/REMOTE.md, section 5): process/which finds
// them on the login shell's PATH (read once), process/spawn starts one —
// its stdin a channel the client opened, its stdout one of the agent's,
// stderr dropped —, process/signal stops it; process/exited tells, once its
// output was all sent. A session's processes are stopped when it ends.
// Language servers now, builds later.
class Process final : public Service
{
public:
    explicit Process(Hooks hooks);
    ~Process() override;
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;
    Process(Process &&) = delete;
    Process &operator=(Process &&) = delete;

    [[nodiscard]] std::string_view name() const override { return "process"; }
    [[nodiscard]] std::vector<std::string_view> capabilities() const override { return {}; }
    void
    handle(std::uint64_t session, std::string_view method, yyjson_val *params, Reply reply, const Channels &channels)
        override;
    void sessionClosed(std::uint64_t session) override;

private:
    struct Child {
        // Read from stdout, not sent yet; written to stdin, not taken yet.
        std::string output;
        std::string input;
        std::uint64_t session{0U};
        std::uint64_t flags{0U};
        std::int64_t pid{-1};
        std::int64_t status{0};
        Fd stdinPipe;
        Fd stdoutPipe;
    };
    static constexpr std::uint64_t OutputEnded{1U << 0U};
    static constexpr std::uint64_t InputEnded{1U << 1U};
    static constexpr std::uint64_t Exited{1U << 2U};
    // Its output fills the buffer: stdout is not read until it is taken.
    static constexpr std::uint64_t Paused{1U << 3U};
    // Its session is gone: only reaped.
    static constexpr std::uint64_t Orphan{1U << 4U};

    void which(yyjson_val *params, Reply reply);
    void spawn(std::uint64_t session, yyjson_val *params, Reply reply, const Channels &channels);
    void signal(std::uint64_t session, yyjson_val *params, Reply reply);
    [[nodiscard]] const std::vector<std::string> &path();
    [[nodiscard]] std::string find(const std::string &name);

    void readOutput(std::int64_t id);
    [[nodiscard]] std::expected<std::optional<std::string>, std::string> pull(std::int64_t id, std::size_t limit);
    void feed(std::int64_t id, std::string_view bytes);
    void endInput(std::int64_t id);
    void flushInput(std::int64_t id);
    void reap(std::int64_t id);
    // Done with: told when it exited and its output went, then dropped.
    void finish(std::int64_t id);

    Hooks m_hooks;
    std::map<std::int64_t, Child> m_children;
    std::vector<std::string> m_path;
    std::int64_t m_nextId{1};
};

} // namespace kode::remote
