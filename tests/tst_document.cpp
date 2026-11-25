#include "tst_document.h"

#include "Document.h"
#include "EditorView.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>

namespace
{

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

QByteArray readFile(const QString &path)
{
    QFile file{path};
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

} // namespace

void TestDocument::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

QString TestDocument::path(const QString &name) const
{
    return m_dir.filePath(name);
}

void TestDocument::roundTripsUtf8()
{
    const QByteArray bytes{"olá, mundo — ℝ ☃ 日本語\n"};
    writeFile(path(QStringLiteral("utf8.txt")), bytes);
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("utf8.txt")))));
    QCOMPARE(editor.text(), QString::fromUtf8(bytes));
    QVERIFY(document.saveAs(QUrl::fromLocalFile(path(QStringLiteral("utf8-copy.txt")))));
    QCOMPARE(readFile(path(QStringLiteral("utf8-copy.txt"))), bytes);
}

void TestDocument::keepsCrlf()
{
    const QByteArray bytes{"one\r\ntwo\r\n\r\nthree"};
    writeFile(path(QStringLiteral("crlf.txt")), bytes);
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("crlf.txt")))));
    // The editor sees plain newlines.
    QCOMPARE(editor.text(), QStringLiteral("one\ntwo\n\nthree"));
    QVERIFY(document.saveAs(QUrl::fromLocalFile(path(QStringLiteral("crlf-copy.txt")))));
    QCOMPARE(readFile(path(QStringLiteral("crlf-copy.txt"))), bytes);
}

void TestDocument::keepsLfAndFinalNewline()
{
    const QByteArray bytes{"a\nb\n"};
    writeFile(path(QStringLiteral("lf.txt")), bytes);
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("lf.txt")))));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("lf.txt"))), bytes);
}

void TestDocument::keepsNonBreakingSpaces()
{
    const QByteArray bytes{"a\xc2\xa0"
                           "b\n"};
    writeFile(path(QStringLiteral("nbsp.txt")), bytes);
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("nbsp.txt")))));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("nbsp.txt"))), bytes);
}

void TestDocument::reportsMissingFile()
{
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    const QSignalSpy errors{&document, &tekst::Document::errorOccurred};
    QVERIFY(!document.load(QUrl::fromLocalFile(path(QStringLiteral("missing.txt")))));
    QCOMPARE(errors.count(), 1);
    QVERIFY(document.fileUrl().isEmpty());
}

void TestDocument::readsWindows1252WhenNotUtf8()
{
    const QByteArray bytes{"caf\xe9 \x80\n"};
    writeFile(path(QStringLiteral("cp1252.txt")), bytes);
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("cp1252.txt")))));
    QCOMPARE(document.encoding(), QStringLiteral("Windows-1252"));
    QCOMPARE(editor.text(), QStringLiteral("café €\n"));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("cp1252.txt"))), bytes);
}

void TestDocument::keepsTheByteOrderMark()
{
    const QByteArray bytes{"\xEF\xBB\xBF"
                           "bom\n"};
    writeFile(path(QStringLiteral("bom.txt")), bytes);
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("bom.txt")))));
    QVERIFY(document.byteOrderMark());
    QCOMPARE(editor.text(), QStringLiteral("bom\n"));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("bom.txt"))), bytes);
}

void TestDocument::reopensWithAnotherEncoding()
{
    writeFile(path(QStringLiteral("utf8-as-latin1.txt")), QByteArray{"caf\xC3\xA9"});
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("utf8-as-latin1.txt")))));
    QCOMPARE(editor.text(), QStringLiteral("café"));
    QVERIFY(document.reopen(QStringLiteral("ISO-8859-1")));
    QCOMPARE(document.encoding(), QStringLiteral("ISO-8859-1"));
    QCOMPARE(editor.text(), QStringLiteral("cafÃ©"));
}

