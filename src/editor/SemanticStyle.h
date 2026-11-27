#pragma once

#include <KSyntaxHighlighting/Theme>

#include <QLatin1StringView>

#include <array>

// What a language server says each name is (semantic highlighting), as Qt
// Creator draws it: a category and mixins (bold declarations…). A token's
// style holds its category in the low byte, its mixins above.
namespace kode::semantic
{

enum Category : quint8 {
    Type,
    Namespace,
    Local,
    Parameter,
    Field,
    Global,
    Enumeration,
    Function,
    VirtualMethod,
    StaticMember,
    Macro,
    Label,
    QmlTypeId,
    QmlLocalId,
    QmlExternalId,
    QmlRootObjectProperty,
    QmlScopeObjectProperty,
    QmlExternalObjectProperty,
    QmlStateName,
    JsScopeVar,
    JsImportVar,
    JsGlobalVar,
    // A QML property's name where it is bound ("width: …").
    Binding,
    Keyword,
    // Plain text, over what the highlighting would style.
    Plain,
    CategoryCount,
};

inline constexpr quint32 categoryMask{0xffU};
inline constexpr quint32 Declaration{1U << 8U};
inline constexpr quint32 FunctionDefinition{1U << 9U};
inline constexpr quint32 OutputArgument{1U << 10U};

// Qt Creator's names, as its color schemes and Kode's themes ("semantic-styles") have them.
inline constexpr std::array<QLatin1StringView, CategoryCount> names{
    QLatin1StringView{"Type"},
    QLatin1StringView{"Namespace"},
    QLatin1StringView{"Local"},
    QLatin1StringView{"Parameter"},
    QLatin1StringView{"Field"},
    QLatin1StringView{"Global"},
    QLatin1StringView{"Enumeration"},
    QLatin1StringView{"Function"},
    QLatin1StringView{"VirtualMethod"},
    QLatin1StringView{"Static"},
    QLatin1StringView{"Macro"},
    QLatin1StringView{"Label"},
    QLatin1StringView{"QmlTypeId"},
    QLatin1StringView{"QmlLocalId"},
    QLatin1StringView{"QmlExternalId"},
    QLatin1StringView{"QmlRootObjectProperty"},
    QLatin1StringView{"QmlScopeObjectProperty"},
    QLatin1StringView{"QmlExternalObjectProperty"},
    QLatin1StringView{"QmlStateName"},
    QLatin1StringView{"JsScopeVar"},
    QLatin1StringView{"JsImportVar"},
    QLatin1StringView{"JsGlobalVar"},
    QLatin1StringView{"Binding"},
    QLatin1StringView{"Keyword"},
    QLatin1StringView{"Plain"},
};

// The mixins' names, by bit from Declaration up.
inline constexpr std::array<QLatin1StringView, 3> mixinNames{
    QLatin1StringView{"Declaration"},
    QLatin1StringView{"FunctionDefinition"},
    QLatin1StringView{"OutputArgument"},
};

// For a theme without them (not Kode's): the nearest text style.
inline constexpr std::array<KSyntaxHighlighting::Theme::TextStyle, CategoryCount> fallbacks{
    KSyntaxHighlighting::Theme::DataType,     KSyntaxHighlighting::Theme::DataType,
    KSyntaxHighlighting::Theme::Variable,     KSyntaxHighlighting::Theme::Variable,
    KSyntaxHighlighting::Theme::Variable,     KSyntaxHighlighting::Theme::Variable,
    KSyntaxHighlighting::Theme::Constant,     KSyntaxHighlighting::Theme::Function,
    KSyntaxHighlighting::Theme::Function,     KSyntaxHighlighting::Theme::Function,
    KSyntaxHighlighting::Theme::Preprocessor, KSyntaxHighlighting::Theme::Variable,
    KSyntaxHighlighting::Theme::DataType,     KSyntaxHighlighting::Theme::Variable,
    KSyntaxHighlighting::Theme::Variable,     KSyntaxHighlighting::Theme::Variable,
    KSyntaxHighlighting::Theme::Variable,     KSyntaxHighlighting::Theme::Variable,
    KSyntaxHighlighting::Theme::Variable,     KSyntaxHighlighting::Theme::Variable,
    KSyntaxHighlighting::Theme::Variable,     KSyntaxHighlighting::Theme::Variable,
    KSyntaxHighlighting::Theme::Attribute,    KSyntaxHighlighting::Theme::Keyword,
    KSyntaxHighlighting::Theme::Normal,
};

} // namespace kode::semantic
