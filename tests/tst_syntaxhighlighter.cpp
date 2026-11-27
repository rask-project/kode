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

void highlightAll(kode::SyntaxHighlighter &highlighter, const kode::TextBuffer &buffer)
{
    QVERIFY(highlighter.advance(buffer.lineCount(), QDeadlineTimer{QDeadlineTimer::Forever}));
}

std::unique_ptr<kode::SyntaxHighlighter> cppHighlighter(const kode::TextBuffer &buffer)
{
    auto highlighter{std::make_unique<kode::SyntaxHighlighter>(&buffer)};
    highlighter->setTheme(kode::SyntaxHighlighter::themeNamed(QStringLiteral("Kode Light")));
    highlighter->setDefinition(kode::SyntaxHighlighter::definitionForName(QStringLiteral("C++")));
    return highlighter;
}

} // namespace

void TestSyntaxHighlighter::detectsByFileName()
{
    QCOMPARE(kode::SyntaxHighlighter::detect(QStringLiteral("main.cpp"), {}).name(), QStringLiteral("C++"));
    QCOMPARE(kode::SyntaxHighlighter::detect(QStringLiteral("CMakeLists.txt"), {}).name(), QStringLiteral("CMake"));
    QVERIFY(!kode::SyntaxHighlighter::detect(QStringLiteral("notes"), QStringLiteral("plain words")).isValid());
}

void TestSyntaxHighlighter::detectsByShebang()
{
    QCOMPARE(
        kode::SyntaxHighlighter::detect(QStringLiteral("tool"), QStringLiteral("#!/usr/bin/env python3")).name(),
        QStringLiteral("Python")
    );
    QCOMPARE(
        kode::SyntaxHighlighter::detect(QStringLiteral("run"), QStringLiteral("#!/bin/sh -e")).name(),
        QStringLiteral("Bash")
    );
}

void TestSyntaxHighlighter::formatsALine()
{
    const kode::TextBuffer buffer{sample};
    const std::unique_ptr<kode::SyntaxHighlighter> highlighter{cppHighlighter(buffer)};
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
    kode::TextBuffer buffer{sample.repeated(20)};
    const std::unique_ptr<kode::SyntaxHighlighter> highlighter{cppHighlighter(buffer)};
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

    const std::unique_ptr<kode::SyntaxHighlighter> fresh{cppHighlighter(buffer)};
    highlightAll(*fresh, buffer);
    for (qsizetype line{0}; line <= buffer.lineCount(); ++line) {
        if (highlighter->startState(line) != fresh->startState(line)) {
            QFAIL(qPrintable(QStringLiteral("line %1 starts in a different state").arg(line)));
        }
    }
}

void TestSyntaxHighlighter::stopsAtTheDeadline()
{
    const kode::TextBuffer buffer{sample.repeated(20000)};
    const std::unique_ptr<kode::SyntaxHighlighter> highlighter{cppHighlighter(buffer)};
    QVERIFY(!highlighter->advance(buffer.lineCount(), QDeadlineTimer{1}));
    QVERIFY(!highlighter->isReady(buffer.lineCount()));
    QVERIFY(highlighter->isReady(0));
}

void TestSyntaxHighlighter::guessesFarAhead()
{
    // The sample is 8 lines and leaves no state open: guessing from the start
    // of one gets it right.
    const kode::TextBuffer buffer{sample.repeated(1000)};
    const std::unique_ptr<kode::SyntaxHighlighter> exact{cppHighlighter(buffer)};
    highlightAll(*exact, buffer);
    const std::unique_ptr<kode::SyntaxHighlighter> highlighter{cppHighlighter(buffer)};
    kode::SyntaxHighlighter::Guess guess;
    highlighter->guess(guess, 5000, 5040, QDeadlineTimer{QDeadlineTimer::Forever});
    QVERIFY(!highlighter->isReady(5000));
    QCOMPARE(guess.first, 5000 - kode::SyntaxHighlighter::guessLead);
    QCOMPARE(guess.first + guess.states.size(), 5040);
    for (qsizetype line{5000}; line < 5040; ++line) {
        const QList<QTextLayout::FormatRange> guessed{
            highlighter->formats(line, buffer.line(line), guess.states.at(line - guess.first))
        };
        QCOMPARE(guessed, exact->formats(line, buffer.line(line)));
    }
}

