#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include "backend.h"
#include "theme.h"
#include "translator.h"
#include "analysis.h"
#include "telemetry.h"
#include <QSqlQuery>

class CellTests : public QObject {
    Q_OBJECT
    void put(const QString &path, const QByteArray &data) {
        QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(data), data.size());
    }
    QString batteryDir(QTemporaryDir &dir) {
        QDir(dir.path()).mkdir("BAT0");
        QString p = dir.path()+"/BAT0/";
        put(p+"type", "Battery"); put(p+"status", "Discharging"); return p;
    }
    QVariantMap sample(qint64 ts, double w=10, const QString &state="Discharging", const QString &device="BAT0") {
        return {{"ts",ts},{"watts",w},{"state",state},{"device",device},{"charge",80-ts/3600.0}};
    }
    QVariantList segment(qint64 start, int minutes, double watts, double cpu=20, double brightness=50, QString profile="power-saver") {
        QVariantList out;
        for(int i=0;i<=minutes*2;++i) {
            auto s=sample(start+i*30,watts);
            s.insert("cpu",cpu); s.insert("brightness",brightness); s.insert("profile",profile); s.insert("display","eDP-1");
            out.append(s);
        }
        return out;
    }
private slots:
    void matchedEarlierSessions() {
        auto rows=segment(1000,16,10);
        rows.append(segment(3000,6,15));
        auto a=Analysis::compare(rows);
        QCOMPARE(a["status"].toString(),QString("ready"));
        QCOMPARE(a["matches"].toInt(),3);
        QVERIFY(std::abs(a["baselineWatts"].toDouble()-10)<1e-8);
        QVERIFY(std::abs(a["deltaWatts"].toDouble()-5)<1e-8);
    }
    void excludesCurrentSessionAndDifferentConditions() {
        QCOMPARE(Analysis::compare(segment(1000,30,10))["status"].toString(),QString("no_matches"));
        for(auto changed : {QString("cpu"),QString("brightness"),QString("profile"),QString("display")}) {
            auto prior=segment(1000,16,10);
            auto current=segment(3000,6,15);
            for(auto &row:current) { auto s=row.toMap(); s[changed]=changed=="cpu" || changed=="brightness" ? QVariant(90) : QVariant("different"); row=s; }
            prior.append(current);
            QCOMPARE(Analysis::compare(prior)["status"].toString(),QString("no_matches"));
        }
    }
    void legacyMissingContextAndSleepAreNotCompared() {
        QVariantList old;
        for(int i=0;i<40;++i) old.append(sample(1000+i*30));
        QVERIFY(Analysis::windows(old).isEmpty());
        auto rows=segment(3000,6,10);
        auto s=rows[4].toMap(); s["cpu"]=QVariant(); rows[4]=s;
        QVERIFY(Analysis::windows(rows).isEmpty());
        rows=segment(3000,4,10); rows.append(segment(4000,4,10));
        QVERIFY(Analysis::windows(rows).isEmpty());
    }
    void newSessionMustWarmUp() {
        auto rows=segment(1000,16,10); rows.append(segment(3000,1,15));
        QCOMPARE(Analysis::compare(rows)["status"].toString(),QString("warming"));
    }
    void exactFiveMinuteIntegration() {
        auto rows=segment(1000,6,10);
        const auto ws=Analysis::windows(rows);
        QCOMPARE(ws.size(),1);
        auto w=ws.first().toMap();
        QCOMPARE(w["end"].toLongLong()-w["start"].toLongLong(),300LL);
        QVERIFY(std::abs(w["watts"].toDouble()-10)<1e-8);
    }
    void correlationsRequireEnoughVariedWindows() {
        auto flat=Analysis::compare(segment(1000,65,10));
        QVERIFY(!flat["cpuCorrelation"].toMap()["available"].toBool());
        auto rows=segment(1000,65,10);
        for(int i=0;i<rows.size();++i) { auto s=rows[i].toMap(); s["cpu"]=10+i*.4; s["watts"]=5+i*.1; rows[i]=s; }
        auto a=Analysis::compare(rows)["cpuCorrelation"].toMap();
        QVERIFY(a["available"].toBool()); QVERIFY(a["r"].toDouble()>.99);
        QVERIFY(!Analysis::compare(rows)["brightnessCorrelation"].toMap()["available"].toBool());
    }
    void cpuUtilizationAndGuestTicks() {
        auto a=Telemetry::parseCpu("cpu 100 0 0 900 0 0 0 0 20 0\n");
        auto b=Telemetry::parseCpu("cpu 150 0 0 950 0 0 0 0 30 0\n");
        QCOMPARE(a["total"].toULongLong(),1000ULL);
        QCOMPARE(Telemetry::utilization(a,b),50.0);
        QCOMPARE(Telemetry::utilization(b,a),-1.0);
        QCOMPARE(Telemetry::utilization({},b),-1.0);
    }
    void contextSensorsAndSleepReset() {
        QTemporaryDir dir; auto sys=dir.path()+"/sys", proc=dir.path()+"/proc";
        QDir().mkpath(sys+"/class/backlight/panel"); QDir().mkpath(sys+"/class/drm/eDP-1"); QDir().mkpath(proc);
        put(sys+"/class/backlight/panel/max_brightness","1000");
        put(sys+"/class/backlight/panel/brightness","500");
        put(sys+"/class/drm/eDP-1/status","connected");
        put(proc+"/stat","cpu 100 0 0 900 0 0 0 0\n");
        Telemetry telemetry(sys,proc); auto first=telemetry.sample(1000);
        QCOMPARE(first["brightness"].toDouble(),50.0); QCOMPARE(first["cpu"].toDouble(),-1.0);
        QCOMPARE(first["display"].toString(),QString("eDP-1"));
        put(proc+"/stat","cpu 150 0 0 950 0 0 0 0\n");
        QCOMPARE(telemetry.sample(1030)["cpu"].toDouble(),50.0);
        put(proc+"/stat","cpu 180 0 0 1000 0 0 0 0\n");
        QCOMPARE(telemetry.sample(1300)["cpu"].toDouble(),-1.0);
    }
    void processCpuAndPidReuse() {
        QTemporaryDir dir; const auto proc=dir.path()+"/proc";
        QDir().mkpath(proc+"/123");
        auto processStat=[](int ticks, int started) {
            QList<QByteArray> fields(20,"0"); fields[0]="S"; fields[11]=QByteArray::number(ticks); fields[19]=QByteArray::number(started);
            return QByteArray("123 (example) ")+fields.join(' ');
        };
        put(proc+"/123/comm","<img src=bad>");
        put(proc+"/123/stat",processStat(10,100)); put(proc+"/stat","cpu 100 0 0 900 0 0 0 0");
        Telemetry telemetry(dir.path()+"/sys",proc); telemetry.sample(1000);
        put(proc+"/123/stat",processStat(30,100)); put(proc+"/stat","cpu 150 0 0 950 0 0 0 0");
        const auto apps=telemetry.sample(1030)["apps"].toList(); QCOMPARE(apps.size(),1);
        QCOMPARE(apps[0].toMap()["cpu"].toDouble(),20.0);
        QCOMPARE(apps[0].toMap()["name"].toString(),QString("<img src=bad>"));
        put(proc+"/123/stat",processStat(80,200)); put(proc+"/stat","cpu 200 0 0 1000 0 0 0 0");
        QVERIFY(telemetry.sample(1060)["apps"].toList().isEmpty());
    }
    void reportIsReadOnlyAndBounded() {
        QTemporaryDir dir; const auto p=batteryDir(dir); put(p+"power_now","10000000"); put(p+"capacity","80");
        const auto db=dir.path()+"/history.sqlite";
        { Backend b(dir.path(),db); b.refresh(); }
        QVERIFY(!(QFile::permissions(db) & (QFile::ReadGroup|QFile::WriteGroup|QFile::ReadOther|QFile::WriteOther)));
        { Backend b(dir.path(),db,false,nullptr,true); auto r=b.report(); QVERIFY2(b.error().isEmpty(),qPrintable(b.error())); QCOMPARE(r["version"].toInt(),1); QCOMPARE(r["points"].toList().size(),1); QVERIFY(r["analysis"].toMap()["status"]!="ready"); }
        { Backend b(dir.path(),db); b.refresh(); QCOMPARE(b.points().size(),1); }
        Backend missing(dir.path(),dir.path()+"/absent.sqlite",false,nullptr,true); QVERIFY(!missing.error().isEmpty()); QVERIFY(!QFile::exists(dir.path()+"/absent.sqlite"));
    }
    void initTestCase() { QQuickStyle::setStyle("Material"); }
    void signedPowerAndUnits() {
        QTemporaryDir dir; const auto p = batteryDir(dir);
        put(p+"power_now", "-8500000"); put(p+"energy_now", "28000000");
        put(p+"energy_full", "45000000"); put(p+"energy_full_design", "50000000");
        put(p+"temp", "302"); put(p+"cycle_count", "324");
        const auto b=Backend::readBattery(dir.path());
        QCOMPARE(b["watts"].toDouble(),8.5); QCOMPARE(b["health"].toDouble(),90.0);
        QCOMPARE(b["temperature"].toDouble(),30.2); QCOMPARE(b["hours"].toDouble(),28.0/8.5);
    }
    void chargeOnlyFallback() {
        QTemporaryDir dir; const auto p=batteryDir(dir);
        put(p+"charge_now","2000000"); put(p+"charge_full","4000000"); put(p+"charge_full_design","5000000");
        put(p+"voltage_min_design","10000000"); put(p+"voltage_now","11000000"); put(p+"current_now","-500000");
        const auto b=Backend::readBattery(dir.path());
        QCOMPARE(b["energy"].toDouble(),20.0); QCOMPARE(b["full"].toDouble(),40.0);
        QCOMPARE(b["charge"].toDouble(),50.0); QCOMPARE(b["watts"].toDouble(),5.5);
    }
    void unavailableIsNotZero() {
        QTemporaryDir dir; batteryDir(dir);
        const auto b=Backend::readBattery(dir.path());
        for(auto key : {"watts","full","health","temperature","cycles","hours"}) QCOMPARE(b[key].toDouble(),-1.0);
    }
    void noBatteryOrPeripheral() {
        QTemporaryDir dir; QVERIFY(Backend::readBattery(dir.path()).isEmpty());
        const auto p=batteryDir(dir); put(p+"scope","Device"); QVERIFY(Backend::readBattery(dir.path()).isEmpty());
    }
    void integratePower() {
        auto result=Backend::summarize({sample(100,10),sample(160,20)});
        QCOMPARE(result.size(),1); auto s=result.first().toMap();
        QCOMPARE(s["watts"].toDouble(),15.0); QCOMPARE(s["energy"].toDouble(),.25);
    }
    void splitSleepChargingDeviceAndMissing() {
        QCOMPARE(Backend::summarize({sample(100),sample(160),sample(600),sample(660)}).size(),2);
        QCOMPARE(Backend::summarize({sample(100),sample(130),sample(160,10,"Charging"),sample(190),sample(220)}).size(),2);
        QCOMPARE(Backend::summarize({sample(100),sample(130),sample(160,10,"Discharging","BAT1"),sample(190,10,"Discharging","BAT1")}).size(),2);
        QCOMPARE(Backend::summarize({sample(100),sample(130,-1),sample(160)}).size(),0);
        QCOMPARE(Backend::summarize({sample(100)}).size(),0);
    }
    void persistsAndDeduplicates() {
        QTemporaryDir dir; const auto p=batteryDir(dir); put(p+"power_now","10000000");
        const auto db=dir.path()+"/history.sqlite";
        { Backend b(dir.path(),db); b.refresh(); b.refresh(); QVERIFY2(b.error().isEmpty(),qPrintable(b.error())); QCOMPARE(b.points().size(),1); }
        { Backend b(dir.path(),db); b.refresh(); QCOMPARE(b.points().size(),1); }
    }
    void reportsDatabaseFailure() {
        QTemporaryDir dir; batteryDir(dir);
        Backend b(dir.path(),dir.path()+"/missing/history.sqlite"); b.refresh(); QVERIFY(!b.error().isEmpty());
    }
    void themeAndTranslation() {
        QTemporaryDir dir; QDir(dir.path()).mkdir("theme");
        Theme theme(dir.path()); QCOMPARE(theme.accent(),QString("#FFD60A"));
        put(dir.path()+"/theme/colors.toml","accent = \"#112233\"\nbackground = \"#fafafa\"\nforeground = \"#121212\"\ndark_background = \"#eeeeee\"\n");
        QTRY_COMPARE(theme.accent(),QString("#112233"));
        QCOMPARE(theme.colors().value("background").toString(),QString("#fafafa"));
        QCOMPARE(theme.colors().value("foreground").toString(),QString("#121212"));
        QCOMPARE(theme.colors().value("surface").toString(),QString("#eeeeee"));
        QVERIFY(theme.colors().value("light").toBool());
        put(dir.path()+"/language","da\n");
        Translator translator(dir.path()+"/language");
        QCOMPARE(translator.translate("Main","Charge"),QString("Opladning"));
        put(dir.path()+"/language","en\n"); QTRY_COMPARE(translator.language(),QString("en"));
    }
    void realQmlInteraction() {
        QTemporaryDir dir;
        Theme theme(dir.path()); Translator translator(dir.path()+"/language");
        Backend backend("", "", true); backend.start();
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("theme",&theme);
        engine.rootContext()->setContextProperty("translator",&translator);
        engine.rootContext()->setContextProperty("backend",&backend);
        engine.rootContext()->setContextProperty("demoMode",true);
        QStringList warnings;
        connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError> &errors){ for(const auto &e:errors) warnings << e.toString(); });
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY2(!engine.rootObjects().isEmpty(),qPrintable(warnings.join('\n')));
        auto window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        QTest::qWait(200);
        QTest::keyClick(window,Qt::Key_Question);
        QTRY_VERIFY(window->property("helpVisible").toBool());
        QTest::keyClick(window,Qt::Key_Escape);
        QTRY_VERIFY(!window->property("helpVisible").toBool());
        QTest::keyClick(window,Qt::Key_Space);
        window->setMinimumSize(QSize(0,0)); window->resize(620,600);
        QTest::qWait(100);
        auto selector=window->findChild<QObject*>("graphSelector"); QVERIFY(selector);
        selector->setProperty("currentIndex",1);
        QCOMPARE(window->property("graphMode").toInt(),1);
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save("compact.png"));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
    }
};
QTEST_MAIN(CellTests)
#include "cell_tests.moc"
