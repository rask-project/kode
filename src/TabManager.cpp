#include "TabManager.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QMetaProperty>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>

#include <algorithm>

namespace tekst
{

namespace
{

const QString fileKey{QStringLiteral("file")};
const QString splitKey{QStringLiteral("split")};
const QString panesKey{QStringLiteral("panes")};
const QString tabsKey{QStringLiteral("tabs")};
const QString currentKey{QStringLiteral("current")};
const QString initialFileProperty{QStringLiteral("initialFile")};

QUrl fileOf(const QObject *pane)
{
    return pane->property("fileUrl").toUrl();
}

// The nodes under `root`, children after their parent (first before second).
QList<PaneNode *> preorder(PaneNode *root)
{
    QList<PaneNode *> nodes;
    QList<PaneNode *> stack;
    if (root != nullptr) {
        stack.emplace_back(root);
    }
    while (!stack.isEmpty()) {
        PaneNode *const node{stack.takeLast()};
        nodes.emplace_back(node);
        if (!node->leaf()) {
            stack.emplace_back(node->second());
            stack.emplace_back(node->first());
        }
    }
    return nodes;
}

// Children before their parent: the order buildTree() reads.
QList<PaneNode *> postorder(PaneNode *root)
{
    QList<PaneNode *> reversed;
    QList<PaneNode *> stack;
    if (root != nullptr) {
        stack.emplace_back(root);
    }
    while (!stack.isEmpty()) {
        PaneNode *const node{stack.takeLast()};
        reversed.emplace_back(node);
        if (!node->leaf()) {
            stack.emplace_back(node->first());
            stack.emplace_back(node->second());
        }
    }
    std::ranges::reverse(reversed);
    return reversed;
}

PaneNode *leafOf(PaneNode *root, const QObject *pane)
{
    const QList<PaneNode *> nodes{preorder(root)};
    const auto leaf{std::ranges::find(nodes, pane, &PaneNode::pane)};
    return leaf != nodes.end() ? *leaf : nullptr;
}

PaneNode *parentOf(PaneNode *root, const PaneNode *node)
{
    const QList<PaneNode *> nodes{preorder(root)};
    const auto parent{std::ranges::find_if(nodes, [node](const PaneNode *candidate) {
        return candidate->first() == node || candidate->second() == node;
    })};
    return parent != nodes.end() ? *parent : nullptr;
}

} // namespace

PaneNode::PaneNode(PanePointer pane) : m_pane{std::move(pane)}
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

PaneNode::PaneNode(Qt::Orientation orientation, Pointer first, Pointer second)
    : m_first{std::move(first)}
    , m_second{std::move(second)}
    , m_orientation{orientation}
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

Tab::Tab(PaneNode::Pointer root) : m_root{std::move(root)}
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    treeChanged();
}

qint32 Tab::paneCount() const
{
    return static_cast<qint32>(panes().size());
}

QList<QObject *> Tab::panes() const
{
    QList<QObject *> panes;
    for (const PaneNode *node : preorder(m_root.get())) {
        if (node->leaf()) {
            panes.emplace_back(node->pane());
        }
    }
    return panes;
}

QString Tab::title() const
{
    return m_activePane != nullptr ? m_activePane->property("title").toString() : QString{};
}

QString Tab::toolTip() const
{
    if (m_activePane == nullptr) {
        return {};
    }
    const QUrl file{fileOf(m_activePane)};
    return file.isLocalFile() ? file.toLocalFile() : title();
}

bool Tab::modified() const
{
    return std::ranges::any_of(panes(), [](const QObject *pane) { return pane->property("modified").toBool(); });
}

void Tab::setActivePane(QObject *pane)
{
    if (m_activePane == pane) {
        return;
    }
    m_activePane = pane;
    emit activePaneChanged();
    emit stateChanged();
}

