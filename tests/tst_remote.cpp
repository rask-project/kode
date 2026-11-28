#include "tst_remote.h"

#include "../remote/protocol/Frame.h"
#include "../remote/protocol/Methods.h"
#include "Document.h"
#include "FileSearch.h"
#include "FileTree.h"
#include "LanguageServers.h"
#include "TextDocument.h"
#include "remote/Connection.h"
#include "remote/Files.h"
#include "remote/Host.h"
#include "remote/Hosts.h"
#include "remote/RemoteProcess.h"
#include "remote/Ssh.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

#include <csignal>

using kode::remote::Connection;
using kode::remote::Frame;
using kode::remote::FrameReader;
using kode::remote::FrameType;
using kode::remote::Host;
using kode::remote::Hosts;
using namespace Qt::StringLiterals;

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
        return std::unexpected{kode::remote::Error{.message = QStringLiteral("no answer"), .data = {}, .code = 0}};
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

namespace
{

namespace files = kode::remote::files;

// An agent over stdio, online.
std::unique_ptr<Connection> serving()
{
    auto connection{std::make_unique<Connection>()};
    connection->start(agentPath, {QStringLiteral("serve")});
    return connection;
}

template <typename T> std::optional<T> waitFor(std::optional<T> &answer)
{
    static_cast<void>(QTest::qWaitFor([&answer] { return answer.has_value(); }, 10000));
    return answer;
}

std::expected<files::Version, kode::remote::Error> writeFile(
    Connection &connection,
    const QString &path,
    const QByteArray &bytes,
    const std::optional<files::Version> &expect = {}
)
{
    std::optional<std::expected<files::Version, kode::remote::Error>> answer;
    files::write(
        connection,
        path,
        bytes,
        expect,
        true,
        [&answer](std::expected<files::Version, kode::remote::Error> result) { answer = std::move(result); }
    );
    return waitFor(answer).value_or(
        std::unexpected{kode::remote::Error{.message = QStringLiteral("no answer"), .data = {}, .code = 0}}
    );
}

std::expected<files::Read, kode::remote::Error> readFile(Connection &connection, const QString &path)
{
    std::optional<std::expected<files::Read, kode::remote::Error>> answer;
    files::read(connection, path, [&answer](std::expected<files::Read, kode::remote::Error> result) {
        answer = std::move(result);
    });
    return waitFor(answer).value_or(
        std::unexpected{kode::remote::Error{.message = QStringLiteral("no answer"), .data = {}, .code = 0}}
    );
}

QByteArray contentsOf(const QString &path)
{
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

} // namespace

void TestRemote::writesAndReadsFiles()
{
    QTemporaryDir folder;
    const auto connection{serving()};
    const QString path{folder.filePath(QStringLiteral("new/folders/notes.txt"))};
    const QByteArray bytes{"one\ntwo\n"};
    const auto written{writeFile(*connection, path, bytes)};
    QVERIFY2(written.has_value(), qPrintable(written ? QString{} : written.error().message));
    QCOMPARE(contentsOf(path), bytes);
    QCOMPARE(written->size, bytes.size());
    const auto read{readFile(*connection, path)};
    QVERIFY(read.has_value());
    QCOMPARE(read->bytes, bytes);
    QCOMPARE(read->version.hash, written->hash);
    QVERIFY(read->version.sameAs(*written));
    // No temporary file left beside it.
    QCOMPARE(
        QDir{QFileInfo{path}.path()}.entryList(QDir::Files | QDir::Hidden),
        QStringList{QStringLiteral("notes.txt")}
    );
}

void TestRemote::movesLargeFiles()
{
    QTemporaryDir folder;
    const auto connection{serving()};
    QByteArray bytes(5 * 1024 * 1024 + 123, Qt::Uninitialized);
    QRandomGenerator::global()->generate(bytes.begin(), bytes.end());
    const QString path{folder.filePath(QStringLiteral("large.bin"))};
    QVERIFY(writeFile(*connection, path, bytes).has_value());
    QCOMPARE(contentsOf(path), bytes);
    const auto read{readFile(*connection, path)};
    QVERIFY(read.has_value());
    QCOMPARE(read->bytes.size(), bytes.size());
    QVERIFY(read->bytes == bytes);
}

void TestRemote::refusesAConflictingWrite()
{
    QTemporaryDir folder;
    const auto connection{serving()};
    const QString path{folder.filePath(QStringLiteral("a.txt"))};
    const auto first{writeFile(*connection, path, "first\n")};
    QVERIFY(first.has_value());
    QFile other{path};
    QVERIFY(other.open(QIODevice::Append));
    other.write("from elsewhere\n");
    other.close();
    const auto second{writeFile(*connection, path, "mine\n", *first)};
    QVERIFY(!second.has_value());
    QCOMPARE(second.error().code, kode::remote::error::conflict);
    QCOMPARE(files::versionOf(second.error().data.toObject()).size, qint64{21});
    QCOMPARE(contentsOf(path), QByteArray{"first\nfrom elsewhere\n"});
    QCOMPARE(QDir{folder.path()}.entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("a.txt")});
}

