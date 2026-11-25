#pragma once

#include <QObject>
#include <QTemporaryDir>

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
    void rejectsInvalidUtf8();
    void tracksModified();
    void clearsToUntitled();

private:
    [[nodiscard]] QString path(const QString &name) const;

    QTemporaryDir m_dir;
};
