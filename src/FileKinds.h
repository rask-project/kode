#pragma once

#include <QColor>
#include <QStringView>

#include <optional>

namespace kode
{

// The language of a code file, for its icon in the sidebar: a short label
// on a colored badge (the system has no icons by language).
struct FileKind {
    // Names the icon: "image://codeicon/<key>".
    QLatin1StringView key;
    QLatin1StringView label;
    QColor color;
};

// By extension (case aside) or by a whole name (CMakeLists.txt, Makefile…).
[[nodiscard]] std::optional<FileKind> fileKind(QStringView fileName);
// The kind named `key`.
[[nodiscard]] std::optional<FileKind> fileKindByKey(QStringView key);

} // namespace kode
