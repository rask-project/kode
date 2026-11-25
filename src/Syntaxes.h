#pragma once

#include "SyntaxHighlighter.h"

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace tekst
{

// The syntaxes the editor can highlight, by section, for the syntax menu.
class Syntaxes : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QStringList sections READ sections CONSTANT)
    // The highlighting themes for the light and the dark appearance.
    Q_PROPERTY(QStringList lightThemes READ lightThemes CONSTANT)
    Q_PROPERTY(QStringList darkThemes READ darkThemes CONSTANT)
public:
    explicit Syntaxes(QObject *parent = nullptr);

    [[nodiscard]] QStringList sections() const;
    [[nodiscard]] static QStringList lightThemes() { return SyntaxHighlighter::themeNames(false); }
    [[nodiscard]] static QStringList darkThemes() { return SyntaxHighlighter::themeNames(true); }
    // The names in `section`, sorted.
    Q_INVOKABLE [[nodiscard]] QStringList names(const QString &section) const;
};

} // namespace tekst
