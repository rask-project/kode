#include "tst_editorview.h"

#include "EditorView.h"
#include "TextDocument.h"

#include <QAccessible>
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

void TestEditorView::accessibleText()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("first line\nsecond words here"));
    QAccessibleInterface *const accessible{QAccessible::queryAccessibleInterface(&editor)};
    QVERIFY(accessible != nullptr);
    QCOMPARE(accessible->role(), QAccessible::EditableText);
    QVERIFY(accessible->state().multiLine);
    QAccessibleTextInterface *const text{accessible->textInterface()};
    QVERIFY(text != nullptr);
    QCOMPARE(text->characterCount(), 28);
    QCOMPARE(text->text(0, 5), QStringLiteral("first"));
}

void TestEditorView::accessibleSelection()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("first line\nsecond words here"));
    QAccessibleTextInterface *const text{QAccessible::queryAccessibleInterface(&editor)->textInterface()};
    text->setSelection(0, 0, 5);
    QCOMPARE(editor.selectedText(), QStringLiteral("first"));
    QCOMPARE(text->selectionCount(), 1);
    text->setCursorPosition(4);
    QCOMPARE(editor.cursorPosition(), 4);
    QCOMPARE(text->selectionCount(), 0);
}

void TestEditorView::accessibleBoundaries()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("first line\nsecond words here"));
    QAccessibleTextInterface *const text{QAccessible::queryAccessibleInterface(&editor)->textInterface()};
    int start{-1};
    int end{-1};
    QCOMPARE(text->textAtOffset(13, QAccessible::WordBoundary, &start, &end), QStringLiteral("second"));
    QCOMPARE(start, 11);
    QCOMPARE(end, 17);
    QCOMPARE(text->textAtOffset(3, QAccessible::LineBoundary, &start, &end), QStringLiteral("first line\n"));
    QCOMPARE(text->textAfterOffset(3, QAccessible::LineBoundary, &start, &end), QStringLiteral("second words here"));
    QCOMPARE(text->textBeforeOffset(13, QAccessible::LineBoundary, &start, &end), QStringLiteral("first line\n"));
}

void TestEditorView::accessibleEditingIsUndoable()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("hello"));
    QAccessibleEditableTextInterface *const editable{
        QAccessible::queryAccessibleInterface(&editor)->editableTextInterface()
    };
    QVERIFY(editable != nullptr);
    editable->insertText(5, QStringLiteral(" world"));
    editable->replaceText(0, 5, QStringLiteral("HELLO"));
    QCOMPARE(editor.text(), QStringLiteral("HELLO world"));
    editor.undo();
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("hello"));
}

void TestEditorView::findNextSelectsMatches()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("one two one\none"));
    editor.setSearchPattern(QStringLiteral("one"));
    QCOMPARE(editor.searchMatchCount(), 3);
    QVERIFY(editor.searchComplete());
    QVERIFY(editor.findNext());
    QCOMPARE(editor.searchCurrent(), 0);
    QVERIFY(editor.findNext());
    QCOMPARE(editor.selectionStart(), 8);
    QCOMPARE(editor.searchCurrent(), 1);
}

void TestEditorView::findWraps()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("one two one\none"));
    editor.setSearchPattern(QStringLiteral("one"));
    editor.select(12, 15);
    QCOMPARE(editor.searchCurrent(), 2);
    QVERIFY(editor.findNext());
    QCOMPARE(editor.searchCurrent(), 0);
    QVERIFY(editor.findPrevious());
    QCOMPARE(editor.searchCurrent(), 2);
}

void TestEditorView::replaceAllIsOneUndo()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("a1 b2\nc3"));
    editor.setSearchRegularExpression(true);
    editor.setSearchPattern(QStringLiteral("([a-z])(\\d)"));
    QCOMPARE(editor.replaceAll(QStringLiteral("$2$1")), 3);
    QCOMPARE(editor.text(), QStringLiteral("1a 2b\n3c"));
    QCOMPARE(editor.searchMatchCount(), 0);
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("a1 b2\nc3"));
    QCOMPARE(editor.searchMatchCount(), 3);
}

