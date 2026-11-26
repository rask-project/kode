#include "tst_folding.h"

#include "Folding.h"
#include "TextBuffer.h"

#include <QTest>

using kode::folding::Fold;
using kode::folding::FoldIndex;

namespace
{

constexpr qsizetype lineCount{20};

qsizetype identity(qsizetype at)
{
    return at;
}

qsizetype clampedLine(qsizetype row)
{
    return std::clamp(row, qsizetype{0}, lineCount - 1);
}

// Every line two rows.
qsizetype doubleRow(qsizetype line)
{
    return line * 2;
}

qsizetype doubleLine(qsizetype row)
{
    return clampedLine(row / 2);
}

} // namespace

void TestFolding::mapsRowsPastFolds()
{
    FoldIndex folds;
    folds.fold({.first = 3, .last = 5});
    folds.fold({.first = 10, .last = 10});
    QCOMPARE(folds.hiddenRows(identity), 4);
    QCOMPARE(folds.rowOf(2, identity), 2);
    QCOMPARE(folds.rowOf(6, identity), 3);
    QCOMPARE(folds.rowOf(11, identity), 7);
    // A hidden line is on its folded line's row.
    QCOMPARE(folds.rowOf(4, identity), 2);
}

void TestFolding::mapsRowsToShownLines()
{
    FoldIndex folds;
    folds.fold({.first = 3, .last = 5});
    folds.fold({.first = 10, .last = 10});
    QCOMPARE(folds.lineAt(2, identity, clampedLine), 2);
    QCOMPARE(folds.lineAt(3, identity, clampedLine), 6);
    QCOMPARE(folds.lineAt(6, identity, clampedLine), 9);
    QCOMPARE(folds.lineAt(7, identity, clampedLine), 11);
}

void TestFolding::findsWhatFolds()
{
    FoldIndex folds;
    folds.fold({.first = 3, .last = 5});
    for (qsizetype line{0}; line < lineCount; line = folds.nextShown(line + 1)) {
        QCOMPARE(folds.lineAt(folds.rowOf(line, identity), identity, clampedLine), line);
    }
    QCOMPARE(folds.nextShown(4), 6);
    QVERIFY(folds.foldedUnder(2));
    QVERIFY(!folds.foldedUnder(3));
    QCOMPARE(folds.hiding(5), (Fold{.first = 3, .last = 5}));
    QVERIFY(!folds.hiding(6));
}

void TestFolding::mapsWrappedRowsPastFolds()
{
    FoldIndex folds;
    folds.fold({.first = 2, .last = 3});
    QCOMPARE(folds.hiddenRows(doubleRow), 4);
    QCOMPARE(folds.rowOf(4, doubleRow), 4);
    // Line 1's second row, then line 4's two.
    QCOMPARE(folds.lineAt(3, doubleRow, doubleLine), 1);
    QCOMPARE(folds.lineAt(4, doubleRow, doubleLine), 4);
    QCOMPARE(folds.lineAt(5, doubleRow, doubleLine), 4);
}

void TestFolding::foldingAroundFoldsTakesThemIn()
{
    FoldIndex folds;
    folds.fold({.first = 4, .last = 5});
    folds.fold({.first = 8, .last = 9});
    folds.fold({.first = 3, .last = 9});
    QCOMPARE(folds.folds(), (QList<Fold>{{.first = 3, .last = 9}}));
    QVERIFY(folds.reveal(6));
    QVERIFY(folds.isEmpty());
    folds.fold({.first = 1, .last = 2});
    QVERIFY(!folds.unfold(1));
    QVERIFY(folds.unfold(0));
}

void TestFolding::editsOpenOrMoveFolds()
{
    FoldIndex folds;
    folds.fold({.first = 3, .last = 5});
    folds.fold({.first = 10, .last = 12});
    // Typing on the folded line keeps it; two lines added above move both.
    folds.linesReplaced(2, 0, 0);
    folds.linesReplaced(0, 0, 2);
    QCOMPARE(folds.folds(), (QList<Fold>{{.first = 5, .last = 7}, {.first = 12, .last = 14}}));
    // A line break on the folded line opens it; an edit inside the other too.
    folds.linesReplaced(4, 0, 1);
    QCOMPARE(folds.folds(), (QList<Fold>{{.first = 13, .last = 15}}));
    folds.linesReplaced(14, 0, 0);
    QVERIFY(folds.isEmpty());
}

void TestFolding::findsIndentedBlocks()
{
    const kode::TextBuffer buffer{QStringLiteral("def f():\n    a = 1\n\n\tif a:\n        b\n\nx = 2\n")};
    const QDeadlineTimer noDeadline{QDeadlineTimer::Forever};
    // The blank line at its end stays out.
    QCOMPARE(kode::folding::indentedBlock(buffer, 0, 4, noDeadline), (Fold{.first = 1, .last = 4}));
    QCOMPARE(kode::folding::indentedBlock(buffer, 3, 4, noDeadline), (Fold{.first = 4, .last = 4}));
    QVERIFY(!kode::folding::indentedBlock(buffer, 6, 4, noDeadline));
}

void TestFolding::findsTheLineABlockIsUnder()
{
    const kode::TextBuffer buffer{QStringLiteral("def f():\n    a = 1\n\n\tif a:\n        b\n\nx = 2\n")};
    const QDeadlineTimer noDeadline{QDeadlineTimer::Forever};
    QCOMPARE(kode::folding::indentOf(QStringLiteral("\t  x"), 4), 6);
    QCOMPARE(kode::folding::indentOf(QStringLiteral("   "), 4), -1);
    QVERIFY(kode::folding::opensIndentedBlock(buffer, 0, 4));
    QVERIFY(!kode::folding::opensIndentedBlock(buffer, 1, 4));
    QCOMPARE(kode::folding::lineAbove(buffer, 4, 4, noDeadline), 3);
    QCOMPARE(kode::folding::lineAbove(buffer, 2, 4, noDeadline), 1);
    QVERIFY(!kode::folding::lineAbove(buffer, 0, 4, noDeadline));
}

QTEST_MAIN(TestFolding)
