#include "Completion.h"

#include "Document.h"
#include "EditorView.h"
#include "LanguageServers.h"
#include "Matching.h"
#include "Protocol.h"
#include "TextBuffer.h"

#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <tuple>

namespace kode
{

using namespace Qt::StringLiterals;

namespace
{

// How `candidate` matches what is typed: 0 it starts with it, 1 it has its
// characters in order (case aside); -1 neither.
qsizetype matchRank(QStringView candidate, QStringView typed)
{
    if (candidate.startsWith(typed, Qt::CaseInsensitive)) {
        return 0;
    }
    qsizetype next{0};
    for (const QChar character : typed) {
        next = candidate.indexOf(character, next, Qt::CaseInsensitive);
        if (next < 0) {
            return -1;
        }
        ++next;
    }
    return 1;
}

// A snippet's text without its tab stops: ${1:name} is name, $1 nothing.
QString withoutTabStops(const QString &snippet)
{
    static const QRegularExpression placeholder{uR"(\$\{\d+(?::([^}]*))?\})"_s};
    static const QRegularExpression stop{uR"(\$\d+)"_s};
    QString text{snippet};
    text.replace(placeholder, u"\\1"_s);
    text.remove(stop);
    return text;
}

} // namespace

Completion::Completion(QObject *parent) : QAbstractListModel(parent) {}

qint32 Completion::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<qint32>(m_shown.size());
}

QVariant Completion::data(const QModelIndex &index, qint32 role) const
{
    if (!index.isValid() || index.row() >= m_shown.size()) {
        return {};
    }
    const Item &item{m_items.at(m_shown.at(index.row()))};
    switch (role) {
    case LabelRole:
        return item.label;
    case DetailRole:
        return item.detail;
    case KindRole:
        return item.kind;
    default:
        return {};
    }
}

QHash<qint32, QByteArray> Completion::roleNames() const
{
    return {{LabelRole, "label"}, {DetailRole, "detail"}, {KindRole, "kind"}};
}

void Completion::setEditor(EditorView *editor)
{
    if (editor == m_editor) {
        return;
    }
    if (m_editor != nullptr) {
        disconnect(m_editor, nullptr, this, nullptr);
    }
    cancel();
    m_editor = editor;
    if (m_editor != nullptr) {
        connect(m_editor, &EditorView::textTyped, this, &Completion::typed);
        connect(m_editor, &EditorView::caretMoved, this, &Completion::followCaret);
        connect(m_editor, &EditorView::documentChanged, this, &Completion::cancel);
        connect(m_editor, &QQuickItem::activeFocusChanged, this, [this] {
            if (!m_editor->hasActiveFocus()) {
                cancel();
            }
        });
    }
    emit editorChanged();
}

void Completion::setDocument(Document *document)
{
    if (document == m_document) {
        return;
    }
    cancel();
    m_document = document;
    emit documentChanged();
}

void Completion::setServers(lsp::LanguageServers *servers)
{
    if (servers == m_servers) {
        return;
    }
    if (m_servers != nullptr) {
        disconnect(m_servers, nullptr, this, nullptr);
    }
    m_servers = servers;
    if (m_servers != nullptr) {
        connect(m_servers, &lsp::LanguageServers::completionsFound, this, &Completion::found);
    }
    emit serversChanged();
}

void Completion::setCurrentIndex(qint32 index)
{
    const qint32 clamped{std::clamp(index, 0, std::max(0, static_cast<qint32>(m_shown.size()) - 1))};
    if (clamped == m_current) {
        return;
    }
    m_current = clamped;
    emit currentIndexChanged();
}

void Completion::trigger()
{
    request({});
}

void Completion::move(qint32 step)
{
    setCurrentIndex(m_current + step);
}

void Completion::cancel()
{
    m_ticket = 0;
    m_items.clear();
    setShown({});
}

void Completion::accept()
{
    if (!active() || m_editor == nullptr || m_document == nullptr) {
        return;
    }
    const Item chosen{m_items.at(m_shown.at(m_current))};
    const qsizetype wordStart{m_start};
    cancel();
    const TextBuffer &buffer{m_document->textDocument()->buffer()};
    const qsizetype caret{m_editor->cursorPosition()};
    const bool edit{chosen.editStart >= 0};
    const qsizetype start{edit ? std::min(chosen.editStart, caret) : wordStart};
    const qsizetype end{edit ? std::min(caret + chosen.editEndPastCaret, buffer.length()) : caret};
    // The other edits apart from the word's, by position, with it.
    QList<TextDocument::Change> changes;
    std::ranges::copy_if(chosen.others, std::back_inserter(changes), [start, end](const TextDocument::Change &other) {
        const qsizetype otherEnd{other.range.position + other.range.count};
        return otherEnd <= start || other.range.position >= end;
    });
    const TextDocument::Change word{.range = {.position = start, .count = end - start}, .text = chosen.text};
    const auto at{std::ranges::upper_bound(changes, start, {}, [](const TextDocument::Change &change) {
        return change.range.position;
    })};
    const qsizetype main{at - changes.begin()};
    changes.insert(main, word);
    m_editor->applyChanges(changes, main);
}

void Completion::typed(const QString &text)
{
    if (m_servers == nullptr || m_document == nullptr || m_editor == nullptr || text.size() != 1 ||
        m_editor->caretCount() > 1) {
        return;
    }
    const QChar character{text.front()};
    if (m_servers->completionTriggers(m_document).contains(text)) {
        request(text);
        return;
    }
    if (!matching::isWordCharacter(character)) {
        cancel();
        return;
    }
    // A word starting: ask; going on: filter, or ask again when the list was cut short.
    const bool asked{m_ticket != 0};
    if ((!asked && !character.isDigit()) || (asked && (m_flags & Incomplete) != 0U)) {
        request({});
    }
}

