#include "Lora.h"


SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);


void recvLoop() {
    String recv;

    while (true) {
        if (radio.available()) {
            int state = radio.readData(recv);

            if (state == RADIOLIB_ERR_NONE) {
                playNotificationSound();
                Serial.print(F("[RADIO] Received packet!"));
                Serial.print(F(" Data:"));
                Serial.print(recv);
                Serial.print(F(" RSSI:"));
                Serial.print(radio.getRSSI());
                Serial.print(F(" dBm"));
                Serial.print(F("  SNR:"));
                Serial.print(radio.getSNR());
                Serial.println(F(" dB"));
            } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
                Serial.println(F("CRC error!"));
            } else {
                Serial.print(F("Failed, code "));
                Serial.println(state);
            }
        } else {
            Serial.println(F("Radio became unavailable!"));
            break;
        }
    }
}


bool setupRadio() {
    pinMode(RADIO_CS_PIN, OUTPUT);
    digitalWrite(RADIO_CS_PIN, HIGH);

    int state = radio.begin(RADIO_FREQ);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("Start Radio success!");
    } else {
        Serial.print("Start Radio failed, code: ");
        Serial.println(state);
        return false;
    }

    if (radio.setFrequency(RADIO_FREQ) == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println(F("Selected frequency is invalid for this module!"));
        return false;
    }

    if (radio.setBandwidth(125.0) == RADIOLIB_ERR_INVALID_BANDWIDTH) {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
        return false;
    }

    if (radio.setSpreadingFactor(10) == RADIOLIB_ERR_INVALID_SPREADING_FACTOR) {
        Serial.println(F("Selected spreading factor is invalid for this module!"));
        return false;
    }

    if (radio.setCodingRate(6) == RADIOLIB_ERR_INVALID_CODING_RATE) {
        Serial.println(F("Selected coding rate is invalid for this module!"));
        return false;
    }

    if (radio.setSyncWord(0xAB) != RADIOLIB_ERR_NONE) {
        Serial.println(F("Unable to set sync word!"));
        return false;
    }

    // Set output power to 17 dBm (accepted range is -17 - 22 dBm)
    if (radio.setOutputPower(17) == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        Serial.println(F("Selected output power is invalid for this module!"));
        return false;
    }

    // Set over current protection limit to 140 mA (accepted range is 45 - 140 mA)
    if (radio.setCurrentLimit(140) == RADIOLIB_ERR_INVALID_CURRENT_LIMIT) {
        Serial.println(F("Selected current limit is invalid for this module!"));
        return false;
    }

    // Set LoRa preamble length to 15 symbols (accepted range is 0 - 65535)
    if (radio.setPreambleLength(15) == RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH) {
        Serial.println(F("Selected preamble length is invalid for this module!"));
        return false;
    }

    if (radio.setCRC(false) == RADIOLIB_ERR_INVALID_CRC_CONFIGURATION) {
        Serial.println(F("Selected CRC is invalid for this module!"));
        return false;
    }

    return true;
}


bool transmit() {
    int state = radio.transmit("Hello World!");

    if (state == RADIOLIB_ERR_NONE) {
        Serial.println(F("Radio transmission successful!"));
        Serial.print(F("[SX1262] Datarate:\t"));
        Serial.print(radio.getDataRate());
        Serial.println(F(" bps"));
        return true;
    } else if (state == RADIOLIB_ERR_PACKET_TOO_LONG) {
        Serial.println(F("Radio packet too long")); // 256 bytes is the maximum packet length
        return false;
    } else if (state == RADIOLIB_ERR_TX_TIMEOUT) {
        Serial.println(F("Radio timeout"));
        return false;
    } else {
        Serial.print(F("Radio error failed, code "));
        Serial.println(state);
        return false;
    }
}
