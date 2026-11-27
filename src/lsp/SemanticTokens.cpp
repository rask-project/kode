#include "SemanticTokens.h"

#include "SemanticStyle.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <array>
#include <utility>

namespace kode::lsp
{

using namespace Qt::StringLiterals;

namespace
{

// What a token type names, across servers (clangd's, qmlls's, the protocol's).
enum Kind : qint8 {
    Other = -1,
    Variable,
    Parameter,
    Function,
    Property,
    Class,
    TypeLike,
    TypeParameter,
    Namespace,
    EnumMember,
    Macro,
    Label,
    // qmlls's own, each a Qt Creator category.
    QmlCategory,
};

struct Named {
    QLatin1StringView name;
    // A Kind, in a full int: the table keeps no padding.
    qint32 kind;
    // Its category (QmlCategory's own; the others' before their modifiers).
    qint32 category;
};

constexpr std::array kinds{
    Named{.name = "variable"_L1, .kind = Variable, .category = semantic::Local},
    Named{.name = "parameter"_L1, .kind = Parameter, .category = semantic::Parameter},
    Named{.name = "function"_L1, .kind = Function, .category = semantic::Function},
    Named{.name = "method"_L1, .kind = Function, .category = semantic::Function},
    Named{.name = "property"_L1, .kind = Property, .category = semantic::Field},
    Named{.name = "field"_L1, .kind = Property, .category = semantic::Field},
    Named{.name = "class"_L1, .kind = Class, .category = semantic::Type},
    Named{.name = "struct"_L1, .kind = TypeLike, .category = semantic::Type},
    Named{.name = "interface"_L1, .kind = TypeLike, .category = semantic::Type},
    Named{.name = "enum"_L1, .kind = TypeLike, .category = semantic::Type},
    Named{.name = "type"_L1, .kind = TypeLike, .category = semantic::Type},
    Named{.name = "concept"_L1, .kind = TypeLike, .category = semantic::Type},
    Named{.name = "typeParameter"_L1, .kind = TypeParameter, .category = semantic::Type},
    Named{.name = "namespace"_L1, .kind = Namespace, .category = semantic::Namespace},
    // Enumerators as Qt Creator shows them: as static members.
    Named{.name = "enumMember"_L1, .kind = EnumMember, .category = semantic::StaticMember},
    Named{.name = "macro"_L1, .kind = Macro, .category = semantic::Macro},
    Named{.name = "label"_L1, .kind = Label, .category = semantic::Label},
    Named{.name = "qmlLocalId"_L1, .kind = QmlCategory, .category = semantic::QmlLocalId},
    Named{.name = "qmlExternalId"_L1, .kind = QmlCategory, .category = semantic::QmlExternalId},
    Named{.name = "qmlRootObjectProperty"_L1, .kind = QmlCategory, .category = semantic::QmlRootObjectProperty},
    Named{.name = "qmlScopeObjectProperty"_L1, .kind = QmlCategory, .category = semantic::QmlScopeObjectProperty},
    Named{.name = "qmlExternalObjectProperty"_L1, .kind = QmlCategory, .category = semantic::QmlExternalObjectProperty},
    Named{.name = "jsScopeVar"_L1, .kind = QmlCategory, .category = semantic::JsScopeVar},
    Named{.name = "jsImportVar"_L1, .kind = QmlCategory, .category = semantic::JsImportVar},
    Named{.name = "jsGlobalVar"_L1, .kind = QmlCategory, .category = semantic::JsGlobalVar},
    Named{.name = "qmlStateName"_L1, .kind = QmlCategory, .category = semantic::QmlStateName},
};

// QmlCategory kinds keep their category above the Kind's bits.
constexpr qint32 categoryShift{8};

quint32 bit(const QStringList &modifiers, QLatin1StringView name)
{
    const qsizetype index{modifiers.indexOf(name)};
    constexpr qsizetype bits{32};
    return index >= 0 && index < bits ? 1U << static_cast<quint32>(index) : 0U;
}

} // namespace

SemanticLegend::SemanticLegend(const QStringList &types, const QStringList &modifiers, bool qml)
    : m_declaration{bit(modifiers, "declaration"_L1)}
    , m_definition{bit(modifiers, "definition"_L1)}
    , m_static{bit(modifiers, "static"_L1)}
    , m_virtual{bit(modifiers, "virtual"_L1)}
    , m_readonly{bit(modifiers, "readonly"_L1)}
    , m_constructor{bit(modifiers, "constructorOrDestructor"_L1)}
    , m_functionScope{bit(modifiers, "functionScope"_L1)}
    , m_classScope{bit(modifiers, "classScope"_L1)}
    , m_globalScope{bit(modifiers, "globalScope"_L1) | bit(modifiers, "fileScope"_L1)}
    , m_mutable{bit(modifiers, "usedAsMutableReference"_L1) | bit(modifiers, "usedAsMutablePointer"_L1)}
{
    m_kinds.reserve(types.size());
    for (const QString &type : types) {
        // qmlls's "list" (of list<T>): a keyword.
        if (qml && type == u"decorator"_s) {
            m_kinds.append(QmlCategory | (semantic::Keyword << categoryShift));
            continue;
        }
        const auto found{std::ranges::find(kinds, type, &Named::name)};
        if (found == kinds.cend()) {
            m_kinds.append(Other);
            continue;
        }
        // In QML as Qt Creator shows it: types as QML types, parameters and
        // methods plain (variables and properties refined by their text).
        if (qml && found->kind == TypeLike) {
            m_kinds.append(QmlCategory | (semantic::QmlTypeId << categoryShift));
        } else if (qml && (found->kind == Parameter || found->kind == Function)) {
            m_kinds.append(QmlCategory | (semantic::Plain << categoryShift));
        } else {
            m_kinds.append(found->kind | (found->category << categoryShift));
        }
    }
}

std::optional<quint32> SemanticLegend::style(quint32 type, quint32 modifiers) const
{
    if (type >= static_cast<quint32>(m_kinds.size())) {
        return std::nullopt;
    }
    const qint32 entry{m_kinds.at(type)};
    const auto kind{static_cast<Kind>(static_cast<qint8>(entry & 0xff))};
    if (kind == Other) {
        return std::nullopt;
    }
    const auto has{[modifiers](quint32 mask) { return (modifiers & mask) != 0U; }};
    quint32 category{static_cast<quint32>(entry >> categoryShift)};
    quint32 mixins{0U};
    switch (kind) {
    case Variable:
        category = has(m_classScope) ? semantic::Field : has(m_globalScope) ? semantic::Global : semantic::Local;
        break;
    case Function:
        category = has(m_virtual) ? semantic::VirtualMethod : semantic::Function;
        mixins |= has(m_definition) ? semantic::FunctionDefinition : 0U;
        break;
    case Class:
        category = has(m_constructor) ? semantic::Function : semantic::Type;
        break;
    case TypeParameter:
        // A template's value parameter.
        category = has(m_readonly) ? semantic::Parameter : semantic::Type;
        break;
    default:
        break;
    }
    mixins |= has(m_declaration) ? semantic::Declaration : 0U;
    mixins |= has(m_mutable) ? semantic::OutputArgument : 0U;
    // A static field as Qt Creator's code has it; a static function as it
    // shows one (plain, not as a static member).
    if (has(m_static) && category == semantic::Field) {
        category = semantic::StaticMember;
    }
    return category | mixins;
}

QList<QList<TextDocument::SemanticToken>>
decodeTokens(const QList<quint32> &data, const SemanticLegend &legend, qsizetype lineCount)
{
    QList<QList<TextDocument::SemanticToken>> lines(lineCount);
    constexpr qsizetype stride{5};
    qsizetype line{0};
    qint32 column{0};
    for (qsizetype at{0}; at + stride <= data.size(); at += stride) {
        const quint32 lineDelta{data.at(at)};
        line += lineDelta;
        column = static_cast<qint32>(lineDelta == 0U ? column + static_cast<qint32>(data.at(at + 1)) : data.at(at + 1));
        if (line >= lineCount) {
            break;
        }
        const std::optional<quint32> style{legend.style(data.at(at + 3), data.at(at + 4))};
        if (style.has_value()) {
            lines[line].append({.column = column, .length = static_cast<qint32>(data.at(at + 2)), .style = *style});
        }
    }
    return lines;
}

namespace
{

// Whether the '{' at `at` in `text` opens a QML object ("Item {", "anchors {",
// "Behavior on x {"), not a JavaScript block or object (after "(", "=",
// ":", ")", "=>", "else"…).
bool opensObject(QStringView text, qsizetype at)
{
    const QStringView before{text.first(at).trimmed()};
    if (before.isEmpty() || !(before.back().isLetterOrNumber() || before.back() == u'_')) {
        return false;
    }
    qsizetype start{before.size()};
    while (start > 0 && (before.at(start - 1).isLetterOrNumber() || before.at(start - 1) == u'_')) {
        --start;
    }
    static constexpr std::array keywords{u"else", u"try", u"finally", u"do"};
    return !std::ranges::contains(keywords, before.sliced(start));
}

// Follows the braces of QML text line by line (strings and comments aside),
// telling whether a column is directly in a QML object.
class BraceScanner
{
public:
    // For each of `columns` (ascending) in `text`, the next line: whether it is in a QML object.
    QList<bool> scan(QStringView text, const QList<qint32> &columns)
    {
        QList<bool> inObject;
        inObject.reserve(columns.size());
        qsizetype at{0};
        for (const qint32 column : columns) {
            while (at < column && at < text.size()) {
                at = step(text, at);
            }
            // The file's top level is a QML object's.
            inObject.append(m_objects.isEmpty() || m_objects.last());
        }
        while (at < text.size()) {
            at = step(text, at);
        }
        return inObject;
    }

private:
    static constexpr quint64 InComment{1U << 0U};

