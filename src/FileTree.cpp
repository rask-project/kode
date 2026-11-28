#include "FileTree.h"

#include "FileKinds.h"
#include "FileOperations.h"
#include "remote/Hosts.h"
#include "remote/Ssh.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QProcess>

namespace kode
{

namespace
{

// The folder that holds `path` on a host ("/" for one at the top).
QString parentOf(const QString &path)
{
    const qsizetype slash{path.lastIndexOf(u'/')};
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

QString childOf(const QString &folder, const QString &name)
{
    return (folder == QLatin1String("/") ? QString{} : folder) + u'/' + name;
}

QUrl withPath(QUrl url, const QString &path)
{
    url.setPath(path);
    return url;
}

} // namespace

FileTree::FileTree(QObject *parent) : QSortFilterProxyModel(parent)
{
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    m_files.setReadOnly(true);
    m_files.setFilter(QDir::AllEntries | QDir::AllDirs | QDir::NoDotAndDotDot);
    setSourceModel(&m_files);
    setDynamicSortFilter(true);
    sort(0);
    connect(&m_files, &QFileSystemModel::directoryLoaded, this, [this](const QString &path) {
        emit folderLoaded(QUrl::fromLocalFile(path));
    });
    connect(&m_remote, &remote::RemoteTree::folderLoaded, this, [this](const QString &path) {
        if (m_remote.host() != nullptr) {
            emit folderLoaded(m_remote.host()->fileUrl(path));
        }
    });
}

FileTree::~FileTree()
{
    setSourceModel(nullptr);
}

void FileTree::setFolder(const QUrl &url)
{
    // Without a trailing slash (the Finder sends one): paths below it are compared.
    QUrl folder;
    if (url.isLocalFile()) {
        folder = QUrl::fromLocalFile(QDir::cleanPath(url.toLocalFile()));
    } else if (!remote::ssh::destinationOf(url).isEmpty()) {
        folder = remote::ssh::fileUrl(remote::ssh::destinationOf(url), QDir::cleanPath(url.path()));
    }
    if (folder == m_folder) {
        return;
    }
    m_folder = folder;
    if (remote()) {
        m_files.setRootPath({});
        m_remote.setShowHidden(showHidden());
        m_remote.setRoot(remote::Hosts::shared().forUrl(folder), folder.path());
        if (sourceModel() != &m_remote) {
            setSourceModel(&m_remote);
        }
    } else {
        m_remote.setRoot(nullptr, {});
        if (sourceModel() != &m_files) {
            setSourceModel(&m_files);
        }
        m_files.setRootPath(folder.toLocalFile());
    }
    emit folderChanged();
    emit rootIndexChanged();
}

void FileTree::setShowHidden(bool show)
{
    if (show == showHidden()) {
        return;
    }
    m_files.setFilter(m_files.filter() ^ QDir::Hidden);
    m_remote.setShowHidden(show);
    emit showHiddenChanged();
    emit rootIndexChanged();
}

QUrl FileTree::parentFolder() const
{
    if (remote()) {
        return m_folder.path() == QLatin1String("/") ? QUrl{} : withPath(m_folder, parentOf(m_folder.path()));
    }
    const QString folder{m_folder.toLocalFile()};
    if (folder.isEmpty() || QDir{folder}.isRoot()) {
        return {};
    }
    return QUrl::fromLocalFile(QFileInfo{folder}.absolutePath());
}

bool FileTree::showHidden() const
{
    return m_files.filter().testFlag(QDir::Hidden);
}

QModelIndex FileTree::rootIndex() const
{
    // A host's folder is the remote tree's top.
    if (!m_folder.isLocalFile()) {
        return {};
    }
    return mapFromSource(m_files.index(m_folder.toLocalFile()));
}

bool FileTree::contains(const QUrl &file) const
{
    if (remote()) {
        const QString root{m_folder.path() == QLatin1String("/") ? QString{} : m_folder.path()};
        return remote::ssh::destinationOf(file) == remote::ssh::destinationOf(m_folder) &&
               file.path().startsWith(root + u'/') && file.path().size() > root.size() + 1;
    }
    const QString folder{m_folder.toLocalFile()};
    return !folder.isEmpty() && file.toLocalFile().startsWith(folder + QLatin1Char('/'));
}

QModelIndex FileTree::indexOf(const QUrl &file)
{
    if (!contains(file)) {
        return {};
    }
    if (remote()) {
        return mapFromSource(m_remote.indexOf(file.path()));
    }
    const QString folder{m_folder.toLocalFile()};
    const QString path{file.toLocalFile()};
    for (qsizetype slash{path.indexOf(QLatin1Char('/'), folder.size() + 1)}; slash >= 0;
         slash = path.indexOf(QLatin1Char('/'), slash + 1)) {
        m_files.fetchMore(m_files.index(path.left(slash)));
    }
    return mapFromSource(m_files.index(path));
}

bool FileTree::isFolder(const QUrl &url) const
{
    return remote() ? m_remote.knowsFolder(url.path()) : QFileInfo{url.toLocalFile()}.isDir();
}

QUrl FileTree::folderFor(const QUrl &url) const
{
    if (remote()) {
        return m_remote.knowsFolder(url.path()) ? url : withPath(url, parentOf(url.path()));
    }
    const QFileInfo info{url.toLocalFile()};
    return QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
}

namespace
{

// The URL of `made`, or an empty one after reporting its error through `report`.
template <typename Report> QUrl urlOf(const std::expected<QString, QString> &made, Report report)
{
    if (!made) {
        report(made.error());
        return {};
    }
    return QUrl::fromLocalFile(*made);
}

} // namespace

QUrl FileTree::createFile(const QUrl &folder)
{
    if (remote()) {
        return remoteCreate(folder, false);
    }
    return urlOf(files::createFile(folder.toLocalFile()), [this](const QString &error) { emit errorOccurred(error); });
}

QUrl FileTree::createFolder(const QUrl &folder)
{
    if (remote()) {
        return remoteCreate(folder, true);
    }
    return urlOf(files::createFolder(folder.toLocalFile()), [this](const QString &error) {
        emit errorOccurred(error);
    });
}

QUrl FileTree::rename(const QUrl &url, const QString &name)
{
    if (remote()) {
        return remoteRename(url, name);
    }
    const QUrl renamed{urlOf(files::rename(url.toLocalFile(), name), [this](const QString &error) {
        emit errorOccurred(error);
    })};
    if (!renamed.isEmpty() && renamed != url) {
        emit moved(url, renamed);
    }
    return renamed;
}

QUrl FileTree::duplicate(const QUrl &url)
{
    if (remote()) {
        return remoteCopy(url, folderFor(url).path(), false);
    }
    const QString path{url.toLocalFile()};
    return urlOf(files::copyInto(path, QFileInfo{path}.absolutePath()), [this](const QString &error) {
        emit errorOccurred(error);
    });
}

bool FileTree::moveToTrash(const QUrl &url)
{
    if (remote()) {
        const QString folder{folderFor(url).path()};
        m_remote.remove(url.path());
        ask(QStringLiteral("fs/trash"), {{QStringLiteral("path"), url.path()}}, folder);
        return true;
    }
    if (const std::expected<void, QString> trashed{files::moveToTrash(url.toLocalFile())}; !trashed) {
        emit errorOccurred(trashed.error());
        return false;
    }
    return true;
}

void FileTree::copy(const QUrl &url, bool cut)
{
    auto mime{std::make_unique<QMimeData>()};
    mime->setUrls({url});
    mime->setText(url.isLocalFile() ? url.toLocalFile() : url.toString());
    // The clipboard owns the data.
    QGuiApplication::clipboard()->setMimeData(mime.release());
    m_cut = cut ? QList<QUrl>{url} : QList<QUrl>{};
}

bool FileTree::canPaste()
{
    const QMimeData *const mime{QGuiApplication::clipboard()->mimeData()};
    return mime != nullptr && std::ranges::any_of(mime->urls(), [](const QUrl &url) {
               return url.isLocalFile() || url.scheme() == QLatin1String("ssh");
           });
}

QUrl FileTree::paste(const QUrl &folder)
{
    const QMimeData *const mime{QGuiApplication::clipboard()->mimeData()};
    if (mime == nullptr) {
        return {};
    }
    const QList<QUrl> urls{mime->urls()};
    const bool cut{!m_cut.isEmpty() && urls == m_cut};
    const auto report{[this](const QString &error) { emit errorOccurred(error); }};
    QUrl last;
    for (const QUrl &url : urls) {
        if (remote()) {
            // Only within the host: nothing crosses between machines here.
            if (remote::ssh::destinationOf(url) != remote::ssh::destinationOf(m_folder)) {
                report(tr("“%1” is not on this host: it can only be pasted where it is.").arg(url.fileName()));
                continue;
            }
            const QUrl pasted{remoteCopy(url, folder.path(), cut)};
            last = pasted.isEmpty() ? last : pasted;
            continue;
        }
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path{url.toLocalFile()};
        const QUrl pasted{
            cut ? urlOf(files::moveInto(path, folder.toLocalFile()), report)
                : urlOf(files::copyInto(path, folder.toLocalFile()), report)
        };
        if (pasted.isEmpty()) {
            continue;
        }
        if (cut && pasted != url) {
            emit moved(url, pasted);
        }
        last = pasted;
    }
    if (cut) {
        m_cut.clear();
        QGuiApplication::clipboard()->clear();
    }
    return last;
}

void FileTree::copyPath(const QUrl &url)
{
    // A host's file: its path there.
    QGuiApplication::clipboard()->setText(url.isLocalFile() ? url.toLocalFile() : url.path());
}

void FileTree::revealInFinder(const QUrl &url)
{
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), url.toLocalFile()});
}

