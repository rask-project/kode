// Measures how long the work that grows with the file blocks the GUI thread
// (Stage 10), on one file, and prints one line of results:
//
//   kode-work-bench FILE
//       size_mb read_ms decode_ms set_text_ms load_blocking_ms load_ms
//       guess_ms highlight_all_ms search_ms search_sliced_ms replace_all_ms undo_ms
//       reindent_ms save_ms
//
// read: the file's bytes; decode: detecting and decoding them, line endings
// made '\n'; set text: the buffer and its line index, the indentation
// detected (Document::load() is the three); load blocking: what
// Document::load() blocks the GUI thread for (a large file is read on a
// worker); load: until its text is there; guess: highlighting a view's
// lines at the end from a guessed state (what a jump there waits for, the
// exact highlighting not there yet); highlight all: every line's
// state as C++, from the top; search: finding "Q_OBJECT" in one go;
// search sliced: the same in the view's 3 ms slices from the event loop,
// until done (the wall time, not blocking); replace all and undo: the
// matches replaced by the view, one edit; reindent: what saving with
// "re-indent on save" adds after a line typed in the middle (it re-indents
// the lines edited), highlighting ready; save: writing it to a
// temporary file.
#include "Document.h"
#include "EditorView.h"
#include "SyntaxHighlighter.h"
#include "TextBuffer.h"
#include "TextCodec.h"
#include "TextDocument.h"
#include "TextSearch.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>
#include <cstdio>

namespace
{

const QString pattern{QStringLiteral("Q_OBJECT")};
// Lines in a view.
constexpr qsizetype viewLines{60};

qreal measure(const auto &work)
{
    QElapsedTimer timer;
    timer.start();
    work();
    return static_cast<qreal>(timer.nsecsElapsed()) / 1.0e6;
}

} // namespace

int main(int argc, char *argv[])
{
    const QGuiApplication app{argc, argv};
    const QStringList arguments{QCoreApplication::arguments()};
    if (arguments.size() != 2) {
        std::fputs("usage: kode-work-bench FILE\n", stderr);
        return 2;
    }
    const QString &path{arguments.at(1)};
    const qreal sizeMegabytes{static_cast<qreal>(QFile{path}.size()) / (1024.0 * 1024.0)};

    // Document::load()'s steps, one by one.
    QByteArray bytes;
    const qreal readMs{measure([&] {
        QFile file{path};
        if (file.open(QIODevice::ReadOnly)) {
            bytes = file.readAll();
        }
    })};
    QString text;
    const qreal decodeMs{measure([&] {
        text = kode::codec::decodeFile(bytes).value_or(kode::codec::Decoded{}).text;
        if (text.contains(QStringLiteral("\r\n"))) {
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        }
    })};
    kode::TextDocument scratch;
    const qreal setTextMs{measure([&] {
        scratch.setText(text);
        scratch.detectIndentation();
    })};

    kode::Document document;
    QElapsedTimer loadTimer;
    loadTimer.start();
    bool loaded{false};
    const qreal loadBlockingMs{measure([&] { loaded = document.load(QUrl::fromLocalFile(path)); })};
    if (!loaded) {
        return 1;
    }
    while (document.loading()) {
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
    }
    const qreal loadMs{static_cast<qreal>(loadTimer.nsecsElapsed()) / 1.0e6};
    kode::TextDocument &textDocument{*document.textDocument()};
    textDocument.setSyntax(QStringLiteral("C++"));
    const qsizetype lastLine{textDocument.buffer().lineCount() - 1};
    const qreal guessMs{measure([&] {
        kode::SyntaxHighlighter::Guess guess;
        const qsizetype first{std::max(lastLine - viewLines, qsizetype{0})};
        textDocument.highlighter().guess(guess, first, lastLine, QDeadlineTimer{QDeadlineTimer::Forever});
        for (qsizetype line{first}; line < lastLine; ++line) {
            std::ignore = textDocument.highlighter()
                              .formats(line, textDocument.buffer().line(line), guess.states.at(line - guess.first));
        }
    })};
    const qreal highlightMs{measure([&] {
        std::ignore = textDocument.highlighter().advance(lastLine, QDeadlineTimer{QDeadlineTimer::Forever});
    })};

    kode::EditorView view;
    view.setDocument(&textDocument);
    const qreal searchMs{measure([&] {
        kode::TextSearch search{&textDocument.buffer()};
        search.setQuery(pattern, true, false);
        std::ignore = search.advance(QDeadlineTimer{QDeadlineTimer::Forever});
    })};

    const qreal searchSlicedMs{measure([&] {
        view.setSearchPattern(pattern);
        while (!view.searchComplete() && view.searchMatchCount() < kode::TextSearch::maxMatches) {
            QCoreApplication::processEvents(QEventLoop::AllEvents);
        }
    })};
    const qreal replaceAllMs{measure([&] { view.replaceAll(QStringLiteral("Q_GADGET")); })};
    const qreal undoMs{measure([&] { view.undo(); })};
    view.setSearchPattern({});

    // Re-indenting's own cost, after a line typed in the middle: highlighting
    // brought up to date first, off the clock.
    textDocument.markSaved();
    view.setCursorPosition(textDocument.buffer().lineStart(textDocument.buffer().lineCount() / 2));
    view.insert(QStringLiteral("x"));
    std::ignore = textDocument.highlighter().advance(lastLine, QDeadlineTimer{QDeadlineTimer::Forever});
    const qreal reindentMs{measure([&] { view.reindentEdited(); })};

    const QTemporaryDir folder;
    const qreal saveMs{measure([&] {
        std::ignore = document.saveAs(QUrl::fromLocalFile(folder.filePath(QStringLiteral("saved.cpp"))));
    })};

    std::printf(
        "%.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f\n",
        sizeMegabytes,
        readMs,
        decodeMs,
        setTextMs,
        loadBlockingMs,
        loadMs,
        guessMs,
        highlightMs,
        searchMs,
        searchSlicedMs,
        replaceAllMs,
        undoMs,
        reindentMs,
        saveMs
    );
    return 0;
}
