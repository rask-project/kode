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
    // Every highlighting theme, light and dark ones together.
    Q_PROPERTY(QStringList themes READ themes CONSTANT)
public:
    explicit Syntaxes(QObject *parent = nullptr);

    [[nodiscard]] QStringList sections() const;
    [[nodiscard]] static QStringList themes() { return SyntaxHighlighter::themeNames(); }
    // The names in `section`, sorted.
    Q_INVOKABLE [[nodiscard]] QStringList names(const QString &section) const;
};

} // namespace tekst
