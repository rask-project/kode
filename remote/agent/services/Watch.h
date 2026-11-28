#pragma once

#include "../Fd.h"
#include "../Service.h"

#include <map>
#include <set>
#include <string>

namespace kode::remote
{

// Changes on disk, pushed (docs/REMOTE.md, section 5): each session names
// the paths it watches (watch/set: the folders it shows, the files it has
// open), and is told watch/changed {paths} when one changes — a folder's
// entries, a file's bytes, or the file replaced. inotify on Linux (a file
// through its folder, so a file replaced by renaming is still seen), kqueue
// on macOS (a replaced file opened again).
class Watch final : public Service
{
public:
    explicit Watch(Hooks hooks);

    [[nodiscard]] std::string_view name() const override { return "watch"; }
    [[nodiscard]] std::vector<std::string_view> capabilities() const override;
    void
    handle(std::uint64_t session, std::string_view method, yyjson_val *params, Reply reply, const Channels &channels)
        override;
    void sessionClosed(std::uint64_t session) override;

private:
    void set(std::uint64_t session, std::set<std::string> paths);
    void add(const std::string &path);
    void remove(const std::string &path);
    void read();
    void report(const std::set<std::string> &changed);

    Hooks m_hooks;
    // Each session's paths, and which sessions watch each path.
    std::map<std::uint64_t, std::set<std::string>> m_sessions;
    std::map<std::string, std::set<std::uint64_t>> m_watchers;
    // The system's handle per watched path (inotify: its folder's watch,
    // shared; kqueue: the path's own descriptor).
    std::map<std::string, std::int64_t> m_handles;
    // inotify: what each watch descriptor is, and how many paths use it.
    std::map<std::int64_t, std::string> m_folders;
    std::map<std::int64_t, std::int64_t> m_uses;
    // kqueue: the descriptors, by path.
    std::map<std::string, Fd> m_files;
    Fd m_queue;
};

} // namespace kode::remote
