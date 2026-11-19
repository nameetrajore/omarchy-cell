#include "telemetry.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <unistd.h>

namespace {
QByteArray read(const QString &path) {
    QFile f(path); return f.open(QIODevice::ReadOnly) ? f.read(65536).trimmed() : QByteArray();
}
double numeric(const QString &path) {
    bool ok; const double n = read(path).toDouble(&ok); return ok ? n : -1;
}
}
Telemetry::Telemetry(const QString &sys, const QString &proc) : m_sys(sys), m_proc(proc) {}
QVariantMap Telemetry::parseCpu(const QByteArray &stat) {
    const auto fields = stat.split('\n').value(0).simplified().split(' ');
    if (fields.size() < 9 || fields[0] != "cpu") return {};
    quint64 total = 0, idle = 0;
    // Guest ticks are already included in user/nice; don't count them twice.
    for (int i=1; i<=8; ++i) {
        bool ok; const quint64 n=fields[i].toULongLong(&ok); if (!ok) return {};
        total += n; if (i==4 || i==5) idle += n;
    }
    return {{"total", total}, {"idle", idle}};
}
double Telemetry::utilization(const QVariantMap &before, const QVariantMap &after) {
    if (before.isEmpty() || after.isEmpty()) return -1;
    const auto a=before.value("total").toULongLong(), b=after.value("total").toULongLong();
    const auto ai=before.value("idle").toULongLong(), bi=after.value("idle").toULongLong();
    if (b<=a || bi<ai || bi-ai>b-a) return -1;
    return 100.0 * (1-double(bi-ai)/double(b-a));
}
QVariantMap Telemetry::sample(qint64 now) {
    QVariantMap context{{"cpu", -1}, {"brightness", -1}, {"profile", ""}, {"display", ""}, {"apps", QVariantList()}};
    const auto cpu=parseCpu(read(m_proc+"/stat"));
    const bool contiguous=m_time>0 && now>m_time && now-m_time<=90;
    const double usage=contiguous ? utilization(m_cpu,cpu) : -1;
    context["cpu"]=usage;
    // No command lines, titles, domains or executable paths are collected.
    QMap<QString, Process> processes;
    QMap<QString,double> groups;
    const quint64 total=cpu.value("total").toULongLong()-m_cpu.value("total").toULongLong();
    for (const auto &pid : QDir(m_proc).entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
        bool ok; pid.toULongLong(&ok);
        if (!ok || QFileInfo(m_proc+"/"+pid).ownerId()!=getuid()) continue;
        const auto stat=read(m_proc+"/"+pid+"/stat");
        const int end=stat.lastIndexOf(')');
        if (end<0) continue;
        const auto fields=stat.mid(end+2).simplified().split(' ');
        if (fields.size()<20) continue;
        Process p{QString::fromUtf8(read(m_proc+"/"+pid+"/comm")).left(64), fields[11].toULongLong()+fields[12].toULongLong(), fields[19].toULongLong()};
        processes.insert(pid,p);
        const auto previous=m_processes.constFind(pid);
        if (usage>=0 && total>0 && previous!=m_processes.cend() && previous->started==p.started && p.ticks>=previous->ticks)
            groups[p.name]+=100.0*double(p.ticks-previous->ticks)/double(total);
    }
    QVariantList apps;
    QList<QPair<QString,double>> sorted;
    for (auto i=groups.cbegin();i!=groups.cend();++i) if(i.value()>.05) sorted.append({i.key(),i.value()});
    std::sort(sorted.begin(),sorted.end(),[](const auto &a,const auto &b){return a.second>b.second;});
    for (int i=0;i<std::min(5,int(sorted.size()));++i) apps.append(QVariantMap{{"name",sorted[i].first},{"cpu",sorted[i].second}});
    context["apps"]=apps;
    m_processes=processes; m_cpu=cpu; m_time=now;

    QStringList displays;
    for (const auto &name : QDir(m_sys+"/class/drm").entryList(QDir::Dirs|QDir::NoDotAndDotDot))
        if (read(m_sys+"/class/drm/"+name+"/status")=="connected") displays << name;
    if (!displays.isEmpty()) context["display"]=displays.join(",");
    const auto backlights=QDir(m_sys+"/class/backlight").entryList(QDir::Dirs|QDir::NoDotAndDotDot);
    // One built-in backlight is measurable; multiple backlights are ambiguous.
    if (backlights.size()==1) {
        const QString base=m_sys+"/class/backlight/"+backlights.first()+"/";
        const double maximum=numeric(base+"max_brightness");
        double value=numeric(base+"actual_brightness");
        if(value<0) value=numeric(base+"brightness");
        if(maximum>0 && value>=0 && value<=maximum) context["brightness"]=100*value/maximum;
    }
    if (m_sys=="/sys") {
        auto request=QDBusMessage::createMethodCall("net.hadess.PowerProfiles", "/net/hadess/PowerProfiles", "org.freedesktop.DBus.Properties", "Get");
        request.setArguments({"net.hadess.PowerProfiles", "ActiveProfile"});
        const auto reply=QDBusConnection::systemBus().call(request,QDBus::Block,200);
        if(reply.type()==QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
            context["profile"]=qvariant_cast<QDBusVariant>(reply.arguments().first()).variant().toString().left(64);
    }
    return context;
}
