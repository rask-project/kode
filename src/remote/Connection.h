#pragma once

#include "../../remote/protocol/Frame.h"

#include <QByteArrayView>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>

namespace kode::remote
{

struct Error {
    QString message;
    // JSON-RPC's code (error:: in Methods.h).
    qint64 code{0};
};

// Kode's end of a kode-remote session: the process that reaches the agent
// (ssh to its bridge; the agent itself in tests), JSON-RPC over its frames.
// initialize goes first; requests made meanwhile follow it. Whatever comes
// from the agent is checked: a broken frame or message ends the connection.
class Connection : public QObject
{
    Q_OBJECT
public:
    enum class State : quint8 {
        Idle,
        Connecting,
        Online,
        Closed,
    };
    Q_ENUM(State)

    using Result = std::expected<QJsonValue, Error>;
    using Handler = std::function<void(const Result &result)>;

    explicit Connection(QObject *parent = nullptr);
    ~Connection() override;
    Q_DISABLE_COPY_MOVE(Connection)

    void start(const QString &program, const QStringList &arguments);
    // Pending requests fail with error::disconnected.
    void close();
    [[nodiscard]] State state() const noexcept { return static_cast<State>(m_state); }
    // The agent's answer to initialize: version, session, platform, capabilities.
    [[nodiscard]] const QJsonObject &agent() const noexcept { return m_agent; }
    // Why it closed, when it was not asked to.
    [[nodiscard]] const QString &errorString() const noexcept { return m_error; }

    void request(const QString &method, const QJsonObject &params, Handler handler);
    void notify(const QString &method, const QJsonObject &params);

signals:
    void stateChanged();
    void notified(const QString &method, const QJsonValue &params);

private:
    void send(const QJsonObject &message);
    void readFrames();
    void handle(const Frame &frame);
    void handleMessage(QByteArrayView payload);
    void initialized(const Result &result);
    // Ends the connection with `message` (empty when asked to).
    void finish(const QString &message);
    void setState(State state);

    QProcess m_process;
    FrameReader m_reader;
    QHash<qint64, Handler> m_pending;
    QJsonObject m_agent;
    QString m_error;
    // What the process wrote to stderr: ssh's messages (bounded).
    QByteArray m_stderr;
    qint64 m_nextId{1};
    // A State (a whole word: no padding).
    quint64 m_state{0U};
};

} // namespace kode::remote
