#include "OpenDocuments.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlEngine>

#include "FileTypes.h"
#include "TextCodec.h"

#include <algorithm>

namespace kode
{

namespace
{

const QString commandScheme{QStringLiteral("kode")};

// A `kode:///path` URL as the file it names.
QUrl localFile(const QUrl &url)
{
    if (url.scheme() != commandScheme || !url.path().startsWith(QLatin1Char('/'))) {
        return url;
    }
    return QUrl::fromLocalFile(QDir::cleanPath(url.path()));
}

// Not text: a NUL byte in its first 8000 bytes (as git tells), or a type the
// system knows as not text (PDF…) and bytes that are not UTF-8 (a TypeScript
// .ts is an MPEG stream to the system).
bool isBinaryFile(const QString &path)
{
    constexpr qint64 headSize{8000};
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray head{file.read(headSize)};
    return codec::isBinary(head) || (isKnownNonTextType(path) && !codec::isUtf8Prefix(head));
}

QVariantList toList(const std::vector<Document *> &documents)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(documents.size()));
    std::ranges::transform(documents, std::back_inserter(list), [](Document *document) {
        return QVariant::fromValue(document);
    });
    return list;
}

} // namespace

OpenDocuments::OpenDocuments(QObject *parent) : QObject(parent)
{
    connect(&m_languageServers, &lsp::LanguageServers::locationsFound, this, &OpenDocuments::locationsFound);
    connect(&m_languageServers, &lsp::LanguageServers::codeActionsFound, this, &OpenDocuments::codeActionsFound);
    // An edit to a file not open (a fix across files): opened for it.
    m_languageServers.setOpener([this](const QUrl &file) { return open(file); });
}

qint32 OpenDocuments::requestCodeActions(Document *document, qint64 start, qint64 end)
{
    return m_languageServers.requestCodeActions(document, std::min(start, end), std::max(start, end));
}

void OpenDocuments::applyCodeAction(qint32 ticket, qint32 index)
{
    m_languageServers.applyCodeAction(ticket, index);
}

qint32 OpenDocuments::findDefinition(Document *document, qint64 position)
{
    return m_languageServers.findDefinition(document, position);
}

qint32 OpenDocuments::switchHeaderSource(Document *document)
{
    return m_languageServers.switchHeaderSource(document);
}

OpenDocuments::~OpenDocuments() = default;

QVariantList OpenDocuments::documents() const
{
    std::vector<Document *> documents;
    std::ranges::transform(m_documents, std::back_inserter(documents), &Pointer::get);
    return toList(documents);
}

QVariantList OpenDocuments::sortedDocuments() const
{
    std::vector<Document *> documents;
    std::ranges::transform(m_documents, std::back_inserter(documents), &Pointer::get);
    std::ranges::sort(documents, [](const Document *a, const Document *b) {
        return a->fileName().localeAwareCompare(b->fileName()) < 0;
    });
    return toList(documents);
}

Document *OpenDocuments::find(const QUrl &file) const
{
    const auto found{std::ranges::find_if(m_documents, [&file](const Pointer &document) {
        return !file.isEmpty() && document->fileUrl() == file;
    })};
    return found != m_documents.end() ? found->get() : nullptr;
}

Document *OpenDocuments::open(const QUrl &file)
{
    const QUrl local{localFile(file)};
    if (Document *const open{find(local)}) {
        return open;
    }
    Pointer document{std::make_unique<Document>().release()};
    connect(document.get(), &Document::errorOccurred, this, &OpenDocuments::errorOccurred);
    if (file.scheme() == commandScheme && local.isLocalFile() && !QFileInfo::exists(local.toLocalFile())) {
        document->startFile(local);
        return adopt(std::move(document));
    }
    // Not text: the system's app for it opens it, as a double click in the Finder.
    if (local.isLocalFile() && isBinaryFile(local.toLocalFile())) {
        QDesktopServices::openUrl(local);
        return nullptr;
    }
    if (!document->load(local)) {
        return nullptr;
    }
    return adopt(std::move(document));
}

bool OpenDocuments::isFolder(const QUrl &url)
{
    // A host's folder is named with a trailing slash (`kode ssh://box/src/`): there is no asking it here.
    if (url.scheme() == QLatin1String("ssh")) {
        return url.path().endsWith(u'/');
    }
    return url.isLocalFile() && QFileInfo{url.toLocalFile()}.isDir();
}

bool OpenDocuments::isRemote(const QUrl &url)
{
    return url.scheme() == QLatin1String("ssh");
}

