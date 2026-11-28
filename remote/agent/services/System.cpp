#include "System.h"

#include "../../protocol/Methods.h"

namespace kode::remote
{

void System::handle(
    std::uint64_t session,
    std::string_view method,
    yyjson_val *params,
    Reply reply,
    const Channels &channels
)
{
    static_cast<void>(channels);
    static_cast<void>(session);
    static_cast<void>(params);
    if (method == "ping") {
        reply.result(yyjson_mut_obj(reply.doc()));
        return;
    }
    reply.error(error::methodNotFound, "No such method.");
}

} // namespace kode::remote
