#include "tst_remote.h"

#include "../remote/protocol/Frame.h"
#include "../remote/protocol/Methods.h"
#include "remote/Connection.h"
#include "remote/Host.h"
#include "remote/Hosts.h"
#include "remote/Ssh.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

using kode::remote::Connection;
using kode::remote::Frame;
using kode::remote::FrameReader;
using kode::remote::FrameType;
using kode::remote::Host;
using kode::remote::Hosts;

namespace
{

const QString agentPath{QStringLiteral(KODE_REMOTE_AGENT)};
const QString askpassPath{QStringLiteral(KODE_ASKPASS)};

// A stand-in for ssh: skips the options and the destination, runs the
// remote command here (HOME and XDG_RUNTIME_DIR from the environment).
constexpr QStringView fakeSsh{u"#!/bin/sh\n"
                              u"while [ $# -gt 0 ]; do if [ \"$1\" = -- ]; then shift; break; fi; shift; done\n"
                              u"shift\n"
                              u"exec /bin/sh -c \"$*\"\n"};

constexpr QStringView refusingSsh{u"#!/bin/sh\necho 'me@box: Permission denied (publickey).' >&2\nexit 255\n"};

QString writeScript(const QTemporaryDir &folder, const QString &name, QStringView script)
{
    const QString path{folder.filePath(name)};
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(script.toUtf8());
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

// A host whose ssh is `ssh`, its home and runtime folder in `folder`, with this build's agent.
kode::remote::Setup setupIn(const QTemporaryDir &folder, const QString &ssh)
{
    QDir{}.mkpath(folder.filePath(QStringLiteral("home")));
    QDir{}.mkpath(folder.filePath(QStringLiteral("runtime")));
    QDir{}.mkpath(folder.filePath(QStringLiteral("agents/darwin-arm64")));
    QFile::copy(agentPath, folder.filePath(QStringLiteral("agents/darwin-arm64/kode-remote")));
    QProcessEnvironment environment{QProcessEnvironment::systemEnvironment()};
    environment.insert(QStringLiteral("HOME"), folder.filePath(QStringLiteral("home")));
    environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), folder.filePath(QStringLiteral("runtime")));
    return kode::remote::Setup{
        .environment = environment,
        .ssh = ssh,
        .controlFolder = folder.path(),
        .agents = folder.filePath(QStringLiteral("agents")),
        .version = QStringLiteral("test"),
    };
}

QString installedAgent(const QTemporaryDir &folder)
{
    return folder.filePath(QStringLiteral("home/.cache/kode/remote/test/kode-remote"));
}

// The daemons the bridge started from `folder`'s installed agent.
void stopDaemons(const QTemporaryDir &folder)
{
    QProcess::execute(
        QStringLiteral("/usr/bin/pkill"),
        {QStringLiteral("-f"), installedAgent(folder) + QStringLiteral(" daemon")}
    );
}

std::string frameOf(FrameType type, std::uint32_t channel, std::string_view payload)
{
    std::string out;
    kode::remote::appendFrame(out, type, channel, payload);
    return out;
}

QByteArray messageFrame(const QJsonObject &message)
{
    const QByteArray json{QJsonDocument{message}.toJson(QJsonDocument::Compact)};
    const std::string frame{
        frameOf(FrameType::Message, 0U, std::string_view{json.constData(), static_cast<std::size_t>(json.size())})
    };
    return QByteArray{frame.data(), static_cast<qsizetype>(frame.size())};
}

// The result (or error) of `method` on an online connection.
Connection::Result ask(Connection &connection, const QString &method)
{
    std::optional<Connection::Result> answer;
    connection.request(method, {}, [&answer](const Connection::Result &result) { answer = result; });
    if (!QTest::qWaitFor([&answer] { return answer.has_value(); })) {
        return std::unexpected{kode::remote::Error{.message = QStringLiteral("no answer"), .code = 0}};
    }
    return *answer;
}

} // namespace

