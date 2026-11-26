#include "tst_indentation.h"

#include "Indentation.h"
#include "TextBuffer.h"

#include <QTest>

using tekst::indentation::Kind;
using tekst::indentation::Style;

void TestIndentation::tabsAndUnits()
{
    const Style spaces{.width = 4, .kind = Kind::Spaces};
    QCOMPARE(tekst::indentation::unit(spaces), QStringLiteral("    "));
    QCOMPARE(tekst::indentation::tabAt(spaces, 1), QStringLiteral("   "));
    QCOMPARE(tekst::indentation::tabAt({.width = 4, .kind = Kind::Tabs}, 1), QStringLiteral("\t"));
    QCOMPARE(tekst::indentation::leadingWhitespace(u" \t x"), QStringView{u" \t "});
}

void TestIndentation::indentsAndOutdentsBlocks()
{
    const Style spaces{.width = 2, .kind = Kind::Spaces};
    QCOMPARE(tekst::indentation::indented(u"a\n\n b", spaces), QStringLiteral("  a\n\n   b"));
    QCOMPARE(tekst::indentation::outdented(u"   a\n\tb\nc", spaces), QStringLiteral(" a\nb\nc"));
}

void TestIndentation::detectsTheStyle()
{
    const Style fallback{.width = 4, .kind = Kind::Spaces};
    const tekst::TextBuffer twoSpaces{QStringLiteral("a {\n  b {\n    c\n  }\n}\n")};
    QCOMPARE(tekst::indentation::detect(twoSpaces, 100, fallback), (Style{.width = 2, .kind = Kind::Spaces}));
    const tekst::TextBuffer tabs{QStringLiteral("a {\n\tb\n\tc\n}\n")};
    QCOMPARE(tekst::indentation::detect(tabs, 100, fallback), (Style{.width = 4, .kind = Kind::Tabs}));
    const tekst::TextBuffer flat{QStringLiteral("a\nb\n")};
    QCOMPARE(
        tekst::indentation::detect(flat, 100, {.width = 8, .kind = Kind::Tabs}),
        (Style{.width = 8, .kind = Kind::Tabs})
    );
}

void TestIndentation::readsTheBrackets()
{
    using tekst::indentation::Brackets;
    using tekst::indentation::brackets;
    QCOMPARE(brackets(u"    } else {", {}), (Brackets{.leading = 1, .open = 1, .closed = 0, .colon = 0}));
    QCOMPARE(brackets(u"foo(function() {", {}), (Brackets{.leading = 0, .open = 2, .closed = 0, .colon = 0}));
    QCOMPARE(brackets(u"  b);", {}), (Brackets{.leading = 0, .open = 0, .closed = 1, .colon = 0}));
    QCOMPARE(brackets(u"if a:  ", {}), (Brackets{.leading = 0, .open = 0, .closed = 0, .colon = 1}));
    // In a string or comment (columns 4..7): nothing.
    QCOMPARE(brackets(u"x = \"{\" ", {{.start = 4, .end = 7}}), Brackets{});
}

void TestIndentation::indentsAfterTheLineAbove()
{
    using tekst::indentation::Brackets;
    using tekst::indentation::indentAfter;
    const tekst::indentation::Style spaces{.width = 4, .kind = tekst::indentation::Kind::Spaces};
    const tekst::indentation::Style tabs{.width = 4, .kind = tekst::indentation::Kind::Tabs};
    QCOMPARE(indentAfter(4, Brackets{.leading = 0, .open = 2, .closed = 0, .colon = 0}, 0, spaces, false), 8);
    QCOMPARE(indentAfter(8, Brackets{.leading = 0, .open = 0, .closed = 1, .colon = 0}, 0, spaces, false), 4);
    QCOMPARE(indentAfter(8, Brackets{}, 1, spaces, false), 4);
    QCOMPARE(indentAfter(0, Brackets{}, 1, spaces, false), 0);
    QCOMPARE(indentAfter(0, Brackets{.leading = 0, .open = 0, .closed = 0, .colon = 1}, 0, spaces, true), 4);
    QCOMPARE(indentAfter(0, Brackets{.leading = 0, .open = 0, .closed = 0, .colon = 1}, 0, spaces, false), 0);
    QCOMPARE(tekst::indentation::whitespace(6, tabs), QStringLiteral("\t  "));
    QCOMPARE(tekst::indentation::columns(u"\t  ", 4), 6);
}

QTEST_MAIN(TestIndentation)
