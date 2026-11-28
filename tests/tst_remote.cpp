#include "tst_remote.h"

#include "../remote/protocol/Frame.h"
#include "../remote/protocol/Methods.h"
#include "remote/Connection.h"

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

namespace
{

const QString agentPath{QStringLiteral(KODE_REMOTE_AGENT)};

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

QTEST_GUILESS_MAIN(TestRemote)