void TestRemote::readsFramesArrivingInPieces()
{
    std::string stream;
    kode::remote::appendFrame(stream, FrameType::Message, 0U, R"({"id":1})");
    kode::remote::appendFrame(stream, FrameType::Data, 7U, std::string(1000, 'x'));
    kode::remote::appendFrame(stream, FrameType::Eof, 7U, {});
    for (qint32 round{0}; round < 50; ++round) {
        FrameReader reader;
        std::vector<Frame> frames;
        for (std::size_t at{0U}; at < stream.size();) {
            const std::size_t piece{
                std::min<std::size_t>(QRandomGenerator::global()->bounded(1, 64), stream.size() - at)
            };
            reader.append(std::string_view{stream}.substr(at, piece));
            at += piece;
            for (std::optional<Frame> frame{reader.next()}; frame; frame = reader.next()) {
                frames.emplace_back(std::move(*frame));
            }
        }
        QVERIFY(!reader.failed());
        QCOMPARE(frames.size(), std::size_t{3});
        QCOMPARE(frames[0].payload, std::string{R"({"id":1})"});
        QCOMPARE(frames[1].type(), FrameType::Data);
        QCOMPARE(frames[1].channel, 7U);
        QCOMPARE(frames[1].payload.size(), std::size_t{1000});
        QCOMPARE(frames[2].type(), FrameType::Eof);
    }
}

void TestRemote::refusesOversizedAndUnknownFrames()
{
    // A Data frame's header claiming more than allowed: refused before its bytes arrive.
    std::string oversized{frameOf(FrameType::Data, 1U, {})};
    const std::uint32_t length{static_cast<std::uint32_t>(kode::remote::maxDataSize + 6U)};
    oversized[0] = static_cast<char>(length >> 24U);
    oversized[1] = static_cast<char>((length >> 16U) & 0xFFU);
    oversized[2] = static_cast<char>((length >> 8U) & 0xFFU);
    oversized[3] = static_cast<char>(length & 0xFFU);
    FrameReader reader;
    reader.append(oversized);
    QVERIFY(!reader.next());
    QVERIFY(reader.failed());

    std::string unknown{frameOf(FrameType::Eof, 1U, {})};
    unknown[4] = 9;
    FrameReader second;
    second.append(unknown);
    QVERIFY(!second.next());
    QVERIFY(second.failed());

    // An Eof with a payload.
    FrameReader third;
    third.append(frameOf(FrameType::Eof, 1U, "x"));
    QVERIFY(!third.next());
    QVERIFY(third.failed());
}

void TestRemote::handshakes()
{
    Connection connection;
    connection.start(agentPath, {QStringLiteral("serve")});
    QTRY_COMPARE(connection.state(), Connection::State::Online);
    const QJsonObject agent{connection.agent()};
    QCOMPARE(agent.value(QStringLiteral("protocol")).toInt(), kode::remote::protocolVersion);
    QCOMPARE(agent.value(QStringLiteral("session")).toString().size(), 32);
    QCOMPARE(
        agent.value(QStringLiteral("platform")).toObject().value(QStringLiteral("os")).toString(),
        QStringLiteral("darwin")
    );
    QVERIFY(agent.value(QStringLiteral("capabilities")).toObject().contains(QStringLiteral("system")));
}

void TestRemote::answersRequests()
{
    Connection connection;
    connection.start(agentPath, {QStringLiteral("serve")});
    // Asked before the answer to initialize: it follows it.
    const Connection::Result pong{ask(connection, QStringLiteral("system/ping"))};
    QVERIFY(pong.has_value());
    QVERIFY(pong->isObject());
    const Connection::Result missing{ask(connection, QStringLiteral("system/nothing"))};
    QVERIFY(!missing.has_value());
    QCOMPARE(missing.error().code, kode::remote::error::methodNotFound);
    const Connection::Result noService{ask(connection, QStringLiteral("nothing/here"))};
    QVERIFY(!noService.has_value());
    QCOMPARE(noService.error().code, kode::remote::error::methodNotFound);
}

void TestRemote::refusesRequestsBeforeInitialize()
{
    QProcess agent;
    agent.start(agentPath, {QStringLiteral("serve")});
    QVERIFY(agent.waitForStarted());
    agent.write(messageFrame({
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("system/ping")},
    }));
    QByteArray answer;
    QVERIFY(QTest::qWaitFor([&] {
        answer.append(agent.readAllStandardOutput());
        return answer.size() > qsizetype{kode::remote::headerSize};
    }));
    QVERIFY(answer.contains(QByteArray::number(kode::remote::error::notInitialized)));
    agent.closeWriteChannel();
    QVERIFY(agent.waitForFinished());
}

void TestRemote::endsOnABrokenFrame()
{
    QProcess agent;
    agent.start(agentPath, {QStringLiteral("serve")});
    QVERIFY(agent.waitForStarted());
    std::string broken{frameOf(FrameType::Eof, 0U, {})};
    broken[4] = 42;
    agent.write(broken.data(), static_cast<qint64>(broken.size()));
    QVERIFY(agent.waitForFinished());
}

