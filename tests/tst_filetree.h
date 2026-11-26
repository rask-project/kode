#pragma once

#include <QObject>
#include <QTemporaryDir>

// FileTree: folders first, names in the Finder's order, hidden files on
// request; the file operations of the sidebar's menu.
class TestFileTree : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void sortsFoldersFirstByName();
    void showsHiddenFilesOnRequest();
    void dropsTheTrailingSlash();
    void createsUntitledEntries();
    void renamesUnlessTaken();
    void copiesAndDuplicates();
    void movesButNotIntoItself();
    void pastesACutFileByMovingIt();
    void knowsCodeFilesByExtensionOrName();

private:
    QTemporaryDir m_dir;
};
