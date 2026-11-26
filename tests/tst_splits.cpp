#include "tst_splits.h"

#include "DocumentHistory.h"
#include "OpenDocuments.h"
#include "Splits.h"

#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTest>

namespace
{

QList<QObject *> viewsOf(const kode::Splits &splits)
{
    QList<QObject *> views;
    for (const QVariant &view : splits.views()) {
        views.emplace_back(view.value<QObject *>());
    }
    return views;
}

QUrl createFile(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes = {})
{
    QFile file{dir.filePath(name)};
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(bytes);
    return QUrl::fromLocalFile(file.fileName());
}

} // namespace

void TestSplits::initTestCase()
{
    QVERIFY(m_files.isValid());
    m_view = std::make_unique<QQmlComponent>(&m_engine);
    m_view->setData(
        "import QtQuick\nQtObject { property url initialFile; property url fileUrl: initialFile }",
        QUrl{QStringLiteral("qrc:/view.qml")}
    );
    QVERIFY2(m_view->isReady(), qPrintable(m_view->errorString()));
}

void TestSplits::splitsViews()
{
    kode::Splits splits;
    splits.setViewComponent(m_view.get());
    QObject *const first{splits.addView()};
    QVERIFY(first != nullptr);
    QCOMPARE(splits.addView(), nullptr);
    QObject *const right{splits.split(first, Qt::Horizontal)};
    QObject *const below{splits.split(right, Qt::Vertical)};
    QCOMPARE(viewsOf(splits), (QList<QObject *>{first, right, below}));
    QCOMPARE(splits.activeView(), below);
    const kode::SplitNode *const root{splits.root()};
    QCOMPARE(root->orientation(), static_cast<qint32>(Qt::Horizontal));
    QCOMPARE(root->first()->view(), first);
    QCOMPARE(root->second()->orientation(), static_cast<qint32>(Qt::Vertical));
}

void TestSplits::removesSplits()
{
    kode::Splits splits;
    splits.setViewComponent(m_view.get());
    QObject *const first{splits.addView()};
    QObject *const right{splits.split(first, Qt::Horizontal)};
    QObject *const below{splits.split(right, Qt::Vertical)};
    QVERIFY(splits.remove(right));
    QCOMPARE(viewsOf(splits), (QList<QObject *>{first, below}));
    QCOMPARE(splits.activeView(), below);
    QVERIFY(splits.remove(below));
    QVERIFY(splits.root()->leaf());
    QCOMPARE(splits.activeView(), first);
    // The last view stays.
    QVERIFY(!splits.remove(first));
    QCOMPARE(splits.count(), 1);
}

void TestSplits::activatesViewsInTurn()
{
    kode::Splits splits;
    splits.setViewComponent(m_view.get());
    QObject *const first{splits.addView()};
    QObject *const second{splits.split(first, Qt::Horizontal)};
    splits.activateAdjacent(1);
    QCOMPARE(splits.activeView(), first);
    splits.activateAdjacent(-1);
    QCOMPARE(splits.activeView(), second);
}

void TestSplits::savesAndRestoresTheLayout()
{
    const QUrl one{createFile(m_files, QStringLiteral("one.txt"))};
    const QUrl gone{QUrl::fromLocalFile(m_files.filePath(QStringLiteral("gone.txt")))};
    kode::Splits saved;
    saved.setViewComponent(m_view.get());
    QObject *const top{saved.addView({{QStringLiteral("initialFile"), one}})};
    saved.split(top, Qt::Vertical, {{QStringLiteral("initialFile"), gone}});

    kode::Splits restored;
    restored.setViewComponent(m_view.get());
    QVERIFY(restored.restore(saved.state()));
    QCOMPARE(restored.root()->orientation(), static_cast<qint32>(Qt::Vertical));
    QCOMPARE(restored.root()->first()->view()->property("fileUrl").toUrl(), one);
    // A file gone since keeps its split, without the file.
    QVERIFY(restored.root()->second()->view()->property("fileUrl").toUrl().isEmpty());
    QCOMPARE(restored.activeView(), restored.root()->first()->view());
}

void TestSplits::opensEachFileOnce()
{
    const QUrl file{createFile(m_files, QStringLiteral("text.txt"), "text\n")};
    kode::OpenDocuments documents;
    kode::Document *const opened{documents.open(file)};
    QVERIFY(opened != nullptr);
    QCOMPARE(documents.open(file), opened);
    QCOMPARE(documents.count(), 1);
    QCOMPARE(opened->textDocument()->text(), QStringLiteral("text\n"));

    const QSignalSpy closing{&documents, &kode::OpenDocuments::documentClosing};
    documents.close(opened);
    QCOMPARE(closing.count(), 1);
    QCOMPARE(documents.count(), 0);
}

