#include "calendarstore.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Dayline");
    app.setApplicationName("Dayline");
    app.setApplicationVersion("0.1.0");
    app.setDesktopFileName("dayline");
    app.setFont(QFont("Noto Sans CJK SC", 10));
    QQuickStyle::setStyle("Basic");

    QCommandLineParser parser;
    parser.setApplicationDescription("Dayline 日笺 — 把日子，过得从容一点。");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"demo", "Open an isolated preview space with sample events."});
    parser.addOption({"screenshot", "Save the rendered window to a PNG and exit.", "file"});
    parser.addOption({"show-editor", "Open the event editor on launch."});
    parser.addOption({"theme", "Choose light or dark appearance.", "theme"});
    parser.process(app);

    if (parser.isSet("theme") && parser.value("theme") != "light" && parser.value("theme") != "dark") {
        qCritical() << "Theme must be light or dark.";
        return 2;
    }

    CalendarStore store(parser.isSet("demo"));
    if (!store.initialize()) {
        qCritical().noquote() << store.error();
        return 1;
    }
    if (parser.isSet("theme")) store.setDarkMode(parser.value("theme") == "dark");

    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/qt/qml");
    engine.rootContext()->setContextProperty("calendarStore", &store);
    engine.load(QUrl("qrc:/qt/qml/Dayline/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;

    if (parser.isSet("show-editor"))
        QTimer::singleShot(100, &engine, [&engine] {
            QMetaObject::invokeMethod(engine.rootObjects().first(), "openNewEvent");
        });

    if (parser.isSet("screenshot")) {
        const QString path = parser.value("screenshot");
        QTimer::singleShot(1200, &app, [&app, &engine, path] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (!window || !window->grabWindow().save(path)) {
                qCritical() << "Could not save screenshot:" << path;
                app.exit(1);
                return;
            }
            qInfo().noquote() << "Saved preview:" << path;
            app.quit();
        });
    }
    return app.exec();
}
