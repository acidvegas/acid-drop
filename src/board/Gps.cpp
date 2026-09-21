#include "board/Gps.h"

#include <TinyGPSPlus.h>

#include "board/pins.h"
#include "core/Log.h"
#include "core/Settings.h"

namespace gps {
namespace {

constexpr const char* TAG = "gps";

TinyGPSPlus s_parser;
bool        s_enabled = false;
bool        s_started = false;
uint32_t    s_lastSentence = 0;

} // namespace

void begin() {
#if ACID_BOARD_TDECK_PLUS
    setEnabled(settings::getBool("gps_enable"));
#else
    LOG_I(TAG, "no GNSS module on this board");
    s_enabled = false;
#endif
}

void setEnabled(bool enabled) {
    if (enabled == s_enabled) return;
    s_enabled = enabled;

    if (enabled) {
        const uint32_t baud = settings::getInt("gps_baud");
        Serial1.begin(baud, SERIAL_8N1, BOARD_GPS_RX, BOARD_GPS_TX);
        s_started = true;
        LOG_I(TAG, "receiver on at %u baud", baud);
    } else if (s_started) {
        Serial1.end();
        s_started = false;
        LOG_I(TAG, "receiver off");
    }
}

bool enabled() { return s_enabled; }

void loop() {
    if (!s_enabled || !s_started) return;

    // Bounded so a receiver spewing sentences cannot stall the UI.
    int budget = 256;
    while (Serial1.available() && budget-- > 0) {
        if (s_parser.encode(static_cast<char>(Serial1.read()))) {
            s_lastSentence = millis();
        }
    }
}

bool hasFix() {
    return s_enabled && s_parser.location.isValid() && s_parser.location.age() < 10000;
}

uint8_t  satellites()     { return s_parser.satellites.isValid() ? s_parser.satellites.value() : 0; }
double   latitude()       { return s_parser.location.lat(); }
double   longitude()      { return s_parser.location.lng(); }
double   altitudeMeters() { return s_parser.altitude.isValid() ? s_parser.altitude.meters() : 0.0; }
double   speedKnots()     { return s_parser.speed.isValid() ? s_parser.speed.knots() : 0.0; }
uint32_t fixAgeMs()       { return s_parser.location.age(); }

String summary() {
    if (!s_enabled) return "off";
    if (!hasFix()) {
        const uint8_t sats = satellites();
        if (s_lastSentence == 0) return "no signal";
        return sats > 0 ? "searching (" + String(sats) + " sats)" : "searching";
    }
    return String(latitude(), 5) + ", " + String(longitude(), 5) +
           " (" + String(satellites()) + " sats)";
}

uint32_t unixTime() {
    if (!s_parser.date.isValid() || !s_parser.time.isValid()) return 0;

    const uint16_t year = s_parser.date.year();
    if (year < 2020 || year > 2100) return 0;

    // GNSS time is UTC. mktime() would interpret the fields in the local zone
    // and there is no portable timegm() here, so count the days directly.
    static const uint16_t kDaysBeforeMonth[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };

    const uint16_t month = s_parser.date.month();
    const uint16_t day   = s_parser.date.day();
    if (month < 1 || month > 12 || day < 1 || day > 31) return 0;

    uint32_t days = 0;
    for (uint16_t y = 1970; y < year; y++) {
        const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        days += leap ? 366 : 365;
    }
    days += kDaysBeforeMonth[month - 1];

    const bool leapThisYear = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (leapThisYear && month > 2) days++;
    days += day - 1;

    return days * 86400UL +
           s_parser.time.hour()   * 3600UL +
           s_parser.time.minute() * 60UL +
           s_parser.time.second();
}

} // namespace gps
