#include "Document.h"

#include "../remote/protocol/Methods.h"
#include "remote/Host.h"
#include "remote/Hosts.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QPointer>
#include <QPromise>
#include <QSaveFile>
#include <QThreadPool>

namespace kode
{

namespace
{

const QString crlfText{QStringLiteral("\r\n")};
// Files from this size on are read off the GUI thread (about 7 ms of work on the GUI thread).
constexpr qint64 backgroundSize{qint64{2} * 1024 * 1024};
// How often a remote file's host is asked whether it changed.
constexpr qint32 pollMilliseconds{5000};
// As git tells a binary file: a NUL in its first bytes.
constexpr qsizetype binaryProbe{8000};

bool isRemote(const QUrl &url)
{
    return url.scheme() == QLatin1String("ssh");
}

} // namespace

Document::Document(QObject *parent) : QObject(parent)
{
    // Writers replace files in steps (rename, write): look once they settle.
    constexpr qint32 settleMilliseconds{100};
    m_checkTimer.setSingleShot(true);
    m_checkTimer.setInterval(settleMilliseconds);
    connect(&m_checkTimer, &QTimer::timeout, this, &Document::checkFile);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_checkTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_checkTimer, qOverload<>(&QTimer::start));
    connect(&m_text, &TextDocument::modifiedChanged, this, &Document::modifiedChanged);
    m_pollTimer.setInterval(pollMilliseconds);
    connect(&m_pollTimer, &QTimer::timeout, this, &Document::checkRemote);
}

QString Document::fileName() const
{
    if (!m_fileUrl.isEmpty()) {
        return m_fileUrl.fileName();
    }
    return m_untitledNumber > 1 ? tr("Untitled %1").arg(m_untitledNumber) : tr("Untitled");
}

void Document::setUntitledNumber(qint32 number)
{
    if (m_untitledNumber == number) {
        return;
    }
    m_untitledNumber = number;
    emit fileUrlChanged();
}

void Document::followMove(const QUrl &fileUrl)
{
    setFileUrl(fileUrl);
    m_text.detectSyntax(fileUrl.fileName());
    watch();
}

bool Document::modified() const
{
    return m_text.modified() || (m_flags & FormatChanged) != 0U;
}

void Document::setFormatChanged(bool changed)
{
    const bool wasModified{modified()};
    m_flags = changed ? (m_flags | FormatChanged) : (m_flags & ~FormatChanged);
    if (wasModified != modified()) {
        emit modifiedChanged();
    }
}

void Document::setEncoding(const QString &encoding)
{
    if (m_format.encoding == encoding || !codec::encodings().contains(encoding)) {
        return;
    }
    // Keeps a byte order mark if the new encoding has one.
    m_format = {.encoding = encoding, .byteOrderMark = byteOrderMark() ? codec::byteOrderMark(encoding) : QByteArray{}};
    emit formatChanged();
    setFormatChanged(true);
}

void Document::setCrlf(bool crlf)
{
    if (this->crlf() == crlf) {
        return;
    }
    m_flags = crlf ? (m_flags | Crlf) : (m_flags & ~Crlf);
    emit formatChanged();
    setFormatChanged(true);
}

bool Document::load(const QUrl &fileUrl)
{
    return read(fileUrl, {}, false);
}

bool Document::reopen(const QString &encoding)
{
    return !m_fileUrl.isEmpty() && read(m_fileUrl, encoding, false);
}

bool Document::reload()
{
    return !m_fileUrl.isEmpty() && read(m_fileUrl, m_format.encoding, true);
}

void Document::watch()
{
    if (isRemote(m_fileUrl)) {
        if (const QStringList watched{m_watcher.files() + m_watcher.directories()}; !watched.isEmpty()) {
            m_watcher.removePaths(watched);
        }
        m_fileSize = -1;
        m_pollTimer.start();
        return;
    }
    m_pollTimer.stop();
    const QString path{m_fileUrl.toLocalFile()};
    const QFileInfo info{path};
    // The folder too: a file replaced by another one comes back there.
    const QStringList directories{path.isEmpty() ? QStringList{} : QStringList{info.absolutePath()}};
    if (m_watcher.directories() != directories) {
        if (!m_watcher.directories().isEmpty()) {
            m_watcher.removePaths(m_watcher.directories());
        }
        if (!directories.isEmpty()) {
            m_watcher.addPaths(directories);
        }
    }
    if (!m_watcher.files().isEmpty() && m_watcher.files() != QStringList{path}) {
        m_watcher.removePaths(m_watcher.files());
    }
    if (path.isEmpty()) {
        m_fileSize = -1;
        return;
    }
    m_fileModified = info.lastModified();
    m_fileSize = info.size();
    // A file not written yet (startFile()): its folder says when it appears.
    if (m_watcher.files().isEmpty() && info.exists()) {
        m_watcher.addPath(path);
    }
}

