#pragma once

#include <QString>

#include <expected>

namespace kode::files
{

// What the sidebar does to files, as the Finder does it: each returns the
// path of the entry it made (or moved), or what went wrong. A name already
// taken in the folder gets a number (" 2", " 3"…) instead of replacing.

// A new, empty "untitled" file (folder) in `folder`.
[[nodiscard]] std::expected<QString, QString> createFile(const QString &folder);
[[nodiscard]] std::expected<QString, QString> createFolder(const QString &folder);
// `path` renamed to `name`, in its folder; refuses a name in use.
[[nodiscard]] std::expected<QString, QString> rename(const QString &path, const QString &name);
// A copy of `path` (a folder with all it holds) in `folder`; "name copy"
// when that is the folder it is in.
[[nodiscard]] std::expected<QString, QString> copyInto(const QString &path, const QString &folder);
// `path` moved into `folder` (not into itself).
[[nodiscard]] std::expected<QString, QString> moveInto(const QString &path, const QString &folder);
[[nodiscard]] std::expected<void, QString> moveToTrash(const QString &path);
// `name` in `folder`, or with `suffix` and a number until it is free
// ("notes.txt" → "notes 2.txt", "notes copy.txt", "notes copy 2.txt").
[[nodiscard]] QString freeName(const QString &folder, const QString &name, const QString &suffix = {});

} // namespace kode::files
