#pragma once

#include "EditorView.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <expected>

namespace tekst
{

// The file behind an editor's text: loads it into an EditorView and saves it
// back. UTF-8; the file's line ending (LF or CRLF) is kept, and saving goes
// through QSaveFile, so a failed write leaves the file as it was.
class Document : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(tekst::EditorView *editor READ editor WRITE setEditor NOTIFY editorChanged)
    // Empty for a new, never saved document.
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileUrlChanged)
    // The file's name, or "Untitled".
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileUrlChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
public:
    explicit Document(QObject *parent = nullptr);

    [[nodiscard]] EditorView *editor() const { return m_editor; }
    void setEditor(EditorView *editor);
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
    void editorChanged();
    void fileUrlChanged();
    void modifiedChanged();
    void errorOccurred(const QString &message);

private:
    [[nodiscard]] std::expected<void, QString> write(const QUrl &fileUrl) const;
    void setFileUrl(const QUrl &fileUrl);

    QPointer<EditorView> m_editor;
    QUrl m_fileUrl;
    // What a line ends with in the file: "\n" or "\r\n".
    QString m_lineEnding{QStringLiteral("\n")};
};

} // namespace tekst
