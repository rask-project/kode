#include "RemoteTree.h"

#include <QSet>

namespace kode::remote
{

RemoteTree::RemoteTree(QObject *parent) : QAbstractItemModel(parent)
{
    m_nodes.emplace_back(Node{.name = {}, .children = {}, .parent = -1, .flags = Folder});
}

RemoteTree::~RemoteTree()
{
    clear();
}

void RemoteTree::clear()
{
    for (qint64 id{0}; id < m_nodes.size(); ++id) {
        const Node &node{m_nodes.at(id)};
        if ((node.flags & (Free | Listed)) == Listed && m_host != nullptr) {
            m_host->unwatch(pathOf(id));
        }
    }
    m_nodes.clear();
    m_free.clear();
    m_nodes.emplace_back(Node{.name = {}, .children = {}, .parent = -1, .flags = Folder});
}

void RemoteTree::setRoot(Host *host, const QString &root)
{
    if (m_host == host && m_root == root) {
        return;
    }
    beginResetModel();
    clear();
    disconnect(m_changed);
    disconnect(m_online);
    m_host = host;
    m_root = root;
    ++m_generation;
    if (m_host != nullptr) {
        m_changed = connect(m_host, &Host::changed, this, &RemoteTree::refresh);
        // Back online: what changed meanwhile was not told.
        m_online = connect(m_host, &Host::stateChanged, this, [this] {
            if (m_host == nullptr || m_host->state() != Host::State::Online) {
                return;
            }
            for (qint64 id{0}; id < m_nodes.size(); ++id) {
                const quint64 flags{m_nodes.at(id).flags};
                if ((flags & (Free | Listed)) == Listed || (id == 0 && (flags & Listed) == 0U)) {
                    list(id);
                }
            }
        });
    }
    endResetModel();
    if (m_host != nullptr && !m_root.isEmpty()) {
        // Connected first when it is not: the root is listed once online (above).
        m_host->whenOnline(this, [](const auto &) {});
        list(0);
    }
}

void RemoteTree::setShowHidden(bool show)
{
    if (showHidden() == show) {
        return;
    }
    m_flags = show ? (m_flags | ShowHidden) : (m_flags & ~ShowHidden);
    for (qint64 id{0}; id < m_nodes.size(); ++id) {
        if ((m_nodes.at(id).flags & (Free | Listed)) == Listed) {
            list(id);
        }
    }
}

qint64 RemoteTree::nodeOf(const QModelIndex &index) const noexcept
{
    return index.isValid() ? static_cast<qint64>(index.internalId()) : 0;
}

QModelIndex RemoteTree::indexOfNode(qint64 id) const
{
    if (id <= 0) {
        return {};
    }
    const qint64 parent{m_nodes.at(id).parent};
    return createIndex(static_cast<qint32>(m_nodes.at(parent).children.indexOf(id)), 0, static_cast<quintptr>(id));
}

QString RemoteTree::pathOf(qint64 id) const
{
    QStringList names;
    for (qint64 at{id}; at > 0; at = m_nodes.at(at).parent) {
        names.prepend(m_nodes.at(at).name);
    }
    if (names.isEmpty()) {
        return m_root;
    }
    return (m_root == QLatin1String("/") ? QString{} : m_root) + u'/' + names.join(u'/');
}

qint64 RemoteTree::child(qint64 parent, const QString &name) const
{
    const QList<qint64> &children{m_nodes.at(parent).children};
    const auto found{std::ranges::find_if(children, [this, &name](qint64 id) { return m_nodes.at(id).name == name; })};
    return found == children.end() ? -1 : *found;
}

qint64 RemoteTree::find(const QString &path) const
{
    if (m_root.isEmpty()) {
        return -1;
    }
    if (path == m_root) {
        return 0;
    }
    const QString prefix{m_root == QLatin1String("/") ? m_root : m_root + u'/'};
    if (!path.startsWith(prefix)) {
        return -1;
    }
    qint64 id{0};
    const QStringList names{path.mid(prefix.size()).split(u'/', Qt::SkipEmptyParts)};
    for (const QString &name : names) {
        id = child(id, name);
        if (id < 0) {
            return -1;
        }
    }
    return id;
}

qint64 RemoteTree::add(qint64 parent, const QString &name, bool folder)
{
    const Node node{.name = name, .children = {}, .parent = parent, .flags = folder ? Folder : 0U};
    qint64 id{0};
    if (m_free.isEmpty()) {
        id = m_nodes.size();
        m_nodes.emplace_back(node);
    } else {
        id = m_free.takeLast();
        m_nodes[id] = node;
    }
    m_nodes[parent].children.append(id);
    return id;
}

void RemoteTree::drop(qint64 id)
{
    // The whole subtree first: a path walks the parents, freed below.
    QList<qint64> subtree{id};
    for (qsizetype at{0}; at < subtree.size(); ++at) {
        subtree.append(m_nodes.at(subtree.at(at)).children);
    }
    std::ranges::for_each(subtree, [this](qint64 at) {
        if ((m_nodes.at(at).flags & Listed) != 0U && m_host != nullptr) {
            m_host->unwatch(pathOf(at));
        }
    });
    std::ranges::for_each(subtree, [this](qint64 at) {
        m_nodes[at] = Node{.name = {}, .children = {}, .parent = -1, .flags = Free};
        m_free.append(at);
    });
}

void RemoteTree::list(qint64 id)
{
    Connection *const connection{m_host != nullptr ? m_host->connection() : nullptr};
    if (connection == nullptr || (m_nodes.at(id).flags & Listing) != 0U) {
        return;
    }
    m_nodes[id].flags |= Listing;
    const QPointer<RemoteTree> self{this};
    const quint64 generation{m_generation};
    const QString path{pathOf(id)};
    files::list(*connection, path, [self, generation, path](auto entries) {
        if (self != nullptr && generation == self->m_generation) {
            self->listed(path, entries);
        }
    });
}

void RemoteTree::listed(const QString &path, const std::expected<QList<files::Entry>, Error> &entries)
{
    const qint64 id{find(path)};
    if (id < 0) {
        return;
    }
    m_nodes[id].flags &= ~Listing;
    const bool wasListed{(m_nodes.at(id).flags & Listed) != 0U};
    m_nodes[id].flags |= Listed;
    if (!wasListed && m_host != nullptr) {
        m_host->watch(path);
    }
    QHash<QString, bool> wanted;
    if (entries) {
        std::ranges::for_each(*entries, [this, &wanted](const files::Entry &entry) {
            if (showHidden() || !entry.name.startsWith(u'.')) {
                wanted.insert(entry.name, entry.isFolder());
            }
        });
    }
    const QModelIndex parent{indexOfNode(id)};
    // Gone, or now of the other kind: removed, from the bottom (rows above stay put).
    for (qsizetype row{m_nodes.at(id).children.size() - 1}; row >= 0; --row) {
        const qint64 old{m_nodes.at(id).children.at(row)};
        const auto still{wanted.constFind(m_nodes.at(old).name)};
        if (still != wanted.cend() && still.value() == ((m_nodes.at(old).flags & Folder) != 0U)) {
            wanted.remove(m_nodes.at(old).name);
            continue;
        }
        beginRemoveRows(parent, static_cast<qint32>(row), static_cast<qint32>(row));
        drop(old);
        m_nodes[id].children.removeAt(row);
        endRemoveRows();
    }
    for (auto entry{wanted.cbegin()}; entry != wanted.cend(); ++entry) {
        const auto row{static_cast<qint32>(m_nodes.at(id).children.size())};
        beginInsertRows(parent, row, row);
        add(id, entry.key(), entry.value());
        endInsertRows();
    }
    emit folderLoaded(path);
}

QString RemoteTree::path(const QModelIndex &index) const
{
    return pathOf(nodeOf(index));
}

QString RemoteTree::name(const QModelIndex &index) const
{
    return m_nodes.at(nodeOf(index)).name;
}

bool RemoteTree::isFolder(const QModelIndex &index) const
{
    return (m_nodes.at(nodeOf(index)).flags & Folder) != 0U;
}

QModelIndex RemoteTree::indexOf(const QString &path)
{
    if (m_root.isEmpty() ||
        (path != m_root && !path.startsWith(m_root == QLatin1String("/") ? m_root : m_root + u'/'))) {
        return {};
    }
    const QStringList names{path.mid(m_root.size()).split(u'/', Qt::SkipEmptyParts)};
    qint64 id{0};
    for (const QString &name : names) {
        if ((m_nodes.at(id).flags & Listed) == 0U) {
            list(id);
            return {};
        }
        id = child(id, name);
        if (id < 0) {
            return {};
        }
    }
    return indexOfNode(id);
}

bool RemoteTree::knowsFolder(const QString &path) const
{
    const qint64 id{find(path)};
    return id >= 0 && (m_nodes.at(id).flags & Folder) != 0U;
}

bool RemoteTree::has(const QString &folder, const QString &name) const
{
    const qint64 id{find(folder)};
    return id >= 0 && child(id, name) >= 0;
}

void RemoteTree::insert(const QString &folder, const QString &name, bool isFolder)
{
    const qint64 id{find(folder)};
    if (id < 0 || (m_nodes.at(id).flags & Listed) == 0U || child(id, name) >= 0) {
        return;
    }
    const auto row{static_cast<qint32>(m_nodes.at(id).children.size())};
    beginInsertRows(indexOfNode(id), row, row);
    add(id, name, isFolder);
    endInsertRows();
}

void RemoteTree::remove(const QString &path)
{
    const qint64 id{find(path)};
    if (id <= 0) {
        return;
    }
    const qint64 parent{m_nodes.at(id).parent};
    const auto row{static_cast<qint32>(m_nodes.at(parent).children.indexOf(id))};
    beginRemoveRows(indexOfNode(parent), row, row);
    drop(id);
    m_nodes[parent].children.removeAt(row);
    endRemoveRows();
}

void RemoteTree::rename(const QString &path, const QString &name)
{
    const qint64 id{find(path)};
    if (id <= 0) {
        return;
    }
    const bool folder{(m_nodes.at(id).flags & Folder) != 0U};
    const QString parent{pathOf(m_nodes.at(id).parent)};
    remove(path);
    insert(parent, name, folder);
}

void RemoteTree::refresh(const QString &folder)
{
    const qint64 id{find(folder)};
    if (id >= 0 && (m_nodes.at(id).flags & Listed) != 0U) {
        list(id);
    }
}

QModelIndex RemoteTree::index(qint32 row, qint32 column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0) {
        return {};
    }
    const QList<qint64> &children{m_nodes.at(nodeOf(parent)).children};
    if (row >= children.size()) {
        return {};
    }
    return createIndex(row, 0, static_cast<quintptr>(children.at(row)));
}

QModelIndex RemoteTree::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return {};
    }
    return indexOfNode(m_nodes.at(nodeOf(child)).parent);
}

qint32 RemoteTree::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) {
        return 0;
    }
    return static_cast<qint32>(m_nodes.at(nodeOf(parent)).children.size());
}

qint32 RemoteTree::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

QVariant RemoteTree::data(const QModelIndex &index, qint32 role) const
{
    if (!index.isValid() || role != Qt::DisplayRole) {
        return {};
    }
    return m_nodes.at(nodeOf(index)).name;
}

bool RemoteTree::hasChildren(const QModelIndex &parent) const
{
    const Node &node{m_nodes.at(nodeOf(parent))};
    if ((node.flags & Folder) == 0U) {
        return false;
    }
    return (node.flags & Listed) == 0U || !node.children.isEmpty();
}

bool RemoteTree::canFetchMore(const QModelIndex &parent) const
{
    const quint64 flags{m_nodes.at(nodeOf(parent)).flags};
    return (flags & Folder) != 0U && (flags & (Listed | Listing)) == 0U;
}

void RemoteTree::fetchMore(const QModelIndex &parent)
{
    list(nodeOf(parent));
}

} // namespace kode::remote
