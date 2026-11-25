#include "tst_tabmanager.h"

#include "TabManager.h"

#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

namespace
{

QList<QObject *> panesOf(const tekst::TabManager &manager)
{
    QList<QObject *> panes;
    for (const QVariant &pane : manager.panes()) {
        panes.emplace_back(pane.value<QObject *>());
    }
    return panes;
}

tekst::Tab *tabAt(const tekst::TabManager &manager, qint32 index)
{
    return manager.tabs().at(index).value<tekst::Tab *>();
}

} // namespace

void TestTabManager::initTestCase()
{
    QVERIFY(m_files.isValid());
    m_paneComponent = std::make_unique<QQmlComponent>(&m_engine);
    m_pane = m_paneComponent.get();
    m_pane->setData(
        "import QtQuick\n"
        "QtObject { property url initialFile; property url fileUrl: initialFile;"
        " property string title: \"Untitled\"; property bool modified: false }",
        QUrl{QStringLiteral("qrc:/pane.qml")}
    );
    QVERIFY2(m_pane->isReady(), qPrintable(m_pane->errorString()));
}

void TestTabManager::addsTabsAndFollowsThePanes()
{
    tekst::TabManager manager;
    manager.setPaneComponent(m_pane);
    QObject *const first{manager.addTab()};
    QObject *const second{manager.addTab()};
    QVERIFY(first != nullptr && second != nullptr);
    QCOMPARE(manager.tabs().size(), 2);
    QCOMPARE(manager.currentIndex(), 1);
    QCOMPARE(manager.activePane(), second);
    manager.activatePane(first);
    QCOMPARE(manager.currentIndex(), 0);
    manager.selectAdjacentTab(-1);
    QCOMPARE(manager.currentIndex(), 1);
}

void TestTabManager::tabsFollowTheirPanes()
{
    tekst::TabManager manager;
    manager.setPaneComponent(m_pane);
    QObject *const pane{manager.addTab()};
    tekst::Tab *const tab{tabAt(manager, 0)};
    const QSignalSpy state{tab, &tekst::Tab::stateChanged};
    pane->setProperty("title", QStringLiteral("notes.txt"));
    pane->setProperty("modified", true);
    QCOMPARE(state.count(), 2);
    QCOMPARE(tab->title(), QStringLiteral("notes.txt"));
    QVERIFY(tab->modified());
}

void TestTabManager::splitsPanes()
{
    tekst::TabManager manager;
    manager.setPaneComponent(m_pane);
    QObject *const first{manager.addTab()};
    QObject *const right{manager.split(first, Qt::Horizontal)};
    QObject *const below{manager.split(right, Qt::Vertical)};
    QCOMPARE(panesOf(manager), (QList<QObject *>{first, right, below}));
    QCOMPARE(manager.activePane(), below);
    const tekst::PaneNode *const root{tabAt(manager, 0)->root()};
    QCOMPARE(root->orientation(), static_cast<qint32>(Qt::Horizontal));
    QCOMPARE(root->first()->pane(), first);
    QCOMPARE(root->second()->orientation(), static_cast<qint32>(Qt::Vertical));
}

void TestTabManager::closesPanes()
{
    tekst::TabManager manager;
    manager.setPaneComponent(m_pane);
    QObject *const first{manager.addTab()};
    QObject *const right{manager.split(first, Qt::Horizontal)};
    QObject *const below{manager.split(right, Qt::Vertical)};
    manager.closePane(right);
    QCOMPARE(panesOf(manager), (QList<QObject *>{first, below}));
    QCOMPARE(tabAt(manager, 0)->root()->second()->pane(), below);
    manager.closePane(below);
    QVERIFY(tabAt(manager, 0)->root()->leaf());
    QCOMPARE(manager.activePane(), first);
}

void TestTabManager::closingTheLastPaneEmpties()
{
    tekst::TabManager manager;
    manager.setPaneComponent(m_pane);
    QObject *const pane{manager.addTab()};
    const QSignalSpy emptied{&manager, &tekst::TabManager::emptied};
    manager.closePane(pane);
    QCOMPARE(emptied.count(), 1);
    QCOMPARE(manager.currentIndex(), -1);
    QCOMPARE(manager.activePane(), nullptr);
}

void TestTabManager::movesPanesBesideOthers()
{
    tekst::TabManager source;
    tekst::TabManager target;
    source.setPaneComponent(m_pane);
    target.setPaneComponent(m_pane);
    QObject *const moving{source.addTab()};
    QObject *const staying{source.split(moving, Qt::Horizontal)};
    QObject *const host{target.addTab()};

    QVERIFY(source.movePane(moving, &target, host, tekst::TabManager::EdgeTop));
    QCOMPARE(panesOf(source), (QList<QObject *>{staying}));
    QCOMPARE(panesOf(target), (QList<QObject *>{moving, host}));
    QCOMPARE(tabAt(target, 0)->root()->orientation(), static_cast<qint32>(Qt::Vertical));
    QCOMPARE(target.activePane(), moving);
    // The source is emptied when its last pane leaves.
    const QSignalSpy emptied{&source, &tekst::TabManager::emptied};
    QVERIFY(source.movePane(staying, &target, host, tekst::TabManager::EdgeLeft));
    QCOMPARE(emptied.count(), 1);
}

