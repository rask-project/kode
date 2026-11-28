#pragma once

#include "Connection.h"

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <QList>
#include <QPointer>
#include <QUrl>

#include <expected>
#include <functional>
#include <memory>

namespace kode::remote
{

// What a Host needs from the app: the programs and folders it uses.
struct Setup {
    QProcessEnvironment environment;
    // /usr/bin/ssh (a stand-in in tests).
    QString ssh;
    // ssh's ControlPath folder (the user's only).
    QString controlFolder;
    // <agents>/<os>-<arch>/kode-remote, the agents bundled in Kode.app.
    QString agents;
    // The agent's version: its folder on the host.
    QString version;
};

// One host reached over SSH: the agent probed, installed when missing or
// changed (checked by its SHA-256 every time), its bridge started, the
// session kept. A connection lost once online is retried with backoff
// (1, 2, 4… 30 s); a failure that will not pass (authentication, a host
// key) stops.
class Host : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Hosts makes them")
    Q_PROPERTY(QString destination READ destination CONSTANT)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    // Why it failed or went offline.
    Q_PROPERTY(QString errorString READ errorString NOTIFY stateChanged)
    // "Linux arm64", once probed.
    Q_PROPERTY(QString platform READ platform NOTIFY stateChanged)
    // The user the agent runs as, once online.
    Q_PROPERTY(QString user READ user NOTIFY stateChanged)
    // Where it stands, in a sentence for the UI.
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    // The user's home there, once online.
    Q_PROPERTY(QString home READ home NOTIFY stateChanged)
public:
    enum class State : quint8 {
        Disconnected,
        Connecting,
        Installing,
        Online,
        // Lost: retried.
        Offline,
        Failed,
    };
    Q_ENUM(State)

    Host(const QString &destination, Setup setup, QObject *parent = nullptr);
    ~Host() override;
    Q_DISABLE_COPY_MOVE(Host)

    [[nodiscard]] QString destination() const { return m_destination; }
    [[nodiscard]] State state() const noexcept { return static_cast<State>(m_state); }
    [[nodiscard]] QString errorString() const { return m_error; }
    [[nodiscard]] QString platform() const;
    [[nodiscard]] QString user() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] QString home() const { return m_home; }
    // The ssh:// URL of `path` there.
    Q_INVOKABLE [[nodiscard]] QUrl fileUrl(const QString &path) const;

    using Ready = std::function<void(std::expected<std::reference_wrapper<Connection>, QString> connection)>;
    // `ready` once online (connecting first when it is not): with the
    // connection, or why it failed or was disconnected. Not called once
    // `context` is gone; while offline it waits for the retries.
    void whenOnline(const QObject *context, Ready ready);
    // Online: the session's connection; else none.
    [[nodiscard]] Connection *connection() const noexcept;

    Q_INVOKABLE void connectToHost();
    Q_INVOKABLE void disconnectFromHost();
    // Offline: tries again now (the window came back, the network changed).
    Q_INVOKABLE void reconnectNow();

signals:
    void stateChanged();

private:
    void probe();
    void probed();
    void install(const QString &agent);
    void installed(const QString &expected);
    void startBridge();
    void connectionChanged();
    // `message` stops it, or is retried when it may pass and it was online once.
    void fail(const QString &message);
    void setState(State state);
    // What ssh wrote to stderr (read once).
    [[nodiscard]] QString sshError();

    Setup m_setup;
    QString m_destination;
    QString m_error;
    QString m_os;
    QString m_arch;
    QString m_home;
    QProcess m_ssh;
    QTimer m_retry;
    struct Waiting {
        QPointer<const QObject> context;
        Ready ready;
    };
    void tellWaiting();

    std::unique_ptr<Connection> m_connection;
    QList<Waiting> m_waiting;
    qint64 m_backoff{1};
    // A State; whether it was online once (a loss is retried).
    quint64 m_state{0U};
    quint64 m_wasOnline{0U};
};

} // namespace kode::remote
