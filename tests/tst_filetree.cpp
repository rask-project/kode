#include "tst_filetree.h"

#include "FileKinds.h"
#include "FileOperations.h"
#include "FileSearch.h"
#include "FileTree.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

namespace
{

QStringList names(const kode::FileTree &tree)
{
    QStringList list;
    const QModelIndex root{tree.rootIndex()};
    for (qint32 row{0}; row < tree.rowCount(root); ++row) {
        list.append(tree.index(row, 0, root).data(Qt::DisplayRole).toString());
    }
    return list;
}

} // namespace

void TestFileTree::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const QDir dir{m_dir.path()};
    QVERIFY(dir.mkdir(QStringLiteral("src")));
    QVERIFY(dir.mkdir(QStringLiteral("Assets")));
    for (const QString &name :
         {QStringLiteral("file10.txt"),
          QStringLiteral("file9.txt"),
          QStringLiteral("README.md"),
          QStringLiteral(".hidden")}) {
        QFile file{dir.filePath(name)};
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
}

void TestFileTree::sortsFoldersFirstByName()
{
    kode::FileTree tree;
    tree.setFolder(QUrl::fromLocalFile(m_dir.path()));
    QVERIFY(tree.rootIndex().isValid());
    QCOMPARE(tree.columnCount(tree.rootIndex()), 1);
    QTRY_COMPARE(
        names(tree),
        (QStringList{
            QStringLiteral("Assets"),
            QStringLiteral("src"),
            QStringLiteral("file9.txt"),
            QStringLiteral("file10.txt"),
            QStringLiteral("README.md")
        })
    );
    const QModelIndex first{tree.index(0, 0, tree.rootIndex())};
    QVERIFY(first.data(kode::FileTree::IsFolderRole).toBool());
    QCOMPARE(
        first.data(kode::FileTree::FileUrlRole).toUrl(),
        QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("Assets")))
    );
    QVERIFY(tree.contains(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("src/x.cpp")))));
    QVERIFY(!tree.contains(QUrl::fromLocalFile(m_dir.path() + QStringLiteral("-other/x.cpp"))));
    QCOMPARE(tree.indexOf(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("Assets")))), first);
    QVERIFY(first.data(kode::FileTree::IconSourceRole).toString().startsWith(QStringLiteral("image://fileicon/")));
    const QModelIndex readme{tree.index(4, 0, tree.rootIndex())};
    QCOMPARE(readme.data(kode::FileTree::IconSourceRole).toString(), QStringLiteral("image://codeicon/md"));
}

void TestFileTree::showsHiddenFilesOnRequest()
{
    kode::FileTree tree;
    tree.setFolder(QUrl::fromLocalFile(m_dir.path()));
    QTRY_COMPARE(names(tree).size(), 5);
    tree.setShowHidden(true);
    QTRY_VERIFY(names(tree).contains(QStringLiteral(".hidden")));
    tree.setShowHidden(false);
    QTRY_VERIFY(!names(tree).contains(QStringLiteral(".hidden")));
}

void TestFileTree::dropsTheTrailingSlash()
{
    kode::FileTree tree;
    tree.setFolder(QUrl::fromLocalFile(m_dir.path() + QLatin1Char('/')));
    QCOMPARE(tree.folder(), QUrl::fromLocalFile(m_dir.path()));
    QVERIFY(tree.contains(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("src/x.cpp")))));
    QCOMPARE(tree.parentFolder(), QUrl::fromLocalFile(QFileInfo{m_dir.path()}.absolutePath()));
    tree.setFolder(QUrl::fromLocalFile(QStringLiteral("/")));
    QVERIFY(tree.parentFolder().isEmpty());
}

void TestFileTree::createsUntitledEntries()
{
    const QTemporaryDir dir;
    QCOMPARE(QFileInfo{*kode::files::createFile(dir.path())}.fileName(), QStringLiteral("untitled"));
    QCOMPARE(QFileInfo{*kode::files::createFile(dir.path())}.fileName(), QStringLiteral("untitled 2"));
    const QString folder{*kode::files::createFolder(dir.path())};
    QVERIFY(QFileInfo{folder}.isDir());
    QCOMPARE(QFileInfo{folder}.fileName(), QStringLiteral("untitled folder"));
}

void TestFileTree::renamesUnlessTaken()
{
    const QTemporaryDir dir;
    const QString notes{*kode::files::createFile(dir.path())};
    const std::expected<QString, QString> renamed{kode::files::rename(notes, QStringLiteral("notes.txt"))};
    QVERIFY(renamed);
    QCOMPARE(*renamed, QDir{dir.path()}.filePath(QStringLiteral("notes.txt")));
    const QString other{*kode::files::createFile(dir.path())};
    QVERIFY(!kode::files::rename(other, QStringLiteral("notes.txt")));
    QVERIFY(!kode::files::rename(other, QStringLiteral("a/b")));
    QVERIFY(!kode::files::rename(other, QString{}));
    QVERIFY(QFileInfo::exists(other));
}

