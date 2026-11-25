#include "Splits.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>

#include <algorithm>
#include <vector>

namespace tekst
{

namespace
{

const QString fileKey{QStringLiteral("file")};
const QString splitKey{QStringLiteral("split")};
const QString initialFileProperty{QStringLiteral("initialFile")};

// The nodes under `root`, children after their parent (first before second).
QList<SplitNode *> preorder(SplitNode *root)
{
    QList<SplitNode *> nodes;
    QList<SplitNode *> stack;
    if (root != nullptr) {
        stack.emplace_back(root);
    }
    while (!stack.isEmpty()) {
        SplitNode *const node{stack.takeLast()};
        nodes.emplace_back(node);
        if (!node->leaf()) {
            stack.emplace_back(node->second());
            stack.emplace_back(node->first());
        }
    }
    return nodes;
}

// Children before their parent: the order restore() reads.
QList<SplitNode *> postorder(SplitNode *root)
{
    QList<SplitNode *> reversed;
    QList<SplitNode *> stack;
    if (root != nullptr) {
        stack.emplace_back(root);
    }
    while (!stack.isEmpty()) {
        SplitNode *const node{stack.takeLast()};
        reversed.emplace_back(node);
        if (!node->leaf()) {
            stack.emplace_back(node->first());
            stack.emplace_back(node->second());
        }
    }
    std::ranges::reverse(reversed);
    return reversed;
}

SplitNode *leafOf(SplitNode *root, const QObject *view)
{
    const QList<SplitNode *> nodes{preorder(root)};
    const auto leaf{std::ranges::find(nodes, view, &SplitNode::view)};
    return leaf != nodes.end() ? *leaf : nullptr;
}

SplitNode *parentOf(SplitNode *root, const SplitNode *node)
{
    const QList<SplitNode *> nodes{preorder(root)};
    const auto parent{std::ranges::find_if(nodes, [node](const SplitNode *candidate) {
        return candidate->first() == node || candidate->second() == node;
    })};
    return parent != nodes.end() ? *parent : nullptr;
}

} // namespace

SplitNode::SplitNode(ViewPointer view) : m_view{std::move(view)}
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

SplitNode::SplitNode(Qt::Orientation orientation, Pointer first, Pointer second)
    : m_first{std::move(first)}
    , m_second{std::move(second)}
    , m_orientation{orientation}
{
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
}

Splits::Splits(QObject *parent) : QObject(parent) {}

Splits::~Splits() = default;

void Splits::setViewComponent(QQmlComponent *component)
{
    if (m_viewComponent == component) {
        return;
    }
    m_viewComponent = component;
    emit viewComponentChanged();
}

QVariantList Splits::views() const
{
    QVariantList views;
    for (const SplitNode *node : preorder(m_root.get())) {
        if (node->leaf()) {
            views.emplace_back(QVariant::fromValue(node->view()));
        }
    }
    return views;
}

qint32 Splits::count() const
{
    const QList<SplitNode *> nodes{preorder(m_root.get())};
    return static_cast<qint32>(std::ranges::count_if(nodes, &SplitNode::leaf));
}

SplitNode::ViewPointer Splits::createView(const QVariantMap &properties)
{
    if (m_viewComponent == nullptr && m_editorPane == nullptr && qmlEngine(this) != nullptr) {
        m_editorPane =
            std::make_unique<QQmlComponent>(qmlEngine(this), QStringLiteral("Tekst"), QStringLiteral("EditorPane"));
    }
    QQmlComponent *const component{m_viewComponent != nullptr ? m_viewComponent.get() : m_editorPane.get()};
    if (component == nullptr || component->engine() == nullptr) {
        return nullptr;
    }
    // The root context: an inline Component is bound to its file's context.
    SplitNode::ViewPointer view{component->createWithInitialProperties(properties, component->engine()->rootContext())};
    if (view != nullptr) {
        QQmlEngine::setObjectOwnership(view.get(), QQmlEngine::CppOwnership);
    }
    return view;
}

SplitNode::Pointer &Splits::slotOf(const SplitNode *node)
{
    SplitNode *const parent{parentOf(m_root.get(), node)};
    if (parent == nullptr) {
        return m_root;
    }
    return parent->first() == node ? parent->m_first : parent->m_second;
}

QObject *Splits::addView(const QVariantMap &properties)
{
    if (m_root != nullptr) {
        return nullptr;
    }
    SplitNode::ViewPointer view{createView(properties)};
    if (view == nullptr) {
        return nullptr;
    }
    QObject *const created{view.get()};
    m_root = std::make_unique<SplitNode>(std::move(view));
    treeChanged();
    setActiveView(created);
    return created;
}

QObject *Splits::split(QObject *view, qint32 orientation, const QVariantMap &properties)
{
    SplitNode *const target{leafOf(m_root.get(), view)};
    if (target == nullptr) {
        return nullptr;
    }
    SplitNode::ViewPointer created{createView(properties)};
    if (created == nullptr) {
        return nullptr;
    }
    QObject *const result{created.get()};
    SplitNode::Pointer &slot{slotOf(target)};
    SplitNode::Pointer existing{std::move(slot)};
    slot = std::make_unique<SplitNode>(
        orientation == Qt::Vertical ? Qt::Vertical : Qt::Horizontal,
        std::move(existing),
        std::make_unique<SplitNode>(std::move(created))
    );
    treeChanged();
    setActiveView(result);
    return result;
}

bool Splits::remove(QObject *view)
{
    SplitNode *const leaf{leafOf(m_root.get(), view)};
    SplitNode *const parent{leaf != nullptr ? parentOf(m_root.get(), leaf) : nullptr};
    if (parent == nullptr) {
        return false;
    }
    SplitNode::Pointer &parentSlot{slotOf(parent)};
    const bool first{parent->first() == leaf};
    // Deletes the view (later) with its leaf.
    const SplitNode::Pointer removed{std::move(first ? parent->m_first : parent->m_second)};
    SplitNode::Pointer sibling{std::move(first ? parent->m_second : parent->m_first)};
    SplitNode *const kept{sibling.get()};
    // Replaces (and deletes) the split.
    parentSlot = std::move(sibling);
    treeChanged();
    // The sibling's first view takes over.
    if (m_activeView == view || m_activeView == nullptr) {
        const QList<SplitNode *> nodes{preorder(kept)};
        const auto firstLeaf{std::ranges::find_if(nodes, &SplitNode::leaf)};
        setActiveView(firstLeaf != nodes.end() ? (*firstLeaf)->view() : nullptr);
    }
    return true;
}

void Splits::activate(QObject *view)
{
    if (leafOf(m_root.get(), view) != nullptr) {
        setActiveView(view);
    }
}

void Splits::activateAdjacent(qint32 step)
{
    QList<QObject *> views;
    for (const SplitNode *node : preorder(m_root.get())) {
        if (node->leaf()) {
            views.emplace_back(node->view());
        }
    }
    const qsizetype count{views.size()};
    if (count < 2) {
        return;
    }
    const qsizetype current{views.indexOf(m_activeView.get())};
    setActiveView(views.at((((current + step) % count) + count) % count));
}

void Splits::setActiveView(QObject *view)
{
    if (m_activeView == view) {
        return;
    }
    m_activeView = view;
    emit activeViewChanged();
}

void Splits::treeChanged()
{
    emit rootChanged();
    for (SplitNode *node : preorder(m_root.get())) {
        if (!node->leaf()) {
            emit node->changed();
        }
    }
}

QJsonArray Splits::state() const
{
    QJsonArray nodes;
    for (const SplitNode *node : postorder(m_root.get())) {
        if (node->leaf()) {
            const QUrl file{node->view()->property("fileUrl").toUrl()};
            nodes.append(QJsonObject{{fileKey, file.isLocalFile() ? file.toString() : QString{}}});
        } else {
            nodes.append(QJsonObject{{splitKey, node->orientation()}});
        }
    }
    return nodes;
}

bool Splits::restore(const QJsonArray &state)
{
    if (m_root != nullptr) {
        return false;
    }
    // A view without a file (untitled, deleted since) shows whatever the
    // app gives it; a split keeps both halves.
    std::vector<SplitNode::Pointer> stack;
    for (const auto &value : state) {
        const QJsonObject node{value.toObject()};
        if (!node.contains(splitKey)) {
            const QUrl file{node.value(fileKey).toString()};
            const bool exists{file.isLocalFile() && QFileInfo::exists(file.toLocalFile())};
            SplitNode::ViewPointer view{createView(exists ? QVariantMap{{initialFileProperty, file}} : QVariantMap{})};
            if (view == nullptr) {
                return false;
            }
            stack.emplace_back(std::make_unique<SplitNode>(std::move(view)));
            continue;
        }
        if (stack.size() < 2) {
            return false;
        }
        SplitNode::Pointer second{std::move(stack.back())};
        stack.pop_back();
        SplitNode::Pointer firstNode{std::move(stack.back())};
        stack.pop_back();
        const auto orientation{node.value(splitKey).toInt() == Qt::Vertical ? Qt::Vertical : Qt::Horizontal};
        stack.emplace_back(std::make_unique<SplitNode>(orientation, std::move(firstNode), std::move(second)));
    }
    if (stack.size() != 1) {
        return false;
    }
    m_root = std::move(stack.front());
    treeChanged();
    const QList<SplitNode *> nodes{preorder(m_root.get())};
    const auto firstLeaf{std::ranges::find_if(nodes, &SplitNode::leaf)};
    setActiveView((*firstLeaf)->view());
    return true;
}

} // namespace tekst
