#include "board/Power.h"

#include <esp32-hal-cpu.h>

#include "board/Display.h"
#include "board/Input.h"
#include "board/pins.h"
#include "core/Log.h"
#include "core/Settings.h"

namespace power {
namespace {

constexpr const char* TAG = "power";

uint16_t s_millivolts = 0;
bool     s_dimmed     = false;
bool     s_screenOn   = true;

uint8_t  s_brightness = 200;
uint8_t  s_dimLevel   = 25;
uint32_t s_dimAfterMs = 20000;
uint32_t s_offAfterMs = 60000;

bool     s_lowWarned  = false;

void sampleBattery() {
    uint32_t total = 0;
    for (uint8_t i = 0; i < BATTERY_SAMPLES; i++) {
        total += analogReadMilliVolts(BOARD_BAT_ADC);
    }
    const uint16_t reading =
        static_cast<uint16_t>((total / BATTERY_SAMPLES) * BATTERY_CONV_FACTOR);

    // Light smoothing: the ADC wanders by tens of millivolts and a jittering
    // percentage in the status bar looks broken.
    s_millivolts = s_millivolts == 0 ? reading : (s_millivolts * 3 + reading) / 4;
}

} // namespace

void begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(BOARD_BAT_ADC, ADC_11db);

    sampleBattery();
    applySettings();

    // Keep in step with whoever changes these, wherever they change them.
    settings::onChange([](const char* key) {
        if (strcmp(key, "brightness") == 0 || strcmp(key, "dim_level") == 0 ||
            strcmp(key, "dim_secs")   == 0 || strcmp(key, "off_secs")  == 0 ||
            strcmp(key, "cpu_mhz")    == 0) {
            applySettings();
        }
    });

    LOG_I(TAG, "battery %u mV (%u%%)", s_millivolts, batteryPercent());
}

void applySettings() {
    s_brightness = settings::getInt("brightness");
    s_dimLevel   = settings::getInt("dim_level");
    s_dimAfterMs = static_cast<uint32_t>(settings::getInt("dim_secs")) * 1000UL;
    s_offAfterMs = static_cast<uint32_t>(settings::getInt("off_secs")) * 1000UL;

    if (s_screenOn && !s_dimmed) display::setBrightness(s_brightness);

    static const uint16_t kFrequencies[] = {80, 160, 240};
    const uint16_t wanted = kFrequencies[settings::getEnum("cpu_mhz")];
    if (getCpuFrequencyMhz() != wanted) {
        setCpuFrequencyMhz(wanted);
        LOG_I(TAG, "CPU set to %u MHz", wanted);
    }
}

void loop() {
    static uint32_t lastSample = 0;
    const uint32_t  now        = millis();

    if (now - lastSample > 5000) {
        lastSample = now;
        sampleBattery();

        const uint8_t percent = batteryPercent();
        const uint8_t warnAt  = settings::getInt("batt_warn");
        if (percent <= warnAt && !probablyCharging()) {
            if (!s_lowWarned) {
                s_lowWarned = true;
                LOG_W(TAG, "battery low: %u%%", percent);
            }
        } else if (percent > warnAt + 5) {
            s_lowWarned = false;
        }
    }

    const uint32_t idle = now - input::lastActivity();

    if (s_offAfterMs > 0 && idle > s_offAfterMs) {
        if (s_screenOn) {
            s_screenOn = false;
            display::sleep();
        }
        return;
    }

    if (s_dimAfterMs > 0 && idle > s_dimAfterMs) {
        if (!s_dimmed && s_screenOn) {
            s_dimmed = true;
            display::setBrightness(s_dimLevel);
        }
        return;
    }

    // Back inside the active window.
    if (!s_screenOn) {
        s_screenOn = true;
        display::setBrightness(s_brightness);
        display::wake();
    }
    if (s_dimmed) {
        s_dimmed = false;
        display::setBrightness(s_brightness);
    }
}

uint16_t batteryMillivolts() { return s_millivolts; }

uint8_t batteryPercent() {
    if (s_millivolts <= BATTERY_MIN_MV) return 0;
    if (s_millivolts >= BATTERY_MAX_MV) return 100;

    // A lithium cell's voltage curve is not a straight line, but across the
    // 3.3-4.2V window a linear fit is within a few percent and needs no table.
    const uint32_t span = BATTERY_MAX_MV - BATTERY_MIN_MV;
    return static_cast<uint8_t>(((s_millivolts - BATTERY_MIN_MV) * 100UL) / span);
}

bool probablyCharging() {
    return s_millivolts > 4250;
}

void wake() {
    input::noteActivity();
    if (!s_screenOn) {
        s_screenOn = true;
        display::wake();
    }
    s_dimmed = false;

    // Read the setting rather than a cached copy: anything that changed the
    // brightness live (the shade slider) would otherwise be undone from here.
    s_brightness = settings::getInt("brightness");
    display::setBrightness(s_brightness);
}

bool screenOn() { return s_screenOn; }

} // namespace power
