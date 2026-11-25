#include "Document.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace tekst
{

namespace
{

const QString crlfText{QStringLiteral("\r\n")};

} // namespace

Document::Document(QObject *parent) : QObject(parent) {}

void Document::setEditor(EditorView *editor)
{
    if (m_editor == editor) {
        return;
    }
    if (m_editor != nullptr) {
        disconnect(m_editor, nullptr, this, nullptr);
    }
    m_editor = editor;
    if (m_editor != nullptr) {
        connect(m_editor, &EditorView::modifiedChanged, this, &Document::modifiedChanged);
    }
    emit editorChanged();
    emit modifiedChanged();
}

QString Document::fileName() const
{
    return m_fileUrl.isEmpty() ? tr("Untitled") : m_fileUrl.fileName();
}

bool Document::modified() const
{
    return (m_editor != nullptr && m_editor->modified()) || (m_flags & FormatChanged) != 0U;
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
    return read(fileUrl, {});
}

bool Document::reopen(const QString &encoding)
{
    return !m_fileUrl.isEmpty() && read(m_fileUrl, encoding);
}

bool Document::read(const QUrl &fileUrl, const QString &encoding)
{
    if (m_editor == nullptr) {
        return false;
    }
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
    m_editor->setText(lines);
    m_editor->detectSyntax(fileUrl.fileName());
    m_editor->detectIndentation();
    setFileUrl(fileUrl);
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
    if (m_editor == nullptr) {
        return false;
    }
    if (const std::expected<void, QString> written{write(fileUrl)}; !written) {
        emit errorOccurred(written.error());
        return false;
    }
    m_editor->markSaved();
    setFormatChanged(false);
    if (fileUrl != m_fileUrl) {
        m_editor->detectSyntax(fileUrl.fileName());
    }
    setFileUrl(fileUrl);
    return true;
}

void Document::clear()
{
    if (m_editor == nullptr) {
        return;
    }
    m_editor->setText({});
    m_editor->setSyntax({});
    m_format = {};
    m_flags = 0U;
    emit formatChanged();
    emit modifiedChanged();
    setFileUrl({});
}

std::expected<void, QString> Document::write(const QUrl &fileUrl) const
{
    const QString path{fileUrl.toLocalFile()};
    QString text{m_editor->text()};
    if (crlf()) {
        text.replace(QLatin1Char('\n'), crlfText);
    }
    const std::optional<QByteArray> bytes{codec::encode(text, m_format)};
    if (!bytes) {
        return std::unexpected{tr("“%1” could not be saved as %2: some characters cannot be represented in it.")
                                   .arg(path, m_format.encoding)};
    }

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

} // namespace tekst
