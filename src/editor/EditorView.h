#pragma once

#include "TextBuffer.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFont>
#include <QQuickItem>
#include <QRectF>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <vector>

class QTextLayout;

namespace tekst
{

class EditorAccessible;

// The editing surface: a TextBuffer drawn by laying out and rendering only
// the lines in view (one QSGTextNode per line and per line number), with
// caret, selection, undo, the clipboard and input methods. Monospaced
// layout, no wrapping. It sits in a Flickable, filling its viewport:
// `contentWidth`/`contentHeight` size the Flickable, `contentX`/`contentY`
// say which part is in view; `cursorRectangle` is what to keep visible.
class EditorView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor selectionColor READ selectionColor WRITE setSelectionColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor caretColor READ caretColor WRITE setCaretColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor currentLineColor READ currentLineColor WRITE setCurrentLineColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor lineNumberColor READ lineNumberColor WRITE setLineNumberColor NOTIFY colorsChanged)
    Q_PROPERTY(qreal contentX READ contentX WRITE setContentX NOTIFY contentXChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentSizeChanged)
    // Width of the line-number gutter, which does not scroll horizontally.
    Q_PROPERTY(qreal gutterWidth READ gutterWidth NOTIFY contentSizeChanged)
    Q_PROPERTY(qint32 lineCount READ lineCount NOTIFY contentSizeChanged)
    Q_PROPERTY(qint64 length READ length NOTIFY textChanged)
    Q_PROPERTY(qint64 cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY cursorChanged)
    Q_PROPERTY(qint32 cursorLine READ cursorLine NOTIFY cursorChanged)
    Q_PROPERTY(qint32 cursorColumn READ cursorColumn NOTIFY cursorChanged)
    // The caret, in content coordinates (gutter included).
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle NOTIFY cursorChanged)
    Q_PROPERTY(qint64 selectionStart READ selectionStart NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectionEnd READ selectionEnd NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged)
    // Changed since setText() or markSaved(); undoing back clears it.
    Q_PROPERTY(bool modified READ modified NOTIFY modifiedChanged)
public:
    explicit EditorView(QQuickItem *parent = nullptr);
    ~EditorView() override;
    Q_DISABLE_COPY_MOVE(EditorView)

    [[nodiscard]] QFont font() const { return m_font; }
    void setFont(const QFont &font);
    [[nodiscard]] QColor textColor() const { return QColor::fromRgba(m_colors.text); }
    void setTextColor(const QColor &color);
    [[nodiscard]] QColor selectionColor() const { return QColor::fromRgba(m_colors.selection); }
    void setSelectionColor(const QColor &color);
    [[nodiscard]] QColor caretColor() const { return QColor::fromRgba(m_colors.caret); }
    void setCaretColor(const QColor &color);
    [[nodiscard]] QColor currentLineColor() const { return QColor::fromRgba(m_colors.currentLine); }
    void setCurrentLineColor(const QColor &color);
    [[nodiscard]] QColor lineNumberColor() const { return QColor::fromRgba(m_colors.lineNumber); }
    void setLineNumberColor(const QColor &color);
    [[nodiscard]] qreal contentX() const noexcept { return m_contentX; }
    void setContentX(qreal contentX);
    [[nodiscard]] qreal contentY() const noexcept { return m_contentY; }
    void setContentY(qreal contentY);
    [[nodiscard]] qreal contentWidth() const;
    [[nodiscard]] qreal contentHeight() const;
    [[nodiscard]] qreal gutterWidth() const;
    [[nodiscard]] qint32 lineCount() const { return static_cast<qint32>(m_buffer.lineCount()); }
    [[nodiscard]] qint64 length() const { return m_buffer.length(); }
    [[nodiscard]] qint64 cursorPosition() const noexcept { return m_cursor; }
    void setCursorPosition(qint64 position);
    [[nodiscard]] qint32 cursorLine() const;
    [[nodiscard]] qint32 cursorColumn() const;
    [[nodiscard]] QRectF cursorRectangle() const;
    [[nodiscard]] qint64 selectionStart() const noexcept { return std::min(m_anchor, m_cursor); }
    [[nodiscard]] qint64 selectionEnd() const noexcept { return std::max(m_anchor, m_cursor); }
    [[nodiscard]] bool hasSelection() const noexcept { return m_anchor != m_cursor; }
    [[nodiscard]] bool canUndo() const noexcept { return m_undoIndex > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return m_undoIndex < m_undo.size(); }
    [[nodiscard]] static bool canPaste();
    [[nodiscard]] bool modified() const noexcept { return m_undoIndex != m_cleanIndex; }

    // The whole text, lines ending with '\n'.
    Q_INVOKABLE [[nodiscard]] QString text() const { return m_buffer.toString(); }
    // Replaces everything: no undo history, not modified, caret at the start.
    Q_INVOKABLE void setText(const QString &text);
    Q_INVOKABLE [[nodiscard]] QString selectedText() const;
    // Replaces the selection (or inserts at the caret), as typing does.
    Q_INVOKABLE void insert(const QString &text);
    Q_INVOKABLE void select(qint64 start, qint64 end);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void copy() const;
    Q_INVOKABLE void paste();
    // The text now matches the file: no longer modified.
    Q_INVOKABLE void markSaved();

