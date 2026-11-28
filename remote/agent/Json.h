#pragma once

#include <yyjson.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace kode::remote::json
{

struct DocDeleter {
    void operator()(yyjson_doc *doc) const noexcept { yyjson_doc_free(doc); }
};
struct MutDocDeleter {
    void operator()(yyjson_mut_doc *doc) const noexcept { yyjson_mut_doc_free(doc); }
};
using Doc = std::unique_ptr<yyjson_doc, DocDeleter>;
using MutDoc = std::unique_ptr<yyjson_mut_doc, MutDocDeleter>;

[[nodiscard]] Doc parse(std::string_view text);
[[nodiscard]] MutDoc create();
// `value` as JSON text.
[[nodiscard]] std::string write(const yyjson_val *value);
[[nodiscard]] std::string write(const yyjson_mut_doc *doc);

[[nodiscard]] std::optional<std::string_view> string(yyjson_val *object, const char *key);
[[nodiscard]] std::optional<std::int64_t> integer(yyjson_val *object, const char *key);

} // namespace kode::remote::json
