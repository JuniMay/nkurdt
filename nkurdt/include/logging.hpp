#ifndef NKURDT_LOGGING_HPP_
#define NKURDT_LOGGING_HPP_

#include <chrono>
#include <format>
#include <iostream>

namespace nkurdt {

enum class LogLevel {
  Debug,
  Info,
  Warn,
  Error,
};

#define GREY "\033[1;30m"
#define GREEN "\033[1;32m"
#define YELLOW "\033[1;33m"
#define RED "\033[1;31m"

#define RESET "\033[0m"

/// A simple logger class.
class Logger {
 private:
  std::ostream& out_;
  std::string name_;

  static bool enable_debug_;

  static std::mutex mutex_;

 public:
  Logger(std::ostream& out, const std::string& name) : out_(out), name_(name) {}

  void log(LogLevel level, std::string msg);

  void warn(std::string msg) { log(LogLevel::Warn, msg); }

  void info(std::string msg) { log(LogLevel::Info, msg); }

  void error(std::string msg) { log(LogLevel::Error, msg); }

  void debug(std::string msg) { log(LogLevel::Debug, msg); }

  static void enable_debug(bool enable) { enable_debug_ = enable; }
};

extern Logger global_logger;

}  // namespace nkurdt

#endif  // NKURDT_LOGGING_HPP_
