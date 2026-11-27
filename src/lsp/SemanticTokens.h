#pragma once

#include "TextDocument.h"

#include <QJsonArray>
#include <QList>
#include <QStringList>

#include <optional>

namespace kode::lsp
{

// A server's semantic tokens legend, turned into Kode's styles as Qt
// Creator turns clangd's and qmlls's: a category by token type and
// modifiers (a variable's scope, virtual, static…), and mixins (a
// declaration, a function's definition, an argument it may change).
class SemanticLegend
{
public:
    SemanticLegend() = default;
    // `qml`: types are QML's (qmlls's "type" is a QML type name).
    SemanticLegend(const QStringList &types, const QStringList &modifiers, bool qml);

    // The style of a token of the `type`th type with `modifiers` (bits by
    // the legend's order); none for what highlighting styles already
    // (keywords, strings, operators…).
    [[nodiscard]] std::optional<quint32> style(quint32 type, quint32 modifiers) const;

private:
    // Each type's kind (Kind in the source), by its index.
    QList<qint32> m_kinds;
    // The modifiers that matter, as bits in a token's modifiers.
    quint32 m_declaration{0U};
    quint32 m_definition{0U};
    quint32 m_static{0U};
    quint32 m_virtual{0U};
    quint32 m_readonly{0U};
    quint32 m_constructor{0U};
    quint32 m_functionScope{0U};
    quint32 m_classScope{0U};
    quint32 m_globalScope{0U};
    quint32 m_mutable{0U};
};

// The tokens in `data` (the protocol's relative encoding: five numbers per
// token) by line, for a text of `lineCount` lines.
[[nodiscard]] QList<QList<TextDocument::SemanticToken>>
decodeTokens(const QList<quint32> &data, const SemanticLegend &legend, qsizetype lineCount);

// `data` changed by `edits` (SemanticTokensEdit: start, deleteCount, data).
void applyTokenEdits(QList<quint32> &data, const QJsonArray &edits);

} // namespace kode::lsp
