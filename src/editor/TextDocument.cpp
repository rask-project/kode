#include "TextDocument.h"

#include "Indentation.h"
#include "SyntaxHighlighter.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Theme>

#include <algorithm>

namespace tekst
{

namespace
{

constexpr qint32 largestIndentWidth{16};

qsizetype newlines(const QString &text)
{
    return text.count(QLatin1Char('\n'));
}

} // namespace

TextDocument::TextDocument(QObject *parent)
    : QObject(parent)
    , m_highlighter{std::make_unique<SyntaxHighlighter>(&m_buffer)}
{
    setTheme(QStringLiteral("Tekst Light"));
}

TextDocument::~TextDocument() = default;

QString TextDocument::syntax() const
{
    return m_highlighter->isActive() ? m_highlighter->definition().name() : QString{};
}

void TextDocument::setSyntax(const QString &syntax)
{
    applyDefinition(SyntaxHighlighter::definitionForName(syntax));
}

void TextDocument::detectSyntax(const QString &fileName)
{
    applyDefinition(SyntaxHighlighter::detect(fileName, m_buffer.lineCount() > 0 ? m_buffer.line(0) : QString{}));
}

void TextDocument::applyDefinition(const KSyntaxHighlighting::Definition &definition)
{
    if (m_highlighter->definition() == definition) {
        return;
    }
    m_highlighter->setDefinition(definition);
    emit syntaxChanged();
    emit highlightingChanged();
}

void TextDocument::setTheme(const QString &name)
{
    if (m_theme == name) {
        return;
    }
    m_theme = name;
    m_highlighter->setTheme(SyntaxHighlighter::themeNamed(name));
    emit highlightingChanged();
}

void TextDocument::setIndentWithTabs(bool tabs)
{
    if (indentWithTabs() == tabs) {
        return;
    }
    m_flags = tabs ? (m_flags | IndentWithTabs) : (m_flags & ~IndentWithTabs);
    emit indentationChanged();
}

void TextDocument::setIndentWidth(qint32 width)
{
    width = std::clamp(width, 1, largestIndentWidth);
    if (m_indentWidth == width) {
        return;
    }
    m_indentWidth = width;
    emit indentationChanged();
}

void TextDocument::setDefaultIndentWithTabs(bool tabs)
{
    if (defaultIndentWithTabs() == tabs) {
        return;
    }
    m_flags = tabs ? (m_flags | DefaultIndentWithTabs) : (m_flags & ~DefaultIndentWithTabs);
    emit defaultIndentationChanged();
}

void TextDocument::setDefaultIndentWidth(qint32 width)
{
    width = std::clamp(width, 1, largestIndentWidth);
    if (m_defaultIndentWidth == width) {
        return;
    }
    m_defaultIndentWidth = width;
    emit defaultIndentationChanged();
}

void TextDocument::detectIndentation()
{
    constexpr qsizetype linesToRead{1000};
    const indentation::Style fallback{
        .width = static_cast<quint8>(m_defaultIndentWidth),
        .kind = defaultIndentWithTabs() ? indentation::Kind::Tabs : indentation::Kind::Spaces
    };
    const indentation::Style style{indentation::detect(m_buffer, linesToRead, fallback)};
    setIndentWithTabs(style.kind == indentation::Kind::Tabs);
    setIndentWidth(style.width);
}

void TextDocument::setText(const QString &text, bool keepPlace)
{
    const bool wasModified{modified()};
    const bool hadHistory{!m_undo.isEmpty()};
    m_buffer = TextBuffer{text};
    m_highlighter->reset();
    m_undo.clear();
    m_undoIndex = 0;
    m_cleanIndex = 0;
    m_flags &= ~MergeOpen;
    if (!keepPlace) {
        m_place = {};
    }
    // New text starts with the default indentation (a file's is detected).
    setIndentWithTabs(defaultIndentWithTabs());
    setIndentWidth(static_cast<qint32>(m_defaultIndentWidth));
    emit reset(keepPlace);
    emit textChanged();
    if (hadHistory) {
        emit undoStateChanged();
    }
    if (wasModified) {
        emit modifiedChanged();
    }
}

void TextDocument::record(const Edit &edit, bool mergeable, qsizetype count)
{
    // Forget what was undone, and the clean state if it was there.
    if (m_undoIndex < m_undo.size()) {
        m_undo.resize(m_undoIndex);
        if (m_cleanIndex > m_undoIndex) {
            m_cleanIndex = -1;
        }
    }
    // Typing and deleting one character at a time extend the last edit; a
    // new line closes the group, so a line typed after it is its own edit.
    const bool canMerge{mergeable && (m_flags & MergeOpen) != 0U && !m_undo.isEmpty() && m_cleanIndex != m_undoIndex};
    if (canMerge) {
        Edit &last{m_undo.last()};
        if (count == 0 && last.removed.isEmpty() && last.position + last.inserted.size() == edit.position) {
            last.inserted.append(edit.inserted);
            last.cursorAfter = edit.cursorAfter;
            return;
        }
        if (edit.inserted.isEmpty() && last.inserted.isEmpty() && edit.position + count == last.position) {
            last.removed.prepend(edit.removed);
            last.position = edit.position;
            last.cursorAfter = edit.position;
            return;
        }
        if (edit.inserted.isEmpty() && last.inserted.isEmpty() && edit.position == last.position) {
            last.removed.append(edit.removed);
            return;
        }
    }
    m_undo.append(edit);
    ++m_undoIndex;
}

void TextDocument::replace(qsizetype position, qsizetype count, const QString &text, bool mergeable, Caret before)
{
    const bool wasModified{modified()};
    const bool couldUndo{canUndo()};
    const bool couldRedo{canRedo()};
    const Edit edit{
        .position = position,
        .removed = m_buffer.text(position, count),
        .inserted = text,
        .before = before,
        .cursorAfter = position + text.size()
    };
    mergeable = mergeable && !text.contains(QLatin1Char('\n'));
    record(edit, mergeable, count);
    m_flags = mergeable ? (m_flags | MergeOpen) : (m_flags & ~MergeOpen);
    apply(position, edit.removed, text);
    if (couldUndo != canUndo() || couldRedo != canRedo()) {
        emit undoStateChanged();
    }
    if (wasModified != modified()) {
        emit modifiedChanged();
    }
}

void TextDocument::apply(qsizetype position, const QString &from, const QString &to)
{
    m_buffer.remove(position, from.size());
    m_buffer.insert(position, to);
    m_highlighter->linesReplaced(m_buffer.lineAt(position), newlines(from), newlines(to));
    emit replaced(position, from, to);
    emit textChanged();
}

std::optional<TextDocument::Caret> TextDocument::undo()
{
    if (!canUndo()) {
        return std::nullopt;
    }
    const bool wasModified{modified()};
    --m_undoIndex;
    m_flags &= ~MergeOpen;
    const Edit edit{m_undo.at(m_undoIndex)};
    apply(edit.position, edit.inserted, edit.removed);
    emit undoStateChanged();
    if (wasModified != modified()) {
        emit modifiedChanged();
    }
    return edit.before;
}

std::optional<TextDocument::Caret> TextDocument::redo()
{
    if (!canRedo()) {
        return std::nullopt;
    }
    const bool wasModified{modified()};
    const Edit edit{m_undo.at(m_undoIndex)};
    ++m_undoIndex;
    m_flags &= ~MergeOpen;
    apply(edit.position, edit.removed, edit.inserted);
    emit undoStateChanged();
    if (wasModified != modified()) {
        emit modifiedChanged();
    }
    return Caret{.cursor = edit.cursorAfter, .anchor = edit.cursorAfter};
}

void TextDocument::markSaved()
{
    if (m_cleanIndex == m_undoIndex) {
        return;
    }
    m_cleanIndex = m_undoIndex;
    m_flags &= ~MergeOpen;
    emit modifiedChanged();
}

} // namespace tekst
