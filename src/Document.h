#pragma once

#include "TextCodec.h"
#include "TextDocument.h"
#include "remote/Connection.h"
#include "remote/Files.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QFuture>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <expected>

namespace kode
{

// An open file: its text (a TextDocument, shown by any number of
// EditorViews), loaded from the file and saved back. The file's encoding (detected, or chosen with reopen()), byte
// order mark and line ending (LF or CRLF) are kept unless changed; saving goes through QSaveFile, so a failed write
// leaves the file as it was. A change to the file by another program reloads it when there are no unsaved changes, else
// emits changedOnDisk(). A large file is read and decoded on a worker thread (`loading` meanwhile).
// An ssh:// file is on a host (docs/REMOTE.md): read and written through its agent, always asynchronously; a save
// is checked there against the version read (a conflict emits changedOnDisk()), and the host is asked every few
// seconds whether the file changed.
class Document : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // The text, for EditorView.document.
    Q_PROPERTY(kode::TextDocument *textDocument READ textDocument CONSTANT)
    // Empty for a new, never saved document.
    Q_PROPERTY(QUrl fileUrl READ fileUrl NOTIFY fileUrlChanged)
    // The file's name, or "Untitled" (numbered from the second one).
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileUrlChanged)
    // The text, or how it is saved (encoding, line ending), changed.
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
    // How the file is saved: an IANA name from `encodings`.
    Q_PROPERTY(QString encoding READ encoding WRITE setEncoding NOTIFY formatChanged)
    Q_PROPERTY(bool byteOrderMark READ byteOrderMark NOTIFY formatChanged)
    // Lines end with CRLF (else LF).
    Q_PROPERTY(bool crlf READ crlf WRITE setCrlf NOTIFY formatChanged)
    Q_PROPERTY(QStringList encodings READ encodings CONSTANT)
    // A large file is being read: the text shows when it is ready.
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
public:
    explicit Document(QObject *parent = nullptr);

    [[nodiscard]] TextDocument *textDocument() noexcept { return &m_text; }
    [[nodiscard]] QUrl fileUrl() const { return m_fileUrl; }
    [[nodiscard]] QString fileName() const;
    // Which untitled document this is, from 1 (OpenDocuments numbers them).
    void setUntitledNumber(qint32 number);
    [[nodiscard]] bool modified() const;
    [[nodiscard]] QString encoding() const { return m_format.encoding; }
    void setEncoding(const QString &encoding);
    [[nodiscard]] bool byteOrderMark() const noexcept { return !m_format.byteOrderMark.isEmpty(); }
    [[nodiscard]] bool crlf() const noexcept { return (m_flags & Crlf) != 0U; }
    void setCrlf(bool crlf);
    [[nodiscard]] static QStringList encodings() { return codec::encodings(); }
    [[nodiscard]] bool loading() const noexcept { return (m_flags & Loading) != 0U; }

    // Each returns false and emits errorOccurred() when it fails (a large or
    // remote file: later, and loadFailed() too if nothing was read before).
    Q_INVOKABLE bool load(const QUrl &fileUrl);
    // Reads the file again as `encoding` (unsaved changes are lost).
    Q_INVOKABLE bool reopen(const QString &encoding);
    // Reads the file again (unsaved changes are lost), keeping the caret.
    Q_INVOKABLE bool reload();
    // Each ends with saved() or saveFailed(), at once for a file on this Mac,
    // once the host answered for a remote one; false: failed already.
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl &fileUrl);
    // After changedOnDisk(): the next save replaces the file's new version.
    Q_INVOKABLE void keepMine();
    // An empty, untitled document.
    Q_INVOKABLE void clear();
    // An empty document for `fileUrl`, a file that does not exist yet: saving writes it.
    void startFile(const QUrl &fileUrl);
    // The file was renamed or moved to `fileUrl` (from Kode's sidebar): the text stays.
    void followMove(const QUrl &fileUrl);

signals:
    void fileUrlChanged();
    void modifiedChanged();
    void formatChanged();
    void errorOccurred(const QString &message);
    // Another program changed the file while it has unsaved changes here.
    void changedOnDisk();
    void loadingChanged();
    void loadFailed();
    void saved();
    void saveFailed();

private:
    static constexpr quint64 Crlf{1U << 0U};
    // The encoding or line ending changed since the file was read or saved.
    static constexpr quint64 FormatChanged{1U << 1U};
    static constexpr quint64 Loading{1U << 2U};
    // A file was read into it once.
    static constexpr quint64 Read{1U << 3U};
    // The views keep their place when the worker's text arrives.
    static constexpr quint64 KeepPlace{1U << 4U};

    struct Loaded {
        TextBuffer buffer;
        codec::Format format;
        // Crlf or nothing.
        quint64 flags{0U};
    };
    using ReadResult = std::expected<Loaded, QString>;

    // Reads the file as `encoding` (detected when empty); views keep their
    // place when `keepPlace`.
    bool read(const QUrl &fileUrl, const QString &encoding, bool keepPlace);
    // Any thread.
    [[nodiscard]] static ReadResult readFile(const QString &path, const QString &encoding);
    [[nodiscard]] static ReadResult decode(QIODevice &device, const QString &encoding, const QString &name);
    bool finishRead(const QUrl &fileUrl, ReadResult result, bool keepPlace);
    // `future` delivers the text read for read number `generation`.
    void finishLater(QFuture<ReadResult> future, const QUrl &fileUrl, quint64 generation);
    void readRemote(const QUrl &fileUrl, const QString &encoding, bool keepPlace);
    void remoteReadFailed(const QUrl &fileUrl, const remote::Error &error);
    bool saveRemote(const QUrl &fileUrl);
    void remoteSaved(const QUrl &fileUrl, const std::expected<remote::files::Version, remote::Error> &result);
    void checkRemote();
    [[nodiscard]] std::expected<QByteArray, QString> encoded(const QString &name) const;
    void setLoading(bool loading);
    [[nodiscard]] std::expected<void, QString> write(const QUrl &fileUrl) const;
    void setFileUrl(const QUrl &fileUrl);
    void setFormatChanged(bool changed);
    // Watches the file, remembering the version read or written.
    void watch();
    void checkFile();

    TextDocument m_text;
    QUrl m_fileUrl;
    codec::Format m_format;
    QFileSystemWatcher m_watcher;
    QTimer m_checkTimer;
    // A remote file: asking the host whether it changed.
    QTimer m_pollTimer;
    // A remote file: the version read or written here, and the last one seen there.
    remote::files::Version m_remote;
    remote::files::Version m_remoteSeen;
    // The file's version last read or written here.
    QDateTime m_fileModified;
    qint64 m_fileSize{-1};
    quint64 m_flags{0U};
    qint64 m_untitledNumber{1};
    // Bumped by each read: a worker's result for an older one is dropped.
    quint64 m_generation{0};
};

} // namespace kode
