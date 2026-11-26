#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Aurora");

    QCommandLineParser parser;
    parser.addOption({"station", "The station to show.", "name", "oslo"});
    parser.process(app);

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"station", parser.value("station")}});
    engine.loadFromModule("Aurora", "Main");
    return app.exec();
}
