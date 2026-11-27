#pragma once

#include "TextDocument.h"

#include <QAbstractListModel>
#include <QJsonArray>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode
{

class Document;
class EditorView;

namespace lsp
{
class LanguageServers;
} // namespace lsp

// An editor's completion list, as in VS Code: asked of the document's
// language server when a word starts, after a trigger character ("." "->"…)
// or on request (⌃Space), then filtered by what is typed after its start.
// Accepting replaces the word and makes the server's other edits (an
// #include…) in one undo step.
class Completion : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(kode::EditorView *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(kode::Document *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(kode::lsp::LanguageServers *servers READ servers WRITE setServers NOTIFY serversChanged)
    // Shown: there are items for what is typed.
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(qint32 currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    // Where the list starts in the text (the word's start), for placing it.
    Q_PROPERTY(qint64 start READ start NOTIFY activeChanged)
public:
    enum Role : quint16 {
        LabelRole = Qt::UserRole + 1,
        DetailRole,
        KindRole,
    };

    explicit Completion(QObject *parent = nullptr);

    [[nodiscard]] qint32 rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, qint32 role) const override;
    [[nodiscard]] QHash<qint32, QByteArray> roleNames() const override;

    [[nodiscard]] EditorView *editor() const { return m_editor; }
    void setEditor(EditorView *editor);
    [[nodiscard]] Document *document() const { return m_document; }
    void setDocument(Document *document);
    [[nodiscard]] lsp::LanguageServers *servers() const { return m_servers; }
    void setServers(lsp::LanguageServers *servers);
    [[nodiscard]] bool active() const noexcept { return !m_shown.isEmpty(); }
    [[nodiscard]] qint32 currentIndex() const noexcept { return m_current; }
    void setCurrentIndex(qint32 index);
    [[nodiscard]] qint64 start() const noexcept { return m_start; }

    // ⌃Space: asks for the completions at the caret.
    Q_INVOKABLE void trigger();
    Q_INVOKABLE void accept();
    Q_INVOKABLE void cancel();
    // Moves the current item by `step` (↑ ↓), within the list.
    Q_INVOKABLE void move(qint32 step);

signals:
    void editorChanged();
    void documentChanged();
    void serversChanged();
    void activeChanged();
    void currentIndexChanged();

private:
    // A CompletionItem, its edit in the text's positions.
    struct Item {
        QString label;
        QString detail;
        QString filter;
        QString sort;
        QString text;
        // Other edits (an #include…), by position.
        QList<TextDocument::Change> others;
        // The range the server's edit replaces: its start, and its end past
        // the caret the request was made at; start -1: the word's.
        qsizetype editStart{-1};
        qsizetype editEndPastCaret{0};
        qint64 kind{0};
    };

    // Typing more needs asking again.
    static constexpr quint64 Incomplete{1U << 0U};

    void typed(const QString &text);
    void followCaret();
    void request(const QString &trigger);
    void found(qint32 ticket, const QJsonArray &items, bool incomplete);
    // Shows the items matching what is typed since the start, best first.
    void filter();
    void setShown(QList<qsizetype> shown);
    [[nodiscard]] Item item(const QJsonValue &value) const;
    [[nodiscard]] QString typedWord() const;

    QPointer<EditorView> m_editor;
    QPointer<Document> m_document;
    QPointer<lsp::LanguageServers> m_servers;
    QList<Item> m_items;
    // Indices into m_items, as listed.
    QList<qsizetype> m_shown;
    qsizetype m_start{-1};
    // The caret when the items were asked for.
    qsizetype m_requestCaret{0};
    quint64 m_flags{0U};
    qint32 m_ticket{0};
    qint32 m_current{0};
};

} // namespace kode
