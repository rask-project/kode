#pragma once

#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>

#include <memory>

// TabManager with stand-in panes (QtObjects with the properties it reads):
// tabs, splits, closing, moving panes and tabs between managers, the session.
class TestTabManager : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void addsTabsAndFollowsThePanes();
    void tabsFollowTheirPanes();
    void splitsPanes();
    void closesPanes();
    void closingTheLastPaneEmpties();
    void movesPanesBesideOthers();
    void movesPanesWithinAManager();
    void movesTabsBetweenManagers();
    void movesAPaneIntoATabOfItsOwn();
    void reordersTabs();
    void savesAndRestoresTheLayout();

private:
    QQmlEngine m_engine;
    QTemporaryDir m_files;
    // After the engine: destroyed before it.
    std::unique_ptr<QQmlComponent> m_paneComponent;
    QQmlComponent *m_pane{nullptr};
};
