#pragma once

#include <QObject>

// The editor's marks: bracket pairs, the word at the caret, whole words.
class TestMatching : public QObject
{
    Q_OBJECT
private slots:
    void matchesBracketsAcrossLines();
    void prefersTheBracketAfterTheCaret();
    void noMatchWhenUnbalancedOrTooFar();
    void skipsBracketsInCommentsAndStrings();
    void findsTheWordAtAColumn();
    void findsWholeWordsOnly();
};
