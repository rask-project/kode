#include "TextDocument.h"

#include "Indentation.h"
#include "SyntaxHighlighter.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Theme>

#include <QVariantMap>

#include <algorithm>
#include <ranges>
#include <utility>

namespace kode
{

using namespace Qt::StringLiterals;

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
    setTheme(QStringLiteral("Kode Light"));
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

void TextDocument::setDiagnostics(QList<Diagnostic> diagnostics)
{
    std::ranges::sort(diagnostics, {}, &Diagnostic::start);
    m_diagnostics = std::move(diagnostics);
    emit diagnosticsChanged();
}

qint32 TextDocument::errorCount() const
{
    return static_cast<qint32>(std::ranges::count(m_diagnostics, Diagnostic::Error, &Diagnostic::severity));
}

qint32 TextDocument::warningCount() const
{
    return static_cast<qint32>(std::ranges::count(m_diagnostics, Diagnostic::Warning, &Diagnostic::severity));
}

QVariantList TextDocument::diagnosticList(qint32 limit) const
{
    // A menu item's text: the first line, cut short.
    constexpr qsizetype maxMessage{120};
    QVariantList list;
    list.reserve(std::min<qsizetype>(limit, m_diagnostics.size()));
    for (const Diagnostic &diagnostic : m_diagnostics | std::views::take(limit)) {
        const qsizetype line{m_buffer.lineAt(diagnostic.start)};
        QString message{diagnostic.message.section(u'\n', 0, 0)};
        if (message.size() > maxMessage) {
            message = message.first(maxMessage - 1) + u'…';
        }
        list.append(QVariantMap{
            {u"position"_s, diagnostic.start},
            {u"line"_s, line},
            {u"column"_s, diagnostic.start - m_buffer.lineStart(line)},
            {u"message"_s, message},
            {u"severity"_s, diagnostic.severity},
        });
    }
    return list;
}

void TextDocument::setLanguageServer(const QString &name, const QString &activity)
{
    if (name == m_languageServer && activity == m_languageServerActivity) {
        return;
    }
    m_languageServer = name;
    m_languageServerActivity = activity;
    emit languageServerChanged();
}

void TextDocument::setText(const QString &text, bool keepPlace)
{
    setBuffer(TextBuffer{text}, keepPlace);
}

void TextDocument::setBuffer(TextBuffer buffer, bool keepPlace)
{
    const bool wasModified{modified()};
    const bool hadHistory{!m_undo.isEmpty()};
    m_buffer = std::move(buffer);
    m_highlighter->reset();
    m_edited.clear();
    if (!m_diagnostics.isEmpty()) {
        m_diagnostics.clear();
        emit diagnosticsChanged();
    }
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

// Typing and deleting one character at a time extend the last step, at
// every caret alike; a new line closes the group (see replaceEach()).
void TextDocument::record(Step step, bool mergeable)
{
    // Forget what was undone, and the clean state if it was there.
    if (m_undoIndex < m_undo.size()) {
        m_undo.resize(m_undoIndex);
        if (m_cleanIndex > m_undoIndex) {
            m_cleanIndex = -1;
        }
    }
    const bool canMerge{mergeable && (m_flags & MergeOpen) != 0U && !m_undo.isEmpty() && m_cleanIndex != m_undoIndex};
    if (canMerge && merge(m_undo.last(), step)) {
        return;
    }
    m_undo.append(std::move(step));
    ++m_undoIndex;
}

// Folds `next` into `last` when each of its edits continues the matching
// one: typing after it, or deleting before it (backspace) or at it.
bool TextDocument::merge(Step &last, const Step &next)
{
    const qsizetype count{last.edits.size()};
    if (next.edits.size() != count) {
        return false;
    }
    // Where each edit of `last` ends up after it: the edits before it shift it.
    QList<qsizetype> after(count);
    qsizetype shift{0};
    for (qsizetype index{0}; index < count; ++index) {
        const Edit &edit{last.edits.at(index)};
        after[index] = edit.position + shift;
        shift += edit.inserted.size() - edit.removed.size();
    }
    // With one caret, typing over a selection starts a step of its own.
    const auto typing{[&](qsizetype index) {
        const Edit &edit{last.edits.at(index)};
        const Edit &more{next.edits.at(index)};
        return more.removed.isEmpty() && !more.inserted.isEmpty() && (count > 1 || edit.removed.isEmpty()) &&
               after.at(index) + edit.inserted.size() == more.position;
    }};
    const auto backspace{[&](qsizetype index) {
        const Edit &more{next.edits.at(index)};
        return more.inserted.isEmpty() && last.edits.at(index).inserted.isEmpty() &&
               more.position + more.removed.size() == after.at(index);
    }};
    const auto forwardDelete{[&](qsizetype index) {
        const Edit &more{next.edits.at(index)};
        return more.inserted.isEmpty() && last.edits.at(index).inserted.isEmpty() && more.position == after.at(index);
    }};
    const auto all{[count](const auto &test) {
        return std::ranges::all_of(std::views::iota(qsizetype{0}, count), test);
    }};
    if (all(typing)) {
        for (qsizetype index{0}; index < count; ++index) {
            last.edits[index].inserted.append(next.edits.at(index).inserted);
        }
    } else if (all(backspace)) {
        for (qsizetype index{0}; index < count; ++index) {
            Edit &edit{last.edits[index]};
            edit.removed.prepend(next.edits.at(index).removed);
            edit.position -= next.edits.at(index).removed.size();
        }
    } else if (all(forwardDelete)) {
        for (qsizetype index{0}; index < count; ++index) {
            last.edits[index].removed.append(next.edits.at(index).removed);
        }
    } else {
        return false;
    }
    last.cursorAfter = next.cursorAfter;
    return true;
}

void TextDocument::replace(qsizetype position, qsizetype count, const QString &text, bool mergeable, Caret before)
{
    replaceEach({Range{.position = position, .count = count}}, text, mergeable, before);
}

void TextDocument::replaceEach(const QList<Range> &ranges, const QString &text, bool mergeable, Caret before)
{
    if (ranges.isEmpty()) {
        return;
    }
    Step step{.before = before, .cursorAfter = ranges.constLast().position + text.size()};
    step.edits.reserve(ranges.size());
    qsizetype shift{0};
    for (const Range &range : ranges) {
        step.edits.append(
            {.position = range.position, .removed = m_buffer.text(range.position, range.count), .inserted = text}
        );
        shift += text.size() - range.count;
    }
    step.cursorAfter += shift - (text.size() - ranges.constLast().count);
    commit(std::move(step), mergeable && !text.contains(QLatin1Char('\n')));
}

void TextDocument::replaceEach(const QList<Change> &changes, Caret before)
{
    if (changes.isEmpty()) {
        return;
    }
    Step step{.before = before, .cursorAfter = before.cursor};
    step.edits.reserve(changes.size());
    for (const Change &change : changes) {
        if (change.range.position < before.cursor) {
            step.cursorAfter += change.text.size() - change.range.count;
        }
        step.edits.append(
            {.position = change.range.position,
             .removed = m_buffer.text(change.range.position, change.range.count),
             .inserted = change.text}
        );
    }
    commit(std::move(step), false);
}

void TextDocument::commit(Step step, bool mergeable)
{
    const bool wasModified{modified()};
    const bool couldUndo{canUndo()};
    const bool couldRedo{canRedo()};
    // From the last: each position is still the one before the step.
    for (const Edit &edit : step.edits | std::views::reverse) {
        apply(edit.position, edit.removed, edit.inserted);
    }
    record(std::move(step), mergeable);
    m_flags = mergeable ? (m_flags | MergeOpen) : (m_flags & ~MergeOpen);
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
    const qsizetype line{m_buffer.lineAt(position)};
    m_highlighter->linesReplaced(line, newlines(from), newlines(to));
    noteEdited(line, newlines(from), newlines(to));
    // A diagnostic's ends move with the text after them; inside the edit, to its end.
    const auto moved{[position, &from, &to](qsizetype at) {
        if (at <= position) {
            return at;
        }
        return at >= position + from.size() ? at + to.size() - from.size() : position + to.size();
    }};
    for (Diagnostic &diagnostic : m_diagnostics) {
        diagnostic.start = moved(diagnostic.start);
        diagnostic.end = moved(diagnostic.end);
    }
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
    const Step step{m_undo.at(m_undoIndex)};
    // Each edit where it ended up (the ones before it shifted it), from the last.
    qsizetype shift{0};
    QList<qsizetype> after;
    after.reserve(step.edits.size());
    for (const Edit &edit : step.edits) {
        after.append(edit.position + shift);
        shift += edit.inserted.size() - edit.removed.size();
    }
    for (qsizetype index{step.edits.size() - 1}; index >= 0; --index) {
        const Edit &edit{step.edits.at(index)};
        apply(after.at(index), edit.inserted, edit.removed);
    }
    emit undoStateChanged();
    if (wasModified != modified()) {
        emit modifiedChanged();
    }
    return step.before;
}

std::optional<TextDocument::Caret> TextDocument::redo()
{
    if (!canRedo()) {
        return std::nullopt;
    }
    const bool wasModified{modified()};
    const Step step{m_undo.at(m_undoIndex)};
    ++m_undoIndex;
    m_flags &= ~MergeOpen;
    for (const Edit &edit : step.edits | std::views::reverse) {
        apply(edit.position, edit.removed, edit.inserted);
    }
    emit undoStateChanged();
    if (wasModified != modified()) {
        emit modifiedChanged();
    }
    return Caret{.cursor = step.cursorAfter, .anchor = step.cursorAfter};
}

void TextDocument::noteEdited(qsizetype line, qsizetype removedLines, qsizetype insertedLines)
{
    const qsizetype delta{insertedLines - removedLines};
    // Typing on a line edited already: nothing to note.
    const auto within{std::ranges::find_if(m_edited, [line, removedLines](const Lines &lines) {
        return lines.first <= line && lines.last >= line + removedLines;
    })};
    if (delta == 0 && within != m_edited.cend()) {
        return;
    }
    // The ranges it touches (or meets) join it; those after it move.
    Lines edited{.first = line, .last = line + insertedLines};
    QList<Lines> joined;
    joined.reserve(m_edited.size() + 1);
    QList<Lines> after;
    for (const Lines &lines : std::as_const(m_edited)) {
        if (lines.last + 1 < line) {
            joined.append(lines);
        } else if (lines.first > line + removedLines + 1) {
            after.append({.first = lines.first + delta, .last = lines.last + delta});
        } else {
            edited.first = std::min(edited.first, lines.first);
            if (lines.last > line + removedLines) {
                edited.last = std::max(edited.last, lines.last + delta);
            }
        }
    }
    joined.append(edited);
    joined.append(after);
    m_edited = std::move(joined);
}

void TextDocument::markSaved()
{
    m_edited.clear();
    if (m_cleanIndex == m_undoIndex) {
        return;
    }
    m_cleanIndex = m_undoIndex;
    m_flags &= ~MergeOpen;
    emit modifiedChanged();
}

} // namespace kode
