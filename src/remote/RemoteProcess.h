#pragma once

#include "Connection.h"
#include "Host.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace kode::remote
{

// A program running on a host (the agent's process/spawn): what is written
// goes to its stdin, its stdout arrives in output(); finished() when it
// exits or the connection goes. Writes before it started wait. Stopped
// (SIGTERM) when this goes.
class RemoteProcess : public QObject
{
    Q_OBJECT
public:
    // `program` (a path on the host, or a name on its PATH) in `folder` there.
    RemoteProcess(Host &host, const QString &program, const QStringList &arguments, const QString &folder);
    ~RemoteProcess() override;
    Q_DISABLE_COPY_MOVE(RemoteProcess)

    [[nodiscard]] QString destination() const { return m_destination; }
    void write(QByteArrayView bytes);
    // Its stdin ends.
    void closeInput();

signals:
    void output(const QByteArray &bytes);
    void finished();

private:
    void started(Connection &connection, const Connection::Result &result);
    void stop();

    QPointer<Connection> m_connection;
    QString m_destination;
    // Written before it started.
    QByteArray m_queued;
    qint64 m_id{0};
    quint64 m_stdin{0U};
    // Its stdin ended, or it is done (a whole word: no padding).
    quint64 m_flags{0U};
};

} // namespace kode::remote