void TestEditorView::goesToLineAndColumn()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("first\nsecond\nthird"));
    QVERIFY(editor.goToLine(QStringLiteral("2")));
    QCOMPARE(editor.cursorPosition(), 6);
    QVERIFY(editor.goToLine(QStringLiteral(" 3:4 ")));
    QCOMPARE(editor.cursorPosition(), 16);
    // Clamped to the text.
    QVERIFY(editor.goToLine(QStringLiteral("99:99")));
    QCOMPARE(editor.cursorPosition(), editor.length());
    QVERIFY(!editor.goToLine(QStringLiteral("two")));
    QVERIFY(!editor.goToLine(QStringLiteral("1:2:3")));
}

void TestEditorView::returnKeepsIndentation()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("    if (x) {"));
    editor.setCursorPosition(editor.length());
    QKeyEvent enter{QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r")};
    QCoreApplication::sendEvent(&editor, &enter);
    QCOMPARE(editor.text(), QStringLiteral("    if (x) {\n    "));
    QKeyEvent tab{QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t")};
    QCoreApplication::sendEvent(&editor, &tab);
    QCOMPARE(editor.text(), QStringLiteral("    if (x) {\n        "));
}

void TestEditorView::tabIndentsSelectedLines()
{
    tekst::EditorView editor;
    editor.setText(QStringLiteral("a\nb\nc"));
    editor.setIndentWidth(2);
    editor.select(0, 3);
    QKeyEvent tab{QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t")};
    QCoreApplication::sendEvent(&editor, &tab);
    QCOMPARE(editor.text(), QStringLiteral("  a\n  b\nc"));
    QCOMPARE(editor.selectedText(), QStringLiteral("  a\n  b"));
    editor.outdent();
    QCOMPARE(editor.text(), QStringLiteral("a\nb\nc"));
    editor.undo();
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("a\nb\nc"));
}

void TestEditorView::wrappedLinesAddRowsAndMoveByRow()
{
    tekst::EditorView editor;
    // Narrow: lines wrap at the fewest columns (20).
    editor.setWidth(10.0);
    editor.setText(QString(45, QLatin1Char('a')) + QStringLiteral("\nb"));
    const qreal unwrapped{editor.contentHeight()};
    editor.setWrap(true);
    const qreal rowHeight{editor.cursorRectangle().height()};
    const qreal firstRow{editor.cursorRectangle().y()};
    QCOMPARE(editor.contentHeight(), unwrapped + (2 * rowHeight));
    QCOMPARE(editor.contentWidth(), editor.width());

    QKeyEvent down{QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier};
    QCoreApplication::sendEvent(&editor, &down);
    QCOMPARE(editor.cursorLine(), 0);
    QVERIFY(editor.cursorPosition() > 15 && editor.cursorPosition() <= 20);
    QCOMPARE(editor.cursorRectangle().y(), firstRow + rowHeight);
    QCoreApplication::sendEvent(&editor, &down);
    QCoreApplication::sendEvent(&editor, &down);
    QCOMPARE(editor.cursorLine(), 1);
}

void TestEditorView::wrappingFollowsEditsAndNewText()
{
    tekst::EditorView editor;
    editor.setWidth(10.0);
    editor.setText(QStringLiteral("b"));
    const qreal oneRow{editor.contentHeight()};
    const qreal rowHeight{editor.cursorRectangle().height()};
    const qreal unwrapped{oneRow + rowHeight};
    editor.setWrap(true);

    // Wrapping survives replacing the text (opening a file).
    editor.setText(QString(45, QLatin1Char('a')) + QStringLiteral("\nb"));
    QVERIFY(editor.wrap());
    QCOMPARE(editor.contentHeight(), unwrapped + (2 * rowHeight));

    // An edit that wraps one more row.
    editor.setCursorPosition(0);
    editor.insert(QString(20, QLatin1Char('b')));
    QCOMPARE(editor.contentHeight(), unwrapped + (3 * rowHeight));
}

void TestEditorView::relayoutKeepsTheTopLine()
{
    tekst::EditorView editor;
    editor.setWidth(10.0);
    editor.setText(QString(45, QLatin1Char('a')).append(QLatin1Char('\n')).repeated(100));
    const qreal top{editor.cursorRectangle().y()};
    const qreal rowHeight{editor.cursorRectangle().height()};
    // Line 50 at the top, the caret far above it.
    editor.setContentY(top + (50 * rowHeight));
    const QSignalSpy scrolls{&editor, &tekst::EditorView::scrollRequested};
    const QSignalSpy caretMoves{&editor, &tekst::EditorView::caretMoved};
    editor.setWrap(true);
    // Each line now takes three rows; the view follows line 50, not the caret.
    QCOMPARE(scrolls.count(), 1);
    QCOMPARE(scrolls.first().first().toReal(), top + (150 * rowHeight));
    QCOMPARE(caretMoves.count(), 0);
}

void TestEditorView::newTextStartsWithTheDefaultIndentation()
{
    tekst::EditorView editor;
    editor.setDefaultIndentWithTabs(true);
    editor.setDefaultIndentWidth(8);
    editor.setIndentWithTabs(false);
    editor.setIndentWidth(2);
    editor.setText(QStringLiteral("no indentation here"));
    QVERIFY(editor.indentWithTabs());
    QCOMPARE(editor.indentWidth(), 8);
    // A file's own indentation wins over the default.
    editor.setText(QStringLiteral("a {\n  b\n}\n"));
    editor.detectIndentation();
    QVERIFY(!editor.indentWithTabs());
    QCOMPARE(editor.indentWidth(), 2);
}

void TestEditorView::viewsShareADocument()
{
    tekst::TextDocument document;
    tekst::EditorView first;
    tekst::EditorView second;
    first.setDocument(&document);
    second.setDocument(&document);
    document.setText(QStringLiteral("hello world"));
    second.setCursorPosition(6);
    const QSignalSpy secondModified{&second, &tekst::EditorView::modifiedChanged};
    first.setCursorPosition(0);
    type(first, QStringLiteral(">> "));
    QCOMPARE(second.text(), QStringLiteral(">> hello world"));
    // The other view's caret stays on "world".
    QCOMPARE(second.cursorPosition(), 9);
    QCOMPARE(first.cursorPosition(), 3);
    QVERIFY(second.modified());
    QCOMPARE(secondModified.count(), 1);
}

void TestEditorView::undoInOneViewReachesTheOther()
{
    tekst::TextDocument document;
    tekst::EditorView first;
    tekst::EditorView second;
    first.setDocument(&document);
    second.setDocument(&document);
    document.setText(QStringLiteral("text"));
    first.setCursorPosition(4);
    type(first, QStringLiteral("!"));
    QVERIFY(second.canUndo());
    second.undo();
    QCOMPARE(first.text(), QStringLiteral("text"));
    QVERIFY(!first.modified());
    QCOMPARE(second.cursorPosition(), 4);
}

void TestEditorView::switchingDocumentsKeepsEachPlace()
{
    tekst::TextDocument one;
    tekst::TextDocument two;
    one.setText(QStringLiteral("first document"));
    two.setText(QStringLiteral("second"));
    tekst::EditorView view;
    view.setDocument(&one);
    view.setCursorPosition(6);
    view.setDocument(&two);
    QCOMPARE(view.text(), QStringLiteral("second"));
    QCOMPARE(view.cursorPosition(), 0);
    view.setDocument(&one);
    QCOMPARE(view.cursorPosition(), 6);
    // Deleting the document shown leaves the view with its own, empty one.
    {
        tekst::TextDocument gone;
        gone.setText(QStringLiteral("gone"));
        view.setDocument(&gone);
    }
    QVERIFY(view.text().isEmpty());
}

QTEST_MAIN(TestEditorView)
