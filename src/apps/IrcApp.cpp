#include "apps/IrcApp.h"

#include <vector>

#include "board/Input.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "ui/StatusBar.h"
#include "ui/TermView.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace ircapp {
namespace {

constexpr const char* TAG = "ircapp";

lv_obj_t* s_page   = nullptr;
lv_obj_t* s_tabs   = nullptr;
lv_obj_t* s_input  = nullptr;
lv_obj_t* s_topic  = nullptr;
TermView  s_view;

size_t s_activeBuffer = 0;
bool   s_alive        = false;

std::vector<lv_obj_t*> s_tabButtons;

// --- helpers --------------------------------------------------------------

IrcBuffer& activeBuffer() {
    IrcClient& client = ui::irc();
    if (s_activeBuffer >= client.bufferCount()) s_activeBuffer = 0;
    return client.buffer(s_activeBuffer);
}

FormatOptions currentFormatOptions() {
    FormatOptions options;
    options.mircColors = settings::getBool("irc_colors");
    options.background = settings::getBool("irc_bgcolor");
    options.ansi       = settings::getBool("irc_ansi");
    options.attributes = settings::getBool("irc_format");
    options.defaultFg  = theme::text();
    options.defaultBg  = lv_color_hex(theme::kBackground);
    return options;
}

void updateTitle() {
    IrcClient& client = ui::irc();
    IrcBuffer& buffer = activeBuffer();

    String title = buffer.isStatus() ? String("status") : buffer.name;
    if (buffer.isChannel() && !buffer.joined) {
        title += buffer.retryAt != 0 ? " (rejoining)" : " (parted)";
    }
    statusbar::setTitle(title + "  " + client.stateText());

    if (!s_topic) return;

    String subtitle;
    if (buffer.isChannel()) {
        if (!buffer.joined && !buffer.retryReason.isEmpty()) {
            subtitle = "Cannot join: " + buffer.retryReason + " - retrying";
        } else if (!buffer.topic.isEmpty()) {
            subtitle = buffer.topic;
        } else {
            subtitle = String(buffer.nicks.size()) + " users";
        }
    } else if (buffer.isStatus()) {
        subtitle = client.stateText();
    } else {
        subtitle = "private message";
    }

    lv_label_set_text(s_topic, subtitle.c_str());
}

// The status-bar dot means "something wants you somewhere", so it clears only
// once no window is still holding a highlight.
void refreshNotificationDot() {
    IrcClient& client = ui::irc();
    for (size_t i = 0; i < client.bufferCount(); i++) {
        if (client.buffer(i).doc.unreadHighlight) {
            statusbar::setNotification(true);
            return;
        }
    }
    statusbar::setNotification(false);
}

void selectBuffer(size_t index) {
    IrcClient& client = ui::irc();
    if (index >= client.bufferCount()) return;

    s_activeBuffer = index;
    IrcBuffer& buffer = client.buffer(index);

    buffer.doc.unread          = 0;
    buffer.doc.unreadHighlight = false;
    refreshNotificationDot();

    s_view.setDocument(&buffer.doc);
    onBufferListChanged();
    updateTitle();
}

void tabEventCb(lv_event_t* event) {
    const size_t index = reinterpret_cast<size_t>(lv_event_get_user_data(event));
    selectBuffer(index);
}

// --- commands -------------------------------------------------------------

void showHelp() {
    IrcBuffer& buffer = activeBuffer();
    static const char* kHelp[] = {
        "Commands:",
        "  /join #chan [key]   /part [#chan]   /close",
        "  /msg nick text      /query nick     /me action",
        "  /nick name          /topic [text]   /names",
        "  /connect            /disconnect     /quit [msg]",
        "  /raw line           /clear          /settings",
        "  /0 .. /9            switch window",
        "Keys: up/down scroll, left/right change window, esc exits",
    };
    for (const char* line : kHelp) {
        buffer.doc.append(line, static_cast<uint32_t>(time(nullptr)), LINE_LOCAL, false);
    }
    s_view.scrollToBottom();
    lv_obj_invalidate(s_view.object());
}

// Returns true when the text was a command (handled or rejected).
bool runCommand(const String& raw) {
    if (!raw.startsWith("/")) return false;

    IrcClient& client = ui::irc();
    IrcBuffer& buffer = activeBuffer();

    const int    space   = raw.indexOf(' ');
    String       verb    = space < 0 ? raw.substring(1) : raw.substring(1, space);
    const String rest    = space < 0 ? String() : raw.substring(space + 1);
    verb.toLowerCase();

    // /0 .. /9 jump straight to a window.
    if (verb.length() <= 2 && verb.length() > 0 && isdigit(static_cast<unsigned char>(verb[0]))) {
        selectBuffer(verb.toInt());
        return true;
    }

    if (verb == "join" && !rest.isEmpty()) {
        const int keySpace = rest.indexOf(' ');
        const String channel = keySpace < 0 ? rest : rest.substring(0, keySpace);
        const String key     = keySpace < 0 ? String() : rest.substring(keySpace + 1);
        client.join(channel, key);
        return true;
    }

    if (verb == "part") {
        const String target = rest.isEmpty() ? buffer.name : rest;
        if (irc::isChannel(target)) client.part(target);
        else ui::toast("Not a channel");
        return true;
    }

    if (verb == "close") {
        if (!client.closeBuffer(s_activeBuffer)) {
            ui::toast("Cannot close the status window");
        } else {
            selectBuffer(0);
        }
        return true;
    }

    if (verb == "msg" || verb == "privmsg") {
        const int textSpace = rest.indexOf(' ');
        if (textSpace < 0) { ui::toast("Usage: /msg target text"); return true; }
        client.say(rest.substring(0, textSpace), rest.substring(textSpace + 1));
        return true;
    }

    if (verb == "query") {
        if (rest.isEmpty()) { ui::toast("Usage: /query nick"); return true; }
        client.ensureBuffer(rest, BufferKind::Query);
        for (size_t i = 0; i < client.bufferCount(); i++) {
            if (irc::equalsIgnoreCaseIrc(client.buffer(i).name, rest)) { selectBuffer(i); break; }
        }
        return true;
    }

    if (verb == "me") {
        if (buffer.isStatus()) { ui::toast("No target here"); return true; }
        client.action(buffer.name, rest);
        return true;
    }

    if (verb == "nick")  { client.setNick(rest); return true; }
    if (verb == "topic") {
        if (!buffer.isChannel()) { ui::toast("Not a channel"); return true; }
        client.sendRaw(rest.isEmpty() ? "TOPIC " + buffer.name
                                      : "TOPIC " + buffer.name + " :" + rest);
        return true;
    }
    if (verb == "names") {
        if (buffer.isChannel()) client.sendRaw("NAMES " + buffer.name);
        return true;
    }

    if (verb == "connect")    { client.connect(); return true; }
    if (verb == "disconnect") { client.disconnect(settings::getText("irc_quitmsg"), true); return true; }
    if (verb == "quit")       { client.disconnect(rest.isEmpty() ? settings::getText("irc_quitmsg") : rest, true); return true; }
    if (verb == "raw")        { client.sendRaw(rest); return true; }
    if (verb == "clear")      { buffer.doc.clear(); lv_obj_invalidate(s_view.object()); return true; }
    if (verb == "settings")   { ui::openApp(ui::AppId::Settings); return true; }
    if (verb == "help")       { showHelp(); return true; }

    ui::toast("Unknown command: /" + verb);
    return true;
}

void submitInput() {
    const char* raw = lv_textarea_get_text(s_input);
    if (raw == nullptr || raw[0] == '\0') return;

    const String text(raw);
    lv_textarea_set_text(s_input, "");

    if (runCommand(text)) return;

    IrcBuffer& buffer = activeBuffer();
    if (buffer.isStatus()) {
        ui::toast("Pick a window, or use /join");
        return;
    }
    ui::irc().say(buffer.name, text);
}

void inputEventCb(lv_event_t* event) {
    if (lv_event_get_code(event) == LV_EVENT_READY) submitInput();
}

// Keys the app wants before the text area sees them.
bool keyHook(uint32_t key) {
    if (!s_alive) return false;

    switch (key) {
        case LV_KEY_UP:
            s_view.scrollRows(1);
            return true;
        case LV_KEY_DOWN:
            s_view.scrollRows(-1);
            return true;

        case LV_KEY_LEFT:
        case LV_KEY_RIGHT: {
            // Only steal the arrows when there is no text to move through.
            const char* text = lv_textarea_get_text(s_input);
            if (text && text[0] != '\0') return false;

            const size_t count = ui::irc().bufferCount();
            if (count == 0) return true;
            if (key == LV_KEY_LEFT) {
                selectBuffer(s_activeBuffer == 0 ? count - 1 : s_activeBuffer - 1);
            } else {
                selectBuffer((s_activeBuffer + 1) % count);
            }
            return true;
        }

        case LV_KEY_ESC:
            ui::back();
            return true;

        default:
            return false;
    }
}

} // namespace

