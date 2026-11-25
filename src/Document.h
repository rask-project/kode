#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <expected>

class QTextDocument;

namespace tekst
{

// The file behind an editor's text: loads it into the TextArea's document and
// saves it back. UTF-8; the file's line ending (LF or CRLF) is kept, and
// saving goes through QSaveFile, so a failed write leaves the file as it was.
class Document : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // The TextArea's textDocument.
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    // Empty for a new, never saved document.
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileUrlChanged)
    // The file's name, or "Untitled".
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileUrlChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
public:
    explicit Document(QObject *parent = nullptr);

    [[nodiscard]] QQuickTextDocument *textDocument() const { return m_textDocument; }
    void setTextDocument(QQuickTextDocument *textDocument);
    [[nodiscard]] QUrl fileUrl() const { return m_fileUrl; }
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] bool modified() const;

    // Each returns false and emits errorOccurred() when it fails.
    Q_INVOKABLE bool load(const QUrl &fileUrl);
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl &fileUrl);
    // An empty, untitled document.
    Q_INVOKABLE void clear();

signals:
    void textDocumentChanged();
    void fileUrlChanged();
    void modifiedChanged();
    // A file was loaded, or the document was cleared: new text from the start.
    void loaded();
    void errorOccurred(const QString &message);

private:
    [[nodiscard]] QTextDocument *document() const;
    [[nodiscard]] std::expected<void, QString> write(const QUrl &fileUrl) const;
    void setFileUrl(const QUrl &fileUrl);

    QPointer<QQuickTextDocument> m_textDocument;
    QUrl m_fileUrl;
    // What a line ends with in the file: "\n" or "\r\n".
    QString m_lineEnding{QStringLiteral("\n")};
};

} // namespace tekst
