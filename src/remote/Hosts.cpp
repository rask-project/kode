#include "Hosts.h"

#include "Ssh.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace kode::remote
{

namespace
{

// A prompt is a line or a few (a host key's fingerprint).
constexpr qsizetype promptLimit{qsizetype{8} * 1024};
constexpr qsizetype tokenBytes{32};

bool sameToken(QByteArrayView given, QByteArrayView token)
{
    if (given.size() != token.size()) {
        return false;
    }
    // Every byte compared: no early exit to time.
    quint32 difference{0U};
    for (qsizetype i{0}; i < token.size(); ++i) {
        difference |= static_cast<quint32>(static_cast<quint8>(given[i]) ^ static_cast<quint8>(token[i]));
    }
    return difference == 0U;
}

Hosts::Prompt kindOf(QByteArrayView askpassPrompt, const QString &text)
{
    static const QRegularExpression secret{
        QStringLiteral(R"(pass(word|phrase)|\bpin\b|code|token|otp)"),
        QRegularExpression::CaseInsensitiveOption
    };
    if (askpassPrompt == "confirm" || text.contains(QLatin1String("(yes/no"))) {
        return Hosts::Prompt::Confirm;
    }
    return secret.match(text).hasMatch() ? Hosts::Prompt::Secret : Hosts::Prompt::Text;
}

} // namespace

Hosts::Hosts(QObject *parent) : QObject(parent)
{
    listen();
}

Hosts::~Hosts()
{
    m_askpass.close();
}

Hosts &Hosts::shared()
{
    static Hosts instance;
    return instance;
}

Hosts *Hosts::create(QQmlEngine * /*qmlEngine*/, QJSEngine * /*jsEngine*/)
{
    Hosts &hosts{shared()};
    // A function-local static: the engine must not delete it.
    QJSEngine::setObjectOwnership(&hosts, QJSEngine::CppOwnership);
    return &hosts;
}

void Hosts::listen()
{
    QByteArray random(tokenBytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(random.begin(), random.end());
    m_token = random.toHex();
    // $TMPDIR is the user's only on macOS.
    const QString path{QDir::temp().filePath(QStringLiteral("kode-askpass-%1").arg(QCoreApplication::applicationPid()))
    };
    QLocalServer::removeServer(path);
    m_askpass.setSocketOptions(QLocalServer::UserAccessOption);
    m_askpass.listen(path);
    connect(&m_askpass, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *const socket{m_askpass.nextPendingConnection()}) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        }
    });
}

void Hosts::read(QLocalSocket *socket)
{
    const QByteArray head{socket->peek(32)};
    const qsizetype newline{head.indexOf('\n')};
    if (newline < 0) {
        if (head.size() >= 32) {
            socket->abort();
        }
        return;
    }
    bool number{false};
    const qsizetype length{head.left(newline).toLongLong(&number)};
    if (!number || length < 0 || length > promptLimit) {
        socket->abort();
        return;
    }
    if (socket->bytesAvailable() < newline + 1 + length) {
        return;
    }
    socket->read(newline + 1);
    const QByteArray payload{socket->read(length)};
    // Asked once: what follows is not read.
    socket->disconnect(this);
    ask(socket, payload);
}

void Hosts::ask(QLocalSocket *socket, QByteArrayView payload)
{
    QList<QByteArrayView> parts;
    for (qint32 field{0}; field < 3; ++field) {
        const qsizetype newline{payload.indexOf('\n')};
        if (newline < 0) {
            socket->abort();
            return;
        }
        parts.emplace_back(payload.first(newline));
        payload = payload.sliced(newline + 1);
    }
    if (!sameToken(parts[0], m_token)) {
        socket->abort();
        return;
    }
    // A notice (touch the security key): nothing to answer.
    if (parts[2] == "none") {
        socket->write("1\n");
        socket->flush();
        socket->disconnectFromServer();
        return;
    }
    const QString text{QString::fromUtf8(payload)};
    m_prompts.emplace_back(Pending{
        .socket = socket,
        .host = QString::fromUtf8(parts[1]),
        .text = text,
        .id = m_nextPrompt++,
        .kind = static_cast<qint64>(kindOf(parts[2], text)),
    });
    if (m_prompts.size() == 1) {
        showNext();
    }
}

void Hosts::showNext()
{
    if (m_prompts.isEmpty()) {
        return;
    }
    const Pending &next{m_prompts.first()};
    emit promptRequested(static_cast<qint32>(next.id), next.host, next.text, static_cast<Prompt>(next.kind));
}

void Hosts::answer(qint32 id, bool accepted, const QString &value)
{
    if (m_prompts.isEmpty() || m_prompts.first().id != id) {
        return;
    }
    const QPointer<QLocalSocket> socket{m_prompts.takeFirst().socket};
    if (socket != nullptr) {
        socket->write(accepted ? QByteArrayLiteral("1\n") + value.toUtf8() : QByteArrayLiteral("0\n"));
        socket->flush();
        socket->disconnectFromServer();
    }
    showNext();
}

Setup Hosts::setup(const QString &destination) const
{
    QProcessEnvironment environment{QProcessEnvironment::systemEnvironment()};
    environment.insert(
        QStringLiteral("SSH_ASKPASS"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/kode-askpass")
    );
    environment.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    environment.insert(QStringLiteral("KODE_ASKPASS_SOCKET"), m_askpass.fullServerName());
    environment.insert(QStringLiteral("KODE_ASKPASS_TOKEN"), QString::fromLatin1(m_token));
    environment.insert(QStringLiteral("KODE_ASKPASS_HOST"), destination);
    // Short: a control socket's path (with ssh's temporary suffix) must fit in 104 bytes.
    const QString control{QDir::home().filePath(QStringLiteral(".ssh/kode"))};
    QDir{}.mkpath(control);
    QFile::setPermissions(control, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return Setup{
        .environment = environment,
        .ssh = QStringLiteral("/usr/bin/ssh"),
        .controlFolder = control,
        .agents = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/remote")),
        .version = QStringLiteral(KODE_VERSION),
    };
}

Host *Hosts::host(const QString &destination)
{
    const QString name{destination.trimmed()};
    return host(name, setup(name));
}

Host *Hosts::host(const QString &destination, Setup setup)
{
    if (const QPointer<Host> known{m_hosts.value(destination)}; known != nullptr) {
        return known;
    }
    // A child: QML sees it as C++'s.
    auto *const made{std::make_unique<Host>(destination, std::move(setup), this).release()};
    m_hosts.insert(destination, made);
    return made;
}

Host *Hosts::forUrl(const QUrl &url)
{
    const QString destination{ssh::destinationOf(url)};
    return destination.isEmpty() ? nullptr : host(destination);
}

QStringList Hosts::configuredHosts()
{
    return ssh::configuredHosts(QDir::home().filePath(QStringLiteral(".ssh/config")));
}

} // namespace kode::remote
