#pragma once
#include <QMap>
#include <QVariantMap>

class Telemetry {
public:
    explicit Telemetry(const QString &sys = "/sys", const QString &proc = "/proc");
    QVariantMap sample(qint64 now);
    static QVariantMap parseCpu(const QByteArray &stat);
    static double utilization(const QVariantMap &before, const QVariantMap &after);
private:
    QString m_sys, m_proc;
    qint64 m_time = 0;
    QVariantMap m_cpu;
    struct Process { QString name; quint64 ticks; quint64 started; };
    QMap<QString, Process> m_processes;
};
