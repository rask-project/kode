#pragma once

#include "EditorView.h"
#include "TextCodec.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <expected>

namespace tekst
{

// The file behind an editor's text: loads it into an EditorView and saves it
// back. The file's encoding (detected, or chosen with reopen()), byte order
// mark and line ending (LF or CRLF) are kept unless changed; saving goes
// through QSaveFile, so a failed write leaves the file as it was. A change
// to the file by another program reloads it when there are no unsaved
// changes, else emits changedOnDisk().
class Document : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(tekst::EditorView *editor READ editor WRITE setEditor NOTIFY editorChanged)
    // Empty for a new, never saved document.
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileUrlChanged)
    // The file's name, or "Untitled".
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileUrlChanged)
    // The text, or how it is saved (encoding, line ending), changed.
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    // How the file is saved: an IANA name from `encodings`.
    Q_PROPERTY(QString encoding READ encoding WRITE setEncoding NOTIFY formatChanged)
    Q_PROPERTY(bool byteOrderMark READ byteOrderMark NOTIFY formatChanged)
    // Lines end with CRLF (else LF).
    Q_PROPERTY(bool crlf READ crlf WRITE setCrlf NOTIFY formatChanged)
    Q_PROPERTY(QStringList encodings READ encodings CONSTANT)
public:
    explicit Document(QObject *parent = nullptr);

    [[nodiscard]] EditorView *editor() const { return m_editor; }
    void setEditor(EditorView *editor);
    [[nodiscard]] QUrl fileUrl() const { return m_fileUrl; }
    [[nodiscard]] QString fileName() const;
    [[nodiscard]] bool modified() const;
    [[nodiscard]] QString encoding() const { return m_format.encoding; }
    void setEncoding(const QString &encoding);
    [[nodiscard]] bool byteOrderMark() const noexcept { return !m_format.byteOrderMark.isEmpty(); }
    [[nodiscard]] bool crlf() const noexcept { return (m_flags & Crlf) != 0U; }
    void setCrlf(bool crlf);
    [[nodiscard]] static QStringList encodings() { return codec::encodings(); }

    // Each returns false and emits errorOccurred() when it fails.
    Q_INVOKABLE bool load(const QUrl &fileUrl);
    // Reads the file again as `encoding` (unsaved changes are lost).
    Q_INVOKABLE bool reopen(const QString &encoding);
    // Reads the file again (unsaved changes are lost), keeping the caret.
    Q_INVOKABLE bool reload();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl &fileUrl);
    // An empty, untitled document.
    Q_INVOKABLE void clear();

signals:
    void editorChanged();
    void fileUrlChanged();
    void modifiedChanged();
    void formatChanged();
    void errorOccurred(const QString &message);
    // Another program changed the file while it has unsaved changes here.
    void changedOnDisk();

private:
    static constexpr quint64 Crlf{1U << 0U};
    // The encoding or line ending changed since the file was read or saved.
    static constexpr quint64 FormatChanged{1U << 1U};

    // Reads the file as `encoding` (detected when empty).
    bool read(const QUrl &fileUrl, const QString &encoding);
    [[nodiscard]] std::expected<void, QString> write(const QUrl &fileUrl) const;
    void setFileUrl(const QUrl &fileUrl);
    void setFormatChanged(bool changed);
    // Watches the file, remembering the version read or written.
    void watch();
    void checkFile();

    QPointer<EditorView> m_editor;
    QUrl m_fileUrl;
    codec::Format m_format;
    QFileSystemWatcher m_watcher;
    QTimer m_checkTimer;
    // The file's version last read or written here.
    QDateTime m_fileModified;
    qint64 m_fileSize{-1};
    quint64 m_flags{0U};
};

} // namespace tekst
