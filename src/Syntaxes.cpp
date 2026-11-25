#include "Syntaxes.h"

#include "SyntaxHighlighter.h"

#include <KSyntaxHighlighting/Definition>

#include <algorithm>

namespace tekst
{

Syntaxes::Syntaxes(QObject *parent) : QObject(parent) {}

QStringList Syntaxes::sections() const
{
    QStringList sections;
    for (const KSyntaxHighlighting::Definition &definition : SyntaxHighlighter::definitions()) {
        if (!sections.contains(definition.translatedSection())) {
            sections.append(definition.translatedSection());
        }
    }
    std::ranges::sort(sections, [](const QString &a, const QString &b) { return a.localeAwareCompare(b) < 0; });
    return sections;
}

QStringList Syntaxes::names(const QString &section) const
{
    QStringList names;
    for (const KSyntaxHighlighting::Definition &definition : SyntaxHighlighter::definitions()) {
        if (definition.translatedSection() == section) {
            names.append(definition.name());
        }
    }
    std::ranges::sort(names, [](const QString &a, const QString &b) { return a.localeAwareCompare(b) < 0; });
    return names;
}

} // namespace tekst
