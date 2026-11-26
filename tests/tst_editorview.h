#pragma once

#include <QObject>

// EditorView's editing model, driven through its API and key and input
// method events (no window needed).
class TestEditorView : public QObject
{
    Q_OBJECT
private slots:
    void typingMergesIntoOneUndo();
    void newlineStartsANewUndo();
    void backspaceMergesAndUndoRestoresCaret();
    void redoAfterUndo();
    void editAfterUndoDropsRedo();
    void modifiedFollowsSavedState();
    void selectionReplacesAndDeletes();
    void shiftArrowsSelect();
    void wordNavigationAndDeletion();
    void verticalMovesKeepColumn();
    void inputMethodCommitAndReplacement();
    void accessibleText();
    void accessibleSelection();
    void accessibleBoundaries();
    void accessibleEditingIsUndoable();
    void findNextSelectsMatches();
    void findWraps();
    void replaceAllIsOneUndo();
    void goesToLineAndColumn();
    void returnKeepsIndentation();
    void tabIndentsSelectedLines();
    void wrappedLinesAddRowsAndMoveByRow();
    void wrappingFollowsEditsAndNewText();
    void relayoutKeepsTheTopLine();
    void newTextStartsWithTheDefaultIndentation();
    void viewsShareADocument();
    void undoInOneViewReachesTheOther();
    void switchingDocumentsKeepsEachPlace();
    void nextOccurrenceSelectsTheWordThenTheNextOne();
    void typingAtEveryOccurrenceIsOneUndo();
    void deletingAndMovingAtEveryCaret();
    void fromACaretOnlyWholeWords();
    void escapeOrAClickLeavesOneCaret();
    void nextOccurrenceWrapsAndSkipsSelected();
    void optionArrowsAddCaretsAboveAndBelow();
    void optionShiftArrowsAddBlockSelections();
    void foldsTheBlockAroundTheCaret();
    void foldsTheBlockStartingOnTheCaretsLine();
    void movesPastFoldsAndOpensThemForEdits();
    void foldsByIndentation();
};