QString FileTree::iconSource(const QString &path, bool folder)
{
    if (!folder) {
        if (const std::optional<FileKind> kind{fileKind(path.sliced(path.lastIndexOf(u'/') + 1))}) {
            return QStringLiteral("image://codeicon/") + kind->key;
        }
    }
    return QStringLiteral("image://fileicon") + QString::fromLatin1(QUrl::toPercentEncoding(path, "/"));
}

bool FileTree::isFolderAt(const QModelIndex &source) const
{
    return remote() ? m_remote.isFolder(source) : m_files.isDir(source);
}

QString FileTree::nameAt(const QModelIndex &source) const
{
    return remote() ? m_remote.name(source) : m_files.fileName(source);
}

QUrl FileTree::urlAt(const QModelIndex &source) const
{
    if (remote()) {
        return m_remote.host() != nullptr ? m_remote.host()->fileUrl(m_remote.path(source)) : QUrl{};
    }
    return QUrl::fromLocalFile(m_files.filePath(source));
}

QVariant FileTree::data(const QModelIndex &index, qint32 role) const
{
    switch (role) {
    case IconSourceRole: {
        const QModelIndex source{mapToSource(index)};
        if (!remote()) {
            return iconSource(m_files.filePath(source), m_files.isDir(source));
        }
        // Not on this Mac: the system's folder or file icon, or a code file's.
        if (m_remote.isFolder(source)) {
            return QStringLiteral("image://fileicon/:folder");
        }
        const QString icon{iconSource(m_remote.name(source), false)};
        return icon.startsWith(QLatin1String("image://codeicon/")) ? icon : QStringLiteral("image://fileicon/:file");
    }
    case IsFolderRole:
        return isFolderAt(mapToSource(index));
    case QFileSystemModel::FileNameRole:
        return nameAt(mapToSource(index));
    case FileUrlRole:
        return urlAt(mapToSource(index));
    default:
        return QSortFilterProxyModel::data(index, role);
    }
}