void TestRemote::writesATouchedFile()
{
    QTemporaryDir folder;
    const auto connection{serving()};
    const QString path{folder.filePath(QStringLiteral("a.txt"))};
    const auto first{writeFile(*connection, path, "same\n")};
    QVERIFY(first.has_value());
    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
    file.close();
    QVERIFY(writeFile(*connection, path, "changed\n", *first).has_value());
    QCOMPARE(contentsOf(path), QByteArray{"changed\n"});
}

void TestRemote::keepsTheModeAndWritesThroughLinks()
{
    QTemporaryDir folder;
    const auto connection{serving()};
    const QString path{folder.filePath(QStringLiteral("real.txt"))};
    const QString link{folder.filePath(QStringLiteral("link.txt"))};
    QVERIFY(writeFile(*connection, path, "a\n").has_value());
    QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
    QVERIFY(QFile::link(path, link));
    QVERIFY(writeFile(*connection, link, "b\n").has_value());
    QVERIFY(QFileInfo{link}.isSymLink());
    QCOMPARE(contentsOf(path), QByteArray{"b\n"});
    QCOMPARE(
        QFile::permissions(path) &
            (QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup),
        QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup
    );
}

void TestRemote::writesInPlaceInAReadOnlyFolder()
{
    QTemporaryDir folder;
    const auto connection{serving()};
    const QString sub{folder.filePath(QStringLiteral("locked"))};
    const QString path{sub + QStringLiteral("/a.txt")};
    QVERIFY(writeFile(*connection, path, "before\n").has_value());
    QVERIFY(::chmod(sub.toUtf8().constData(), 0555) == 0);
    const auto written{writeFile(*connection, path, "after\n")};
    QVERIFY2(written.has_value(), qPrintable(written ? QString{} : written.error().message));
    QCOMPARE(contentsOf(path), QByteArray{"after\n"});
    // A new file there is refused.
    const auto refused{writeFile(*connection, sub + QStringLiteral("/b.txt"), "x")};
    QVERIFY(!refused.has_value());
    QCOMPARE(refused.error().code, kode::remote::error::permission);
    QVERIFY(::chmod(sub.toUtf8().constData(), 0755) == 0);
}

void TestRemote::listsAFolder()
{
    QTemporaryDir folder;
    const auto connection{serving()};
    QVERIFY(writeFile(*connection, folder.filePath(QStringLiteral("sub/x.txt")), "x").has_value());
    QVERIFY(writeFile(*connection, folder.filePath(QStringLiteral("file.txt")), "12345").has_value());
    QVERIFY(QFile::link(folder.filePath(QStringLiteral("sub")), folder.filePath(QStringLiteral("to-sub"))));
    std::optional<std::expected<QList<files::Entry>, kode::remote::Error>> answer;
    files::list(*connection, folder.path(), [&answer](std::expected<QList<files::Entry>, kode::remote::Error> result) {
        answer = std::move(result);
    });
    const auto listed{waitFor(answer)};
    QVERIFY(listed.has_value() && listed->has_value());
    QList<files::Entry> entries{**listed};
    std::ranges::sort(entries, {}, &files::Entry::name);
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries[0].name, QStringLiteral("file.txt"));
    QCOMPARE(entries[0].version.size, qint64{5});
    QVERIFY(!entries[0].isFolder());
    QCOMPARE(entries[1].name, QStringLiteral("sub"));
    QVERIFY(entries[1].isFolder());
    QCOMPARE(entries[2].type, QStringLiteral("link"));
    QVERIFY(entries[2].isFolder());
}

