#include "tst_matching.h"

#include "Matching.h"
#include "TextBuffer.h"

#include <QTest>

using tekst::matching::BracketPair;
using tekst::matching::Range;

void TestMatching::matchesBracketsAcrossLines()
{
    const tekst::TextBuffer buffer{QStringLiteral("f(a[0]) {\n  g();\n}\n")};
    // "{" at 8, "}" at 17.
    QCOMPARE(tekst::matching::matchingBracket(buffer, 8, 10), (BracketPair{.bracket = 8, .match = 17}));
    QCOMPARE(tekst::matching::matchingBracket(buffer, 18, 10), (BracketPair{.bracket = 17, .match = 8}));
    // "(" at 1 skips the nested "[...]".
    QCOMPARE(tekst::matching::matchingBracket(buffer, 1, 10), (BracketPair{.bracket = 1, .match = 6}));
}

void TestMatching::prefersTheBracketAfterTheCaret()
{
    const tekst::TextBuffer buffer{QStringLiteral("()[]")};
    // Between ")" and "[": the "[" after the caret wins.
    QCOMPARE(tekst::matching::matchingBracket(buffer, 2, 10), (BracketPair{.bracket = 2, .match = 3}));
    QCOMPARE(tekst::matching::matchingBracket(buffer, 4, 10), (BracketPair{.bracket = 3, .match = 2}));
}

void TestMatching::noMatchWhenUnbalancedOrTooFar()
{
    const tekst::TextBuffer unbalanced{QStringLiteral("((x)")};
    QVERIFY(!tekst::matching::matchingBracket(unbalanced, 0, 10));
    const tekst::TextBuffer far{QStringLiteral("{\n\n\n\n}")};
    QVERIFY(!tekst::matching::matchingBracket(far, 0, 2));
    QVERIFY(tekst::matching::matchingBracket(far, 0, 4));
    QVERIFY(!tekst::matching::matchingBracket(far, 2, 10));
}

void TestMatching::findsTheWordAtAColumn()
{
    QCOMPARE(tekst::matching::wordAt(u"int value_1 = 2;", 6), (Range{.start = 4, .end = 11}));
    QCOMPARE(tekst::matching::wordAt(u"int value_1 = 2;", 11), (Range{.start = 4, .end = 11}));
    const Range none{tekst::matching::wordAt(u"a = b", 2)};
    QCOMPARE(none.start, none.end);
}

void TestMatching::findsWholeWordsOnly()
{
    const QList<Range> found{tekst::matching::wholeWords(u"size sizes resize size", u"size")};
    QCOMPARE(found, (QList<Range>{{.start = 0, .end = 4}, {.start = 18, .end = 22}}));
}

QTEST_MAIN(TestMatching)
