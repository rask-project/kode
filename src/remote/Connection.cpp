#include "Connection.h"

#include "../../remote/protocol/Methods.h"

#include <QJsonDocument>
#include <QStringView>

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
    // Each handler may make new requests: they fail at once.
    const QHash<qint64, Handler> pending{std::exchange(m_pending, {})};
    const Error gone{
        .message = message.isEmpty() ? tr("The connection was closed.") : message,
        .code = error::disconnected
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
        handler(std::unexpected{Error{.message = tr("Not connected."), .code = error::disconnected}});
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
    const QByteArray json{QJsonDocument{message}.toJson(QJsonDocument::Compact)};
    std::string frame;
    appendFrame(
        frame,
        FrameType::Message,
        controlChannel,
        std::string_view{json.constData(), static_cast<std::size_t>(json.size())}
    );
    m_process.write(frame.data(), static_cast<qint64>(frame.size()));
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
        return;
    case FrameType::Data:
    case FrameType::Eof:
    case FrameType::Credit: {
        // No channel is open yet.
        std::string reset;
        appendFrame(reset, FrameType::Reset, frame.channel, {});
        m_process.write(reset.data(), static_cast<qint64>(reset.size()));
        return;
    }
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
            .code = object.value(QStringLiteral("code")).toInteger(),
        }});
        return;
    }
    handler(message.value(QStringLiteral("result")));
}

} // namespace kode::remote
