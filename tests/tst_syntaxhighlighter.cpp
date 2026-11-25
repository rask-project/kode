#include "tst_syntaxhighlighter.h"

#include "SyntaxHighlighter.h"
#include "TextBuffer.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Theme>

#include <QRandomGenerator>
#include <QTest>

#include <array>

namespace
{

const QString sample{QStringLiteral("#include <QString>\n"
                                    "/* A block\n"
                                    "   comment */\n"
                                    "int main() {\n"
                                    "    const char *text = \"a \\\"string\\\"\";\n"
                                    "    // a line comment\n"
                                    "    return 0;\n"
                                    "}\n")};

void highlightAll(tekst::SyntaxHighlighter &highlighter, const tekst::TextBuffer &buffer)
{
    QVERIFY(highlighter.advance(buffer.lineCount(), QDeadlineTimer{QDeadlineTimer::Forever}));
}

std::unique_ptr<tekst::SyntaxHighlighter> cppHighlighter(const tekst::TextBuffer &buffer)
{
    auto highlighter{std::make_unique<tekst::SyntaxHighlighter>(&buffer)};
    highlighter->setTheme(tekst::SyntaxHighlighter::defaultTheme(false));
    highlighter->setDefinition(tekst::SyntaxHighlighter::definitionForName(QStringLiteral("C++")));
    return highlighter;
}

} // namespace

void TestSyntaxHighlighter::detectsByFileName()
{
    QCOMPARE(tekst::SyntaxHighlighter::detect(QStringLiteral("main.cpp"), {}).name(), QStringLiteral("C++"));
    QCOMPARE(tekst::SyntaxHighlighter::detect(QStringLiteral("CMakeLists.txt"), {}).name(), QStringLiteral("CMake"));
    QVERIFY(!tekst::SyntaxHighlighter::detect(QStringLiteral("notes"), QStringLiteral("plain words")).isValid());
}

void TestSyntaxHighlighter::detectsByShebang()
{
    QCOMPARE(
        tekst::SyntaxHighlighter::detect(QStringLiteral("tool"), QStringLiteral("#!/usr/bin/env python3")).name(),
        QStringLiteral("Python")
    );
    QCOMPARE(
        tekst::SyntaxHighlighter::detect(QStringLiteral("run"), QStringLiteral("#!/bin/sh -e")).name(),
        QStringLiteral("Bash")
    );
}

void TestSyntaxHighlighter::formatsALine()
{
    const tekst::TextBuffer buffer{sample};
    const std::unique_ptr<tekst::SyntaxHighlighter> highlighter{cppHighlighter(buffer)};
    highlightAll(*highlighter, buffer);
    // Inside the block comment: the whole line is one comment format.
    const QList<QTextLayout::FormatRange> comment{highlighter->formats(2, buffer.line(2))};
    QCOMPARE(comment.size(), 1);
    QCOMPARE(comment.first().start, 0);
    QCOMPARE(comment.first().length, buffer.line(2).size());
    QVERIFY(!highlighter->formats(4, buffer.line(4)).isEmpty());
}

void TestSyntaxHighlighter::incrementalMatchesFull()
{
    static constexpr std::array snippets{"/*", "*/", "\n", "\"", "x", "// c\n", "\n\n", "}"};
    QRandomGenerator random{42};
    tekst::TextBuffer buffer{sample.repeated(20)};
    const std::unique_ptr<tekst::SyntaxHighlighter> highlighter{cppHighlighter(buffer)};
    highlightAll(*highlighter, buffer);

    for (qint32 round{0}; round < 400; ++round) {
        const auto position{static_cast<qsizetype>(random.bounded(static_cast<qint64>(buffer.length() + 1)))};
        const auto count{std::min(static_cast<qsizetype>(random.bounded(8)), buffer.length() - position)};
        const QString removed{buffer.text(position, count)};
        const QString inserted{QString::fromLatin1(snippets.at(random.bounded(static_cast<quint32>(snippets.size()))))};
        const qsizetype line{buffer.lineAt(position)};
        buffer.remove(position, count);
        buffer.insert(position, inserted);
        highlighter->linesReplaced(line, removed.count(QLatin1Char('\n')), inserted.count(QLatin1Char('\n')));
        // Sometimes only part of the text before the next edit.
        const auto target{static_cast<qsizetype>(random.bounded(static_cast<qint64>(buffer.lineCount() + 1)))};
        highlighter->advance(round % 3 == 0 ? target : buffer.lineCount(), QDeadlineTimer{QDeadlineTimer::Forever});
    }
    highlightAll(*highlighter, buffer);

    const std::unique_ptr<tekst::SyntaxHighlighter> fresh{cppHighlighter(buffer)};
    highlightAll(*fresh, buffer);
    for (qsizetype line{0}; line <= buffer.lineCount(); ++line) {
        if (highlighter->startState(line) != fresh->startState(line)) {
            QFAIL(qPrintable(QStringLiteral("line %1 starts in a different state").arg(line)));
        }
    }
}

void TestSyntaxHighlighter::stopsAtTheDeadline()
{
    const tekst::TextBuffer buffer{sample.repeated(20000)};
    const std::unique_ptr<tekst::SyntaxHighlighter> highlighter{cppHighlighter(buffer)};
    QVERIFY(!highlighter->advance(buffer.lineCount(), QDeadlineTimer{1}));
    QVERIFY(!highlighter->isReady(buffer.lineCount()));
    QVERIFY(highlighter->isReady(0));
}

QTEST_MAIN(TestSyntaxHighlighter)
