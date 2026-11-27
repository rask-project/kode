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
    kode::EditorView &editor,
    Qt::Key key,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier,
    const QString &text = {}
)
{
    QKeyEvent event{QEvent::KeyPress, key, modifiers, text};
    QCoreApplication::sendEvent(&editor, &event);
}

void type(kode::EditorView &editor, const QString &text)
{
    for (const QChar character : text) {
        press(editor, Qt::Key_unknown, Qt::NoModifier, QString{character});
    }
}

} // namespace

void TestEditorView::typingMergesIntoOneUndo()
{
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
    type(editor, QStringLiteral("abc"));
    editor.undo();
    type(editor, QStringLiteral("x"));
    QVERIFY(!editor.canRedo());
    QCOMPARE(editor.text(), QStringLiteral("x"));
}

void TestEditorView::modifiedFollowsSavedState()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("saved"));
    const QSignalSpy spy{&editor, &kode::EditorView::modifiedChanged};
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
    editor.setText(QStringLiteral("auto value = some_name(42);"));
    press(editor, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 4);
    press(editor, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 10);
    // Punctuation is a stop of its own, as in VS Code and Qt Creator.
    press(editor, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 12);
    press(editor, Qt::Key_Right, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 22);
    press(editor, Qt::Key_Backspace, Qt::AltModifier);
    QCOMPARE(editor.text(), QStringLiteral("auto value = (42);"));
    press(editor, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 11);
    press(editor, Qt::Key_Left, Qt::AltModifier);
    QCOMPARE(editor.cursorPosition(), 5);
    // Each caret stops at the punctuation, not at the next word further on
    // (a space inside the quotes is skipped as a space).
    editor.setText(QStringLiteral("['E', 'L']\n[' ', 'Z']"));
    press(editor, Qt::Key_Down, Qt::AltModifier);
    press(editor, Qt::Key_Right, Qt::AltModifier);
    press(editor, Qt::Key_Right, Qt::AltModifier);
    type(editor, QStringLiteral("|"));
    QCOMPARE(editor.text(), QStringLiteral("['E|', 'L']\n[' ',| 'Z']"));
}

void TestEditorView::verticalMovesKeepColumn()
{
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
    editor.setText(QStringLiteral("    foo();"));
    editor.setCursorPosition(editor.length());
    press(editor, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCOMPARE(editor.text(), QStringLiteral("    foo();\n    "));
    // A level more after a bracket left open.
    editor.setText(QStringLiteral("    if (x) {"));
    editor.setCursorPosition(editor.length());
    press(editor, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCOMPARE(editor.text(), QStringLiteral("    if (x) {\n        "));
    // Between brackets, the closing one goes on a line of its own.
    editor.setText(QStringLiteral("f() {}"));
    editor.setCursorPosition(5);
    press(editor, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCOMPARE(editor.text(), QStringLiteral("f() {\n    \n}"));
    QCOMPARE(editor.cursorLine(), 1);
    QCOMPARE(editor.cursorColumn(), 4);
}

void TestEditorView::closingBracketGoesUnderItsOpening()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("    if (x) {\n        a();\n        "));
    editor.setCursorPosition(editor.length());
    type(editor, QStringLiteral("}"));
    QCOMPARE(editor.text(), QStringLiteral("    if (x) {\n        a();\n    }"));
    QCOMPARE(editor.cursorColumn(), 5);
    // Not after other text.
    type(editor, QStringLiteral(")"));
    QCOMPARE(editor.text(), QStringLiteral("    if (x) {\n        a();\n    })"));
}

void TestEditorView::reindentsByBrackets()
{
    kode::EditorView editor;
    editor.setSyntax(QStringLiteral("C++"));
    editor.setText(QStringLiteral("void f() {\nif (a) {\nx = \"{\";   // {\n}\n\n  /* a\n   * b */\nfoo(1,\n2);\n}"));
    editor.selectAll();
    editor.reindent();
    // Brackets in strings and comments don't count; a comment's lines move with it.
    QCOMPARE(
        editor.text(),
        QStringLiteral(
            "void f() {\n    if (a) {\n        x = \"{\";   // {\n    }\n\n    /* a\n     * b */\n    foo(1,\n        "
            "2);\n}"
        )
    );
    // One line: the caret stays on its text.
    editor.setText(QStringLiteral("{\nab\n}"));
    editor.setCursorPosition(3);
    editor.reindent();
    QCOMPARE(editor.text(), QStringLiteral("{\n    ab\n}"));
    QCOMPARE(editor.cursorColumn(), 5);
}

void TestEditorView::reindentsEditedLinesKeepingTheCaret()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("a {\nb {\nc;\n}\n}"));
    // Nothing edited: nothing moves.
    editor.reindentEdited();
    QVERIFY(!editor.canUndo());
    editor.setCursorPosition(editor.text().indexOf(QLatin1Char('c')) + 1);
    editor.insert(QStringLiteral("x"));
    editor.reindentEdited();
    // Only the edited line, under the line above it.
    QCOMPARE(editor.text(), QStringLiteral("a {\nb {\n    cx;\n}\n}"));
    QCOMPARE(editor.cursorLine(), 2);
    QCOMPARE(editor.cursorColumn(), 6);
    // One undo.
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("a {\nb {\ncx;\n}\n}"));
}

