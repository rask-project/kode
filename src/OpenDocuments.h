#pragma once

#include "Document.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <vector>

namespace kode
{

// Every document open in the app, as in Qt Creator: any split of any window
// shows one of them, and several may show the same one. Owns the
// Documents; the most recently shown comes first.
class OpenDocuments : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Most recently shown first.
    Q_PROPERTY(QVariantList documents READ documents NOTIFY documentsChanged)
    // By name, for the documents menu.
    Q_PROPERTY(QVariantList sortedDocuments READ sortedDocuments NOTIFY documentsChanged)
    Q_PROPERTY(qint32 count READ count NOTIFY documentsChanged)
public:
    explicit OpenDocuments(QObject *parent = nullptr);
    ~OpenDocuments() override;

    [[nodiscard]] QVariantList documents() const;
    [[nodiscard]] QVariantList sortedDocuments() const;
    [[nodiscard]] qint32 count() const noexcept { return static_cast<qint32>(m_documents.size()); }

    // The document of `file`, read now if it is not open yet; null (and
    // errorOccurred()) when it cannot be read.
    Q_INVOKABLE kode::Document *open(const QUrl &file);
    // Opens each of `files` still there, silently (a restored session).
    Q_INVOKABLE void restore(const QStringList &files);
    // The open document of `file`, or null.
    Q_INVOKABLE [[nodiscard]] kode::Document *find(const QUrl &file) const;
    // A new, untitled document.
    Q_INVOKABLE kode::Document *create();
    // Removes `document` (deleted on the next event loop turn).
    Q_INVOKABLE void close(kode::Document *document);
    // `document` was shown: it moves to the front.
    Q_INVOKABLE void touch(kode::Document *document);
    // The most recent document other than `document`, or null.
    Q_INVOKABLE [[nodiscard]] kode::Document *other(const kode::Document *document) const;
    Q_INVOKABLE [[nodiscard]] QVariantList modifiedDocuments() const;
    // The documents of `url`, or of files in it when it is a folder.
    Q_INVOKABLE [[nodiscard]] QVariantList documentsIn(const QUrl &url) const;
    // `from` (a file or folder) was renamed or moved to `to`: the documents of it, or in it, follow.
    Q_INVOKABLE void fileMoved(const QUrl &from, const QUrl &to);
    // `url` is a folder on disk (a window's folder, not a document).
    Q_INVOKABLE [[nodiscard]] static bool isFolder(const QUrl &url);
    // `folder` without a trailing slash, to compare and name it.
    Q_INVOKABLE [[nodiscard]] static QUrl cleanFolder(const QUrl &folder);

signals:
    void documentsChanged();
    // Views showing `document` must show another one.
    void documentClosing(kode::Document *document);
    void errorOccurred(const QString &message);
    // Another program changed `document`'s file while it has unsaved changes.
    void changedOnDisk(kode::Document *document);

private:
    struct DeleteLater {
        void operator()(QObject *object) const { object->deleteLater(); }
    };
    using Pointer = std::unique_ptr<Document, DeleteLater>;

    Document *adopt(Pointer document);
    void numberUntitled();

    // Most recently shown first. std::vector: QList needs copyable elements.
    std::vector<Pointer> m_documents;
};

} // namespace kode