void TestRemote::failsPendingRequestsOnClose()
{
    Connection connection;
    connection.start(agentPath, {QStringLiteral("serve")});
    std::optional<Connection::Result> answer;
    connection.request(QStringLiteral("system/ping"), {}, [&answer](const Connection::Result &result) {
        answer = result;
    });
    connection.close();
    QVERIFY(answer.has_value());
    QVERIFY(!answer->has_value());
    QCOMPARE(answer->error().code, kode::remote::error::disconnected);
    QCOMPARE(connection.state(), Connection::State::Closed);
}

void TestRemote::bridgesToTheDaemon()
{
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
    QProcess daemon;
    daemon.start(agentPath, {QStringLiteral("daemon"), QStringLiteral("--idle"), QStringLiteral("1")});
    QVERIFY(daemon.waitForStarted());
    {
        Connection first;
        Connection second;
        first.start(agentPath, {QStringLiteral("bridge")});
        second.start(agentPath, {QStringLiteral("bridge")});
        QTRY_COMPARE(first.state(), Connection::State::Online);
        QTRY_COMPARE(second.state(), Connection::State::Online);
        QVERIFY(first.agent().value(QStringLiteral("session")) != second.agent().value(QStringLiteral("session")));
        QVERIFY(ask(first, QStringLiteral("system/ping")).has_value());
        QVERIFY(QFileInfo{runtime.filePath(QStringLiteral("kode"))}.isDir());
        QCOMPARE(
            QFile::permissions(runtime.filePath(QStringLiteral("kode"))) & (QFile::ReadGroup | QFile::ReadOther),
            QFile::Permissions{}
        );
    }
    // Idle once both sessions ended: it exits.
    QVERIFY(daemon.waitForFinished(5000));
    QCOMPARE(daemon.exitCode(), 0);
    qunsetenv("XDG_RUNTIME_DIR");
}

void TestRemote::refusesAnOpenRuntimeFolder()
{
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    const QString folder{runtime.filePath(QStringLiteral("kode"))};
    QVERIFY(QDir{}.mkdir(folder));
    QVERIFY(::chmod(folder.toUtf8().constData(), 0755) == 0);
    qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
    Connection connection;
    connection.start(agentPath, {QStringLiteral("bridge")});
    QTRY_COMPARE(connection.state(), Connection::State::Closed);
    QVERIFY2(connection.errorString().contains(QStringLiteral("refused")), qPrintable(connection.errorString()));
    qunsetenv("XDG_RUNTIME_DIR");
}

void TestRemote::cleanup()
{
    qunsetenv("XDG_RUNTIME_DIR");
}

void TestRemote::validatesDestinations()
{
    using kode::remote::ssh::validDestination;
    QVERIFY(validDestination(QStringLiteral("box")));
    QVERIFY(validDestination(QStringLiteral("me@build-01.example.com")));
    QVERIFY(!validDestination(QStringLiteral("-oProxyCommand=touch /tmp/x")));
    QVERIFY(!validDestination(QStringLiteral("box; rm -rf ~")));
    QVERIFY(!validDestination(QStringLiteral("a b")));
    QVERIFY(!validDestination(QString{}));
}

void TestRemote::quotesForSh()
{
    QProcess sh;
    sh.start(
        QStringLiteral("/bin/sh"),
        {QStringLiteral("-c"), QStringLiteral("printf %s ") + kode::remote::ssh::quote(QStringLiteral("it's $HOME `x`"))
        }
    );
    QVERIFY(sh.waitForFinished());
    QCOMPARE(QString::fromUtf8(sh.readAllStandardOutput()), QStringLiteral("it's $HOME `x`"));
}

void TestRemote::parsesTheProbe()
{
    const auto probe{kode::remote::ssh::parseProbe("Linux\naarch64\n/home/me\nnone\n")};
    QVERIFY(probe.has_value());
    QCOMPARE(probe->os, QStringLiteral("linux"));
    QCOMPARE(probe->arch, QStringLiteral("arm64"));
    QCOMPARE(probe->home, QStringLiteral("/home/me"));
    QVERIFY(probe->hash.isEmpty());
    QVERIFY(!kode::remote::ssh::parseProbe("Linux\nx86_64\n").has_value());
    QVERIFY(!kode::remote::ssh::parseProbe("Linux\nx86_64\nrelative\nnone\n").has_value());
}

