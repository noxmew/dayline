#include "calendarstore.h"
#include "desktopcontroller.h"

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
    // The desktop integration uses EWMH through X11, including XWayland sessions.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && !qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "xcb");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Dayline");
    app.setApplicationName("Dayline");
    app.setApplicationVersion(DAYLINE_VERSION);
    app.setDesktopFileName("dayline");
    app.setFont(QFont("Noto Sans CJK SC", 10));
    QQuickStyle::setStyle("Basic");

    QCommandLineParser parser;
    parser.setApplicationDescription("Dayline 日笺 — 把日子，过得从容一点。");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"demo", "Open an isolated preview space with sample events."});
    parser.addOption({"desktop", "Start the calendar on the desktop using X11 desktop window protocols."});
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
    DesktopController desktop(parser.isSet("demo"), parser.isSet("desktop"));
    if (parser.isSet("desktop") && !desktop.supported()) {
        qCritical().noquote() << desktop.supportMessage();
        return 2;
    }

    QQmlApplicationEngine engine;
    engine.addImportPath("qrc:/qt/qml");
    engine.rootContext()->setContextProperty("calendarStore", &store);
    engine.rootContext()->setContextProperty("desktopController", &desktop);
    engine.load(QUrl("qrc:/qt/qml/Dayline/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    auto *manager = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    engine.load(QUrl("qrc:/qt/qml/Dayline/qml/DesktopCalendar.qml"));
    if (engine.rootObjects().size() != 2) return 1;
    desktop.attach(manager, qobject_cast<QQuickWindow *>(engine.rootObjects().last()));

    if (parser.isSet("show-editor"))
        QTimer::singleShot(100, &engine, [&engine, &desktop] {
            QMetaObject::invokeMethod(desktop.enabled() ? engine.rootObjects().last() : engine.rootObjects().first(), "openNewEvent");
        });

    if (parser.isSet("screenshot")) {
        const QString path = parser.value("screenshot");
        const bool showEditor = parser.isSet("show-editor");
        QTimer::singleShot(1200, &app, [&app, &engine, &desktop, path, showEditor] {
            auto *window = qobject_cast<QQuickWindow *>(desktop.enabled() ? engine.rootObjects().last() : engine.rootObjects().first());
            if (showEditor) {
                for (auto *candidate : app.allWindows()) {
                    if (candidate->objectName() == "eventEditor" && candidate->isVisible()) {
                        window = qobject_cast<QQuickWindow *>(candidate);
                        break;
                    }
                }
            }
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
