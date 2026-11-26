#include "Preferences.h"

#include <QFontDatabase>
#include <QSettings>

#include <algorithm>

namespace tekst
{

namespace
{

// The keys the editor used before this class, kept so saved values stay.
const QString fontFamilyKey{QStringLiteral("editor/fontFamily")};
const QString fontSizeKey{QStringLiteral("view/fontSize")};
const QString themeKey{QStringLiteral("view/theme")};
const QString wrapKey{QStringLiteral("view/wrap")};
const QString showInvisiblesKey{QStringLiteral("view/showInvisibles")};
const QString showIndentGuidesKey{QStringLiteral("view/showIndentGuides")};
const QString reindentOnSaveKey{QStringLiteral("editor/reindentOnSave")};
const QString indentWithTabsKey{QStringLiteral("editor/indentWithTabs")};
const QString indentWidthKey{QStringLiteral("editor/indentWidth")};
const QString windowBlurKey{QStringLiteral("window/blur")};
const QString windowOpacityKey{QStringLiteral("window/opacity")};

constexpr qint32 defaultIndentWidth{4};
constexpr qint32 largestIndentWidth{16};
constexpr qreal defaultWindowOpacity{1.0};
constexpr qreal leastWindowOpacity{0.5};

} // namespace

Preferences::Preferences(QObject *parent) : QObject(parent) {}

void Preferences::store(const QString &key, const QVariant &value)
{
    QSettings settings;
    if (settings.value(key) == value) {
        return;
    }
    settings.setValue(key, value);
    emit changed();
}

QString Preferences::fontFamily()
{
    return QSettings().value(fontFamilyKey, QFontDatabase::systemFont(QFontDatabase::FixedFont).family()).toString();
}

void Preferences::setFontFamily(const QString &family)
{
    store(fontFamilyKey, family);
}

qint32 Preferences::fontSize()
{
    return std::clamp(QSettings().value(fontSizeKey, defaultFontSize()).toInt(), smallestFontSize(), largestFontSize());
}

void Preferences::setFontSize(qint32 size)
{
    store(fontSizeKey, std::clamp(size, smallestFontSize(), largestFontSize()));
}

QFont Preferences::font()
{
    return QFont{fontFamily(), fontSize()};
}

QString Preferences::theme()
{
    return QSettings().value(themeKey).toString();
}

void Preferences::setTheme(const QString &theme)
{
    store(themeKey, theme);
}

bool Preferences::wrap()
{
    return QSettings().value(wrapKey, false).toBool();
}

void Preferences::setWrap(bool wrap)
{
    store(wrapKey, wrap);
}

bool Preferences::showInvisibles()
{
    return QSettings().value(showInvisiblesKey, false).toBool();
}

void Preferences::setShowInvisibles(bool show)
{
    store(showInvisiblesKey, show);
}

bool Preferences::showIndentGuides()
{
    return QSettings().value(showIndentGuidesKey, true).toBool();
}

void Preferences::setShowIndentGuides(bool show)
{
    store(showIndentGuidesKey, show);
}

bool Preferences::reindentOnSave()
{
    return QSettings().value(reindentOnSaveKey, false).toBool();
}

void Preferences::setReindentOnSave(bool reindent)
{
    store(reindentOnSaveKey, reindent);
}

bool Preferences::indentWithTabs()
{
    return QSettings().value(indentWithTabsKey, false).toBool();
}

void Preferences::setIndentWithTabs(bool tabs)
{
    store(indentWithTabsKey, tabs);
}

qint32 Preferences::indentWidth()
{
    return std::clamp(QSettings().value(indentWidthKey, defaultIndentWidth).toInt(), 1, largestIndentWidth);
}

void Preferences::setIndentWidth(qint32 width)
{
    store(indentWidthKey, std::clamp(width, 1, largestIndentWidth));
}

bool Preferences::windowBlur()
{
    return QSettings().value(windowBlurKey, true).toBool();
}

void Preferences::setWindowBlur(bool blur)
{
    store(windowBlurKey, blur);
}

qreal Preferences::windowOpacity()
{
    return std::clamp(QSettings().value(windowOpacityKey, defaultWindowOpacity).toReal(), leastWindowOpacity, 1.0);
}

void Preferences::setWindowOpacity(qreal opacity)
{
    store(windowOpacityKey, std::clamp(opacity, leastWindowOpacity, 1.0));
}

QStringList Preferences::monospacedFamilies()
{
    QStringList families{QFontDatabase::families()};
    families.removeIf([](const QString &family) {
        return family.startsWith(QLatin1Char('.')) || !QFontDatabase::isFixedPitch(family);
    });
    std::ranges::sort(families, [](const QString &a, const QString &b) { return a.localeAwareCompare(b) < 0; });
    return families;
}

} // namespace tekst