void TestRemote::readsConfiguredHosts()
{
    QTemporaryDir folder;
    QDir{}.mkpath(folder.filePath(QStringLiteral("config.d")));
    QFile main{folder.filePath(QStringLiteral("config"))};
    QVERIFY(main.open(QIODevice::WriteOnly));
    main.write("# hosts\nHost box build-*\n  HostName 10.0.0.2\nInclude config.d/*\nHost=work !old\nHost *\n  "
               "ServerAliveInterval 5\n");
    main.close();
    QFile included{folder.filePath(QStringLiteral("config.d/extra"))};
    QVERIFY(included.open(QIODevice::WriteOnly));
    included.write("Host pi box\n");
    included.close();
    QCOMPARE(
        kode::remote::ssh::configuredHosts(main.fileName()),
        (QStringList{QStringLiteral("box"), QStringLiteral("pi"), QStringLiteral("work")})
    );
}

void TestRemote::installsTheAgent()
{
    QTemporaryDir folder;
    {
        Host host{QStringLiteral("box"), setupIn(folder, writeScript(folder, QStringLiteral("ssh"), fakeSsh))};
        QSignalSpy states{&host, &Host::stateChanged};
        host.connectToHost();
        QTRY_COMPARE_WITH_TIMEOUT(host.state(), Host::State::Online, 10000);
        QCOMPARE(host.platform(), QStringLiteral("macOS arm64"));
        QVERIFY(!host.user().isEmpty());
        QVERIFY(host.connection() != nullptr);
        QCOMPARE(
            QFile::permissions(installedAgent(folder)) & (QFile::ReadGroup | QFile::ReadOther | QFile::ExeOther),
            QFile::Permissions{}
        );
        host.disconnectFromHost();
        QCOMPARE(host.state(), Host::State::Disconnected);
        // Found by its hash: not installed again.
        bool installing{false};
        connect(&host, &Host::stateChanged, &host, [&] {
            installing = installing || host.state() == Host::State::Installing;
        });
        host.connectToHost();
        QTRY_COMPARE_WITH_TIMEOUT(host.state(), Host::State::Online, 10000);
        QVERIFY(!installing);
    }
    stopDaemons(folder);
}

void TestRemote::reinstallsAChangedAgent()
{
    QTemporaryDir folder;
    {
        Host host{QStringLiteral("box"), setupIn(folder, writeScript(folder, QStringLiteral("ssh"), fakeSsh))};
        host.connectToHost();
        QTRY_COMPARE_WITH_TIMEOUT(host.state(), Host::State::Online, 10000);
        host.disconnectFromHost();
        stopDaemons(folder);
        QFile agent{installedAgent(folder)};
        QVERIFY(agent.open(QIODevice::Append));
        agent.write("changed");
        agent.close();
        bool installing{false};
        connect(&host, &Host::stateChanged, &host, [&] {
            installing = installing || host.state() == Host::State::Installing;
        });
        host.connectToHost();
        QTRY_COMPARE_WITH_TIMEOUT(host.state(), Host::State::Online, 10000);
        QVERIFY(installing);
    }
    stopDaemons(folder);
}

void TestRemote::failsWithoutAnAgentForThePlatform()
{
    QTemporaryDir folder;
    kode::remote::Setup setup{setupIn(folder, writeScript(folder, QStringLiteral("ssh"), fakeSsh))};
    setup.agents = folder.filePath(QStringLiteral("none"));
    Host host{QStringLiteral("box"), setup};
    host.connectToHost();
    QTRY_COMPARE(host.state(), Host::State::Failed);
    QVERIFY2(host.errorString().contains(QStringLiteral("no agent for macOS arm64")), qPrintable(host.errorString()));
}

void TestRemote::failsOnAPermanentError()
{
    QTemporaryDir folder;
    Host host{QStringLiteral("box"), setupIn(folder, writeScript(folder, QStringLiteral("ssh"), refusingSsh))};
    host.connectToHost();
    QTRY_COMPARE(host.state(), Host::State::Failed);
    QVERIFY(host.errorString().contains(QStringLiteral("Permission denied")));
    QVERIFY(!kode::remote::ssh::isTransient(host.errorString()));
}

