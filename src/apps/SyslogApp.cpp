#include "apps/SyslogApp.h"

#include "board/Input.h"
#include "core/Log.h"
#include "ui/TermView.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace syslogapp {
namespace {

lv_obj_t* s_page = nullptr;
TermDoc   s_doc;
TermView  s_view;
size_t    s_shown = 0;

const char* colorFor(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "\x03" "04";
        case LogLevel::Warn:  return "\x03" "07";
        case LogLevel::Info:  return "\x03" "09";
        case LogLevel::Debug: return "\x03" "14";
    }
    return "";
}

void appendEntry(const LogEntry& entry) {
    String line = String(colorFor(entry.level)) + "[" + entry.tag + "]\x0F " + entry.message;
    s_doc.append(line, entry.ms / 1000, 0, entry.level == LogLevel::Error);
}

bool keyHook(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:   s_view.scrollRows(1);  return true;
        case LV_KEY_DOWN: s_view.scrollRows(-1); return true;
        case LV_KEY_ESC:  ui::back();            return true;
        default:          return false;
    }
}

} // namespace

void create(lv_obj_t* parent) {
    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(s_page, 2, 0);
    lv_obj_set_scrollable(s_page, false);

    s_doc.clear();
    s_doc.setMaxLines(300);
    s_shown = 0;

    FormatOptions options;
    options.defaultFg = theme::text();
    options.defaultBg = lv_color_hex(theme::kBackground);

    s_view.create(s_page);
    s_view.setFont(&acid_mono_10);
    s_view.setTimestampMode(0);
    s_view.setFormatOptions(options);
    s_view.setDocument(&s_doc);

    input::setKeyHook(keyHook);
    tick();
}

void destroy() {
    input::clearKeyHook();
    s_view.setDocument(nullptr);
    s_page = nullptr;
}

void tick() {
    if (!s_page) return;

    const auto& entries = logging::entries();

    // The ring buffer drops from the front, so a shrunk index means it wrapped.
    if (entries.size() < s_shown) s_shown = 0;
    if (entries.size() == s_shown) return;

    for (size_t i = s_shown; i < entries.size(); i++) appendEntry(entries[i]);
    s_shown = entries.size();

    if (s_view.atBottom()) s_view.scrollToBottom();
    lv_obj_invalidate(s_view.object());
}

} // namespace syslogapp
