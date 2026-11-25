#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <vector>

namespace tekst
{

// Deleted on the next event loop turn: a pane may close from its own signal.
struct DeleteLater {
    void operator()(QObject *object) const { object->deleteLater(); }
};

// A window's tabs, each a binary tree of split panes, like Konsoll's
// workspaces. A pane is an object made from `paneComponent` (EditorPane) and
// owned by its leaf; panes and whole tabs move between windows' managers
// with their documents untouched. Panes are read through their `title`,
// `modified` and `fileUrl` properties.
class PaneNode : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by TabManager")
    Q_PROPERTY(bool leaf READ leaf CONSTANT)
    // Qt::Horizontal: side by side.
    Q_PROPERTY(qint32 orientation READ orientation CONSTANT)
    Q_PROPERTY(tekst::PaneNode *first READ first NOTIFY changed)
    Q_PROPERTY(tekst::PaneNode *second READ second NOTIFY changed)
    Q_PROPERTY(QObject *pane READ pane CONSTANT)
public:
    using PanePointer = std::unique_ptr<QObject, DeleteLater>;
    using Pointer = std::unique_ptr<PaneNode>;

    explicit PaneNode(PanePointer pane);
    PaneNode(Qt::Orientation orientation, Pointer first, Pointer second);

    [[nodiscard]] bool leaf() const noexcept { return m_pane != nullptr; }
    [[nodiscard]] qint32 orientation() const noexcept { return static_cast<qint32>(m_orientation); }
    [[nodiscard]] PaneNode *first() const noexcept { return m_first.get(); }
    [[nodiscard]] PaneNode *second() const noexcept { return m_second.get(); }
    [[nodiscard]] QObject *pane() const noexcept { return m_pane.get(); }

signals:
    void changed();

private:
    friend class Tab;
    friend class TabManager;

    Pointer m_first;
    Pointer m_second;
    PanePointer m_pane;
    // qint64 for the layout (-Wpadded).
    qint64 m_orientation{Qt::Horizontal};
};

class Tab : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made by TabManager")
    Q_PROPERTY(tekst::PaneNode *root READ root NOTIFY rootChanged)
    Q_PROPERTY(QObject *activePane READ activePane NOTIFY activePaneChanged)
    Q_PROPERTY(qint32 paneCount READ paneCount NOTIFY rootChanged)
    // The active pane's title and path; modified when any pane is.
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString toolTip READ toolTip NOTIFY stateChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
public:
    using Pointer = std::unique_ptr<Tab>;

    explicit Tab(PaneNode::Pointer root);

    [[nodiscard]] PaneNode *root() const noexcept { return m_root.get(); }
    [[nodiscard]] QObject *activePane() const noexcept { return m_activePane; }
    [[nodiscard]] qint32 paneCount() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString toolTip() const;
    [[nodiscard]] bool modified() const;
    // In layout order: left to right, top to bottom.
    [[nodiscard]] QList<QObject *> panes() const;

signals:
    void rootChanged();
    void activePaneChanged();
    void stateChanged();

private:
    friend class TabManager;

    void setActivePane(QObject *pane);
    // After a change to the tree: QML rebuilds the layout, the state follows the panes.
    void treeChanged();

    PaneNode::Pointer m_root;
    QPointer<QObject> m_activePane;
    QList<QMetaObject::Connection> m_paneConnections;
};

class TabManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Makes the panes, in the engine's root context (a pane outlives the
    // window it was made in); Tekst's EditorPane when not set.
    Q_PROPERTY(QQmlComponent *paneComponent READ paneComponent WRITE setPaneComponent NOTIFY paneComponentChanged)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(qint32 currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    Q_PROPERTY(tekst::Tab *currentTab READ currentTab NOTIFY currentChanged)
    Q_PROPERTY(QObject *activePane READ activePane NOTIFY activePaneChanged)
    // Every pane of every tab.
    Q_PROPERTY(QVariantList panes READ panes NOTIFY panesChanged)
    Q_PROPERTY(qint32 paneCount READ paneCount NOTIFY panesChanged)
public:
    // The side of a target pane a moved pane lands on.
    enum Edge : quint8 {
        EdgeLeft,
        EdgeTop,
        EdgeRight,
        EdgeBottom,
    };
    Q_ENUM(Edge)

    explicit TabManager(QObject *parent = nullptr);
    ~TabManager() override;

    [[nodiscard]] QQmlComponent *paneComponent() const { return m_paneComponent; }
    void setPaneComponent(QQmlComponent *component);
    [[nodiscard]] QVariantList tabs() const;
    [[nodiscard]] qint32 currentIndex() const noexcept { return static_cast<qint32>(m_currentIndex); }
    void setCurrentIndex(qint32 index);
    [[nodiscard]] Tab *currentTab() const;
    [[nodiscard]] QObject *activePane() const;
    [[nodiscard]] QVariantList panes() const;
    [[nodiscard]] qint32 paneCount() const;

    // A new tab after the others, made current, with a pane made with
    // `properties`; returns the pane (null if it could not be made).
    Q_INVOKABLE QObject *addTab(const QVariantMap &properties = {});
    // A new pane beside `pane` (Qt::Horizontal: on its right), made active.
    Q_INVOKABLE QObject *split(QObject *pane, qint32 orientation);
    // Removes `pane`; a tab left without panes goes, and a manager left
    // without tabs emits emptied().
    Q_INVOKABLE void closePane(QObject *pane);
    Q_INVOKABLE void closeTab(qint32 index);
    // The index of the tab holding `pane`, or -1.
    Q_INVOKABLE [[nodiscard]] qint32 tabOf(const QObject *pane) const;
    Q_INVOKABLE void activatePane(QObject *pane);
    // The next (1) or previous (-1) tab, wrapping around.
    Q_INVOKABLE void selectAdjacentTab(qint32 step);
    Q_INVOKABLE void moveTab(qint32 fromIndex, qint32 toIndex);
    // The panes of the tab at `index`, in layout order.
    Q_INVOKABLE [[nodiscard]] QVariantList tabPanes(qint32 index) const;
    // The tab at `index` to `target` (another window's manager) at `targetIndex`.
    Q_INVOKABLE bool moveTabTo(qint32 index, tekst::TabManager *target, qint32 targetIndex);
    // `pane` beside `targetPane` (in `target`, possibly this manager), on `edge`.
    Q_INVOKABLE bool movePane(QObject *pane, tekst::TabManager *target, QObject *targetPane, qint32 edge);
    // `pane` into a tab of its own in `target` at `index`.
    Q_INVOKABLE bool movePaneToTab(QObject *pane, tekst::TabManager *target, qint32 index);

    // The tabs and their saved panes' files, for restoring a session.
    Q_INVOKABLE [[nodiscard]] QJsonObject state() const;
    // Adds the tabs of `state` (panes made with `initialFile`).
    Q_INVOKABLE void restore(const QJsonObject &state);

signals:
    void paneComponentChanged();
    void tabsChanged();
    void currentChanged();
    void activePaneChanged();
    void panesChanged();
    void emptied();

private:
    [[nodiscard]] PaneNode::PanePointer createPane(const QVariantMap &properties);
    // Takes the leaf of `pane` out of its tab; a tab left empty is removed
    // (and `emptied()` held back while `notifyEmpty` is false).
    PaneNode::Pointer extractPane(QObject *pane, bool notifyEmpty);
    // Puts `leaf` beside `targetPane` on `edge`, made active.
    void insertBeside(QObject *targetPane, PaneNode::Pointer leaf, Edge edge);
    void insertTab(Tab::Pointer tab, qsizetype index);
    Tab::Pointer takeTab(qsizetype index);
    void removeEmptyTab(qsizetype index, bool notifyEmpty);
    void connectTab(Tab *tab);
    void structureChanged();
    [[nodiscard]] PaneNode::Pointer buildTree(const QJsonArray &nodes);
    [[nodiscard]] qsizetype tabCount() const noexcept { return static_cast<qsizetype>(m_tabs.size()); }
    [[nodiscard]] Tab *tabAt(qsizetype index) const { return std::next(m_tabs.begin(), index)->get(); }

    QPointer<QQmlComponent> m_paneComponent;
    std::unique_ptr<QQmlComponent> m_editorPane;
    // std::vector: QList needs copyable elements.
    std::vector<Tab::Pointer> m_tabs;
    // qint64 for the layout (-Wpadded).
    qint64 m_currentIndex{-1};
};

} // namespace tekst
