#pragma once

#include <QFont>
#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode
{

// The user's preferences, kept in QSettings (the only copy) and shared by
// every window: the editor's font and theme, wrapping, invisibles, indent guides and re-indenting on save, the
// indentation for new documents, the window's blur and opacity, hidden files in the sidebar and whether it follows the
// editor.
class Preferences : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY changed)
    Q_PROPERTY(qint32 fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(qint32 defaultFontSize READ defaultFontSize CONSTANT)
    Q_PROPERTY(qint32 smallestFontSize READ smallestFontSize CONSTANT)
    Q_PROPERTY(qint32 largestFontSize READ largestFontSize CONSTANT)
    // The editor's font: fontFamily at fontSize.
    Q_PROPERTY(QFont font READ font NOTIFY changed)
    // A highlighting theme family; its light or dark theme follows the appearance.
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(bool wrap READ wrap WRITE setWrap NOTIFY changed)
    Q_PROPERTY(bool showInvisibles READ showInvisibles WRITE setShowInvisibles NOTIFY changed)
    Q_PROPERTY(bool showIndentGuides READ showIndentGuides WRITE setShowIndentGuides NOTIFY changed)
    // Re-Indent the whole text before saving it.
    Q_PROPERTY(bool reindentOnSave READ reindentOnSave WRITE setReindentOnSave NOTIFY changed)
    // For new documents and files whose indentation cannot be told.
    Q_PROPERTY(bool indentWithTabs READ indentWithTabs WRITE setIndentWithTabs NOTIFY changed)
    Q_PROPERTY(qint32 indentWidth READ indentWidth WRITE setIndentWidth NOTIFY changed)
    Q_PROPERTY(bool windowBlur READ windowBlur WRITE setWindowBlur NOTIFY changed)
    Q_PROPERTY(qreal windowOpacity READ windowOpacity WRITE setWindowOpacity NOTIFY changed)
    // The code area shows the window's background and blur (else a solid background; the bars stay translucent).
    Q_PROPERTY(bool transparentEditor READ transparentEditor WRITE setTransparentEditor NOTIFY changed)
    // The sidebar lists hidden files and folders too.
    Q_PROPERTY(bool showHiddenFiles READ showHiddenFiles WRITE setShowHiddenFiles NOTIFY changed)
    // The sidebar follows the document in view (opens its folders, scrolls to it), as Qt Creator's.
    Q_PROPERTY(bool syncWithEditor READ syncWithEditor WRITE setSyncWithEditor NOTIFY changed)
    // The sidebar's text and icons: 0 small, 1 medium, 2 large (as the system's sidebar icon size).
    Q_PROPERTY(qint32 sidebarSize READ sidebarSize WRITE setSidebarSize NOTIFY changed)
    // The installed monospaced font families, sorted.
    Q_PROPERTY(QStringList monospacedFamilies READ monospacedFamilies CONSTANT)
public:
    explicit Preferences(QObject *parent = nullptr);

    [[nodiscard]] static QString fontFamily();
    void setFontFamily(const QString &family);
    [[nodiscard]] static qint32 fontSize();
    void setFontSize(qint32 size);
    [[nodiscard]] static qint32 defaultFontSize() noexcept { return 13; }
    [[nodiscard]] static qint32 smallestFontSize() noexcept { return 6; }
    [[nodiscard]] static qint32 largestFontSize() noexcept { return 72; }
    [[nodiscard]] static QFont font();
    [[nodiscard]] static QString theme();
    void setTheme(const QString &theme);
    [[nodiscard]] static bool wrap();
    void setWrap(bool wrap);
    [[nodiscard]] static bool showInvisibles();
    void setShowInvisibles(bool show);
    [[nodiscard]] static bool showIndentGuides();
    void setShowIndentGuides(bool show);
    [[nodiscard]] static bool reindentOnSave();
    void setReindentOnSave(bool reindent);
    [[nodiscard]] static bool indentWithTabs();
    void setIndentWithTabs(bool tabs);
    [[nodiscard]] static qint32 indentWidth();
    void setIndentWidth(qint32 width);
    [[nodiscard]] static bool windowBlur();
    void setWindowBlur(bool blur);
    [[nodiscard]] static qreal windowOpacity();
    void setWindowOpacity(qreal opacity);
    [[nodiscard]] static bool transparentEditor();
    void setTransparentEditor(bool transparent);
    [[nodiscard]] static bool showHiddenFiles();
    void setShowHiddenFiles(bool show);
    [[nodiscard]] static bool syncWithEditor();
    void setSyncWithEditor(bool sync);
    [[nodiscard]] static qint32 sidebarSize();
    void setSidebarSize(qint32 size);
    [[nodiscard]] static QStringList monospacedFamilies();

signals:
    void changed();

private:
    // Stores `value` under `key` and notifies, unless it is already there.
    void store(const QString &key, const QVariant &value);
};

} // namespace kode
