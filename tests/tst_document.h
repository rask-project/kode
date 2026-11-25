#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTemporaryDir>

#include <memory>

// Document against a real TextEdit's document, offscreen.
class TestDocument : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void roundTripsUtf8();
    void keepsCrlf();
    void keepsLfAndFinalNewline();
    void keepsNonBreakingSpaces();
    void reportsMissingFile();
    void rejectsInvalidUtf8();
    void tracksModified();
    void clearsToUntitled();

private:
    [[nodiscard]] QString path(const QString &name) const;

    QTemporaryDir m_dir;
    QQmlEngine m_engine;
    std::unique_ptr<QObject> m_editor;
};