void Tab::treeChanged()
{
    const QList<QObject *> panes{this->panes()};
    // The state follows every pane's title, file and unsaved changes.
    for (const QMetaObject::Connection &connection : std::exchange(m_paneConnections, {})) {
        disconnect(connection);
    }
    const QMetaMethod stateSignal{QMetaMethod::fromSignal(&Tab::stateChanged)};
    for (QObject *pane : panes) {
        const QMetaObject *meta{pane->metaObject()};
        for (const char *name : {"title", "modified", "fileUrl"}) {
            const QMetaProperty property{meta->property(meta->indexOfProperty(name))};
            if (property.hasNotifySignal()) {
                m_paneConnections.emplace_back(connect(pane, property.notifySignal(), this, stateSignal));
            }
        }
    }
    if (!panes.contains(m_activePane.get())) {
        setActivePane(panes.isEmpty() ? nullptr : panes.first());
    }
    emit rootChanged();
    for (PaneNode *node : preorder(m_root.get())) {
        if (!node->leaf()) {
            emit node->changed();
        }
    }
    emit stateChanged();
}

TabManager::TabManager(QObject *parent) : QObject(parent) {}

TabManager::~TabManager() = default;

void TabManager::setPaneComponent(QQmlComponent *component)
{
    if (m_paneComponent == component) {
        return;
    }
    m_paneComponent = component;
    emit paneComponentChanged();
}

QVariantList TabManager::tabs() const
{
    QVariantList tabs;
    tabs.reserve(tabCount());
    for (const Tab::Pointer &tab : m_tabs) {
        tabs.emplace_back(QVariant::fromValue(static_cast<QObject *>(tab.get())));
    }
    return tabs;
}

void TabManager::setCurrentIndex(qint32 index)
{
    const qint32 clamped{m_tabs.empty() ? -1 : std::clamp(index, 0, static_cast<qint32>(tabCount()) - 1)};
    if (m_currentIndex == clamped) {
        return;
    }
    m_currentIndex = clamped;
    emit currentChanged();
    emit activePaneChanged();
}

Tab *TabManager::currentTab() const
{
    return m_currentIndex >= 0 && m_currentIndex < tabCount() ? tabAt(m_currentIndex) : nullptr;
}

QObject *TabManager::activePane() const
{
    const Tab *const tab{currentTab()};
    return tab != nullptr ? tab->activePane() : nullptr;
}

QVariantList TabManager::panes() const
{
    QVariantList panes;
    for (const Tab::Pointer &tab : m_tabs) {
        for (QObject *pane : tab->panes()) {
            panes.emplace_back(QVariant::fromValue(pane));
        }
    }
    return panes;
}

qint32 TabManager::paneCount() const
{
    qint32 count{0};
    for (const Tab::Pointer &tab : m_tabs) {
        count += tab->paneCount();
    }
    return count;
}

qint32 TabManager::tabOf(const QObject *pane) const
{
    const auto tab{std::ranges::find_if(m_tabs, [pane](const Tab::Pointer &candidate) {
        return leafOf(candidate->root(), pane) != nullptr;
    })};
    return tab != m_tabs.end() ? static_cast<qint32>(std::distance(m_tabs.begin(), tab)) : -1;
}

PaneNode::PanePointer TabManager::createPane(const QVariantMap &properties)
{
    if (m_paneComponent == nullptr && m_editorPane == nullptr && qmlEngine(this) != nullptr) {
        m_editorPane =
            std::make_unique<QQmlComponent>(qmlEngine(this), QStringLiteral("Tekst"), QStringLiteral("EditorPane"));
    }
    QQmlComponent *const component{m_paneComponent != nullptr ? m_paneComponent.get() : m_editorPane.get()};
    if (component == nullptr || component->engine() == nullptr) {
        return nullptr;
    }
    // The engine's root context, not the window's: the pane may move to another window.
    PaneNode::PanePointer pane{component->createWithInitialProperties(properties, component->engine()->rootContext())};
    if (pane != nullptr) {
        QQmlEngine::setObjectOwnership(pane.get(), QQmlEngine::CppOwnership);
    }
    return pane;
}

