#include "tst_lsp.h"

#include "Document.h"
#include "LanguageServers.h"
#include "Protocol.h"
#include "TextDocument.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using kode::TextDocument;
using kode::lsp::frame;
using kode::lsp::MessageReader;

namespace
{

QJsonObject message(qint32 id)
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}};
}

} // namespace

void TestLsp::readsMessagesArrivingInPieces()
{
    const QByteArray bytes{frame(message(7))};
    MessageReader reader;
    for (qsizetype i{0}; i < bytes.size() - 1; ++i) {
        reader.append(bytes.sliced(i, 1));
        const auto next{reader.next()};
        QVERIFY(next.has_value());
        QVERIFY(!next->has_value());
    }
    reader.append(bytes.sliced(bytes.size() - 1));
    const auto next{reader.next()};
    QVERIFY(next.has_value() && next->has_value());
    QCOMPARE((**next)[QStringLiteral("id")].toInt(), 7);
}

void TestLsp::readsSeveralMessagesAtOnce()
{
    MessageReader reader;
    reader.append(frame(message(1)) + frame(message(2)));
    const auto first{reader.next()};
    const auto second{reader.next()};
    QCOMPARE((**first)[QStringLiteral("id")].toInt(), 1);
    QCOMPARE((**second)[QStringLiteral("id")].toInt(), 2);
    QVERIFY(!reader.next()->has_value());
}

void TestLsp::skipsAMalformedMessage()
{
    MessageReader reader;
    reader.append(QByteArrayLiteral("Content-Length: 3\r\n\r\n{x}") + frame(message(3)));
    QVERIFY(!reader.next().has_value());
    const auto next{reader.next()};
    QCOMPARE((**next)[QStringLiteral("id")].toInt(), 3);
}

void TestLsp::diagnosticsMoveWithEdits()
{
    TextDocument document;
    document.setText(QStringLiteral("int a = b;\nint c = d;\n"));
    document.setDiagnostics({
        {.message = QStringLiteral("d"), .start = 19, .end = 20, .severity = TextDocument::Diagnostic::Warning},
        {.message = QStringLiteral("b"), .start = 8, .end = 9, .severity = TextDocument::Diagnostic::Error},
    });
    QCOMPARE(document.diagnostics().first().message, QStringLiteral("b"));
    QCOMPARE(document.errorCount(), 1);
    QCOMPARE(document.warningCount(), 1);
    // Two characters before both: they move.
    document.replace(0, 0, QStringLiteral("//"), false, {});
    QCOMPARE(document.diagnostics().at(0).start, 10);
    QCOMPARE(document.diagnostics().at(1).start, 21);
    // Over the first one: it shrinks to the edit's end.
    document.replace(9, 3, QStringLiteral("x"), false, {});
    QCOMPARE(document.diagnostics().at(0).start, 10);
    QCOMPARE(document.diagnostics().at(0).end, 10);
    QCOMPARE(document.diagnostics().at(1).start, 19);
}

void TestLsp::newTextClearsDiagnostics()
{
    TextDocument document;
    document.setText(QStringLiteral("x\n"));
    document.setDiagnostics({{.message = QStringLiteral("x"), .start = 0, .end = 1, .severity = 1}});
    document.setText(QStringLiteral("y\n"));
    QVERIFY(document.diagnostics().isEmpty());
}

void TestLsp::clangdReportsAnError()
{
    const QStringList folders{
        qEnvironmentVariable("PATH").split(u':') +
        QStringList{QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")}
    };
    const QString xcode{
        QStringLiteral("/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clangd")
    };
    if (QStandardPaths::findExecutable(QStringLiteral("clangd"), folders).isEmpty() &&
        !QFileInfo{xcode}.isExecutable()) {
        QSKIP("clangd is not installed");
    }
    const QTemporaryDir folder;
    const QString path{folder.filePath(QStringLiteral("main.cpp"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("int main() { return undefinedName; }\n");
    file.close();

    kode::Document document;
    QVERIFY(document.load(QUrl::fromLocalFile(path)));
    kode::lsp::LanguageServers servers;
    servers.add(&document);
    QTRY_VERIFY_WITH_TIMEOUT(document.textDocument()->errorCount() == 1, 30000);
    const TextDocument::Diagnostic &error{document.textDocument()->diagnostics().first()};
    QCOMPARE(
        document.textDocument()->text().mid(error.start, error.end - error.start),
        QStringLiteral("undefinedName")
    );

    // Fixed: the server hears of the edit and drops the error.
    document.textDocument()->replace(error.start, error.end - error.start, QStringLiteral("0"), false, {});
    QTRY_VERIFY_WITH_TIMEOUT(document.textDocument()->errorCount() == 0, 30000);
    servers.remove(&document);
}

void TestLsp::clangdCompletesAMember()
{
    const QStringList folders{
        qEnvironmentVariable("PATH").split(u':') +
        QStringList{QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")}
    };
    if (QStandardPaths::findExecutable(QStringLiteral("clangd"), folders).isEmpty()) {
        QSKIP("clangd is not installed");
    }
    const QTemporaryDir folder;
    const QString path{folder.filePath(QStringLiteral("main.cpp"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("struct S { int member; };\nint main() { S s; return s.; }\n");
    file.close();

    kode::Document document;
    QVERIFY(document.load(QUrl::fromLocalFile(path)));
    kode::lsp::LanguageServers servers;
    servers.setSearchFolders(folders);
    servers.add(&document);
    // Once the server knows the file (it reports on it), ask after "s.".
    QTRY_VERIFY_WITH_TIMEOUT(!servers.completionTriggers(&document).isEmpty(), 30000);
    const qsizetype position{document.textDocument()->text().indexOf(QStringLiteral("s.;")) + 2};
    QSignalSpy found{&servers, &kode::lsp::LanguageServers::completionsFound};
    const qint32 ticket{servers.requestCompletion(&document, position, QStringLiteral("."))};
    QVERIFY(ticket != 0);
    QTRY_VERIFY_WITH_TIMEOUT(!found.isEmpty(), 30000);
    QCOMPARE(found.first().at(0).toInt(), ticket);
    const QJsonArray items(found.first().at(1).toJsonArray());
    QVERIFY(std::ranges::any_of(items, [](const QJsonValue &item) {
        return item[QStringLiteral("label")].toString().contains(QStringLiteral("member"));
    }));
    servers.remove(&document);
}

QTEST_GUILESS_MAIN(TestLsp)
