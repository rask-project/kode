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
    // With clangd installed: completions after "s." list the struct's member.
    void clangdCompletesAMember();
    // clangd's legend: a variable's scope, virtual, static, a constructor and mixins.
    void legendStylesAsQtCreator();
    // The protocol's relative encoding, by line; types without a style skipped.
    void decodesRelativeTokens();
    void appliesTokenEdits();
    // Tokens move with an edit on their line; those it touches go; lines split.
    void semanticTokensMoveWithEdits();
    // With clangd installed: a type's name styled as a type.
    void clangdStylesAType();
};
