#include "analysis.h"
#include <algorithm>
#include <cmath>

namespace {
bool valid(const QVariantMap &s) {
    for (const auto &key : {"watts", "cpu", "brightness"}) {
        if (s.value(key).isNull()) return false;
        bool ok; double n=s.value(key).toDouble(&ok);
        if(!ok || !std::isfinite(n) || n<0 || (QString(key)!="watts" && n>100)) return false;
    }
    return s.value("state")=="Discharging" && !s.value("profile").toString().isEmpty() && !s.value("display").toString().isEmpty();
}
double median(QList<double> values) {
    std::sort(values.begin(),values.end()); int n=values.size();
    return n%2 ? values[n/2] : (values[n/2-1]+values[n/2])/2;
}
QVariantMap correlation(const QVariantList &windows, const QString &key) {
    if(windows.size()<12) return {{"available",false},{"count",windows.size()}};
    double x=0,y=0,xx=0,yy=0,xy=0,lo=100,hi=0;
    for(const auto &v:windows) {
        const auto w=v.toMap(); double a=w.value(key).toDouble(), b=w.value("watts").toDouble();
        x+=a; y+=b; xx+=a*a; yy+=b*b; xy+=a*b; lo=std::min(lo,a); hi=std::max(hi,a);
    }
    const double n=windows.size(), vx=n*xx-x*x, vy=n*yy-y*y;
    if(hi-lo<10 || vx<=1e-8 || vy<=1e-8) return {{"available",false},{"count",windows.size()}};
    return {{"available",true},{"count",windows.size()},{"r",std::clamp((n*xy-x*y)/std::sqrt(vx*vy),-1.0,1.0)}};
}
}
QVariantList Analysis::windows(const QVariantList &samples) {
    QVariantList result;
    QVariantMap previous;
    int session=0; double seconds=0,energy=0,cpu=0,brightness=0; qint64 start=0;
    auto reset=[&]{ seconds=energy=cpu=brightness=0; start=0; };
    for(const auto &v:samples) {
        const auto s=v.toMap();
        const qint64 ts=s.value("ts").toLongLong(), before=previous.value("ts").toLongLong();
        const double dt=ts-before;
        const bool contiguous=!previous.isEmpty() && dt>0 && dt<=90 && s.value("device")==previous.value("device") && s.value("state")=="Discharging" && previous.value("state")=="Discharging";
        if(!contiguous) { ++session; reset(); }
        if(!contiguous || !valid(s) || !valid(previous) || s.value("profile")!=previous.value("profile") || s.value("display")!=previous.value("display")) {
            reset(); previous=s; continue;
        }
        double offset=0;
        while(offset<dt) {
            if(seconds==0) start=before+qRound64(offset);
            const double take=std::min(dt-offset,300-seconds);
            const double wa=previous.value("watts").toDouble(), wb=s.value("watts").toDouble();
            const double ba=previous.value("brightness").toDouble(), bb=s.value("brightness").toDouble();
            const double fraction=(offset+take/2)/dt;
            energy+=(wa+(wb-wa)*fraction)*take/3600;
            brightness+=(ba+(bb-ba)*fraction)*take;
            cpu+=s.value("cpu").toDouble()*take;
            seconds+=take; offset+=take;
            if(seconds>=300-1e-8) {
                result.append(QVariantMap{{"start",start},{"end",before+qRound64(offset)},{"device",s.value("device")},{"session",session},{"watts",energy*12},
                    {"cpu",cpu/300},{"brightness",brightness/300},{"profile",s.value("profile")},{"display",s.value("display")}});
                reset();
            }
        }
        previous=s;
    }
    return result;
}
QVariantMap Analysis::compare(const QVariantList &samples) {
    const auto ws=windows(samples);
    QVariantMap out{{"status","warming"},{"windowCount",ws.size()},{"matches",0},
        {"cpuCorrelation",correlation(ws,"cpu")},{"brightnessCorrelation",correlation(ws,"brightness")}};
    QMap<QString,QPair<double,int>> profiles;
    for(const auto &v:ws) { auto w=v.toMap(); auto &p=profiles[w.value("profile").toString()]; p.first+=w.value("watts").toDouble(); ++p.second; }
    QVariantList groups;
    for(auto i=profiles.cbegin();i!=profiles.cend();++i) if(i->second>=3) groups.append(QVariantMap{{"profile",i.key()},{"watts",i->first/i->second},{"windows",i->second}});
    out["profiles"]=groups;
    if(ws.isEmpty() || samples.isEmpty()) return out;
    const auto current=ws.last().toMap(), latest=samples.last().toMap();
    if(!valid(latest) || latest.value("ts").toLongLong()-current.value("end").toLongLong()>300 || latest.value("profile")!=current.value("profile") || latest.value("display")!=current.value("display")) return out;
    // Do not compare an old completed session while a new one is warming up.
    const qint64 end=current.value("end").toLongLong();
    QVariantMap previous;
    for(const auto &v:samples) {
        const auto s=v.toMap();
        if(s.value("ts").toLongLong()>end && (s.value("device")!=current.value("device") || s.value("state")!="Discharging" || (!previous.isEmpty() && s.value("ts").toLongLong()-previous.value("ts").toLongLong()>90))) return out;
        previous=s;
    }
    out["current"]=current; out["status"]="no_matches";
    QList<double> peers;
    for(const auto &v:ws) {
        const auto w=v.toMap();
        if(w.value("session")==current.value("session") || w.value("end").toLongLong()>current.value("start").toLongLong()) continue;
        if(w.value("profile")==current.value("profile") && w.value("display")==current.value("display") &&
           std::abs(w.value("cpu").toDouble()-current.value("cpu").toDouble())<=10 &&
           std::abs(w.value("brightness").toDouble()-current.value("brightness").toDouble())<=10)
            peers.append(w.value("watts").toDouble());
    }
    out["matches"]=peers.size();
    if(peers.size()>=3) {
        const double baseline=median(peers);
        out["status"]="ready"; out["baselineWatts"]=baseline;
        out["deltaWatts"]=current.value("watts").toDouble()-baseline;
        out["deltaPercent"]=baseline>.1 ? (current.value("watts").toDouble()/baseline-1)*100 : QVariant();
        out["lowWatts"]=*std::min_element(peers.begin(),peers.end());
        out["highWatts"]=*std::max_element(peers.begin(),peers.end());
    }
    return out;
}
