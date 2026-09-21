#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <deque>
#include <vector>

#include "ui/TextFormat.h"

// A character-cell view over a scrollback of raw IRC lines.
//
// Lines are kept as the bytes that arrived, control codes and all, and are
// expanded into cells only for the rows currently on screen. That keeps the
// scrollback small enough to sit in PSRAM and means a change of font, width or
// colour setting just redraws - nothing has to be re-wrapped up front.

struct TermLine {
    String   text;        // raw, still carrying mIRC/ANSI control codes
    uint32_t stamp;       // unix seconds, 0 when unknown
    uint8_t  kind;        // app-defined (message, join, notice, ...)
    bool     highlight;   // draw the attention marker in the gutter

    // Cached wrap result, valid while `generation` matches the document's.
    uint16_t rows;
    uint16_t generation;
};

// The model. One per IRC window, so switching windows keeps both the history
// and the reading position.
class TermDoc {
public:
    void append(const String& text, uint32_t stamp, uint8_t kind, bool highlight);
    void clear();

    void setMaxLines(uint16_t lines);
    uint16_t maxLines() const { return m_maxLines; }

    size_t    size() const       { return m_lines.size(); }
    TermLine& at(size_t index)   { return m_lines[index]; }
    const TermLine& at(size_t index) const { return m_lines[index]; }

    // Bumped whenever cached wrap results stop being valid.
    uint16_t generation() const { return m_generation; }
    void     invalidate();

    // Owned by the document so it survives window switches.
    int32_t  scrollBack     = 0;
    bool     stickToBottom  = true;
    uint16_t unread         = 0;
    bool     unreadHighlight = false;

private:
    std::deque<TermLine> m_lines;
    uint16_t m_maxLines   = 1000;
    uint16_t m_generation = 1;
};

class TermView {
public:
    // Creates the widget under `parent`. The view fills its parent.
    void create(lv_obj_t* parent);

    // Switching documents redraws; nothing is copied.
    void setDocument(TermDoc* doc);
    TermDoc* document() const { return m_doc; }

    void setFont(const lv_font_t* font);
    void setLineSpacing(int8_t extraPixels);
    void setTimestampMode(uint8_t mode);        // 0 none, 1 HH:MM, 2 HH:MM:SS
    void setFormatOptions(const FormatOptions& options);
    void setWordWrap(bool enabled);

    // Scrolling is in display rows, not logical lines. Positive scrolls back
    // towards older text.
    void scrollRows(int32_t delta);
    void scrollToBottom();
    void scrollPageUp();
    void scrollPageDown();
    bool atBottom() const;

    void refresh();                              // re-wrap and redraw

    uint16_t columns() const { return m_columns; }
    uint16_t visibleRows() const { return m_visibleRows; }
    const lv_font_t* font() const { return m_font; }

    lv_obj_t* object() const { return m_obj; }

private:
    static void drawEventCb(lv_event_t* event);
    static void geometryEventCb(lv_event_t* event);

    void recomputeGeometry();
    uint16_t rowsFor(TermLine& line);
    void draw(lv_event_t* event);
    uint8_t timestampWidth() const;
    void buildCells(const TermLine& line, std::vector<TermCell>& out) const;

    // Collects the (line, row-within-line) pairs the viewport needs, newest
    // first, clamping the scroll position if the buffer is shorter than it.
    struct RowRef { uint16_t line; uint16_t row; };
    void collectVisibleRows(std::vector<RowRef>& out);

    lv_obj_t*        m_obj  = nullptr;
    const lv_font_t* m_font = nullptr;
    TermDoc*         m_doc  = nullptr;

    // Geometry, all in pixels except columns/rows.
    int32_t  m_cellWidth   = 6;
    int32_t  m_cellHeight  = 14;
    int8_t   m_lineSpacing = 0;
    uint16_t m_columns     = 40;
    uint16_t m_visibleRows = 12;

    uint8_t       m_timestampMode = 1;
    bool          m_wordWrap      = true;
    FormatOptions m_format;

    // Scratch buffers reused every frame so drawing does not allocate.
    mutable std::vector<TermCell>         m_cellScratch;
    mutable std::vector<textfmt::RowSpan> m_rowScratch;
};
