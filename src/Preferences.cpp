#include "Preferences.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QSettings>
#include <QVariantMap>

#include <algorithm>

namespace kode
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
const QString showMinimapKey{QStringLiteral("view/showMinimap")};
const QString reindentOnSaveKey{QStringLiteral("editor/reindentOnSave")};
const QString indentWithTabsKey{QStringLiteral("editor/indentWithTabs")};
const QString indentWidthKey{QStringLiteral("editor/indentWidth")};
const QString windowBlurKey{QStringLiteral("window/blur")};
const QString windowOpacityKey{QStringLiteral("window/opacity")};
const QString transparentEditorKey{QStringLiteral("window/transparentEditor")};
const QString showHiddenFilesKey{QStringLiteral("sidebar/showHiddenFiles")};
const QString syncWithEditorKey{QStringLiteral("sidebar/syncWithEditor")};
const QString sidebarSizeKey{QStringLiteral("sidebar/size")};
const QString languageServersKey{QStringLiteral("languageServers/enabled")};
const QString languageServerFoldersKey{QStringLiteral("languageServers/folders")};
const QString clangdThreadsKey{QStringLiteral("languageServers/clangdThreads")};
const QString customLanguageServersKey{QStringLiteral("languageServers/custom")};

constexpr qint32 defaultIndentWidth{4};
constexpr qint32 largestIndentWidth{16};
constexpr qreal defaultWindowOpacity{1.0};
constexpr qreal leastWindowOpacity{0.5};
constexpr qint32 largestSidebarSize{2};
// Analyzing in the background without taking the machine.
constexpr qint32 mostClangdThreads{2};

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

bool Preferences::showMinimap()
{
    return QSettings().value(showMinimapKey, true).toBool();
}

void Preferences::setShowMinimap(bool show)
{
    store(showMinimapKey, show);
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

bool Preferences::transparentEditor()
{
    return QSettings().value(transparentEditorKey, true).toBool();
}

void Preferences::setTransparentEditor(bool transparent)
{
    store(transparentEditorKey, transparent);
}

bool Preferences::showHiddenFiles()
{
    return QSettings().value(showHiddenFilesKey, false).toBool();
}

void Preferences::setShowHiddenFiles(bool show)
{
    store(showHiddenFilesKey, show);
}

bool Preferences::syncWithEditor()
{
    return QSettings().value(syncWithEditorKey, true).toBool();
}

void Preferences::setSyncWithEditor(bool sync)
{
    store(syncWithEditorKey, sync);
}

qint32 Preferences::sidebarSize()
{
    return std::clamp(QSettings().value(sidebarSizeKey, 1).toInt(), 0, largestSidebarSize);
}

void Preferences::setSidebarSize(qint32 size)
{
    store(sidebarSizeKey, std::clamp(size, 0, largestSidebarSize));
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

bool Preferences::languageServers()
{
    return QSettings().value(languageServersKey, true).toBool();
}

void Preferences::setLanguageServers(bool on)
{
    store(languageServersKey, on);
}

QStringList Preferences::languageServerFolders()
{
    return QSettings().value(languageServerFoldersKey).toStringList();
}

void Preferences::addLanguageServerFolder(const QUrl &folder)
{
    addLanguageServerPath(folder.toLocalFile());
}

bool Preferences::addLanguageServerPath(const QString &path)
{
    const QString trimmed{path.trimmed()};
    const QString expanded{trimmed.startsWith(u'~') ? QDir::homePath() + trimmed.sliced(1) : trimmed};
    if (expanded.isEmpty() || !QFileInfo{expanded}.isDir()) {
        return false;
    }
    const QString clean{QDir::cleanPath(QFileInfo{expanded}.absoluteFilePath())};
    QStringList folders{languageServerFolders()};
    if (!folders.contains(clean)) {
        folders.append(clean);
        store(languageServerFoldersKey, folders);
    }
    return true;
}

void Preferences::removeLanguageServerFolder(qint32 index)
{
    QStringList folders{languageServerFolders()};
    if (index >= 0 && index < folders.size()) {
        folders.removeAt(index);
        store(languageServerFoldersKey, folders);
    }
}

QVariantList Preferences::customLanguageServers()
{
    return QSettings().value(customLanguageServersKey).toList();
}

bool Preferences::addCustomLanguageServer(const QString &syntax, const QString &command, const QString &languageId)
{
    if (syntax.isEmpty() || command.trimmed().isEmpty()) {
        return false;
    }
    QVariantList servers{customLanguageServers()};
    servers.removeIf([&syntax](const QVariant &item) {
        return item.toMap().value(QStringLiteral("syntax")).toString() == syntax;
    });
    servers.append(QVariantMap{
        {QStringLiteral("syntax"), syntax},
        {QStringLiteral("command"), command.trimmed()},
        {QStringLiteral("languageId"), languageId.trimmed()},
    });
    store(customLanguageServersKey, servers);
    return true;
}

void Preferences::removeCustomLanguageServer(qint32 index)
{
    QVariantList servers{customLanguageServers()};
    if (index >= 0 && index < servers.size()) {
        servers.removeAt(index);
        store(customLanguageServersKey, servers);
    }
}

qint32 Preferences::clangdThreads()
{
    return std::clamp(QSettings().value(clangdThreadsKey, 1).toInt(), 1, mostClangdThreads);
}

void Preferences::setClangdThreads(qint32 threads)
{
    store(clangdThreadsKey, std::clamp(threads, 1, mostClangdThreads));
}

} // namespace kode
