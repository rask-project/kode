#include "tst_document.h"

#include "Document.h"

#include <QFile>
#include <QQmlComponent>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

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

QQuickTextDocument *textDocumentOf(const QObject *editor)
{
    return editor->property("textDocument").value<QQuickTextDocument *>();
}

} // namespace

void TestDocument::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void TestDocument::init()
{
    QQmlComponent component{&m_engine};
    component.setData("import QtQuick\nTextEdit {}", QUrl{QStringLiteral("qrc:/tst_document.qml")});
    m_editor.reset(component.create());
    QVERIFY2(m_editor != nullptr, qPrintable(component.errorString()));
}

void TestDocument::cleanup()
{
    m_editor.reset();
}

QString TestDocument::path(const QString &name) const
{
    return m_dir.filePath(name);
}

void TestDocument::roundTripsUtf8()
{
    const QByteArray bytes{"olá, mundo — ℝ ☃ 日本語\n"};
    writeFile(path(QStringLiteral("utf8.txt")), bytes);
    tekst::Document document;
    document.setTextDocument(textDocumentOf(m_editor.get()));
    const QSignalSpy loaded{&document, &tekst::Document::loaded};
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("utf8.txt")))));
    QCOMPARE(loaded.count(), 1);
    QCOMPARE(m_editor->property("text").toString(), QString::fromUtf8(bytes));
    QVERIFY(document.saveAs(QUrl::fromLocalFile(path(QStringLiteral("utf8-copy.txt")))));
    QCOMPARE(readFile(path(QStringLiteral("utf8-copy.txt"))), bytes);
}

void TestDocument::keepsCrlf()
{
    const QByteArray bytes{"one\r\ntwo\r\n\r\nthree"};
    writeFile(path(QStringLiteral("crlf.txt")), bytes);
    tekst::Document document;
    document.setTextDocument(textDocumentOf(m_editor.get()));
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("crlf.txt")))));
    // The editor sees plain newlines.
    QCOMPARE(m_editor->property("text").toString(), QStringLiteral("one\ntwo\n\nthree"));
    QVERIFY(document.saveAs(QUrl::fromLocalFile(path(QStringLiteral("crlf-copy.txt")))));
    QCOMPARE(readFile(path(QStringLiteral("crlf-copy.txt"))), bytes);
}

void TestDocument::keepsLfAndFinalNewline()
{
    const QByteArray bytes{"a\nb\n"};
    writeFile(path(QStringLiteral("lf.txt")), bytes);
    tekst::Document document;
    document.setTextDocument(textDocumentOf(m_editor.get()));
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("lf.txt")))));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("lf.txt"))), bytes);
}

void TestDocument::keepsNonBreakingSpaces()
{
    const QByteArray bytes{"a\xc2\xa0"
                           "b\n"};
    writeFile(path(QStringLiteral("nbsp.txt")), bytes);
    tekst::Document document;
    document.setTextDocument(textDocumentOf(m_editor.get()));
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("nbsp.txt")))));
    QVERIFY(document.save());
    QCOMPARE(readFile(path(QStringLiteral("nbsp.txt"))), bytes);
}

void TestDocument::reportsMissingFile()
{
    tekst::Document document;
    document.setTextDocument(textDocumentOf(m_editor.get()));
    const QSignalSpy errors{&document, &tekst::Document::errorOccurred};
    QVERIFY(!document.load(QUrl::fromLocalFile(path(QStringLiteral("missing.txt")))));
    QCOMPARE(errors.count(), 1);
    QVERIFY(document.fileUrl().isEmpty());
}

void TestDocument::rejectsInvalidUtf8()
{
    writeFile(path(QStringLiteral("latin1.txt")), QByteArray{"caf\xe9\n"});
    tekst::Document document;
    document.setTextDocument(textDocumentOf(m_editor.get()));
    const QSignalSpy errors{&document, &tekst::Document::errorOccurred};
    QVERIFY(!document.load(QUrl::fromLocalFile(path(QStringLiteral("latin1.txt")))));
    QCOMPARE(errors.count(), 1);
}

void TestDocument::tracksModified()
{
    writeFile(path(QStringLiteral("edit.txt")), QByteArray{"text\n"});
    tekst::Document document;
    document.setTextDocument(textDocumentOf(m_editor.get()));
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("edit.txt")))));
    QVERIFY(!document.modified());

    const QSignalSpy modifiedSpy{&document, &tekst::Document::modifiedChanged};
    QTextCursor cursor{textDocumentOf(m_editor.get())->textDocument()};
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("more\n"));
    QVERIFY(document.modified());
    QVERIFY(!modifiedSpy.isEmpty());

    QVERIFY(document.save());
    QVERIFY(!document.modified());
    QCOMPARE(readFile(path(QStringLiteral("edit.txt"))), QByteArray{"text\nmore\n"});
}

void TestDocument::clearsToUntitled()
{
    writeFile(path(QStringLiteral("clear.txt")), QByteArray{"text\n"});
    tekst::Document document;
    document.setTextDocument(textDocumentOf(m_editor.get()));
    QVERIFY(document.load(QUrl::fromLocalFile(path(QStringLiteral("clear.txt")))));
    document.clear();
    QVERIFY(document.fileUrl().isEmpty());
    QCOMPARE(document.fileName(), QStringLiteral("Untitled"));
    QVERIFY(m_editor->property("text").toString().isEmpty());
    const QSignalSpy errors{&document, &tekst::Document::errorOccurred};
    QVERIFY(!document.save());
    QCOMPARE(errors.count(), 1);
}

QTEST_MAIN(TestDocument)
