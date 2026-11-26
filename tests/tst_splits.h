#pragma once

#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>

#include <memory>

// Splits with stand-in views (QtObjects with the properties it reads) and
// OpenDocuments: splitting, removing splits, the saved layout, the list of
// open documents, the history of a split.
class TestSplits : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void splitsViews();
    void removesSplits();
    void activatesViewsInTurn();
    void savesAndRestoresTheLayout();
    void opensEachFileOnce();
    void reportsMissingFiles();
    void keepsTheMostRecentFirst();
    void numbersUntitledDocuments();
    void goesBackAndForward();
    void visitingDropsWhatWasAhead();
    void forgetsClosedDocuments();
    void keepsTheSplitsRecentDocuments();

private:
    QQmlEngine m_engine;
    QTemporaryDir m_files;
    // After the engine: destroyed before it.
    std::unique_ptr<QQmlComponent> m_view;
};
