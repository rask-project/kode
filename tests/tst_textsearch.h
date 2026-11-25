#pragma once

#include <QObject>

// TextSearch: matches, options, slices, replacements with captures.
class TestTextSearch : public QObject
{
    Q_OBJECT
private slots:
    void findsPlainTextIgnoringCase();
    void honoursCaseAndRegularExpressions();
    void reportsInvalidExpressions();
    void searchesInSlices();
    void expandsCaptures();
};
