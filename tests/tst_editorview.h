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
};
