#include "tst_textsearch.h"

#include "TextBuffer.h"
#include "TextSearch.h"

#include <QTest>

namespace
{

QList<qsizetype> starts(const tekst::TextSearch &search)
{
    QList<qsizetype> result;
    for (const tekst::TextSearch::Match &match : search.matches()) {
        result.append(match.start);
    }
    return result;
}

void searchAll(tekst::TextSearch &search)
{
    QVERIFY(search.advance(QDeadlineTimer{QDeadlineTimer::Forever}));
}

} // namespace

void TestTextSearch::findsPlainTextIgnoringCase()
{
    const tekst::TextBuffer buffer{QStringLiteral("a.b A.B\nx a.b")};
    tekst::TextSearch search{&buffer};
    search.setQuery(QStringLiteral("a.b"), false, false);
    searchAll(search);
    QCOMPARE(starts(search), (QList<qsizetype>{0, 4, 10}));
    QCOMPARE(search.indexFrom(5), 2);
    QCOMPARE(search.indexOf(4, 3), 1);
    QCOMPARE(search.indexOf(4, 2), -1);
}

void TestTextSearch::honoursCaseAndRegularExpressions()
{
    const tekst::TextBuffer buffer{QStringLiteral("Foo foo f0o")};
    tekst::TextSearch search{&buffer};
    search.setQuery(QStringLiteral("foo"), true, false);
    searchAll(search);
    QCOMPARE(starts(search), (QList<qsizetype>{4}));
    search.setQuery(QStringLiteral("f\\wo"), false, true);
    searchAll(search);
    QCOMPARE(starts(search), (QList<qsizetype>{0, 4, 8}));
}

void TestTextSearch::reportsInvalidExpressions()
{
    const tekst::TextBuffer buffer{QStringLiteral("text")};
    tekst::TextSearch search{&buffer};
    search.setQuery(QStringLiteral("(unclosed"), false, true);
    QVERIFY(!search.isActive());
    QVERIFY(!search.error().isEmpty());
    searchAll(search);
    QVERIFY(search.matches().isEmpty());
}

void TestTextSearch::searchesInSlices()
{
    const tekst::TextBuffer buffer{QStringLiteral("needle in a haystack\n").repeated(200000)};
    tekst::TextSearch search{&buffer};
    search.setQuery(QStringLiteral("needle"), false, false);
    QVERIFY(!search.advance(QDeadlineTimer{1}));
    QVERIFY(!search.matches().isEmpty());
    searchAll(search);
    // Stored up to the cap.
    QCOMPARE(search.matches().size(), tekst::TextSearch::maxMatches);
    QVERIFY(search.isCapped());
}

void TestTextSearch::expandsCaptures()
{
    const tekst::TextBuffer buffer{QStringLiteral("key = value")};
    tekst::TextSearch search{&buffer};
    search.setQuery(QStringLiteral("(\\w+) = (\\w+)"), false, true);
    searchAll(search);
    QCOMPARE(
        search.replacementFor(search.matches().first(), QStringLiteral("$2: $1 ($0)")),
        QStringLiteral("value: key (key = value)")
    );
    search.setQuery(QStringLiteral("key"), false, false);
    searchAll(search);
    QCOMPARE(search.replacementFor(search.matches().first(), QStringLiteral("$1")), QStringLiteral("$1"));
}

QTEST_MAIN(TestTextSearch)
