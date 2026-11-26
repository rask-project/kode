#pragma once

#include "Document.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>

namespace tekst
{

// The documents a split showed, in order, to go back and forward through
// them as in Xcode or a browser: visiting one after going back drops what
// was ahead.
class DocumentHistory : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
public:
    explicit DocumentHistory(QObject *parent = nullptr);

    [[nodiscard]] bool canGoBack() const noexcept { return m_current > 0; }
    [[nodiscard]] bool canGoForward() const noexcept { return m_current + 1 < m_entries.size(); }

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

    // A document deleted without forget() leaves a null entry until the next change.
    QList<QPointer<Document>> m_entries;
    qsizetype m_current{-1};
};

} // namespace tekst
