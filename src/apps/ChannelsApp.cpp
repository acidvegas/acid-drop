#include "apps/ChannelsApp.h"

#include "board/Input.h"
#include "irc/ChannelList.h"
#include "irc/IrcClient.h"
#include "ui/Theme.h"
#include "ui/Ui.h"

namespace channelsapp {
namespace {

lv_obj_t* s_page   = nullptr;
lv_obj_t* s_list   = nullptr;
lv_obj_t* s_editor = nullptr;

// Widgets inside the open editor, and which entry it is editing.
lv_obj_t* s_nameField     = nullptr;
lv_obj_t* s_keyField      = nullptr;
lv_obj_t* s_autojoinSwitch = nullptr;
lv_obj_t* s_retrySwitch   = nullptr;
size_t    s_editingIndex  = SIZE_MAX;   // SIZE_MAX means "adding a new one"

void rebuild();

void closeEditor() {
    if (s_editor) {
        lv_obj_delete(s_editor);
        s_editor = nullptr;
    }
    s_nameField = s_keyField = s_autojoinSwitch = s_retrySwitch = nullptr;
    s_editingIndex = SIZE_MAX;
}

void saveEditor() {
    String name(lv_textarea_get_text(s_nameField));
    name.trim();
    if (name.isEmpty()) {
        ui::toast("Channel name cannot be empty");
        return;
    }
    if (!irc::isChannel(name)) name = "#" + name;

    IrcChannelConfig channel;
    channel.name     = name;
    channel.key      = String(lv_textarea_get_text(s_keyField));
    channel.autojoin = lv_obj_has_state(s_autojoinSwitch, LV_STATE_CHECKED);
    channel.retry    = lv_obj_has_state(s_retrySwitch, LV_STATE_CHECKED);

    if (s_editingIndex == SIZE_MAX) channels::add(channel);
    else                            channels::update(s_editingIndex, channel);

    closeEditor();
    rebuild();
}

lv_obj_t* addSwitchRow(lv_obj_t* parent, const char* label, const char* help, bool on) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_scrollable(row, false);

    lv_obj_t* title = lv_label_create(row);
    lv_label_set_text(title, label);
    lv_obj_set_style_text_font(title, theme::uiFont(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* toggle = lv_switch_create(row);
    lv_obj_set_size(toggle, 40, 22);
    lv_obj_align(toggle, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(toggle, theme::accent(), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(toggle, LV_STATE_CHECKED);
    lv_group_add_obj(input::group(), toggle);

    if (help) {
        lv_obj_t* hint = lv_label_create(row);
        lv_label_set_text(hint, help);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(hint, LV_PCT(80));
        lv_obj_set_style_text_font(hint, theme::uiFontSmall(), 0);
        lv_obj_set_style_text_color(hint, theme::textDim(), 0);
        lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 20);
    }

    return toggle;
}

void openEditor(size_t index) {
    closeEditor();
    s_editingIndex = index;

    const bool adding = index >= channels::count();
    IrcChannelConfig existing;
    if (!adding) existing = channels::all()[index];

    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_clickable(overlay, true);
    s_editor = overlay;

    lv_obj_t* card = lv_obj_create(overlay);
    theme::stylePanel(card);
    lv_obj_set_width(card, LV_PCT(92));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);

    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, adding ? "Add channel" : "Edit channel");
    lv_obj_set_style_text_color(title, theme::accent(), 0);
    lv_obj_set_style_text_font(title, theme::uiFont(), 0);

    s_nameField = lv_textarea_create(card);
    lv_textarea_set_one_line(s_nameField, true);
    lv_textarea_set_placeholder_text(s_nameField, "#channel");
    lv_textarea_set_text(s_nameField, existing.name.c_str());
    lv_obj_set_width(s_nameField, LV_PCT(100));
    lv_group_add_obj(input::group(), s_nameField);

    s_keyField = lv_textarea_create(card);
    lv_textarea_set_one_line(s_keyField, true);
    lv_textarea_set_placeholder_text(s_keyField, "key (for +k channels)");
    lv_textarea_set_text(s_keyField, existing.key.c_str());
    lv_obj_set_width(s_keyField, LV_PCT(100));
    lv_group_add_obj(input::group(), s_keyField);

    s_autojoinSwitch = addSwitchRow(card, "Auto-join",
                                    "Join this channel after connecting",
                                    existing.autojoin);
    s_retrySwitch = addSwitchRow(card, "Keep retrying",
                                 "Retry through +i, +k, +b and full, and rejoin after a kick",
                                 existing.retry);

    lv_obj_t* buttons = lv_obj_create(card);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_PCT(100), 34);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, 6, 0);
    lv_obj_set_scrollable(buttons, false);

