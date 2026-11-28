#include "Watch.h"

#include "../../protocol/Methods.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/inotify.h>
#else
#include <sys/event.h>
#endif

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstring>

namespace kode::remote
{

namespace
{

bool isFolder(const std::string &path)
{
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::string parentOf(const std::string &path)
{
    const std::size_t slash{path.rfind('/')};
    return slash == 0U ? std::string{"/"} : path.substr(0, slash);
}

std::string pathsJson(const std::set<std::string> &paths)
{
    json::MutDoc doc{json::create()};
    yyjson_mut_val *const root{yyjson_mut_obj(doc.get())};
    yyjson_mut_val *const list{yyjson_mut_arr(doc.get())};
    std::ranges::for_each(paths, [&doc, list](const std::string &path) {
        yyjson_mut_arr_add_strncpy(doc.get(), list, path.data(), path.size());
    });
    yyjson_mut_obj_add_val(doc.get(), root, "paths", list);
    yyjson_mut_doc_set_root(doc.get(), root);
    return json::write(doc.get());
}

} // namespace

Watch::Watch(Hooks hooks) : m_hooks{std::move(hooks)}
{
#if defined(__linux__)
    m_queue = Fd{::inotify_init1(IN_NONBLOCK | IN_CLOEXEC)};
#else
    m_queue = Fd{::kqueue()};
#endif
    if (m_queue.valid()) {
        m_hooks.watch(m_queue.get(), POLLIN, [this] { read(); });
    }
}

std::vector<std::string_view> Watch::capabilities() const
{
    if (!m_queue.valid()) {
        return {};
    }
#if defined(__linux__)
    return {"inotify"};
#else
    return {"kqueue"};
#endif
}

void Watch::handle(
    std::uint64_t session,
    std::string_view method,
    yyjson_val *params,
    Reply reply,
    const Channels &channels
)
{
    static_cast<void>(channels);
    if (method != "set") {
        reply.error(error::methodNotFound, "No such method.");
        return;
    }
    yyjson_val *const paths{yyjson_obj_get(params, "paths")};
    if (!yyjson_is_arr(paths)) {
        reply.error(error::invalidParams, "paths: a list of paths is expected.");
        return;
    }
    std::set<std::string> wanted;
    std::size_t index{0U};
    std::size_t count{0U};
    yyjson_val *value{nullptr};
    yyjson_arr_foreach(paths, index, count, value)
    {
        if (yyjson_is_str(value) && yyjson_get_str(value)[0] == '/') {
            wanted.emplace(yyjson_get_str(value), yyjson_get_len(value));
        }
    }
    set(session, std::move(wanted));
    reply.result(yyjson_mut_obj(reply.doc()));
}

void Watch::sessionClosed(std::uint64_t session)
{
    set(session, {});
    m_sessions.erase(session);
}

void Watch::set(std::uint64_t session, std::set<std::string> paths)
{
    std::set<std::string> &current{m_sessions[session]};
    std::ranges::for_each(current, [&](const std::string &path) {
        if (paths.contains(path)) {
            return;
        }
        std::set<std::uint64_t> &sessions{m_watchers[path]};
        sessions.erase(session);
        if (sessions.empty()) {
            m_watchers.erase(path);
            remove(path);
        }
    });
    std::ranges::for_each(paths, [&](const std::string &path) {
        if (current.contains(path)) {
            return;
        }
        std::set<std::uint64_t> &sessions{m_watchers[path]};
        if (sessions.empty()) {
            add(path);
        }
        sessions.insert(session);
    });
    current = std::move(paths);
}

#if defined(__linux__)

void Watch::add(const std::string &path)
{
    constexpr std::uint32_t events{
        IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF
    };
    // A file through its folder: replaced by renaming, it is still seen.
    const std::string folder{isFolder(path) ? path : parentOf(path)};
    const int wd{::inotify_add_watch(m_queue.get(), folder.c_str(), events)};
    if (wd < 0) {
        return;
    }
    m_handles[path] = wd;
    m_folders[wd] = folder;
    ++m_uses[wd];
}

void Watch::remove(const std::string &path)
{
    const auto handle{m_handles.find(path)};
    if (handle == m_handles.end()) {
        return;
    }
    const std::int64_t wd{handle->second};
    m_handles.erase(handle);
    if (--m_uses[wd] > 0) {
        return;
    }
    m_uses.erase(wd);
    m_folders.erase(wd);
    ::inotify_rm_watch(m_queue.get(), static_cast<int>(wd));
}

void Watch::read()
{
    alignas(inotify_event) std::array<char, 64 * 1024> buffer{};
    std::set<std::string> changed;
    while (true) {
        const ssize_t count{::read(m_queue.get(), buffer.data(), buffer.size())};
        if (count <= 0) {
            break;
        }
        for (std::size_t at{0U}; at < static_cast<std::size_t>(count);) {
            inotify_event event{};
            std::memcpy(&event, buffer.data() + at, sizeof(event));
            const char *const named{buffer.data() + at + sizeof(event)};
            const std::string_view name{
                event.len > 0U ? std::string_view{named, ::strnlen(named, event.len)} : std::string_view{}
            };
            at += sizeof(event) + event.len;
            const auto folder{m_folders.find(event.wd)};
            if (folder == m_folders.end()) {
                continue;
            }
            // The folder itself, and the file named in it (when that is watched).
            changed.insert(folder->second);
            if (!name.empty()) {
                changed.insert((folder->second == "/" ? std::string{} : folder->second) + "/" + std::string{name});
            }
        }
    }
    report(changed);
}

#else

void Watch::add(const std::string &path)
{
    Fd file{::open(path.c_str(), O_EVTONLY | O_CLOEXEC)};
    if (!file.valid()) {
        return;
    }
    struct kevent change{};
    EV_SET(
        &change,
        static_cast<uintptr_t>(file.get()),
        EVFILT_VNODE,
        EV_ADD | EV_CLEAR,
        NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME,
        0,
        nullptr
    );
    if (::kevent(m_queue.get(), &change, 1, nullptr, 0, nullptr) != 0) {
        return;
    }
    m_handles[path] = file.get();
    m_files[path] = std::move(file);
}

void Watch::remove(const std::string &path)
{
    // Closing the descriptor drops its kevent.
    m_files.erase(path);
    m_handles.erase(path);
}

void Watch::read()
{
    std::array<struct kevent, 64> events{};
    const timespec now{};
    std::set<std::string> changed;
    std::set<std::string> reopen;
    while (true) {
        const int count{::kevent(m_queue.get(), nullptr, 0, events.data(), static_cast<int>(events.size()), &now)};
        if (count <= 0) {
            break;
        }
        for (std::size_t i{0U}; i < static_cast<std::size_t>(count); ++i) {
            const auto fd{static_cast<std::int64_t>(events.at(i).ident)};
            const auto found{std::ranges::find_if(m_handles, [fd](const auto &entry) { return entry.second == fd; })};
            if (found == m_handles.end()) {
                continue;
            }
            changed.insert(found->first);
            if ((events.at(i).fflags & (NOTE_DELETE | NOTE_RENAME)) != 0U) {
                reopen.insert(found->first);
            }
        }
    }
    // Replaced by renaming: the new file at the same path.
    std::ranges::for_each(reopen, [this](const std::string &path) {
        remove(path);
        add(path);
    });
    report(changed);
}

#endif

void Watch::report(const std::set<std::string> &changed)
{
    std::map<std::uint64_t, std::set<std::string>> told;
    std::ranges::for_each(changed, [&](const std::string &path) {
        const auto watchers{m_watchers.find(path)};
        if (watchers == m_watchers.end()) {
            return;
        }
        std::ranges::for_each(watchers->second, [&](std::uint64_t session) { told[session].insert(path); });
    });
    std::ranges::for_each(told, [this](const auto &entry) {
        m_hooks.notify(entry.first, "watch/changed", pathsJson(entry.second));
    });
}

} // namespace kode::remote
