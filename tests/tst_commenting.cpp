#include "tst_commenting.h"

#include "Commenting.h"

#include <QTest>

namespace
{

const kode::commenting::Markers cpp{
    .line = QStringLiteral("//"),
    .open = QStringLiteral("/*"),
    .close = QStringLiteral("*/")
};
const kode::commenting::Markers python{.line = QStringLiteral("#"), .open = {}, .close = {}};
const kode::commenting::Markers html{.line = {}, .open = QStringLiteral("<!--"), .close = QStringLiteral("-->")};

} // namespace

void TestCommenting::commentsLinesAtTheLeastIndentation()
{
    QCOMPARE(
        kode::commenting::toggledLines(u"    if (x) {\n        a();\n\n    }", cpp, false),
        QStringLiteral("    // if (x) {\n    //     a();\n\n    // }")
    );
    QCOMPARE(kode::commenting::toggledLines(u"x = 1", python, false), QStringLiteral("# x = 1"));
}

void TestCommenting::uncommentsWhenAllAre()
{
    QCOMPARE(
        kode::commenting::toggledLines(u"    // if (x) {\n    //     a();\n\n    // }", cpp, false),
        QStringLiteral("    if (x) {\n        a();\n\n    }")
    );
    QCOMPARE(kode::commenting::toggledLines(u"#x\n# y", python, false), QStringLiteral("x\ny"));
    // One not commented: all are commented (again).
    QCOMPARE(kode::commenting::toggledLines(u"// a\nb", cpp, false), QStringLiteral("// // a\n// b"));
}

void TestCommenting::putsTheMarkerAtColumnZeroWhenAsked()
{
    QCOMPARE(kode::commenting::toggledLines(u"    x", cpp, true), QStringLiteral("//     x"));
    QCOMPARE(kode::commenting::toggledLines(u"//     x", cpp, true), QStringLiteral("    x"));
}

void TestCommenting::wrapsLinesWithoutALineMarker()
{
    QCOMPARE(
        kode::commenting::toggledLines(u"  <p>a</p>\n<b/>", html, false),
        QStringLiteral("  <!-- <p>a</p> -->\n<!-- <b/> -->")
    );
    QCOMPARE(
        kode::commenting::toggledLines(u"  <!-- <p>a</p> -->\n<!-- <b/> -->", html, false),
        QStringLiteral("  <p>a</p>\n<b/>")
    );
}

void TestCommenting::togglesABlock()
{
    QCOMPARE(kode::commenting::toggledBlock(u"a + b", cpp), QStringLiteral("/* a + b */"));
    QCOMPARE(kode::commenting::toggledBlock(u"/* a + b */", cpp), QStringLiteral("a + b"));
    QCOMPARE(kode::commenting::toggledBlock(u"x\ny", cpp), QStringLiteral("/* x\ny */"));
    QCOMPARE(kode::commenting::toggledBlock(u"a", python), QStringLiteral("a"));
}

void TestCommenting::leavesTextWithoutMarkers()
{
    QCOMPARE(kode::commenting::toggledLines(u"text", kode::commenting::Markers{}, false), QStringLiteral("text"));
    QCOMPARE(kode::commenting::toggledLines(u"\n  \n", cpp, false), QStringLiteral("\n  \n"));
}

QTEST_GUILESS_MAIN(TestCommenting)
