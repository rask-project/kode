#include "tst_wrapindex.h"

#include "TextBuffer.h"
#include "WrapIndex.h"

#include <QRandomGenerator>
#include <QTest>

namespace
{

constexpr qsizetype columns{10};

qsizetype expectedRows(const tekst::TextBuffer &buffer, qsizetype line)
{
    return std::max(qsizetype{1}, (buffer.lineLength(line) + columns - 1) / columns);
}

// Whether `index` holds the estimates of every line of `buffer`.
bool matches(const tekst::WrapIndex &index, const tekst::TextBuffer &buffer)
{
    if (index.lineCount() != buffer.lineCount()) {
        return false;
    }
    qsizetype row{0};
    for (qsizetype line{0}; line < buffer.lineCount(); ++line) {
        const qsizetype rows{expectedRows(buffer, line)};
        if (index.rows(line) != rows || index.rowOf(line) != row || index.lineAt(row + rows - 1) != line) {
            return false;
        }
        row += rows;
    }
    return index.totalRows() == row;
}

} // namespace

void TestWrapIndex::estimatesFromLengths()
{
    const tekst::TextBuffer buffer{QStringLiteral("short\n0123456789abc\n\n") + QString(25, QLatin1Char('x'))};
    tekst::WrapIndex index;
    index.reset(buffer, columns);
    QCOMPARE(index.rows(0), 1);
    QCOMPARE(index.rows(1), 2);
    QCOMPARE(index.rows(2), 1);
    QCOMPARE(index.rows(3), 3);
    QCOMPARE(index.totalRows(), 7);
    QCOMPARE(index.rowOf(3), 4);
    QCOMPARE(index.lineAt(5), 3);
    QCOMPARE(index.lineAt(99), 3);
}

void TestWrapIndex::randomEditsMatchCountingAgain()
{
    QRandomGenerator random{7};
    QString text;
    for (qint32 line{0}; line < 5000; ++line) {
        text += QString(static_cast<qsizetype>(random.bounded(40)), QLatin1Char('a')) + QLatin1Char('\n');
    }
    tekst::TextBuffer buffer{text};
    tekst::WrapIndex index;
    index.reset(buffer, columns);
    QVERIFY(matches(index, buffer));
    const std::array<QString, 4>
        snippets{QStringLiteral("abcdefghijkl"), QStringLiteral("\n"), QStringLiteral("x\ny\nz"), QString{}};
    for (qint32 round{0}; round < 300; ++round) {
        const auto position{static_cast<qsizetype>(random.bounded(static_cast<qint64>(buffer.length() + 1)))};
        const auto count{std::min(static_cast<qsizetype>(random.bounded(3000)), buffer.length() - position)};
        const QString removed{buffer.text(position, count)};
        const QString &inserted{snippets.at(random.bounded(static_cast<quint32>(snippets.size())))};
        const qsizetype line{buffer.lineAt(position)};
        buffer.remove(position, count);
        buffer.insert(position, inserted);
        index.linesReplaced(buffer, line, removed.count(QLatin1Char('\n')), inserted.count(QLatin1Char('\n')));
        if (round % 50 == 0 && !matches(index, buffer)) {
            QFAIL(qPrintable(QStringLiteral("differs after edit %1").arg(round)));
        }
    }
    QVERIFY(matches(index, buffer));
}

void TestWrapIndex::measuredRowsMoveTheLinesBelow()
{
    const tekst::TextBuffer buffer{QStringLiteral("a\nb\nc\n")};
    tekst::WrapIndex index;
    index.reset(buffer, columns);
    index.setRows(1, 4);
    QCOMPARE(index.rowOf(2), 5);
    QCOMPARE(index.lineAt(4), 1);
    QCOMPARE(index.totalRows(), 7);
}

QTEST_MAIN(TestWrapIndex)
