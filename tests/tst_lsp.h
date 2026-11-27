#pragma once

#include <QObject>

// The Language Server Protocol's framing, and diagnostics following edits.
class TestLsp : public QObject
{
    Q_OBJECT
private slots:
    void readsMessagesArrivingInPieces();
    void readsSeveralMessagesAtOnce();
    void skipsAMalformedMessage();
    void diagnosticsMoveWithEdits();
    void newTextClearsDiagnostics();
    // With clangd installed: an error in a file shows as a diagnostic, and
    // goes once fixed.
    void clangdReportsAnError();
};
