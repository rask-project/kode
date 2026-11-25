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
    const tekst::Preferences preferences;
    QCOMPARE(preferences.fontSize(), tekst::Preferences::defaultFontSize());
    QVERIFY(!preferences.fontFamily().isEmpty());
    QVERIFY(preferences.theme().isEmpty());
    QVERIFY(!preferences.wrap());
    QCOMPARE(preferences.indentWidth(), 4);
    QVERIFY(preferences.windowBlur());
    QVERIFY(preferences.monospacedFamilies().contains(QStringLiteral("Menlo")));
}

void TestPreferences::clampsAndNotifiesOnlyOnChange()
{
    tekst::Preferences preferences;
    const QSignalSpy changes{&preferences, &tekst::Preferences::changed};
    preferences.setFontSize(200);
    QCOMPARE(preferences.fontSize(), tekst::Preferences::largestFontSize());
    preferences.setFontSize(200);
    preferences.setWindowOpacity(0.1);
    QCOMPARE(preferences.windowOpacity(), 0.5);
    QCOMPARE(changes.count(), 2);
}

void TestPreferences::instancesShareTheSettings()
{
    tekst::Preferences first;
    first.setTheme(QStringLiteral("Dracula"));
    first.setIndentWithTabs(true);
    const tekst::Preferences second;
    QCOMPARE(second.theme(), QStringLiteral("Dracula"));
    QVERIFY(second.indentWithTabs());
}

QTEST_MAIN(TestPreferences)
