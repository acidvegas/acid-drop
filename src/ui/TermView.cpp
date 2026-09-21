#include "ui/TermView.h"

#include <time.h>

#include "core/Log.h"

namespace {

// Space reserved down the left edge for the attention marker. Without it the
// marker overdraws the first character, and the first column sits hard against
// the screen edge where it gets clipped.
constexpr int32_t kGutter = 4;

constexpr lv_opa_t kShadeOpa[3] = {LV_OPA_20, LV_OPA_40, LV_OPA_70};  // U+2591..U+2593

// Block-element characters are drawn as rectangles rather than glyphs. A font's
// block glyphs are sized to its own em box, not to our cell, so drawing them as
// text leaves hairline seams between rows - which is exactly where ANSI art
// falls apart. Rectangles tile perfectly.
//
// Returns false for anything that should be drawn as a normal glyph.
bool drawBlockCell(lv_layer_t* layer, const lv_area_t& cell, const TermCell& source) {
    const uint32_t ch = source.ch;
    if (ch < 0x2580 || ch > 0x2595) return false;

    const int32_t w = cell.x2 - cell.x1 + 1;
    const int32_t h = cell.y2 - cell.y1 + 1;

    lv_draw_fill_dsc_t dsc;
    lv_draw_fill_dsc_init(&dsc);
    dsc.color = source.fg;
    dsc.opa   = LV_OPA_COVER;

    lv_area_t area = cell;

    switch (ch) {
        case 0x2580:                                   // upper half
            area.y2 = cell.y1 + h / 2 - 1;
            break;
        case 0x2588:                                   // full block
            break;
        case 0x2590:                                   // right half
            area.x1 = cell.x1 + w / 2;
            break;
        case 0x2594:                                   // upper one eighth
            area.y2 = cell.y1 + (h + 7) / 8 - 1;
            break;
        case 0x2595:                                   // right one eighth
            area.x1 = cell.x2 - (w + 7) / 8 + 1;
            break;

        case 0x2591: case 0x2592: case 0x2593:         // light/medium/dark shade
            dsc.opa = kShadeOpa[ch - 0x2591];
            break;

        default:
            if (ch >= 0x2581 && ch <= 0x2587) {        // lower N eighths
                const int32_t eighths = ch - 0x2580;
                area.y1 = cell.y2 - (h * eighths) / 8 + 1;
            } else if (ch >= 0x2589 && ch <= 0x258F) { // left N eighths
                const int32_t eighths = 8 - (ch - 0x2588);
                area.x2 = cell.x1 + (w * eighths) / 8 - 1;
            } else {
                return false;                          // quadrants: use the glyph
            }
            break;
    }

    if (area.x2 >= area.x1 && area.y2 >= area.y1) lv_draw_fill(layer, &dsc, &area);
    return true;
}

} // namespace

// --- TermDoc -------------------------------------------------------------

void TermDoc::append(const String& text, uint32_t stamp, uint8_t kind, bool highlight) {
    TermLine line;
    line.text       = text;
    line.stamp      = stamp;
    line.kind       = kind;
    line.highlight  = highlight;
    line.rows       = 0;
    line.generation = 0;   // forces a wrap the first time it is drawn

    m_lines.push_back(std::move(line));

    // Scrollback is String data on the internal heap, so the line cap alone is
    // not a memory bound: long lines across several windows can still exhaust
    // it. Below a floor, drop history hard rather than fail an allocation
    // somewhere less recoverable.
    constexpr uint32_t kHeapFloor = 45000;
    if (ESP.getFreeHeap() < kHeapFloor) {
        const size_t keep = m_lines.size() / 2 > 40 ? m_lines.size() / 2 : 40;
        while (m_lines.size() > keep) m_lines.pop_front();
        scrollBack = 0;
        stickToBottom = true;
    }

    while (m_lines.size() > m_maxLines) {
        // Dropping the oldest line shifts everything up by its height. If the
        // reader is up in the scrollback, pull the anchor down by the same
        // amount so the text under their eyes does not jump.
        if (!stickToBottom && m_lines.front().generation == m_generation) {
            const uint16_t dropped = m_lines.front().rows;
            scrollBack = scrollBack > dropped ? scrollBack - dropped : 0;
        }
        m_lines.pop_front();
    }

    if (stickToBottom) scrollBack = 0;
}

