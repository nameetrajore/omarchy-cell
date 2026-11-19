#include <QGuiApplication>
#include <QCoreApplication>
#include <memory>
#include <QIcon>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QTemporaryFile>
#include <cstdio>
#include "backend.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char **argv) {
    const QString command = argc > 1 ? QString::fromLocal8Bit(argv[1]) : "open";
    const bool cli = command == "status" || command == "report" || command == "collect" || command == "skill" || command == "--help" || command == "-h";
    std::unique_ptr<QCoreApplication> application;
    if (cli) application=std::make_unique<QCoreApplication>(argc,argv);
    else application=std::make_unique<QGuiApplication>(argc,argv);
    auto &app=*application;
    app.setApplicationName("cell");
    app.setOrganizationName("cell");
    app.setApplicationVersion("0.2.0");
    if (!cli) {
        QGuiApplication::setDesktopFileName("cell");
        QGuiApplication::setWindowIcon(QIcon::fromTheme("cell"));
    }
    const QStringList args = app.arguments();
    if (command == "--help" || command == "-h") {
        puts("Cell — local battery monitoring\n  cell open [--demo] [--language da]\n  cell status --json\n  cell collect --json\n  cell report --json\n  cell skill install --json\n\nRecords every 30 seconds while open or collecting. Background startup is managed separately by cell.service."); return 0;
    }
    if (command == "status") {
        auto data = Backend::readBattery("/sys/class/power_supply");
        if (data.isEmpty()) { fputs("No readable laptop battery found.\n", stderr); return 1; }
        puts(QJsonDocument::fromVariant(data).toJson(QJsonDocument::Compact).constData()); return 0;
    }
    if (command == "skill") {
        if (args.value(2) != "install") { fputs("Usage: cell skill install --json\n", stderr); return 1; }
        QString error;
        if (!Backend::installSkill(&error)) { fprintf(stderr, "%s\n", qPrintable(error)); return 1; }
        puts("{\"installed\":true}"); return 0;
    }
    if (command != "open" && command != "collect" && command != "report") { fputs("Unknown command. Use cell --help.\n", stderr); return 1; }
    Backend backend("/sys/class/power_supply", {}, args.contains("--demo"), nullptr, command=="report");
    if (command=="report") {
        const auto report=backend.report();
        if (!backend.error().isEmpty()) { fprintf(stderr,"%s\n",qPrintable(backend.error())); return 1; }
        puts(QJsonDocument::fromVariant(report).toJson(QJsonDocument::Compact).constData()); return 0;
    }
    if (command == "collect") {
        QObject::connect(&backend, &Backend::changed, &app, [&] {
            if (!backend.error().isEmpty()) { fprintf(stderr, "%s\n", qPrintable(backend.error())); app.exit(1); }
            else { puts(QJsonDocument::fromVariant(backend.battery()).toJson(QJsonDocument::Compact).constData()); fflush(stdout); }
        });
        QTimer::singleShot(0, &backend, [&] { backend.start(false); });
        return app.exec();
    }
    QQuickStyle::setStyle("Material");
    // Previewing another language must not change the desktop's language setting.
    QTemporaryFile languageFile;
    const int languageIndex = args.indexOf("--language");
    if (languageIndex >= 0) {
        if (args.value(languageIndex+1).isEmpty() || !languageFile.open()) return 1;
        languageFile.write(args.value(languageIndex+1).toUtf8()); languageFile.flush();
    }
    Translator translator(languageIndex >= 0 ? languageFile.fileName() : QString());
    app.installTranslator(&translator);
    Theme theme(args.contains("--screenshot") ? "/nonexistent-cell-preview" : QString());
    backend.start();
    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("demoMode", args.contains("--demo"));
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    // Deterministic screenshot mode uses synthetic data and never opens the history database.
    const int shot = args.indexOf("--screenshot");
    if (shot >= 0) {
        if (!args.contains("--demo") || args.value(shot+1).isEmpty()) return 1;
        QTimer::singleShot(1200, &app, [&] {
            auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            app.exit(window && window->grabWindow().save(args.value(shot+1)) ? 0 : 1);
        });
    }
    return app.exec();
}
