#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationDomain(QStringLiteral("com.github.rask-project"));
    QCoreApplication::setOrganizationName(QStringLiteral("rask-project"));
    QCoreApplication::setApplicationName(QStringLiteral("Tekst"));
    QCoreApplication::setApplicationVersion(
        QStringLiteral(TEKST_VERSION) + QStringLiteral(" (") + QStringLiteral(TEKST_GIT_COMMIT) + QStringLiteral(")")
    );

    const QGuiApplication app{argc, argv};

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );
    engine.loadFromModule("Tekst", "Main");

    return QGuiApplication::exec();
}
