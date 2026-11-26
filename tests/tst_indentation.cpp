#include "tst_indentation.h"

#include "Indentation.h"
#include "TextBuffer.h"

#include <QTest>

using kode::indentation::Kind;
using kode::indentation::Style;

void TestIndentation::tabsAndUnits()
{
    const Style spaces{.width = 4, .kind = Kind::Spaces};
    QCOMPARE(kode::indentation::unit(spaces), QStringLiteral("    "));
    QCOMPARE(kode::indentation::tabAt(spaces, 1), QStringLiteral("   "));
    QCOMPARE(kode::indentation::tabAt({.width = 4, .kind = Kind::Tabs}, 1), QStringLiteral("\t"));
    QCOMPARE(kode::indentation::leadingWhitespace(u" \t x"), QStringView{u" \t "});
}

void TestIndentation::indentsAndOutdentsBlocks()
{
    const Style spaces{.width = 2, .kind = Kind::Spaces};
    QCOMPARE(kode::indentation::indented(u"a\n\n b", spaces), QStringLiteral("  a\n\n   b"));
    QCOMPARE(kode::indentation::outdented(u"   a\n\tb\nc", spaces), QStringLiteral(" a\nb\nc"));
}

void TestIndentation::detectsTheStyle()
{
    const Style fallback{.width = 4, .kind = Kind::Spaces};
    const kode::TextBuffer twoSpaces{QStringLiteral("a {\n  b {\n    c\n  }\n}\n")};
    QCOMPARE(kode::indentation::detect(twoSpaces, 100, fallback), (Style{.width = 2, .kind = Kind::Spaces}));
    const kode::TextBuffer tabs{QStringLiteral("a {\n\tb\n\tc\n}\n")};
    QCOMPARE(kode::indentation::detect(tabs, 100, fallback), (Style{.width = 4, .kind = Kind::Tabs}));
    const kode::TextBuffer flat{QStringLiteral("a\nb\n")};
    QCOMPARE(
        kode::indentation::detect(flat, 100, {.width = 8, .kind = Kind::Tabs}),
        (Style{.width = 8, .kind = Kind::Tabs})
    );
}

void TestIndentation::readsTheBrackets()
{
    using kode::indentation::Brackets;
    using kode::indentation::brackets;
    QCOMPARE(brackets(u"    } else {", {}), (Brackets{.leading = 1, .open = 1, .closed = 0, .colon = 0}));
    QCOMPARE(brackets(u"foo(function() {", {}), (Brackets{.leading = 0, .open = 2, .closed = 0, .colon = 0}));
    QCOMPARE(brackets(u"  b);", {}), (Brackets{.leading = 0, .open = 0, .closed = 1, .colon = 0}));
    QCOMPARE(brackets(u"if a:  ", {}), (Brackets{.leading = 0, .open = 0, .closed = 0, .colon = 1}));
    // In a string or comment (columns 4..7): nothing.
    QCOMPARE(brackets(u"x = \"{\" ", {{.start = 4, .end = 7}}), Brackets{});
}

void TestIndentation::indentsAfterTheLineAbove()
{
    using kode::indentation::Brackets;
    using kode::indentation::indentAfter;
    const kode::indentation::Style spaces{.width = 4, .kind = kode::indentation::Kind::Spaces};
    const kode::indentation::Style tabs{.width = 4, .kind = kode::indentation::Kind::Tabs};
    QCOMPARE(indentAfter(4, Brackets{.leading = 0, .open = 2, .closed = 0, .colon = 0}, 0, spaces, false), 8);
    QCOMPARE(indentAfter(8, Brackets{.leading = 0, .open = 0, .closed = 1, .colon = 0}, 0, spaces, false), 4);
    QCOMPARE(indentAfter(8, Brackets{}, 1, spaces, false), 4);
    QCOMPARE(indentAfter(0, Brackets{}, 1, spaces, false), 0);
    QCOMPARE(indentAfter(0, Brackets{.leading = 0, .open = 0, .closed = 0, .colon = 1}, 0, spaces, true), 4);
    QCOMPARE(indentAfter(0, Brackets{.leading = 0, .open = 0, .closed = 0, .colon = 1}, 0, spaces, false), 0);
    QCOMPARE(kode::indentation::whitespace(6, tabs), QStringLiteral("\t  "));
    QCOMPARE(kode::indentation::columns(u"\t  ", 4), 6);
}

QTEST_MAIN(TestIndentation)
