#pragma once

#include <QObject>

// TextBuffer against a plain QString doing the same edits.
class TestTextBuffer : public QObject
{
    Q_OBJECT
private slots:
    void empty();
    void lines();
    void insertsAndRemoves();
    void coalescesTyping();
    void randomEditsMatchReference();
};
