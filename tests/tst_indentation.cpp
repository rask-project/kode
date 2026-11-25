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

QTEST_MAIN(TestIndentation)