void TestRemote::reportsAMissingFile()
{
    QTemporaryDir folder;
    const auto connection{serving()};
    const auto read{readFile(*connection, folder.filePath(QStringLiteral("nothing.txt")))};
    QVERIFY(!read.has_value());
    QCOMPARE(read.error().code, kode::remote::error::notFound);
    std::optional<std::expected<QList<files::Entry>, kode::remote::Error>> answer;
    files::stat(*connection, {folder.filePath(QStringLiteral("nothing.txt"))}, [&answer](auto result) {
        answer = std::move(result);
    });
    const auto stat{waitFor(answer)};
    QVERIFY(stat.has_value() && stat->has_value());
    QCOMPARE((*stat)->first().type, QStringLiteral("missing"));
}

namespace
{

// A host named for one test, its ssh a stand-in running here.
struct RemoteHost {
    QTemporaryDir folder;
    QString destination;

    // `loginPath`: what the host's login shell tells as its PATH (a stand-in $SHELL).
    explicit RemoteHost(const QString &loginPath = {})
    {
        static qint32 count{0};
        destination = QStringLiteral("box%1").arg(++count);
        kode::remote::Setup setup{setupIn(folder, writeScript(folder, QStringLiteral("ssh"), fakeSsh))};
        if (!loginPath.isEmpty()) {
            const QString shell{u"#!/bin/sh\nprintf '__KODE_PATH__%s' '"_s + loginPath + u"'\n"_s};
            setup.environment.insert(QStringLiteral("SHELL"), writeScript(folder, QStringLiteral("shell"), shell));
        }
        static_cast<void>(Hosts::shared().host(destination, std::move(setup)));
    }
    RemoteHost(const RemoteHost &) = delete;
    RemoteHost &operator=(const RemoteHost &) = delete;
    RemoteHost(RemoteHost &&) = delete;
    RemoteHost &operator=(RemoteHost &&) = delete;
    ~RemoteHost()
    {
        Hosts::shared().host(destination)->disconnectFromHost();
        stopDaemons(folder);
    }

    [[nodiscard]] QString path(const QString &name) const { return folder.filePath(QStringLiteral("files/") + name); }
    [[nodiscard]] QUrl url(const QString &name) const { return kode::remote::ssh::fileUrl(destination, path(name)); }
    void write(const QString &name, const QByteArray &bytes) const
    {
        QDir{}.mkpath(QFileInfo{path(name)}.path());
        QFile file{path(name)};
        if (file.open(QIODevice::WriteOnly)) {
            file.write(bytes);
        }
    }
};

void typeAtStart(kode::Document &document, const QString &text)
{
    document.textDocument()->replace(0, 0, text, false, {});
}

} // namespace

void TestRemote::opensAndSavesARemoteDocument()
{
    const RemoteHost host;
    host.write(QStringLiteral("hello.txt"), "hello\n");
    kode::Document document;
    QVERIFY(document.load(host.url(QStringLiteral("hello.txt"))));
    QVERIFY(document.loading());
    QTRY_VERIFY_WITH_TIMEOUT(!document.loading(), 10000);
    QCOMPARE(document.textDocument()->text(), QStringLiteral("hello\n"));
    QCOMPARE(document.fileName(), QStringLiteral("hello.txt"));
    QCOMPARE(document.textDocument()->syntax(), QString{});
    typeAtStart(document, QStringLiteral("well, "));
    QVERIFY(document.modified());
    QSignalSpy saved{&document, &kode::Document::saved};
    QVERIFY(document.save());
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 10000);
    QVERIFY(!document.modified());
    QCOMPARE(contentsOf(host.path(QStringLiteral("hello.txt"))), QByteArray{"well, hello\n"});
}

void TestRemote::keepsARemoteDocumentsEncoding()
{
    const RemoteHost host;
    host.write(QStringLiteral("latin.txt"), "caf\xe9\r\n");
    kode::Document document;
    QVERIFY(document.load(host.url(QStringLiteral("latin.txt"))));
    QTRY_VERIFY_WITH_TIMEOUT(!document.loading(), 10000);
    QCOMPARE(document.textDocument()->text(), QStringLiteral("caf\u00e9\n"));
    QVERIFY(document.crlf());
    typeAtStart(document, QStringLiteral("un "));
    QSignalSpy saved{&document, &kode::Document::saved};
    QVERIFY(document.save());
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 10000);
    QCOMPARE(contentsOf(host.path(QStringLiteral("latin.txt"))), QByteArray{"un caf\xe9\r\n"});
}

