#pragma once

#include "TextBuffer.h"

#include <QColor>
#include <QFont>
#include <QQuickItem>
#include <QRectF>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <vector>

class QTextLayout;

namespace tekst
{

// Prototype of the editing surface (roadmap, Stage 3): a TextBuffer drawn
// by laying out and rendering only the lines in view, one QSGTextNode per
// line. Monospaced, no wrapping, no selection yet. Placed in a Flickable:
// `contentHeight` sizes it, `contentY` says which part is in view.
class EditorView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)
    Q_PROPERTY(QColor caretColor READ caretColor WRITE setCaretColor NOTIFY caretColorChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(qint32 lineCount READ lineCount NOTIFY contentHeightChanged)
    Q_PROPERTY(qint64 length READ length NOTIFY textChanged)
    Q_PROPERTY(qint64 cursorPosition READ cursorPosition WRITE setCursorPosition NOTIFY cursorChanged)
    Q_PROPERTY(qint32 cursorLine READ cursorLine NOTIFY cursorChanged)
    Q_PROPERTY(qint32 cursorColumn READ cursorColumn NOTIFY cursorChanged)
    // The caret, in content coordinates (for the Flickable to keep in view).
    Q_PROPERTY(QRectF cursorRectangle READ cursorRectangle NOTIFY cursorChanged)
public:
    explicit EditorView(QQuickItem *parent = nullptr);
    ~EditorView() override;
    Q_DISABLE_COPY_MOVE(EditorView)

    [[nodiscard]] QFont font() const { return m_font; }
    void setFont(const QFont &font);
    [[nodiscard]] QColor textColor() const { return QColor::fromRgba(m_textColor); }
    void setTextColor(const QColor &color);
    [[nodiscard]] QColor caretColor() const { return QColor::fromRgba(m_caretColor); }
    void setCaretColor(const QColor &color);
    [[nodiscard]] qreal contentY() const noexcept { return m_contentY; }
    void setContentY(qreal contentY);
    [[nodiscard]] qreal contentHeight() const;
    [[nodiscard]] qint32 lineCount() const { return static_cast<qint32>(m_buffer.lineCount()); }
    [[nodiscard]] qint64 length() const { return m_buffer.length(); }
    [[nodiscard]] qint64 cursorPosition() const noexcept { return m_cursor; }
    void setCursorPosition(qint64 position);
    [[nodiscard]] qint32 cursorLine() const;
    [[nodiscard]] qint32 cursorColumn() const;
    [[nodiscard]] QRectF cursorRectangle() const;

    // Reads a UTF-8 file (line endings become '\n'). False when it can't.
    Q_INVOKABLE bool load(const QUrl &fileUrl);
    Q_INVOKABLE void setText(const QString &text);
    Q_INVOKABLE void insert(const QString &text);

signals:
    void fontChanged();
    void textColorChanged();
    void caretColorChanged();
    void contentYChanged();
    void contentHeightChanged();
    void textChanged();
    void cursorChanged();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    // A laid out line in view.
    struct Row {
        qsizetype line{-1};
        quint64 revision{0};
        std::unique_ptr<QTextLayout> layout;
    };

    void updateMetrics();
    void textEdited();
    void moveCursor(qsizetype position, bool keepColumn = false);
    void moveVertically(qsizetype lines);
    [[nodiscard]] qsizetype firstVisibleLine() const;
    [[nodiscard]] qsizetype visibleLineCount() const;
    [[nodiscard]] std::unique_ptr<QTextLayout> layoutLine(qsizetype line) const;
    [[nodiscard]] qreal xForColumn(qsizetype line, qsizetype column) const;
    [[nodiscard]] qsizetype columnForX(qsizetype line, qreal x) const;

    TextBuffer m_buffer;
    // Move-only rows: std::vector (QList needs copyable elements).
    std::vector<Row> m_rows;
    QFont m_font;
    qreal m_contentY{0.0};
    qreal m_lineHeight{16.0};
    qreal m_ascent{12.0};
    qreal m_charWidth{8.0};
    // The column vertical moves aim for, kept across short lines.
    qreal m_preferredX{-1.0};
    qsizetype m_cursor{0};
    // Bumped by every edit: rows laid out before it are stale.
    quint64 m_revision{1};
    QRgb m_textColor{0xffe6e6e6U};
    QRgb m_caretColor{0xff4c9bffU};
};

} // namespace tekst
