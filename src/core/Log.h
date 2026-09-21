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
LogLevel level();

void write(LogLevel level, const char* tag, const char* fmt, ...);

// Newest last. Capped at kMaxEntries.
const std::vector<LogEntry>& entries();
void clear();

// Called whenever a line is appended, so the syslog app can refresh.
void onAppend(void (*cb)(const LogEntry&));

} // namespace logging

#define LOG_E(tag, ...) logging::write(LogLevel::Error, tag, __VA_ARGS__)
#define LOG_W(tag, ...) logging::write(LogLevel::Warn,  tag, __VA_ARGS__)
#define LOG_I(tag, ...) logging::write(LogLevel::Info,  tag, __VA_ARGS__)
#define LOG_D(tag, ...) logging::write(LogLevel::Debug, tag, __VA_ARGS__)
