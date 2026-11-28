#pragma once

#include "Channel.h"
#include "Json.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kode::remote
{

// The answer to one request, given now or later (moved into whatever
// finishes the work). One left unanswered answers with an internal error;
// a notification's (no id) sends nothing.
class Reply
{
public:
    using Send = std::function<void(std::string message)>;

    // `id`: the request's id as JSON text, empty for a notification.
    Reply(std::string id, Send send);
    Reply(const Reply &) = delete;
    Reply &operator=(const Reply &) = delete;
    Reply(Reply &&other) noexcept;
    Reply &operator=(Reply &&other) noexcept;
    ~Reply();

    // Where the result is built.
    [[nodiscard]] yyjson_mut_doc *doc() const noexcept { return m_doc.get(); }
    void result(yyjson_mut_val *value);
    void error(std::int32_t code, std::string_view message);
    // With `data` (built in doc()) for the client to act on.
    void error(std::int32_t code, std::string_view message, yyjson_mut_val *data);

private:
    void finish(yyjson_mut_val *root);

    json::MutDoc m_doc;
    Send m_send;
    std::string m_id;
};

// One feature of the agent: its methods ("<name>/<method>"), what it offers
// on this host, and what it keeps for each session.
class Service
{
public:
    Service() = default;
    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;
    Service(Service &&) = delete;
    Service &operator=(Service &&) = delete;
    virtual ~Service() = default;

    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual std::vector<std::string_view> capabilities() const = 0;
    // `method` without the service's prefix; one it does not have answers
    // with error::methodNotFound.
    virtual void handle(
        std::uint64_t session,
        std::string_view method,
        yyjson_val *params,
        Reply reply,
        const Channels &channels
    ) = 0;
    // What the session owned goes.
    virtual void sessionClosed(std::uint64_t session) { static_cast<void>(session); }
};

} // namespace kode::remote
