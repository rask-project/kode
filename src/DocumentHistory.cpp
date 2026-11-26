#include "DocumentHistory.h"

#include <algorithm>
#include <utility>

namespace tekst
{

namespace
{

constexpr qsizetype maximumEntries{100};

} // namespace

DocumentHistory::DocumentHistory(QObject *parent) : QObject(parent) {}

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
    if (m_current >= 0 && m_entries.at(m_current) == document) {
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
    m_current = m_entries.isEmpty() ? -1 : std::max<qsizetype>(current, 0);
}

} // namespace tekst