void TermDoc::clear() {
    m_lines.clear();
    scrollBack      = 0;
    stickToBottom   = true;
    unread          = 0;
    unreadHighlight = false;
}

void TermDoc::setMaxLines(uint16_t lines) {
    m_maxLines = lines < 50 ? 50 : lines;
    while (m_lines.size() > m_maxLines) m_lines.pop_front();
}

void TermDoc::invalidate() {
    m_generation++;
    if (m_generation == 0) m_generation = 1;
}

// --- lifecycle ------------------------------------------------------------

void TermView::create(lv_obj_t* parent) {
    m_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(m_obj);
    lv_obj_set_size(m_obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(m_obj, m_format.defaultBg, 0);
    lv_obj_set_style_bg_opa(m_obj, LV_OPA_COVER, 0);
    lv_obj_set_clickable(m_obj, true);
    lv_obj_set_scrollable(m_obj, false);

    lv_obj_set_user_data(m_obj, this);
    lv_obj_add_event_cb(m_obj, drawEventCb, LV_EVENT_DRAW_MAIN, this);
    lv_obj_add_event_cb(m_obj, geometryEventCb, LV_EVENT_SIZE_CHANGED, this);

    recomputeGeometry();
}

void TermView::drawEventCb(lv_event_t* event) {
    static_cast<TermView*>(lv_event_get_user_data(event))->draw(event);
}

void TermView::geometryEventCb(lv_event_t* event) {
    static_cast<TermView*>(lv_event_get_user_data(event))->recomputeGeometry();
}

void TermView::setDocument(TermDoc* doc) {
    if (m_doc == doc) return;
    m_doc = doc;
    if (m_doc) {
        m_doc->invalidate();          // geometry may differ from its last view
        m_doc->unread = 0;
        m_doc->unreadHighlight = false;
    }
    if (m_obj) lv_obj_invalidate(m_obj);
}

// --- configuration --------------------------------------------------------

void TermView::setFont(const lv_font_t* font) {
    if (m_font == font) return;
    m_font = font;
    recomputeGeometry();
}

void TermView::setLineSpacing(int8_t extraPixels) {
    if (m_lineSpacing == extraPixels) return;
    m_lineSpacing = extraPixels;
    recomputeGeometry();
}

void TermView::setTimestampMode(uint8_t mode) {
    if (m_timestampMode == mode) return;
    m_timestampMode = mode;
    refresh();
}

void TermView::setFormatOptions(const FormatOptions& options) {
    m_format = options;
    if (m_obj) lv_obj_set_style_bg_color(m_obj, m_format.defaultBg, 0);
    refresh();
}

void TermView::setWordWrap(bool enabled) {
    if (m_wordWrap == enabled) return;
    m_wordWrap = enabled;
    refresh();
}

void TermView::refresh() {
    if (m_doc) m_doc->invalidate();
    if (m_obj) lv_obj_invalidate(m_obj);
}

void TermView::recomputeGeometry() {
    if (!m_obj || !m_font) return;

    // Every glyph in these fonts has the same advance, so one lookup is enough.
    const int32_t advance = lv_font_get_glyph_width(m_font, 'M', 0);
    m_cellWidth  = advance > 0 ? advance : 6;
    m_cellHeight = m_font->line_height + m_lineSpacing;
    if (m_cellHeight < 4) m_cellHeight = 4;

    const int32_t width  = lv_obj_get_content_width(m_obj) - kGutter;
    const int32_t height = lv_obj_get_content_height(m_obj);

    m_columns     = width  > 0 ? width  / m_cellWidth  : 1;
    m_visibleRows = height > 0 ? height / m_cellHeight : 1;
    if (m_columns == 0)     m_columns = 1;
    if (m_visibleRows == 0) m_visibleRows = 1;

    refresh();
}

// --- scrolling ------------------------------------------------------------

void TermView::scrollRows(int32_t delta) {
    if (!m_doc) return;
    m_doc->scrollBack += delta;
    if (m_doc->scrollBack < 0) m_doc->scrollBack = 0;
    m_doc->stickToBottom = m_doc->scrollBack == 0;
    if (m_obj) lv_obj_invalidate(m_obj);   // clamping happens during the draw
}

void TermView::scrollToBottom() {
    if (!m_doc) return;
    m_doc->scrollBack    = 0;
    m_doc->stickToBottom = true;
    if (m_obj) lv_obj_invalidate(m_obj);
}

void TermView::scrollPageUp()   { scrollRows(m_visibleRows > 1 ? m_visibleRows - 1 : 1); }
void TermView::scrollPageDown() { scrollRows(-(m_visibleRows > 1 ? m_visibleRows - 1 : 1)); }

bool TermView::atBottom() const { return !m_doc || m_doc->stickToBottom; }

bool TermView::atTop() const {
    // collectVisibleRows() clamps scrollBack to the oldest row it can reach, so
    // asking again after a draw tells us whether we are pinned there.
    return !m_doc || m_doc->scrollBack == m_atTopScrollBack;
}

// --- wrapping -------------------------------------------------------------

uint8_t TermView::timestampWidth() const {
    switch (m_timestampMode) {
        case 1:  return 6;   // "HH:MM "
        case 2:  return 9;   // "HH:MM:SS "
        default: return 0;
    }
}

void TermView::buildCells(const TermLine& line, std::vector<TermCell>& out) const {
    textfmt::toCells(line.text, m_format, out);

    const uint8_t stampWidth = timestampWidth();
    if (stampWidth == 0 || line.stamp == 0) return;

    char text[12];
    const time_t when = static_cast<time_t>(line.stamp);
    struct tm parts;
    localtime_r(&when, &parts);
    if (m_timestampMode == 2) {
        snprintf(text, sizeof(text), "%02d:%02d:%02d ", parts.tm_hour, parts.tm_min, parts.tm_sec);
    } else {
        snprintf(text, sizeof(text), "%02d:%02d ", parts.tm_hour, parts.tm_min);
    }

    const lv_color_t dim = lv_color_hex(0x5A6068);
    std::vector<TermCell> stamped;
    stamped.reserve(out.size() + stampWidth);
    for (const char* p = text; *p; p++) {
        stamped.push_back({static_cast<uint32_t>(*p), dim, m_format.defaultBg, 0});
    }
    stamped.insert(stamped.end(), out.begin(), out.end());
    out.swap(stamped);
}

uint16_t TermView::rowsFor(TermLine& line) {
    if (line.generation == m_doc->generation()) return line.rows;

    buildCells(line, m_cellScratch);
    textfmt::wrap(m_cellScratch, m_columns, timestampWidth(), m_wordWrap, m_rowScratch);

    line.rows       = m_rowScratch.size();
    line.generation = m_doc->generation();
    return line.rows;
}

void TermView::collectVisibleRows(std::vector<RowRef>& out) {
    out.clear();
    if (!m_doc) return;

    const int32_t needed = m_doc->scrollBack + m_visibleRows;
    out.reserve(needed);

    for (int32_t index = static_cast<int32_t>(m_doc->size()) - 1;
         index >= 0 && static_cast<int32_t>(out.size()) < needed; index--) {
        const uint16_t rows = rowsFor(m_doc->at(index));
        for (int32_t row = rows - 1;
             row >= 0 && static_cast<int32_t>(out.size()) < needed; row--) {
            out.push_back({static_cast<uint16_t>(index), static_cast<uint16_t>(row)});
        }
    }

    // We ran out of buffer before satisfying the scroll position, so pin the
    // view to the oldest line we have.
    if (static_cast<int32_t>(out.size()) < needed) {
        const int32_t maxScroll = static_cast<int32_t>(out.size()) - m_visibleRows;
        m_doc->scrollBack    = maxScroll > 0 ? maxScroll : 0;
        m_doc->stickToBottom = m_doc->scrollBack == 0;
        m_atTopScrollBack    = m_doc->scrollBack;
    } else {
        m_atTopScrollBack    = -1;
    }
}

// --- drawing --------------------------------------------------------------

void TermView::draw(lv_event_t* event) {
    if (!m_font || !m_doc) return;

    lv_layer_t* layer = lv_event_get_layer(event);

    lv_area_t content;
    lv_obj_get_content_coords(m_obj, &content);

    std::vector<RowRef> visible;
    collectVisibleRows(visible);
    if (visible.empty()) return;

    // `visible` runs newest-first; walk it backwards to paint top to bottom.
    const int32_t last  = static_cast<int32_t>(visible.size()) - 1;
    int32_t       first = m_doc->scrollBack + m_visibleRows - 1;
    if (first > last) first = last;

    lv_draw_letter_dsc_t glyph;
    lv_draw_letter_dsc_init(&glyph);
    glyph.font    = m_font;
    glyph.opa     = LV_OPA_COVER;
    glyph.scale_x = LV_SCALE_NONE;
    glyph.scale_y = LV_SCALE_NONE;

    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);

    uint32_t cachedLine = UINT32_MAX;
    int32_t  screenRow  = 0;

    for (int32_t i = first; i >= m_doc->scrollBack && screenRow < m_visibleRows; i--, screenRow++) {
        const RowRef&   ref  = visible[i];
        const TermLine& line = m_doc->at(ref.line);

        if (cachedLine != ref.line) {
            buildCells(line, m_cellScratch);
            textfmt::wrap(m_cellScratch, m_columns, timestampWidth(), m_wordWrap, m_rowScratch);
            cachedLine = ref.line;
        }
        if (ref.row >= m_rowScratch.size()) continue;

        const textfmt::RowSpan& span = m_rowScratch[ref.row];
        const int32_t y = content.y1 + screenRow * m_cellHeight;

        // Backgrounds first, merging runs of the same colour into one fill so a
        // line of coloured spaces costs one rectangle instead of forty.
        int32_t runStart = -1;
        lv_color_t runColor = m_format.defaultBg;
        for (uint16_t k = span.start; k <= span.end; k++) {
            const bool inRun = k < span.end && (m_cellScratch[k].flags & CELL_HAS_BG);
            const lv_color_t color = inRun ? m_cellScratch[k].bg : m_format.defaultBg;

            const bool sameRun = runStart >= 0 && inRun &&
                                 lv_color_to_u32(color) == lv_color_to_u32(runColor);
            if (sameRun) continue;

            if (runStart >= 0) {
                const int32_t column = span.indent + (runStart - span.start);
                lv_area_t area;
                area.x1 = content.x1 + kGutter + column * m_cellWidth;
                area.y1 = y;
                area.x2 = content.x1 + kGutter + (span.indent + (k - span.start)) * m_cellWidth - 1;
                area.y2 = y + m_cellHeight - 1;
                fill.color = runColor;
                fill.opa   = LV_OPA_COVER;
                if (area.x2 >= area.x1) lv_draw_fill(layer, &fill, &area);
                runStart = -1;
            }

            if (inRun) {
                runStart = k;
                runColor = color;
            }
        }

        // Then the glyphs.
        for (uint16_t k = span.start; k < span.end; k++) {
            const TermCell& cell = m_cellScratch[k];
            if (cell.ch == ' ' || cell.ch == 0) continue;

            const int32_t column = span.indent + (k - span.start);
            lv_area_t box;
            box.x1 = content.x1 + kGutter + column * m_cellWidth;
            box.y1 = y;
            box.x2 = box.x1 + m_cellWidth - 1;
            box.y2 = y + m_cellHeight - 1;

            if (drawBlockCell(layer, box, cell)) continue;

            glyph.unicode = cell.ch;
            glyph.color   = cell.fg;
            glyph.skew_x  = (cell.flags & CELL_ITALIC) ? 12 : 0;
            glyph.decor   = (cell.flags & CELL_UNDERLINE) ? LV_TEXT_DECOR_UNDERLINE
                                                          : LV_TEXT_DECOR_NONE;
            if (cell.flags & CELL_STRIKE) {
                glyph.decor = static_cast<lv_text_decor_t>(glyph.decor | LV_TEXT_DECOR_STRIKETHROUGH);
            }

            lv_point_t at{box.x1, box.y1};
            lv_draw_letter(layer, &glyph, &at);

            // There is no bold face in the terminal font, so smear the glyph one
            // pixel to the right - the same trick a text-mode console uses.
            if (cell.flags & CELL_BOLD) {
                lv_point_t bolder{box.x1 + 1, box.y1};
                lv_draw_letter(layer, &glyph, &bolder);
            }
        }

        // Attention marker for lines that mention us.
        if (line.highlight && ref.row == 0) {
            lv_area_t marker;
            marker.x1 = content.x1;
            marker.y1 = y;
            marker.x2 = content.x1 + 1;   // inside the gutter, clear of the text
            marker.y2 = y + m_cellHeight - 1;
            fill.color = lv_color_hex(0xFF3B6E);
            fill.opa   = LV_OPA_COVER;
            lv_draw_fill(layer, &fill, &marker);
        }
    }
}