void TestRemote::refusesAConflictingSave()
{
    const RemoteHost host;
    host.write(QStringLiteral("a.txt"), "first\n");
    kode::Document document;
    QVERIFY(document.load(host.url(QStringLiteral("a.txt"))));
    QTRY_VERIFY_WITH_TIMEOUT(!document.loading(), 10000);
    host.write(QStringLiteral("a.txt"), "theirs, longer\n");
    typeAtStart(document, QStringLiteral("mine "));
    QSignalSpy changed{&document, &kode::Document::changedOnDisk};
    QSignalSpy failed{&document, &kode::Document::saveFailed};
    QSignalSpy saved{&document, &kode::Document::saved};
    QVERIFY(document.save());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
    QVERIFY(changed.count() >= 1);
    QVERIFY(document.modified());
    QCOMPARE(contentsOf(host.path(QStringLiteral("a.txt"))), QByteArray{"theirs, longer\n"});
    document.keepMine();
    QVERIFY(document.save());
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 10000);
    QCOMPARE(contentsOf(host.path(QStringLiteral("a.txt"))), QByteArray{"mine first\n"});
}

void TestRemote::keepsEditsMadeDuringASave()
{
    const RemoteHost host;
    host.write(QStringLiteral("a.txt"), "text\n");
    kode::Document document;
    QVERIFY(document.load(host.url(QStringLiteral("a.txt"))));
    QTRY_VERIFY_WITH_TIMEOUT(!document.loading(), 10000);
    typeAtStart(document, QStringLiteral("1"));
    QSignalSpy saved{&document, &kode::Document::saved};
    QVERIFY(document.save());
    typeAtStart(document, QStringLiteral("2"));
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 10000);
    QCOMPARE(contentsOf(host.path(QStringLiteral("a.txt"))), QByteArray{"1text\n"});
    QVERIFY(document.modified());
    document.textDocument()->undo();
    QVERIFY(!document.modified());
}

void TestRemote::reloadsARemoteChange()
{
    const RemoteHost host;
    host.write(QStringLiteral("a.txt"), "before\n");
    kode::Document document;
    QVERIFY(document.load(host.url(QStringLiteral("a.txt"))));
    QTRY_VERIFY_WITH_TIMEOUT(!document.loading(), 10000);
    host.write(QStringLiteral("a.txt"), "after, from elsewhere\n");
    QTRY_COMPARE_WITH_TIMEOUT(document.textDocument()->text(), QStringLiteral("after, from elsewhere\n"), 15000);
    QVERIFY(!document.modified());
}

void TestRemote::startsAMissingRemoteFile()
{
    const RemoteHost host;
    kode::Document document;
    QSignalSpy failed{&document, &kode::Document::loadFailed};
    QVERIFY(document.load(host.url(QStringLiteral("new/folder/notes.md"))));
    QTRY_VERIFY_WITH_TIMEOUT(!document.loading(), 10000);
    QCOMPARE(failed.count(), 0);
    QVERIFY(document.textDocument()->text().isEmpty());
    QCOMPARE(document.textDocument()->syntax(), QStringLiteral("Markdown"));
    typeAtStart(document, QStringLiteral("# Notes\n"));
    QSignalSpy saved{&document, &kode::Document::saved};
    QVERIFY(document.save());
    QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 10000);
    QCOMPARE(contentsOf(host.path(QStringLiteral("new/folder/notes.md"))), QByteArray{"# Notes\n"});
}

namespace
{

Connection::Result askWith(Connection &connection, const QString &method, const QJsonObject &params)
{
    std::optional<Connection::Result> answer;
    connection.request(method, params, [&answer](const Connection::Result &result) { answer = result; });
    return waitFor(answer).value_or(
        std::unexpected{kode::remote::Error{.message = QStringLiteral("no answer"), .data = {}, .code = 0}}
    );
}

void touchFile(const QString &path, const QByteArray &bytes)
{
    QDir{}.mkpath(QFileInfo{path}.path());
    QFile file{path};
    if (file.open(QIODevice::WriteOnly)) {
        file.write(bytes);
    }
}

// The paths told changed since `from`.
QStringList toldPaths(const QSignalSpy &told, qsizetype from = 0)
{
    QStringList paths;
    for (qsizetype i{from}; i < told.count(); ++i) {
        if (told.at(i).at(0).toString() != QLatin1String("watch/changed")) {
            continue;
        }
        const QJsonArray list(told.at(i).at(1).value<QJsonValue>().toObject().value(QStringLiteral("paths")).toArray());
        std::ranges::for_each(list, [&paths](const QJsonValue &path) { paths.append(path.toString()); });
    }
    return paths;
}

} // namespace

