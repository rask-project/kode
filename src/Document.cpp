#include "Document.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTextDocument>

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

void Document::setTextDocument(QQuickTextDocument *textDocument)
{
    if (m_textDocument == textDocument) {
        return;
    }
    if (const QTextDocument *const previous{document()}; previous != nullptr) {
        disconnect(previous, nullptr, this, nullptr);
    }
    m_textDocument = textDocument;
    if (const QTextDocument *const current{document()}; current != nullptr) {
        connect(current, &QTextDocument::modificationChanged, this, &Document::modifiedChanged);
    }
    emit textDocumentChanged();
    emit modifiedChanged();
}

QString Document::fileName() const
{
    return m_fileUrl.isEmpty() ? tr("Untitled") : m_fileUrl.fileName();
}

bool Document::modified() const
{
    const QTextDocument *const current{document()};
    return current != nullptr && current->isModified();
}

bool Document::load(const QUrl &fileUrl)
{
    QTextDocument *const current{document()};
    if (current == nullptr) {
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
    // Also resets the undo history and the modified flag.
    current->setPlainText(lines);
    current->setModified(false);
    setFileUrl(fileUrl);
    emit loaded();
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
    QTextDocument *const current{document()};
    if (current == nullptr) {
        return false;
    }
    if (const std::expected<void, QString> written{write(fileUrl)}; !written) {
        emit errorOccurred(written.error());
        return false;
    }
    current->setModified(false);
    setFileUrl(fileUrl);
    return true;
}

void Document::clear()
{
    QTextDocument *const current{document()};
    if (current == nullptr) {
        return;
    }
    current->setPlainText({});
    current->setModified(false);
    m_lineEnding = QStringLiteral("\n");
    setFileUrl({});
    emit loaded();
}

QTextDocument *Document::document() const
{
    return m_textDocument != nullptr ? m_textDocument->textDocument() : nullptr;
}

std::expected<void, QString> Document::write(const QUrl &fileUrl) const
{
    const QString path{fileUrl.toLocalFile()};
    // toRawText keeps non-breaking spaces (toPlainText turns them into spaces)
    // and separates paragraphs with U+2029.
    QString text{document()->toRawText()};
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
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
