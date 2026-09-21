#include "core/Storage.h"

#include <SD.h>
#include <SPI.h>

#include "board/pins.h"
#include "core/Log.h"

namespace storage {
namespace {

constexpr const char* TAG = "storage";

bool s_mounted = false;
bool s_tried   = false;

} // namespace

bool ensureSdCard() {
    if (s_mounted) return true;
    if (s_tried)   return false;

    s_tried = true;

    pinMode(BOARD_SDCARD_CS, OUTPUT);
    digitalWrite(BOARD_SDCARD_CS, HIGH);

    // The display owns this bus; SD needs it configured as an Arduino SPIClass
    // too, so set it up here rather than at boot where it would disturb the
    // panel before anything has been drawn.
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    if (!SD.begin(BOARD_SDCARD_CS, SPI, 4000000U)) {
        LOG_I(TAG, "no SD card");
        return false;
    }

    s_mounted = true;
    LOG_I(TAG, "SD card mounted, %llu MB", SD.cardSize() / (1024ULL * 1024ULL));
    return true;
}

bool sdMounted() { return s_mounted; }

void forgetSdCard() {
    if (s_mounted) SD.end();
    s_mounted = false;
    s_tried   = false;
}

} // namespace storage
