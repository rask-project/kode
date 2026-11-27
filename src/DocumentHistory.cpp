#include "DocumentHistory.h"

#include "TextDocument.h"

#include <algorithm>
#include <utility>

namespace kode
{

using namespace Qt::StringLiterals;

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
    const auto open{std::ranges::find_if(m_recent, [](const QPointer<Document> &document) {
        return !document.isNull();
    })};
    return open != m_recent.cend() ? open->data() : nullptr;
}

void DocumentHistory::visit(Document *document)
{
    if (document != nullptr && touch(document)) {
        emit changed();
    }
}

void DocumentHistory::add(Document *document, qint64 line, qint64 column)
{
    if (document == nullptr) {
        return;
    }
    remove(nullptr);
    m_current = std::min(m_current, m_places.size());
    // The same line as the place before it: nothing to come back to.
    if (m_current > 0) {
        const Place &previous{m_places.at(m_current - 1)};
        if (previous.document == document && previous.line == line) {
            return;
        }
    }
    m_places.insert(m_current, Place{.document = document, .line = line, .column = column});
    ++m_current;
    // Full: from the end farther from the current position.
    while (m_places.size() >= maximumEntries) {
        if (m_current > maximumEntries / 2) {
            m_places.removeFirst();
            --m_current;
        } else {
            m_places.removeLast();
        }
    }
    emit changed();
}

void DocumentHistory::addLeft(Document *document)
{
    if (document == nullptr) {
        return;
    }
    const TextBuffer &buffer{document->textDocument()->buffer()};
    const qsizetype cursor{std::clamp(document->textDocument()->place().caret.cursor, qsizetype{0}, buffer.length())};
    const qsizetype line{buffer.lineAt(cursor)};
    add(document, line, cursor - buffer.lineStart(line));
}

void DocumentHistory::storeCurrent(Document *document, qint64 line, qint64 column)
{
    if (document == nullptr) {
        return;
    }
    const Place place{.document = document, .line = line, .column = column};
    if (m_current < m_places.size()) {
        m_places[m_current] = place;
    } else {
        m_places.append(place);
        m_current = m_places.size() - 1;
    }
}

QVariantMap DocumentHistory::back(Document *document, qint64 line, qint64 column)
{
    remove(nullptr);
    if (!canGoBack()) {
        return {};
    }
    storeCurrent(document, line, column);
    --m_current;
    return open(-1);
}

QVariantMap DocumentHistory::forward(Document *document, qint64 line, qint64 column)
{
    remove(nullptr);
    if (!canGoForward()) {
        return {};
    }
    storeCurrent(document, line, column);
    ++m_current;
    return open(1);
}

QVariantMap DocumentHistory::open(qsizetype step)
{
    while (m_current >= 0 && m_current < m_places.size() && m_places.at(m_current).document.isNull()) {
        m_places.removeAt(m_current);
        m_current += step < 0 ? -1 : 0;
    }
    m_current = std::clamp<qsizetype>(m_current, 0, m_places.size());
    emit changed();
    if (m_current >= m_places.size()) {
        return {};
    }
    const Place &place{m_places.at(m_current)};
    return {
        {u"document"_s, QVariant::fromValue(place.document.data())},
        {u"line"_s, place.line},
        {u"column"_s, place.column},
    };
}

void DocumentHistory::forget(const Document *document)
{
    if (document == nullptr) {
        return;
    }
    remove(document);
    emit changed();
}

// Drops the places of `document` (null: of deleted documents) and the
// repeats that leaves side by side, keeping the current position where it was.
void DocumentHistory::remove(const Document *document)
{
    QList<Place> kept;
    kept.reserve(m_places.size());
    qsizetype current{m_current >= m_places.size() ? -1 : 0};
    for (qsizetype index{0}; index < m_places.size(); ++index) {
        const Place &place{m_places.at(index)};
        const bool repeated{
            !kept.isEmpty() && kept.constLast().document == place.document && kept.constLast().line == place.line
        };
        if (index == m_current) {
            current = kept.size();
        }
        if (!place.document.isNull() && place.document != document && !repeated) {
            kept.append(place);
        }
    }
    m_current = current < 0 ? kept.size() : std::min(current, kept.size());
    m_places = std::move(kept);
    m_recent.removeIf([document](const QPointer<Document> &entry) { return entry.isNull() || entry == document; });
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
