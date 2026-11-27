#pragma once

#include "Protocol.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace kode::lsp
{

// One language server: its process, talked to over stdin/stdout. Messages
// sent before it answered `initialize` wait; requests get their result
// through a handler. Requests from the server get an empty answer (Kode
// offers nothing it would ask for).
class LanguageServer : public QObject
{
    Q_OBJECT
public:
    using Handler = std::function<void(const QJsonValue &result)>;

    // `program` with `arguments` (and `path` as PATH) for the folder `root`;
    // `options` are its initializationOptions.
    LanguageServer(
        const QString &program,
        const QStringList &arguments,
        const QJsonObject &options,
        const QString &path,
        const QUrl &root
    );
    ~LanguageServer() override;
    Q_DISABLE_COPY_MOVE(LanguageServer)

    [[nodiscard]] QString program() const { return m_process.program(); }
    [[nodiscard]] QStringList arguments() const { return m_process.arguments(); }
    [[nodiscard]] QUrl root() const { return m_root; }
    // Edits go as ranges (else the whole text each time).
    [[nodiscard]] bool incrementalSync() const noexcept { return m_sync == IncrementalSync; }
    // What it is busy with, the latest work it reported ("Indexing 12/120
    // (10%)"); empty when idle.
    [[nodiscard]] QString activity() const;

    void notify(const QString &method, const QJsonValue &params);
    void request(const QString &method, const QJsonValue &params, Handler handler);

signals:
    // textDocument/publishDiagnostics: for `version` of `uri` (-1: any).
    void diagnosticsPublished(const QUrl &uri, qint64 version, const QJsonArray &diagnostics);
    // clangd's textDocument/clangd.fileStatus: what it is doing with `uri` ("idle" when done).
    void fileStatusChanged(const QUrl &uri, const QString &state);
    void activityChanged();
    // The process ended or could not start.
    void finished();

private:
    static constexpr qint64 FullSync{1};
    static constexpr qint64 IncrementalSync{2};
    static constexpr quint64 Initialized{1U << 0U};

    // Work in progress ($/progress), by token.
    struct Progress {
        QString token;
        QString title;
        QString message;
        qint64 percentage{-1};
    };

    void initialize(const QJsonObject &options);
    void progress(const QJsonObject &params);
    void send(const QJsonObject &message);
    void write(const QJsonObject &message);
    void readOutput();
    void handle(const QJsonObject &message);
    void answer(const QJsonObject &request);

    QProcess m_process;
    MessageReader m_reader;
    QHash<qint64, Handler> m_pending;
    // Held until the server is initialized.
    QList<QJsonObject> m_queued;
    QList<Progress> m_progress;
    QUrl m_root;
    qint64 m_nextId{1};
    qint64 m_sync{FullSync};
    quint64 m_flags{0U};
};

} // namespace kode::lsp
