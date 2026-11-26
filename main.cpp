#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

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

    // kode [FILE]. parse(), not process(): unknown options (macOS adds some
    // when launching from the Finder) are ignored rather than fatal.
    QCommandLineParser parser;
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("The file to open."));
    parser.parse(QCoreApplication::arguments());
    const QStringList files{parser.positionalArguments()};
    const QUrl initialFile{files.isEmpty() ? QUrl{} : QUrl::fromLocalFile(QFileInfo{files.first()}.absoluteFilePath())};

    QQmlApplicationEngine engine;
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