QObject *TabManager::addTab(const QVariantMap &properties)
{
    PaneNode::PanePointer pane{createPane(properties)};
    if (pane == nullptr) {
        return nullptr;
    }
    QObject *const created{pane.get()};
    insertTab(std::make_unique<Tab>(std::make_unique<PaneNode>(std::move(pane))), tabCount());
    setCurrentIndex(static_cast<qint32>(tabCount()) - 1);
    return created;
}

QObject *TabManager::split(QObject *pane, qint32 orientation)
{
    if (tabOf(pane) < 0) {
        return nullptr;
    }
    PaneNode::PanePointer created{createPane({})};
    if (created == nullptr) {
        return nullptr;
    }
    QObject *const result{created.get()};
    insertBeside(
        pane,
        std::make_unique<PaneNode>(std::move(created)),
        orientation == Qt::Vertical ? EdgeBottom : EdgeRight
    );
    return result;
}

void TabManager::insertBeside(QObject *targetPane, PaneNode::Pointer leaf, Edge edge)
{
    const qint32 index{tabOf(targetPane)};
    if (index < 0) {
        return;
    }
    Tab &tab{*tabAt(index)};
    PaneNode *const target{leafOf(tab.root(), targetPane)};
    PaneNode *const parent{parentOf(tab.root(), target)};
    PaneNode::Pointer &slot{
        parent == nullptr ? tab.m_root : (parent->first() == target ? parent->m_first : parent->m_second)
    };
    QObject *const moved{leaf->pane()};
    const bool before{edge == EdgeLeft || edge == EdgeTop};
    const Qt::Orientation orientation{edge == EdgeLeft || edge == EdgeRight ? Qt::Horizontal : Qt::Vertical};
    PaneNode::Pointer existing{std::move(slot)};
    slot = before ? std::make_unique<PaneNode>(orientation, std::move(leaf), std::move(existing))
                  : std::make_unique<PaneNode>(orientation, std::move(existing), std::move(leaf));
    tab.treeChanged();
    tab.setActivePane(moved);
    setCurrentIndex(index);
    structureChanged();
}

PaneNode::Pointer TabManager::extractPane(QObject *pane, bool notifyEmpty)
{
    const qint32 index{tabOf(pane)};
    if (index < 0) {
        return nullptr;
    }
    Tab &tab{*tabAt(index)};
    PaneNode *const leaf{leafOf(tab.root(), pane)};
    PaneNode *const parent{parentOf(tab.root(), leaf)};
    if (parent == nullptr) {
        PaneNode::Pointer root{std::move(tab.m_root)};
        removeEmptyTab(index, notifyEmpty);
        return root;
    }
    PaneNode *const grandparent{parentOf(tab.root(), parent)};
    PaneNode::Pointer &parentSlot{
        grandparent == nullptr ? tab.m_root
                               : (grandparent->first() == parent ? grandparent->m_first : grandparent->m_second)
    };
    const bool first{parent->first() == leaf};
    PaneNode::Pointer extracted{std::move(first ? parent->m_first : parent->m_second)};
    PaneNode::Pointer sibling{std::move(first ? parent->m_second : parent->m_first)};
    // Replaces (and deletes) the split.
    parentSlot = std::move(sibling);
    tab.treeChanged();
    structureChanged();
    return extracted;
}

void TabManager::removeEmptyTab(qsizetype index, bool notifyEmpty)
{
    const Tab::Pointer removed{takeTab(index)};
    if (notifyEmpty && m_tabs.empty()) {
        emit emptied();
    }
}

void TabManager::closePane(QObject *pane)
{
    const PaneNode::Pointer removed{extractPane(pane, true)};
}

void TabManager::closeTab(qint32 index)
{
    if (index < 0 || index >= tabCount()) {
        return;
    }
    removeEmptyTab(index, true);
}

void TabManager::activatePane(QObject *pane)
{
    const qint32 index{tabOf(pane)};
    if (index < 0) {
        return;
    }
    tabAt(index)->setActivePane(pane);
    setCurrentIndex(index);
}

