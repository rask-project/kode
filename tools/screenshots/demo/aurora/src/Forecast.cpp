#include "Forecast.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace aurora
{

namespace
{

constexpr qsizetype window{12};    // readings in the trend
constexpr qsizetype hoursAhead{6};

// Least-squares slope of the pressure, in hPa per reading.
double pressureSlope(const QList<Reading> &readings)
{
    const auto n{static_cast<double>(readings.size())};
    if (n < 2) {
        return 0.0;
    }
    double sumX{0}, sumY{0}, sumXY{0}, sumXX{0};
    for (qsizetype i{0}; i < readings.size(); ++i) {
        const double x{static_cast<double>(i)};
        const double y{readings[i].pressure};
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;
    }
    return (n * sumXY - sumX * sumY) / (n * sumXX - sumX * sumX);
}

} // namespace

Forecast::Forecast(QObject *parent) : QObject(parent) {}

void Forecast::setStation(const QString &station)
{
    if (m_station == station) {
        return;
    }
    m_station = station;
    m_readings.clear();
    emit stationChanged();
    recompute();
}

void Forecast::addReading(const Reading &reading)
{
    m_readings.append(reading);
    if (m_readings.size() > window) {
        m_readings.removeFirst();
    }
    recompute();
}

QVariantList Forecast::hours() const
{
    QVariantList list;
    for (const Reading &hour : m_hours) {
        list.append(QVariantMap{
            {"time", hour.time},
            {"temperature", std::round(hour.temperature)},
            {"wind", hour.windSpeed},
        });
    }
    return list;
}

void Forecast::recompute()
{
    m_hours.clear();
    m_trend = pressureSlope(m_readings);
    if (m_readings.isEmpty()) {
        emit updated();
        return;
    }
    const Reading &last{m_readings.last()};
    for (qsizetype hour{1}; hour <= hoursAhead; ++hour) {
        Reading next{last};
        next.time = last.time.addSecs(hour * 3600);
        // Falling pressure: cooler and windier.
        next.temperature += m_trend * 0.8 * hour;
        next.windSpeed = std::max(0.0, last.windSpeed
                                           - m_trend * 1.5 * hour);
        m_hours.append(next);
    }
    emit updated();
}

} // namespace aurora