void TestRemote::tellsWatchedChanges()
{
    QTemporaryDir temporary;
    // The real path: the agent reports what it watches.
    const QString folder{QFileInfo{temporary.path()}.canonicalFilePath()};
    const QString file{folder + QStringLiteral("/watched.txt")};
    touchFile(file, "one\n");
    const auto connection{serving()};
    QSignalSpy told{connection.get(), &Connection::notified};
    QVERIFY(askWith(*connection, QStringLiteral("watch/set"), {{QStringLiteral("paths"), QJsonArray{folder, file}}})
                .has_value());
    QVERIFY(!connection->agent()
                 .value(QStringLiteral("capabilities"))
                 .toObject()
                 .value(QStringLiteral("watch"))
                 .toArray()
                 .isEmpty());

    touchFile(folder + QStringLiteral("/new.txt"), "x");
    QTRY_VERIFY_WITH_TIMEOUT(toldPaths(told).contains(folder), 5000);

    qsizetype mark{told.count()};
    QFile inPlace{file};
    QVERIFY(inPlace.open(QIODevice::Append));
    inPlace.write("two\n");
    inPlace.close();
    QTRY_VERIFY_WITH_TIMEOUT(toldPaths(told, mark).contains(file), 5000);

    // Replaced by renaming (as editors save): still watched.
    mark = told.count();
    touchFile(folder + QStringLiteral("/.watched.tmp"), "three\n");
    QVERIFY(::rename((folder + QStringLiteral("/.watched.tmp")).toUtf8().constData(), file.toUtf8().constData()) == 0);
    QTRY_VERIFY_WITH_TIMEOUT(toldPaths(told, mark).contains(file), 5000);
    mark = told.count();
    QFile again{file};
    QVERIFY(again.open(QIODevice::Append));
    again.write("four\n");
    again.close();
    QTRY_VERIFY_WITH_TIMEOUT(toldPaths(told, mark).contains(file), 5000);

    // No longer watched: not told.
    QVERIFY(askWith(*connection, QStringLiteral("watch/set"), {{QStringLiteral("paths"), QJsonArray{}}}).has_value());
    mark = told.count();
    touchFile(folder + QStringLiteral("/later.txt"), "x");
    QTest::qWait(300);
    QVERIFY(toldPaths(told, mark).isEmpty());
}

void TestRemote::listsFilesToFind()
{
    QTemporaryDir folder;
    for (const QString &name :
         {QStringLiteral("src/main.cpp"),
          QStringLiteral("src/deep/a.h"),
          QStringLiteral("README.md"),
          QStringLiteral(".git/config"),
          QStringLiteral("build/out.o"),
          QStringLiteral("cmake-build-debug/x"),
          QStringLiteral("node_modules/m/index.js"),
          QStringLiteral(".hidden")}) {
        touchFile(folder.filePath(name), "x");
    }
    QVERIFY(QFile::link(folder.filePath(QStringLiteral("src")), folder.filePath(QStringLiteral("to-src"))));
    QVERIFY(QFile::link(folder.filePath(QStringLiteral("README.md")), folder.filePath(QStringLiteral("readme-link"))));
    const auto connection{serving()};
    std::optional<QByteArray> listed;
    Connection *const link{connection.get()};
    // The channel's bytes follow the answer at once: taken in its handler.
    connection->request(
        QStringLiteral("search/files"),
        {{QStringLiteral("path"), folder.path()}},
        [link, &listed](const Connection::Result &result) {
            QVERIFY(result.has_value());
            link->receive(
                static_cast<quint32>(result->toObject().value(QStringLiteral("channel")).toInteger()),
                [&listed, bytes = QByteArray{}](QByteArrayView piece, kode::remote::ChannelEnd end) mutable {
                    if (end == kode::remote::ChannelEnd::More) {
                        bytes.append(piece);
                    } else {
                        listed = bytes;
                    }
                }
            );
        }
    );
    QTRY_VERIFY(listed.has_value());
    QStringList files{QString::fromUtf8(*listed).split(u'\n', Qt::SkipEmptyParts)};
    files.sort();
    QCOMPARE(
        files,
        (QStringList{
            QStringLiteral("README.md"),
            QStringLiteral("readme-link"),
            QStringLiteral("src/deep/a.h"),
            QStringLiteral("src/main.cpp")
        })
    );
}

