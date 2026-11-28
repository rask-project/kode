#pragma once

#include "EventLoop.h"
#include "Fd.h"
#include "Service.h"
#include "Session.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kode::remote
{

// The agent: its sessions and services on one event loop. Services see
// only requests; the transport, sessions and handshake are here.
class Agent
{
public:
    Agent();

    void addService(std::unique_ptr<Service> service);
    [[nodiscard]] EventLoop &loop() noexcept { return m_loop; }

    // One session on stdin/stdout; returns when it ends.
    int serve();
    // Sessions from the socket in the runtime folder, one daemon per user
    // and version; exits `idle` after the last session ends.
    int daemon(std::chrono::seconds idle);

    // Sends a JSON-RPC message to the session, if it is still there.
    void send(std::uint64_t session, std::string_view message);
    // For services that wait on the system (watch).
    [[nodiscard]] Hooks hooks();

private:
    void open(Fd input, Fd output);
    void close(std::uint64_t key);
    void receive(std::uint64_t key);
    void flush(std::uint64_t key);
    void handle(Session &session, const Frame &frame);
    // Data, Eof, Credit and Reset frames; false when the client broke the protocol.
    [[nodiscard]] bool handleChannel(Session &session, const Frame &frame);
    // Feeds the session's channels as their credit and its output allow.
    void pump(Session &session);
    [[nodiscard]] Channels channels(std::uint64_t key);
    void handleMessage(Session &session, std::string_view payload);
    void initialize(Session &session, yyjson_val *params, Reply reply);
    void accept();
    void sessionsChanged();
    [[nodiscard]] Reply::Send sender(std::uint64_t key);

    EventLoop m_loop;
    std::map<std::uint64_t, std::unique_ptr<Session>> m_sessions;
    std::vector<std::unique_ptr<Service>> m_services;
    Fd m_listener;
    Fd m_lock;
    std::chrono::seconds m_idle{0};
    std::uint64_t m_nextKey{1U};
    std::uint64_t m_idleTimer{0U};
};

} // namespace kode::remote