void Completion::followCaret()
{
    if (m_ticket == 0 || m_editor == nullptr || m_document == nullptr) {
        return;
    }
    const qsizetype caret{m_editor->cursorPosition()};
    const QString word{typedWord()};
    if (caret < m_start || m_editor->hasSelection() || !std::ranges::all_of(word, matching::isWordCharacter)) {
        cancel();
        return;
    }
    filter();
}

void Completion::request(const QString &trigger)
{
    if (m_servers == nullptr || m_document == nullptr || m_editor == nullptr) {
        return;
    }
    const TextBuffer &buffer{m_document->textDocument()->buffer()};
    const qsizetype caret{m_editor->cursorPosition()};
    const qsizetype lineStart{buffer.lineStart(buffer.lineAt(caret))};
    const QString before{buffer.text(lineStart, caret - lineStart)};
    qsizetype start{before.size()};
    while (start > 0 && matching::isWordCharacter(before.at(start - 1))) {
        --start;
    }
    m_start = lineStart + start;
    m_requestCaret = caret;
    m_ticket = m_servers->requestCompletion(m_document, caret, trigger);
    if (m_ticket == 0) {
        cancel();
    }
}

void Completion::found(qint32 ticket, const QJsonArray &items, bool incomplete)
{
    if (ticket != m_ticket || ticket == 0) {
        return;
    }
    m_items.clear();
    m_items.reserve(items.size());
    std::ranges::transform(items, std::back_inserter(m_items), [this](const QJsonValue &value) { return item(value); });
    m_flags = incomplete ? (m_flags | Incomplete) : (m_flags & ~Incomplete);
    filter();
}

Completion::Item Completion::item(const QJsonValue &value) const
{
    const TextBuffer &buffer{m_document->textDocument()->buffer()};
    const QString label{value[u"label"_s].toString()};
    Item item{
        .label = label,
        .detail = value[u"detail"_s].toString(),
        .filter = value[u"filterText"_s].toString(label),
        .sort = value[u"sortText"_s].toString(label),
        .text = value[u"insertText"_s].toString(label),
        .others = {},
        .kind = value[u"kind"_s].toInteger(),
    };
    const QJsonValue edit{value[u"textEdit"_s]};
    if (edit.isObject()) {
        // A TextEdit's range, or an InsertReplaceEdit's insert range.
        const QJsonValue range{edit[u"range"_s].isObject() ? edit[u"range"_s] : edit[u"insert"_s]};
        item.text = edit[u"newText"_s].toString();
        item.editStart = lsp::positionIn(buffer, range[u"start"_s]);
        item.editEndPastCaret = std::max(qsizetype{0}, lsp::positionIn(buffer, range[u"end"_s]) - m_requestCaret);
    }
    // The protocol's insertTextFormat 2: a snippet (sent despite asking for plain text).
    constexpr qint64 snippet{2};
    if (value[u"insertTextFormat"_s].toInteger() == snippet) {
        item.text = withoutTabStops(item.text);
    }
    const QJsonArray others(value[u"additionalTextEdits"_s].toArray());
    std::ranges::transform(others, std::back_inserter(item.others), [&buffer](const QJsonValue &other) {
        const qsizetype from{lsp::positionIn(buffer, other[u"range"_s][u"start"_s])};
        const qsizetype to{lsp::positionIn(buffer, other[u"range"_s][u"end"_s])};
        return TextDocument::Change{
            .range = {.position = from, .count = to - from},
            .text = other[u"newText"_s].toString(),
        };
    });
    std::ranges::sort(item.others, {}, [](const TextDocument::Change &change) { return change.range.position; });
    return item;
}

void Completion::filter()
{
    const QString word{typedWord()};
    struct Ranked {
        qsizetype index{0};
        qsizetype rank{0};
    };
    QList<Ranked> ranked;
    for (qsizetype index{0}; index < m_items.size(); ++index) {
        if (const qsizetype rank{matchRank(m_items.at(index).filter, word)}; rank >= 0) {
            ranked.append({.index = index, .rank = rank});
        }
    }
    std::ranges::stable_sort(ranked, [this](const Ranked &a, const Ranked &b) {
        const Item &first{m_items.at(a.index)};
        const Item &second{m_items.at(b.index)};
        return std::tie(a.rank, first.sort, first.label) < std::tie(b.rank, second.sort, second.label);
    });
    QList<qsizetype> shown;
    shown.reserve(ranked.size());
    std::ranges::transform(ranked, std::back_inserter(shown), &Ranked::index);
    setShown(std::move(shown));
}

void Completion::setShown(QList<qsizetype> shown)
{
    const bool wasActive{active()};
    beginResetModel();
    m_shown = std::move(shown);
    endResetModel();
    if (m_current != 0) {
        m_current = 0;
        emit currentIndexChanged();
    }
    if (active() != wasActive) {
        emit activeChanged();
    }
}

QString Completion::typedWord() const
{
    if (m_editor == nullptr || m_document == nullptr || m_start < 0) {
        return {};
    }
    const qsizetype caret{m_editor->cursorPosition()};
    return caret > m_start ? m_document->textDocument()->buffer().text(m_start, caret - m_start) : QString{};
}

} // namespace kode
