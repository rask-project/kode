#pragma once

#include <QObject>

// SyntaxHighlighter: language detection, and incremental highlighting after
// random edits matching a highlight from scratch.
class TestSyntaxHighlighter : public QObject
{
    Q_OBJECT
private slots:
    void detectsByFileName();
    void detectsByShebang();
    void formatsALine();
    void incrementalMatchesFull();
    void stopsAtTheDeadline();
};
