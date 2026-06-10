#include "ecopost_sensors.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const sensor_def SENSORS[SERIES_COUNT] = {
    {"air.temperature", "Температура", "°C"},
    {"air.humidity", "Влажность", "%"},
    {"air.pressure", "Давление", "гПа"},
    {"wind.speed", "Ветер", "м/с"},
    {"pm25.concentration", "PM2.5", "мкг/м³"},
    {"battery.voltage", "Аккумулятор", "В"},
};

static double smooth_noise(size_t i, double scale) {
    double a = sin((double)i * 0.073) * 0.5 + cos((double)i * 0.017) * 0.5;
    return a * scale;
}

double sensor_value(size_t index, size_t sensor_index, int pollution_active) {
    double seconds = (double)((index * (size_t)STEP_MS) / 1000u);
    double day = fmod(seconds, 86400.0) / 86400.0;
    double daylight = sin((day - 0.25) * 2.0 * M_PI);
    if (daylight < 0.0) {
        daylight = 0.0;
    }
    double temperature = 12.0 + 13.0 * sin((day - 0.32) * 2.0 * M_PI) + smooth_noise(index, 0.45);

    switch (sensor_index) {
        case 0:
            return temperature;
        case 1:
            return 74.0 - (temperature - 12.0) * 1.2 + smooth_noise(index + 31u, 1.0);
        case 2:
            return 1013.0 + 3.5 * sin(day * 2.0 * M_PI + 0.6) + smooth_noise(index, 0.18);
        case 3: {
            double gust = ((index % 1800u) > 760u && (index % 1800u) < 805u) ? 5.0 : 0.0;
            return 2.2 + daylight * 2.1 + gust + fabs(smooth_noise(index + 9u, 0.8));
        }
        case 4: {
            double manual_spike = pollution_active ? 90.0 : 0.0;
            return 18.0 + 4.0 * sin(day * 2.0 * M_PI + 1.2) + manual_spike + smooth_noise(index, 1.5);
        }
        case 5:
            return 3.62 + daylight * 0.35 - (1.0 - daylight) * 0.08 + smooth_noise(index, 0.015);
        default:
            return 0.0;
    }
}
