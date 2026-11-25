#pragma once

#include <QObject>

// WrapIndex against counting every line again, through random edits.
class TestWrapIndex : public QObject
{
    Q_OBJECT
private slots:
    void estimatesFromLengths();
    void randomEditsMatchCountingAgain();
    void measuredRowsMoveTheLinesBelow();
};
