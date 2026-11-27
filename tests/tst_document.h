#pragma once

#include <QObject>
#include <QTemporaryDir>
#include <QUrl>

// Document against an EditorView (no window needed).
class TestDocument : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void roundTripsUtf8();
    void keepsCrlf();
    void keepsLfAndFinalNewline();
    void keepsNonBreakingSpaces();
    void reportsMissingFile();
    void readsWindows1252WhenNotUtf8();
    void readsUtf8CutAcrossBlocks();
    void readsAFileEndingInACutCharacter();
    void keepsTheByteOrderMark();
    void reopensWithAnotherEncoding();
    void convertsEncodingAndLineEnding();
    void refusesCharactersTheEncodingLacks();
    void tracksModified();
    void clearsToUntitled();
    void startsAFileNotWrittenYet();
    void reloadsAChangeOnDisk();
    void reportsAChangeOnDiskWhenModified();
    void ignoresItsOwnSaves();
    void readsALargeFileOffTheGuiThread();
    void namesALargeFileWhileReadingIt();
    void refusesToSaveWhileReading();
    void reportsALargeFileItCannotRead();

private:
    [[nodiscard]] QString path(const QString &name) const;
    [[nodiscard]] QUrl writeLargeFile(const QString &name) const;

    QTemporaryDir m_dir;
};