void TestFileTree::copiesAndDuplicates()
{
    const QTemporaryDir dir;
    const QDir root{dir.path()};
    QVERIFY(root.mkpath(QStringLiteral("src/deep")));
    QFile file{root.filePath(QStringLiteral("src/deep/a.cpp"))};
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();
    QCOMPARE(
        QFileInfo{*kode::files::copyInto(file.fileName(), root.filePath(QStringLiteral("src/deep")))}.fileName(),
        QStringLiteral("a copy.cpp")
    );
    QCOMPARE(
        QFileInfo{*kode::files::copyInto(file.fileName(), root.filePath(QStringLiteral("src/deep")))}.fileName(),
        QStringLiteral("a copy 2.cpp")
    );
    const std::expected<QString, QString> folder{kode::files::copyInto(root.filePath(QStringLiteral("src")), dir.path())
    };
    QVERIFY(folder);
    QCOMPARE(QFileInfo{*folder}.fileName(), QStringLiteral("src copy"));
    QVERIFY(QFileInfo::exists(QDir{*folder}.filePath(QStringLiteral("deep/a.cpp"))));
    QVERIFY(!kode::files::copyInto(root.filePath(QStringLiteral("src")), root.filePath(QStringLiteral("src/deep"))));
}

void TestFileTree::movesButNotIntoItself()
{
    const QTemporaryDir dir;
    const QDir root{dir.path()};
    QVERIFY(root.mkpath(QStringLiteral("a/b")));
    QVERIFY(root.mkpath(QStringLiteral("c")));
    QVERIFY(!kode::files::moveInto(root.filePath(QStringLiteral("a")), root.filePath(QStringLiteral("a/b"))));
    const std::expected<QString, QString> moved{
        kode::files::moveInto(root.filePath(QStringLiteral("a")), root.filePath(QStringLiteral("c")))
    };
    QVERIFY(moved);
    QVERIFY(QFileInfo{root.filePath(QStringLiteral("c/a/b"))}.isDir());
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("a"))));
}

void TestFileTree::pastesACutFileByMovingIt()
{
    const QTemporaryDir dir;
    const QDir root{dir.path()};
    QVERIFY(root.mkpath(QStringLiteral("to")));
    const QUrl file{QUrl::fromLocalFile(*kode::files::createFile(dir.path()))};
    kode::FileTree tree;
    tree.setFolder(QUrl::fromLocalFile(dir.path()));
    const QSignalSpy moved{&tree, &kode::FileTree::moved};
    tree.copy(file, false);
    QVERIFY(kode::FileTree::canPaste());
    const QUrl copied{tree.paste(QUrl::fromLocalFile(root.filePath(QStringLiteral("to"))))};
    QVERIFY(QFileInfo::exists(copied.toLocalFile()));
    QVERIFY(QFileInfo::exists(file.toLocalFile()));
    QCOMPARE(moved.count(), 0);
    tree.copy(file, true);
    const QUrl pasted{tree.paste(QUrl::fromLocalFile(root.filePath(QStringLiteral("to"))))};
    QCOMPARE(pasted.fileName(), QStringLiteral("untitled 2"));
    QVERIFY(!QFileInfo::exists(file.toLocalFile()));
    QCOMPARE(moved.count(), 1);
}

void TestFileTree::knowsCodeFilesByExtensionOrName()
{
    QCOMPARE(kode::fileKind(u"main.cpp")->label, QLatin1StringView{"C++"});
    QCOMPARE(kode::fileKind(u"View.H")->key, QLatin1StringView{"h"});
    QCOMPARE(kode::fileKind(u"CMakeLists.txt")->label, QLatin1StringView{"CM"});
    QCOMPARE(kode::fileKind(u"Makefile")->label, QLatin1StringView{"MK"});
    QVERIFY(!kode::fileKind(u"notes.txt"));
    QVERIFY(!kode::fileKind(u".gitignore"));
    QVERIFY(!kode::fileKind(u"README"));
    QCOMPARE(kode::fileKindByKey(u"cpp")->label, QLatin1StringView{"C++"});
    QCOMPARE(kode::fileKindByKey(u"CM")->color, kode::fileKind(u"x.cmake")->color);
}

void TestFileTree::scoresFileNames()
{
    using kode::FileSearch;
    QVERIFY(FileSearch::score(u"main.cpp", u"main") < FileSearch::score(u"domain.cpp", u"main"));
    QVERIFY(FileSearch::score(u"domain.cpp", u"main") < FileSearch::score(u"EditorView.cpp", u"edv"));
    // Word starts: E and V of EditorView before letters scattered in a word.
    QVERIFY(FileSearch::score(u"EditorView.cpp", u"ev") < FileSearch::score(u"beaver.cpp", u"ev"));
    QCOMPARE(FileSearch::score(u"main.cpp", u"xyz"), -1);
    QCOMPARE(FileSearch::matchedLetters(u"EditorView.cpp", u"ev"), (QList<qsizetype>{0, 6}));
    QCOMPARE(FileSearch::matchedLetters(u"domain.cpp", u"main"), (QList<qsizetype>{2, 3, 4, 5}));
}

void TestFileTree::listsFilesForSearch()
{
    const QTemporaryDir folder;
    const QDir root{folder.path()};
    for (const QString &path : {u"src/a.cpp"_s, u"build/b.cpp"_s, u".git/c"_s, u"src/deep/d.h"_s}) {
        QVERIFY(root.mkpath(QFileInfo{root.filePath(path)}.path()));
        QFile file{root.filePath(path)};
        QVERIFY(file.open(QIODevice::WriteOnly));
    }
    QVERIFY(QFile::link(root.filePath(u"src"_s), root.filePath(u"loop"_s)));
    QStringList files{kode::FileSearch::listFiles(folder.path(), true, 100)};
    files.sort();
    QCOMPARE(files, (QStringList{u"src/a.cpp"_s, u"src/deep/d.h"_s}));
}

QTEST_MAIN(TestFileTree)