void TabManager::selectAdjacentTab(qint32 step)
{
    const qsizetype count{tabCount()};
    if (count < 2) {
        return;
    }
    setCurrentIndex(static_cast<qint32>((((m_currentIndex + step) % count) + count) % count));
}

void TabManager::moveTab(qint32 fromIndex, qint32 toIndex)
{
    const auto count{static_cast<qint32>(tabCount())};
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count || fromIndex == toIndex) {
        return;
    }
    const Tab *const current{currentTab()};
    // Rotates the tab into place, keeping the others' order.
    if (fromIndex < toIndex) {
        std::ranges::rotate(
            std::next(m_tabs.begin(), fromIndex),
            std::next(m_tabs.begin(), fromIndex + 1),
            std::next(m_tabs.begin(), toIndex + 1)
        );
    } else {
        std::ranges::rotate(
            std::next(m_tabs.begin(), toIndex),
            std::next(m_tabs.begin(), fromIndex),
            std::next(m_tabs.begin(), fromIndex + 1)
        );
    }
    emit tabsChanged();
    const auto found{std::ranges::find(m_tabs, current, &Tab::Pointer::get)};
    m_currentIndex = static_cast<qint32>(std::distance(m_tabs.begin(), found));
    emit currentChanged();
}

QVariantList TabManager::tabPanes(qint32 index) const
{
    QVariantList panes;
    if (index < 0 || index >= tabCount()) {
        return panes;
    }
    for (QObject *pane : tabAt(index)->panes()) {
        panes.emplace_back(QVariant::fromValue(pane));
    }
    return panes;
}

bool TabManager::moveTabTo(qint32 index, TabManager *target, qint32 targetIndex)
{
    if (target == nullptr || index < 0 || index >= tabCount()) {
        return false;
    }
    if (target == this) {
        // `targetIndex` is an insertion point among the tabs as they are.
        moveTab(index, targetIndex > index ? targetIndex - 1 : targetIndex);
        return true;
    }
    Tab::Pointer tab{takeTab(index)};
    const qsizetype position{std::clamp<qsizetype>(targetIndex, 0, target->tabCount())};
    target->insertTab(std::move(tab), position);
    target->setCurrentIndex(static_cast<qint32>(position));
    if (m_tabs.empty()) {
        emit emptied();
    }
    return true;
}

bool TabManager::movePane(QObject *pane, TabManager *target, QObject *targetPane, qint32 edge)
{
    if (target == nullptr || pane == targetPane || tabOf(pane) < 0 || target->tabOf(targetPane) < 0) {
        return false;
    }
    PaneNode::Pointer leaf{extractPane(pane, false)};
    target
        ->insertBeside(targetPane, std::move(leaf), static_cast<Edge>(std::clamp<qint32>(edge, EdgeLeft, EdgeBottom)));
    if (m_tabs.empty()) {
        emit emptied();
    }
    return true;
}

bool TabManager::movePaneToTab(QObject *pane, TabManager *target, qint32 index)
{
    const qint32 tabIndex{tabOf(pane)};
    if (target == nullptr || tabIndex < 0) {
        return false;
    }
    // A tab's only pane moves the tab.
    if (tabAt(tabIndex)->paneCount() == 1) {
        return moveTabTo(tabIndex, target, index);
    }
    PaneNode::Pointer leaf{extractPane(pane, false)};
    const qsizetype position{std::clamp<qsizetype>(index, 0, target->tabCount())};
    target->insertTab(std::make_unique<Tab>(std::move(leaf)), position);
    target->setCurrentIndex(static_cast<qint32>(position));
    return true;
}