void OpenDocuments::fileMoved(const QUrl &from, const QUrl &to)
{
    // By path on the same machine (this Mac, or one host): file:// and ssh:// alike.
    const QString fromPath{from.path()};
    for (const Pointer &document : m_documents) {
        const QUrl url{document->fileUrl()};
        if (url.isEmpty() || url.scheme() != from.scheme() || url.authority() != from.authority()) {
            continue;
        }
        const QString path{url.path()};
        if (path == fromPath) {
            document->followMove(to);
        } else if (path.startsWith(fromPath + QLatin1Char('/'))) {
            QUrl moved{to};
            moved.setPath(to.path() + path.mid(fromPath.size()));
            document->followMove(moved);
        }
    }
    emit documentsChanged();
}

QUrl OpenDocuments::cleanFolder(const QUrl &folder)
{
    if (isRemote(folder)) {
        QUrl clean{folder};
        clean.setPath(
            folder.path().size() > 1 && folder.path().endsWith(u'/') ? folder.path().chopped(1) : folder.path()
        );
        return clean;
    }
    if (!folder.isLocalFile()) {
        return folder;
    }
    // The real path: the Finder sends /private/tmp for /tmp, and the folder keys its session.
    const QString canonical{QFileInfo{folder.toLocalFile()}.canonicalFilePath()};
    return QUrl::fromLocalFile(canonical.isEmpty() ? QDir::cleanPath(folder.toLocalFile()) : canonical);
}

void OpenDocuments::restore(const QStringList &files)
{
    for (const QString &file : files) {
        const QUrl url{file};
        // A remote file is asked for: it opens if it is there (see Document).
        if ((url.isLocalFile() && QFileInfo::exists(url.toLocalFile())) || url.scheme() == QLatin1String("ssh")) {
            open(url);
        }
    }
}

Document *OpenDocuments::create()
{
    return adopt(Pointer{std::make_unique<Document>().release()});
}

Document *OpenDocuments::adopt(Pointer document)
{
    Document *const adopted{document.get()};
    QQmlEngine::setObjectOwnership(adopted, QQmlEngine::CppOwnership);
    disconnect(adopted, nullptr, this, nullptr);
    connect(adopted, &Document::errorOccurred, this, &OpenDocuments::errorOccurred);
    connect(adopted, &Document::changedOnDisk, this, [this, adopted] { emit changedOnDisk(adopted); });
    // A large file that could not be read after all: it was never open.
    connect(adopted, &Document::loadFailed, this, [this, adopted] { close(adopted); });
    // Saving an untitled document names it: the others are numbered again.
    connect(adopted, &Document::fileUrlChanged, this, [this] {
        numberUntitled();
        emit documentsChanged();
    });
    m_documents.insert(m_documents.begin(), std::move(document));
    m_languageServers.add(adopted);
    numberUntitled();
    emit documentsChanged();
    return adopted;
}

void OpenDocuments::numberUntitled()
{
    // Oldest first: a new untitled document gets the next number.
    qint32 number{0};
    for (const Pointer &document : std::ranges::reverse_view(m_documents)) {
        if (document->fileUrl().isEmpty()) {
            document->setUntitledNumber(++number);
        }
    }
}

void OpenDocuments::close(Document *document)
{
    const auto found{std::ranges::find(m_documents, document, &Pointer::get)};
    if (found == m_documents.end()) {
        return;
    }
    emit documentClosing(document);
    m_languageServers.remove(document);
    const Pointer closed{std::move(*found)};
    m_documents.erase(found);
    disconnect(closed.get(), nullptr, this, nullptr);
    numberUntitled();
    emit documentsChanged();
}

void OpenDocuments::touch(Document *document)
{
    const auto found{std::ranges::find(m_documents, document, &Pointer::get)};
    if (found == m_documents.end() || found == m_documents.begin()) {
        return;
    }
    std::ranges::rotate(m_documents.begin(), found, std::next(found));
    emit documentsChanged();
}

Document *OpenDocuments::other(const Document *document) const
{
    const auto found{std::ranges::find_if(m_documents, [document](const Pointer &candidate) {
        return candidate.get() != document;
    })};
    return found != m_documents.end() ? found->get() : nullptr;
}

QVariantList OpenDocuments::documentsIn(const QUrl &url) const
{
    // By path on the same machine (this Mac, or one host).
    const QString root{url.path()};
    std::vector<Document *> found;
    for (const Pointer &document : m_documents) {
        const QUrl file{document->fileUrl()};
        if (root.isEmpty() || file.isEmpty() || file.scheme() != url.scheme() || file.authority() != url.authority()) {
            continue;
        }
        const QString path{file.path()};
        if (path == root || path.startsWith(root + QLatin1Char('/'))) {
            found.emplace_back(document.get());
        }
    }
    return toList(found);
}

QVariantList OpenDocuments::modifiedDocuments() const
{
    std::vector<Document *> modified;
    for (const Pointer &document : m_documents) {
        if (document->modified()) {
            modified.emplace_back(document.get());
        }
    }
    return toList(modified);
}

} // namespace kode
