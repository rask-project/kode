#include "tst_document.h"

#include "Document.h"
#include "EditorView.h"
#include "TextCodec.h"

#include <QBuffer>
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

constexpr qsizetype largeSize{qsizetype{3} * 1024 * 1024};

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
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("utf8.txt")))));
    QCOMPARE(editor.text(), QString::fromUtf8(bytes));
    QVERIFY(document.saveAs(QUrl::fromLocalFile(path(QStringLiteral("utf8-copy.txt")))));
    QCOMPARE(readFile(path(QStringLiteral("utf8-copy.txt"))), bytes);
}

void TestDocument::keepsCrlf()
{
    const QByteArray bytes{"one\r\ntwo\r\n\r\nthree"};
    writeFile(path(QStringLiteral("crlf.txt")), bytes);
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
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
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("lf.txt")))));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("lf.txt"))), bytes);
}

void TestDocument::keepsNonBreakingSpaces()
{
    const QByteArray bytes{"a\xc2\xa0"
                           "b\n"};
    writeFile(path(QStringLiteral("nbsp.txt")), bytes);
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("nbsp.txt")))));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("nbsp.txt"))), bytes);
}

void TestDocument::reportsMissingFile()
{
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    const QSignalSpy errors{&document, &kode::Document::errorOccurred};
    QVERIFY(!document.load(QUrl::fromLocalFile(path(QStringLiteral("missing.txt")))));
    QCOMPARE(errors.count(), 1);
    QVERIFY(document.fileUrl().isEmpty());
}

void TestDocument::readsWindows1252WhenNotUtf8()
{
    const QByteArray bytes{"caf\xe9 \x80\n"};
    writeFile(path(QStringLiteral("cp1252.txt")), bytes);
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("cp1252.txt")))));
    QCOMPARE(document.encoding(), QStringLiteral("Windows-1252"));
    QCOMPARE(editor.text(), QStringLiteral("café €\n"));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("cp1252.txt"))), bytes);
}

void TestDocument::readsUtf8CutAcrossBlocks()
{
    // Characters of 2, 3 and 4 bytes across every block boundary (4 MB).
    const QString characters{QStringLiteral("é€😀")};
    QString expected;
    for (qsizetype padding{0}; expected.size() < qsizetype{9} * 1024 * 1024; ++padding) {
        expected += QString{padding % 7, QLatin1Char('a')} + characters;
    }
    QByteArray bytes{expected.toUtf8()};
    QBuffer device{&bytes};
    QVERIFY(device.open(QIODevice::ReadOnly));
    const std::expected<kode::codec::Decoded, QString> decoded{kode::codec::readFile(device)};
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->format.encoding, QStringLiteral("UTF-8"));
    QVERIFY(decoded->text == expected);
}

void TestDocument::readsAFileEndingInACutCharacter()
{
    // "é" without its second byte: not UTF-8, so Windows-1252.
    QByteArray bytes{QByteArrayLiteral("caf\xc3")};
    QBuffer device{&bytes};
    QVERIFY(device.open(QIODevice::ReadOnly));
    const std::expected<kode::codec::Decoded, QString> decoded{kode::codec::readFile(device)};
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->format.encoding, QStringLiteral("Windows-1252"));
    QCOMPARE(decoded->text, QStringLiteral("caf\u00c3"));
}

void TestDocument::keepsTheByteOrderMark()
{
    const QByteArray bytes{"\xEF\xBB\xBF"
                           "bom\n"};
    writeFile(path(QStringLiteral("bom.txt")), bytes);
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("bom.txt")))));
    QVERIFY(document.byteOrderMark());
    QCOMPARE(editor.text(), QStringLiteral("bom\n"));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("bom.txt"))), bytes);
}

void TestDocument::reopensWithAnotherEncoding()
{
    writeFile(path(QStringLiteral("utf8-as-latin1.txt")), QByteArray{"caf\xC3\xA9"});
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("utf8-as-latin1.txt")))));
    QCOMPARE(editor.text(), QStringLiteral("café"));
    QVERIFY(document.reopen(QStringLiteral("ISO-8859-1")));
    QCOMPARE(document.encoding(), QStringLiteral("ISO-8859-1"));
    QCOMPARE(editor.text(), QStringLiteral("cafÃ©"));
}

