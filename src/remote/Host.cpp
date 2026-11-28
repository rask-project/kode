#include "Host.h"

#include "Ssh.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>

namespace kode::remote
{

namespace
{

constexpr qint64 longestBackoff{30};
constexpr qint32 millisecondsPerSecond{1000};

// The SHA-256 of a file, as hex; empty when it cannot be read.
QString hashOf(const QString &path)
{
    QFile file{path};
    QCryptographicHash hash{QCryptographicHash::Sha256};
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

Host::Host(const QString &destination, Setup setup, QObject *parent)
    : QObject(parent)
    , m_setup{std::move(setup)}
    , m_destination{destination}
{
    m_ssh.setProcessEnvironment(m_setup.environment);
    connect(&m_ssh, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            fail(tr("“%1” could not be started: %2").arg(m_setup.ssh, m_ssh.errorString()));
        }
    });
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &Host::connectToHost);
    m_watchTimer.setSingleShot(true);
    m_watchTimer.setInterval(0);
    connect(&m_watchTimer, &QTimer::timeout, this, &Host::sendWatches);
}

void Host::watch(const QString &path)
{
    if (m_watched[path]++ == 0) {
        m_watchTimer.start();
    }
}

void Host::unwatch(const QString &path)
{
    const auto found{m_watched.find(path)};
    if (found == m_watched.end() || --found.value() > 0) {
        return;
    }
    m_watched.erase(found);
    m_watchTimer.start();
}

bool Host::canWatch() const
{
    return state() == State::Online && !m_connection->agent()
                                            .value(QStringLiteral("capabilities"))
                                            .toObject()
                                            .value(QStringLiteral("watch"))
                                            .toArray()
                                            .isEmpty();
}

void Host::sendWatches()
{
    if (state() != State::Online) {
        return;
    }
    m_connection->request(
        QStringLiteral("watch/set"),
        {{QStringLiteral("paths"), QJsonArray::fromStringList(m_watched.keys())}},
        [](const Connection::Result &) {}
    );
}

Host::~Host()
{
    m_ssh.disconnect(this);
    m_ssh.kill();
    m_ssh.waitForFinished();
}

QString Host::platform() const
{
    if (m_os.isEmpty()) {
        return {};
    }
    const QString os{m_os == QLatin1String("darwin") ? QStringLiteral("macOS") : m_os.left(1).toUpper() + m_os.mid(1)};
    return os + u' ' + m_arch;
}

QString Host::user() const
{
    if (m_connection == nullptr) {
        return {};
    }
    return m_connection->agent().value(QStringLiteral("platform")).toObject().value(QStringLiteral("user")).toString();
}

QString Host::status() const
{
    switch (state()) {
    case State::Disconnected:
        return tr("Not connected.");
    case State::Connecting:
        return tr("Connecting to “%1”…").arg(m_destination);
    case State::Installing:
        return tr("Installing Kode’s agent on “%1”…").arg(m_destination);
    case State::Online:
        return tr("Connected to “%1” as %2 (%3).").arg(m_destination, user(), platform());
    case State::Offline:
        return tr("“%1” is unreachable; trying again: %2").arg(m_destination, m_error);
    case State::Failed:
        return m_error;
    }
    return {};
}

Connection *Host::connection() const noexcept
{
    return state() == State::Online ? m_connection.get() : nullptr;
}

void Host::setState(State state)
{
    if (this->state() == state) {
        return;
    }
    m_state = static_cast<quint64>(state);
    emit stateChanged();
    tellWaiting();
}

QUrl Host::fileUrl(const QString &path) const
{
    return ssh::fileUrl(m_destination, path);
}

void Host::whenOnline(const QObject *context, Ready ready)
{
    m_waiting.emplace_back(Waiting{.context = context, .ready = std::move(ready)});
    if (state() == State::Online) {
        tellWaiting();
        return;
    }
    connectToHost();
}

void Host::tellWaiting()
{
    const bool online{state() == State::Online};
    if (!online && state() != State::Failed && state() != State::Disconnected) {
        return;
    }
    // Each may ask again: they wait for the next time.
    const QList<Waiting> waiting{std::exchange(m_waiting, {})};
    for (const Waiting &waiter : waiting) {
        if (waiter.context == nullptr) {
            continue;
        }
        if (online) {
            waiter.ready(std::ref(*m_connection));
        } else {
            waiter.ready(std::unexpected{m_error.isEmpty() ? tr("Not connected to “%1”.").arg(m_destination) : m_error}
            );
        }
    }
}

void Host::connectToHost()
{
    if (state() == State::Connecting || state() == State::Installing || state() == State::Online) {
        return;
    }
    m_retry.stop();
    m_error.clear();
    if (!ssh::validDestination(m_destination)) {
        fail(tr("“%1” is not a host name ssh accepts.").arg(m_destination));
        return;
    }
    setState(State::Connecting);
    probe();
}

void Host::disconnectFromHost()
{
    m_retry.stop();
    m_wasOnline = 0U;
    m_backoff = 1;
    m_error.clear();
    setState(State::Disconnected);
    m_ssh.kill();
    if (m_connection != nullptr) {
        m_connection->disconnect(this);
        m_connection.release()->deleteLater();
    }
}

