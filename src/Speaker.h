#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <AudioGeneratorRTTTL.h>
#include <AudioOutputI2S.h>
#include <AudioFileSourcePROGMEM.h>

#include "pins.h"


#define BOARD_I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 44100


void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = BOARD_I2S_BCK,
        .ws_io_num = BOARD_I2S_WS,
        .data_out_num = BOARD_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(BOARD_I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(BOARD_I2S_PORT, &pin_config);
    i2s_set_clk(BOARD_I2S_PORT, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}


void playTone(float frequency, int duration, int volume = 16383) {
    volume = constrain(volume, 0, 32767);
    const int wave_period = SAMPLE_RATE / frequency;
    int16_t sample_buffer[wave_period];

    for (int i = 0; i < wave_period; ++i)
        sample_buffer[i] = (i < wave_period / 2) ? volume : -volume;

    int total_samples = SAMPLE_RATE * duration / 1000;
    int samples_written = 0;

    while (samples_written < total_samples) {
        int to_write = min(wave_period, total_samples - samples_written);
        i2s_write(BOARD_I2S_PORT, sample_buffer, to_write * sizeof(int16_t), (size_t *)&to_write, portMAX_DELAY);
        samples_written += to_write;
    }
}


void playRTTTL(const char* rtttl) {
    static AudioGeneratorRTTTL *rtttlGenerator = new AudioGeneratorRTTTL();
    static AudioOutputI2S *audioOutput = new AudioOutputI2S();
    static AudioFileSourcePROGMEM *fileSource = new AudioFileSourcePROGMEM(rtttl, strlen(rtttl));

    audioOutput->begin();
    rtttlGenerator->begin(fileSource, audioOutput);

    while (rtttlGenerator->isRunning())
        rtttlGenerator->loop();

    rtttlGenerator->stop();
    fileSource->close();
}


void playNotificationSound() {
    playTone(1000, 200);
    delay(100);
    playTone(1500, 200);
    delay(100);
    playTone(2000, 200);
}