void TestRemote::makesTheSidebarsOperations()
{
    QTemporaryDir folder;
    const QString home{folder.filePath(QStringLiteral("home"))};
    QDir{}.mkpath(home);
    auto connection{std::make_unique<Connection>()};
    QProcessEnvironment environment{QProcessEnvironment::systemEnvironment()};
    environment.insert(QStringLiteral("HOME"), home);
    connection->setEnvironment(environment);
    connection->start(agentPath, {QStringLiteral("serve")});
    const QString file{folder.filePath(QStringLiteral("a.txt"))};
    QVERIFY(askWith(*connection, QStringLiteral("fs/create"), {{QStringLiteral("path"), file}}).has_value());
    QVERIFY(QFileInfo::exists(file));
    const auto taken{askWith(*connection, QStringLiteral("fs/create"), {{QStringLiteral("path"), file}})};
    QVERIFY(!taken.has_value());
    QCOMPARE(taken.error().code, kode::remote::error::exists);
    const QString sub{folder.filePath(QStringLiteral("sub"))};
    QVERIFY(askWith(
                *connection,
                QStringLiteral("fs/create"),
                {{QStringLiteral("path"), sub}, {QStringLiteral("folder"), true}}
    ).has_value());
    touchFile(sub + QStringLiteral("/inner/deep.txt"), "deep");
    QVERIFY(askWith(
                *connection,
                QStringLiteral("fs/copy"),
                {{QStringLiteral("from"), sub}, {QStringLiteral("to"), sub + QStringLiteral(" copy")}}
    ).has_value());
    QCOMPARE(contentsOf(sub + QStringLiteral(" copy/inner/deep.txt")), QByteArray{"deep"});
    const auto renamedOver{
        askWith(*connection, QStringLiteral("fs/rename"), {{QStringLiteral("from"), file}, {QStringLiteral("to"), sub}})
    };
    QVERIFY(!renamedOver.has_value());
    QCOMPARE(renamedOver.error().code, kode::remote::error::exists);
    QVERIFY(askWith(
                *connection,
                QStringLiteral("fs/rename"),
                {{QStringLiteral("from"), file}, {QStringLiteral("to"), sub + QStringLiteral("/b.txt")}}
    ).has_value());
    QVERIFY(QFileInfo::exists(sub + QStringLiteral("/b.txt")));
    QVERIFY(askWith(*connection, QStringLiteral("fs/remove"), {{QStringLiteral("path"), sub + QStringLiteral(" copy")}})
                .has_value());
    QVERIFY(!QFileInfo::exists(sub + QStringLiteral(" copy")));
    // The Trash is on another disk than a temporary folder on some Macs: then it says so.
    const auto trashed{askWith(*connection, QStringLiteral("fs/trash"), {{QStringLiteral("path"), sub}})};
    if (trashed) {
        QVERIFY(QFileInfo::exists(home + QStringLiteral("/.Trash/sub/b.txt")));
        QVERIFY(!QFileInfo::exists(sub));
    } else {
        QVERIFY2(trashed.error().message.contains(QStringLiteral("another disk")), qPrintable(trashed.error().message));
    }
}