void TestSplits::reportsMissingFiles()
{
    kode::OpenDocuments documents;
    const QSignalSpy errors{&documents, &kode::OpenDocuments::errorOccurred};
    QCOMPARE(documents.open(QUrl::fromLocalFile(m_files.filePath(QStringLiteral("missing.txt")))), nullptr);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(documents.count(), 0);
}

void TestSplits::keepsTheMostRecentFirst()
{
    kode::OpenDocuments documents;
    kode::Document *const first{documents.create()};
    kode::Document *const second{documents.create()};
    QCOMPARE(documents.documents().first().value<kode::Document *>(), second);
    documents.touch(first);
    QCOMPARE(documents.documents().first().value<kode::Document *>(), first);
    QCOMPARE(documents.other(first), second);
    QCOMPARE(documents.other(nullptr), first);
}

void TestSplits::numbersUntitledDocuments()
{
    kode::OpenDocuments documents;
    kode::Document *const first{documents.create()};
    kode::Document *const second{documents.create()};
    QCOMPARE(first->fileName(), QStringLiteral("Untitled"));
    QCOMPARE(second->fileName(), QStringLiteral("Untitled 2"));
    documents.close(first);
    QCOMPARE(second->fileName(), QStringLiteral("Untitled"));
}

void TestSplits::goesBackAndForward()
{
    kode::OpenDocuments documents;
    kode::Document *const a{documents.create()};
    kode::Document *const b{documents.create()};
    kode::Document *const c{documents.create()};
    kode::DocumentHistory history;
    history.visit(a);
    history.visit(b);
    history.visit(b);
    history.visit(c);
    QVERIFY(history.canGoBack());
    QVERIFY(!history.canGoForward());
    QCOMPARE(history.back(), b);
    QCOMPARE(history.back(), a);
    QCOMPARE(history.back(), nullptr);
    QCOMPARE(history.forward(), b);
    QCOMPARE(history.forward(), c);
    QCOMPARE(history.forward(), nullptr);
}

void TestSplits::visitingDropsWhatWasAhead()
{
    kode::OpenDocuments documents;
    kode::Document *const a{documents.create()};
    kode::Document *const b{documents.create()};
    kode::Document *const c{documents.create()};
    kode::DocumentHistory history;
    history.visit(a);
    history.visit(b);
    history.visit(c);
    QCOMPARE(history.back(), b);
    history.visit(a);
    QVERIFY(!history.canGoForward());
    QCOMPARE(history.back(), b);
    QCOMPARE(history.back(), a);
    QVERIFY(!history.canGoBack());
}

void TestSplits::forgetsClosedDocuments()
{
    kode::OpenDocuments documents;
    kode::Document *const a{documents.create()};
    kode::Document *const b{documents.create()};
    kode::Document *const c{documents.create()};
    kode::DocumentHistory history;
    history.visit(a);
    history.visit(b);
    history.visit(a);
    history.visit(c);
    history.forget(a);
    // b, c: the two a's went, and c is still current.
    QCOMPARE(history.current(), c);
    QCOMPARE(history.back(), b);
    QVERIFY(!history.canGoBack());
    QCOMPARE(history.forward(), c);
    // A deleted document leaves no entry behind.
    documents.close(b);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!history.canGoForward());
    QCOMPARE(history.back(), nullptr);
    QCOMPARE(history.current(), c);
}

void TestSplits::keepsTheSplitsRecentDocuments()
{
    kode::OpenDocuments documents;
    kode::Document *const a{documents.create()};
    kode::Document *const b{documents.create()};
    kode::Document *const c{documents.create()};
    kode::DocumentHistory history;
    history.visit(a);
    history.visit(b);
    history.visit(c);
    history.visit(a);
    QCOMPARE(history.recent(), (QVariantList{QVariant::fromValue(a), QVariant::fromValue(c), QVariant::fromValue(b)}));
    // Going back is a use too.
    QCOMPARE(history.back(), c);
    history.visit(c);
    QCOMPARE(history.recent().constFirst(), QVariant::fromValue(c));
    history.forget(c);
    QCOMPARE(history.recent(), (QVariantList{QVariant::fromValue(a), QVariant::fromValue(b)}));
}

void TestSplits::ordersEveryOpenDocumentForTheSwitcher()
{
    kode::OpenDocuments documents;
    kode::Document *const a{documents.create()};
    kode::Document *const b{documents.create()};
    kode::Document *const c{documents.create()};
    kode::Document *const d{documents.create()};
    kode::DocumentHistory history;
    history.visit(c);
    history.visit(a);
    // The split's own first (a, c), then the rest as given (d, b).
    const QVariantList
        open{QVariant::fromValue(d), QVariant::fromValue(a), QVariant::fromValue(b), QVariant::fromValue(c)};
    QCOMPARE(
        history.recentAmong(open),
        (QVariantList{QVariant::fromValue(a), QVariant::fromValue(c), QVariant::fromValue(d), QVariant::fromValue(b)})
    );
}

QTEST_MAIN(TestSplits)
