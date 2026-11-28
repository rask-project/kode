#include "Agent.h"

#include "../protocol/Methods.h"
#include "Runtime.h"

#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/random.h>
#endif

#include <algorithm>
#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>

namespace kode::remote
{

namespace
{

// 128 random bits, as hex.
std::string randomId()
{
    std::array<unsigned char, 16> bytes{};
    if (::getentropy(bytes.data(), bytes.size()) != 0) {
        std::abort();
    }
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string id;
    std::ranges::for_each(bytes, [&id, digits](unsigned char byte) {
        id.push_back(digits[byte >> 4U]);
        id.push_back(digits[byte & 0x0FU]);
    });
    return id;
}

std::string lowercase(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return text;
}

yyjson_mut_val *platform(yyjson_mut_doc *doc)
{
    utsname names{};
    ::uname(&names);
    std::string arch{static_cast<const char *>(names.machine)};
    if (arch == "aarch64") {
        arch = "arm64";
    }
    const passwd *const user{::getpwuid(::getuid())};
    const char *const home{std::getenv("HOME")};
    const char *const shell{std::getenv("SHELL")};
    yyjson_mut_val *const info{yyjson_mut_obj(doc)};
    yyjson_mut_obj_add_strcpy(doc, info, "os", lowercase(static_cast<const char *>(names.sysname)).c_str());
    yyjson_mut_obj_add_strcpy(doc, info, "arch", arch.c_str());
    yyjson_mut_obj_add_strcpy(doc, info, "user", user != nullptr ? user->pw_name : "");
    yyjson_mut_obj_add_strcpy(doc, info, "home", home != nullptr ? home : (user != nullptr ? user->pw_dir : ""));
    yyjson_mut_obj_add_strcpy(doc, info, "shell", shell != nullptr ? shell : (user != nullptr ? user->pw_shell : ""));
    return info;
}

} // namespace

Agent::Agent()
{
    // A client gone mid-write is an error to handle, not a signal that kills the agent.
    std::signal(SIGPIPE, SIG_IGN);
}

void Agent::addService(std::unique_ptr<Service> service)
{
    m_services.emplace_back(std::move(service));
}

int Agent::serve()
{
    if (!setNonBlocking(STDIN_FILENO) || !setNonBlocking(STDOUT_FILENO)) {
        return EXIT_FAILURE;
    }
    open(Fd{STDIN_FILENO}, Fd{STDOUT_FILENO});
    m_loop.run();
    return EXIT_SUCCESS;
}

int Agent::daemon(std::chrono::seconds idle)
{
    m_idle = idle;
    const std::expected<std::string, std::string> folder{runtimeFolder()};
    if (!folder) {
        std::fprintf(stderr, "kode-remote: %s\n", folder.error().c_str());
        return EXIT_FAILURE;
    }
    m_lock = Fd{::open(lockPath(*folder).c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600)};
    if (!m_lock.valid()) {
        std::fprintf(stderr, "kode-remote: %s\n", systemError("Cannot open the lock").c_str());
        return EXIT_FAILURE;
    }
    // Another daemon of this version runs: it serves.
    if (::flock(m_lock.get(), LOCK_EX | LOCK_NB) != 0) {
        return EXIT_SUCCESS;
    }
    std::expected<Fd, std::string> listener{listenOn(socketPath(*folder))};
    if (!listener) {
        std::fprintf(stderr, "kode-remote: %s\n", listener.error().c_str());
        return EXIT_FAILURE;
    }
    m_listener = std::move(*listener);
    m_loop.watch(m_listener.get(), [] { return static_cast<short>(POLLIN); }, [this](short) { accept(); });
    sessionsChanged();
    m_loop.run();
    ::unlink(socketPath(*folder).c_str());
    return EXIT_SUCCESS;
}

void Agent::accept()
{
    Fd client{::accept(m_listener.get(), nullptr, nullptr)};
    if (!client.valid()) {
        return;
    }
    ::fcntl(client.get(), F_SETFD, FD_CLOEXEC);
    if (!peerIsUser(client.get()) || !setNonBlocking(client.get())) {
        return;
    }
    open(std::move(client), Fd{});
}

void Agent::open(Fd input, Fd output)
{
    const std::uint64_t key{m_nextKey++};
    auto session{std::make_unique<Session>(key, std::move(input), std::move(output))};
    const Session &added{*session};
    m_sessions.emplace(key, std::move(session));
    if (added.separateOutput()) {
        m_loop.watch(added.inputFd(), [] { return static_cast<short>(POLLIN); }, [this, key](short) { receive(key); });
        m_loop.watch(
            added.outputFd(),
            [&added] { return static_cast<short>(added.hasPendingOutput() ? POLLOUT : 0); },
            [this, key](short) { flush(key); }
        );
    } else {
        m_loop.watch(
            added.inputFd(),
            [&added] { return static_cast<short>(added.hasPendingOutput() ? POLLIN | POLLOUT : POLLIN); },
            [this, key](short revents) {
                if ((static_cast<unsigned int>(revents) & POLLOUT) != 0U) {
                    flush(key);
                }
                if ((static_cast<unsigned int>(revents) & ~static_cast<unsigned int>(POLLOUT)) != 0U) {
                    receive(key);
                }
            }
        );
    }
    sessionsChanged();
}

void Agent::close(std::uint64_t key)
{
    const auto found{m_sessions.find(key)};
    if (found == m_sessions.end()) {
        return;
    }
    m_loop.unwatch(found->second->inputFd());
    m_loop.unwatch(found->second->outputFd());
    std::ranges::for_each(m_services, [key](const std::unique_ptr<Service> &service) { service->sessionClosed(key); });
    m_sessions.erase(found);
    sessionsChanged();
}

void Agent::sessionsChanged()
{
    // serve(): the loop ends with its session.
    if (!m_listener.valid()) {
        if (m_sessions.empty()) {
            m_loop.quit();
        }
        return;
    }
    m_loop.stopTimer(m_idleTimer);
    m_idleTimer = 0U;
    if (m_sessions.empty()) {
        m_idleTimer = m_loop.startTimer(m_idle, [this] { m_loop.quit(); });
    }
}

void Agent::receive(std::uint64_t key)
{
    const auto found{m_sessions.find(key)};
    if (found == m_sessions.end()) {
        return;
    }
    std::optional<std::vector<Frame>> frames{found->second->read()};
    if (!frames) {
        close(key);
        return;
    }
    for (const Frame &frame : *frames) {
        // A frame may end the session.
        const auto session{m_sessions.find(key)};
        if (session == m_sessions.end()) {
            return;
        }
        handle(*session->second, frame);
    }
    flush(key);
}

void Agent::flush(std::uint64_t key)
{
    const auto found{m_sessions.find(key)};
    if (found != m_sessions.end() && !found->second->flush()) {
        close(key);
    }
}

void Agent::send(std::uint64_t session, std::string_view message)
{
    const auto found{m_sessions.find(session)};
    if (found == m_sessions.end()) {
        return;
    }
    // Written when the loop sees the descriptor take it: never from inside a handler.
    found->second->send(FrameType::Message, controlChannel, message);
}

Reply::Send Agent::sender(std::uint64_t key)
{
    return [this, key](const std::string &message) { send(key, message); };
}

void Agent::handle(Session &session, const Frame &frame)
{
    switch (frame.type()) {
    case FrameType::Message:
        if (frame.channel != controlChannel) {
            close(session.key());
            return;
        }
        handleMessage(session, frame.payload);
        return;
    case FrameType::Reset:
        return;
    case FrameType::Data:
    case FrameType::Eof:
    case FrameType::Credit:
        // No channel is open yet: whatever it was, it is gone.
        session.send(FrameType::Reset, frame.channel, {});
        return;
    }
}

void Agent::handleMessage(Session &session, std::string_view payload)
{
    const json::Doc doc{json::parse(payload)};
    yyjson_val *const root{doc ? yyjson_doc_get_root(doc.get()) : nullptr};
    if (!yyjson_is_obj(root)) {
        Reply{"null", sender(session.key())}.error(error::parseError, "The message is not a JSON object.");
        return;
    }
    yyjson_val *const id{yyjson_obj_get(root, "id")};
    const std::optional<std::string_view> method{json::string(root, "method")};
    // An answer to a request of ours: the agent sends none yet.
    if (!method) {
        return;
    }
    Reply reply{id != nullptr ? json::write(id) : std::string{}, sender(session.key())};
    yyjson_val *const params{yyjson_obj_get(root, "params")};
    if (*method == method::initialize) {
        initialize(session, params, std::move(reply));
        return;
    }
    if (!session.initialized()) {
        reply.error(error::notInitialized, "The session is not initialized.");
        return;
    }
    if (*method == method::cancel) {
        return;
    }
    const std::size_t slash{method->find('/')};
    const std::string_view prefix{method->substr(0, slash)};
    const auto service{std::ranges::find_if(m_services, [prefix](const std::unique_ptr<Service> &candidate) {
        return candidate->name() == prefix;
    })};
    if (slash == std::string_view::npos || service == m_services.end()) {
        reply.error(error::methodNotFound, "No such method.");
        return;
    }
    (*service)->handle(session.key(), method->substr(slash + 1), params, std::move(reply));
}

void Agent::initialize(Session &session, yyjson_val *params, Reply reply)
{
    if (session.initialized()) {
        reply.error(error::invalidRequest, "The session is already initialized.");
        return;
    }
    const std::optional<std::int64_t> protocol{json::integer(params, "protocol")};
    if (protocol != protocolVersion) {
        reply.error(error::invalidRequest, "This protocol version is not supported.");
        return;
    }
    session.initialize(randomId());
    yyjson_mut_doc *const doc{reply.doc()};
    yyjson_mut_val *const result{yyjson_mut_obj(doc)};
    yyjson_mut_obj_add_str(doc, result, "agent", KODE_REMOTE_VERSION);
    yyjson_mut_obj_add_int(doc, result, "protocol", protocolVersion);
    yyjson_mut_obj_add_strcpy(doc, result, "session", session.id().c_str());
    yyjson_mut_obj_add_val(doc, result, "platform", platform(doc));
    yyjson_mut_val *const capabilities{yyjson_mut_obj(doc)};
    for (const std::unique_ptr<Service> &service : m_services) {
        yyjson_mut_val *const list{yyjson_mut_arr(doc)};
        for (const std::string_view capability : service->capabilities()) {
            yyjson_mut_arr_add_strncpy(doc, list, capability.data(), capability.size());
        }
        const std::string_view name{service->name()};
        yyjson_mut_obj_add(capabilities, yyjson_mut_strncpy(doc, name.data(), name.size()), list);
    }
    yyjson_mut_obj_add_val(doc, result, "capabilities", capabilities);
    reply.result(result);
}

} // namespace kode::remote
