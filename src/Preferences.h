#pragma once

#include <QFont>
#include <QObject>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace tekst
{

// The user's preferences, kept in QSettings (the only copy) and shared by
// every window: the editor's font and theme, wrapping and invisibles, the
// indentation for new documents, the window's blur and opacity.
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
    // A highlighting theme's name; empty: Tekst Light or Dark, as the appearance.
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(bool wrap READ wrap WRITE setWrap NOTIFY changed)
    Q_PROPERTY(bool showInvisibles READ showInvisibles WRITE setShowInvisibles NOTIFY changed)
    // For new documents and files whose indentation cannot be told.
    Q_PROPERTY(bool indentWithTabs READ indentWithTabs WRITE setIndentWithTabs NOTIFY changed)
    Q_PROPERTY(qint32 indentWidth READ indentWidth WRITE setIndentWidth NOTIFY changed)
    Q_PROPERTY(bool windowBlur READ windowBlur WRITE setWindowBlur NOTIFY changed)
    Q_PROPERTY(qreal windowOpacity READ windowOpacity WRITE setWindowOpacity NOTIFY changed)
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
    [[nodiscard]] static bool indentWithTabs();
    void setIndentWithTabs(bool tabs);
    [[nodiscard]] static qint32 indentWidth();
    void setIndentWidth(qint32 width);
    [[nodiscard]] static bool windowBlur();
    void setWindowBlur(bool blur);
    [[nodiscard]] static qreal windowOpacity();
    void setWindowOpacity(qreal opacity);
    [[nodiscard]] static QStringList monospacedFamilies();

signals:
    void changed();

private:
    // Stores `value` under `key` and notifies, unless it is already there.
    void store(const QString &key, const QVariant &value);
};

} // namespace tekst
