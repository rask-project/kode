#pragma once

#include "Files.h"
#include "Host.h"

#include <QAbstractListModel>
#include <QCollator>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode::remote
{

// A folder on a host, for picking a file there (NSOpenPanel cannot browse a
// host): its entries, folders first, by name as the Finder sorts them,
// hidden ones left out; read again when the folder or the host's state
// changes. Starts at the user's home.
class RemoteFolder : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(kode::remote::Host *host READ host WRITE setHost NOTIFY hostChanged)
    // An absolute path there; empty: the home.
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    // The folder that holds it; empty at the top.
    Q_PROPERTY(QString parentFolder READ parentFolder NOTIFY folderChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY loadingChanged)
public:
    enum Role : quint16 {
        NameRole = Qt::UserRole + 1,
        IsFolderRole,
        PathRole,
        UrlRole,
    };
    Q_ENUM(Role)

    explicit RemoteFolder(QObject *parent = nullptr);

    [[nodiscard]] Host *host() const { return m_host; }
    void setHost(Host *host);
    [[nodiscard]] QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    [[nodiscard]] QString parentFolder() const;
    [[nodiscard]] bool loading() const noexcept { return m_loading != 0U; }
    [[nodiscard]] QString errorString() const { return m_error; }

    [[nodiscard]] qint32 rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, qint32 role) const override;
    [[nodiscard]] QHash<qint32, QByteArray> roleNames() const override;

signals:
    void hostChanged();
    void folderChanged();
    void loadingChanged();

private:
    void refresh();
    void listed(const QString &folder, const std::expected<QList<files::Entry>, Error> &entries);
    [[nodiscard]] QString path(qsizetype row) const;

    QPointer<Host> m_host;
    QList<files::Entry> m_entries;
    QCollator m_collator;
    QString m_folder;
    QString m_error;
    // Bumped by each listing: an older answer is dropped.
    quint64 m_generation{0U};
    quint64 m_loading{0U};
};

} // namespace kode::remote
