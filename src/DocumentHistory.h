#pragma once

#include "Document.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode
{

// A split's navigation history, as Qt Creator's: the places (a document, a
// line and column) it went back and forward through. A place joins when the
// caret leaves where it edited, before a jump (Go to Definition, Go to
// Line) and when the split shows another document; going back stores where
// the caret is first, so forward comes back to it. Also the documents by
// recent use, for the Ctrl+Tab switcher.
class DocumentHistory : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
    // Each document once, the most recently shown first.
    Q_PROPERTY(QVariantList recent READ recent NOTIFY changed)
public:
    explicit DocumentHistory(QObject *parent = nullptr);

    [[nodiscard]] bool canGoBack() const noexcept { return m_current > 0; }
    [[nodiscard]] bool canGoForward() const noexcept { return m_current + 1 < m_places.size(); }
    [[nodiscard]] QVariantList recent() const;

    // `documents` (the open ones) for the Ctrl+Tab switcher: those this split
    // showed, most recent first, then the others in their order.
    Q_INVOKABLE [[nodiscard]] QVariantList recentAmong(const QVariantList &documents) const;
    // The document shown most recently, or null.
    Q_INVOKABLE [[nodiscard]] kode::Document *current() const;
    // `document` is shown now: first among the recent ones.
    Q_INVOKABLE void visit(kode::Document *document);
    // A place to come back to (none when it repeats the one before it).
    Q_INVOKABLE void add(kode::Document *document, qint64 line, qint64 column);
    // The same at the place `document` keeps (where the split left it).
    Q_INVOKABLE void addLeft(kode::Document *document);
    // From the caret at `line`, `column` of `document`: the place before
    // (after) it, {document, line, column}; empty at the start (end).
    Q_INVOKABLE QVariantMap back(kode::Document *document, qint64 line, qint64 column);
    Q_INVOKABLE QVariantMap forward(kode::Document *document, qint64 line, qint64 column);
    // Drops `document` (it is closing).
    Q_INVOKABLE void forget(const kode::Document *document);

signals:
    void changed();

private:
    struct Place {
        QPointer<Document> document;
        qsizetype line{0};
        qsizetype column{0};
    };

    // Stores the caret's place at the current position (going back or forward from it).
    void storeCurrent(Document *document, qint64 line, qint64 column);
    // The place at the current position, its document still open; dropping
    // those of closed ones on the way in `step` (-1 back, 1 forward).
    [[nodiscard]] QVariantMap open(qsizetype step);
    void remove(const Document *document);
    // Moves `document` to the front of m_recent; false when it already was.
    bool touch(Document *document);

    QList<Place> m_places;
    QList<QPointer<Document>> m_recent;
    // Where the caret's place goes; m_places.size() when it is not stored.
    qsizetype m_current{0};
};

} // namespace kode
