#pragma once

#include "Host.h"

#include <QHash>
#include <QJSEngine>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode::remote
{

// Every host Kode reaches over SSH, one each (shared by every window), and
// ssh's prompts: kode-askpass asks through a socket of the user's only,
// with a token set for the ssh processes Kode starts; promptRequested()
// lets the app show it, answer() answers. One prompt at a time: the next
// is asked once the one shown is answered.
class Hosts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    enum class Prompt : quint8 {
        // A password, passphrase, code: a secure field.
        Secret,
        Text,
        // A question (an unknown host key): yes or no.
        Confirm,
    };
    Q_ENUM(Prompt)

    // The app's (QML's singleton too); others only in tests.
    explicit Hosts(QObject *parent = nullptr);
    ~Hosts() override;
    Q_DISABLE_COPY_MOVE(Hosts)

    [[nodiscard]] static Hosts &shared();
    [[nodiscard]] static Hosts *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // The host `destination` names, made on first use.
    Q_INVOKABLE kode::remote::Host *host(const QString &destination);
    // Made with `setup` when it is new (tests: a stand-in ssh).
    [[nodiscard]] Host *host(const QString &destination, Setup setup);
    // The host of an ssh:// URL; none for another URL.
    Q_INVOKABLE [[nodiscard]] kode::remote::Host *forUrl(const QUrl &url);
    // The hosts named in ~/.ssh/config.
    Q_INVOKABLE [[nodiscard]] static QStringList configuredHosts();
    Q_INVOKABLE void answer(qint32 id, bool accepted, const QString &value);
    // What a host's ssh runs with: the askpass environment, the folders.
    [[nodiscard]] Setup setup(const QString &destination) const;

signals:
    void promptRequested(qint32 id, const QString &host, const QString &text, kode::remote::Hosts::Prompt kind);

private:
    void listen();
    void read(QLocalSocket *socket);
    void ask(QLocalSocket *socket, QByteArrayView payload);

    struct Pending {
        QPointer<QLocalSocket> socket;
        QString host;
        QString text;
        qint64 id{0};
        // A Prompt.
        qint64 kind{0};
    };

    void showNext();

    QLocalServer m_askpass;
    // Owned as children.
    QHash<QString, QPointer<Host>> m_hosts;
    // The prompts waiting for an answer, the one shown first.
    QList<Pending> m_prompts;
    QByteArray m_token;
    qint64 m_nextPrompt{1};
};

} // namespace kode::remote
