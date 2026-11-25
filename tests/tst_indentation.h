#pragma once

#include <QObject>

// Indentation: units, block indent/outdent, style detection.
class TestIndentation : public QObject
{
    Q_OBJECT
private slots:
    void tabsAndUnits();
    void indentsAndOutdentsBlocks();
    void detectsTheStyle();
};
