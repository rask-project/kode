// Measures one editor on one file and prints one line of results:
//
//   tekst-editor-bench textedit|editorview FILE
//       mode size_mb open_ms key_update_avg key_update_max key_frame_max
//       scroll_update_avg scroll_update_max rss_delta_mb
//   tekst-editor-bench try FILE     opens FILE in EditorView to try it by hand
//
// "open" is from handing the text to the editor to its first frame on
// screen. For a keystroke in the middle of the text and for a jump of the
// view, "update" is the scene's update for that frame (see Frame: the
// editor's own cost), "frame" until it is on screen (frameSwapped, which
// waits for the display: 16.7 ms at 60 Hz when nothing is late). Reading and decoding the
// file happen before the clock starts, the same for both editors.
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickView>
#include <QStringDecoder>
#include <QTimer>

#include <mach/mach.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <numeric>

namespace
{

constexpr qint32 keystrokes{100};
constexpr qint32 scrollSteps{60};
constexpr qint32 frameTimeoutMs{300000};

qreal residentMegabytes()
{
    mach_task_basic_info info{};
    mach_msg_type_number_t count{MACH_TASK_BASIC_INFO_COUNT};
    const kern_return_t result{
        task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count)
    };
    return result == KERN_SUCCESS ? static_cast<qreal>(info.resident_size) / (1024.0 * 1024.0) : 0.0;
}

// The next frame, in ms of `timer`: how long the scene took to update
// (from the end of animations, on the GUI thread, through polishing the
// items and synchronizing their nodes, to the end of synchronization: the
// editor's own cost, layout and scene graph included) and when the frame was
// on screen.
struct Frame {
    qreal updateMs{-1.0};
    qreal frameMs{-1.0};
};

// Waits for the next frame `window` puts on screen; frameMs < 0 on timeout.
Frame waitForFrame(QQuickWindow &window, const QElapsedTimer &timer)
{
    std::atomic<qint64> animated{-1};
    std::atomic<qint64> synchronized{-1};
    const QMetaObject::Connection animatedConnection{QObject::connect(
        &window,
        &QQuickWindow::afterAnimating,
        &window,
        [&animated, &timer] {
            qint64 none{-1};
            animated.compare_exchange_strong(none, timer.nsecsElapsed());
        },
        Qt::DirectConnection
    )};
    // From the render thread.
    const QMetaObject::Connection synchronizedConnection{QObject::connect(
        &window,
        &QQuickWindow::afterSynchronizing,
        &window,
        [&synchronized, &timer] {
            qint64 none{-1};
            synchronized.compare_exchange_strong(none, timer.nsecsElapsed());
        },
        Qt::DirectConnection
    )};
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&loop] { loop.exit(1); });
    QObject::connect(&window, &QQuickWindow::frameSwapped, &loop, [&loop] { loop.exit(0); });
    window.update();
    timeout.start(frameTimeoutMs);
    const bool swapped{loop.exec() == 0};
    QObject::disconnect(animatedConnection);
    QObject::disconnect(synchronizedConnection);
    if (!swapped) {
        return {};
    }
    return {
        .updateMs = static_cast<qreal>(synchronized.load() - animated.load()) / 1.0e6,
        .frameMs = static_cast<qreal>(timer.nsecsElapsed()) / 1.0e6
    };
}

qreal average(const QList<qreal> &values)
{
    return std::accumulate(values.cbegin(), values.cend(), 0.0) / static_cast<qreal>(values.size());
}

QString readText(const QString &path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QStringDecoder decoder{QStringDecoder::Utf8};
    QString text{decoder.decode(file.readAll())};
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return text;
}

} // namespace

int main(int argc, char *argv[])
{
    const QGuiApplication app{argc, argv};
    const QStringList arguments{QCoreApplication::arguments()};
    if (arguments.size() != 3) {
        std::fputs("usage: tekst-editor-bench textedit|editorview|try FILE\n", stderr);
        return 2;
    }
    const QString &mode{arguments.at(1)};
    const QString text{readText(arguments.at(2))};
    const qreal sizeMegabytes{static_cast<qreal>(QFile{arguments.at(2)}.size()) / (1024.0 * 1024.0)};

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.loadFromModule("TekstEditorBench", mode == QStringLiteral("textedit") ? "TextEditCase" : "EditorViewCase");
    QObject *const root{view.rootObject()};
    if (root == nullptr) {
        return 1;
    }
    view.resize(900, 700);
    view.setPosition(300, 120);
    view.show();
    view.requestActivate();
    QElapsedTimer timer;
    timer.start();
    waitForFrame(view, timer);

    if (mode == QStringLiteral("try")) {
        QMetaObject::invokeMethod(root, "setText", Q_ARG(QVariant, text));
        QMetaObject::invokeMethod(root, "prepareTyping");
        return QGuiApplication::exec();
    }

    const qreal rssBefore{residentMegabytes()};
    timer.restart();
    QMetaObject::invokeMethod(root, "setText", Q_ARG(QVariant, text));
    const Frame opened{waitForFrame(view, timer)};
    const qreal rssDelta{residentMegabytes() - rssBefore};
    if (opened.frameMs < 0.0) {
        std::printf("%s %.1f timeout - - - - - -\n", qPrintable(mode), sizeMegabytes);
        return 0;
    }

    QMetaObject::invokeMethod(root, "prepareTyping");
    waitForFrame(view, timer);
    QList<qreal> keyUpdate;
    QList<qreal> keyFrame;
    for (qint32 i{0}; i < keystrokes; ++i) {
        QKeyEvent press{QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral("a")};
        QKeyEvent release{QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, QStringLiteral("a")};
        timer.restart();
        QCoreApplication::sendEvent(&view, &press);
        QCoreApplication::sendEvent(&view, &release);
        const Frame frame{waitForFrame(view, timer)};
        keyUpdate.append(frame.updateMs);
        keyFrame.append(frame.frameMs);
    }

    QList<qreal> scrollUpdate;
    for (qint32 i{0}; i < scrollSteps; ++i) {
        const qreal fraction{static_cast<qreal>((i * 37) % scrollSteps) / static_cast<qreal>(scrollSteps)};
        timer.restart();
        QMetaObject::invokeMethod(root, "scrollTo", Q_ARG(QVariant, fraction));
        scrollUpdate.append(waitForFrame(view, timer).updateMs);
    }

    std::printf(
        "%s %.1f %.1f %.2f %.2f %.2f %.2f %.2f %.0f\n",
        qPrintable(mode),
        sizeMegabytes,
        opened.frameMs,
        average(keyUpdate),
        *std::ranges::max_element(keyUpdate),
        *std::ranges::max_element(keyFrame),
        average(scrollUpdate),
        *std::ranges::max_element(scrollUpdate),
        rssDelta
    );
    return 0;
}
