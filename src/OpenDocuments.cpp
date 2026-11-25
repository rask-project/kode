#include "OpenDocuments.h"

#include <QFileInfo>
#include <QQmlEngine>

#include <algorithm>

namespace tekst
{

namespace
{

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

OpenDocuments::OpenDocuments(QObject *parent) : QObject(parent) {}

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
    if (Document *const open{find(file)}) {
        return open;
    }
    Pointer document{std::make_unique<Document>().release()};
    connect(document.get(), &Document::errorOccurred, this, &OpenDocuments::errorOccurred);
    if (!document->load(file)) {
        return nullptr;
    }
    return adopt(std::move(document));
}

void OpenDocuments::restore(const QStringList &files)
{
    for (const QString &file : files) {
        const QUrl url{file};
        if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile())) {
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
    // Saving an untitled document names it: the others are numbered again.
    connect(adopted, &Document::fileUrlChanged, this, [this] {
        numberUntitled();
        emit documentsChanged();
    });
    m_documents.insert(m_documents.begin(), std::move(document));
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

} // namespace tekst
