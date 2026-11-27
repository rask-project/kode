#pragma once

#include "SyntaxHighlighter.h"

#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode
{

// The syntaxes the editor can highlight, by section, for the syntax menu.
class Syntaxes : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QStringList sections READ sections CONSTANT)
    // The highlighting themes with a light and a dark version, by family name.
    Q_PROPERTY(QStringList themes READ themes CONSTANT)
    // Every syntax's name, sorted (for picking one in Settings).
    Q_PROPERTY(QStringList all READ all CONSTANT)
public:
    explicit Syntaxes(QObject *parent = nullptr);

    [[nodiscard]] QStringList sections() const;
    [[nodiscard]] static QStringList all();
    [[nodiscard]] static QStringList themes() { return SyntaxHighlighter::themeFamilies(); }
    // The family `theme` names (Kode if none), and its light or dark theme.
    Q_INVOKABLE [[nodiscard]] static QString themeFamily(const QString &theme)
    {
        return SyntaxHighlighter::themeFamily(theme);
    }
    Q_INVOKABLE [[nodiscard]] static QString themeName(const QString &theme, bool dark)
    {
        return SyntaxHighlighter::themeName(theme, dark);
    }
    // The names in `section`, sorted.
    Q_INVOKABLE [[nodiscard]] QStringList names(const QString &section) const;
};

} // namespace kode
