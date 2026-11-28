#pragma once

#include "Files.h"
#include "Host.h"

#include <QAbstractItemModel>
#include <QList>
#include <QPointer>
#include <QString>

namespace kode::remote
{

// A folder on a host as a tree for the sidebar (FileTree sorts and filters
// it): one column of names; a folder listed when it is expanded (fetchMore),
// watched there once listed and listed again when the host says it
// changed. The operations FileTree makes there show at once (insert(),
// remove(), rename()); a failed one lists the folder again.
class RemoteTree : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit RemoteTree(QObject *parent = nullptr);
    ~RemoteTree() override;
    Q_DISABLE_COPY_MOVE(RemoteTree)

    // `root` on `host`; none clears it.
    void setRoot(Host *host, const QString &root);
    [[nodiscard]] Host *host() const { return m_host; }
    [[nodiscard]] const QString &root() const noexcept { return m_root; }
    void setShowHidden(bool show);
    [[nodiscard]] bool showHidden() const noexcept { return (m_flags & ShowHidden) != 0U; }

    [[nodiscard]] QString path(const QModelIndex &index) const;
    [[nodiscard]] QString name(const QModelIndex &index) const;
    [[nodiscard]] bool isFolder(const QModelIndex &index) const;
    // The entry of `path`; invalid until the folders on the way are listed
    // (they are asked for: folderLoaded() tells).
    [[nodiscard]] QModelIndex indexOf(const QString &path);
    // `path` is a folder this tree knows.
    [[nodiscard]] bool knowsFolder(const QString &path) const;
    // `name` is taken in the folder `folder` (as far as its listing tells).
    [[nodiscard]] bool has(const QString &folder, const QString &name) const;

    void insert(const QString &folder, const QString &name, bool isFolder);
    void remove(const QString &path);
    void rename(const QString &path, const QString &name);
    // Lists `folder` again, if it was listed.
    void refresh(const QString &folder);

    [[nodiscard]] QModelIndex index(qint32 row, qint32 column, const QModelIndex &parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex &child) const override;
    [[nodiscard]] qint32 rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] qint32 columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, qint32 role) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex &parent = {}) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

signals:
    void folderLoaded(const QString &path);

private:
    static constexpr quint64 Folder{1U << 0U};
    static constexpr quint64 Listed{1U << 1U};
    static constexpr quint64 Listing{1U << 2U};
    // A slot free for reuse.
    static constexpr quint64 Free{1U << 3U};
    static constexpr quint64 ShowHidden{1U << 0U};

    struct Node {
        QString name;
        QList<qint64> children;
        qint64 parent{-1};
        quint64 flags{0U};
    };

    [[nodiscard]] qint64 nodeOf(const QModelIndex &index) const noexcept;
    [[nodiscard]] QModelIndex indexOfNode(qint64 id) const;
    [[nodiscard]] QString pathOf(qint64 id) const;
    // The node of `path`, or -1.
    [[nodiscard]] qint64 find(const QString &path) const;
    [[nodiscard]] qint64 child(qint64 parent, const QString &name) const;
    qint64 add(qint64 parent, const QString &name, bool folder);
    // `id` and what it holds go (each listed folder unwatched).
    void drop(qint64 id);
    void list(qint64 id);
    void listed(const QString &path, const std::expected<QList<files::Entry>, Error> &entries);
    void clear();

    QList<Node> m_nodes;
    QList<qint64> m_free;
    QPointer<Host> m_host;
    QMetaObject::Connection m_changed;
    QMetaObject::Connection m_online;
    QString m_root;
    // Bumped with the root: an older listing is dropped.
    quint64 m_generation{0U};
    quint64 m_flags{0U};
};

} // namespace kode::remote
