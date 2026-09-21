#include "board/Input.h"

#include <Wire.h>

#include "board/Display.h"
#include "board/pins.h"
#include "core/Log.h"

namespace input {
namespace {

constexpr const char* TAG = "input";

lv_indev_t* s_keypad  = nullptr;
lv_indev_t* s_pointer = nullptr;
lv_group_t* s_group   = nullptr;

KeyHook  s_hook;
uint32_t s_lastActivity = 0;
uint32_t s_readyAt      = 0;   // ignore input until the pins have settled
uint8_t  s_ballDivisor  = 2;

// --- key queue ------------------------------------------------------------
// LVGL polls at its own rate, so keys are buffered rather than dropped.
constexpr uint8_t kQueueSize = 16;

struct KeyQueue {
    uint32_t items[kQueueSize];
    uint8_t  head = 0;
    uint8_t  tail = 0;

    bool empty() const { return head == tail; }

    void push(uint32_t key) {
        const uint8_t next = (head + 1) % kQueueSize;
        if (next == tail) return;        // full: drop the newest
        items[head] = key;
        head = next;
    }

    uint32_t pop() {
        if (empty()) return 0;
        const uint32_t key = items[tail];
        tail = (tail + 1) % kQueueSize;
        return key;
    }
};

KeyQueue s_keys;

// The key LVGL is currently told is held down. LVGL wants a press and a release
// for every key, so each queued key spans two read callbacks.
uint32_t s_heldKey     = 0;
bool     s_keyReleased = true;

// --- trackball ------------------------------------------------------------
volatile int16_t s_ballUp    = 0;
volatile int16_t s_ballDown  = 0;
volatile int16_t s_ballLeft  = 0;
volatile int16_t s_ballRight = 0;

void IRAM_ATTR onBallUp()    { s_ballUp++; }
void IRAM_ATTR onBallDown()  { s_ballDown++; }
void IRAM_ATTR onBallLeft()  { s_ballLeft++; }
void IRAM_ATTR onBallRight() { s_ballRight++; }

// Drains one axis' pulse counter into key events.
void drainAxis(volatile int16_t& counter, uint32_t key) {
    noInterrupts();
    const int16_t pulses = counter;
    if (pulses >= s_ballDivisor) counter = 0;
    interrupts();

    if (pulses < s_ballDivisor) return;

    int steps = pulses / s_ballDivisor;
    if (steps > 4) steps = 4;            // a hard flick should not spray keys
    for (int i = 0; i < steps; i++) s_keys.push(key);
    noteActivity();
}

// --- keyboard -------------------------------------------------------------
uint32_t translateKeyboard(char raw) {
    switch (raw) {
        case 0x08: return LV_KEY_BACKSPACE;
        case 0x0D:
        case 0x0A: return LV_KEY_ENTER;
        case 0x09: return LV_KEY_NEXT;
        case 0x1B: return LV_KEY_ESC;
        default:   return static_cast<uint32_t>(raw);
    }
}

void pollKeyboard() {
    // The keypad returns 0x00 when nothing was pressed since the last read.
    if (Wire.requestFrom(static_cast<uint8_t>(KEYBOARD_I2C_ADDR), static_cast<uint8_t>(1)) != 1) {
        return;
    }
    const int raw = Wire.read();
    if (raw <= 0) return;

    s_keys.push(translateKeyboard(static_cast<char>(raw)));
    noteActivity();
}

// --- LVGL callbacks -------------------------------------------------------
void keypadReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    LV_UNUSED(indev);

    if (!s_keyReleased) {
        // Release the key we reported last time.
        data->key   = s_heldKey;
        data->state = LV_INDEV_STATE_RELEASED;
        s_keyReleased = true;
        data->continue_reading = !s_keys.empty();
        return;
    }