void TestEditorView::reindentsManyEditedLinesAtOnce()
{
    // More lines than are changed one by one: one replacement, one undo.
    kode::EditorView editor;
    editor.setText(QStringLiteral("{\n}"));
    editor.setCursorPosition(2);
    const QString pasted{QStringLiteral("x;\n").repeated(1500)};
    editor.insert(pasted);
    editor.reindentEdited();
    QCOMPARE(editor.text(), QStringLiteral("{\n") + QStringLiteral("    x;\n").repeated(1500) + QStringLiteral("}"));
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("{\n") + pasted + QStringLiteral("}"));
}

void TestEditorView::followsEditedLinesAcrossEdits()
{
    kode::EditorView editor;
    editor.setSyntax(QStringLiteral("C++"));
    editor.setText(QStringLiteral("a {\n  b;\n}\nd {\n  c;\n}"));
    editor.setCursorPosition(editor.text().indexOf(QStringLiteral("c;")) + 2);
    editor.insert(QStringLiteral("x"));
    // A line added above moves the edited one down.
    editor.setCursorPosition(3);
    editor.insert(QStringLiteral("\n"));
    editor.reindentEdited();
    QCOMPARE(editor.text(), QStringLiteral("a {\n\n  b;\n}\nd {\n    c;x\n}"));
    // Saved: nothing edited any more.
    editor.document()->markSaved();
    editor.reindentEdited();
    QCOMPARE(editor.text(), QStringLiteral("a {\n\n  b;\n}\nd {\n    c;x\n}"));
}

void TestEditorView::colonOpensABlockInPython()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("def f():"));
    editor.setCursorPosition(editor.length());
    press(editor, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCOMPARE(editor.text(), QStringLiteral("def f():\n"));
    editor.setSyntax(QStringLiteral("Python"));
    editor.setText(QStringLiteral("def f():  # x"));
    editor.setCursorPosition(editor.length());
    press(editor, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    QCOMPARE(editor.text(), QStringLiteral("def f():  # x\n    "));
    // Python's indentation is its own: Re-Indent leaves it.
    editor.setText(QStringLiteral("if a:\nb"));
    editor.selectAll();
    editor.reindent();
    QCOMPARE(editor.text(), QStringLiteral("if a:\nb"));
}

void TestEditorView::tabIndentsSelectedLines()
{
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
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
    kode::EditorView editor;
    editor.setWidth(10.0);
    editor.setText(QString(45, QLatin1Char('a')).append(QLatin1Char('\n')).repeated(100));
    const qreal top{editor.cursorRectangle().y()};
    const qreal rowHeight{editor.cursorRectangle().height()};
    // Line 50 at the top, the caret far above it.
    editor.setContentY(top + (50 * rowHeight));
    const QSignalSpy scrolls{&editor, &kode::EditorView::scrollRequested};
    const QSignalSpy caretMoves{&editor, &kode::EditorView::caretMoved};
    editor.setWrap(true);
    // Each line now takes three rows; the view follows line 50, not the caret.
    QCOMPARE(scrolls.count(), 1);
    QCOMPARE(scrolls.first().first().toReal(), top + (150 * rowHeight));
    QCOMPARE(caretMoves.count(), 0);
}

void TestEditorView::newTextStartsWithTheDefaultIndentation()
{
    kode::EditorView editor;
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
    kode::TextDocument document;
    kode::EditorView first;
    kode::EditorView second;
    first.setDocument(&document);
    second.setDocument(&document);
    document.setText(QStringLiteral("hello world"));
    second.setCursorPosition(6);
    const QSignalSpy secondModified{&second, &kode::EditorView::modifiedChanged};
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
    kode::TextDocument document;
    kode::EditorView first;
    kode::EditorView second;
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
    kode::TextDocument one;
    kode::TextDocument two;
    one.setText(QStringLiteral("first document"));
    two.setText(QStringLiteral("second"));
    kode::EditorView view;
    view.setDocument(&one);
    view.setCursorPosition(6);
    view.setDocument(&two);
    QCOMPARE(view.text(), QStringLiteral("second"));
    QCOMPARE(view.cursorPosition(), 0);
    view.setDocument(&one);
    QCOMPARE(view.cursorPosition(), 6);
    // Deleting the document shown leaves the view with its own, empty one.
    {
        kode::TextDocument gone;
        gone.setText(QStringLiteral("gone"));
        view.setDocument(&gone);
    }
    QVERIFY(view.text().isEmpty());
}

void TestEditorView::optionArrowsAddCaretsAboveAndBelow()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("abcd\nabcd\nab\nabcd"));
    editor.setCursorPosition(3);
    press(editor, Qt::Key_Down, Qt::AltModifier);
    press(editor, Qt::Key_Down, Qt::AltModifier);
    press(editor, Qt::Key_Down, Qt::AltModifier);
    // Past the last line: nothing more.
    press(editor, Qt::Key_Down, Qt::AltModifier);
    QCOMPARE(editor.caretCount(), 4);
    // The short line gets its end; the next one the column again.
    type(editor, QStringLiteral("X"));
    QCOMPARE(editor.text(), QStringLiteral("abcXd\nabcXd\nabX\nabcXd"));
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("abcd\nabcd\nab\nabcd"));
    editor.setCursorPosition(6);
    press(editor, Qt::Key_Up, Qt::AltModifier);
    press(editor, Qt::Key_Up, Qt::AltModifier);
    QCOMPARE(editor.caretCount(), 2);
    type(editor, QStringLiteral("<"));
    QCOMPARE(editor.text(), QStringLiteral("a<bcd\na<bcd\nab\nabcd"));
}

