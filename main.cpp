#include <QCommandLineParser>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationDomain(QStringLiteral("com.github.rask-project"));
    QCoreApplication::setOrganizationName(QStringLiteral("rask-project"));
    QCoreApplication::setApplicationName(QStringLiteral("Tekst"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral(TEKST_VERSION) + QStringLiteral(" (") + QStringLiteral(TEKST_GIT_COMMIT) + QStringLiteral(")")
    );

    const QGuiApplication app{argc, argv};

    // tekst [FILE]. parse(), not process(): unknown options (macOS adds some
    // when launching from the Finder) are ignored rather than fatal.
    QCommandLineParser parser;
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("The file to open."));
    parser.parse(QCoreApplication::arguments());
    const QStringList files{parser.positionalArguments()};
    const QUrl initialFile{files.isEmpty() ? QUrl{} : QUrl::fromLocalFile(QFileInfo{files.first()}.absoluteFilePath())};

    QFont editorFont{QFontDatabase::systemFont(QFontDatabase::FixedFont)};
    editorFont.setPointSize(13);

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
        {QStringLiteral("editorFont"), editorFont},
    });
    engine.loadFromModule("Tekst", "Main");

    return QGuiApplication::exec();
}