void Document::checkFile()
{
    const QString path{m_fileUrl.toLocalFile()};
    const QFileInfo info{path};
    // Gone (deleted or moved away): keep the text; watch it again if it returns.
    if (path.isEmpty() || !info.exists()) {
        return;
    }
    // A replaced file (saved by renaming a new one over it) is no longer watched.
    if (m_watcher.files().isEmpty()) {
        m_watcher.addPath(path);
    }
    if (info.lastModified() == m_fileModified && info.size() == m_fileSize) {
        return;
    }
    m_fileModified = info.lastModified();
    m_fileSize = info.size();
    if (modified()) {
        emit changedOnDisk();
        return;
    }
    reload();
}

bool Document::read(const QUrl &fileUrl, const QString &encoding, bool keepPlace)
{
    if (isRemote(fileUrl)) {
        readRemote(fileUrl, encoding, keepPlace);
        return true;
    }
    const QString path{fileUrl.toLocalFile()};
    const quint64 generation{++m_generation};
    if (QFileInfo{path}.size() < backgroundSize) {
        setLoading(false);
        return finishRead(fileUrl, readFile(path, encoding), keepPlace);
    }
    setLoading(true);
    m_flags = keepPlace ? (m_flags | KeepPlace) : (m_flags & ~KeepPlace);
    setFileUrl(fileUrl);
    QPromise<ReadResult> promise;
    QFuture<ReadResult> future{promise.future()};
    QThreadPool::globalInstance()->start([promise = std::move(promise), path, encoding]() mutable {
        promise.start();
        promise.addResult(readFile(path, encoding));
        promise.finish();
    });
    finishLater(std::move(future), fileUrl, generation);
    return true;
}

void Document::finishLater(QFuture<ReadResult> future, const QUrl &fileUrl, quint64 generation)
{
    // Not run if this document is gone by then.
    future.then(this, [this, generation, fileUrl](ReadResult result) {
        if (generation != m_generation) {
            return;
        }
        setLoading(false);
        const bool readBefore{(m_flags & Read) != 0U};
        if (!finishRead(fileUrl, std::move(result), (m_flags & KeepPlace) != 0U) && !readBefore) {
            emit loadFailed();
        }
    });
}

void Document::readRemote(const QUrl &fileUrl, const QString &encoding, bool keepPlace)
{
    const quint64 generation{++m_generation};
    setLoading(true);
    m_flags = keepPlace ? (m_flags | KeepPlace) : (m_flags & ~KeepPlace);
    setFileUrl(fileUrl);
    remote::Host *const host{remote::Hosts::shared().forUrl(fileUrl)};
    if (host == nullptr) {
        remoteReadFailed(
            fileUrl,
            remote::Error{.message = tr("This is not a host Kode can reach."), .data = {}, .code = 0}
        );
        return;
    }
    host->whenOnline(this, [this, generation, fileUrl, encoding](auto connection) {
        if (generation != m_generation) {
            return;
        }
        if (!connection) {
            remoteReadFailed(fileUrl, remote::Error{.message = connection.error(), .data = {}, .code = 0});
            return;
        }
        const QPointer<Document> self{this};
        remote::files::read(connection->get(), fileUrl.path(), [self, generation, fileUrl, encoding](auto read) {
            if (self == nullptr || generation != self->m_generation) {
                return;
            }
            if (!read) {
                self->remoteReadFailed(fileUrl, read.error());
                return;
            }
            self->m_remote = read->version;
            self->m_remoteSeen = read->version;
            QPromise<ReadResult> promise;
            QFuture<ReadResult> future{promise.future()};
            QThreadPool::globalInstance()->start([promise = std::move(promise),
                                                  bytes = std::move(read->bytes),
                                                  encoding,
                                                  name = fileUrl.path()]() mutable {
                promise.start();
                QBuffer buffer{&bytes};
                buffer.open(QIODevice::ReadOnly);
                if (bytes.first(std::min(bytes.size(), binaryProbe)).contains('\0')) {
                    promise.addResult(ReadResult{std::unexpected{tr("“%1” is not a text file.").arg(name)}});
                } else {
                    promise.addResult(decode(buffer, encoding, name));
                }
                promise.finish();
            });
            self->finishLater(std::move(future), fileUrl, generation);
        });
    });
}

