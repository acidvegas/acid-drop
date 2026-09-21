#include "board/Touch.h"

#include <Wire.h>

#include "board/pins.h"
#include "core/Log.h"
#include "core/Settings.h"

namespace touch {
namespace {

constexpr const char* TAG = "touch";

// GT911 register map (16-bit, big-endian addresses).
constexpr uint16_t kRegStatus = 0x814E;
constexpr uint16_t kRegPoint0 = 0x8150;

uint8_t s_address = 0;
bool    s_flipped = false;

bool writeRegister(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(s_address);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readRegisters(uint16_t reg, uint8_t* out, size_t length) {
    Wire.beginTransmission(s_address);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;

    if (Wire.requestFrom(s_address, static_cast<uint8_t>(length)) != length) return false;
    for (size_t i = 0; i < length; i++) out[i] = Wire.read();
    return true;
}

bool probe(uint8_t candidate) {
    Wire.beginTransmission(candidate);
    return Wire.endTransmission() == 0;
}

} // namespace

bool begin() {
    for (uint8_t candidate : {TOUCH_I2C_ADDR_PRI, TOUCH_I2C_ADDR_ALT}) {
        if (probe(candidate)) {
            s_address = candidate;
            LOG_I(TAG, "GT911 at 0x%02X", candidate);
            return true;
        }
    }

    s_address = 0;
    LOG_W(TAG, "no touch controller found; touch disabled");
    return false;
}

void applyMapping(uint8_t index, uint16_t rawX, uint16_t rawY, int16_t& x, int16_t& y) {
    // The controller reports in the panel's native portrait frame. Which
    // quarter turn maps that onto the landscape UI depends on how the panel is
    // mounted, and it is measured by the calibration screen rather than
    // assumed here.
    switch (index) {
        case 1:
            x = static_cast<int16_t>((BOARD_TFT_WIDTH - 1) - rawY);
            y = static_cast<int16_t>(rawX);
            break;
        case 2:
            x = static_cast<int16_t>(rawY);
            y = static_cast<int16_t>(rawX);
            break;
        case 3:
            x = static_cast<int16_t>((BOARD_TFT_WIDTH - 1) - rawY);
            y = static_cast<int16_t>((BOARD_TFT_HEIGHT - 1) - rawX);
            break;
        default:
            x = static_cast<int16_t>(rawY);
            y = static_cast<int16_t>((BOARD_TFT_HEIGHT - 1) - rawX);
            break;
    }
}

bool readRaw(uint16_t& rawX, uint16_t& rawY) {
    if (s_address == 0) return false;

    uint8_t status = 0;
    if (!readRegisters(kRegStatus, &status, 1)) return false;
    if (!(status & 0x80)) return false;

    bool got = false;
    if ((status & 0x0F) > 0) {
        uint8_t point[8];
        if (readRegisters(kRegPoint0, point, sizeof(point))) {
            rawX = static_cast<uint16_t>(point[1]) | (static_cast<uint16_t>(point[2]) << 8);
            rawY = static_cast<uint16_t>(point[3]) | (static_cast<uint16_t>(point[4]) << 8);
            got = true;
        }
    }

    writeRegister(kRegStatus, 0);
    return got;
}

void setFlipped(bool flipped) { s_flipped = flipped; }

bool present()    { return s_address != 0; }
uint8_t address() { return s_address; }

bool read(int16_t& x, int16_t& y) {
    if (s_address == 0) return false;

    uint8_t status = 0;
    if (!readRegisters(kRegStatus, &status, 1)) return false;

    // Bit 7 means the controller has a fresh sample; the low nibble is the
    // number of points it is reporting.
    const bool ready  = status & 0x80;
    const uint8_t hits = status & 0x0F;
    if (!ready) return false;

    bool touched = false;
    if (hits > 0) {
        uint8_t point[8];
        if (readRegisters(kRegPoint0, point, sizeof(point))) {
            const uint16_t rawX = static_cast<uint16_t>(point[1]) | (static_cast<uint16_t>(point[2]) << 8);
            const uint16_t rawY = static_cast<uint16_t>(point[3]) | (static_cast<uint16_t>(point[4]) << 8);

            // The panel reports in its native portrait orientation (240x320)
            // while the UI runs landscape, so rotate a quarter turn here.
            applyMapping(settings::getEnum("touch_map"), rawX, rawY, x, y);

            if (s_flipped) {
                x = static_cast<int16_t>((BOARD_TFT_WIDTH - 1) - x);
                y = static_cast<int16_t>((BOARD_TFT_HEIGHT - 1) - y);
            }

            if (x < 0) x = 0;
            if (y < 0) y = 0;
            if (x >= BOARD_TFT_WIDTH)  x = BOARD_TFT_WIDTH - 1;
            if (y >= BOARD_TFT_HEIGHT) y = BOARD_TFT_HEIGHT - 1;

            // At info level for the first few, so the mapping can be checked
            // against a known tap without rebuilding.
            static uint8_t logged = 0;
            if (logged < 12) {
                logged++;
                LOG_I(TAG, "raw %u,%u -> screen %d,%d (mapping %u)",
                      rawX, rawY, x, y, settings::getEnum("touch_map"));
            }
            touched = true;
        }
    }

    // The status flag has to be cleared or the controller stops reporting.
    writeRegister(kRegStatus, 0);
    return touched;
}

} // namespace touch