    // Takes in what starts at `at` (a character, a quote to its end, a
    // comment's mark); returns where the next starts.
    qsizetype step(QStringView text, qsizetype at)
    {
        const QChar character{text.at(at)};
        const QChar following{at + 1 < text.size() ? text.at(at + 1) : QChar{}};
        if ((m_flags & InComment) != 0U) {
            if (character == u'*' && following == u'/') {
                m_flags &= ~InComment;
                return at + 2;
            }
            return at + 1;
        }
        if (character == u'/' && following == u'/') {
            return text.size();
        }
        if (character == u'/' && following == u'*') {
            m_flags |= InComment;
            return at + 2;
        }
        if (character == u'"' || character == u'\'' || character == u'`') {
            // To its closing quote on this line, escapes aside.
            qsizetype end{at + 1};
            while (end < text.size() && text.at(end) != character) {
                end += text.at(end) == u'\\' ? 2 : 1;
            }
            return std::min(end + 1, text.size());
        }
        if (character == u'{') {
            m_objects.append(opensObject(text, at));
        } else if (character == u'}' && !m_objects.isEmpty()) {
            m_objects.removeLast();
        }
        return at + 1;
    }

    QList<bool> m_objects;
    quint64 m_flags{0U};
};

// A qmlls token's style as Qt Creator shows it in QML.
quint32 qmlStyle(const TextDocument::SemanticToken &token, const QString &text, const QSet<QString> &ids, bool inObject)
{
    const quint32 mixins{token.style & ~semantic::categoryMask};
    switch (token.style & semantic::categoryMask) {
    case semantic::Local:
        // An id in italics, as a property of the scope; other variables plain.
        return (ids.contains(text.mid(token.column, token.length)) ? semantic::QmlScopeObjectProperty : semantic::Plain
               ) |
               mixins;
    case semantic::Field: {
        // Bound ("name: …", "property T name: …"); a member read is plain.
        const qsizetype end{std::min<qsizetype>(text.size(), qsizetype{token.column} + token.length)};
        const QStringView rest{QStringView{text}.sliced(end).trimmed()};
        // Starting the statement: alone, after "property T" or a group's
        // name ("anchors." of anchors.fill).
        static const QRegularExpression lead{
            uR"(^(?:(?:(?:readonly|required|default)\s+)*property\s+[\w.<>]+|[\w.]*\.)?$)"_s
        };
        const QString before{text.first(std::min<qsizetype>(token.column, text.size())).trimmed()};
        const bool bound{inObject && rest.startsWith(u':') && !rest.startsWith(u"::") && lead.match(before).hasMatch()};
        return (bound ? semantic::Binding : semantic::Plain) | mixins;
    }
    case semantic::QmlTypeId: {
        // Value types (url, bool…) plain; var as a type.
        const QString word{text.mid(token.column, token.length)};
        const bool valueType{!word.isEmpty() && word.front().isLower() && word != u"var"_s};
        return valueType ? quint32{semantic::Plain} : token.style;
    }
    default:
        return token.style;
    }
}

} // namespace

void refineQmlTokens(QList<QList<TextDocument::SemanticToken>> &lines, const TextBuffer &buffer)
{
    static const QRegularExpression idLine{uR"(^\s*id\s*:\s*([A-Za-z_]\w*))"_s};
    QSet<QString> ids;
    for (qsizetype line{0}; line < buffer.lineCount(); ++line) {
        if (const QRegularExpressionMatch match{idLine.match(buffer.line(line))}; match.hasMatch()) {
            ids.insert(match.captured(1));
        }
    }
    BraceScanner braces;
    for (qsizetype line{0}; line < lines.size(); ++line) {
        QList<TextDocument::SemanticToken> &tokens{lines[line]};
        const QString text{buffer.line(line)};
        QList<qint32> columns;
        columns.reserve(tokens.size());
        std::ranges::transform(tokens, std::back_inserter(columns), &TextDocument::SemanticToken::column);
        const QList<bool> inObject{braces.scan(text, columns)};
        for (qsizetype index{0}; index < tokens.size(); ++index) {
            tokens[index].style = qmlStyle(tokens.at(index), text, ids, inObject.at(index));
        }
    }
}

void applyTokenEdits(QList<quint32> &data, const QJsonArray &edits)
{
    // From the last: each start is still where the edits before it left it.
    QList<QJsonObject> sorted;
    sorted.reserve(edits.size());
    std::ranges::transform(edits, std::back_inserter(sorted), [](const QJsonValue &edit) { return edit.toObject(); });
    std::ranges::sort(sorted, std::ranges::greater{}, [](const QJsonObject &edit) {
        return edit.value(u"start"_s).toInteger();
    });
    for (const QJsonObject &edit : std::as_const(sorted)) {
        const qsizetype start{std::clamp<qsizetype>(edit.value(u"start"_s).toInteger(), 0, data.size())};
        const qsizetype count{std::clamp<qsizetype>(edit.value(u"deleteCount"_s).toInteger(), 0, data.size() - start)};
        const QJsonArray inserted(edit.value(u"data"_s).toArray());
        data.remove(start, count);
        QList<quint32> values;
        values.reserve(inserted.size());
        std::ranges::transform(inserted, std::back_inserter(values), [](const QJsonValue &value) {
            return static_cast<quint32>(value.toInteger());
        });
        data.insert(start, values.size(), 0U);
        std::ranges::copy(values, data.begin() + start);
    }
}

} // namespace kode::lsp
