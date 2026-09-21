#include "core/Log.h"

#include <stdarg.h>

namespace logging {
namespace {

constexpr size_t kMaxEntries = 250;

LogLevel              s_level = LogLevel::Info;
std::vector<LogEntry> s_entries;
void                (*s_cb)(const LogEntry&) = nullptr;

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "E";
        case LogLevel::Warn:  return "W";
        case LogLevel::Info:  return "I";
        case LogLevel::Debug: return "D";
    }
    return "?";
}

} // namespace

void begin(unsigned long baud) {
    Serial.begin(baud);
    s_entries.reserve(kMaxEntries);
}

void setLevel(LogLevel level) { s_level = level; }
LogLevel level() { return s_level; }

void write(LogLevel level, const char* tag, const char* fmt, ...) {
    if (static_cast<uint8_t>(level) > static_cast<uint8_t>(s_level)) return;

    char    stackBuf[192];
    va_list args;
    va_start(args, fmt);
    const int needed = vsnprintf(stackBuf, sizeof(stackBuf), fmt, args);
    va_end(args);

    String message;
    if (needed >= static_cast<int>(sizeof(stackBuf))) {
        // Message was truncated; redo it into a heap buffer the right size.
        std::vector<char> heapBuf(needed + 1);
        va_start(args, fmt);
        vsnprintf(heapBuf.data(), heapBuf.size(), fmt, args);
        va_end(args);
        message = heapBuf.data();
    } else {
        message = stackBuf;
    }

    LogEntry entry{millis(), level, tag, message};

    Serial.printf("[%8lu][%s][%s] %s\n",
                  static_cast<unsigned long>(entry.ms), levelName(level), tag, message.c_str());

    if (s_entries.size() >= kMaxEntries) s_entries.erase(s_entries.begin());
    s_entries.push_back(entry);

    if (s_cb) s_cb(s_entries.back());
}

const std::vector<LogEntry>& entries() { return s_entries; }

void clear() { s_entries.clear(); }

void onAppend(void (*cb)(const LogEntry&)) { s_cb = cb; }

} // namespace logging
