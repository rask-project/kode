#include "tst_preferences.h"

#include "Preferences.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTest>

void TestPreferences::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void TestPreferences::init()
{
    QSettings().clear();
}

void TestPreferences::hasDefaults()
{
    const kode::Preferences preferences;
    QCOMPARE(preferences.fontSize(), kode::Preferences::defaultFontSize());
    QVERIFY(!preferences.fontFamily().isEmpty());
    QVERIFY(preferences.theme().isEmpty());
    QVERIFY(!preferences.wrap());
    QCOMPARE(preferences.indentWidth(), 4);
    QVERIFY(preferences.windowBlur());
    QVERIFY(preferences.monospacedFamilies().contains(QStringLiteral("Menlo")));
}

void TestPreferences::clampsAndNotifiesOnlyOnChange()
{
    kode::Preferences preferences;
    const QSignalSpy changes{&preferences, &kode::Preferences::changed};
    preferences.setFontSize(200);
    QCOMPARE(preferences.fontSize(), kode::Preferences::largestFontSize());
    preferences.setFontSize(200);
    preferences.setWindowOpacity(0.1);
    QCOMPARE(preferences.windowOpacity(), 0.5);
    QCOMPARE(changes.count(), 2);
}

void TestPreferences::instancesShareTheSettings()
{
    kode::Preferences first;
    first.setTheme(QStringLiteral("Dracula"));
    first.setIndentWithTabs(true);
    const kode::Preferences second;
    QCOMPARE(second.theme(), QStringLiteral("Dracula"));
    QVERIFY(second.indentWithTabs());
}

QTEST_MAIN(TestPreferences)
