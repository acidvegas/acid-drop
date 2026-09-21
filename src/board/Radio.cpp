#include "board/Radio.h"

#include <RadioLib.h>
#include <SPI.h>

#include "board/pins.h"
#include "core/Log.h"
#include "core/Settings.h"

namespace radio {

std::function<void(const String&, float, float)> onPacket;

namespace {

constexpr const char* TAG = "lora";

SX1262* s_radio  = nullptr;
bool    s_enabled = false;
String  s_error;

// Set from the DIO1 interrupt, cleared by loop().
volatile bool s_packetWaiting = false;

void IRAM_ATTR onDio1() { s_packetWaiting = true; }

float bandwidthKhz() {
    static const float kValues[] = {125.0f, 250.0f, 500.0f};
    return kValues[settings::getEnum("lora_bw")];
}

} // namespace

void begin() {
    if (settings::getBool("lora_enable")) setEnabled(true);
}

bool setEnabled(bool enable) {
    if (enable == s_enabled) return true;

    if (!enable) {
        if (s_radio) {
            s_radio->sleep();
            detachInterrupt(digitalPinToInterrupt(RADIO_DIO1_PIN));
            delete s_radio;
            s_radio = nullptr;
        }
        s_enabled = false;
        LOG_I(TAG, "radio off");
        return true;
    }

    // The display already owns SPI2; RadioLib just needs the same bus.
    s_radio = new SX1262(new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN));

    const float    frequency = settings::getFloat("lora_freq");
    const uint8_t  spreading = settings::getInt("lora_sf");
    const uint8_t  coding    = settings::getInt("lora_cr");
    const int8_t   power     = settings::getInt("lora_power");

    const int16_t status = s_radio->begin(frequency, bandwidthKhz(), spreading, coding,
                                          RADIOLIB_SX126X_SYNC_WORD_PRIVATE, power);
    if (status != RADIOLIB_ERR_NONE) {
        s_error = "init failed (" + String(status) + ")";
        LOG_E(TAG, "%s", s_error.c_str());
        delete s_radio;
        s_radio = nullptr;
        return false;
    }

    s_radio->setDio1Action(onDio1);

    const int16_t listening = s_radio->startReceive();
    if (listening != RADIOLIB_ERR_NONE) {
        s_error = "receive failed (" + String(listening) + ")";
        LOG_E(TAG, "%s", s_error.c_str());
    }

    s_enabled = true;
    s_error   = "";
    LOG_I(TAG, "up on %.2f MHz, SF%u, BW %.0f kHz, %d dBm",
          frequency, spreading, bandwidthKhz(), power);
    return true;
}

bool enabled() { return s_enabled; }

void applySettings() {
    if (!s_enabled) return;
    // The cheapest way to apply a whole new radio configuration is to restart.
    setEnabled(false);
    setEnabled(true);
}

void loop() {
    if (!s_enabled || !s_radio || !s_packetWaiting) return;
    s_packetWaiting = false;

    String payload;
    const int16_t status = s_radio->readData(payload);

    if (status == RADIOLIB_ERR_NONE) {
        const float rssi = s_radio->getRSSI();
        const float snr  = s_radio->getSNR();
        LOG_I(TAG, "rx %u bytes, %.1f dBm, %.1f dB", payload.length(), rssi, snr);
        if (onPacket) onPacket(payload, rssi, snr);
    } else if (status != RADIOLIB_ERR_RX_TIMEOUT) {
        LOG_W(TAG, "rx error %d", status);
    }

    s_radio->startReceive();
}

bool send(const String& text) {
    if (!s_enabled || !s_radio) return false;

    // RadioLib takes a non-const String reference, so hand it a copy.
    String payload = text;
    const int16_t status = s_radio->transmit(payload);
    s_radio->startReceive();

    if (status != RADIOLIB_ERR_NONE) {
        s_error = "transmit failed (" + String(status) + ")";
        LOG_W(TAG, "%s", s_error.c_str());
        return false;
    }
    return true;
}

String lastError() { return s_error; }

} // namespace radio
