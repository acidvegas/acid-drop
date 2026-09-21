#include "apps/TouchCalibApp.h"

#include "board/Input.h"
#include "board/Touch.h"
#include "board/pins.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace touchcalib {
namespace {

constexpr const char* TAG = "touchcal";

// Deliberately off-centre and away from both diagonals, so the four candidate
// rotations all put it somewhere clearly different and the nearest one is
// unambiguous.
constexpr int32_t kTargetX = 48;
constexpr int32_t kTargetY = 40;

lv_obj_t* s_page   = nullptr;
lv_obj_t* s_status = nullptr;
lv_obj_t* s_target = nullptr;
bool      s_done   = false;

bool keyHook(uint32_t key) {
    if (key != LV_KEY_ESC) return false;
    ui::back();
    return true;
}

} // namespace

void create(lv_obj_t* parent) {
    s_done = false;

    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_page, theme::background(), 0);
    lv_obj_set_style_bg_opa(s_page, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(s_page, false);

    lv_obj_t* title = lv_label_create(s_page);
    lv_label_set_text(title, "Touch calibration");
    lv_obj_set_style_text_font(title, theme::uiFont(), 0);
    lv_obj_set_style_text_color(title, theme::accent(), 0);
    lv_obj_align(title, LV_ALIGN_BOTTOM_LEFT, 8, -44);

    s_status = lv_label_create(s_page);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status, LV_PCT(92));
    lv_obj_set_style_text_font(s_status, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_status, theme::textDim(), 0);
    lv_label_set_text(s_status, "Tap the green circle. The correct orientation is "
                                "worked out from where the panel says you touched.");
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_LEFT, 8, -8);

    s_target = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_target);
    lv_obj_set_size(s_target, 26, 26);
    lv_obj_set_style_radius(s_target, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_target, theme::accent(), 0);
    lv_obj_set_style_bg_opa(s_target, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_target, kTargetX - 13, kTargetY - 13);

    input::setKeyHook(keyHook);
    LOG_I(TAG, "waiting for a tap at %ld,%ld", (long)kTargetX, (long)kTargetY);
}

void destroy() {
    input::clearKeyHook();
    s_page   = nullptr;
    s_status = nullptr;
    s_target = nullptr;
}

void tick() {
    if (!s_page || s_done) return;

    uint16_t rawX = 0;
    uint16_t rawY = 0;
    if (!touch::readRaw(rawX, rawY)) return;

    // Score every candidate against where the target actually is and take the
    // closest. No assumption about which rotation the panel uses.
    uint8_t  best         = 0;
    int32_t  bestDistance = INT32_MAX;
    int16_t  bestX        = 0;
    int16_t  bestY        = 0;

    for (uint8_t candidate = 0; candidate < touch::kMappingCount; candidate++) {
        int16_t x = 0;
        int16_t y = 0;
        touch::applyMapping(candidate, rawX, rawY, x, y);

        const int32_t dx = x - kTargetX;
        const int32_t dy = y - kTargetY;
        const int32_t distance = dx * dx + dy * dy;

        LOG_I(TAG, "mapping %u -> %d,%d (distance %ld)", candidate, x, y, (long)distance);

        if (distance < bestDistance) {
            bestDistance = distance;
            best         = candidate;
            bestX        = x;
            bestY        = y;
        }
    }

    s_done = true;
    settings::setEnum("touch_map", best);

    LOG_I(TAG, "raw %u,%u picked mapping %u (%d,%d)", rawX, rawY, best, bestX, bestY);

    static const char* const kNames[] = {"A", "B", "C", "D"};
    lv_label_set_text_fmt(s_status,
                          "Using mapping %s.\nRaw %u,%u mapped to %d,%d, target was %ld,%ld.\n"
                          "Taps should land correctly now. Hold the trackball for home.",
                          kNames[best], rawX, rawY, bestX, bestY,
                          (long)kTargetX, (long)kTargetY);
    lv_obj_set_style_text_color(s_status, theme::accent(), 0);
    lv_obj_set_style_bg_color(s_target, lv_color_hex(theme::kTextFaint), 0);
}

} // namespace touchcalib
