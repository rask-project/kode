#include "tst_lsp.h"

#include "Document.h"
#include "LanguageServers.h"
#include "Protocol.h"
#include "SemanticStyle.h"
#include "SemanticTokens.h"
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
using namespace Qt::StringLiterals;
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

namespace
{

const QStringList clangdTypes{
    u"variable"_s, u"variable"_s,  u"parameter"_s,     u"function"_s, u"method"_s,     u"function"_s, u"property"_s,
    u"variable"_s, u"class"_s,     u"interface"_s,     u"enum"_s,     u"enumMember"_s, u"type"_s,     u"type"_s,
    u"unknown"_s,  u"namespace"_s, u"typeParameter"_s, u"concept"_s,  u"type"_s,       u"macro"_s,    u"modifier"_s,
    u"operator"_s, u"bracket"_s,   u"label"_s,         u"comment"_s,
};
const QStringList clangdModifiers{
    u"declaration"_s,
    u"definition"_s,
    u"deprecated"_s,
    u"deduced"_s,
    u"readonly"_s,
    u"static"_s,
    u"abstract"_s,
    u"virtual"_s,
    u"dependentName"_s,
    u"defaultLibrary"_s,
    u"usedAsMutableReference"_s,
    u"usedAsMutablePointer"_s,
    u"constructorOrDestructor"_s,
    u"userDefined"_s,
    u"functionScope"_s,
    u"classScope"_s,
    u"fileScope"_s,
    u"globalScope"_s,
};

quint32 modifier(const QString &name)
{
    return 1U << static_cast<quint32>(clangdModifiers.indexOf(name));
}

quint32 type(const QString &name)
{
    return static_cast<quint32>(clangdTypes.indexOf(name));
}

} // namespace

void TestLsp::legendStylesAsQtCreator()
{
    namespace semantic = kode::semantic;
    const kode::lsp::SemanticLegend legend{clangdTypes, clangdModifiers, false};
    QCOMPARE(legend.style(type(u"variable"_s), modifier(u"functionScope"_s)), semantic::Local);
    QCOMPARE(
        legend.style(type(u"variable"_s), modifier(u"functionScope"_s) | modifier(u"declaration"_s)),
        semantic::Local | semantic::Declaration
    );
    QCOMPARE(legend.style(type(u"variable"_s), modifier(u"globalScope"_s)), semantic::Global);
    QCOMPARE(legend.style(type(u"variable"_s), modifier(u"classScope"_s)), semantic::Field);
    QCOMPARE(legend.style(type(u"method"_s), modifier(u"virtual"_s)), semantic::VirtualMethod);
    // As Qt Creator shows them: a static function plain, an enumerator as a static member.
    QCOMPARE(legend.style(type(u"method"_s), modifier(u"static"_s)), semantic::Function);
    QCOMPARE(legend.style(type(u"enumMember"_s), modifier(u"readonly"_s)), semantic::StaticMember);
    QCOMPARE(legend.style(type(u"property"_s), modifier(u"static"_s)), semantic::StaticMember);
    QCOMPARE(legend.style(type(u"class"_s), modifier(u"constructorOrDestructor"_s)), semantic::Function);
    QCOMPARE(
        legend.style(type(u"parameter"_s), modifier(u"usedAsMutableReference"_s)),
        semantic::Parameter | semantic::OutputArgument
    );
    QCOMPARE(legend.style(type(u"namespace"_s), 0U), semantic::Namespace);
    // Left to the highlighting.
    QCOMPARE(legend.style(type(u"comment"_s), 0U), std::nullopt);
    QCOMPARE(legend.style(type(u"operator"_s), 0U), std::nullopt);
    // qmlls's own names and its QML types.
    const kode::lsp::SemanticLegend qml{{u"type"_s, u"qmlScopeObjectProperty"_s, u"keyword"_s}, {}, true};
    QCOMPARE(qml.style(0, 0U), semantic::QmlTypeId);
    QCOMPARE(qml.style(1, 0U), semantic::QmlScopeObjectProperty);
    QCOMPARE(qml.style(2, 0U), std::nullopt);
}

void TestLsp::decodesRelativeTokens()
{
    const kode::lsp::SemanticLegend legend{clangdTypes, clangdModifiers, false};
    // "Point p;" then, two lines down, "p" and an operator (no style).
    const QList<quint32> data{
        0, 0, 5, type(u"class"_s),    0,
        0, 6, 1, type(u"variable"_s), modifier(u"functionScope"_s),
        2, 4, 1, type(u"variable"_s), modifier(u"functionScope"_s),
        0, 1, 1, type(u"operator"_s), 0,
    };
    const QList<QList<TextDocument::SemanticToken>> lines{kode::lsp::decodeTokens(data, legend, 3)};
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0).size(), 2);
    QCOMPARE(lines.at(0).at(0), (TextDocument::SemanticToken{.column = 0, .length = 5, .style = kode::semantic::Type}));
    QCOMPARE(lines.at(0).at(1).column, 6);
    QVERIFY(lines.at(1).isEmpty());
    QCOMPARE(lines.at(2).size(), 1);
    QCOMPARE(lines.at(2).at(0).column, 4);
}

void TestLsp::appliesTokenEdits()
{
    QList<quint32> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const QJsonArray edits{
        QJsonObject{{u"start"_s, 5}, {u"deleteCount"_s, 5}, {u"data"_s, QJsonArray{60, 70}}},
        QJsonObject{{u"start"_s, 0}, {u"deleteCount"_s, 1}, {u"data"_s, QJsonArray{}}},
    };
    kode::lsp::applyTokenEdits(data, edits);
    QCOMPARE(data, (QList<quint32>{2, 3, 4, 5, 60, 70}));
}

