#include "DocumentHistory.h"

#include <algorithm>
#include <utility>

namespace kode
{

namespace
{

constexpr qsizetype maximumEntries{100};

} // namespace

DocumentHistory::DocumentHistory(QObject *parent) : QObject(parent) {}

QVariantList DocumentHistory::recent() const
{
    QVariantList documents;
    documents.reserve(m_recent.size());
    for (const QPointer<Document> &document : m_recent) {
        if (!document.isNull()) {
            documents.emplace_back(QVariant::fromValue(document.data()));
        }
    }
    return documents;
}

QVariantList DocumentHistory::recentAmong(const QVariantList &documents) const
{
    // By pointer: QVariants holding object pointers do not compare equal reliably.
    const auto pointer{[](const QVariant &document) { return document.value<QObject *>(); }};
    QList<QObject *> seen;
    seen.reserve(m_recent.size());
    std::ranges::transform(m_recent, std::back_inserter(seen), [](const QPointer<Document> &document) {
        return static_cast<QObject *>(document.data());
    });
    QVariantList ordered;
    ordered.reserve(documents.size());
    for (const QObject *document : seen) {
        const auto found{std::ranges::find(documents, document, pointer)};
        if (document != nullptr && found != documents.end()) {
            ordered.emplace_back(*found);
        }
    }
    std::ranges::copy_if(documents, std::back_inserter(ordered), [&seen, &pointer](const QVariant &document) {
        return !seen.contains(pointer(document));
    });
    return ordered;
}

Document *DocumentHistory::current() const
{
    return m_current >= 0 ? m_entries.at(m_current).data() : nullptr;
}

void DocumentHistory::visit(Document *document)
{
    if (document == nullptr) {
        return;
    }
    remove(nullptr);
    const bool touched{touch(document)};
    if (m_current >= 0 && m_entries.at(m_current) == document) {
        if (touched) {
            emit changed();
        }
        return;
    }
    m_entries.resize(m_current + 1);
    m_entries.emplace_back(document);
    if (m_entries.size() > maximumEntries) {
        m_entries.removeFirst();
    }
    m_current = m_entries.size() - 1;
    emit changed();
}

Document *DocumentHistory::back()
{
    remove(nullptr);
    if (!canGoBack()) {
        return nullptr;
    }
    --m_current;
    emit changed();
    return m_entries.at(m_current);
}

Document *DocumentHistory::forward()
{
    remove(nullptr);
    if (!canGoForward()) {
        return nullptr;
    }
    ++m_current;
    emit changed();
    return m_entries.at(m_current);
}

void DocumentHistory::forget(const Document *document)
{
    if (document == nullptr) {
        return;
    }
    remove(document);
    emit changed();
}

// Drops the entries of `document` (null: of deleted documents) and the
// repeats that leaves side by side; the current entry becomes the closest
// one kept before it.
void DocumentHistory::remove(const Document *document)
{
    QList<QPointer<Document>> kept;
    kept.reserve(m_entries.size());
    qsizetype current{-1};
    for (qsizetype index{0}; index < m_entries.size(); ++index) {
        const QPointer<Document> &entry{m_entries.at(index)};
        const bool repeated{!kept.isEmpty() && kept.constLast() == entry};
        if (!entry.isNull() && entry != document && !repeated) {
            kept.emplace_back(entry);
        }
        if (index == m_current) {
            current = kept.size() - 1;
        }
    }
    m_entries = std::move(kept);
    m_recent.removeIf([document](const QPointer<Document> &entry) { return entry.isNull() || entry == document; });
    m_current = m_entries.isEmpty() ? -1 : std::max<qsizetype>(current, 0);
}

bool DocumentHistory::touch(Document *document)
{
    if (!m_recent.isEmpty() && m_recent.constFirst() == document) {
        return false;
    }
    m_recent.removeAll(document);
    m_recent.prepend(document);
    if (m_recent.size() > maximumEntries) {
        m_recent.removeLast();
    }
    return true;
}

} // namespace kode