void TestDocument::convertsEncodingAndLineEnding()
{
    writeFile(path(QStringLiteral("convert.txt")), QByteArray{"a\xC3\xA9\nb\n"});
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
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
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("lacks.txt")))));
    document.setEncoding(QStringLiteral("ISO-8859-1"));
    const QSignalSpy errors{&document, &kode::Document::errorOccurred};
    QVERIFY(!document.save());
    QCOMPARE(errors.count(), 1);
    QCOMPARE(readFile(path(QStringLiteral("lacks.txt"))), QByteArray{"\xE2\x82\xAC\n"});
}

void TestDocument::tracksModified()
{
    writeFile(path(QStringLiteral("edit.txt")), QByteArray{"text\n"});
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("edit.txt")))));
    QVERIFY(!document.modified());

    const QSignalSpy modifiedSpy{&document, &kode::Document::modifiedChanged};
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
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("clear.txt")))));
    document.clear();
    QVERIFY(document.fileUrl().isEmpty());
    QCOMPARE(document.fileName(), QStringLiteral("Untitled"));
    QVERIFY(editor.text().isEmpty());
    const QSignalSpy errors{&document, &kode::Document::errorOccurred};
    QVERIFY(!document.save());
    QCOMPARE(errors.count(), 1);
}

void TestDocument::startsAFileNotWrittenYet()
{
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    const QUrl file{QUrl::fromLocalFile(path(QStringLiteral("new/folders/notes.md")))};
    document.startFile(file);
    QCOMPARE(document.fileUrl(), file);
    QVERIFY(!document.modified());
    QCOMPARE(document.textDocument()->syntax(), QStringLiteral("Markdown"));
    editor.insert(QStringLiteral("# Notes\n"));
    QVERIFY(document.save());
    QCOMPARE(readFile(file.toLocalFile()), QByteArray{"# Notes\n"});
}

void TestDocument::reloadsAChangeOnDisk()
{
    const QString file{path(QStringLiteral("watched.txt"))};
    writeFile(file, QByteArray{"first\nsecond\n"});
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
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
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(file)));
    editor.insert(QStringLiteral("typed "));
    QSignalSpy changed{&document, &kode::Document::changedOnDisk};
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
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QVERIFY(document.load(QUrl::fromLocalFile(file)));
    editor.insert(QStringLiteral("more "));
    QSignalSpy changed{&document, &kode::Document::changedOnDisk};
    QVERIFY(document.save());
    editor.insert(QStringLiteral("again "));
    QVERIFY(!changed.wait(500));
    QCOMPARE(editor.text(), QStringLiteral("more again text\n"));
}

void TestDocument::readsALargeFileOffTheGuiThread()
{
    const QUrl url{writeLargeFile(QStringLiteral("large.cpp"))};
    kode::EditorView editor;
    kode::Document document;
    editor.setDocument(document.textDocument());
    QSignalSpy loaded{&document, &kode::Document::loadingChanged};
    QVERIFY(document.load(url));
    QVERIFY(document.loading());
    QVERIFY(loaded.wait());
    QVERIFY(!document.loading());
    QVERIFY(document.crlf());
    QVERIFY(editor.text().startsWith(QStringLiteral("int value = 42; // the answer\nint")));
}

void TestDocument::namesALargeFileWhileReadingIt()
{
    kode::Document document;
    QVERIFY(document.load(writeLargeFile(QStringLiteral("named.cpp"))));
    QCOMPARE(document.fileName(), QStringLiteral("named.cpp"));
    QVERIFY(!document.modified());
}

void TestDocument::refusesToSaveWhileReading()
{
    kode::Document document;
    const QSignalSpy errors{&document, &kode::Document::errorOccurred};
    QVERIFY(document.load(writeLargeFile(QStringLiteral("unsaved.cpp"))));
    // Its text is not there yet: saving would empty the file.
    QVERIFY(!document.save());
    QCOMPARE(errors.size(), 1);
}

void TestDocument::reportsALargeFileItCannotRead()
{
    // Bytes that are not UTF-8 (read as Windows-1252), then asked for as UTF-8.
    writeFile(path(QStringLiteral("odd.txt")), QByteArray(largeSize, '\xE9'));
    kode::Document document;
    QSignalSpy failed{&document, &kode::Document::loadFailed};
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("odd.txt")))));
    // A reopen replaces the first read: only its result counts.
    QVERIFY(document.reopen(QStringLiteral("UTF-8")));
    QVERIFY(failed.wait());
}

QUrl TestDocument::writeLargeFile(const QString &name) const
{
    // Past the size read on a worker thread, with CRLF line endings.
    const QByteArray line{"int value = 42; // the answer\r\n"};
    writeFile(path(name), line.repeated(largeSize / line.size()));
    return QUrl::fromLocalFile(path(name));
}

QTEST_MAIN(TestDocument)
