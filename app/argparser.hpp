#ifndef NKURDT_APP_ARGPARSER_HPP_
#define NKURDT_APP_ARGPARSER_HPP_

#include <any>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class ArgumanetParser;

class Argument {
 private:
  friend class ArgumanetParser;

  /// Help text for the argument.
  std::string help_text_;
  /// The name of the argument.
  std::string name_;
  /// The default value of the argument.
  std::optional<std::any> default_value_;
  //// The value of the argument.
  std::optional<std::any> value_;
  /// The validator function.
  std::function<bool(const std::string&)> validator_;
  /// The callback function.
  std::function<void(const std::string&)> callback_;
  /// If the argument is required.
  bool required_;
  /// If the argument is a flag, i.e. it stores a bool value.
  bool is_flag_;

  /// The parser function
  std::function<std::any(const std::string&)> parser_;

 public:
  Argument() : required_(false), is_flag_(false) {}
  Argument& help_text(const std::string& text) {
    this->help_text_ = text;
    return *this;
  }
  Argument& name(const std::string& name) {
    this->name_ = name;
    return *this;
  }

  template <typename T>
  Argument& default_value(const T& value) {
    this->default_value_ = value;
    return *this;
  }

  template <typename T>
  Argument& scan() {
    parser_ = [](const std::string& value) -> std::any {
      if constexpr (std::is_same_v<T, std::string>) {
        return value;
      } else if constexpr (std::is_same_v<T, int>) {
        return std::stoi(value);
      } else if constexpr (std::is_same_v<T, size_t>) {
        std::stringstream ss(value);
        size_t result;
        ss >> result;
        return result;
      } else if constexpr (std::is_same_v<T, float>) {
        return std::stof(value);
      } else if constexpr (std::is_same_v<T, double>) {
        return std::stod(value);
      } else if constexpr (std::is_same_v<T, bool>) {
        return value == "true";
      } else {
        return std::any();
      }
    };
    return *this;
  }

  Argument& validator(const std::function<bool(const std::string&)>& validator
  ) {
    this->validator_ = validator;
    return *this;
  }
  Argument& callback(const std::function<void(const std::string&)>& callback) {
    this->callback_ = callback;
    return *this;
  }
  Argument& required(bool required) {
    this->required_ = required;
    return *this;
  }
  Argument& is_flag(bool is_flag) {
    this->is_flag_ = is_flag;
    this->default_value_ = false;
    return *this;
  }
};

/// An general purpose argument parser.
class ArgumanetParser {
 private:
  std::unordered_map<std::string, Argument> arguments_;

 public:
  Argument& add_argument(const std::string& name) {
    this->arguments_.emplace(name, Argument());
    this->arguments_.at(name).name(name);
    return this->arguments_.at(name);
  }

  template <typename T>
  std::optional<T> get(const std::string& name) {
    if (this->arguments_.find(name) == this->arguments_.end()) {
      std::cerr << "no such argument: " << name << std::endl;
      return std::nullopt;
    }
    if (!this->arguments_.at(name).value_.has_value()) {
      std::cerr << "argument not initialized: " << name << std::endl;
      return std::nullopt;
    }
    try {
      return std::any_cast<T>(this->arguments_.at(name).value_.value());
    } catch (const std::bad_any_cast&) {
      std::cerr << "argument type mismatch: " << name << std::endl;
      std::cerr << "expected type: " << typeid(T).name() << std::endl;
      std::cerr << "actual type: "
                << this->arguments_.at(name).value_.value().type().name()
                << std::endl;
      return std::nullopt;
    }
  }

  bool parse_args(int argc, char* argv[]) {
    std::unordered_map<std::string, std::optional<std::string>> parsed_args;

    // initialize the vectors
    for (const auto& [name, argument] : this->arguments_) {
      parsed_args[name] = std::nullopt;
    }

    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];

      auto equal_pos = arg.find('=');

      // only support --name=value style.
      if (arg.starts_with("--")) {
        if (equal_pos == std::string::npos) {
          // this should be a flag
          auto name = arg.substr(2);
          parsed_args.at(name) = "true";
        } else {
          // this is a named argument
          auto name = arg.substr(2, equal_pos - 2);
          auto value = arg.substr(equal_pos + 1);
          parsed_args.at(name) = value;
        }
      } else {
        std::cerr << "invalid argument: " << arg << std::endl;
        return false;
      }
    }

    for (auto& [name, value] : parsed_args) {
      if (value.has_value()) {
        this->arguments_.at(name).value_ =
          this->arguments_.at(name).parser_(value.value());
      } else {
        if (this->arguments_.at(name).default_value_.has_value()) {
          this->arguments_.at(name).value_ =
            this->arguments_.at(name).default_value_;
        } else if (this->arguments_.at(name).required_) {
          std::cerr << "missing required argument: " << name << std::endl;
          return false;
        }
      }
    }

    return true;
  }
};

#endif  // NKURDT_APP_ARGPARSER_HPP_
