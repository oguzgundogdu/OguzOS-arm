#pragma once

#include "types.h"

#ifndef SYSLOG_MIN_LEVEL
#define SYSLOG_MIN_LEVEL 0
#endif

namespace syslog {

enum class Level : u8 {
  DEBUG = 0,
  INFO = 1,
  WARN = 2,
  ERROR = 3,
};

// Initialize file logging (call after fs::init)
void init();

// Set/get runtime log level filter
void set_level(Level level);
Level get_level();

// Core log function: syslog::log(Level::INFO, "kbd", "key=%d", code);
void log(Level level, const char *subsystem, const char *fmt, ...);

// Convenience wrappers
void debug(const char *subsystem, const char *fmt, ...);
void info(const char *subsystem, const char *fmt, ...);
void warn(const char *subsystem, const char *fmt, ...);
void error(const char *subsystem, const char *fmt, ...);

} // namespace syslog
