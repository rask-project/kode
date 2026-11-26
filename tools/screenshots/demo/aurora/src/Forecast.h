#pragma once

#include "Reading.h"

#include <QList>
#include <QObject>
#include <QtQmlIntegration>

namespace aurora
{

class Forecast : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString station READ station WRITE setStation
                   NOTIFY stationChanged)
    Q_PROPERTY(double trend READ trend NOTIFY updated)
    Q_PROPERTY(QVariantList hours READ hours NOTIFY updated)

public:
    explicit Forecast(QObject *parent = nullptr);

    QString station() const { return m_station; }
    void setStation(const QString &station);

    double trend() const { return m_trend; }
    QVariantList hours() const;

    void addReading(const Reading &reading);

signals:
    void stationChanged();
    void updated();

private:
    void recompute();

    QString m_station;
    QList<Reading> m_readings;
    QList<Reading> m_hours;
    double m_trend{0.0};
};

} // namespace aurora
