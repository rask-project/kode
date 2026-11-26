#include "Document.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace kode
{

namespace
{

const QString crlfText{QStringLiteral("\r\n")};

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
    const QString path{fileUrl.toLocalFile()};
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("“%1” could not be opened: %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray bytes{file.readAll()};
    codec::Format format{codec::detect(bytes)};
    if (!encoding.isEmpty() && encoding != format.encoding) {
        format = {.encoding = encoding, .byteOrderMark = {}};
    }
    const std::optional<QString> text{codec::decode(bytes.sliced(format.byteOrderMark.size()), format.encoding)};
    if (!text) {
        emit errorOccurred(tr("“%1” is not valid %2 text.").arg(path, format.encoding));
        return false;
    }
    const bool fileCrlf{text->contains(crlfText)};
    QString lines{*text};
    lines.replace(crlfText, QStringLiteral("\n"));
    m_format = format;
    m_flags = fileCrlf ? Crlf : 0U;
    // Also clears the undo history and the modified state.
    m_text.setText(lines, keepPlace);
    m_text.detectSyntax(fileUrl.fileName());
    m_text.detectIndentation();
    setFileUrl(fileUrl);
    watch();
    emit formatChanged();
    emit modifiedChanged();
    return true;
}

bool Document::save()
{
    if (m_fileUrl.isEmpty()) {
        emit errorOccurred(tr("The document has no file yet."));
        return false;
    }
    return saveAs(m_fileUrl);
}

bool Document::saveAs(const QUrl &fileUrl)
{
    if (const std::expected<void, QString> written{write(fileUrl)}; !written) {
        emit errorOccurred(written.error());
        return false;
    }
    m_text.markSaved();
    setFormatChanged(false);
    if (fileUrl != m_fileUrl) {
        m_text.detectSyntax(fileUrl.fileName());
    }
    setFileUrl(fileUrl);
    watch();
    return true;
}

void Document::clear()
{
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
    m_text.setText({});
    m_text.detectSyntax(fileUrl.fileName());
    m_format = {};
    m_flags = 0U;
    emit formatChanged();
    emit modifiedChanged();
    setFileUrl(fileUrl);
    watch();
}

std::expected<void, QString> Document::write(const QUrl &fileUrl) const
{
    const QString path{fileUrl.toLocalFile()};
    QString text{m_text.text()};
    if (crlf()) {
        text.replace(QLatin1Char('\n'), crlfText);
    }
    const std::optional<QByteArray> bytes{codec::encode(text, m_format)};
    if (!bytes) {
        return std::unexpected{tr("“%1” could not be saved as %2: some characters cannot be represented in it.")
                                   .arg(path, m_format.encoding)};
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
