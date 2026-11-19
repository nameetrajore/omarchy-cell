#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QTimer>
#include <QVariantMap>
#include "telemetry.h"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap battery READ battery NOTIFY changed)
    Q_PROPERTY(QVariantList points READ points NOTIFY changed)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit Backend(const QString &root = "/sys/class/power_supply", const QString &database = {}, bool demo = false, QObject *parent = nullptr, bool readOnly = false);
    ~Backend() override;
    QVariantMap battery() const { return m_battery; }
    QVariantList points() const { return m_points; }
    QVariantList sessions() const { return m_sessions; }
    QString error() const { return m_error; }
    Q_INVOKABLE void refresh();
    void start(bool history = true);
    QVariantMap report();
    static QVariantMap readBattery(const QString &root);
    static QVariantList summarize(const QVariantList &samples);
    static bool installSkill(QString *error);
signals:
    void changed();
private:
    QString m_root, m_error, m_connection;
    bool m_demo;
    bool m_history = true;
    Telemetry m_telemetry;
    bool loadHistory();
    QSqlDatabase m_db;
    QVariantMap m_battery;
    QVariantList m_points, m_sessions;
    QTimer m_timer;
};
