/**
 * @file logger.cpp
 * @author Max Yvon Zimmermann
 *
 * @copyright GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)
 *
 */

#include <labrat/lbot/logger.hpp>
#include <labrat/lbot/clock.hpp>
#include <labrat/lbot/manager.hpp>
#include <labrat/lbot/message.hpp>
#include <labrat/lbot/msg/foxglove/Log.fb.hpp>
#include <labrat/lbot/node.hpp>

#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

inline namespace labrat {
namespace lbot {

Logger::Verbosity Logger::log_level = Verbosity::info;
bool Logger::use_color = true;
bool Logger::print_location = false;
bool Logger::print_time = true;

static std::mutex io_mutex;

class EntryMessage : public MessageBase<foxglove::Log, Logger::Entry> {
public:
  static void convertFrom(const Converted &source, Storage &destination) {
    switch (source.verbosity) {
      case (Logger::Verbosity::critical): {
        destination.level = foxglove::LogLevel::FATAL;
        break;
      }

      case (Logger::Verbosity::error): {
        destination.level = foxglove::LogLevel::ERROR;
        break;
      }

      case (Logger::Verbosity::warning): {
        destination.level = foxglove::LogLevel::WARNING;
        break;
      }

      case (Logger::Verbosity::info): {
        destination.level = foxglove::LogLevel::INFO;
        break;
      }

      case (Logger::Verbosity::debug): {
        destination.level = foxglove::LogLevel::DEBUG;
        break;
      }
    }

    const Clock::duration duration = source.timestamp.time_since_epoch();
    destination.timestamp = std::make_unique<foxglove::Time>(std::chrono::duration_cast<std::chrono::seconds>(duration).count(),
      (duration % std::chrono::seconds(1)).count());
    destination.name = source.logger_name;
    destination.message = source.message;
    destination.file = source.file;
    destination.line = source.line;
  }
};

class LoggerNode : public UniqueNode {
private:
  Sender<EntryMessage>::Ptr sender;

public:
  explicit LoggerNode() : UniqueNode("logger") {
    const MessageReflection reflection = MessageReflection(EntryMessage::getName());
    if (!reflection.isValid()) {
      getLogger().logCritical()
        << "Message schema of the log message is unknown. Please set the LBOT_REFLECTION_PATH environment variable.";
      exit(1);
    }

    sender = addSender<EntryMessage>("/log");
  }

  void send(const Logger::Entry &entry) {
    sender->put(entry);
  }

  void trace(const Logger::Entry &entry) {
    sender->trace(entry);
  }
};

class Color {
public:
  enum class Code : i16 {
    black = 30,
    red = 31,
    green = 32,
    yellow = 33,
    blue = 34,
    magenta = 35,
    cyan = 36,
    white = 37,
    normal = 39,
  };

  explicit Color(bool enable_color, Code code = Code::normal) : code(code), enable_color(enable_color) {}

  friend std::ostream &

  operator<<(std::ostream &stream, const Color &color) {
    if (color.enable_color) {
      return stream << "\033[" << static_cast<i16>(color.code) << "m";
    }

    return stream;
  }

private:
  const Code code;
  const bool enable_color;
};

std::string getVerbosityLong(Logger::Verbosity verbosity);
std::string getVerbosityShort(Logger::Verbosity verbosity);
Color getVerbosityColor(Logger::Verbosity verbosity);

std::shared_ptr<LoggerNode> Logger::node;

Logger::Logger(std::string name) : name(std::move(name)) {}

void Logger::initialize() {
  node = Manager::get()->addNode<LoggerNode>("logger");
}

void Logger::deinitialize() {
  node.reset();
}

Logger::LogStream Logger::write(Verbosity verbosity, LoggerLocation &&location) {
  return LogStream(*this, verbosity, std::move(location));
}

void Logger::send(const Entry &entry) {
  if (node) {
    node->send(entry);
  }
}

void Logger::trace(const Entry &entry) {
  if (node) {
    node->trace(entry);
  }
}

Logger::LogStream::LogStream(const Logger &logger, Verbosity verbosity, LoggerLocation &&location) :
  logger(logger), verbosity(verbosity), location(location) {}

Logger::LogStream::~LogStream() {
  const Clock::time_point now = Clock::initialized() ? Clock::now() : Clock::time_point();

  if (verbosity <= Logger::log_level) {
    const std::chrono::system_clock::time_point now_local = std::chrono::time_point<std::chrono::system_clock>(now.time_since_epoch());
    const std::time_t current_time = std::chrono::system_clock::to_time_t(now_local);

    std::lock_guard guard(io_mutex);

    std::cout << getVerbosityColor(verbosity) << "[" << getVerbosityShort(verbosity) << "]" << Color(isColorEnabled()) << " ("
              << logger.name;

    if (isLocationEnabled() || isTimeEnabled()) {
      std::cout << " @";
    }

    if (isTimeEnabled()) {
      std::cout << " " << std::put_time(std::localtime(&current_time), "%T");
    }
    if (isLocationEnabled()) {
      std::cout << " " << location.file << ":" << location.line;
    }

    std::cout << "): ";

    std::cout << line.str() << std::endl;
  }

  Entry entry;
  entry.verbosity = verbosity;
  entry.timestamp = now;
  entry.logger_name = logger.name;
  entry.message = line.str();
  entry.file = location.file;
  entry.line = location.line;

  if (!logger.send_topic) {
    return;
  }

  if (verbosity <= Verbosity::info) {
    logger.send(entry);
  } else {
    logger.trace(entry);
  }
}

Logger::LogStream &Logger::LogStream::operator<<(std::ostream &(*func)(std::ostream &)) {
  line << func;

  return *this;
}

std::string getVerbosityLong(Logger::Verbosity verbosity) {
  switch (verbosity) {
    case (Logger::Verbosity::critical): {
      return "critical";
    }

    case (Logger::Verbosity::error): {
      return "error";
    }

    case (Logger::Verbosity::warning): {
      return "warning";
    }

    case (Logger::Verbosity::info): {
      return "info";
    }

    case (Logger::Verbosity::debug): {
      return "debug";
    }

    default: {
      return "";
    }
  }
}

std::string getVerbosityShort(Logger::Verbosity verbosity) {
  switch (verbosity) {
    case (Logger::Verbosity::critical): {
      return "CRIT";
    }

    case (Logger::Verbosity::error): {
      return "ERRO";
    }

    case (Logger::Verbosity::warning): {
      return "WARN";
    }

    case (Logger::Verbosity::info): {
      return "INFO";
    }

    case (Logger::Verbosity::debug): {
      return "DBUG";
    }

    default: {
      return "";
    }
  }
}

Color getVerbosityColor(Logger::Verbosity verbosity) {
  switch (verbosity) {
    case (Logger::Verbosity::critical):
    case (Logger::Verbosity::error): {
      return Color(Logger::isColorEnabled(), Color::Code::red);
    }

    case (Logger::Verbosity::warning): {
      return Color(Logger::isColorEnabled(), Color::Code::yellow);
    }

    case (Logger::Verbosity::info): {
      return Color(Logger::isColorEnabled(), Color::Code::cyan);
    }

    case (Logger::Verbosity::debug): {
      return Color(Logger::isColorEnabled(), Color::Code::magenta);
    }

    default: {
      return Color(Logger::isColorEnabled());
    }
  }
}

}  // namespace lbot
}  // namespace labrat
