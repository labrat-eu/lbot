/**
 * @file topic.hpp
 * @author Max Yvon Zimmermann
 *
 * @copyright GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)
 *
 */

#pragma once

#include <labrat/lbot/base.hpp>
#include <labrat/lbot/message.hpp>
#include <labrat/lbot/utils/atomic.hpp>
#include <labrat/lbot/utils/types.hpp>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

inline namespace labrat {
namespace lbot {

class TopicMap {
public:
  class Topic {
  private:
    void *sender;
    std::vector<void *> receivers;
    std::vector<void *> const_receivers;

    std::atomic_flag change_flag;
    std::atomic<std::size_t> use_count;

    friend class ReceiverList;

  public:
    using Handle = std::size_t;

    template <bool IsConst>
    class ReceiverList {
    public:
      explicit inline ReceiverList(Topic &topic) : topic(topic) {
        while (true) {
          topic.use_count.fetch_add(1);

          [[likely]] if (!topic.change_flag.test()) { break; }

          topic.use_count.fetch_sub(1);
          topic.use_count.notify_all();
          topic.change_flag.wait(true);
        }
      }

      inline ReceiverList(ReceiverList &&rhs) noexcept : topic(rhs.topic) {
        topic.use_count.fetch_add(1);
      }

      inline ~ReceiverList() {
        topic.use_count.fetch_sub(1);
        topic.use_count.notify_all();
      }

      [[nodiscard]] inline std::vector<void *>::iterator begin() const {
        if constexpr (IsConst) {
          return topic.const_receivers.begin();
        } else {
          return topic.receivers.begin();
        }
      }

      [[nodiscard]] inline std::vector<void *>::iterator end() const {
        if constexpr (IsConst) {
          return topic.const_receivers.end();
        } else {
          return topic.receivers.end();
        }
      }

      [[nodiscard]] inline std::size_t size() const {
        if constexpr (IsConst) {
          return topic.const_receivers.size();
        } else {
          return topic.receivers.size();
        }
      }

    private:
      Topic &topic;
    };

    Topic(Handle handle, std::string name);

    [[nodiscard]] void *getSender() const;
    void addSender(void *new_sender);
    void removeSender(void *old_sender);

    [[nodiscard]] inline ReceiverList<false> getReceivers() {
      return ReceiverList<false>(*this);
    }

    [[nodiscard]] inline ReceiverList<true> getConstReceivers() {
      return ReceiverList<true>(*this);
    }

    void addReceiver(void *new_receiver, bool is_const);
    void removeReceiver(void *old_receiver);

    const Handle handle;
    const std::string name;
  };

  TopicMap();
  ~TopicMap() = default;

  template <typename T>
  requires is_message<T>
  Topic &addSender(const std::string &topic_name, void *sender) {
    Topic &topic = getTopicInternal(topic_name, typeid(typename T::Content).hash_code());

    topic.addSender(sender);

    return topic;
  }

  Topic &removeSender(const std::string &topic_name, void *sender) {
    Topic &topic = getTopicInternal(topic_name);

    topic.removeSender(sender);

    return topic;
  }

  template <typename T>
  requires is_message<T>
  Topic &addReceiver(const std::string &topic_name, void *receiver) {
    Topic &topic = getTopicInternal(topic_name, typeid(typename T::Content).hash_code());

    topic.addReceiver(receiver, is_const_message<T>);

    return topic;
  }

  Topic &removeReceiver(const std::string &topic_name, void *receiver) {
    Topic &topic = getTopicInternal(topic_name);

    topic.removeReceiver(receiver);

    return topic;
  }

  void forceFlush();

private:
  Topic &getTopicInternal(const std::string &topic);
  Topic &getTopicInternal(const std::string &topic, std::size_t handle);

  std::unordered_map<std::string, Topic> map;
};

}  // namespace lbot
}  // namespace labrat
