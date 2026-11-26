#include "FileIcons.h"

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

#include <memory>

int main(int argc, char *argv[])
{
    // In domain order: QSettings reverses it (com.github.rask-project.Kode).
    QCoreApplication::setOrganizationDomain(QStringLiteral("rask-project.github.com"));
    QCoreApplication::setOrganizationName(QStringLiteral("rask-project"));
    QCoreApplication::setApplicationName(QStringLiteral("Kode"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral(KODE_VERSION) + QStringLiteral(" (") + QStringLiteral(KODE_GIT_COMMIT) + QStringLiteral(")")
    );

    const QGuiApplication app{argc, argv};

    // kode [FILE | FOLDER]. parse(), not process(): unknown options (macOS adds some
    // when launching from the Finder) are ignored rather than fatal.
    QCommandLineParser parser;
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("The file or folder to open."));
    parser.parse(QCoreApplication::arguments());
    const QStringList files{parser.positionalArguments()};
    const QUrl initialFile{files.isEmpty() ? QUrl{} : QUrl::fromLocalFile(QFileInfo{files.first()}.absoluteFilePath())};

    QQmlApplicationEngine engine;
    // The engine owns its image providers.
    engine.addImageProvider(QStringLiteral("fileicon"), std::make_unique<kode::FileIcons>().release());
    engine.addImageProvider(QStringLiteral("codeicon"), std::make_unique<kode::CodeIcons>().release());
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );
    engine.setInitialProperties({
        {QStringLiteral("initialFile"), initialFile},
    });
    engine.loadFromModule("Kode", "Main");

    return QGuiApplication::exec();
}