void TabManager::insertTab(Tab::Pointer tab, qsizetype index)
{
    Tab *const inserted{tab.get()};
    const Tab *const current{currentTab()};
    m_tabs.insert(std::next(m_tabs.begin(), index), std::move(tab));
    connectTab(inserted);
    if (current != nullptr) {
        const auto found{std::ranges::find(m_tabs, current, &Tab::Pointer::get)};
        m_currentIndex = static_cast<qint32>(std::distance(m_tabs.begin(), found));
    }
    emit tabsChanged();
    emit currentChanged();
    structureChanged();
}

Tab::Pointer TabManager::takeTab(qsizetype index)
{
    Tab::Pointer tab{std::move(*std::next(m_tabs.begin(), index))};
    m_tabs.erase(std::next(m_tabs.begin(), index));
    disconnect(tab.get(), nullptr, this, nullptr);
    if (index < m_currentIndex || m_currentIndex >= tabCount()) {
        --m_currentIndex;
    }
    if (m_currentIndex < 0 && !m_tabs.empty()) {
        m_currentIndex = 0;
    }
    emit tabsChanged();
    emit currentChanged();
    emit activePaneChanged();
    structureChanged();
    return tab;
}

void TabManager::connectTab(Tab *tab)
{
    connect(tab, &Tab::activePaneChanged, this, [this, tab] {
        if (tab == currentTab()) {
            emit activePaneChanged();
        }
    });
}

void TabManager::structureChanged()
{
    emit panesChanged();
}

QJsonObject TabManager::state() const
{
    QJsonArray tabs;
    for (const Tab::Pointer &tab : m_tabs) {
        QJsonArray nodes;
        for (const PaneNode *node : postorder(tab->root())) {
            if (node->leaf()) {
                const QUrl file{fileOf(node->pane())};
                nodes.append(QJsonObject{{fileKey, file.isLocalFile() ? file.toString() : QString{}}});
            } else {
                nodes.append(QJsonObject{{splitKey, node->orientation()}});
            }
        }
        tabs.append(QJsonObject{{panesKey, nodes}});
    }
    return {{tabsKey, tabs}, {currentKey, m_currentIndex}};
}

void TabManager::restore(const QJsonObject &state)
{
    const qsizetype first{tabCount()};
    for (const auto &tab : state.value(tabsKey).toArray()) {
        PaneNode::Pointer root{buildTree(tab.toObject().value(panesKey).toArray())};
        if (root != nullptr) {
            insertTab(std::make_unique<Tab>(std::move(root)), tabCount());
        }
    }
    if (tabCount() > first) {
        setCurrentIndex(static_cast<qint32>(first) + state.value(currentKey).toInt());
    }
}

PaneNode::Pointer TabManager::buildTree(const QJsonArray &nodes)
{
    // Children come before their split; panes without a file (untitled,
    // deleted since) are left out.
    std::vector<PaneNode::Pointer> stack;
    for (const auto &value : nodes) {
        const QJsonObject node{value.toObject()};
        if (!node.contains(splitKey)) {
            const QUrl file{node.value(fileKey).toString()};
            const bool exists{file.isLocalFile() && QFileInfo::exists(file.toLocalFile())};
            PaneNode::PanePointer pane{exists ? createPane({{initialFileProperty, file}}) : nullptr};
            stack.emplace_back(pane != nullptr ? std::make_unique<PaneNode>(std::move(pane)) : nullptr);
            continue;
        }
        if (stack.size() < 2) {
            return nullptr;
        }
        PaneNode::Pointer second{std::move(stack.back())};
        stack.pop_back();
        PaneNode::Pointer firstNode{std::move(stack.back())};
        stack.pop_back();
        if (firstNode == nullptr || second == nullptr) {
            stack.emplace_back(firstNode != nullptr ? std::move(firstNode) : std::move(second));
            continue;
        }
        const auto orientation{node.value(splitKey).toInt() == Qt::Vertical ? Qt::Vertical : Qt::Horizontal};
        stack.emplace_back(std::make_unique<PaneNode>(orientation, std::move(firstNode), std::move(second)));
    }
    return stack.size() == 1 ? std::move(stack.front()) : nullptr;
}

} // namespace tekst
