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

void TestPreferences::keepsOneCustomServerPerSyntax()
{
    kode::Preferences preferences;
    QVERIFY(preferences.addCustomLanguageServer(QStringLiteral("Java"), QStringLiteral("jdtls"), {}));
    QVERIFY(preferences.addCustomLanguageServer(QStringLiteral("Lua"), QStringLiteral("lua-ls"), {}));
    QVERIFY(preferences.addCustomLanguageServer(QStringLiteral("Java"), QStringLiteral("jdtls -data /tmp"), {}));
    const QVariantList servers(kode::Preferences::customLanguageServers());
    QCOMPARE(servers.size(), 2);
    QCOMPARE(servers.at(0).toMap().value(QStringLiteral("syntax")).toString(), QStringLiteral("Lua"));
    QCOMPARE(servers.at(1).toMap().value(QStringLiteral("command")).toString(), QStringLiteral("jdtls -data /tmp"));
    preferences.removeCustomLanguageServer(0);
    QCOMPARE(kode::Preferences::customLanguageServers().size(), 1);
}

void TestPreferences::keepsRemoteHostsAndTheirFolders()
{
    kode::Preferences preferences;
    QVERIFY(preferences.addRemoteHost(QStringLiteral(" me@box ")));
    QVERIFY(preferences.addRemoteHost(QStringLiteral("ssh://me@other:2222")));
    QVERIFY(preferences.addRemoteHost(QStringLiteral("me@box")));
    QVERIFY(!preferences.addRemoteHost(QStringLiteral("-oProxyCommand=x")));
    QVERIFY(!preferences.addRemoteHost(QStringLiteral("two words")));
    QCOMPARE(
        kode::Preferences::remoteHosts(),
        (QStringList{QStringLiteral("me@box"), QStringLiteral("ssh://me@other:2222")})
    );
    QVERIFY(preferences.addRemoteServerFolder(QStringLiteral("me@box"), QStringLiteral("/opt/Qt/bin/")));
    QVERIFY(preferences.addRemoteServerFolder(QStringLiteral("me@box"), QStringLiteral("~/.local/bin")));
    QVERIFY(!preferences.addRemoteServerFolder(QStringLiteral("me@box"), QStringLiteral("relative/bin")));
    QCOMPARE(
        kode::Preferences::remoteServerFolders().value(QStringLiteral("me@box")).toStringList(),
        (QStringList{QStringLiteral("/opt/Qt/bin"), QStringLiteral("~/.local/bin")})
    );
    preferences.removeRemoteServerFolder(QStringLiteral("me@box"), 0);
    QCOMPARE(
        kode::Preferences::remoteServerFolders().value(QStringLiteral("me@box")).toStringList(),
        QStringList{QStringLiteral("~/.local/bin")}
    );
    preferences.removeRemoteHost(QStringLiteral("me@box"));
    QCOMPARE(kode::Preferences::remoteHosts(), QStringList{QStringLiteral("ssh://me@other:2222")});
    QVERIFY(kode::Preferences::remoteServerFolders().isEmpty());
}

QTEST_MAIN(TestPreferences)
