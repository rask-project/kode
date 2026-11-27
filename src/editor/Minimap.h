#pragma once

#include <KSyntaxHighlighting/State>

#include <QColor>
#include <QList>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QSet>
#include <QTextLayout>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <limits>

namespace kode
{

class EditorView;
class MinimapNode;
class TextBuffer;
class TextDocument;

// A view's text at a small scale beside it, as VS Code's minimap: each run of
// characters a block in its highlighting color, the part in view under a
// slider. A click scrolls the view to that line, a drag moves the slider.
// Only the lines it shows are kept, each highlighted once; they are drawn in
// chunks of lines that scrolling moves without filling them again.
class Minimap : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(kode::EditorView *view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QColor sliderColor READ sliderColor WRITE setSliderColor NOTIFY colorsChanged)
    // The slider under the pointer or dragged.
    Q_PROPERTY(QColor sliderActiveColor READ sliderActiveColor WRITE setSliderActiveColor NOTIFY colorsChanged)
public:
    explicit Minimap(QQuickItem *parent = nullptr);
    ~Minimap() override;
    Q_DISABLE_COPY_MOVE(Minimap)

    [[nodiscard]] EditorView *view() const { return m_view; }
    void setView(EditorView *view);
    [[nodiscard]] QColor sliderColor() const { return QColor::fromRgba(m_sliderColor); }
    void setSliderColor(const QColor &color);
    [[nodiscard]] QColor sliderActiveColor() const { return QColor::fromRgba(m_sliderActiveColor); }
    void setSliderActiveColor(const QColor &color);

signals:
    void viewChanged();
    void colorsChanged();
    // Scroll the view to `contentY`.
    void scrollRequested(qreal contentY);

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;

private:
    // A run of characters in one color, in columns.
    struct Block {
        qint32 column{0};
        qint32 length{0};
        QRgb color{0U};
    };

    // How a line was drawn: not yet, without highlighting (not ready), or highlighted from `state`.
    static constexpr qint64 Missing{0};
    static constexpr qint64 Plain{1};
    static constexpr qint64 Highlighted{2};

    struct Line {
        KSyntaxHighlighting::State state;
        QList<Block> blocks;
        qint64 kind{Missing};
    };

    static constexpr quint32 Hovered{1U << 0U};
    static constexpr quint32 Dragging{1U << 1U};

    [[nodiscard]] const TextBuffer &buffer() const;
    // Follows the view's document.
    void attach();
    void documentReplaced(qsizetype position, const QString &removed, const QString &inserted);
    // Every line drawn again (colors or tab stops changed).
    void invalidate();
    // The first line shown, fractional: past the minimap's height it scrolls
    // with the view, its top at the same fraction of the text.
    [[nodiscard]] qreal firstShown() const;
    // Keeps the lines [first, end), dropping the others.
    void keepLines(qsizetype first, qsizetype end);
    // Draws the lines kept that changed, until a deadline; false when some are left.
    [[nodiscard]] bool fillLines();
    // The view's highlighting reached further: lines drawn plain may get their colors.
    void highlightingAdvanced();
    [[nodiscard]] bool stale(const Line &line, qsizetype index) const;
    [[nodiscard]] Line measure(qsizetype index) const;
    static void appendBlocks(
        QList<Block> &blocks,
        QStringView text,
        const QList<QTextLayout::FormatRange> &formats,
        QRgb plain,
        qsizetype tabWidth,
        qsizetype columns
    );
    void syncChunks(MinimapNode *node);
    void fillChunk(QSGNode *transform, qsizetype chunk) const;
    // How far the view scrolls, and the slider moves.
    [[nodiscard]] qreal scrollRange() const;
    [[nodiscard]] qreal sliderTravel() const;

    QPointer<EditorView> m_view;
    QPointer<TextDocument> m_document;
    // The lines kept, from m_linesFirst.
    QList<Line> m_lines;
    // The chunks to fill again, and all from m_dirtyFrom on (lines moved).
    QSet<qsizetype> m_dirtyChunks;
    QTimer m_fillTimer;
    QRectF m_slider;
    qreal m_first{0.0};
    qreal m_dragY{0.0};
    qreal m_dragContentY{0.0};
    qsizetype m_linesFirst{0};
    // The first line kept that is drawn without highlighting (the view's
    // highlighting has not reached it); -1: none.
    qsizetype m_plainFrom{-1};
    qsizetype m_dirtyFrom{std::numeric_limits<qsizetype>::max()};
    quint32 m_flags{0U};
    QRgb m_textColor{0U};
    QRgb m_sliderColor{0x14000000U};
    QRgb m_sliderActiveColor{0x29000000U};
};

} // namespace kode
