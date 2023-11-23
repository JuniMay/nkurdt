#include "logging.hpp"

namespace nkurdt {

// A global logger instance.
Logger global_logger(std::cout, "@@@@@@@@@@");

std::mutex Logger::mutex_;
bool Logger::enable_debug_ = false;

void Logger::log(LogLevel level, std::string msg) {
  if (level == LogLevel::Debug && !enable_debug_) {
    return;
  }

  std::lock_guard<std::mutex> lock(this->mutex_);

  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();

  switch (level) {
    case LogLevel::Debug:
        out_ << GREY << std::format("[ {} {:10} DEBUG ] {}", now, name_, msg)
             << RESET << std::endl;
      break;
    case LogLevel::Info:
      out_ << GREEN << std::format("[ {} {:10} INFO  ] {}", now, name_, msg)
           << RESET << std::endl;
      break;
    case LogLevel::Warn:
      out_ << YELLOW << std::format("[ {} {:10} WARN  ] {}", now, name_, msg)
           << RESET << std::endl;
      break;
    case LogLevel::Error:
      out_ << RED << std::format("[ {} {:10} ERROR ] {}", now, name_, msg)
           << RESET << std::endl;
      break;
    default:
      out_ << RESET << std::format("[ {} {:10}       ] {}", now, name_, msg)
           << RESET << std::endl;
      break;
  }
}

}  // namespace nkurdt
