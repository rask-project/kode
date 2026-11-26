#pragma once

#include <QQuickImageProvider>

namespace kode
{

// "image://fileicon/<path>": the icon the system shows for that file or
// folder (the Finder's), at the size asked for. A "?v=<n>" after it is
// ignored: changing it loads the icon again.
class FileIcons : public QQuickImageProvider
{
public:
    FileIcons();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// "image://codeicon/<key>": a code file's icon (FileKinds): the system's
// document icon with a badge of its language's label and color, as Xcode's
// document icons. One per language, shared by its files.
class CodeIcons : public QQuickImageProvider
{
public:
    CodeIcons();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

} // namespace kode
