#include "Connection.h"

#include "../../remote/agent/Channel.h"
#include "../../remote/protocol/Methods.h"

#include <QJsonDocument>
#include <QStringView>
#include <QtEndian>

namespace kode::remote
{

namespace
{

// Enough of stderr for ssh's message about why it failed.
constexpr qsizetype stderrLimit{4096};

QString text(std::string_view name)
{
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

} // namespace

Connection::Connection(QObject *parent) : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Connection::readFrames);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr.append(m_process.readAllStandardError());
        if (m_stderr.size() > stderrLimit) {
            m_stderr.remove(0, m_stderr.size() - stderrLimit);
        }
    });
    connect(&m_process, &QProcess::finished, this, [this] {
        const QString message{QString::fromUtf8(m_stderr).trimmed()};
        finish(message.isEmpty() ? tr("The connection to the host closed.") : message);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish(tr("“%1” could not be started: %2").arg(m_process.program(), m_process.errorString()));
        }
    });
}

Connection::~Connection()
{
    // Not as an error: nothing is told.
    m_pending.clear();
    m_process.disconnect(this);
    m_process.kill();
    m_process.waitForFinished();
}

void Connection::start(const QString &program, const QStringList &arguments)
{
    if (state() != State::Idle) {
        return;
    }
    setState(State::Connecting);
    m_process.start(program, arguments);
    const QJsonObject params{
        {QStringLiteral("protocol"), protocolVersion},
        {QStringLiteral("client"), QStringLiteral(KODE_VERSION)},
    };
    request(text(method::initialize), params, [this](const Result &result) { initialized(result); });
}

void Connection::initialized(const Result &result)
{
    if (!result) {
        finish(result.error().message);
        return;
    }
    m_agent = result->toObject();
    setState(State::Online);
}

void Connection::close()
{
    finish({});
}

void Connection::finish(const QString &message)
{
    if (state() == State::Closed) {
        return;
    }
    m_error = message;
    setState(State::Closed);
    m_process.kill();
    m_outgoing.clear();
    const QHash<quint32, Incoming> incoming{std::exchange(m_incoming, {})};
    std::ranges::for_each(incoming, [](const Incoming &in) { in.receiver({}, ChannelEnd::Failed); });
    // Each handler may make new requests: they fail at once.
    const QHash<qint64, Handler> pending{std::exchange(m_pending, {})};
    const Error gone{
        .message = message.isEmpty() ? tr("The connection was closed.") : message,
        .data = {},
        .code = error::disconnected,
    };
    std::ranges::for_each(pending, [&gone](const Handler &handler) { handler(std::unexpected{gone}); });
}

void Connection::setState(State state)
{
    if (this->state() == state) {
        return;
    }
    m_state = static_cast<quint64>(state);
    emit stateChanged();
}

void Connection::request(const QString &method, const QJsonObject &params, Handler handler)
{
    if (state() == State::Closed || state() == State::Idle) {
        handler(std::unexpected{Error{.message = tr("Not connected."), .data = {}, .code = error::disconnected}});
        return;
    }
    const qint64 id{m_nextId++};
    m_pending.insert(id, std::move(handler));
    send({
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    });
}

void Connection::notify(const QString &method, const QJsonObject &params)
{
    if (state() == State::Closed || state() == State::Idle) {
        return;
    }
    send({
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), method},
        {QStringLiteral("params"), params},
    });
}

void Connection::send(const QJsonObject &message)
{
    writeFrame(FrameType::Message, controlChannel, QJsonDocument{message}.toJson(QJsonDocument::Compact));
}

void Connection::writeFrame(FrameType type, quint32 channel, QByteArrayView payload)
{
    std::string frame;
    appendFrame(frame, type, channel, std::string_view{payload.data(), static_cast<std::size_t>(payload.size())});
    m_process.write(frame.data(), static_cast<qint64>(frame.size()));
}

void Connection::receive(quint32 channel, Receiver receiver)
{
    if (state() == State::Closed) {
        receiver({}, ChannelEnd::Failed);
        return;
    }
    m_incoming.insert(channel, Incoming{.receiver = std::move(receiver), .window = static_cast<qint64>(initialCredit)});
}

void Connection::send(quint32 channel, QByteArray bytes)
{
    if (state() == State::Closed) {
        return;
    }
    m_outgoing.insert(
        channel,
        Outgoing{.bytes = std::move(bytes), .sent = 0, .credit = static_cast<qint64>(initialCredit)}
    );
    pump(channel);
}