void TestRemote::showsARemoteFolder()
{
    const RemoteHost host;
    host.write(QStringLiteral("project/main.cpp"), "int main() {}\n");
    host.write(QStringLiteral("project/src/a.h"), "#pragma once\n");
    const QString root{host.path(QStringLiteral("project"))};
    kode::FileTree tree;
    tree.setFolder(host.url(QStringLiteral("project")));
    QVERIFY(tree.remote());
    QTRY_COMPARE_WITH_TIMEOUT(tree.rowCount(tree.rootIndex()), 2, 10000);
    // Folders first.
    QCOMPARE(tree.index(0, 0, tree.rootIndex()).data().toString(), QStringLiteral("src"));
    QVERIFY(tree.index(0, 0, tree.rootIndex()).data(kode::FileTree::IsFolderRole).toBool());
    QCOMPARE(tree.index(1, 0, tree.rootIndex()).data().toString(), QStringLiteral("main.cpp"));
    QCOMPARE(
        tree.index(1, 0, tree.rootIndex()).data(kode::FileTree::FileUrlRole).toUrl(),
        host.url(QStringLiteral("project/main.cpp"))
    );

    const QUrl made{tree.createFile(host.url(QStringLiteral("project")))};
    QCOMPARE(made.fileName(), QStringLiteral("untitled"));
    QCOMPARE(tree.rowCount(tree.rootIndex()), 3);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(root + QStringLiteral("/untitled")), 10000);
    QSignalSpy moved{&tree, &kode::FileTree::moved};
    const QUrl renamed{tree.rename(made, QStringLiteral("notes.md"))};
    QCOMPARE(renamed.fileName(), QStringLiteral("notes.md"));
    QTRY_COMPARE_WITH_TIMEOUT(moved.count(), 1, 10000);
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/notes.md")));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/untitled")));

    // Made elsewhere: the watched folder shows it.
    host.write(QStringLiteral("project/later.txt"), "x");
    QTRY_COMPARE_WITH_TIMEOUT(tree.rowCount(tree.rootIndex()), 4, 10000);

    // A folder's entries, as it is expanded.
    const QModelIndex src{tree.index(0, 0, tree.rootIndex())};
    QVERIFY(tree.canFetchMore(src));
    tree.fetchMore(src);
    QTRY_COMPARE_WITH_TIMEOUT(tree.rowCount(src), 1, 10000);
    QVERIFY(tree.indexOf(host.url(QStringLiteral("project/src/a.h"))).isValid());
}

void TestRemote::findsRemoteFiles()
{
    const RemoteHost host;
    host.write(QStringLiteral("project/src/EditorView.cpp"), "x");
    host.write(QStringLiteral("project/src/other.txt"), "x");
    host.write(QStringLiteral("project/build/EditorView.o"), "x");
    kode::FileSearch search;
    search.setFolder(host.url(QStringLiteral("project")));
    search.setQuery(QStringLiteral("ev"));
    search.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(search.count(), 1, 10000);
    QCOMPARE(search.fileAt(0), host.url(QStringLiteral("project/src/EditorView.cpp")));
}

namespace
{

// The agent's channel named by `key` in `result`, taken whole.
void collect(
    Connection &connection,
    const Connection::Result &result,
    const QString &key,
    std::optional<QByteArray> &out
)
{
    connection.receive(
        static_cast<quint32>(result->toObject().value(key).toInteger()),
        [&out, bytes = QByteArray{}](QByteArrayView piece, kode::remote::ChannelEnd end) mutable {
            if (end == kode::remote::ChannelEnd::More) {
                bytes.append(piece);
            } else {
                out = bytes;
            }
        }
    );
}

// `argv` spawned; its output collected in `out`, its stdin channel returned.
quint32
spawned(Connection &connection, const QStringList &argv, std::optional<QByteArray> &out, std::optional<qint64> &id)
{
    const quint32 input{connection.openChannel()};
    Connection *const link{&connection};
    connection.request(
        QStringLiteral("process/spawn"),
        {{QStringLiteral("argv"), QJsonArray::fromStringList(argv)},
         {QStringLiteral("cwd"), QDir::tempPath()},
         {QStringLiteral("stdin"), static_cast<qint64>(input)}},
        [link, &out, &id](const Connection::Result &result) {
            if (!result) {
                qWarning() << result.error().message;
                return;
            }
            id = result->toObject().value(QStringLiteral("id")).toInteger();
            collect(*link, result, QStringLiteral("stdout"), out);
        }
    );
    return input;
}

bool exitedWith(const QSignalSpy &told, qint64 id, qint64 code)
{
    return std::ranges::any_of(told, [id, code](const QList<QVariant> &signal) {
        const QJsonObject params{signal.at(1).value<QJsonValue>().toObject()};
        return signal.at(0).toString() == QLatin1String("process/exited") &&
               params.value(QStringLiteral("id")).toInteger() == id &&
               params.value(QStringLiteral("code")).toInteger() == code;
    });
}

} // namespace

