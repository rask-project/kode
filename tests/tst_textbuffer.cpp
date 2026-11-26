#include "tst_textbuffer.h"

#include "TextBuffer.h"

#include <QRandomGenerator>
#include <QStringList>
#include <QTest>

#include <algorithm>
#include <iterator>

namespace
{

// Line `line` of `buffer`, starting at `start`, is `expected`.
void compareLine(const kode::TextBuffer &buffer, qsizetype line, qsizetype start, const QString &expected)
{
    QCOMPARE(buffer.lineStart(line), start);
    QCOMPARE(buffer.line(line), expected);
    QCOMPARE(buffer.lineLength(line), expected.size());
    QCOMPARE(buffer.lineAt(start), line);
}

// Every line, line start, line length and line-of-position of `buffer`
// matches `reference`.
void compareWithReference(const kode::TextBuffer &buffer, const QString &reference)
{
    QCOMPARE(buffer.toString(), reference);
    QCOMPARE(buffer.length(), reference.size());
    const QStringList lines{reference.split(QLatin1Char('\n'))};
    QCOMPARE(buffer.lineCount(), lines.size());
    qsizetype start{0};
    for (qsizetype line{0}; line < lines.size(); ++line) {
        compareLine(buffer, line, start, lines.at(line));
        start += lines.at(line).size() + 1;
    }
    QCOMPARE(buffer.lineAt(reference.size()), lines.size() - 1);
    QList<qsizetype> lengths;
    std::ranges::transform(lines, std::back_inserter(lengths), &QString::size);
    QCOMPARE(buffer.lineLengths(), lengths);
}

} // namespace

void TestTextBuffer::empty()
{
    const kode::TextBuffer buffer;
    QCOMPARE(buffer.length(), 0);
    QCOMPARE(buffer.lineCount(), 1);
    QCOMPARE(buffer.line(0), QString{});
    QCOMPARE(buffer.lineAt(0), 0);
}

void TestTextBuffer::lines()
{
    const QString text{QStringLiteral("one\ntwo\n\nthree\n")};
    const kode::TextBuffer buffer{text};
    compareWithReference(buffer, text);
    QCOMPARE(buffer.lineCount(), 5);
    QCOMPARE(buffer.line(3), QStringLiteral("three"));
    QCOMPARE(buffer.line(4), QString{});
}

void TestTextBuffer::insertsAndRemoves()
{
    QString reference{QStringLiteral("hello world\nsecond line")};
    kode::TextBuffer buffer{reference};

    buffer.insert(5, u",");
    reference.insert(5, QLatin1Char(','));
    compareWithReference(buffer, reference);

    buffer.insert(0, u"first\n");
    reference.insert(0, QStringLiteral("first\n"));
    compareWithReference(buffer, reference);

    buffer.insert(reference.size(), u"\nend");
    reference.append(QStringLiteral("\nend"));
    compareWithReference(buffer, reference);

    // Across pieces and newlines.
    buffer.remove(3, 12);
    reference.remove(3, 12);
    compareWithReference(buffer, reference);

    buffer.remove(0, reference.size());
    reference.clear();
    compareWithReference(buffer, reference);
}

void TestTextBuffer::coalescesTyping()
{
    kode::TextBuffer buffer{QStringLiteral("ab")};
    const QString typed{QStringLiteral("typing one key at a time\n")};
    qsizetype position{1};
    for (const QChar character : typed) {
        buffer.insert(position, QStringView{&character, 1});
        ++position;
    }
    QCOMPARE(buffer.toString(), QStringLiteral("a") + typed + QStringLiteral("b"));
    // The original split in two around one added piece.
    QCOMPARE(buffer.pieceCount(), 3);
}

void TestTextBuffer::randomEditsMatchReference()
{
    QRandomGenerator random{20261125U};
    const QString alphabet{QStringLiteral("abc \n\t日é")};
    QString reference{QStringLiteral("start\nof the\ntext\n")};
    kode::TextBuffer buffer{reference};
    for (qint32 step{0}; step < 2000; ++step) {
        const bool insert{reference.isEmpty() || random.bounded(3) != 0};
        if (insert) {
            const auto position{static_cast<qsizetype>(random.bounded(static_cast<qint32>(reference.size()) + 1))};
            QString text;
            const qint32 length{random.bounded(1, 6)};
            for (qint32 i{0}; i < length; ++i) {
                text.append(alphabet.at(random.bounded(static_cast<qint32>(alphabet.size()))));
            }
            buffer.insert(position, text);
            reference.insert(position, text);
        } else {
            const auto position{static_cast<qsizetype>(random.bounded(static_cast<qint32>(reference.size())))};
            const auto count{static_cast<qsizetype>(random.bounded(1, 8))};
            buffer.remove(position, count);
            reference.remove(position, count);
        }
        if (step % 100 == 0) {
            compareWithReference(buffer, reference);
        }
    }
    compareWithReference(buffer, reference);
}

QTEST_GUILESS_MAIN(TestTextBuffer)