signals:
    void fontChanged();
    void colorsChanged();
    void contentXChanged();
    void contentYChanged();
    void contentSizeChanged();
    void textChanged();
    void cursorChanged();
    void selectionChanged();
    void undoStateChanged();
    void canPasteChanged();
    void modifiedChanged();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    friend class EditorAccessible;

    // One undoable change: at `position`, `removed` was replaced by
    // `inserted`; the caret and anchor before and the caret after.
    struct Edit {
        qsizetype position{0};
        QString removed;
        QString inserted;
        qsizetype cursorBefore{0};
        qsizetype anchorBefore{0};
        qsizetype cursorAfter{0};
    };

    // A laid out line in view. Rows are found again by their text (and
    // `variant`), not their line number: an edit above a line moves it but
    // keeps its layout, and its scene graph node (matched by `id`).
    struct Row {
        quint64 id{0};
        qsizetype line{-1};
        // 0, or the preedit generation for the line showing a composition.
        quint64 variant{0};
        QString text;
        std::unique_ptr<QTextLayout> layout;
        qreal width{0.0};
    };

    // A laid out line number in view, found again by its line.
    struct Number {
        qsizetype line{-1};
        std::unique_ptr<QTextLayout> layout;
        qreal width{0.0};
    };

    // How a move treats the selection.
    enum class Select : quint8 {
        Collapse,
        Extend
    };

    struct Colors {
        QRgb text{0xff1d1d1fU};
        QRgb selection{0x553d8bffU};
        QRgb caret{0xff3d8bffU};
        QRgb currentLine{0x0a000000U};
        QRgb lineNumber{0x66000000U};
    };

    // The next typing may merge into the last edit.
    static constexpr quint32 MergeOpen{1U << 0U};

    void updateMetrics();
    void textEdited();
    // The one way the text changes: replaces `count` characters at
    // `position` with `text`, recorded for undo; typing merges into the
    // previous edit while `mergeable`.
    void replace(qsizetype position, qsizetype count, const QString &text, bool mergeable);
    void deleteSelection();
    void applyEdit(const Edit &edit, bool forward);
    void moveCaret(qsizetype position, Select select);
    void moveVertically(qsizetype lines, Select select);
    [[nodiscard]] qsizetype wordBoundary(qsizetype position, bool forward) const;
    [[nodiscard]] bool handleNavigation(const QKeyEvent *event);
    [[nodiscard]] bool handleDeletion(const QKeyEvent *event);
    [[nodiscard]] qsizetype firstVisibleLine() const;
    [[nodiscard]] qsizetype visibleLineCount() const;
    [[nodiscard]] QString displayText(qsizetype line) const;
    [[nodiscard]] std::unique_ptr<QTextLayout> layoutLine(qsizetype line) const;
    [[nodiscard]] std::unique_ptr<QTextLayout> layoutText(const QString &text, qsizetype preeditColumn) const;
    void layoutRows();
    void layoutDecorations();
    [[nodiscard]] std::unique_ptr<QTextLayout> layoutNumber(qsizetype line) const;
    [[nodiscard]] qreal xForPosition(qsizetype position) const;
    [[nodiscard]] qsizetype positionAt(const QPointF &point) const;
    [[nodiscard]] qreal textLeft() const;
    // The top of `line`, in content coordinates.
    [[nodiscard]] qreal lineTop(qsizetype line) const;
    // Tells assistive technologies about a change of text, caret or selection.
    void notifyTextChanged(qsizetype position, const QString &removed, const QString &inserted);
    void notifyCursorChanged();
    void setColor(QRgb &field, const QColor &color);

    TextBuffer m_buffer;
    QList<Edit> m_undo;
    // Move-only rows: std::vector (QList needs copyable elements).
    std::vector<Row> m_rows;
    std::vector<Number> m_numbers;
    // Set by updatePolish() for updatePaintNode(), relative to m_baseLine's
    // top (so coordinates stay small in huge files): the selection, one
    // rectangle per line, and the caret.
    QList<QRectF> m_selectionRects;
    QRectF m_caretRect;
    QString m_preedit;
    QFont m_font;
    QElapsedTimer m_lastDoubleClick;
    qreal m_contentX{0.0};
    qreal m_contentY{0.0};
    qreal m_lineHeight{16.0};
    qreal m_charWidth{8.0};
    // The widest line laid out so far: the horizontal scroll range grows as
    // lines come into view (measuring every line would walk the whole text).
    qreal m_widestLine{0.0};
    // The x vertical moves aim for, kept across short lines.
    qreal m_preferredX{-1.0};
    qsizetype m_cursor{0};
    qsizetype m_anchor{0};
    qsizetype m_undoIndex{0};
    // The undo index when the text matched the file; -1: never again.
    qsizetype m_cleanIndex{0};
    qsizetype m_preeditCursor{0};
    // The first line laid out; the scene is placed relative to it.
    qsizetype m_baseLine{0};
    quint64 m_nextRowId{1};
    // Bumped when the composition changes, so its line is laid out again.
    quint64 m_preeditGeneration{1};
    Colors m_colors;
    quint32 m_flags{0U};
};

} // namespace tekst
