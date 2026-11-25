#include "tst_editorview.h"

#include "EditorView.h"

#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QTest>

namespace
{

void press(
    tekst::EditorView &editor,
    Qt::Key key,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
    const QString &text = {}
)
{
    QKeyEvent event{QEvent::KeyPress, key, modifiers, text};
    QCoreApplication::sendEvent(&editor, &event);
}

void type(tekst::EditorView &editor, const QString &text)
{
    for (const QChar character : text) {
        press(editor, Qt::Key_unknown, Qt::NoModifier, QString{character});
    }
}

} // namespace

void TestEditorView::typingMergesIntoOneUndo()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("ab"));
    editor.setCursorPosition(1);
    type(editor, QStringLiteral("hello"));
    QCOMPARE(editor.text(), QStringLiteral("ahellob"));
    QVERIFY(editor.canUndo());
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("ab"));
    QCOMPARE(editor.cursorPosition(), 1);
    QVERIFY(!editor.canUndo());
}

void TestEditorView::newlineStartsANewUndo()
{
    tekst::EditorView editor;
    type(editor, QStringLiteral("one"));
    press(editor, Qt::Key_Return);
    type(editor, QStringLiteral("two"));
    QCOMPARE(editor.text(), QStringLiteral("one\ntwo"));
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("one\n"));
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("one"));
    editor.undo();
    QCOMPARE(editor.text(), QString{});
}

void TestEditorView::backspaceMergesAndUndoRestoresCaret()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("hello world"));
    editor.setCursorPosition(11);
    for (qint32 i{0}; i < 5; ++i) {
        press(editor, Qt::Key_Backspace);
    }
    QCOMPARE(editor.text(), QStringLiteral("hello "));
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("hello world"));
    QCOMPARE(editor.cursorPosition(), 11);
    QVERIFY(!editor.canUndo());
}

void TestEditorView::redoAfterUndo()
{
    tekst::EditorView editor;
    type(editor, QStringLiteral("abc"));
    editor.undo();
    QVERIFY(editor.canRedo());
    editor.redo();
    QCOMPARE(editor.text(), QStringLiteral("abc"));
    QCOMPARE(editor.cursorPosition(), 3);
    QVERIFY(!editor.canRedo());
}

void TestEditorView::editAfterUndoDropsRedo()
{
    tekst::EditorView editor;
    type(editor, QStringLiteral("abc"));
    editor.undo();
    type(editor, QStringLiteral("x"));
    QVERIFY(!editor.canRedo());
    QCOMPARE(editor.text(), QStringLiteral("x"));
}

void TestEditorView::modifiedFollowsSavedState()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("saved"));
    const QSignalSpy spy{&editor, &tekst::EditorView::modifiedChanged};
    QVERIFY(!editor.modified());
    type(editor, QStringLiteral("!"));
    QVERIFY(editor.modified());
    editor.undo();
    QVERIFY(!editor.modified());
    editor.redo();
    QVERIFY(editor.modified());
    editor.markSaved();
    QVERIFY(!editor.modified());
    // Typing after a save starts a new edit, so undo returns to the saved text.
    type(editor, QStringLiteral("?"));
    editor.undo();
    QVERIFY(!editor.modified());
    QCOMPARE(spy.count(), 6);
}

void TestEditorView::selectionReplacesAndDeletes()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("one two three"));
    editor.select(4, 7);
    QVERIFY(editor.hasSelection());
    QCOMPARE(editor.selectedText(), QStringLiteral("two"));
    type(editor, QStringLiteral("2"));
    QCOMPARE(editor.text(), QStringLiteral("one 2 three"));
    QVERIFY(!editor.hasSelection());
    editor.select(0, 4);
    press(editor, Qt::Key_Backspace);
    QCOMPARE(editor.text(), QStringLiteral("2 three"));
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("one 2 three"));
    QCOMPARE(editor.selectedText(), QStringLiteral("one "));
}

void TestEditorView::shiftArrowsSelect()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("abcdef"));
    editor.setCursorPosition(1);
    press(editor, Qt::Key_Right, Qt::ShiftModifier);
    press(editor, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(editor.selectedText(), QStringLiteral("bc"));
    // Left without Shift collapses to the selection's start.
    press(editor, Qt::Key_Left);
    QVERIFY(!editor.hasSelection());
    QCOMPARE(editor.cursorPosition(), 1);
    // ⌘⇧→ selects to the end of the line.
    press(editor, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(editor.selectedText(), QStringLiteral("bcdef"));
    editor.selectAll();
    QCOMPARE(editor.selectedText(), QStringLiteral("abcdef"));
}

void TestEditorView::wordNavigationAndDeletion()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("auto value = some_name(42);"));
    press(editor, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 4);
    press(editor, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 10);
    press(editor, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 22); // past "= some_name"
    press(editor, Qt::Key_Backspace, Qt::AltModifier);
    QCOMPARE(editor.text(), QStringLiteral("auto value = (42);"));
    press(editor, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 5);
}

void TestEditorView::verticalMovesKeepColumn()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("a long line\nab\nanother long line"));
    editor.setCursorPosition(8);
    press(editor, Qt::Key_Down);
    QCOMPARE(editor.cursorLine(), 1);
    QCOMPARE(editor.cursorColumn(), 2); // the short line's end
    press(editor, Qt::Key_Down);
    QCOMPARE(editor.cursorLine(), 2);
    QCOMPARE(editor.cursorColumn(), 8); // back to the column it came from
}

void TestEditorView::inputMethodCommitAndReplacement()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("cafe"));
    editor.setCursorPosition(4);
    // A composition in progress changes nothing in the text.
    QInputMethodEvent compose{QStringLiteral("´"), {}};
    QCoreApplication::sendEvent(&editor, &compose);
    QCOMPARE(editor.text(), QStringLiteral("cafe"));
    // Press and hold: the accent replaces the character before the caret.
    QInputMethodEvent accent;
    accent.setCommitString(QStringLiteral("é"), -1, 1);
    QCoreApplication::sendEvent(&editor, &accent);
    QCOMPARE(editor.text(), QStringLiteral("café"));
    QCOMPARE(editor.cursorPosition(), 4);
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral(" au lait"));
    QCoreApplication::sendEvent(&editor, &commit);
    QCOMPARE(editor.text(), QStringLiteral("café au lait"));
}

QTEST_MAIN(TestEditorView)