void TestEditorView::optionShiftArrowsAddBlockSelections()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("abcdef\nabcdef\nabcdef"));
    editor.select(1, 3);
    press(editor, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    press(editor, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    type(editor, QStringLiteral("Z"));
    QCOMPARE(editor.text(), QStringLiteral("aZdef\naZdef\naZdef"));
    // From carets, ⇧ extends each selection: to the line's end with ⌘.
    editor.setCursorPosition(2);
    press(editor, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    press(editor, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    press(editor, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
    type(editor, QStringLiteral("!"));
    QCOMPARE(editor.text(), QStringLiteral("aZ!\naZ!\naZ!"));
    // Home and End move every caret (⇧: selecting).
    press(editor, Qt::Key_Home);
    type(editor, QStringLiteral("["));
    press(editor, Qt::Key_End);
    type(editor, QStringLiteral("]"));
    QCOMPARE(editor.text(), QStringLiteral("[aZ!]\n[aZ!]\n[aZ!]"));
    press(editor, Qt::Key_Home, Qt::ShiftModifier);
    type(editor, QStringLiteral("-"));
    QCOMPARE(editor.text(), QStringLiteral("-\n-\n-"));
}

namespace
{

// Lines: 0 int main(), 1 {, 2 if (x) {, 3 a();, 4 b();, 5 }, 6 return 0;, 7 }.
const QString cppSource{
    QStringLiteral("int main()\n{\n    if (x) {\n        a();\n        b();\n    }\n    return 0;\n}\n")
};

} // namespace

void TestEditorView::foldsTheBlockAroundTheCaret()
{
    kode::EditorView editor;
    editor.setSyntax(QStringLiteral("C++"));
    editor.setText(cppSource);
    const qreal height{editor.contentHeight()};
    editor.setCursorPosition(editor.text().indexOf(QStringLiteral("b();")));
    editor.fold();
    // a() and b() hidden; the caret on the if, at its end; "}" still shown.
    QCOMPARE(editor.cursorLine(), 2);
    QCOMPARE(editor.cursorColumn(), 12);
    const qreal folded{editor.contentHeight()};
    QVERIFY(folded < height);
    // Folding again from there folds the function's body around it.
    editor.fold();
    QVERIFY(editor.contentHeight() < folded);
    QCOMPARE(editor.cursorLine(), 1);
    editor.unfoldAll();
    QCOMPARE(editor.contentHeight(), height);
}

void TestEditorView::selectsGrowingBlocks()
{
    kode::EditorView editor;
    editor.setSyntax(QStringLiteral("C++"));
    editor.setText(cppSource);
    editor.setCursorPosition(editor.text().indexOf(QStringLiteral("b();")));
    editor.selectBlock();
    QCOMPARE(editor.selectedText(), QStringLiteral("        a();\n        b();"));
    editor.selectBlock();
    QCOMPARE(editor.selectedText(), QStringLiteral("    if (x) {\n        a();\n        b();\n    }"));
    editor.selectBlock();
    QVERIFY(editor.selectedText().startsWith(QStringLiteral("    if (x) {")));
    QVERIFY(editor.selectedText().endsWith(QStringLiteral("return 0;")));
    editor.selectBlock();
    QCOMPARE(editor.selectedText(), cppSource.chopped(1));
    // Back down the same steps, to the caret.
    editor.unselectBlock();
    QVERIFY(editor.selectedText().endsWith(QStringLiteral("return 0;")));
    editor.unselectBlock();
    editor.unselectBlock();
    QCOMPARE(editor.selectedText(), QStringLiteral("        a();\n        b();"));
    editor.unselectBlock();
    QVERIFY(!editor.hasSelection());
    QCOMPARE(editor.cursorPosition(), editor.text().indexOf(QStringLiteral("b();")));
}

void TestEditorView::togglesCommentsByTheLanguage()
{
    kode::EditorView editor;
    editor.setSyntax(QStringLiteral("C++"));
    editor.setText(cppSource);
    const qsizetype a{editor.text().indexOf(QStringLiteral("a();"))};
    editor.select(a, editor.text().indexOf(QStringLiteral("b();")) + 4);
    editor.toggleLineComment();
    QVERIFY(editor.text().contains(QStringLiteral("        // a();\n        // b();\n")));
    editor.undo();
    QCOMPARE(editor.text(), cppSource);
    editor.redo();
    editor.select(editor.text().indexOf(QStringLiteral("// a();")), editor.text().indexOf(QStringLiteral("b();")) + 4);
    editor.toggleLineComment();
    QCOMPARE(editor.text(), cppSource);
    // A block around the selection.
    editor.select(a, a + 3);
    editor.toggleBlockComment();
    QVERIFY(editor.text().contains(QStringLiteral("/* a() */;")));
    editor.toggleBlockComment();
    QCOMPARE(editor.text(), cppSource);

    // Python has no block comments: the line's.
    editor.setSyntax(QStringLiteral("Python"));
    editor.setText(QStringLiteral("if x:\n    y = 1\n"));
    editor.setCursorPosition(8);
    editor.toggleBlockComment();
    QCOMPARE(editor.text(), QStringLiteral("if x:\n    # y = 1\n"));
}

void TestEditorView::foldsTheBlockStartingOnTheCaretsLine()
{
    kode::EditorView editor;
    editor.setSyntax(QStringLiteral("C++"));
    editor.setText(cppSource);
    const qreal height{editor.contentHeight()};
    editor.setCursorPosition(editor.text().indexOf(QStringLiteral("b();")));
    editor.fold();
    const qreal folded{editor.contentHeight()};
    editor.unfoldAll();
    editor.setCursorPosition(editor.text().indexOf(QStringLiteral("if")));
    editor.fold();
    QCOMPARE(editor.contentHeight(), folded);
    QCOMPARE(editor.cursorLine(), 2);
    editor.unfold();
    QCOMPARE(editor.contentHeight(), height);
}

void TestEditorView::movesPastFoldsAndOpensThemForEdits()
{
    kode::EditorView editor;
    editor.setSyntax(QStringLiteral("C++"));
    editor.setText(cppSource);
    const qreal height{editor.contentHeight()};
    editor.setCursorPosition(editor.text().indexOf(QStringLiteral("if")));
    editor.fold();
    // ↓ and → go past the hidden lines, ← comes back.
    press(editor, Qt::Key_Down);
    QCOMPARE(editor.cursorLine(), 5);
    press(editor, Qt::Key_Up);
    QCOMPARE(editor.cursorLine(), 2);
    press(editor, Qt::Key_End);
    press(editor, Qt::Key_Right);
    QCOMPARE(editor.cursorLine(), 5);
    QCOMPARE(editor.cursorColumn(), 0);
    press(editor, Qt::Key_Left);
    QCOMPARE(editor.cursorLine(), 2);
    // Two lines hidden. A line added above moves the fold (one line more
    // shown); going to a hidden line opens it.
    const qreal folded{editor.contentHeight()};
    const qreal lineHeight{(height - folded) / 2.0};
    editor.setCursorPosition(0);
    press(editor, Qt::Key_Return);
    QCOMPARE(editor.contentHeight(), folded + lineHeight);
    QVERIFY(editor.goToLine(QStringLiteral("5")));
    QCOMPARE(editor.contentHeight(), height + lineHeight);
}

void TestEditorView::foldsByIndentation()
{
    kode::EditorView editor;
    editor.setSyntax(QStringLiteral("Python"));
    editor.setText(QStringLiteral("def f():\n    a = 1\n\n    return a\nx = f()\n"));
    const qreal height{editor.contentHeight()};
    editor.fold();
    QCOMPARE(editor.cursorLine(), 0);
    QVERIFY(editor.contentHeight() < height);
    press(editor, Qt::Key_Down);
    QCOMPARE(editor.cursorLine(), 4);
}

QTEST_MAIN(TestEditorView)

void TestEditorView::nextOccurrenceSelectsTheWordThenTheNextOne()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("let foo = foo + bar(foo);"));
    editor.setCursorPosition(5);
    editor.selectNextOccurrence();
    QCOMPARE(editor.selectedText(), QStringLiteral("foo"));
    QCOMPARE(editor.caretCount(), 1);
    editor.selectNextOccurrence();
    QCOMPARE(editor.caretCount(), 2);
    QCOMPARE(editor.selectionStart(), 10);
    editor.selectNextOccurrence();
    QCOMPARE(editor.caretCount(), 3);
    QCOMPARE(editor.selectionStart(), 20);
    // None left: nothing changes.
    editor.selectNextOccurrence();
    QCOMPARE(editor.caretCount(), 3);
}

void TestEditorView::typingAtEveryOccurrenceIsOneUndo()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("let foo = foo + bar(foo);"));
    editor.setCursorPosition(4);
    editor.selectNextOccurrence();
    editor.selectNextOccurrence();
    editor.selectNextOccurrence();
    type(editor, QStringLiteral("value"));
    QCOMPARE(editor.text(), QStringLiteral("let value = value + bar(value);"));
    QCOMPARE(editor.caretCount(), 3);
    QCOMPARE(editor.cursorPosition(), 29);
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("let foo = foo + bar(foo);"));
    QVERIFY(!editor.canUndo());
    editor.redo();
    QCOMPARE(editor.text(), QStringLiteral("let value = value + bar(value);"));
}

