#pragma once

#include <QString>

namespace kode
{

// Whether the system knows `fileName`'s extension as a type that is not text (PDF, images, archives…).
[[nodiscard]] bool isKnownNonTextType(const QString &fileName);

} // namespace kode