    if (!adding) {
        lv_obj_t* remove = lv_button_create(buttons);
        lv_obj_set_style_bg_color(remove, lv_color_hex(0x5A1828), 0);
        lv_obj_t* removeLabel = lv_label_create(remove);
        lv_label_set_text(removeLabel, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(removeLabel, lv_color_hex(theme::kDanger), 0);
        lv_obj_center(removeLabel);
        lv_group_add_obj(input::group(), remove);
        lv_obj_add_event_cb(remove, [](lv_event_t*) {
            channels::remove(s_editingIndex);
            closeEditor();
            rebuild();
        }, LV_EVENT_CLICKED, nullptr);
    }

    lv_obj_t* cancel = lv_button_create(buttons);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(theme::kSurfaceAlt), 0);
    lv_obj_t* cancelLabel = lv_label_create(cancel);
    lv_label_set_text(cancelLabel, "Cancel");
    lv_obj_center(cancelLabel);
    lv_group_add_obj(input::group(), cancel);
    lv_obj_add_event_cb(cancel, [](lv_event_t*) { closeEditor(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* save = lv_button_create(buttons);
    lv_obj_set_style_bg_color(save, theme::accent(), 0);
    lv_obj_t* saveLabel = lv_label_create(save);
    lv_label_set_text(saveLabel, "Save");
    lv_obj_set_style_text_color(saveLabel, lv_color_hex(theme::kBackground), 0);
    lv_obj_center(saveLabel);
    lv_group_add_obj(input::group(), save);
    lv_obj_add_event_cb(save, [](lv_event_t*) { saveEditor(); }, LV_EVENT_CLICKED, nullptr);

    lv_group_focus_obj(s_nameField);
}

void rowEventCb(lv_event_t* event) {
    openEditor(reinterpret_cast<size_t>(lv_event_get_user_data(event)));
}

void addBadge(lv_obj_t* parent, const char* text, lv_color_t colour) {
    lv_obj_t* badge = lv_label_create(parent);
    lv_label_set_text(badge, text);
    lv_obj_set_style_text_font(badge, theme::uiFontSmall(), 0);
    lv_obj_set_style_text_color(badge, colour, 0);
}

void rebuild() {
    if (!s_list) return;
    lv_obj_clean(s_list);

    // Add button first, so it is reachable without scrolling past the list.
    lv_obj_t* add = lv_obj_create(s_list);
    lv_obj_remove_style_all(add);
    lv_obj_set_size(add, LV_PCT(100), LV_SIZE_CONTENT);
    theme::styleRow(add);
    lv_obj_set_style_bg_color(add, lv_color_hex(theme::kAccentDim), 0);
    lv_obj_set_clickable(add, true);
    lv_obj_set_scrollable(add, false);
    lv_group_add_obj(input::group(), add);

    lv_obj_t* addLabel = lv_label_create(add);
    lv_label_set_text(addLabel, LV_SYMBOL_PLUS "  Add channel");
    lv_obj_align(addLabel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(add, [](lv_event_t*) { openEditor(SIZE_MAX); }, LV_EVENT_CLICKED, nullptr);

    IrcClient& client = ui::irc();

    for (size_t i = 0; i < channels::count(); i++) {
        const IrcChannelConfig& channel = channels::all()[i];

        lv_obj_t* row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        theme::styleRow(row);
        lv_obj_set_clickable(row, true);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(row, 2, 0);
        lv_group_add_obj(input::group(), row);

        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text(name, channel.name.c_str());
        lv_obj_set_style_text_font(name, theme::uiFont(), 0);

        lv_obj_t* badges = lv_obj_create(row);
        lv_obj_remove_style_all(badges);
        lv_obj_set_size(badges, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(badges, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(badges, 8, 0);
        lv_obj_set_scrollable(badges, false);

        addBadge(badges, channel.autojoin ? "auto-join" : "manual",
                 channel.autojoin ? theme::accent() : theme::textDim());
        if (channel.retry)          addBadge(badges, "retry", theme::textDim());
        if (!channel.key.isEmpty()) addBadge(badges, LV_SYMBOL_EYE_CLOSE " key", theme::textDim());

        // Live state, so the list doubles as a view of what is actually joined.
        if (IrcBuffer* buffer = client.findBuffer(channel.name)) {
            if (buffer->joined) {
                addBadge(badges, "joined", theme::accent());
            } else if (buffer->retryAt != 0) {
                const String state = buffer->retryReason.isEmpty()
                                     ? String("joining")
                                     : "retrying: " + buffer->retryReason;
                addBadge(badges, state.c_str(), lv_color_hex(theme::kWarning));
            }
        }

        lv_obj_add_event_cb(row, rowEventCb, LV_EVENT_CLICKED, reinterpret_cast<void*>(i));
    }

    if (channels::count() == 0) {
        lv_obj_t* empty = lv_label_create(s_list);
        lv_label_set_text(empty, "No saved channels.\nAdd one, or /join and it is saved for you.");
        lv_obj_set_style_text_color(empty, theme::textDim(), 0);
        lv_obj_set_style_text_font(empty, theme::uiFontSmall(), 0);
    }
}

bool keyHook(uint32_t key) {
    if (key != LV_KEY_ESC) return false;
    ui::back();
    return true;
}

} // namespace

void create(lv_obj_t* parent) {
    s_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_page);
    lv_obj_set_size(s_page, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(s_page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_page, 6, 0);
    lv_obj_set_style_pad_row(s_page, 6, 0);
    lv_obj_set_scrollable(s_page, false);

    ui::createAppHeader(s_page, "Channels");

    s_list = lv_obj_create(s_page);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 5, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    input::setKeyHook(keyHook);
    rebuild();
}

void destroy() {
    closeEditor();
    input::clearKeyHook();
    s_page = nullptr;
    s_list = nullptr;
}

void tick() {}

bool handleBack() {
    if (s_editor) { closeEditor(); return true; }
    return false;
}

} // namespace channelsapp