remote::Connection *FileTree::connection()
{
    remote::Host *const host{m_remote.host()};
    remote::Connection *const connection{host != nullptr ? host->connection() : nullptr};
    if (connection == nullptr) {
        emit errorOccurred(tr("The host is not connected."));
    }
    return connection;
}

QString FileTree::remoteFreeName(const QString &folder, const QString &name, const QString &suffix) const
{
    const qsizetype dot{name.lastIndexOf(u'.')};
    const bool split{dot > 0 && !m_remote.knowsFolder(childOf(folder, name))};
    const QString base{split ? name.left(dot) : name};
    const QString extension{split ? name.mid(dot) : QString{}};
    QString candidate{suffix.isEmpty() ? name : base + suffix + extension};
    for (qint32 number{2}; m_remote.has(folder, candidate); ++number) {
        candidate = base + suffix + u' ' + QString::number(number) + extension;
    }
    return candidate;
}

void FileTree::ask(
    const QString &method,
    const QJsonObject &params,
    const QString &folder,
    const QUrl &from,
    const QUrl &to
)
{
    remote::Connection *const link{connection()};
    if (link == nullptr) {
        m_remote.refresh(folder);
        return;
    }
    const QPointer<FileTree> self{this};
    link->request(method, params, [self, folder, from, to](const remote::Connection::Result &result) {
        if (self == nullptr) {
            return;
        }
        if (!result) {
            emit self->errorOccurred(result.error().message);
            self->m_remote.refresh(folder);
            return;
        }
        if (!from.isEmpty()) {
            emit self->moved(from, to);
        }
    });
}