void TestLsp::refinesQmlTokens()
{
    namespace semantic = kode::semantic;
    const kode::TextBuffer buffer{u"Item {\n"
                                  "    id: root\n"
                                  "    property url file: x ? root.width : 0\n"
                                  "    function f() { return g({key: 1}); }\n"
                                  "}\n"_s};
    // As the legend leaves them: variables Local, properties Field, types QmlTypeId.
    const auto token{[](qint32 column, qint32 length, quint32 style) {
        return TextDocument::SemanticToken{.column = column, .length = length, .style = style};
    }};
    QList<QList<TextDocument::SemanticToken>> lines{
        {token(0, 4, semantic::QmlTypeId)},
        {token(4, 2, semantic::Field), token(8, 4, semantic::Local)},
        {token(13, 3, semantic::QmlTypeId),
         token(17, 4, semantic::Field),
         token(27, 4, semantic::Local),
         token(32, 5, semantic::Field)},
        {token(30, 3, semantic::Field)},
        {},
    };
    kode::lsp::refineQmlTokens(lines, buffer);
    QCOMPARE(lines.at(0).at(0).style, quint32{semantic::QmlTypeId});
    // "id:" bound; the id in italics.
    QCOMPARE(lines.at(1).at(0).style, quint32{semantic::Binding});
    QCOMPARE(lines.at(1).at(1).style, quint32{semantic::QmlScopeObjectProperty});
    // A value type plain; the property declared with a value bound; a member read plain, even before ':'.
    QCOMPARE(lines.at(2).at(0).style, quint32{semantic::Plain});
    QCOMPARE(lines.at(2).at(1).style, quint32{semantic::Binding});
    QCOMPARE(lines.at(2).at(2).style, quint32{semantic::QmlScopeObjectProperty});
    QCOMPARE(lines.at(2).at(3).style, quint32{semantic::Plain});
    // A JavaScript object's key: plain.
    QCOMPARE(lines.at(3).at(0).style, quint32{semantic::Plain});
}

void TestLsp::semanticTokensMoveWithEdits()
{
    TextDocument document;
    document.setText(u"int value = other;\nnext"_s);
    document.setSemanticTokens({
        {{.column = 4, .length = 5, .style = 0U}, {.column = 12, .length = 5, .style = 1U}},
        {{.column = 0, .length = 4, .style = 2U}},
    });
    // Before "value": both move; inside "value": it goes, "other" moves.
    document.replace(0, 0, u"  "_s, false, {});
    QCOMPARE(document.semanticTokens(0).at(0).column, 6);
    QCOMPARE(document.semanticTokens(0).at(1).column, 14);
    document.replace(7, 1, u"aa"_s, false, {});
    QCOMPARE(document.semanticTokens(0).size(), 1);
    QCOMPARE(document.semanticTokens(0).at(0).column, 15);
    // A line split: the next line's tokens move down with it.
    document.replace(0, 0, u"\n"_s, false, {});
    QVERIFY(document.semanticTokens(0).isEmpty());
    QVERIFY(document.semanticTokens(1).isEmpty());
    QCOMPARE(document.semanticTokens(2).size(), 1);
    QCOMPARE(document.semanticTokens(2).at(0).style, 2U);
    // New text clears them.
    document.setText(u"x"_s);
    QVERIFY(document.semanticTokens(0).isEmpty());
}

void TestLsp::clangdStylesAType()
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
    file.write("struct Point { int x; };\nint main() { Point p; return p.x; }\n");
    file.close();

    kode::Document document;
    QVERIFY(document.load(QUrl::fromLocalFile(path)));
    kode::lsp::LanguageServers servers;
    servers.setSearchFolders(folders);
    servers.add(&document);
    const TextDocument *const text{document.textDocument()};
    QTRY_VERIFY_WITH_TIMEOUT(!text->semanticTokens(1).isEmpty(), 30000);
    const qsizetype column{text->buffer().line(1).indexOf(u"Point"_s)};
    const QList<TextDocument::SemanticToken> &tokens{text->semanticTokens(1)};
    const auto point{std::ranges::find(tokens, static_cast<qint32>(column), &TextDocument::SemanticToken::column)};
    QVERIFY(point != tokens.cend());
    QCOMPARE(point->style & kode::semantic::categoryMask, static_cast<quint32>(kode::semantic::Type));
    servers.remove(&document);
}

void TestLsp::clangdFixesAnError()
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
    file.write("int main()\n{\n    int x = 1\n    return x;\n}\n");
    file.close();

    kode::Document document;
    QVERIFY(document.load(QUrl::fromLocalFile(path)));
    kode::lsp::LanguageServers servers;
    servers.setSearchFolders(folders);
    servers.add(&document);
    TextDocument *const text{document.textDocument()};
    QTRY_VERIFY_WITH_TIMEOUT(text->errorCount() > 0, 30000);
    const TextDocument::Diagnostic &error{text->diagnostics().first()};
    QVERIFY(error.message.contains(u"fix available: ⌥↩"_s));

    QSignalSpy found{&servers, &kode::lsp::LanguageServers::codeActionsFound};
    const qint32 ticket{servers.requestCodeActions(&document, error.start, error.end)};
    QVERIFY(ticket != 0);
    QTRY_VERIFY_WITH_TIMEOUT(!found.isEmpty(), 30000);
    const QStringList titles{found.first().at(1).toStringList()};
    QVERIFY(!titles.isEmpty());
    servers.applyCodeAction(ticket, 0);
    QCOMPARE(text->text(), u"int main()\n{\n    int x = 1;\n    return x;\n}\n"_s);
    // The server hears of it: the error goes.
    QTRY_VERIFY_WITH_TIMEOUT(text->errorCount() == 0, 30000);
    servers.remove(&document);
}

QTEST_GUILESS_MAIN(TestLsp)
