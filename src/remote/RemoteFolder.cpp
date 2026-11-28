#include "RemoteFolder.h"

namespace kode::remote
{

RemoteFolder::RemoteFolder(QObject *parent) : QAbstractListModel(parent)
{
    m_collator.setNumericMode(true);
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
}

void RemoteFolder::setHost(Host *host)
{
    if (m_host == host) {
        return;
    }
    if (m_host != nullptr) {
        m_host->disconnect(this);
    }
    m_host = host;
    if (m_host != nullptr) {
        connect(m_host, &Host::stateChanged, this, &RemoteFolder::refresh);
    }
    emit hostChanged();
    refresh();
}

void RemoteFolder::setFolder(const QString &folder)
{
    if (m_folder == folder) {
        return;
    }
    m_folder = folder;
    emit folderChanged();
    refresh();
}

QString RemoteFolder::parentFolder() const
{
    if (m_folder.isEmpty() || m_folder == QLatin1String("/")) {
        return {};
    }
    const qsizetype slash{m_folder.lastIndexOf(u'/')};
    return slash <= 0 ? QStringLiteral("/") : m_folder.left(slash);
}

void RemoteFolder::refresh()
{
    Connection *const connection{m_host != nullptr ? m_host->connection() : nullptr};
    if (connection == nullptr) {
        return;
    }
    if (m_folder.isEmpty()) {
        m_folder = m_host->home();
        emit folderChanged();
    }
    const quint64 generation{++m_generation};
    m_loading = 1U;
    emit loadingChanged();
    const QPointer<RemoteFolder> self{this};
    const QString folder{m_folder};
    files::list(*connection, folder, [self, generation, folder](auto entries) {
        if (self != nullptr && generation == self->m_generation) {
            self->listed(folder, entries);
        }
    });
}

void RemoteFolder::listed(const QString &folder, const std::expected<QList<files::Entry>, Error> &entries)
{
    beginResetModel();
    m_entries.clear();
    m_error = entries ? QString{} : tr("“%1” could not be read: %2").arg(folder, entries.error().message);
    if (entries) {
        std::ranges::copy_if(*entries, std::back_inserter(m_entries), [](const files::Entry &entry) {
            return !entry.name.startsWith(u'.');
        });
        std::ranges::sort(m_entries, [this](const files::Entry &left, const files::Entry &right) {
            if (left.isFolder() != right.isFolder()) {
                return left.isFolder();
            }
            return m_collator.compare(left.name, right.name) < 0;
        });
    }
    endResetModel();
    m_loading = 0U;
    emit loadingChanged();
}

QString RemoteFolder::path(qsizetype row) const
{
    return (m_folder == QLatin1String("/") ? QString{} : m_folder) + u'/' + m_entries.at(row).name;
}

qint32 RemoteFolder::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<qint32>(m_entries.size());
}

QVariant RemoteFolder::data(const QModelIndex &index, qint32 role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) {
        return {};
    }
    const files::Entry &entry{m_entries.at(index.row())};
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case IsFolderRole:
        return entry.isFolder();
    case PathRole:
        return path(index.row());
    case UrlRole:
        return m_host != nullptr ? m_host->fileUrl(path(index.row())) : QUrl{};
    default:
        return {};
    }
}

QHash<qint32, QByteArray> RemoteFolder::roleNames() const
{
    return {
        {NameRole, QByteArrayLiteral("name")},
        {IsFolderRole, QByteArrayLiteral("isFolder")},
        {PathRole, QByteArrayLiteral("path")},
        {UrlRole, QByteArrayLiteral("fileUrl")},
    };
}

} // namespace kode::remote
