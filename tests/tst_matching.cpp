#include "tst_matching.h"

#include "Matching.h"
#include "TextBuffer.h"

#include <QTest>

using kode::matching::BracketPair;
using kode::matching::Range;

void TestMatching::matchesBracketsAcrossLines()
{
    const kode::TextBuffer buffer{QStringLiteral("f(a[0]) {\n  g();\n}\n")};
    // "{" at 8, "}" at 17.
    QCOMPARE(kode::matching::matchingBracket(buffer, 8, 10), (BracketPair{.bracket = 8, .match = 17}));
    QCOMPARE(kode::matching::matchingBracket(buffer, 18, 10), (BracketPair{.bracket = 17, .match = 8}));
    // "(" at 1 skips the nested "[...]".
    QCOMPARE(kode::matching::matchingBracket(buffer, 1, 10), (BracketPair{.bracket = 1, .match = 6}));
}

void TestMatching::prefersTheBracketAfterTheCaret()
{
    const kode::TextBuffer buffer{QStringLiteral("()[]")};
    // Between ")" and "[": the "[" after the caret wins.
    QCOMPARE(kode::matching::matchingBracket(buffer, 2, 10), (BracketPair{.bracket = 2, .match = 3}));
    QCOMPARE(kode::matching::matchingBracket(buffer, 4, 10), (BracketPair{.bracket = 3, .match = 2}));
}

void TestMatching::noMatchWhenUnbalancedOrTooFar()
{
    const kode::TextBuffer unbalanced{QStringLiteral("((x)")};
    QVERIFY(!kode::matching::matchingBracket(unbalanced, 0, 10));
    const kode::TextBuffer far{QStringLiteral("{\n\n\n\n}")};
    QVERIFY(!kode::matching::matchingBracket(far, 0, 2));
    QVERIFY(kode::matching::matchingBracket(far, 0, 4));
    QVERIFY(!kode::matching::matchingBracket(far, 2, 10));
}

void TestMatching::findsTheWordAtAColumn()
{
    QCOMPARE(kode::matching::wordAt(u"int value_1 = 2;", 6), (Range{.start = 4, .end = 11}));
    QCOMPARE(kode::matching::wordAt(u"int value_1 = 2;", 11), (Range{.start = 4, .end = 11}));
    const Range none{kode::matching::wordAt(u"a = b", 2)};
    QCOMPARE(none.start, none.end);
}

void TestMatching::findsWholeWordsOnly()
{
    const QList<Range> found{kode::matching::wholeWords(u"size sizes resize size", u"size")};
    QCOMPARE(found, (QList<Range>{{.start = 0, .end = 4}, {.start = 18, .end = 22}}));
}

QTEST_MAIN(TestMatching)
