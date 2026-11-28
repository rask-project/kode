#include "Service.h"

#include "../protocol/Methods.h"

#include <utility>

namespace kode::remote
{

Reply::Reply(std::string id, Send send) : m_doc{json::create()}, m_send{std::move(send)}, m_id{std::move(id)} {}

Reply::Reply(Reply &&other) noexcept
    : m_doc{std::move(other.m_doc)}
    , m_send{std::move(other.m_send)}
    , m_id{std::exchange(other.m_id, {})}
{}

Reply &Reply::operator=(Reply &&other) noexcept
{
    if (this != &other) {
        m_doc = std::move(other.m_doc);
        m_send = std::move(other.m_send);
        m_id = std::exchange(other.m_id, {});
    }
    return *this;
}

Reply::~Reply()
{
    if (!m_id.empty() && m_doc) {
        error(error::internalError, "The request was not answered.");
    }
}

void Reply::result(yyjson_mut_val *value)
{
    if (m_id.empty() || !m_doc) {
        return;
    }
    yyjson_mut_val *const root{yyjson_mut_obj(m_doc.get())};
    yyjson_mut_obj_add_val(m_doc.get(), root, "result", value);
    finish(root);
}

void Reply::error(std::int32_t code, std::string_view message)
{
    if (m_id.empty() || !m_doc) {
        return;
    }
    yyjson_mut_doc *const doc{m_doc.get()};
    yyjson_mut_val *const failure{yyjson_mut_obj(doc)};
    yyjson_mut_obj_add_int(doc, failure, "code", code);
    yyjson_mut_obj_add_strncpy(doc, failure, "message", message.data(), message.size());
    yyjson_mut_val *const root{yyjson_mut_obj(doc)};
    yyjson_mut_obj_add_val(doc, root, "error", failure);
    finish(root);
}

void Reply::finish(yyjson_mut_val *root)
{
    yyjson_mut_doc *const doc{m_doc.get()};
    yyjson_mut_obj_add_str(doc, root, "jsonrpc", "2.0");
    yyjson_mut_obj_add_val(doc, root, "id", yyjson_mut_rawncpy(doc, m_id.data(), m_id.size()));
    yyjson_mut_doc_set_root(doc, root);
    std::string message{json::write(doc)};
    m_doc.reset();
    m_id.clear();
    if (m_send) {
        m_send(std::move(message));
    }
}

} // namespace kode::remote
