#include "LanguageServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QVariantList>

#include <algorithm>
#include <optional>
#include <utility>

namespace kode::lsp
{

namespace
{

// How long quitting waits for a server to exit before killing it.
constexpr qint32 exitWaitMs{300};

QJsonObject clientCapabilities()
{
    return {
        {QStringLiteral("general"),
         QJsonObject{{QStringLiteral("positionEncodings"), QJsonArray{QStringLiteral("utf-16")}}}},
        {QStringLiteral("window"), QJsonObject{{QStringLiteral("workDoneProgress"), true}}},
        {QStringLiteral("textDocument"),
         QJsonObject{
             {QStringLiteral("synchronization"), QJsonObject{{QStringLiteral("dynamicRegistration"), false}}},
             {QStringLiteral("publishDiagnostics"), QJsonObject{{QStringLiteral("versionSupport"), true}}},
         }},
    };
}

} // namespace

LanguageServer::LanguageServer(
    const QString &program,
    const QStringList &arguments,
    const QJsonObject &options,
    const QString &path,
    const QUrl &root
)
    : m_root{root}
{
    QProcessEnvironment environment{QProcessEnvironment::systemEnvironment()};
    environment.insert(QStringLiteral("PATH"), path);
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(root.toLocalFile());
    m_process.setStandardErrorFile(QProcess::nullDevice());
    m_process.setProgram(program);
    m_process.setArguments(arguments);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &LanguageServer::readOutput);
    connect(&m_process, &QProcess::finished, this, &LanguageServer::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit finished();
        }
    });
    m_process.start();
    initialize(options);
}

LanguageServer::~LanguageServer()
{
    if (m_process.state() == QProcess::NotRunning) {
        return;
    }
    disconnect(&m_process, nullptr, this, nullptr);
    write(
        {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
         {QStringLiteral("id"), m_nextId++},
         {QStringLiteral("method"), QStringLiteral("shutdown")}}
    );
    write({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("method"), QStringLiteral("exit")}});
    m_process.closeWriteChannel();
    if (!m_process.waitForFinished(exitWaitMs)) {
        m_process.kill();
        m_process.waitForFinished(exitWaitMs);
    }
}

QString LanguageServer::activity() const
{
    if ((m_flags & Initialized) == 0U) {
        return tr("Starting");
    }
    if (m_progress.isEmpty()) {
        return {};
    }
    const Progress &latest{m_progress.constLast()};
    QString text{latest.title};
    if (!text.isEmpty()) {
        text[0] = text[0].toUpper();
    }
    if (!latest.message.isEmpty()) {
        text += u' ' + latest.message;
    }
    if (latest.percentage >= 0) {
        text += QStringLiteral(" (%1%)").arg(latest.percentage);
    }
    return text;
}

void LanguageServer::initialize(const QJsonObject &options)
{
    QJsonObject params{
        {QStringLiteral("processId"), QCoreApplication::applicationPid()},
        {QStringLiteral("clientInfo"),
         QJsonObject{
             {QStringLiteral("name"), QCoreApplication::applicationName()},
             {QStringLiteral("version"), QCoreApplication::applicationVersion()},
         }},
        {QStringLiteral("rootUri"), m_root.toString()},
        {QStringLiteral("workspaceFolders"),
         QJsonArray{QJsonObject{
             {QStringLiteral("uri"), m_root.toString()},
             {QStringLiteral("name"), QDir{m_root.toLocalFile()}.dirName()},
         }}},
        {QStringLiteral("capabilities"), clientCapabilities()},
    };
    if (!options.isEmpty()) {
        params.insert(QStringLiteral("initializationOptions"), options);
    }
    const QJsonObject message{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), m_nextId},
        {QStringLiteral("method"), QStringLiteral("initialize")},
        {QStringLiteral("params"), params},
    };
    m_pending.insert(m_nextId++, [this](const QJsonValue &result) {
        const QJsonValue sync{result[QStringLiteral("capabilities")][QStringLiteral("textDocumentSync")]};
        m_sync = sync.isObject() ? sync[QStringLiteral("change")].toInteger(FullSync) : sync.toInteger(FullSync);
        m_flags |= Initialized;
        write(
            {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
             {QStringLiteral("method"), QStringLiteral("initialized")},
             {QStringLiteral("params"), QJsonObject{}}}
        );
        for (const QJsonObject &queued : std::as_const(m_queued)) {
            write(queued);
        }
        m_queued.clear();
        emit activityChanged();
    });
    write(message);
}

