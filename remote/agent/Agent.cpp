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
    // While the session is still found: what the sinks answer goes nowhere.
    found->second->resetChannels();
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
    if (const auto session{m_sessions.find(key)}; session != m_sessions.end()) {
        pump(*session->second);
    }
    flush(key);
}

void Agent::flush(std::uint64_t key)
{
    const auto found{m_sessions.find(key)};
    if (found == m_sessions.end()) {
        return;
    }
    if (!found->second->flush()) {
        close(key);
        return;
    }
    // Room again: the channels go on.
    pump(*found->second);
}

void Agent::pump(Session &session)
{
    // Keeps a session's output short: channel 0 is not held back behind a channel's data.
    constexpr std::size_t outputLimit{std::size_t{1024} * 1024U};
    auto &outgoing{session.outgoing()};
    for (auto channel{outgoing.begin()}; channel != outgoing.end();) {
        Session::Outgoing &out{channel->second};
        bool ended{false};
        while (out.credit > 0U && session.pendingOutput() < outputLimit) {
            std::expected<std::optional<std::string>, std::string> piece{
                out.source->next(std::min(out.credit, maxDataSize))
            };
            const std::optional<std::string> bytes{piece ? std::move(*piece) : std::nullopt};
            if (!bytes.has_value()) {
                session.send(piece ? FrameType::Eof : FrameType::Reset, channel->first, {});
                ended = true;
                break;
            }
            out.credit -= bytes->size();
            session.send(FrameType::Data, channel->first, *bytes);
        }
        channel = ended ? outgoing.erase(channel) : std::next(channel);
    }
}

Channels Agent::channels(std::uint64_t key)
{
    return Channels{
        .open = [this, key](std::unique_ptr<Source> source) -> std::uint32_t {
            const auto found{m_sessions.find(key)};
            if (found == m_sessions.end()) {
                return 0U;
            }
            const std::uint32_t id{found->second->nextChannel()};
            found->second->outgoing().emplace(
                id,
                Session::Outgoing{.source = std::move(source), .credit = initialCredit}
            );
            return id;
        },
        .accept =
            [this, key](std::uint32_t id, std::unique_ptr<Sink> sink) {
                const auto found{m_sessions.find(key)};
                // The client's channels are odd.
                if (found == m_sessions.end() || id % 2U == 0U || found->second->incoming().contains(id)) {
                    return false;
                }
                found->second->incoming().emplace(
                    id,
                    Session::Incoming{.sink = std::move(sink), .window = initialCredit}
                );
                return true;
            },
    };
}

bool Agent::handleChannel(Session &session, const Frame &frame)
{
    auto &incoming{session.incoming()};
    const auto in{incoming.find(frame.channel)};
    switch (frame.type()) {
    case FrameType::Data: {
        if (in == incoming.end()) {
            session.send(FrameType::Reset, frame.channel, {});
            return true;
        }
        if (frame.payload.size() > in->second.window) {
            return false;
        }
        if (!in->second.sink->write(frame.payload)) {
            incoming.erase(in);
            session.send(FrameType::Reset, frame.channel, {});
            return true;
        }
        // Written: the client may send as much again.
        std::string credit;
        const auto size{static_cast<std::uint32_t>(frame.payload.size())};
        credit.push_back(static_cast<char>(size >> 24U));
        credit.push_back(static_cast<char>((size >> 16U) & 0xFFU));
        credit.push_back(static_cast<char>((size >> 8U) & 0xFFU));
        credit.push_back(static_cast<char>(size & 0xFFU));
        session.send(FrameType::Credit, frame.channel, credit);
        return true;
    }
    case FrameType::Eof:
        if (in != incoming.end()) {
            const std::unique_ptr<Sink> sink{std::move(in->second.sink)};
            incoming.erase(in);
            sink->end();
        }
        return true;
    case FrameType::Reset:
        if (in != incoming.end()) {
            const std::unique_ptr<Sink> sink{std::move(in->second.sink)};
            incoming.erase(in);
            sink->reset();
        }
        session.outgoing().erase(frame.channel);
        return true;
    case FrameType::Credit: {
        const auto out{session.outgoing().find(frame.channel)};
        if (out == session.outgoing().end()) {
            return true;
        }
        const auto byte{[&frame](std::size_t index) {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(frame.payload[index]));
        }};
        out->second.credit += (byte(0) << 24U) | (byte(1) << 16U) | (byte(2) << 8U) | byte(3);
        return true;
    }
    case FrameType::Message:
        return false;
    }
    return false;
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
    case FrameType::Data:
    case FrameType::Eof:
    case FrameType::Credit:
        if (!handleChannel(session, frame)) {
            close(session.key());
        }
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
    (*service)->handle(session.key(), method->substr(slash + 1), params, std::move(reply), channels(session.key()));
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
