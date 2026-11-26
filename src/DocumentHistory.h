#pragma once

#include "Document.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace tekst
{

// The documents a split showed, in order, to go back and forward through
// them as in a browser (visiting one after going back drops what was
// ahead), and by recent use for the Ctrl+Tab switcher, as in Qt Creator.
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
    [[nodiscard]] bool canGoForward() const noexcept { return m_current + 1 < m_entries.size(); }
    [[nodiscard]] QVariantList recent() const;

    // `documents` (the open ones) for the Ctrl+Tab switcher: those this split
    // showed, most recent first, then the others in their order.
    Q_INVOKABLE [[nodiscard]] QVariantList recentAmong(const QVariantList &documents) const;
    // The document shown now, or null.
    Q_INVOKABLE [[nodiscard]] tekst::Document *current() const;
    // `document` is shown now (nothing changes when it already is).
    Q_INVOKABLE void visit(tekst::Document *document);
    // The document before (after) the current one, which it becomes; null at the start (end).
    Q_INVOKABLE tekst::Document *back();
    Q_INVOKABLE tekst::Document *forward();
    // Drops `document` (it is closing).
    Q_INVOKABLE void forget(const tekst::Document *document);

signals:
    void changed();

private:
    void remove(const Document *document);
    // Moves `document` to the front of m_recent; false when it already was.
    bool touch(Document *document);

    // A document deleted without forget() leaves a null entry until the next change.
    QList<QPointer<Document>> m_entries;
    QList<QPointer<Document>> m_recent;
    qsizetype m_current{-1};
};

} // namespace tekst
