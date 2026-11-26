#pragma once

#include <QDateTime>

namespace aurora
{

struct Reading {
    QDateTime time;
    double temperature{0.0}; // °C
    double humidity{0.0};    // %
    double pressure{0.0};    // hPa
    double windSpeed{0.0};   // m/s
};

} // namespace aurora
