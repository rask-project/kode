#include "FileOperations.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace kode::files
{

namespace
{

QString tr(const char *text)
{
    return QCoreApplication::translate("kode::files", text);
}

// "notes.txt" → {"notes", ".txt"}; a folder, or a name that is all extension (".gitignore"), has none.
std::pair<QString, QString> splitName(const QString &folder, const QString &name)
{
    const qsizetype dot{name.lastIndexOf(QLatin1Char('.'))};
    if (dot <= 0 || QFileInfo{QDir{folder}.filePath(name)}.isDir()) {
        return {name, QString{}};
    }
    return {name.left(dot), name.mid(dot)};
}

std::expected<void, QString> copyFolder(const QString &from, const QString &to)
{
    if (!QDir{}.mkpath(to)) {
        return std::unexpected{tr("“%1” could not be created.").arg(to)};
    }
    const QDir source{from};
    QDirIterator entries{
        from,
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories
    };
    while (entries.hasNext()) {
        const QFileInfo entry{entries.nextFileInfo()};
        const QString target{QDir{to}.filePath(source.relativeFilePath(entry.filePath()))};
        const bool copied{
            entry.isDir() && !entry.isSymLink() ? QDir{}.mkpath(target) : QFile::copy(entry.filePath(), target)
        };
        if (!copied) {
            return std::unexpected{tr("“%1” could not be copied.").arg(entry.fileName())};
        }
    }
    return {};
}

} // namespace

QString freeName(const QString &folder, const QString &name, const QString &suffix)
{
    const QDir dir{folder};
    const auto [base, extension]{splitName(folder, name)};
    QString candidate{suffix.isEmpty() ? name : base + suffix + extension};
    for (qint32 number{2}; QFileInfo::exists(dir.filePath(candidate)); ++number) {
        candidate = base + suffix + QLatin1Char(' ') + QString::number(number) + extension;
    }
    return candidate;
}

std::expected<QString, QString> createFile(const QString &folder)
{
    const QString path{QDir{folder}.filePath(freeName(folder, tr("untitled")))};
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return std::unexpected{file.errorString()};
    }
    return path;
}

std::expected<QString, QString> createFolder(const QString &folder)
{
    const QString path{QDir{folder}.filePath(freeName(folder, tr("untitled folder")))};
    if (!QDir{}.mkdir(path)) {
        return std::unexpected{tr("“%1” could not be created.").arg(QFileInfo{path}.fileName())};
    }
    return path;
}

std::expected<QString, QString> rename(const QString &path, const QString &name)
{
    const QString trimmed{name.trimmed()};
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/')) || trimmed == QLatin1String(".") ||
        trimmed == QLatin1String("..")) {
        return std::unexpected{tr("“%1” is not a valid name.").arg(name)};
    }
    const QFileInfo info{path};
    const QString target{info.dir().filePath(trimmed)};
    if (target == path) {
        return path;
    }
    // A change of case only is the same entry on a case-insensitive disk.
    if (QFileInfo::exists(target) && target.compare(path, Qt::CaseInsensitive) != 0) {
        return std::unexpected{tr("The name “%1” is already taken.").arg(trimmed)};
    }
    if (!QDir{}.rename(path, target)) {
        return std::unexpected{tr("“%1” could not be renamed.").arg(info.fileName())};
    }
    return target;
}

std::expected<QString, QString> copyInto(const QString &path, const QString &folder)
{
    const QFileInfo info{path};
    const bool sameFolder{info.absolutePath() == QDir{folder}.absolutePath()};
    const QString target{QDir{folder}.filePath(freeName(folder, info.fileName(), sameFolder ? tr(" copy") : QString{}))
    };
    if (info.isDir() && !info.isSymLink()) {
        if (QDir::cleanPath(folder).startsWith(QDir::cleanPath(path) + QLatin1Char('/'))) {
            return std::unexpected{tr("“%1” cannot be copied into itself.").arg(info.fileName())};
        }
        if (const std::expected<void, QString> copied{copyFolder(path, target)}; !copied) {
            return std::unexpected{copied.error()};
        }
        return target;
    }
    if (!QFile::copy(path, target)) {
        return std::unexpected{tr("“%1” could not be copied.").arg(info.fileName())};
    }
    return target;
}

std::expected<QString, QString> moveInto(const QString &path, const QString &folder)
{
    const QFileInfo info{path};
    const QString cleanFolder{QDir::cleanPath(folder)};
    if (info.absolutePath() == cleanFolder) {
        return path;
    }
    if (cleanFolder == QDir::cleanPath(path) || cleanFolder.startsWith(QDir::cleanPath(path) + QLatin1Char('/'))) {
        return std::unexpected{tr("“%1” cannot be moved into itself.").arg(info.fileName())};
    }
    const QString target{QDir{folder}.filePath(freeName(folder, info.fileName()))};
    if (!QDir{}.rename(path, target)) {
        return std::unexpected{tr("“%1” could not be moved.").arg(info.fileName())};
    }
    return target;
}

std::expected<void, QString> moveToTrash(const QString &path)
{
    if (!QFile::moveToTrash(path)) {
        return std::unexpected{tr("“%1” could not be moved to the Trash.").arg(QFileInfo{path}.fileName())};
    }
    return {};
}

} // namespace kode::files
