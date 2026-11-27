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
    // With a syntax: brackets and quotes typed get their closing one.
    void typingPairsWithASyntax();
    // Return after "Rectangle {" with nothing under it: the closing brace two lines down.
    void returnAfterAnOpenBraceAddsItsClosing();
    void closingBracketGoesUnderItsOpening();
    // The navigation history's places, as Qt Creator's: where it edited, once
    // the caret leaves that line; where Go to Line left from; not a restore.
    void recordsNavigationPlaces();
    void reindentsByBrackets();
    void reindentsEditedLinesKeepingTheCaret();
    void reindentsManyEditedLinesAtOnce();
    void followsEditedLinesAcrossEdits();
    void colonOpensABlockInPython();
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
    void selectsGrowingBlocks();
    void togglesCommentsByTheLanguage();
    void foldsTheBlockStartingOnTheCaretsLine();
    void movesPastFoldsAndOpensThemForEdits();
    void foldsByIndentation();
};