void TestEditorView::deletingAndMovingAtEveryCaret()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("ab ab ab"));
    editor.setCursorPosition(0);
    editor.selectNextOccurrence();
    editor.selectNextOccurrence();
    editor.selectNextOccurrence();
    // → collapses each selection to its end; Backspace then deletes the b's.
    press(editor, Qt::Key_Right);
    press(editor, Qt::Key_Backspace);
    QCOMPARE(editor.text(), QStringLiteral("a a a"));
    press(editor, Qt::Key_Left);
    type(editor, QStringLiteral("<"));
    QCOMPARE(editor.text(), QStringLiteral("<a <a <a"));
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("a a a"));
    editor.undo();
    QCOMPARE(editor.text(), QStringLiteral("ab ab ab"));
}

void TestEditorView::fromACaretOnlyWholeWords()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("item items item"));
    editor.setCursorPosition(1);
    editor.selectNextOccurrence();
    editor.selectNextOccurrence();
    // "items" is skipped: the word came from the caret.
    QCOMPARE(editor.selectionStart(), 11);
    // A selection made by hand matches inside words too.
    editor.select(0, 4);
    editor.selectNextOccurrence();
    QCOMPARE(editor.selectionStart(), 5);
}

void TestEditorView::escapeOrAClickLeavesOneCaret()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("x x x"));
    editor.setCursorPosition(0);
    editor.selectNextOccurrence();
    editor.selectNextOccurrence();
    QCOMPARE(editor.caretCount(), 2);
    press(editor, Qt::Key_Escape);
    QCOMPARE(editor.caretCount(), 1);
    editor.selectNextOccurrence();
    QCOMPARE(editor.caretCount(), 2);
    press(editor, Qt::Key_Up);
    QCOMPARE(editor.caretCount(), 1);
    type(editor, QStringLiteral("y"));
    QCOMPARE(editor.text().count(QLatin1Char('y')), 1);
}

void TestEditorView::nextOccurrenceWrapsAndSkipsSelected()
{
    kode::EditorView editor;
    editor.setText(QStringLiteral("one\ntwo one\none"));
    editor.select(8, 11);
    editor.selectNextOccurrence();
    QCOMPARE(editor.selectionStart(), 12);
    // Around the end: the first one.
    editor.selectNextOccurrence();
    QCOMPARE(editor.selectionStart(), 0);
    QCOMPARE(editor.caretCount(), 3);
    editor.selectNextOccurrence();
    QCOMPARE(editor.caretCount(), 3);
    type(editor, QStringLiteral("1"));
    QCOMPARE(editor.text(), QStringLiteral("1\ntwo 1\n1"));
}