// --- lifecycle ------------------------------------------------------------

void create(lv_obj_t* parent) {
    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_page, 0, 0);
    lv_obj_set_style_pad_row(s_page, 0, 0);
    lv_obj_set_scrollable(s_page, false);

    // Window tabs.
    s_tabs = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_tabs);
    lv_obj_set_size(s_tabs, LV_PCT(100), 22);
    lv_obj_set_flex_flow(s_tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_tabs, 3, 0);
    lv_obj_set_style_pad_hor(s_tabs, 4, 0);
    lv_obj_set_style_bg_color(s_tabs, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_bg_opa(s_tabs, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(s_tabs, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_tabs, LV_SCROLLBAR_MODE_OFF);

    // Topic / context line.
    s_topic = lv_label_create(s_page);
    lv_obj_set_width(s_topic, LV_PCT(100));
    lv_label_set_long_mode(s_topic, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_topic, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(s_topic, theme::textDim(), 0);
    lv_obj_set_style_bg_color(s_topic, lv_color_hex(theme::kSurfaceAlt), 0);
    lv_obj_set_style_bg_opa(s_topic, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_topic, 5, 0);
    lv_obj_set_style_pad_ver(s_topic, 2, 0);
    lv_label_set_text(s_topic, "");

    // The message grid.
    lv_obj_t* viewHost = lv_obj_create(s_page);
    lv_obj_remove_style_all(viewHost);
    lv_obj_set_width(viewHost, LV_PCT(100));
    lv_obj_set_flex_grow(viewHost, 1);
    lv_obj_set_style_pad_all(viewHost, 2, 0);
    lv_obj_set_scrollable(viewHost, false);

    s_view.create(viewHost);
    applySettings();

    // Input line.
    s_input = lv_textarea_create(s_page);
    lv_obj_set_size(s_input, LV_PCT(100), 30);
    lv_textarea_set_one_line(s_input, true);
    lv_textarea_set_placeholder_text(s_input, "message or /command");
    lv_textarea_set_max_length(s_input, 400);
    lv_obj_set_style_bg_color(s_input, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_border_color(s_input, lv_color_hex(theme::kBorder), 0);
    lv_obj_set_style_border_width(s_input, 1, 0);
    lv_obj_set_style_radius(s_input, 0, 0);
    lv_obj_set_style_text_font(s_input, theme::termFont(), 0);
    lv_obj_set_style_text_color(s_input, theme::text(), 0);
    lv_obj_add_event_cb(s_input, inputEventCb, LV_EVENT_READY, nullptr);

    lv_group_add_obj(input::group(), s_input);
    lv_group_focus_obj(s_input);

    s_alive = true;
    input::setKeyHook(keyHook);

    selectBuffer(s_activeBuffer);
    LOG_I(TAG, "IRC window ready");
}

void destroy() {
    s_alive = false;
    input::clearKeyHook();
    s_view.setDocument(nullptr);

    s_page  = nullptr;
    s_tabs  = nullptr;
    s_input = nullptr;
    s_topic = nullptr;
    s_tabButtons.clear();
}

void tick() {
    if (!s_alive) return;

    static uint32_t lastTitle = 0;
    const uint32_t  now       = millis();
    if (now - lastTitle > 1000) {
        lastTitle = now;
        updateTitle();
    }
}

void applySettings() {
    s_view.setFont(theme::termFont());
    s_view.setLineSpacing(settings::getInt("term_linesp"));
    s_view.setTimestampMode(settings::getEnum("irc_ts"));
    s_view.setFormatOptions(currentFormatOptions());

    if (s_input) lv_obj_set_style_text_font(s_input, theme::termFont(), 0);
}

// --- notifications from the client ---------------------------------------

void onBufferChanged(IrcBuffer& buffer) {
    if (!s_alive) return;

    if (&buffer.doc == s_view.document()) {
        if (s_view.atBottom()) s_view.scrollToBottom();
        lv_obj_invalidate(s_view.object());
        buffer.doc.unread          = 0;
        buffer.doc.unreadHighlight = false;
        refreshNotificationDot();
    } else {
        onBufferListChanged();   // refresh the unread badge
    }
}

void onBufferListChanged() {
    if (!s_alive || !s_tabs) return;

    lv_obj_clean(s_tabs);
    s_tabButtons.clear();

    IrcClient& client = ui::irc();
    for (size_t i = 0; i < client.bufferCount(); i++) {
        IrcBuffer& buffer = client.buffer(i);

        lv_obj_t* tab = lv_obj_create(s_tabs);
        lv_obj_remove_style_all(tab);
        lv_obj_set_size(tab, LV_SIZE_CONTENT, 18);
        lv_obj_set_style_pad_hor(tab, 6, 0);
        lv_obj_set_style_radius(tab, 4, 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
        lv_obj_set_scrollable(tab, false);
        lv_obj_set_clickable(tab, true);

        const bool active = i == s_activeBuffer;
        lv_obj_set_style_bg_color(tab,
                                  active ? theme::accent() : lv_color_hex(theme::kSurfaceAlt), 0);

        lv_obj_t* label = lv_label_create(tab);
        String text = String(i) + ":" + (buffer.isStatus() ? String("status") : buffer.name);
        if (!active && buffer.doc.unread > 0) text += " (" + String(buffer.doc.unread) + ")";

        lv_label_set_text(label, text.c_str());
        lv_obj_set_style_text_font(label, theme::uiFontSmall(), 0);

        lv_color_t colour = active ? lv_color_hex(theme::kBackground) : theme::textDim();
        if (!active && buffer.doc.unreadHighlight) colour = lv_color_hex(theme::kDanger);
        lv_obj_set_style_text_color(label, colour, 0);
        lv_obj_center(label);

        lv_obj_add_event_cb(tab, tabEventCb, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
        s_tabButtons.push_back(tab);
    }

    // Keep the active tab in view when there are more than fit.
    if (s_activeBuffer < s_tabButtons.size()) {
        lv_obj_scroll_to_view(s_tabButtons[s_activeBuffer], LV_ANIM_OFF);
    }
}

void onIrcStateChanged(IrcState state) {
    LV_UNUSED(state);
    if (s_alive) updateTitle();
}

} // namespace ircapp
