/**
 * @file clock.cpp
 * @author Max Yvon Zimmermann
 *
 * @copyright GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)
 *
 */

#include <labrat/lbot/clock.hpp>
#include <labrat/lbot/config.hpp>
#include <labrat/lbot/node.hpp>
#include <labrat/lbot/exception.hpp>
#include <labrat/lbot/utils/thread.hpp>
#include <labrat/lbot/msg/timestamp.fb.hpp>

inline namespace labrat {
namespace lbot {

class TimeMessage : public MessageBase<labrat::lbot::Timestamp, Clock::time_point> {
public:
  static void convertFrom(const Converted &source, Storage &destination) {
    const Clock::duration duration = source.time_since_epoch();
    destination.value = std::make_unique<foxglove::Time>(std::chrono::duration_cast<std::chrono::seconds>(duration).count(),
      (duration % std::chrono::seconds(1)).count());
  }

  static void convertTo(const Storage &source, Converted &destination) {
    destination = Clock::time_point(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(source.value->sec()) + std::chrono::nanoseconds(source.value->nsec())));
  }
};

class Clock::Node : public UniqueNode {};

class Clock::SenderNode : public Clock::Node {
public:
  SenderNode() {
    sender = addSender<TimeMessage>("/time");
    thread = TimerThread(&SenderNode::timerFunction, std::chrono::milliseconds(10), "clock", 1, this);
  }

private:
  void timerFunction() {
    sender->put(Clock::now());
  }

  Sender<TimeMessage>::Ptr sender;
  TimerThread thread;
};

class Clock::ReceiverNode : public Clock::Node {
public:
  ReceiverNode() {
    receiver = addReceiver<const TimeMessage>("/time");
    receiver->setCallback(&ReceiverNode::receiverCallback);
  }

private:
  static void receiverCallback(const Clock::time_point &message) {
    Clock::setTime(message);
  }

  Receiver<const TimeMessage>::Ptr receiver;
};

bool Clock::is_initialized = false;
Clock::Mode Clock::mode; 
std::atomic<Clock::time_point> Clock::current_time;
std::atomic_flag Clock::exit_flag;

std::shared_ptr<Clock::Node> Clock::node;

std::priority_queue<std::shared_ptr<Clock::WaiterRegistration>> Clock::waiter_queue;
std::mutex Clock::mutex;

void Clock::initialize() {
  if (is_initialized) {
    throw ClockException("Clock is already initialized");
  }

  const std::string &mode_name = lbot::Config::get()->getParameterFallback("/lbot/clock_mode", "system").get<std::string>();

  if (mode_name == "system") {
    mode = Mode::system;
  } else if (mode_name == "steady") {
    mode = Mode::steady;
  } else if (mode_name == "custom") {
    mode = Mode::custom;
  } else {
    throw InvalidArgumentException("Invalid clock mode"); 
  }

  is_initialized = true;

  if (mode == Mode::custom) {
    node = Manager::get()->addNode<ReceiverNode>("time_node");
  } else {
    node = Manager::get()->addNode<SenderNode>("time_node");
  }
}

void Clock::deinitialize() {
  cleanup();
  is_initialized = false;
}

bool Clock::initialized() {
  return is_initialized;
}
  
Clock::time_point Clock::now() {
  if (!is_initialized) {
    throw ClockException("Clock is not initialized");
  }

  switch (mode) {
    case Mode::system: {
      return time_point(std::chrono::duration_cast<duration>(std::chrono::system_clock::now().time_since_epoch()));
    }

    case Mode::steady: {
      return time_point(std::chrono::duration_cast<duration>(std::chrono::steady_clock::now().time_since_epoch()));
    }

    case Mode::custom: {
      return time_point(current_time.load());
    }

    default: {
      throw BadUsageException("Unsupported clock mode");
    }
  }
}

void Clock::setTime(time_point time) {
  if (time < current_time.load(std::memory_order_relaxed)) {
    throw ClockException("Updated time is in the past");
  }

  {
    std::lock_guard guard(mutex);
    current_time.store(time, std::memory_order_release);

    while (!waiter_queue.empty()) {
      std::shared_ptr<WaiterRegistration> top = waiter_queue.top();

      if (top->wakeup_time > time) {
        break;
      }

      top->wakeup_flag.test_and_set();
      top->wakeup_flag.notify_all();

      waiter_queue.pop();
    }
  }
}

std::shared_ptr<Clock::WaiterRegistration> Clock::registerWaiter(const time_point wakeup_time) {
  std::shared_ptr<WaiterRegistration> result(new WaiterRegistration{.wakeup_time = wakeup_time});

  {
    std::lock_guard guard(mutex);

    if (wakeup_time > current_time.load(std::memory_order_acquire)) {
      waiter_queue.emplace(result);
      return result;
    }
  }

  result->wakeup_flag.test_and_set();

  return result;
}

void Clock::cleanup() {
  if (node) {
    node.reset();
  }

  exit_flag.test_and_set();
}


}  // namespace lbot
}  // namespace labrat
