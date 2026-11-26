#pragma once

#include <QObject>

// FoldIndex's rows and edits, and the blocks indentation makes.
class TestFolding : public QObject
{
    Q_OBJECT
private slots:
    void mapsRowsPastFolds();
    void mapsRowsToShownLines();
    void findsWhatFolds();
    void mapsWrappedRowsPastFolds();
    void foldingAroundFoldsTakesThemIn();
    void editsOpenOrMoveFolds();
    void findsIndentedBlocks();
    void findsTheLineABlockIsUnder();
};
