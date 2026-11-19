#include "backend.h"
#include "analysis.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>

namespace {
QString text(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}
double number(const QString &path, double scale = 1) {
    bool ok = false;
    const double n = text(path).toDouble(&ok);
    return ok && std::isfinite(n) ? n / scale : -1;
}
}

QVariantMap Backend::readBattery(const QString &root) {
    QStringList devices;
    for (const auto &name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (text(root + "/" + name + "/type") == "Battery" && text(root + "/" + name + "/scope") != "Device" && text(root + "/" + name + "/present") != "0")
            devices << name;
    if (devices.isEmpty()) return {};
    const QString id = devices.first(), p = root + "/" + id + "/";
    double energy = number(p + "energy_now", 1e6), full = number(p + "energy_full", 1e6), design = number(p + "energy_full_design", 1e6);
    // Charge-only batteries need nominal voltage, not fluctuating terminal voltage.
    double nominal = number(p + "voltage_min_design", 1e6);
    if (nominal <= 0) nominal = number(p + "voltage_max_design", 1e6);
    if (nominal > 0) {
        if (energy < 0) { double c = number(p + "charge_now", 1e6); if (c >= 0) energy = c * nominal; }
        if (full < 0) { double c = number(p + "charge_full", 1e6); if (c >= 0) full = c * nominal; }
        if (design < 0) { double c = number(p + "charge_full_design", 1e6); if (c > 0) design = c * nominal; }
    }
    // Apple SMC reports negative discharge power; -1 cannot be a missing sentinel before scaling.
    bool ok = false;
    double watts = text(p + "power_now").toDouble(&ok) / 1e6;
    if (ok) watts = std::abs(watts);
    else {
        double current = text(p + "current_now").toDouble(&ok) / 1e6;
        double voltage = number(p + "voltage_now", 1e6);
        watts = ok && voltage > 0 ? std::abs(current) * voltage : -1;
    }
    const QString state = text(p + "status");
    double charge = number(p + "capacity");
    if (charge < 0 && energy >= 0 && full > 0) charge = energy / full * 100;
    return {{"device", id}, {"model", text(p + "model_name")}, {"state", state}, {"charge", charge},
            {"watts", watts}, {"energy", energy}, {"full", full}, {"design", design},
            {"health", full > 0 && design > 0 ? full / design * 100 : -1},
            {"cycles", number(p + "cycle_count")}, {"temperature", number(p + "temp", 10)},
            {"voltage", number(p + "voltage_now", 1e6)}, {"devices", devices.size()},
            {"hours", state == "Discharging" && watts > .1 && energy >= 0 ? energy / watts : -1}};
}

QVariantList Backend::summarize(const QVariantList &samples) {
    QVariantList result;
    QVariantMap first, previous;
    double wh = 0, seconds = 0;
    auto finish = [&] {
        if (seconds > 0) result.prepend(QVariantMap{{"start", first.value("ts")}, {"end", previous.value("ts")},
            {"hours", seconds / 3600}, {"energy", wh}, {"watts", wh * 3600 / seconds},
            {"drop", first.value("charge").toDouble() >= 0 && previous.value("charge").toDouble() >= 0 ? first.value("charge").toDouble() - previous.value("charge").toDouble() : -1}});
        first.clear(); previous.clear(); wh = 0; seconds = 0;
    };
    for (const auto &v : samples) {
        auto s = v.toMap();
        if (s.value("state") != "Discharging" || s.value("watts").toDouble() < 0) { finish(); continue; }
        double dt = s.value("ts").toLongLong() - previous.value("ts").toLongLong();
        if (!previous.isEmpty() && (dt <= 0 || dt > 90 || s.value("device") != previous.value("device"))) finish();
        if (first.isEmpty()) first = s;
        else {
            seconds += dt;
            wh += (s.value("watts").toDouble() + previous.value("watts").toDouble()) / 2 * dt / 3600;
        }
        previous = s;
    }
    finish();
    return result;
}

Backend::Backend(const QString &root, const QString &database, bool demo, QObject *parent, bool readOnly)
    : QObject(parent), m_root(root), m_demo(demo),
      m_telemetry(root == "/sys/class/power_supply" ? "/sys" : root, root == "/sys/class/power_supply" ? "/proc" : "/nonexistent") {
    if (demo) return;
    QString path = database;
    if (path.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (!readOnly) {
            if (!QDir().mkpath(dir) || !QFile::setPermissions(dir,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner)) {
                m_error=tr("Cannot secure the local history directory."); return;
            }
        }
        path = dir + "/history.sqlite";
    }
    m_connection = QUuid::createUuid().toString();
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(path);
    if (readOnly && !QFile::exists(path)) { m_error = tr("No recorded history yet. Start the Cell collector."); return; }
    m_db.setConnectOptions(readOnly ? "QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=2000" : "QSQLITE_BUSY_TIMEOUT=2000");
    if (!m_db.open()) { m_error = m_db.lastError().text(); return; }
    if (readOnly) return;
    if (path != ":memory:" && !QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner)) {
        m_error=tr("Cannot secure the local history file."); m_db.close(); return;
    }
    QSqlQuery q(m_db);
    if (!q.exec("CREATE TABLE IF NOT EXISTS contexts (ts INTEGER NOT NULL, device TEXT NOT NULL, cpu REAL, brightness REAL, profile TEXT, display TEXT, apps TEXT, PRIMARY KEY(ts,device))")) {
        m_error = q.lastError().text(); return;
    }
    if (!q.exec("PRAGMA journal_mode=WAL") || !q.exec("CREATE TABLE IF NOT EXISTS samples (ts INTEGER NOT NULL, device TEXT NOT NULL, state TEXT, charge REAL, watts REAL, full REAL, health REAL, PRIMARY KEY(ts,device))") || !q.exec("CREATE INDEX IF NOT EXISTS samples_device_time ON samples(device,ts)"))
        m_error = q.lastError().text();
}
Backend::~Backend() {
    if (!m_connection.isEmpty()) {
        m_db.close(); m_db = QSqlDatabase(); QSqlDatabase::removeDatabase(m_connection);
    }
}
void Backend::start(bool history) {
    m_history = history;
    refresh();
    m_timer.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, &Backend::refresh);
    m_timer.start();
}
void Backend::refresh() {
    if (m_demo) {
        m_battery = {{"device", "demo"}, {"model", tr("Demonstration battery")}, {"state", "Discharging"}, {"charge", 67}, {"watts", 8.3}, {"health", 91.2}, {"full", 47.4}, {"design", 52}, {"energy", 31.8}, {"cycles", 182}, {"temperature", 29.4}, {"voltage", 11.8}, {"hours", 3.8}, {"devices", 1}, {"averageWatts", 8.1}, {"sampledHours", 1.0}};
        m_points.clear();
        qint64 now = QDateTime::currentSecsSinceEpoch();
        for (int i = 0; i < 120; ++i) m_points.append(QVariantMap{{"ts", now - (119-i)*30}, {"device", "demo"}, {"state", "Discharging"}, {"charge", 80-i*.11}, {"watts", 8+2*std::sin(i*.18)+std::sin(i*.7)}, {"full", 47.4}, {"health", 91.2}});
        m_sessions = summarize(m_points);
        m_battery.insert("averageWatts", m_sessions.first().toMap().value("watts"));
        emit changed(); return;
    }
    m_battery = readBattery(m_root);
    if (m_battery.isEmpty()) { m_error = tr("No readable laptop battery found."); emit changed(); return; }
    if (!m_db.isOpen()) { emit changed(); return; }
    const qint64 now=QDateTime::currentSecsSinceEpoch(), bucket=now/30*30;
    if (!m_db.transaction()) { m_error=m_db.lastError().text(); emit changed(); return; }
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO samples VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(bucket);
    for (const auto &key : {"device", "state", "charge", "watts", "full", "health"}) q.addBindValue(m_battery.value(key));
    if (!q.exec()) { m_error = q.lastError().text(); m_db.rollback(); emit changed(); return; }
    const auto context = m_telemetry.sample(now);
    q.prepare("INSERT INTO contexts VALUES (?,?,?,?,?,?,?) ON CONFLICT(ts,device) DO UPDATE SET cpu=excluded.cpu, brightness=excluded.brightness, profile=excluded.profile, display=excluded.display, apps=excluded.apps WHERE contexts.cpu<0 AND excluded.cpu>=0");
    q.addBindValue(bucket);
    q.addBindValue(m_battery.value("device"));
    for (const auto &key : {"cpu", "brightness", "profile", "display"}) q.addBindValue(context.value(key));
    q.addBindValue(QString::fromUtf8(QJsonDocument::fromVariant(context.value("apps")).toJson(QJsonDocument::Compact)));
    if (!q.exec()) { m_error=q.lastError().text(); m_db.rollback(); emit changed(); return; }
    if (!m_db.commit()) { m_error=m_db.lastError().text(); m_db.rollback(); emit changed(); return; }
    if (m_history) loadHistory();
    else m_error.clear();
    emit changed();
}
bool Backend::loadHistory() {
    QSqlQuery q(m_db);
    // Older records remain intact; legacy rows without context are never guessed.
    q.prepare("SELECT s.ts,s.device,s.state,s.charge,s.watts,s.full,s.health,c.cpu,c.brightness,c.profile,c.display,c.apps FROM samples s LEFT JOIN contexts c ON s.ts=c.ts AND s.device=c.device WHERE s.device=? AND s.ts>=? ORDER BY s.ts");
    q.addBindValue(m_battery.value("device"));
    q.addBindValue(QDateTime::currentSecsSinceEpoch()-86400*7);
    if (!q.exec()) { m_error = q.lastError().text(); return false; }
    m_points.clear();
    while (q.next()) {
        QVariantMap s; int i=0;
        for (const auto &key : {"ts", "device", "state", "charge", "watts", "full", "health", "cpu", "brightness", "profile", "display"}) s.insert(key, q.value(i++));
        s.insert("apps", QJsonDocument::fromJson(q.value(i).toString().toUtf8()).array().toVariantList());
        m_points.append(s);
    }
    m_sessions = summarize(m_points);
    double totalWh = 0, totalHours = 0;
    for (const auto &session : m_sessions) {
        totalWh += session.toMap().value("energy").toDouble();
        totalHours += session.toMap().value("hours").toDouble();
    }
    m_battery.insert("averageWatts", totalHours >= 1.0/6 ? totalWh / totalHours : -1);
    m_battery.insert("sampledHours", totalHours);
    m_error.clear(); return true;
}
QVariantMap Backend::report() {
    if (m_demo) refresh();
    else {
        m_battery=readBattery(m_root);
        if (m_battery.isEmpty()) { m_error=tr("No readable laptop battery found."); return {}; }
        if (!m_db.isOpen() || !loadHistory()) return {};
    }
    QVariantList graph;
    const int begin=std::max(0,int(m_points.size())-120);
    for (int i=begin;i<m_points.size();++i) {
        auto s=m_points[i].toMap();
        graph.append(QVariantMap{{"ts",s.value("ts")},{"watts",s.value("watts")},{"charge",s.value("charge")}});
    }
    QVariantMap context;
    if (!m_points.isEmpty()) {
        auto s=m_points.last().toMap();
        for(const auto &key:{"ts","cpu","brightness","profile","display","apps"}) context.insert(key,s.value(key));
    }
    const bool stale=context.isEmpty() || QDateTime::currentSecsSinceEpoch()-context.value("ts").toLongLong()>90;
    auto analysis=Analysis::compare(m_points);
    if(stale) analysis["status"]="stale";
    QVariantList sessions;
    for(int i=0;i<std::min(5,int(m_sessions.size()));++i) sessions.append(m_sessions[i]);
    return {{"version",1},{"generatedAt",QDateTime::currentSecsSinceEpoch()},{"battery",m_battery},{"context",context},
        {"analysis",analysis},{"points",graph},{"sessions",sessions},{"stale",stale}};
}
bool Backend::installSkill(QString *error) {
    const QString path = QDir::homePath()+"/.pi/agent/skills/cell/SKILL.md";
    if (QFile::exists(path)) { *error = tr("Skill already exists; refusing to overwrite it."); return false; }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile source(":/SKILL.md"); QSaveFile target(path);
    if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) { *error = tr("Cannot write Cell skill."); return false; }
    const QByteArray data = source.readAll();
    if (target.write(data) != data.size() || !target.commit()) { *error = target.errorString(); return false; }
    return true;
}
