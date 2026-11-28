#include "Json.h"

namespace kode::remote::json
{

namespace
{

// The written text lives in an allocator of its own, freed with it.
struct AllocatorDeleter {
    void operator()(yyjson_alc *allocator) const noexcept { yyjson_alc_dyn_free(allocator); }
};
using Allocator = std::unique_ptr<yyjson_alc, AllocatorDeleter>;

std::string text(const char *written, std::size_t length)
{
    return written != nullptr ? std::string{written, length} : std::string{};
}

} // namespace

Doc parse(std::string_view text)
{
    return Doc{yyjson_read(text.data(), text.size(), YYJSON_READ_NOFLAG)};
}

MutDoc create()
{
    return MutDoc{yyjson_mut_doc_new(nullptr)};
}

std::string write(const yyjson_val *value)
{
    const Allocator allocator{yyjson_alc_dyn_new()};
    std::size_t length{0U};
    const char *const written{yyjson_val_write_opts(value, YYJSON_WRITE_NOFLAG, allocator.get(), &length, nullptr)};
    return text(written, length);
}

std::string write(const yyjson_mut_doc *doc)
{
    const Allocator allocator{yyjson_alc_dyn_new()};
    std::size_t length{0U};
    const char *const written{yyjson_mut_write_opts(doc, YYJSON_WRITE_NOFLAG, allocator.get(), &length, nullptr)};
    return text(written, length);
}

std::optional<std::string_view> string(yyjson_val *object, const char *key)
{
    yyjson_val *const value{yyjson_obj_get(object, key)};
    if (!yyjson_is_str(value)) {
        return std::nullopt;
    }
    return std::string_view{yyjson_get_str(value), yyjson_get_len(value)};
}

std::optional<std::int64_t> integer(yyjson_val *object, const char *key)
{
    yyjson_val *const value{yyjson_obj_get(object, key)};
    if (!yyjson_is_int(value)) {
        return std::nullopt;
    }
    return yyjson_get_sint(value);
}

} // namespace kode::remote::json