void TestRemote::reconnectsAfterALoss()
{
    QTemporaryDir folder;
    {
        Host host{QStringLiteral("box"), setupIn(folder, writeScript(folder, QStringLiteral("ssh"), fakeSsh))};
        host.connectToHost();
        QTRY_COMPARE_WITH_TIMEOUT(host.state(), Host::State::Online, 10000);
        const QString session{host.connection()->agent().value(QStringLiteral("session")).toString()};
        stopDaemons(folder);
        QTRY_COMPARE_WITH_TIMEOUT(host.state(), Host::State::Offline, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(host.state(), Host::State::Online, 10000);
        QVERIFY(host.connection()->agent().value(QStringLiteral("session")).toString() != session);
    }
    stopDaemons(folder);
}

void TestRemote::answersAskpass()
{
    Hosts hosts;
    const QProcessEnvironment environment{hosts.setup(QStringLiteral("box")).environment};
    QSignalSpy prompts{&hosts, &Hosts::promptRequested};
    QProcess askpass;
    askpass.setProcessEnvironment(environment);
    askpass.start(askpassPath, {QStringLiteral("me@box's password: ")});
    QTRY_COMPARE(prompts.count(), 1);
    QCOMPARE(prompts.at(0).at(1).toString(), QStringLiteral("box"));
    QCOMPARE(prompts.at(0).at(3).value<Hosts::Prompt>(), Hosts::Prompt::Secret);
    hosts.answer(prompts.at(0).at(0).toInt(), true, QStringLiteral("s3cret"));
    QTRY_COMPARE(askpass.state(), QProcess::NotRunning);
    QCOMPARE(askpass.exitCode(), 0);
    QCOMPARE(askpass.readAllStandardOutput(), QByteArray{"s3cret\n"});

    QProcess cancelled;
    cancelled.setProcessEnvironment(environment);
    cancelled.start(askpassPath, {QStringLiteral("Enter passphrase for key: ")});
    QTRY_COMPARE(prompts.count(), 2);
    hosts.answer(prompts.at(1).at(0).toInt(), false, {});
    QTRY_COMPARE(cancelled.state(), QProcess::NotRunning);
    QCOMPARE(cancelled.exitCode(), 1);
    QVERIFY(cancelled.readAllStandardOutput().isEmpty());
}

void TestRemote::refusesAskpassWithAWrongToken()
{
    Hosts hosts;
    QProcessEnvironment environment{hosts.setup(QStringLiteral("box")).environment};
    environment.insert(QStringLiteral("KODE_ASKPASS_TOKEN"), QStringLiteral("guessed"));
    QSignalSpy prompts{&hosts, &Hosts::promptRequested};
    QProcess askpass;
    askpass.setProcessEnvironment(environment);
    askpass.start(askpassPath, {QStringLiteral("Password: ")});
    QTRY_COMPARE(askpass.state(), QProcess::NotRunning);
    QCOMPARE(askpass.exitCode(), 1);
    QCOMPARE(prompts.count(), 0);
}

void TestRemote::tellsPromptKinds()
{
    Hosts hosts;
    const QProcessEnvironment environment{hosts.setup(QStringLiteral("box")).environment};
    QSignalSpy prompts{&hosts, &Hosts::promptRequested};
    const QList<std::pair<QString, Hosts::Prompt>> cases{
        {QStringLiteral("Are you sure you want to continue connecting (yes/no/[fingerprint])? "), Hosts::Prompt::Confirm
        },
        {QStringLiteral("Verification code: "), Hosts::Prompt::Secret},
        {QStringLiteral("Username: "), Hosts::Prompt::Text},
    };
    std::vector<std::unique_ptr<QProcess>> processes;
    for (const auto &[text, kind] : cases) {
        auto process{std::make_unique<QProcess>()};
        process->setProcessEnvironment(environment);
        process->start(askpassPath, {text});
        processes.emplace_back(std::move(process));
    }
    // One at a time: the next shows once the one before is answered.
    for (qsizetype i{0}; i < cases.size(); ++i) {
        QTRY_COMPARE(prompts.count(), i + 1);
        QTest::qWait(50);
        QCOMPARE(prompts.count(), i + 1);
        // They may arrive in any order: each kind by its text.
        const QString text{prompts.at(i).at(2).toString()};
        const auto expected{std::ranges::find(cases, text, &std::pair<QString, Hosts::Prompt>::first)};
        QVERIFY(expected != cases.end());
        QCOMPARE(prompts.at(i).at(3).value<Hosts::Prompt>(), expected->second);
        hosts.answer(prompts.at(i).at(0).toInt(), false, {});
    }
    // Waited for with the event loop running: the answers are written from it.
    QTRY_VERIFY(std::ranges::all_of(processes, [](const std::unique_ptr<QProcess> &process) {
        return process->state() == QProcess::NotRunning;
    }));
}

QTEST_GUILESS_MAIN(TestRemote)
