#include "SshKey.h"

#include "Hosts.h"
#include "Ssh.h"

#include <QDir>
#include <QFile>
#include <QHostInfo>

#include <algorithm>
#include <array>

namespace kode::remote
{

SshKey::SshKey(QObject *parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::finished, this, &SshKey::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finished(-1, QProcess::CrashExit);
        }
    });
}

SshKey::~SshKey()
{
    m_process.disconnect(this);
    m_process.kill();
    m_process.waitForFinished(1000);
}

void SshKey::setDestination(const QString &destination)
{
    if (destination == m_destination) {
        return;
    }
    // The step under way is for the other host: its end is ignored.
    m_step = static_cast<qint64>(Step::None);
    m_process.kill();
    m_process.waitForFinished(1000);
    m_destination = destination;
    emit destinationChanged();
    set(State::Unknown, {});
}

QString SshKey::publicKeyPath()
{
    constexpr std::array names{"id_ed25519.pub", "id_ecdsa.pub", "id_rsa.pub"};
    const QDir folder{QDir::home().filePath(QStringLiteral(".ssh"))};
    const auto found{std::ranges::find_if(names, [&folder](const char *name) {
        return QFile::exists(folder.filePath(QLatin1StringView{name}));
    })};
    return found == names.end() ? QString{} : folder.filePath(QLatin1StringView{*found});
}

void SshKey::check()
{
    if (m_destination.isEmpty() || m_process.state() != QProcess::NotRunning) {
        return;
    }
    set(State::Checking, tr("Checking…"));
    run(Step::Checking, QStringLiteral("/usr/bin/ssh"), ssh::keyCheckArguments(m_destination));
}

void SshKey::install()
{
    if (m_destination.isEmpty() || m_process.state() != QProcess::NotRunning) {
        return;
    }
    if (!publicKeyPath().isEmpty()) {
        copy();
        return;
    }
    set(State::Installing, tr("Making a key for this Mac…"));
    QDir{}.mkpath(QDir::home().filePath(QStringLiteral(".ssh")));
    QFile::setPermissions(
        QDir::home().filePath(QStringLiteral(".ssh")),
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
    );
    run(Step::Generating,
        QStringLiteral("/usr/bin/ssh-keygen"),
        {QStringLiteral("-q"),
         QStringLiteral("-t"),
         QStringLiteral("ed25519"),
         QStringLiteral("-N"),
         QString{},
         QStringLiteral("-C"),
         QStringLiteral("kode@") + QHostInfo::localHostName(),
         QStringLiteral("-f"),
         QDir::home().filePath(QStringLiteral(".ssh/id_ed25519"))});
}

void SshKey::copy()
{
    QFile file{publicKeyPath()};
    if (!file.open(QIODevice::ReadOnly)) {
        set(State::NoKey, tr("This Mac’s public key could not be read: %1").arg(file.errorString()));
        return;
    }
    const QByteArray key{file.readAll().trimmed()};
    set(State::Installing, tr("Adding this Mac’s key on “%1”…").arg(m_destination));
    const Setup setup{Hosts::shared().setup(m_destination)};
    m_process.setProcessEnvironment(setup.environment);
    run(Step::Copying, setup.ssh, ssh::arguments(m_destination, setup.controlFolder, ssh::keyInstallCommand()));
    m_process.write(key + '\n');
    m_process.closeWriteChannel();
}

void SshKey::run(Step step, const QString &program, const QStringList &arguments)
{
    m_step = static_cast<qint64>(step);
    m_process.start(program, arguments);
}

void SshKey::finished(qint32 exitCode, QProcess::ExitStatus status)
{
    const auto step{static_cast<Step>(m_step)};
    m_step = static_cast<qint64>(Step::None);
    const bool ok{status == QProcess::NormalExit && exitCode == 0};
    const QString output{QString::fromLocal8Bit(m_process.readAll()).trimmed()};
    m_process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    switch (step) {
    case Step::Generating:
        if (ok) {
            copy();
        } else {
            set(State::NoKey, tr("ssh-keygen failed: %1").arg(output));
        }
        return;
    case Step::Copying:
        if (ok) {
            set(State::Checking, tr("Checking…"));
            run(Step::Checking, QStringLiteral("/usr/bin/ssh"), ssh::keyCheckArguments(m_destination));
        } else {
            set(State::NoKey, tr("The key could not be added: %1").arg(output));
        }
        return;
    case Step::Checking:
        set(ok ? State::Works : State::NoKey,
            ok ? tr("Signs in with this Mac’s key: no password asked.")
               : tr("Asks for a password: install this Mac’s key to sign in without one."));
        return;
    case Step::None:
        return;
    }
}

void SshKey::set(State state, const QString &message)
{
    m_state = static_cast<qint64>(state);
    m_message = message;
    emit stateChanged();
}

} // namespace kode::remote