void Document::remoteReadFailed(const QUrl &fileUrl, const remote::Error &error)
{
    setLoading(false);
    // A file the `kode` command named that does not exist yet: saving writes it.
    if (error.code == remote::error::notFound && (m_flags & Read) == 0U) {
        startFile(fileUrl);
        return;
    }
    emit errorOccurred(tr("“%1” could not be opened: %2").arg(fileUrl.path(), error.message));
    if ((m_flags & Read) == 0U) {
        emit loadFailed();
    }
}

Document::ReadResult Document::readFile(const QString &path, const QString &encoding)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected{tr("“%1” could not be opened: %2").arg(path, file.errorString())};
    }
    return decode(file, encoding, path);
}

Document::ReadResult Document::decode(QIODevice &device, const QString &encoding, const QString &name)
{
    std::expected<codec::Decoded, QString> decoded{codec::readFile(device, encoding)};
    if (!decoded) {
        return std::unexpected{tr("“%1” is not valid %2 text.").arg(name, decoded.error())};
    }
    const bool crlf{decoded->text.contains(crlfText)};
    if (crlf) {
        decoded->text.replace(crlfText, QStringLiteral("\n"));
    }
    return Loaded{
        .buffer = TextBuffer{std::move(decoded->text)},
        .format = std::move(decoded->format),
        .flags = crlf ? Crlf : 0U
    };
}

bool Document::finishRead(const QUrl &fileUrl, ReadResult result, bool keepPlace)
{
    if (!result) {
        emit errorOccurred(result.error());
        return false;
    }
    m_format = std::move(result->format);
    m_flags = (m_flags & Loading) | Read | result->flags;
    // Also clears the undo history and the modified state.
    m_text.setBuffer(std::move(result->buffer), keepPlace);
    m_text.detectSyntax(fileUrl.fileName());
    m_text.detectIndentation();
    setFileUrl(fileUrl);
    watch();
    emit formatChanged();
    emit modifiedChanged();
    return true;
}

void Document::setLoading(bool loading)
{
    if (this->loading() == loading) {
        return;
    }
    m_flags = loading ? (m_flags | Loading) : (m_flags & ~Loading);
    emit loadingChanged();
}

bool Document::save()
{
    if (m_fileUrl.isEmpty()) {
        emit errorOccurred(tr("The document has no file yet."));
        emit saveFailed();
        return false;
    }
    return saveAs(m_fileUrl);
}

bool Document::saveAs(const QUrl &fileUrl)
{
    // Its text is not there yet: saving would empty the file.
    if (loading()) {
        emit errorOccurred(tr("“%1” is still being opened.").arg(fileName()));
        emit saveFailed();
        return false;
    }
    if (isRemote(fileUrl)) {
        return saveRemote(fileUrl);
    }
    if (const std::expected<void, QString> written{write(fileUrl)}; !written) {
        emit errorOccurred(written.error());
        emit saveFailed();
        return false;
    }
    m_text.markSaved();
    setFormatChanged(false);
    if (fileUrl != m_fileUrl) {
        m_text.detectSyntax(fileUrl.fileName());
    }
    setFileUrl(fileUrl);
    watch();
    emit saved();
    return true;
}

bool Document::saveRemote(const QUrl &fileUrl)
{
    const auto fail{[this](const QString &message) {
        emit errorOccurred(message);
        emit saveFailed();
        return false;
    }};
    const std::expected<QByteArray, QString> bytes{encoded(fileUrl.path())};
    if (!bytes) {
        return fail(bytes.error());
    }
    remote::Host *const host{remote::Hosts::shared().forUrl(fileUrl)};
    remote::Connection *const connection{host != nullptr ? host->connection() : nullptr};
    if (connection == nullptr) {
        return fail(tr("“%1” could not be saved: the host is not connected. Your changes are kept here.")
                        .arg(fileUrl.fileName()));
    }
    // The version it replaces, when it is the file read here.
    const std::optional<remote::files::Version> expect{
        fileUrl == m_fileUrl && m_remote.valid() ? std::optional{m_remote} : std::nullopt
    };
    m_text.beginSave();
    const QPointer<Document> self{this};
    remote::files::write(*connection, fileUrl.path(), *bytes, expect, true, [self, fileUrl](auto result) {
        if (self != nullptr) {
            self->remoteSaved(fileUrl, result);
        }
    });
    return true;
}