    if (s_keys.empty()) {
        data->key   = s_heldKey;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint32_t key = s_keys.pop();

    // Give the active app first refusal.
    if (s_hook && s_hook(key)) {
        data->key   = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = !s_keys.empty();
        return;
    }

    // LVGL moves focus with NEXT/PREV; a bare arrow key is delivered to the
    // focused widget instead. Rolling the ball up and down should walk the
    // screen, so translate the vertical axis once no app has claimed it.
    // Horizontal is left alone, so sliders and rollers still adjust.
    if (key == LV_KEY_UP)   key = LV_KEY_PREV;
    if (key == LV_KEY_DOWN) key = LV_KEY_NEXT;

    s_heldKey     = key;
    s_keyReleased = false;
    data->key     = key;
    data->state   = LV_INDEV_STATE_PRESSED;
    data->continue_reading = false;
}

void pointerReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
    LV_UNUSED(indev);

    static int32_t lastX = 0;
    static int32_t lastY = 0;

    uint16_t x = 0;
    uint16_t y = 0;
    const bool touched = gfx.getTouch(&x, &y);

    if (touched) {
        lastX = x;
        lastY = y;
        data->state = LV_INDEV_STATE_PRESSED;
        noteActivity();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    data->point.x = lastX;
    data->point.y = lastY;
}

} // namespace

void begin() {
    s_group = lv_group_create();
    lv_group_set_default(s_group);

    s_keypad = lv_indev_create();
    lv_indev_set_type(s_keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_keypad, keypadReadCb);
    lv_indev_set_group(s_keypad, s_group);

    s_pointer = lv_indev_create();
    lv_indev_set_type(s_pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_pointer, pointerReadCb);

    // The trackball pulls each line low as the ball rolls past the sensor.
    const struct { uint8_t pin; void (*handler)(); } kBall[] = {
        {BOARD_TRACKBALL_UP,    onBallUp},
        {BOARD_TRACKBALL_DOWN,  onBallDown},
        {BOARD_TRACKBALL_LEFT,  onBallLeft},
        {BOARD_TRACKBALL_RIGHT, onBallRight},
    };
    for (const auto& axis : kBall) {
        pinMode(axis.pin, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(axis.pin), axis.handler, FALLING);
    }
    pinMode(BOARD_TRACKBALL_CLICK, INPUT_PULLUP);

    // The click line is GPIO0, which is also the boot strapping pin. Give the
    // pull-up time to win before anything reads it as a press.
    s_readyAt      = millis() + 400;
    s_lastActivity = millis();
    LOG_I(TAG, "touch, trackball and keyboard registered");
}

void loop() {
    static uint32_t lastKeyboardPoll = 0;

    const uint32_t now = millis();
    if (static_cast<int32_t>(now - s_readyAt) < 0) return;

    // Seeded from the pin rather than from false, so a line that is already low
    // when we start up is not mistaken for a fresh press. Getting this wrong
    // fires an ENTER into whatever has focus the moment the UI appears.
    static bool lastClick = digitalRead(BOARD_TRACKBALL_CLICK) == LOW;

    // 20ms is faster than anyone types and slow enough to stay off the I2C bus.
    if (now - lastKeyboardPoll >= 20) {
        lastKeyboardPoll = now;
        pollKeyboard();
    }

    drainAxis(s_ballUp,    LV_KEY_UP);
    drainAxis(s_ballDown,  LV_KEY_DOWN);
    drainAxis(s_ballLeft,  LV_KEY_LEFT);
    drainAxis(s_ballRight, LV_KEY_RIGHT);

    const bool click = digitalRead(BOARD_TRACKBALL_CLICK) == LOW;
    if (click && !lastClick) {
        s_keys.push(LV_KEY_ENTER);
        noteActivity();
    }
    lastClick = click;
}

lv_indev_t* keypad()  { return s_keypad; }
lv_indev_t* pointer() { return s_pointer; }
lv_group_t* group()   { return s_group; }

void setKeyHook(KeyHook hook) { s_hook = std::move(hook); }
void clearKeyHook()           { s_hook = nullptr; }

uint32_t lastActivity() { return s_lastActivity; }
void     noteActivity() { s_lastActivity = millis(); }

void setBallDivisor(uint8_t divisor) { s_ballDivisor = divisor ? divisor : 1; }

} // namespace input
