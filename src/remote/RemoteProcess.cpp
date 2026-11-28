#include "RemoteProcess.h"

#include <QJsonArray>
#include <QJsonObject>

namespace kode::remote
{

namespace
{

constexpr quint64 InputClosed{1U << 0U};
constexpr quint64 Done{1U << 1U};

} // namespace

RemoteProcess::RemoteProcess(Host &host, const QString &program, const QStringList &arguments, const QString &folder)
    : m_destination{host.destination()}
{
    const QStringList argv{QStringList{program} + arguments};
    host.whenOnline(this, [this, argv, folder](auto connection) {
        if (!connection) {
            stop();
            return;
        }
        Connection &link{connection->get()};
        m_connection = &link;
        m_stdin = link.openChannel();
        connect(&link, &Connection::notified, this, [this](const QString &method, const QJsonValue &params) {
            if (method == QLatin1String("process/exited") &&
                params.toObject().value(QStringLiteral("id")).toInteger() == m_id) {
                stop();
            }
        });
        connect(&link, &Connection::stateChanged, this, [this, &link] {
            if (link.state() == Connection::State::Closed) {
                stop();
            }
        });
        link.request(
            QStringLiteral("process/spawn"),
            {
                {QStringLiteral("argv"), QJsonArray::fromStringList(argv)},
                {QStringLiteral("cwd"), folder},
                {QStringLiteral("stdin"), static_cast<qint64>(m_stdin)},
            },
            [self = QPointer<RemoteProcess>{this}, &link](const Connection::Result &result) {
                if (self != nullptr) {
                    self->started(link, result);
                }
            }
        );
    });
}

RemoteProcess::~RemoteProcess()
{
    if (m_connection != nullptr && m_id != 0 && (m_flags & Done) == 0U) {
        m_connection->end(static_cast<quint32>(m_stdin));
        m_connection->request(
            QStringLiteral("process/signal"),
            {{QStringLiteral("id"), m_id}, {QStringLiteral("signal"), QStringLiteral("TERM")}},
            [](const Connection::Result &) {}
        );
    }
}

void RemoteProcess::started(Connection &connection, const Connection::Result &result)
{
    if (!result) {
        stop();
        return;
    }
    const QJsonObject info{result->toObject()};
    m_id = info.value(QStringLiteral("id")).toInteger();
    const auto stdoutChannel{static_cast<quint32>(info.value(QStringLiteral("stdout")).toInteger())};
    // Its output follows the answer at once: taken now.
    connection.receive(stdoutChannel, [self = QPointer<RemoteProcess>{this}](QByteArrayView piece, ChannelEnd end) {
        if (self == nullptr) {
            return;
        }
        if (end == ChannelEnd::More) {
            emit self->output(piece.toByteArray());
        }
    });
    if (!m_queued.isEmpty()) {
        connection.write(static_cast<quint32>(m_stdin), m_queued);
        m_queued.clear();
    }
    if ((m_flags & InputClosed) != 0U) {
        connection.end(static_cast<quint32>(m_stdin));
    }
}

void RemoteProcess::write(QByteArrayView bytes)
{
    if ((m_flags & (InputClosed | Done)) != 0U) {
        return;
    }
    if (m_connection == nullptr || m_id == 0) {
        m_queued.append(bytes);
        return;
    }
    m_connection->write(static_cast<quint32>(m_stdin), bytes);
}

void RemoteProcess::closeInput()
{
    m_flags |= InputClosed;
    if (m_connection != nullptr && m_id != 0) {
        m_connection->end(static_cast<quint32>(m_stdin));
    }
}

void RemoteProcess::stop()
{
    if ((m_flags & Done) != 0U) {
        return;
    }
    m_flags |= Done;
    emit finished();
}

} // namespace kode::remote
