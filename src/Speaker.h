#pragma once

#include <Arduino.h>
#include <driver/i2s.h>

#include "pins.h"

#define BOARD_I2S_PORT I2S_NUM_0
#define SAMPLE_RATE 44100


const float NOTE_FREQS[] = {
    261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99, 392.00, 415.30, 440.00, 466.16, 493.88, // C4 to B4
    523.25, 554.37, 587.33, 622.25, 659.25, 698.46, 739.99, 783.99, 830.61, 880.00, 932.33, 987.77, // C5 to B5
    1046.50, 1108.73, 1174.66, 1244.51, 1318.51, 1396.91, 1480.00, 1568.00, 1661.22, 1760.00, 1864.66, 1975.53, // C6 to B6
    2093.00, 2217.46, 2349.32, 2489.02, 2637.02, 2793.83, 2960.00, 3136.00, 3322.44, 3520.00, 3729.31, 3951.07  // C7 to B7
};


float getNoteFrequency(char note, int octave) {
    if (note == 'p') return 0;  // Pause
    int noteIndex = 0;
    switch (note) {
        case 'c': noteIndex = 0; break;
        case 'd': noteIndex = 2; break;
        case 'e': noteIndex = 4; break;
        case 'f': noteIndex = 5; break;
        case 'g': noteIndex = 7; break;
        case 'a': noteIndex = 9; break;
        case 'b': noteIndex = 11; break;
        default: return 0;
    }
    return NOTE_FREQS[noteIndex + ((octave - 4) * 12)];
}


void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,  // Default interrupt priority
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
    volume = constrain(volume, 0, 32767); // Max volume is 32767, we default to half volume if not specified
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


void playRTTTL(const char* rtttl, int volume = 16383, int bpm = -1) {
    int default_duration = 4;
    int default_octave = 6;
    int internal_bpm = 63;

    const char* p = rtttl;

    // Skip name
    while (*p && *p != ':') p++;
    if (*p == ':') p++;

    while (*p && *p != ':') {
        char param = *p++;
        if (*p == '=') p++;
        int value = atoi(p);
        while (*p && isdigit(*p)) p++;
        if (*p == ',') p++;
        switch (param) {
            case 'd': default_duration = value; break;
            case 'o': default_octave = value; break;
            case 'b': internal_bpm = value; break;
        }
    }

    if (*p == ':') p++;

    if (bpm != -1)
        internal_bpm = bpm;

    int beat_duration = 60000 / internal_bpm;

    while (*p) {
        int duration = 0;
        if (isdigit(*p)) {
            duration = atoi(p);
            while (isdigit(*p)) p++;
        } else {
            duration = default_duration;
        }

        char note = *p++;
        int frequency = getNoteFrequency(note, default_octave);

        if (*p == '#') {
            frequency = getNoteFrequency(note + 1, default_octave);
            p++;
        }

        int octave = default_octave;

        if (isdigit(*p))
            octave = *p++ - '0';

        if (*p == '.') {
            duration = duration * 1.5;
            p++;
        }

        int note_duration = (beat_duration * 4) / duration;

        if (frequency > 0)
            playTone(frequency, note_duration, volume);
        else
            delay(note_duration);

        if (*p == ',') p++;
    }
}


void playNotificationSound() {
    playTone(1000, 200);
    delay(100);
    playTone(1500, 200);
    delay(100);
    playTone(2000, 200);
}