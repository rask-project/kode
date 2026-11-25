#include "Document.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>

namespace tekst
{

namespace
{

const QString crlf{QStringLiteral("\r\n")};

// The file's text, decoded as UTF-8.
std::expected<QString, QString> readText(const QString &path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected{Document::tr("“%1” could not be opened: %2").arg(path, file.errorString())};
    }
    const QByteArray bytes{file.readAll()};
    QStringDecoder decoder{QStringDecoder::Utf8};
    QString text{decoder.decode(bytes)};
    if (decoder.hasError()) {
        return std::unexpected{Document::tr("“%1” is not UTF-8 text.").arg(path)};
    }
    return text;
}

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
    return m_editor != nullptr && m_editor->modified();
}

bool Document::load(const QUrl &fileUrl)
{
    if (m_editor == nullptr) {
        return false;
    }
    const std::expected<QString, QString> text{readText(fileUrl.toLocalFile())};
    if (!text) {
        emit errorOccurred(text.error());
        return false;
    }
    m_lineEnding = text->contains(crlf) ? crlf : QStringLiteral("\n");
    QString lines{*text};
    lines.replace(crlf, QStringLiteral("\n"));
    // Also clears the undo history and the modified state.
    m_editor->setText(lines);
    setFileUrl(fileUrl);
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
    setFileUrl(fileUrl);
    return true;
}

void Document::clear()
{
    if (m_editor == nullptr) {
        return;
    }
    m_editor->setText({});
    m_lineEnding = QStringLiteral("\n");
    setFileUrl({});
}

std::expected<void, QString> Document::write(const QUrl &fileUrl) const
{
    const QString path{fileUrl.toLocalFile()};
    QString text{m_editor->text()};
    if (m_lineEnding == crlf) {
        text.replace(QLatin1Char('\n'), crlf);
    }

    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected{tr("“%1” could not be saved: %2").arg(path, file.errorString())};
    }
    const QByteArray bytes{text.toUtf8()};
    if (file.write(bytes) != bytes.size() || !file.commit()) {
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
