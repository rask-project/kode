#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>
#include <vector>

namespace kode
{

class Document;

namespace lsp
{

class LanguageServer;

// The language servers the open documents talk to: for each document whose
// syntax has one (clangd for C and C++, pyright for Python…), the server
// found on the user's PATH (the login shell's: an app opened from the
// Finder gets a short one), one per program and root folder (the nearest
// with a .git, else the file's), started for the first document that needs
// it. Tells it about the document's text (opened, each edit, closed) and
// gives the document the diagnostics it publishes.
class LanguageServers : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by OpenDocuments")
    // Settings (Preferences' copy, bound by the app): off, folders searched
    // before the shell's PATH, clangd's threads (-j).
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged)
    Q_PROPERTY(QStringList searchFolders READ searchFolders WRITE setSearchFolders NOTIFY settingsChanged)
    Q_PROPERTY(qint32 clangdThreads READ clangdThreads WRITE setClangdThreads NOTIFY settingsChanged)
    // For each group of languages: {languages, servers (the names looked
    // for), path (the one found; empty: none), install (a command that
    // installs one)}.
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
    // The user's own servers, before the built-in ones: each {syntax
    // (KSyntaxHighlighting's name), command, languageId (empty: the syntax
    // in lower case)}.
    Q_PROPERTY(QVariantList customServers READ customServers WRITE setCustomServers NOTIFY settingsChanged)
    // customServers with each one's `path` found (empty: none).
    Q_PROPERTY(QVariantList custom READ custom NOTIFY serversChanged)
    // The login shell's PATH, searched after `searchFolders`.
    Q_PROPERTY(QStringList shellPath READ shellPath NOTIFY serversChanged)
public:
    explicit LanguageServers(QObject *parent = nullptr);
    ~LanguageServers() override;
    Q_DISABLE_COPY_MOVE(LanguageServers)

    // Follows `document` until remove().
    void add(Document *document);
    void remove(const Document *document);

    [[nodiscard]] bool enabled() const noexcept { return (m_flags & Enabled) != 0U; }
    void setEnabled(bool enabled);
    [[nodiscard]] QStringList searchFolders() const { return m_searchFolders; }
    void setSearchFolders(const QStringList &folders);
    [[nodiscard]] qint32 clangdThreads() const noexcept { return m_clangdThreads; }
    void setClangdThreads(qint32 threads);
    [[nodiscard]] QVariantList servers() const { return m_found; }
    [[nodiscard]] QStringList shellPath() const;
    [[nodiscard]] QVariantList customServers() const { return m_customServers; }
    void setCustomServers(const QVariantList &servers);
    [[nodiscard]] QVariantList custom() const { return m_customFound; }
    // Reads the shell's PATH again (a server installed since), then looks for the servers.
    Q_INVOKABLE void detect();
    // Puts the `index`th group's install command on the clipboard.
    Q_INVOKABLE static void copyInstallCommand(qint32 index);

    // Where the symbol at `position` is defined; locationsFound() with the
    // ticket returned (0: no server to ask).
    qint32 findDefinition(const Document *document, qsizetype position);
    // The header of a source or the other way round: beside it, else as
    // clangd knows it; locationsFound() with the ticket (an empty list: none).
    qint32 switchHeaderSource(const Document *document);
    // Completions at `position` (after typing `trigger`, if any):
    // completionsFound() with the ticket returned (0: no server offers them).
    qint32 requestCompletion(const Document *document, qsizetype position, const QString &trigger);
    // What typed asks `document`'s server for completions besides a word.
    [[nodiscard]] QStringList completionTriggers(const Document *document) const;

signals:
    void settingsChanged();
    void serversChanged();
    // Each {file, line, column (-1, 0: no place), label}.
    void locationsFound(qint32 ticket, const QVariantList &locations);
    // The protocol's CompletionItems; `incomplete`: typing more needs asking again.
    void completionsFound(qint32 ticket, const QJsonArray &items, bool incomplete);

private:
    static constexpr quint64 Enabled{1U << 0U};

    // A document followed, and the server that knows it (null: none).
    struct Entry {
        QPointer<Document> document;
        QUrl uri;
        // What the server is doing with it (clangd's file status).
        QString fileState;
        QString languageId;
        // Its last semantic tokens as the server sent them, and their id
        // (for asking only what changed since).
        QList<quint32> semanticData;
        QString semanticResultId;
        const LanguageServer *server{nullptr};
        qint64 version{0};
    };

    // Opening a file changes its text, name and syntax in turn: it is opened
    // on a server once they settle.
    void scheduleBind(const Document *document);
    void bindScheduled();
    // Its semantic tokens, asked once edits pause.
    void scheduleSemantic(const Document *document);
    void semanticScheduled();
    void requestSemantic(Entry &entry);
    void semanticTokensFound(const Document *document, qint64 version, const QJsonValue &result);
    // Opens the document on the server for it, if any, after closing it where it was.
    void bind(Entry &entry);
    void unbind(Entry &entry);
    // Tells the document its server and what it is busy with.
    void showStatus(const Entry &entry);
    void fileStatusChanged(const LanguageServer *server, const QUrl &uri, const QString &state);
    void activityChanged(const LanguageServer *server);
    void documentEdited(const Document *document, qsizetype position, const QString &removed, const QString &inserted);
    void documentReset(const Document *document);
    void diagnosticsPublished(const LanguageServer *server, const QUrl &uri, qint64 version, const QJsonArray &list);
    void serverFinished(const LanguageServer *server);
    [[nodiscard]] LanguageServer *
    server(const QString &program, const QStringList &arguments, const QJsonObject &options, const QUrl &root);
    // The server `server` points to, to talk to it.
    [[nodiscard]] LanguageServer *find(const LanguageServer *server) const;
    void readShellPath();
    // The server to start for `syntax`: the program, its arguments and the protocol's language id.
    struct Choice {
        QString path;
        QStringList arguments;
        QString languageId;
    };
    [[nodiscard]] std::optional<Choice> serverFor(const QString &syntax) const;
    // Stops the servers and opens the documents again with the settings.
    void restart();
    // Where servers are looked for, in order: searchFolders, then the shell's PATH.
    [[nodiscard]] QStringList searchPath() const;

    std::vector<std::unique_ptr<LanguageServer>> m_servers;
    QHash<const Document *, Entry> m_entries;
    QSet<const Document *> m_toBind;
    QProcess m_shell;
    QTimer m_bindTimer;
    QTimer m_semanticTimer;
    QSet<const Document *> m_semanticPending;
    QTimer m_shellTimeout;
    QStringList m_searchFolders;
    QVariantList m_found;
    QVariantList m_customServers;
    QVariantList m_customFound;
    // The login shell's PATH; empty until it is known.
    QString m_shellPath;
    quint64 m_flags{Enabled};
    qint32 m_clangdThreads{1};
    qint32 m_lastTicket{0};
};

} // namespace lsp
} // namespace kode