void Connection::pump(quint32 channel)
{
    const auto out{m_outgoing.find(channel)};
    if (out == m_outgoing.end()) {
        return;
    }
    while (out->credit > 0 && out->sent < out->bytes.size()) {
        const qint64 size{std::min({out->credit, static_cast<qint64>(maxDataSize), out->bytes.size() - out->sent})};
        writeFrame(FrameType::Data, channel, QByteArrayView{out->bytes}.sliced(out->sent, size));
        out->sent += size;
        out->credit -= size;
    }
    if (out->sent == out->bytes.size()) {
        writeFrame(FrameType::Eof, channel, {});
        m_outgoing.erase(out);
    }
}

bool Connection::handleChannel(const Frame &frame)
{
    const quint32 channel{frame.channel};
    switch (frame.type()) {
    case FrameType::Data: {
        const auto in{m_incoming.find(channel)};
        if (in == m_incoming.end()) {
            writeFrame(FrameType::Reset, channel, {});
            return true;
        }
        const auto size{static_cast<qint64>(frame.payload.size())};
        if (size > in->window) {
            return false;
        }
        // Out while it runs: it may close the connection.
        Incoming taken{std::move(*in)};
        m_incoming.erase(in);
        taken.receiver(QByteArrayView{frame.payload.data(), size}, ChannelEnd::More);
        if (state() == State::Closed) {
            return true;
        }
        m_incoming.insert(channel, std::move(taken));
        const quint32 credit{qToBigEndian(static_cast<quint32>(size))};
        writeFrame(FrameType::Credit, channel, QByteArrayView{reinterpret_cast<const char *>(&credit), sizeof(credit)});
        return true;
    }
    case FrameType::Eof:
    case FrameType::Reset: {
        m_outgoing.remove(channel);
        const auto in{m_incoming.find(channel)};
        if (in != m_incoming.end()) {
            const Receiver receiver{std::move(in->receiver)};
            m_incoming.erase(in);
            receiver({}, frame.type() == FrameType::Eof ? ChannelEnd::Complete : ChannelEnd::Failed);
        }
        return true;
    }
    case FrameType::Credit: {
        const auto out{m_outgoing.find(channel)};
        if (out != m_outgoing.end()) {
            out->credit += qFromBigEndian<quint32>(frame.payload.data());
            pump(channel);
        }
        return true;
    }
    case FrameType::Message:
        return false;
    }
    return false;
}

void Connection::readFrames()
{
    const QByteArray bytes{m_process.readAllStandardOutput()};
    m_reader.append(std::string_view{bytes.constData(), static_cast<std::size_t>(bytes.size())});
    for (std::optional<Frame> frame{m_reader.next()}; frame && state() != State::Closed; frame = m_reader.next()) {
        handle(*frame);
    }
    if (m_reader.failed()) {
        finish(tr("The host's agent broke the protocol."));
    }
}

void Connection::handle(const Frame &frame)
{
    switch (frame.type()) {
    case FrameType::Message:
        if (frame.channel != controlChannel) {
            finish(tr("The host's agent broke the protocol."));
            return;
        }
        handleMessage(QByteArrayView{frame.payload.data(), static_cast<qsizetype>(frame.payload.size())});
        return;
    case FrameType::Reset:
    case FrameType::Data:
    case FrameType::Eof:
    case FrameType::Credit:
        if (!handleChannel(frame)) {
            finish(tr("The host's agent broke the protocol."));
        }
        return;
    }
}

void Connection::handleMessage(QByteArrayView payload)
{
    QJsonParseError parseError{};
    const QJsonDocument document{QJsonDocument::fromJson(payload.toByteArray(), &parseError)};
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        finish(tr("The host's agent broke the protocol."));
        return;
    }
    const QJsonObject message{document.object()};
    const QJsonValue id{message.value(QStringLiteral("id"))};
    const QJsonValue method{message.value(QStringLiteral("method"))};
    if (method.isString()) {
        // Kode answers no requests from the agent yet.
        if (!id.isUndefined()) {
            send({
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), id},
                {QStringLiteral("error"),
                 QJsonObject{
                     {QStringLiteral("code"), error::methodNotFound},
                     {QStringLiteral("message"), QStringLiteral("No such method.")},
                 }},
            });
            return;
        }
        emit notified(method.toString(), message.value(QStringLiteral("params")));
        return;
    }
    const Handler handler{m_pending.take(id.toInteger(-1))};
    if (!handler) {
        return;
    }
    if (const QJsonValue failure{message.value(QStringLiteral("error"))}; failure.isObject()) {
        const QJsonObject object{failure.toObject()};
        handler(std::unexpected{Error{
            .message = object.value(QStringLiteral("message")).toString(),
            .data = object.value(QStringLiteral("data")),
            .code = object.value(QStringLiteral("code")).toInteger(),
        }});
        return;
    }
    handler(message.value(QStringLiteral("result")));
}

} // namespace kode::remote
