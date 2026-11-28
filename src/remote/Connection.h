#pragma once

#include "../../remote/protocol/Frame.h"

#include <QByteArrayView>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>

namespace kode::remote
{

struct Error {
    QString message;
    // What the agent adds (fs: a conflict's current version).
    QJsonValue data;
    // JSON-RPC's code (error:: in Methods.h).
    qint64 code{0};
};

// How a channel's bytes arrive: pieces, then its end.
enum class ChannelEnd : quint8 {
    More,
    Complete,
    // Reset by the agent, or the connection closed.
    Failed,
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
    // Called with each piece (More), then once with the end.
    using Receiver = std::function<void(QByteArrayView piece, ChannelEnd end)>;

    explicit Connection(QObject *parent = nullptr);
    ~Connection() override;
    Q_DISABLE_COPY_MOVE(Connection)

    // Before start(): the process's environment (ssh's askpass).
    void setEnvironment(const QProcessEnvironment &environment) { m_process.setProcessEnvironment(environment); }
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

    // The agent's channel `channel` (named in an answer) goes to `receiver`;
    // what it takes is granted back as credit.
    void receive(quint32 channel, Receiver receiver);
    // A new channel of Kode's, for the request that names it.
    [[nodiscard]] quint32 openChannel() noexcept { return static_cast<quint32>(m_nextChannel += 2U); }
    // `bytes` on Kode's `channel` as the agent's credit allows, then its end.
    void send(quint32 channel, const QByteArray &bytes);
    // `bytes` added to what Kode's `channel` sends; it goes on until end().
    void write(quint32 channel, QByteArrayView bytes);
    void end(quint32 channel);

signals:
    void stateChanged();
    void notified(const QString &method, const QJsonValue &params);

private:
    void send(const QJsonObject &message);
    void readFrames();
    void handle(const Frame &frame);
    // False when the agent broke the protocol.
    [[nodiscard]] bool handleChannel(const Frame &frame);
    void pump(quint32 channel);
    void writeFrame(FrameType type, quint32 channel, QByteArrayView payload);
    void handleMessage(QByteArrayView payload);
    void initialized(const Result &result);
    // Ends the connection with `message` (empty when asked to).
    void finish(const QString &message);
    void setState(State state);

    QProcess m_process;
    FrameReader m_reader;
    struct Incoming {
        Receiver receiver;
        // What the agent may still send.
        qint64 window{0};
    };
    struct Outgoing {
        QByteArray bytes;
        qint64 sent{0};
        // What the agent still accepts.
        qint64 credit{0};
        // Its end is sent once the bytes went (a whole word: no padding).
        qint64 ending{0};
    };

    QHash<qint64, Handler> m_pending;
    QHash<quint32, Incoming> m_incoming;
    QHash<quint32, Outgoing> m_outgoing;
    QJsonObject m_agent;
    QString m_error;
    // What the process wrote to stderr: ssh's messages (bounded).
    QByteArray m_stderr;
    qint64 m_nextId{1};
    // The last channel id of Kode's (odd).
    quint64 m_nextChannel{static_cast<quint64>(-1)};
    // A State (a whole word: no padding).
    quint64 m_state{0U};
};

} // namespace kode::remote
