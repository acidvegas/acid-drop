#pragma once

#include <Arduino.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioGeneratorRTTTL.h>
#include <AudioOutputI2S.h>
#include <driver/i2s.h>

#include "pins.h"

#define BOARD_I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 44100

void setupI2S();
void playTone(float frequency, int duration, int volume = 16383);
void playRTTTL(const char* rtttl);
void playNotificationSound();