void TestRemote::runsPrograms()
{
    const auto connection{serving()};
    const Connection::Result which{askWith(
        *connection,
        QStringLiteral("process/which"),
        {{QStringLiteral("names"), QJsonArray{QStringLiteral("sh"), QStringLiteral("no-such-program-here")}}}
    )};
    QVERIFY(which.has_value());
    const QJsonObject found{which->toObject().value(QStringLiteral("found")).toObject()};
    QVERIFY(found.value(QStringLiteral("sh")).toString().endsWith(QStringLiteral("/sh")));
    QVERIFY(!found.contains(QStringLiteral("no-such-program-here")));

    QSignalSpy told{connection.get(), &Connection::notified};
    std::optional<QByteArray> echoed;
    std::optional<qint64> catId;
    const quint32 input{spawned(*connection, {QStringLiteral("cat")}, echoed, catId)};
    connection->write(input, "hello ");
    connection->write(input, "there\n");
    connection->end(input);
    QTRY_VERIFY_WITH_TIMEOUT(echoed.has_value(), 10000);
    QCOMPARE(*echoed, QByteArray{"hello there\n"});
    QTRY_VERIFY_WITH_TIMEOUT(exitedWith(told, *catId, 0), 10000);

    std::optional<QByteArray> large;
    std::optional<qint64> largeId;
    connection->end(spawned(
        *connection,
        {QStringLiteral("/bin/sh"), QStringLiteral("-c"), QStringLiteral("yes 0123456789 | head -c 3000000")},
        large,
        largeId
    ));
    QTRY_VERIFY_WITH_TIMEOUT(large.has_value(), 20000);
    QCOMPARE(large->size(), 3000000);

    std::optional<QByteArray> slept;
    std::optional<qint64> sleepId;
    static_cast<void>(spawned(*connection, {QStringLiteral("sleep"), QStringLiteral("30")}, slept, sleepId));
    QTRY_VERIFY(sleepId.has_value());
    QVERIFY(askWith(
                *connection,
                QStringLiteral("process/signal"),
                {{QStringLiteral("id"), *sleepId}, {QStringLiteral("signal"), QStringLiteral("TERM")}}
    ).has_value());
    QTRY_VERIFY_WITH_TIMEOUT(slept.has_value(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(exitedWith(told, *sleepId, 128 + SIGTERM), 10000);
}

void TestRemote::runsARemoteProcess()
{
    const RemoteHost host;
    kode::remote::RemoteProcess
        process{*Hosts::shared().host(host.destination), QStringLiteral("cat"), {}, QDir::tempPath()};
    QByteArray output;
    connect(&process, &kode::remote::RemoteProcess::output, &process, [&output](const QByteArray &bytes) {
        output.append(bytes);
    });
    QSignalSpy finished{&process, &kode::remote::RemoteProcess::finished};
    // Before it started: it waits.
    process.write("echo me\n");
    QTRY_COMPARE_WITH_TIMEOUT(output, QByteArray{"echo me\n"}, 10000);
    process.closeInput();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
}

void TestRemote::remoteClangdReportsAnError()
{
    const QString clangd{QStandardPaths::findExecutable(
        QStringLiteral("clangd"),
        {QStringLiteral("/opt/homebrew/opt/llvm/bin"),
         QStringLiteral("/opt/homebrew/bin"),
         QStringLiteral("/usr/local/bin")}
    )};
    if (clangd.isEmpty()) {
        QSKIP("clangd is not installed");
    }
    const RemoteHost host{QFileInfo{clangd}.path() + QStringLiteral(":/usr/bin:/bin")};
    host.write(QStringLiteral("project/main.cpp"), "int main() { return undefinedName; }\n");
    QDir{}.mkpath(host.path(QStringLiteral("project/.git")));
    kode::Document document;
    QVERIFY(document.load(host.url(QStringLiteral("project/main.cpp"))));
    QTRY_VERIFY_WITH_TIMEOUT(!document.loading(), 10000);
    kode::lsp::LanguageServers servers;
    servers.add(&document);
    QTRY_VERIFY_WITH_TIMEOUT(document.textDocument()->errorCount() == 1, 60000);
    const kode::TextDocument::Diagnostic &error{document.textDocument()->diagnostics().first()};
    QCOMPARE(
        document.textDocument()->text().mid(error.start, error.end - error.start),
        QStringLiteral("undefinedName")
    );
    document.textDocument()->replace(error.start, error.end - error.start, QStringLiteral("0"), false, {});
    QTRY_VERIFY_WITH_TIMEOUT(document.textDocument()->errorCount() == 0, 30000);
    servers.remove(&document);
}

QTEST_MAIN(TestRemote)