void Document::remoteSaved(const QUrl &fileUrl, const std::expected<remote::files::Version, remote::Error> &result)
{
    if (!result) {
        if (result.error().code == remote::error::conflict) {
            m_remoteSeen = remote::files::versionOf(result.error().data.toObject());
            emit changedOnDisk();
        } else {
            emit errorOccurred(tr("“%1” could not be saved: %2").arg(fileUrl.fileName(), result.error().message));
        }
        emit saveFailed();
        return;
    }
    m_remote = *result;
    m_remoteSeen = *result;
    m_text.finishSave();
    setFormatChanged(false);
    if (fileUrl != m_fileUrl) {
        m_text.detectSyntax(fileUrl.fileName());
    }
    setFileUrl(fileUrl);
    watch();
    emit saved();
}

void Document::keepMine()
{
    if (isRemote(m_fileUrl) && m_remoteSeen.valid()) {
        m_remote = m_remoteSeen;
    }
}

void Document::checkRemote()
{
    remote::Host *const host{remote::Hosts::shared().forUrl(m_fileUrl)};
    remote::Connection *const connection{host != nullptr ? host->connection() : nullptr};
    if (connection == nullptr || loading()) {
        return;
    }
    const QPointer<Document> self{this};
    const QUrl url{m_fileUrl};
    remote::files::stat(*connection, {url.path()}, [self, url](auto entries) {
        if (self == nullptr || self->m_fileUrl != url || !entries || entries->isEmpty()) {
            return;
        }
        const remote::files::Version now{entries->first().version};
        // Gone (deleted or moved away): keep the text.
        if (!now.valid() || now.sameAs(self->m_remoteSeen)) {
            return;
        }
        self->m_remoteSeen = now;
        if (now.sameAs(self->m_remote)) {
            return;
        }
        if (self->modified()) {
            emit self->changedOnDisk();
            return;
        }
        self->reload();
    });
}

void Document::clear()
{
    m_remote = {};
    m_remoteSeen = {};
    m_text.setText({});
    m_text.setSyntax({});
    m_format = {};
    m_flags = 0U;
    emit formatChanged();
    emit modifiedChanged();
    setFileUrl({});
    watch();
}

void Document::startFile(const QUrl &fileUrl)
{
    m_remote = {};
    m_remoteSeen = {};
    m_text.setText({});
    m_text.detectSyntax(fileUrl.fileName());
    m_format = {};
    m_flags = 0U;
    emit formatChanged();
    emit modifiedChanged();
    setFileUrl(fileUrl);
    watch();
}

std::expected<QByteArray, QString> Document::encoded(const QString &name) const
{
    QString text{m_text.text()};
    if (crlf()) {
        text.replace(QLatin1Char('\n'), crlfText);
    }
    std::optional<QByteArray> bytes{codec::encode(text, m_format)};
    if (!bytes) {
        return std::unexpected{tr("“%1” could not be saved as %2: some characters cannot be represented in it.")
                                   .arg(name, m_format.encoding)};
    }
    return std::move(*bytes);
}

std::expected<void, QString> Document::write(const QUrl &fileUrl) const
{
    const QString path{fileUrl.toLocalFile()};
    const std::expected<QByteArray, QString> bytes{encoded(path)};
    if (!bytes) {
        return std::unexpected{bytes.error()};
    }

    // A file the `kode` command named may be in folders that do not exist yet.
    QDir{}.mkpath(QFileInfo{path}.absolutePath());
    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected{tr("“%1” could not be saved: %2").arg(path, file.errorString())};
    }
    if (file.write(*bytes) != bytes->size() || !file.commit()) {
        return std::unexpected{tr("“%1” could not be saved: %2").arg(path, file.errorString())};
    }
    return {};
}

void Document::setFileUrl(const QUrl &fileUrl)
{
    if (m_fileUrl == fileUrl) {
        return;
    }
    m_fileUrl = fileUrl;
    emit fileUrlChanged();
}

} // namespace kode