void Host::reconnectNow()
{
    if (state() == State::Offline) {
        connectToHost();
    }
}

void Host::probe()
{
    connect(&m_ssh, &QProcess::finished, this, &Host::probed, Qt::SingleShotConnection);
    m_ssh.start(m_setup.ssh, ssh::arguments(m_destination, m_setup.controlFolder, ssh::probeCommand(m_setup.version)));
}

QString Host::sshError()
{
    QStringList lines{QString::fromUtf8(m_ssh.readAllStandardError()).split(u'\n')};
    lines.removeIf([](const QString &line) {
        return line.trimmed().isEmpty() || line.startsWith(QLatin1String("Warning: Permanently added"));
    });
    if (lines.isEmpty()) {
        return tr("ssh could not reach “%1” (exit code %2).").arg(m_destination).arg(m_ssh.exitCode());
    }
    return lines.join(u'\n').trimmed();
}

void Host::probed()
{
    if (state() != State::Connecting) {
        return;
    }
    if (m_ssh.exitStatus() != QProcess::NormalExit || m_ssh.exitCode() != 0) {
        fail(sshError());
        return;
    }
    const std::optional<ssh::Probe> probe{ssh::parseProbe(m_ssh.readAllStandardOutput())};
    if (!probe) {
        fail(tr("“%1” did not answer as a POSIX shell would.").arg(m_destination));
        return;
    }
    m_os = probe->os;
    m_arch = probe->arch;
    m_home = probe->home;
    emit stateChanged();
    const QString agent{m_setup.agents + u'/' + m_os + u'-' + m_arch + QStringLiteral("/kode-remote")};
    if (!QFile::exists(agent)) {
        fail(tr("This build of Kode has no agent for %1.").arg(platform()));
        return;
    }
    if (hashOf(agent) == probe->hash) {
        startBridge();
        return;
    }
    install(agent);
}

void Host::install(const QString &agent)
{
    QFile file{agent};
    if (!file.open(QIODevice::ReadOnly)) {
        fail(tr("The agent for %1 could not be read: %2").arg(platform(), file.errorString()));
        return;
    }
    const QByteArray bytes{file.readAll()};
    const QString expected{QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())};
    setState(State::Installing);
    connect(&m_ssh, &QProcess::finished, this, [this, expected] { installed(expected); }, Qt::SingleShotConnection);
    m_ssh.start(
        m_setup.ssh,
        ssh::arguments(m_destination, m_setup.controlFolder, ssh::installCommand(m_setup.version))
    );
    m_ssh.write(bytes);
    m_ssh.closeWriteChannel();
}

void Host::installed(const QString &expected)
{
    if (state() != State::Installing) {
        return;
    }
    if (m_ssh.exitStatus() != QProcess::NormalExit || m_ssh.exitCode() != 0) {
        fail(sshError());
        return;
    }
    if (QString::fromLatin1(m_ssh.readAllStandardOutput()).trimmed() != expected) {
        fail(tr("The agent installed on “%1” does not match the one Kode sent.").arg(m_destination));
        return;
    }
    startBridge();
}

void Host::startBridge()
{
    setState(State::Connecting);
    m_connection = std::make_unique<Connection>();
    m_connection->setEnvironment(m_setup.environment);
    connect(m_connection.get(), &Connection::stateChanged, this, &Host::connectionChanged);
    connect(m_connection.get(), &Connection::notified, this, [this](const QString &method, const QJsonValue &params) {
        if (method != QLatin1String("watch/changed")) {
            return;
        }
        const QJsonArray paths(params.toObject().value(QStringLiteral("paths")).toArray());
        std::ranges::for_each(paths, [this](const QJsonValue &path) { emit changed(path.toString()); });
    });
    m_connection->start(
        m_setup.ssh,
        ssh::arguments(m_destination, m_setup.controlFolder, ssh::bridgeCommand(m_home, m_setup.version))
    );
}

void Host::connectionChanged()
{
    switch (m_connection->state()) {
    case Connection::State::Online:
        m_wasOnline = 1U;
        m_backoff = 1;
        setState(State::Online);
        // A new session: what is watched is told again.
        sendWatches();
        return;
    case Connection::State::Closed:
        fail(m_connection->errorString());
        return;
    case Connection::State::Idle:
    case Connection::State::Connecting:
        return;
    }
}

void Host::fail(const QString &message)
{
    m_error = message;
    // A step that never started leaves its handler waiting.
    disconnect(&m_ssh, &QProcess::finished, this, nullptr);
    if (m_connection != nullptr) {
        // It may be the one telling.
        m_connection->disconnect(this);
        m_connection.release()->deleteLater();
    }
    if (m_wasOnline == 0U || !ssh::isTransient(message)) {
        setState(State::Failed);
        return;
    }
    setState(State::Offline);
    m_retry.start(static_cast<qint32>(m_backoff) * millisecondsPerSecond);
    m_backoff = std::min(m_backoff * 2, longestBackoff);
}

} // namespace kode::remote