void LanguageServer::notify(const QString &method, const QJsonValue &params)
{
    send(
        {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
         {QStringLiteral("method"), method},
         {QStringLiteral("params"), params}}
    );
}

void LanguageServer::request(const QString &method, const QJsonValue &params, Handler handler)
{
    m_pending.insert(m_nextId, std::move(handler));
    send(
        {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
         {QStringLiteral("id"), m_nextId++},
         {QStringLiteral("method"), method},
         {QStringLiteral("params"), params}}
    );
}

void LanguageServer::send(const QJsonObject &message)
{
    if ((m_flags & Initialized) == 0U) {
        m_queued.append(message);
        return;
    }
    write(message);
}

void LanguageServer::write(const QJsonObject &message)
{
    m_process.write(frame(message));
}

void LanguageServer::readOutput()
{
    m_reader.append(m_process.readAllStandardOutput());
    while (true) {
        const auto next{m_reader.next()};
        // A malformed message is skipped; the stream goes on after it.
        if (!next.has_value()) {
            continue;
        }
        const std::optional<QJsonObject> &message{*next};
        if (!message.has_value()) {
            return;
        }
        handle(*message);
    }
}

void LanguageServer::handle(const QJsonObject &message)
{
    const QJsonValue id{message[QStringLiteral("id")]};
    const QString method{message[QStringLiteral("method")].toString()};
    if (!method.isEmpty() && !id.isUndefined()) {
        answer(message);
        return;
    }
    if (method == QLatin1String("textDocument/publishDiagnostics")) {
        const QJsonObject params{message[QStringLiteral("params")].toObject()};
        emit diagnosticsPublished(
            QUrl{params[QStringLiteral("uri")].toString()},
            params[QStringLiteral("version")].toInteger(-1),
            params[QStringLiteral("diagnostics")].toArray()
        );
        return;
    }
    if (method == QLatin1String("$/progress")) {
        progress(message[QStringLiteral("params")].toObject());
        return;
    }
    if (method == QLatin1String("textDocument/clangd.fileStatus")) {
        const QJsonObject params{message[QStringLiteral("params")].toObject()};
        emit fileStatusChanged(
            QUrl{params[QStringLiteral("uri")].toString()},
            params[QStringLiteral("state")].toString()
        );
        return;
    }
    if (!method.isEmpty()) {
        return;
    }
    const qint64 key{id.toInteger(-1)};
    if (!m_pending.contains(key)) {
        return;
    }
    const Handler handler{m_pending.take(key)};
    handler(message[QStringLiteral("result")]);
}

void LanguageServer::progress(const QJsonObject &params)
{
    // A token is a string or a number.
    const QString token{params[QStringLiteral("token")].toVariant().toString()};
    const QJsonValue value{params[QStringLiteral("value")]};
    const QString kind{value[QStringLiteral("kind")].toString()};
    const auto found{std::ranges::find(m_progress, token, &Progress::token)};
    if (kind == QLatin1String("end")) {
        if (found != m_progress.end()) {
            m_progress.erase(found);
            emit activityChanged();
        }
        return;
    }
    if (kind == QLatin1String("begin") && found == m_progress.end()) {
        m_progress.append(Progress{.token = token, .title = value[QStringLiteral("title")].toString()});
    }
    const auto entry{std::ranges::find(m_progress, token, &Progress::token)};
    if (entry == m_progress.end()) {
        return;
    }
    if (value[QStringLiteral("message")].isString()) {
        entry->message = value[QStringLiteral("message")].toString();
    }
    if (value[QStringLiteral("percentage")].isDouble()) {
        entry->percentage = value[QStringLiteral("percentage")].toInteger();
    }
    emit activityChanged();
}

void LanguageServer::answer(const QJsonObject &request)
{
    // workspace/configuration asks for one value per item: none for each.
    QJsonValue result{QJsonValue::Null};
    if (request[QStringLiteral("method")].toString() == QLatin1String("workspace/configuration")) {
        const QJsonArray items(request[QStringLiteral("params")][QStringLiteral("items")].toArray());
        result = QJsonArray::fromVariantList(QVariantList(items.size()));
    }
    write(
        {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
         {QStringLiteral("id"), request[QStringLiteral("id")]},
         {QStringLiteral("result"), result}}
    );
}

} // namespace kode::lsp
