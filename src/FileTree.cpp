#include "FileTree.h"

#include "FileKinds.h"
#include "FileOperations.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>

namespace kode
{

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
}

FileTree::~FileTree()
{
    setSourceModel(nullptr);
}

void FileTree::setFolder(const QUrl &url)
{
    // Without a trailing slash (the Finder sends one): paths below it are compared.
    const QUrl folder{url.isLocalFile() ? QUrl::fromLocalFile(QDir::cleanPath(url.toLocalFile())) : QUrl{}};
    if (folder == m_folder) {
        return;
    }
    m_folder = folder;
    m_files.setRootPath(folder.toLocalFile());
    emit folderChanged();
    emit rootIndexChanged();
}

void FileTree::setShowHidden(bool show)
{
    if (show == showHidden()) {
        return;
    }
    m_files.setFilter(m_files.filter() ^ QDir::Hidden);
    emit showHiddenChanged();
    emit rootIndexChanged();
}

QUrl FileTree::parentFolder() const
{
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
    if (!m_folder.isLocalFile()) {
        return {};
    }
    return mapFromSource(m_files.index(m_folder.toLocalFile()));
}

bool FileTree::contains(const QUrl &file) const
{
    const QString folder{m_folder.toLocalFile()};
    return !folder.isEmpty() && file.toLocalFile().startsWith(folder + QLatin1Char('/'));
}

QModelIndex FileTree::indexOf(const QUrl &file)
{
    if (!contains(file)) {
        return {};
    }
    const QString folder{m_folder.toLocalFile()};
    const QString path{file.toLocalFile()};
    for (qsizetype slash{path.indexOf(QLatin1Char('/'), folder.size() + 1)}; slash >= 0;
         slash = path.indexOf(QLatin1Char('/'), slash + 1)) {
        m_files.fetchMore(m_files.index(path.left(slash)));
    }
    return mapFromSource(m_files.index(path));
}

QUrl FileTree::folderFor(const QUrl &url)
{
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
    return urlOf(files::createFile(folder.toLocalFile()), [this](const QString &error) { emit errorOccurred(error); });
}

QUrl FileTree::createFolder(const QUrl &folder)
{
    return urlOf(files::createFolder(folder.toLocalFile()), [this](const QString &error) {
        emit errorOccurred(error);
    });
}

QUrl FileTree::rename(const QUrl &url, const QString &name)
{
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
    const QString path{url.toLocalFile()};
    return urlOf(files::copyInto(path, QFileInfo{path}.absolutePath()), [this](const QString &error) {
        emit errorOccurred(error);
    });
}

bool FileTree::moveToTrash(const QUrl &url)
{
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
    mime->setText(url.toLocalFile());
    // The clipboard owns the data.
    QGuiApplication::clipboard()->setMimeData(mime.release());
    m_cut = cut ? QList<QUrl>{url} : QList<QUrl>{};
}

bool FileTree::canPaste()
{
    const QMimeData *const mime{QGuiApplication::clipboard()->mimeData()};
    return mime != nullptr && std::ranges::any_of(mime->urls(), &QUrl::isLocalFile);
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
    QGuiApplication::clipboard()->setText(url.toLocalFile());
}

void FileTree::revealInFinder(const QUrl &url)
{
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), url.toLocalFile()});
}

QVariant FileTree::data(const QModelIndex &index, qint32 role) const
{
    switch (role) {
    case IconSourceRole: {
        const QModelIndex source{mapToSource(index)};
        if (!m_files.isDir(source)) {
            if (const std::optional<FileKind> kind{fileKind(m_files.fileName(source))}) {
                return QStringLiteral("image://codeicon/") + kind->key;
            }
        }
        return QStringLiteral("image://fileicon") +
               QString::fromLatin1(QUrl::toPercentEncoding(m_files.filePath(source), "/"));
    }
    case IsFolderRole:
        return m_files.isDir(mapToSource(index));
    case FileUrlRole:
        return QUrl::fromLocalFile(m_files.filePath(mapToSource(index)));
    default:
        return QSortFilterProxyModel::data(index, role);
    }
}

QHash<qint32, QByteArray> FileTree::roleNames() const
{
    QHash<qint32, QByteArray> names{QSortFilterProxyModel::roleNames()};
    names.insert(IconSourceRole, QByteArrayLiteral("iconSource"));
    names.insert(IsFolderRole, QByteArrayLiteral("isFolder"));
    names.insert(FileUrlRole, QByteArrayLiteral("fileUrl"));
    return names;
}

bool FileTree::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    const bool leftFolder{m_files.isDir(left)};
    if (leftFolder != m_files.isDir(right)) {
        return leftFolder;
    }
    return m_collator.compare(m_files.fileName(left), m_files.fileName(right)) < 0;
}

bool FileTree::filterAcceptsColumn(qint32 column, const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return column == 0;
}

} // namespace kode
