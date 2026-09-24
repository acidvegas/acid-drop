#include "board/Input.h"


#include "board/Audio.h"
#include "board/Display.h"
#include "board/pins.h"
#include "core/Log.h"
#include "core/Settings.h"

namespace input {
namespace {

constexpr const char* TAG = "input";

lv_indev_t* s_keypad  = nullptr;
lv_indev_t* s_pointer = nullptr;
lv_group_t* s_group   = nullptr;

KeyHook  s_hook;
std::function<void()> s_holdHandler;
uint32_t s_lastActivity = 0;
uint32_t s_readyAt      = 0;   // ignore input until the pins have settled
// Trackball pulses required per emitted key, per axis. Lower is more
// sensitive. Set from the settings; the defaults are what shipped before they
// were configurable.
uint8_t  s_ballVerticalStep   = 2;
uint8_t  s_ballHorizontalStep = 5;

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

volatile uint32_t s_clickEdges = 0;

void IRAM_ATTR onBallClick() { s_clickEdges++; }

// Every pulse on any axis also bumps this. It is the only reliable way to ask
// "has the ball moved recently": the per-axis counters cannot answer, because
// pulses below the emit threshold are never consumed and so never read as
// zero. See drainBall().
volatile uint32_t s_ballTicks = 0;

void IRAM_ATTR onBallUp()    { s_ballUp++;    s_ballTicks++; }
void IRAM_ATTR onBallDown()  { s_ballDown++;  s_ballTicks++; }
void IRAM_ATTR onBallLeft()  { s_ballLeft++;  s_ballTicks++; }
void IRAM_ATTR onBallRight() { s_ballRight++; s_ballTicks++; }

// Axis lock.
//
// A roll across this trackball is never clean: a flick meant to scroll the
// backlog also ticks the left or right sensor a few times on the way past, and
// those stray pulses were switching IRC windows mid-scroll. So the first axis
// to cross its threshold claims the ball, the other axis is discarded while
// that lasts, and the claim is released once the ball has been still.
enum class BallAxis : uint8_t { None, Vertical, Horizontal };

BallAxis s_ballAxis      = BallAxis::None;
uint32_t s_ballLastMove  = 0;
uint32_t s_ballSeenTicks = 0;

// How long the ball has to sit still before the other axis can have a turn.
constexpr uint32_t kAxisReleaseMs = 300;

int16_t peek(volatile int16_t& counter) {
    noInterrupts();
    const int16_t value = counter;
    interrupts();
    return value;
}

void clearCounter(volatile int16_t& counter) {
    noInterrupts();
    counter = 0;
    interrupts();
}

// Drains one axis' pulse counter into key events.
void emitAxis(volatile int16_t& counter, uint32_t key, uint8_t step) {
    noInterrupts();
    const int16_t pulses = counter;
    if (pulses >= step) counter = 0;
    interrupts();

    if (pulses < step) return;

    int steps = pulses / step;
    if (steps > 4) steps = 4;            // a hard flick should not spray keys
    for (int i = 0; i < steps; i++) s_keys.push(key);
    noteActivity();
}

void drainBall() {
    const uint32_t now = millis();

    noInterrupts();
    const uint32_t ticks = s_ballTicks;
    interrupts();

    // Idle is measured from the pulse counter, not from the per-axis totals.
    // emitAxis() only consumes a counter once it reaches the threshold, so a
    // few sub-threshold pulses sit there indefinitely - and testing those for
    // zero meant the axis lock, once taken, was never released. The ball stayed
    // locked to whichever direction moved first and the other axis was wiped
    // on every pass, which killed backlog scrolling outright.
    if (ticks != s_ballSeenTicks) {
        s_ballSeenTicks = ticks;
        s_ballLastMove  = now;
    } else if (s_ballAxis != BallAxis::None &&
               static_cast<int32_t>(now - s_ballLastMove) >
               static_cast<int32_t>(kAxisReleaseMs)) {
        // Still for long enough: drop the lock and bin the leftover jitter, so
        // it cannot add up over minutes into a stray window change.
        s_ballAxis = BallAxis::None;
        clearCounter(s_ballUp);
        clearCounter(s_ballDown);
        clearCounter(s_ballLeft);
        clearCounter(s_ballRight);
        return;
    }

    const int16_t up    = peek(s_ballUp);
    const int16_t down  = peek(s_ballDown);
    const int16_t left  = peek(s_ballLeft);
    const int16_t right = peek(s_ballRight);

    const int16_t vertical   = up + down;
    const int16_t horizontal = left + right;

    if (vertical == 0 && horizontal == 0) return;

    const uint8_t verticalStep   = s_ballVerticalStep;
    const uint8_t horizontalStep = s_ballHorizontalStep;

    // Claim an axis for this gesture. Whichever direction has travelled
    // further wins, and it still has to clear its own threshold - below that
    // there is not enough movement to say which way this roll is going, so
    // the pulses are kept and reconsidered next pass.
    if (s_ballAxis == BallAxis::None) {
        if (vertical >= verticalStep && vertical >= horizontal) {
            s_ballAxis = BallAxis::Vertical;
        } else if (horizontal >= horizontalStep && horizontal > vertical) {
            s_ballAxis = BallAxis::Horizontal;
        } else {
            return;
        }
    }

    if (s_ballAxis == BallAxis::Vertical) {
        emitAxis(s_ballUp,   LV_KEY_UP,   verticalStep);
        emitAxis(s_ballDown, LV_KEY_DOWN, verticalStep);
        clearCounter(s_ballLeft);
        clearCounter(s_ballRight);
    } else {
        emitAxis(s_ballLeft,  LV_KEY_LEFT,  horizontalStep);
        emitAxis(s_ballRight, LV_KEY_RIGHT, horizontalStep);
        clearCounter(s_ballUp);
        clearCounter(s_ballDown);
    }
}

// --- keyboard backlight ---------------------------------------------------
// Commands taken from LilyGo's own Keyboard_T_Deck_Master example, which is
// the documentation for this interface.
constexpr uint8_t kKbBrightnessCmd     = 0x01;   // set brightness now, 0-255
constexpr uint8_t kKbAltBBrightnessCmd = 0x02;   // what ALT+B toggles to, 30-255

// LilyGo's example waits 500ms after the peripheral rail comes up before
// addressing the keyboard controller, because the C3 has to boot first. The
// rail is switched on about 150ms into setup(), so this is measured from
// power-on with that headroom folded in.
constexpr uint32_t kKeyboardReadyMs = 700;

void writeKeyboard(uint8_t command, uint8_t value) {
    const uint8_t payload[2] = {command, value};
    // Through lgfx::i2c for the same reason the key poll is: LovyanGFX owns
    // this port for the touch controller, and a second driver on it means one
    // of them silently stops working.
    if (!lgfx::i2c::transactionWrite(0, KEYBOARD_I2C_ADDR, payload,
                                     sizeof(payload), BOARD_I2C_FREQ).has_value()) {
        LOG_W(TAG, "keyboard did not accept command 0x%02X", command);
    }
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
    // Through lgfx::i2c, not Wire: LovyanGFX owns this port for the touch
    // controller, and two drivers on one port means one of them silently stops
    // working. The keypad returns 0x00 when nothing has been pressed.
    uint8_t raw = 0;
    if (!lgfx::i2c::transactionRead(0, KEYBOARD_I2C_ADDR, &raw,
                                    1, BOARD_I2C_FREQ).has_value()) {
        return;
    }
    if (raw == 0) return;

    s_keys.push(translateKeyboard(static_cast<char>(raw)));
    noteActivity();
    audio::alert(Alert::Key);   // no-op unless "Key clicks" is on
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

    // Copied before the call: a hook that switches apps tears down the app
    // that installed it, which clears s_hook - destroying the callable while
    // its own invocation is still on the stack.
    KeyHook hook = s_hook;
    if (hook && hook(key)) {
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
    // LVGL wraps focus by default, so rolling past the last row jumped back to
    // the first one. On a trackball that reads as the list teleporting: down
    // means down, and the end of the list is the end.
    lv_group_set_wrap(s_group, false);
    // Deliberately NOT lv_group_set_default(): that auto-adds every focusable
    // widget anyone creates, including the quick-settings sliders and toast
    // buttons living on the top layer. Trackball focus would then walk off the
    // current screen into invisible controls. Screens add their own widgets.

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
    // GPIO 0, shared with the boot strapping pin. Verified against LilyGo's
    // own UnitTest example, which puts the centre press in the same array as
    // the four direction lines and watches all five for CHANGE rather than
    // reading a level - which is what the previous attempt did, and why
    // holding the button registered as nothing at all.
    pinMode(BOARD_TRACKBALL_CLICK, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(BOARD_TRACKBALL_CLICK), onBallClick, CHANGE);

    // The click line is GPIO0, which is also the boot strapping pin. Give the
    // pull-up time to win before anything reads it as a press.
    s_readyAt      = millis() + 400;
    s_lastActivity = millis();

    applySettings();

    // The keyboard controller has to have finished booting before it will
    // answer. In practice the display bring-up has already taken longer than
    // this, so the wait is almost always zero.
    while (millis() < kKeyboardReadyMs) delay(10);
    applyKeyboardBacklight();

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

    drainBall();

    // A tap selects; a hold is the way back to the launcher, since the T-Deck
    // keyboard has no escape key and not every screen has room for a button.
    static uint32_t clickStartedAt = 0;
    static bool     holdFired      = false;
    constexpr uint32_t kHoldMs = 600;

    // An edge means the button moved. Sample the level right after to decide
    // whether that edge was a press or a release.
    static uint32_t seenEdges = 0;
    noInterrupts();
    const uint32_t edges = s_clickEdges;
    interrupts();

    if (edges != seenEdges) {
        seenEdges = edges;
        const bool down = digitalRead(BOARD_TRACKBALL_CLICK) == LOW;
        LOG_I(TAG, "trackball button %s", down ? "down" : "up");

        if (down) {
            clickStartedAt = now;
            holdFired      = false;
        } else if (!holdFired) {
            s_keys.push(LV_KEY_ENTER);
        }
        noteActivity();
        lastClick = down;
    }

    // A hold never produces a second edge, so it has to be timed from the
    // press rather than waited for.
    if (lastClick && !holdFired && now - clickStartedAt >= kHoldMs) {
        holdFired = true;
        noteActivity();
        LOG_I(TAG, "trackball held (handler %s)", s_holdHandler ? "set" : "MISSING");
        auto handler = s_holdHandler;   // same reason as the key hook
        if (handler) handler();
    }
}

lv_group_t* group()   { return s_group; }

void applySettings() {
    const int32_t vertical   = settings::getInt("ball_vstep");
    const int32_t horizontal = settings::getInt("ball_hstep");

    // Never zero: emitAxis divides by the step.
    s_ballVerticalStep   = vertical   > 0 ? static_cast<uint8_t>(vertical)   : 1;
    s_ballHorizontalStep = horizontal > 0 ? static_cast<uint8_t>(horizontal) : 1;
}

void setKeyboardBacklight(uint8_t brightness) {
    writeKeyboard(kKbBrightnessCmd, brightness);
}

void applyKeyboardBacklight() {
    const bool    on    = settings::getBool("kb_light");
    const uint8_t level = on ? static_cast<uint8_t>(settings::getInt("kb_bright")) : 0;

    setKeyboardBacklight(level);

    // Keep ALT+B working: tell the keyboard to toggle back to the brightness
    // chosen here rather than to its own default. Its range for this command
    // starts at 30, so a lower setting is clamped up for the toggle only - the
    // backlight itself is still whatever was asked for.
    if (on) writeKeyboard(kKbAltBBrightnessCmd, level < 30 ? 30 : level);

    LOG_I(TAG, "keyboard backlight %s (level %u)", on ? "on" : "off", level);
}

void setKeyHook(KeyHook hook) { s_hook = std::move(hook); }
void setHoldHandler(std::function<void()> handler) { s_holdHandler = std::move(handler); }
void clearKeyHook()           { s_hook = nullptr; }

uint32_t lastActivity() { return s_lastActivity; }
void     noteActivity() { s_lastActivity = millis(); }


} // namespace input
