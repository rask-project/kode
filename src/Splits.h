#pragma once

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace tekst
{

// Deleted on the next event loop turn: a view may go from its own signal.
struct DeleteLater {
    void operator()(QObject *object) const { object->deleteLater(); }
};

// A node of a window's split tree: a view (leaf) or two halves.
class SplitNode : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by Splits")
    Q_PROPERTY(bool leaf READ leaf CONSTANT)
    // Qt::Horizontal: side by side.
    Q_PROPERTY(qint32 orientation READ orientation CONSTANT)
    Q_PROPERTY(tekst::SplitNode *first READ first NOTIFY changed)
    Q_PROPERTY(tekst::SplitNode *second READ second NOTIFY changed)
    Q_PROPERTY(QObject *view READ view CONSTANT)
public:
    using ViewPointer = std::unique_ptr<QObject, DeleteLater>;
    using Pointer = std::unique_ptr<SplitNode>;

    explicit SplitNode(ViewPointer view);
    SplitNode(Qt::Orientation orientation, Pointer first, Pointer second);

    [[nodiscard]] bool leaf() const noexcept { return m_view != nullptr; }
    [[nodiscard]] qint32 orientation() const noexcept { return static_cast<qint32>(m_orientation); }
    [[nodiscard]] SplitNode *first() const noexcept { return m_first.get(); }
    [[nodiscard]] SplitNode *second() const noexcept { return m_second.get(); }
    [[nodiscard]] QObject *view() const noexcept { return m_view.get(); }

signals:
    void changed();

private:
    friend class Splits;

    Pointer m_first;
    Pointer m_second;
    ViewPointer m_view;
    // qint64 for the layout (-Wpadded).
    qint64 m_orientation{Qt::Horizontal};
};

// A window's editor splits, as in Qt Creator: a binary tree of views (made
// from `viewComponent`, Tekst's EditorPane), each showing one of the open
// documents. Split a view, remove a split; one view is active. A view's
// `fileUrl` property is what state() saves.
class Splits : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Tekst.EditorPane when not set.
    Q_PROPERTY(QQmlComponent *viewComponent READ viewComponent WRITE setViewComponent NOTIFY viewComponentChanged)
    Q_PROPERTY(tekst::SplitNode *root READ root NOTIFY rootChanged)
    Q_PROPERTY(QObject *activeView READ activeView NOTIFY activeViewChanged)
    // In layout order: left to right, top to bottom.
    Q_PROPERTY(QVariantList views READ views NOTIFY rootChanged)
    Q_PROPERTY(qint32 count READ count NOTIFY rootChanged)
public:
    explicit Splits(QObject *parent = nullptr);
    ~Splits() override;

    [[nodiscard]] QQmlComponent *viewComponent() const { return m_viewComponent; }
    void setViewComponent(QQmlComponent *component);
    [[nodiscard]] SplitNode *root() const noexcept { return m_root.get(); }
    [[nodiscard]] QObject *activeView() const { return m_activeView; }
    [[nodiscard]] QVariantList views() const;
    [[nodiscard]] qint32 count() const;

    // The first view, made with `properties`, when there is none yet.
    Q_INVOKABLE QObject *addView(const QVariantMap &properties = {});
    // A new view (made with `properties`) beside `view` (Qt::Horizontal:
    // on its right), made active; returns it.
    Q_INVOKABLE QObject *split(QObject *view, qint32 orientation, const QVariantMap &properties = {});
    // Removes `view`'s split; the last view stays. False when it did.
    Q_INVOKABLE bool remove(QObject *view);
    Q_INVOKABLE void activate(QObject *view);
    // The next (1) or previous (-1) view, wrapping around.
    Q_INVOKABLE void activateAdjacent(qint32 step);

    // The tree with each view's file, children before their split.
    Q_INVOKABLE [[nodiscard]] QJsonArray state() const;
    // Builds the tree of `state` (views made with `initialFile`) when there
    // is none yet; false when nothing could be built.
    Q_INVOKABLE bool restore(const QJsonArray &state);

signals:
    void viewComponentChanged();
    void rootChanged();
    void activeViewChanged();

private:
    [[nodiscard]] SplitNode::ViewPointer createView(const QVariantMap &properties);
    // The slot holding `node`: the root or a split's half.
    [[nodiscard]] SplitNode::Pointer &slotOf(const SplitNode *node);
    void setActiveView(QObject *view);
    void treeChanged();

    QPointer<QQmlComponent> m_viewComponent;
    std::unique_ptr<QQmlComponent> m_editorPane;
    SplitNode::Pointer m_root;
    QPointer<QObject> m_activeView;
};

} // namespace tekst