void TestSyntaxHighlighter::guessesFromTheLastReadyLine()
{
    const kode::TextBuffer buffer{sample.repeated(1000)};
    const std::unique_ptr<kode::SyntaxHighlighter> highlighter{cppHighlighter(buffer)};
    QVERIFY(highlighter->advance(4902, QDeadlineTimer{QDeadlineTimer::Forever}));
    kode::SyntaxHighlighter::Guess guess;
    highlighter->guess(guess, 5000, 5040, QDeadlineTimer{QDeadlineTimer::Forever});
    QCOMPARE(guess.first, 4902);
    QCOMPARE(guess.states.constFirst(), highlighter->startState(4902));
    // Further down by less than the lead: extended, not started over.
    highlighter->guess(guess, 5100, 5140, QDeadlineTimer{QDeadlineTimer::Forever});
    QCOMPARE(guess.first, 4902);
    QCOMPARE(guess.first + guess.states.size(), 5140);
    QVERIFY(guess.covers(5100, 5140));
    QVERIFY(!guess.covers(4900, 5140));
}

void TestSyntaxHighlighter::dropsGuessesAfterAnEdit()
{
    kode::SyntaxHighlighter::Guess guess{.states = QList<KSyntaxHighlighting::State>(40), .first = 100};
    kode::SyntaxHighlighter::guessEdited(guess, 120);
    QCOMPARE(guess.states.size(), 21);
    kode::SyntaxHighlighter::guessEdited(guess, 99);
    QVERIFY(guess.states.isEmpty());
}

void TestSyntaxHighlighter::loadsKodeThemes()
{
    QCOMPARE(kode::SyntaxHighlighter::themeNamed(QStringLiteral("Kode Dark")).name(), QStringLiteral("Kode Dark"));
}

void TestSyntaxHighlighter::offersOnlyLightAndDarkPairs()
{
    const QStringList families{kode::SyntaxHighlighter::themeFamilies()};
    QVERIFY(families.contains(QStringLiteral("Kode")));
    QVERIFY(families.contains(QStringLiteral("Solarized")));
    QVERIFY(!families.contains(QStringLiteral("Dracula")));
    QVERIFY(!families.contains(QStringLiteral("Vim")));
    QCOMPARE(kode::SyntaxHighlighter::themeName(QStringLiteral("Solarized"), true), QStringLiteral("Solarized Dark"));
    QCOMPARE(kode::SyntaxHighlighter::themeFamily(QStringLiteral("GitHub Light")), QStringLiteral("GitHub"));
    QCOMPARE(kode::SyntaxHighlighter::themeName(QStringLiteral("Dracula"), false), QStringLiteral("Kode Light"));
    QCOMPARE(kode::SyntaxHighlighter::themeName({}, true), QStringLiteral("Kode Dark"));
}

void TestSyntaxHighlighter::kodeDarkColorsKeywords()
{
    // Qt Creator's Dark (2024) keyword: cyan, italic.
    const kode::TextBuffer buffer{QStringLiteral("return 0;")};
    const std::unique_ptr<kode::SyntaxHighlighter> highlighter{cppHighlighter(buffer)};
    highlighter->setTheme(kode::SyntaxHighlighter::themeNamed(QStringLiteral("Kode Dark")));
    highlightAll(*highlighter, buffer);
    const QList<QTextLayout::FormatRange> formats{highlighter->formats(0, buffer.line(0))};
    QVERIFY(!formats.isEmpty());
    QCOMPARE(formats.first().start, 0);
    QCOMPARE(formats.first().length, 6);
    QCOMPARE(formats.first().format.foreground().color(), QColor(QStringLiteral("#45c6d6")));
    QVERIFY(formats.first().format.fontItalic());
}

QTEST_MAIN(TestSyntaxHighlighter)
