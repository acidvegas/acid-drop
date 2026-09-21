#pragma once

#include <Arduino.h>
#include <vector>

// Serial logging that also keeps a ring buffer, so the on-device syslog view can
// show the same lines without a USB cable attached.

enum class LogLevel : uint8_t {
    Error = 0,
    Warn  = 1,
    Info  = 2,
    Debug = 3,
};

struct LogEntry {
    uint32_t ms;
    LogLevel level;
    String   tag;
    String   message;
};

namespace logging {

void begin(unsigned long baud);
void setLevel(LogLevel level);

// Whether to keep the ring buffer the on-device syslog reads from.
void setKeepHistory(bool keep);
LogLevel level();

void write(LogLevel level, const char* tag, const char* fmt, ...);

// Newest last. Capped at kMaxEntries.
const std::vector<LogEntry>& entries();
void clear();

// Re-prints the whole ring buffer to serial.
//
// The S3 talks over USB-CDC, which throws output away whenever it thinks no
// terminal is attached - so everything logged during boot is lost unless a
// monitor happened to be connected at the time, and attaching one tends to
// reset the board. Calling this when a host turns up replays what was missed.
void replay();

// Called whenever a line is appended, so the syslog app can refresh.
void onAppend(void (*cb)(const LogEntry&));

} // namespace logging

#define LOG_E(tag, ...) logging::write(LogLevel::Error, tag, __VA_ARGS__)
#define LOG_W(tag, ...) logging::write(LogLevel::Warn,  tag, __VA_ARGS__)
#define LOG_I(tag, ...) logging::write(LogLevel::Info,  tag, __VA_ARGS__)
#define LOG_D(tag, ...) logging::write(LogLevel::Debug, tag, __VA_ARGS__)
