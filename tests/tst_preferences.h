#pragma once

#include <QObject>
#include <QTemporaryDir>

// Preferences: defaults, clamping, change notifications, shared storage.
class TestPreferences : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void hasDefaults();
    void clampsAndNotifiesOnlyOnChange();
    void instancesShareTheSettings();

private:
    QTemporaryDir m_settingsDir;
};