void TestTabManager::movesPanesWithinAManager()
{
    tekst::TabManager manager;
    manager.setPaneComponent(m_pane);
    QObject *const host{manager.addTab()};
    QObject *const moving{manager.split(host, Qt::Vertical)};
    QVERIFY(manager.movePane(moving, &manager, host, tekst::TabManager::EdgeLeft));
    QCOMPARE(panesOf(manager), (QList<QObject *>{moving, host}));
    QCOMPARE(tabAt(manager, 0)->root()->orientation(), static_cast<qint32>(Qt::Horizontal));
    QVERIFY(!manager.movePane(host, &manager, host, tekst::TabManager::EdgeLeft));
}

void TestTabManager::movesTabsBetweenManagers()
{
    tekst::TabManager source;
    tekst::TabManager target;
    source.setPaneComponent(m_pane);
    target.setPaneComponent(m_pane);
    QObject *const first{source.addTab()};
    QObject *const second{source.addTab()};
    QObject *const existing{target.addTab()};

    QVERIFY(source.moveTabTo(0, &target, 0));
    QCOMPARE(panesOf(source), (QList<QObject *>{second}));
    QCOMPARE(panesOf(target), (QList<QObject *>{first, existing}));
    QCOMPARE(target.currentIndex(), 0);
    QCOMPARE(target.activePane(), first);
}

void TestTabManager::movesAPaneIntoATabOfItsOwn()
{
    tekst::TabManager manager;
    manager.setPaneComponent(m_pane);
    QObject *const left{manager.addTab()};
    QObject *const right{manager.split(left, Qt::Horizontal)};
    QVERIFY(manager.movePaneToTab(right, &manager, 0));
    QCOMPARE(manager.tabs().size(), 2);
    QCOMPARE(manager.tabPanes(0), (QVariantList{QVariant::fromValue(right)}));
    QCOMPARE(manager.tabPanes(1), (QVariantList{QVariant::fromValue(left)}));
    QCOMPARE(manager.currentIndex(), 0);
}

void TestTabManager::reordersTabs()
{
    tekst::TabManager manager;
    manager.setPaneComponent(m_pane);
    QObject *const a{manager.addTab()};
    QObject *const b{manager.addTab()};
    QObject *const c{manager.addTab()};
    manager.setCurrentIndex(0);
    manager.moveTab(0, 2);
    QCOMPARE(panesOf(manager), (QList<QObject *>{b, c, a}));
    QCOMPARE(manager.currentIndex(), 2);
    // An insertion point among the tabs as they are, as a drop gives it.
    QVERIFY(manager.moveTabTo(2, &manager, 0));
    QCOMPARE(panesOf(manager), (QList<QObject *>{a, b, c}));
}

void TestTabManager::savesAndRestoresTheLayout()
{
    const auto file{[this](const QString &name) {
        QFile created{m_files.filePath(name)};
        return created.open(QIODevice::WriteOnly) ? QUrl::fromLocalFile(created.fileName()) : QUrl{};
    }};
    const QUrl one{file(QStringLiteral("one.txt"))};
    const QUrl two{file(QStringLiteral("two.txt"))};
    const QUrl gone{QUrl::fromLocalFile(m_files.filePath(QStringLiteral("gone.txt")))};

    tekst::TabManager saved;
    saved.setPaneComponent(m_pane);
    QObject *const left{saved.addTab({{QStringLiteral("initialFile"), one}})};
    QObject *const right{saved.split(left, Qt::Vertical)};
    right->setProperty("fileUrl", two);
    saved.addTab({{QStringLiteral("initialFile"), gone}});
    saved.addTab();
    saved.setCurrentIndex(0);

    tekst::TabManager restored;
    restored.setPaneComponent(m_pane);
    restored.restore(saved.state());
    // The missing file's tab and the untitled one are left out.
    QCOMPARE(restored.tabs().size(), 1);
    const tekst::PaneNode *const root{tabAt(restored, 0)->root()};
    QCOMPARE(root->orientation(), static_cast<qint32>(Qt::Vertical));
    QCOMPARE(root->first()->pane()->property("fileUrl").toUrl(), one);
    QCOMPARE(root->second()->pane()->property("fileUrl").toUrl(), two);
    QCOMPARE(restored.currentIndex(), 0);
}

QTEST_MAIN(TestTabManager)