void TestDocument::convertsEncodingAndLineEnding()
{
    writeFile(path(QStringLiteral("convert.txt")), QByteArray{"a\xC3\xA9\nb\n"});
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("convert.txt")))));
    document.setEncoding(QStringLiteral("UTF-16LE"));
    document.setCrlf(true);
    QVERIFY(document.modified());
    QVERIFY(document.save());
    QVERIFY(!document.modified());
    QCOMPARE(readFile(path(QStringLiteral("convert.txt"))), QByteArray("a\0\xE9\0\r\0\n\0b\0\r\0\n\0", 14));
}

void TestDocument::refusesCharactersTheEncodingLacks()
{
    writeFile(path(QStringLiteral("lacks.txt")), QByteArray{"\xE2\x82\xAC\n"});
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("lacks.txt")))));
    document.setEncoding(QStringLiteral("ISO-8859-1"));
    const QSignalSpy errors{&document, &tekst::Document::errorOccurred};
    QVERIFY(!document.save());
    QCOMPARE(errors.count(), 1);
    QCOMPARE(readFile(path(QStringLiteral("lacks.txt"))), QByteArray{"\xE2\x82\xAC\n"});
}

void TestDocument::tracksModified()
{
    writeFile(path(QStringLiteral("edit.txt")), QByteArray{"text\n"});
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("edit.txt")))));
    QVERIFY(!document.modified());

    const QSignalSpy modifiedSpy{&document, &tekst::Document::modifiedChanged};
    editor.setCursorPosition(editor.length());
    editor.insert(QStringLiteral("more\n"));
    QVERIFY(document.modified());
    QVERIFY(!modifiedSpy.isEmpty());

    QVERIFY(document.save());
    QVERIFY(!document.modified());
    QCOMPARE(readFile(path(QStringLiteral("edit.txt"))), QByteArray{"text\nmore\n"});
}

void TestDocument::clearsToUntitled()
{
    writeFile(path(QStringLiteral("clear.txt")), QByteArray{"text\n"});
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("clear.txt")))));
    document.clear();
    QVERIFY(document.fileUrl().isEmpty());
    QCOMPARE(document.fileName(), QStringLiteral("Untitled"));
    QVERIFY(editor.text().isEmpty());
    const QSignalSpy errors{&document, &tekst::Document::errorOccurred};
    QVERIFY(!document.save());
    QCOMPARE(errors.count(), 1);
}

void TestDocument::reloadsAChangeOnDisk()
{
    const QString file{path(QStringLiteral("watched.txt"))};
    writeFile(file, QByteArray{"first\nsecond\n"});
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(file)));
    editor.setCursorPosition(6);
    writeFile(file, QByteArray{"first\nchanged elsewhere\n"});
    QVERIFY(QTest::qWaitFor([&editor] { return editor.text().contains(QStringLiteral("elsewhere")); }));
    QVERIFY(!document.modified());
    QCOMPARE(editor.cursorPosition(), 6);
}

void TestDocument::reportsAChangeOnDiskWhenModified()
{
    const QString file{path(QStringLiteral("conflict.txt"))};
    writeFile(file, QByteArray{"mine\n"});
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(file)));
    editor.insert(QStringLiteral("typed "));
    QSignalSpy changed{&document, &tekst::Document::changedOnDisk};
    writeFile(file, QByteArray{"theirs, longer\n"});
    QVERIFY(changed.wait());
    QCOMPARE(editor.text(), QStringLiteral("typed mine\n"));
    QVERIFY(document.reload());
    QCOMPARE(editor.text(), QStringLiteral("theirs, longer\n"));
}

void TestDocument::ignoresItsOwnSaves()
{
    const QString file{path(QStringLiteral("own.txt"))};
    writeFile(file, QByteArray{"text\n"});
    tekst::EditorView editor;
    tekst::Document document;
    document.setEditor(&editor);
    QVERIFY(document.load(QUrl::fromLocalFile(file)));
    editor.insert(QStringLiteral("more "));
    QSignalSpy changed{&document, &tekst::Document::changedOnDisk};
    QVERIFY(document.save());
    editor.insert(QStringLiteral("again "));
    QVERIFY(!changed.wait(500));
    QCOMPARE(editor.text(), QStringLiteral("more again text\n"));
}

QTEST_MAIN(TestDocument)
