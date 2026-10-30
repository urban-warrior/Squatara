#ifndef SQUATARA_PERSISTENT_H
#define SQUATARA_PERSISTENT_H

Preferences inline preferences;

constexpr const char *LOG_LEVEL_KEY = "log=level";
constexpr const char *THRESHOLD_KEY = "threshold";
constexpr const char *BATTERY_KEY = "battery";
constexpr const char *BUZZTIME_KEY = "buzztime";
constexpr const char *HYSTERESIS_KEY = "hysteresis";

static void setup_log_level()
{
    preferences.putUChar(LOG_LEVEL_KEY, preferences.getUChar(LOG_LEVEL_KEY, static_cast<uint8_t>(LogLevel::None)));
    Logger.setLevel(static_cast<LogLevel>(preferences.getUChar(LOG_LEVEL_KEY, static_cast<uint8_t>(LogLevel::None))));
}

static void setup_persistent()
{
    preferences.putUChar(THRESHOLD_KEY, preferences.getUChar(THRESHOLD_KEY, 75));
    preferences.putUChar(BATTERY_KEY, preferences.getUChar(BATTERY_KEY, true));
    preferences.putFloat(BUZZTIME_KEY, preferences.getFloat(BUZZTIME_KEY, 2.0 /* seconds */));
    preferences.putUChar(HYSTERESIS_KEY, preferences.getUChar(HYSTERESIS_KEY, 5 /* % */));
}

#endif //SQUATARA_PERSISTENT_H