QUrl FileTree::remoteCreate(const QUrl &folder, bool isFolder)
{
    const QString dir{folder.path()};
    if (m_remote.host() == nullptr || !m_remote.knowsFolder(dir)) {
        return {};
    }
    const QString name{remoteFreeName(dir, isFolder ? tr("untitled folder") : tr("untitled"))};
    const QString path{childOf(dir, name)};
    m_remote.insert(dir, name, isFolder);
    ask(QStringLiteral("fs/create"), {{QStringLiteral("path"), path}, {QStringLiteral("folder"), isFolder}}, dir);
    return m_remote.host()->fileUrl(path);
}

QUrl FileTree::remoteRename(const QUrl &url, const QString &name)
{
    const QString trimmed{name.trimmed()};
    if (trimmed.isEmpty() || trimmed.contains(u'/') || trimmed == QLatin1String(".") ||
        trimmed == QLatin1String("..")) {
        emit errorOccurred(tr("“%1” is not a valid name.").arg(name));
        return {};
    }
    const QString parent{parentOf(url.path())};
    if (trimmed == url.fileName()) {
        return url;
    }
    if (m_remote.has(parent, trimmed)) {
        emit errorOccurred(tr("The name “%1” is already taken.").arg(trimmed));
        return {};
    }
    const QString to{childOf(parent, trimmed)};
    const QUrl renamed{m_remote.host()->fileUrl(to)};
    m_remote.rename(url.path(), trimmed);
    ask(QStringLiteral("fs/rename"),
        {{QStringLiteral("from"), url.path()}, {QStringLiteral("to"), to}},
        parent,
        url,
        renamed);
    return renamed;
}

QUrl FileTree::remoteCopy(const QUrl &url, const QString &folder, bool move)
{
    const QString parent{parentOf(url.path())};
    if (move && parent == folder) {
        return url;
    }
    const QString name{remoteFreeName(folder, url.fileName(), !move && parent == folder ? tr(" copy") : QString{})};
    const QString to{childOf(folder, name)};
    const QUrl made{m_remote.host()->fileUrl(to)};
    m_remote.insert(folder, name, m_remote.knowsFolder(url.path()));
    if (move) {
        m_remote.remove(url.path());
    }
    const QJsonObject params{{QStringLiteral("from"), url.path()}, {QStringLiteral("to"), to}};
    if (!move) {
        ask(QStringLiteral("fs/copy"), params, folder);
        return made;
    }
    // Moved: the documents of it follow.
    ask(QStringLiteral("fs/rename"), params, folder, url, made);
    return made;
}

QHash<qint32, QByteArray> FileTree::roleNames() const
{
    QHash<qint32, QByteArray> names{QSortFilterProxyModel::roleNames()};
    // Its own, not the source's: a host's tree has no QFileSystemModel roles.
    names.insert(QFileSystemModel::FileNameRole, QByteArrayLiteral("fileName"));
    names.insert(IconSourceRole, QByteArrayLiteral("iconSource"));
    names.insert(IsFolderRole, QByteArrayLiteral("isFolder"));
    names.insert(FileUrlRole, QByteArrayLiteral("fileUrl"));
    return names;
}

bool FileTree::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const bool leftFolder{isFolderAt(left)};
    if (leftFolder != isFolderAt(right)) {
        return leftFolder;
    }
    return m_collator.compare(nameAt(left), nameAt(right)) < 0;
}

bool FileTree::filterAcceptsColumn(qint32 column, const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return column == 0;
}

} // namespace kode
