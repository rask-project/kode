#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode::remote
{

// Signing in to a host with this Mac's key instead of a password, as
// ssh-copy-id does: check() tells whether the host takes the key; install()
// makes one when there is none (~/.ssh/id_ed25519, no passphrase), adds the
// public key to the host's ~/.ssh/authorized_keys (ssh asks for the password
// through Kode, as when connecting), then checks again.
class SshKey : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString destination READ destination WRITE setDestination NOTIFY destinationChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    // What the last step found, for the user.
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
public:
    enum class State : quint8 {
        Unknown,
        Checking,
        Installing,
        // The host takes the key: no password asked.
        Works,
        // It asks for a password (or the check could not reach it).
        NoKey,
    };
    Q_ENUM(State)

    explicit SshKey(QObject *parent = nullptr);
    ~SshKey() override;
    Q_DISABLE_COPY_MOVE(SshKey)

    [[nodiscard]] QString destination() const { return m_destination; }
    void setDestination(const QString &destination);
    [[nodiscard]] State state() const noexcept { return static_cast<State>(m_state); }
    [[nodiscard]] QString message() const { return m_message; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void install();

    // ~/.ssh's first public key (ed25519, ecdsa, rsa); empty: none.
    [[nodiscard]] static QString publicKeyPath();

signals:
    void destinationChanged();
    void stateChanged();

private:
    enum class Step : quint8 {
        None,
        Generating,
        Copying,
        Checking,
    };

    void run(Step step, const QString &program, const QStringList &arguments);
    void finished(qint32 exitCode, QProcess::ExitStatus status);
    void copy();
    void set(State state, const QString &message);

    QProcess m_process;
    QString m_destination;
    QString m_message;
    // A State and a Step, as whole words: no padding.
    qint64 m_state{0};
    qint64 m_step{0};
};

} // namespace kode::remote
