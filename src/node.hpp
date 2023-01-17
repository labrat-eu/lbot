/**
 * @file node.hpp
 * @author Max Yvon Zimmermann
 *
 * @copyright GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)
 *
 */

#pragma once

#include <labrat/robot/exception.hpp>
#include <labrat/robot/logger.hpp>
#include <labrat/robot/message.hpp>
#include <labrat/robot/plugin.hpp>
#include <labrat/robot/service.hpp>
#include <labrat/robot/topic.hpp>
#include <labrat/robot/utils/fifo.hpp>
#include <labrat/robot/utils/types.hpp>

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace labrat::robot {

class Manager;

/**
 * @brief Base class for all nodes. A node should perform a specific task within the application.
 *
 */
class Node {
public:
  /**
   * @brief Information on the environment in which the node is created.
   * This data will be copied by a node upon construction.
   *
   */
  struct Environment {
    const std::string name;

    TopicMap &topic_map;
    ServiceMap &service_map;
    Plugin::List &plugin_list;
  };

  /**
   * @brief Destroy the Node object.
   *
   */
  ~Node() = default;

  /**
   * @brief Generic class to send out messages over a topic.
   *
   * @tparam MessageType Type of the messages sent over the topic.
   * @tparam ContainerType Type of the objects provided by the user to be accepted by the sender.
   */
  template <typename MessageType, typename ContainerType = MessageType>
  requires is_message<MessageType>
  class Sender {
  private:
    /**
     * @brief Construct a new Sender object.
     *
     * @param topic_name Name of the topic.
     * @param node Reference to the parent node.
     * @param conversion_function Conversion function convert from ContainerType to MessageType.
     * @param user_ptr User pointer to be used by the conversion function.
     */
    Sender(const std::string &topic_name, Node &node,
      ConversionFunction<ContainerType, MessageType> conversion_function = defaultSenderConversionFunction<MessageType, ContainerType>,
      const void *user_ptr = nullptr) :
      topic_info(Plugin::TopicInfo::get<MessageType>(topic_name)),
      node(node), conversion_function(conversion_function), user_ptr(user_ptr),
      topic(node.environment.topic_map.addSender<MessageType>(topic_name, this)) {
      for (Plugin &plugin : node.environment.plugin_list) {
        if (plugin.delete_flag.test() || !plugin.filter.check(topic_info.topic_hash)) {
          continue;
        }

        utils::ConsumerGuard<u32> guard(plugin.use_count);

        plugin.topic_callback(plugin.user_ptr, topic_info);
      }
    }

    friend class Node;

    const Plugin::TopicInfo topic_info;

    Node &node;
    const ConversionFunction<ContainerType, MessageType> conversion_function;
    const void *const user_ptr;
    TopicMap::Topic &topic;

  public:
    using Ptr = std::unique_ptr<Sender<MessageType, ContainerType>>;

    /**
     * @brief Destroy the Sender object.
     *
     */
    ~Sender() {
      flush();

      node.environment.topic_map.removeSender(topic.name, this);
    }

    /**
     * @brief Send out a message onto the topic.
     *
     * @param container Object caintaining the data to be sent out.
     */
    void put(const ContainerType &container) {
      for (void *pointer : topic.getReceivers()) {
        Receiver<MessageType> *receiver = reinterpret_cast<Receiver<MessageType> *>(pointer);

        const std::size_t count = receiver->write_count.fetch_add(1, std::memory_order_relaxed);
        const std::size_t index = count & receiver->index_mask;

        {
          std::lock_guard guard(receiver->message_buffer[index].mutex);
          conversion_function(container, receiver->message_buffer[index].message, user_ptr);

          receiver->read_count.store(count);
        }

        receiver->flush_flag = false;
        receiver->read_count.notify_one();

        if (receiver->callback.valid()) {
          receiver->callback(*receiver, receiver->callback_ptr);
        }
      }

      log(container);
    }

    /**
     * @brief Flush all receivers of the relevant topic.
     * This will unblock any waiting receivers calling the next() function and will invalidate the data stored in their buffers.
     *
     */
    void flush() {
      for (void *pointer : topic.getReceivers()) {
        Receiver<MessageType> *receiver = reinterpret_cast<Receiver<MessageType> *>(pointer);

        const std::size_t count = receiver->write_count.fetch_add(1, std::memory_order_relaxed);

        receiver->flush_flag = true;
        receiver->read_count.store(count);
        receiver->read_count.notify_one();
      }
    }

    /**
     * @brief Provide a message to the active plugins without actually send out any data over the topic.
     *
     * @param container Object caintaining the data to be sent out.
     */
    void log(const ContainerType &container) {
      if (node.environment.plugin_list.empty()) {
        return;
      }

      MessageType message;
      conversion_function(container, message, user_ptr);

      Plugin::MessageInfo message_info = {
        .topic_info = topic_info,
        .timestamp = message.getTimestamp(),
      };

      if (!message().SerializeToString(&message_info.serialized_message)) {
        throw SerializationException("Failure during message serialization.", node.getLogger());
      }

      for (Plugin &plugin : node.environment.plugin_list) {
        if (plugin.delete_flag.test() || !plugin.filter.check(topic_info.topic_hash)) {
          continue;
        }

        utils::ConsumerGuard<u32> guard(plugin.use_count);

        plugin.message_callback(plugin.user_ptr, message_info);
      }
    }

    /**
     * @brief Get the name of the relevant topic.
     *
     * @return const std::string& Name of the topic.
     */
    inline const std::string &getTopicName() const {
      return topic.name;
    }
  };

  /**
   * @brief Alias of the Sender class with only the ContainerType template parameter provided that the relevant type satisfies the
   * is_container concept.
   *
   * @tparam ContainerType Type of the objects provided by the user to be accepted by the sender.
   */
  template <typename ContainerType>
  requires is_container<ContainerType>
  using ContainerSender = Sender<typename ContainerType::MessageType, ContainerType>;

  /**
   * @brief Generic class to receive messages from a topic.
   *
   * @tparam MessageType Type of the messages sent over the topic.
   * @tparam ContainerType Type of the objects provided by the receiver to be used by the user.
   */
  template <typename MessageType, typename ContainerType = MessageType>
  requires is_message<MessageType>
  class Receiver {
  private:
    /**
     * @brief Construct a new Receiver object.
     *
     * @param topic_name Name of the topic.
     * @param node Reference to the parent node.
     * @param conversion_function Conversion function convert from MessageType to ContainerType.
     * @param user_ptr User pointer to be used by the conversion function.
     * @param buffer_size Size of the internal receiver buffer. It must be at least 4 and should ideally be a power of 2.
     */
    Receiver(const std::string &topic_name, Node &node,
      ConversionFunction<MessageType, ContainerType> conversion_function = defaultReceiverConversionFunction<MessageType, ContainerType>,
      const void *user_ptr = nullptr, std::size_t buffer_size = 4) :
      topic_info(Plugin::TopicInfo::get<MessageType>(topic_name)),
      node(node), conversion_function(conversion_function), user_ptr(user_ptr),
      topic(node.environment.topic_map.addReceiver<MessageType>(topic_name, this)), index_mask(calculateBufferMask(buffer_size)),
      message_buffer(calculateBufferSize(buffer_size)), write_count(0), read_count(index_mask) {
      next_count = index_mask;
      flush_flag = true;
    }

    /**
     * @brief Calculate the internal buffer size required to satisfy the provided buffer size.
     * This will either be the provided buffer size itself or the next power of 2.
     *
     * @param buffer_size Provided buffer size of the receiver.
     * @return std::size_t Internal buffer size.
     */
    std::size_t calculateBufferSize(std::size_t buffer_size) {
      if (buffer_size < 4) {
        throw InvalidArgumentException("The buffer size for a Receiver must be at least 4.", node.getLogger());
      }

      std::size_t mask = std::size_t(1) << ((sizeof(std::size_t) * 8) - 1);

      while (true) {
        if (mask & buffer_size) {
          if (mask ^ buffer_size) {
            return mask << 1;
          } else {
            return mask;
          }
        }

        mask >>= 1;
      }
    }

    /**
     * @brief Calculate the buffer mask used internally to compute a buffer index from a receive count.
     *
     * @param buffer_size Provided buffer size of the receiver.
     * @return std::size_t Internal buffer mask.
     */
    std::size_t calculateBufferMask(std::size_t buffer_size) {
      return calculateBufferSize(buffer_size) - 1;
    }

    friend class Node;
    friend class Sender<MessageType>;
    friend class TopicMap;

    /**
     * @brief Internal message buffer.
     *
     */
    class MessageBuffer {
    public:
      struct MessageData {
        MessageType message;
        std::mutex mutex;
      };

      /**
       * @brief Construct a new Message Buffer object given its size.
       *
       * @param size Size of the message buffer.
       */
      MessageBuffer(std::size_t size) : size(size) {
        buffer = allocator.allocate(size);

        for (std::size_t i = 0; i < size; ++i) {
          std::construct_at<MessageData>(buffer + i);
        }
      }

      /**
       * @brief Destroy the Message Buffer object.
       *
       */
      ~MessageBuffer() {
        for (std::size_t i = 0; i < size; ++i) {
          std::destroy_at<MessageData>(buffer + i);
        }

        allocator.deallocate(buffer, size);
      }

      /**
       * @brief Access elements in the buffer.
       *
       * @param index Index of the requested element.
       * @return MessageData& Reference to the requested element.
       */
      MessageData &operator[](std::size_t index) volatile {
        return buffer[index];
      }

    private:
      MessageData *buffer;
      std::allocator<MessageData> allocator;

      const std::size_t size;
    };

    const Plugin::TopicInfo topic_info;

    Node &node;
    const ConversionFunction<MessageType, ContainerType> conversion_function;
    const void *user_ptr;
    TopicMap::Topic &topic;

    const std::size_t index_mask;
    volatile MessageBuffer message_buffer;

    std::atomic<std::size_t> write_count;
    std::atomic<std::size_t> read_count;
    std::size_t next_count;
    volatile bool flush_flag;

  public:
    using Ptr = std::unique_ptr<Receiver<MessageType, ContainerType>>;

    /**
     * @brief Callback function to be called by the corresponding sender on each put operation.
     *
     */
    class CallbackFunction {
    public:
      template <typename DataType = void>
      using Function = void (*)(Receiver<MessageType, ContainerType> &, DataType *);

      /**
       * @brief Default constructor invalidating the object.
       *
       */
      CallbackFunction() : function(nullptr) {}

      /**
       * @brief Construct a new Callback Function object by providing a function and user pointer.
       *
       * @tparam DataType Type of the data pointed to by the user pointer.
       * @param function Function to be used as a callback function.
       */
      template <typename DataType = void>
      CallbackFunction(Function<DataType> function) : function(reinterpret_cast<Function<void>>(function)) {}

      /**
       * @brief Call the stored callback function.
       *
       * @param receiver Reference to the relevant receiver instance.
       * @param user_ptr User pointer to access generic external data.
       */
      inline void operator()(Receiver<MessageType, ContainerType> &receiver, void *user_ptr) const {
        function(receiver, user_ptr);
      }

      /**
       * @brief Validate that a function is stored in this instance.
       *
       * @return true A function is stored.
       * @return false A function is not stored.
       */
      inline bool valid() {
        return function != nullptr;
      }

    private:
      Function<void> function;
    };

    /**
     * @brief Destroy the Receiver object.
     *
     */
    ~Receiver() {
      node.environment.topic_map.removeReceiver(topic.name, this);
    }

    /**
     * @brief Get the lastest message sent over the topic.
     * This call is guaranteed to not block.
     *
     * @return ContainerType The latest message sent over the topic.
     * @throw TopicNoDataAvailableException When the topic has no valid data available.
     */
    ContainerType latest() {
      ContainerType result;

      const std::size_t index = read_count.load() & index_mask;

      if (flush_flag) {
        throw TopicNoDataAvailableException("Topic was flushed.", node.getLogger());
      }

      {
        std::lock_guard guard(message_buffer[index].mutex);
        conversion_function(message_buffer[index].message, result, user_ptr);
      }

      return result;
    };

    /**
     * @brief Get the next message sent over the topic.
     * This call might block. However, it guaranteed that successive calls will yield different messages.
     *
     * @return ContainerType The next message sent over the topic.
     * @throw TopicNoDataAvailableException When the topic has no valid data available.
     */
    ContainerType next() {
      if (flush_flag) {
        throw TopicNoDataAvailableException("Topic was flushed.", node.getLogger());
      }

      ContainerType result;

      read_count.wait(next_count);

      if (flush_flag) {
        throw TopicNoDataAvailableException("Topic was flushed during wait operation.", node.getLogger());
      }

      const std::size_t index = read_count.load() & index_mask;

      {
        std::lock_guard guard(message_buffer[index].mutex);
        conversion_function(message_buffer[index].message, result, user_ptr);
      }

      next_count = index | (read_count.load() & ~index_mask);

      return result;
    };

    /**
     * @brief Register a callback function.
     *
     * @param function Callback function to be registered.
     * @param ptr User pointer to access generic external data.
     */
    void setCallback(CallbackFunction function, void *ptr = nullptr) {
      callback = function;
      callback_ptr = ptr;
    }

    /**
     * @brief Check whether new data is available on the topic and a call to next() will not block.
     *
     * @return true New data is availabe, a call to next() will not block.
     * @return false No new data is availabe, a call to next() will block.
     */
    inline bool newDataAvailable() {
      return (read_count.load() != next_count);
    }

    /**
     * @brief Get the internal buffer size.
     *
     * @return std::size_t
     */
    inline std::size_t getBufferSize() const {
      return index_mask + 1;
    }

    /**
     * @brief Get the name of the relevant topic.
     *
     * @return const std::string& Name of the topic.
     */
    inline const std::string &getTopicName() const {
      return topic.name;
    }

  private:
    CallbackFunction callback;
    void *callback_ptr;
  };

  /**
   * @brief Alias of the Receiver class with only the ContainerType template parameter provided that the relevant type satisfies the
   * is_container concept.
   *
   * @tparam ContainerType Type of the objects provided by the receiver to be used by the user.
   */
  template <typename ContainerType>
  requires is_container<ContainerType>
  using ContainerReceiver = Receiver<typename ContainerType::MessageType, ContainerType>;

  /**
   * @brief Generic class to handle requests made to a service.
   *
   * @tparam RequestType Type of the request made to a service.
   * @tparam ResponseType Type of the response made to by a service.
   */
  template <typename RequestType, typename ResponseType>
  class Server {
  public:
    /**
     * @brief Handler function to handle requests made to a service.
     *
     */
    class HandlerFunction {
    public:
      template <typename DataType = void>
      using Function = ResponseType (*)(const RequestType &, DataType *);

      /**
       * @brief Construct a new handler function.
       *
       * @tparam DataType Type of the data pointed to by the user pointer.
       * @param function Function to be used as a handler function.
       */
      template <typename DataType = void>
      HandlerFunction(Function<DataType> function) : function(reinterpret_cast<Function<void>>(function)) {}

      /**
       * @brief Call the stored conversion function.
       *
       * @param request Request sent by the client.
       * @param user_ptr User pointer to access generic external data.
       * @return ResponseType Response to be sent to the client.
       */
      inline ResponseType operator()(const RequestType &request, void *user_ptr) const {
        return function(request, user_ptr);
      }

    private:
      Function<void> function;
    };

  private:
    /**
     * @brief Construct a new Server object
     *
     * @param service_name Name of the service.
     * @param node Reference to the parent node.
     * @param handler_function Handler function to handle requests made to a service.
     * @param user_ptr User pointer to be used by the handler function.
     */
    Server(const std::string &service_name, Node &node, HandlerFunction handler_function, void *user_ptr = nullptr) :
      node(node), handler_function(handler_function), user_ptr(user_ptr),
      service(node.environment.service_map.addServer<RequestType, ResponseType>(service_name, this)) {}

    friend class Node;
    friend class Client;

    Node &node;
    const HandlerFunction handler_function;
    void *const user_ptr;
    ServiceMap::Service &service;

  public:
    using Ptr = std::unique_ptr<Server<RequestType, ResponseType>>;

    /**
     * @brief Destroy the Server object.
     *
     */
    ~Server() {
      service.removeServer(this);
    }

    /**
     * @brief Get the name of the relevant service.
     *
     * @return const std::string& Name of the service.
     */
    inline const std::string &getServiceName() const {
      return service.name;
    }
  };

  /**
   * @brief Generic class to perform requests to a service.
   *
   * @tparam RequestType Type of the request made to a service.
   * @tparam ResponseType Type of the response made to by a service.
   */
  template <typename RequestType, typename ResponseType>
  class Client {
  private:
    /**
     * @brief Construct a new Client object.
     *
     * @param service_name Name of the service.
     * @param node Reference to the parent node.
     */
    Client(const std::string &service_name, Node &node) :
      node(node), service(node.environment.service_map.getService<RequestType, ResponseType>(service_name)) {}

    friend class Node;

    Node &node;
    ServiceMap::Service &service;

  public:
    using Ptr = std::unique_ptr<Client<RequestType, ResponseType>>;
    using Future = std::shared_future<ResponseType>;

    /**
     * @brief Destroy the Client object.
     *
     */
    ~Client() = default;

    /**
     * @brief Make a request to a service asynchronously.
     * A call to this function will not block.
     *
     * @param request Object containing the data to be processed by the corresponding server.
     * @return Future Future to be completed by the server.
     * @throw ServiceUnavailableException When no server is handling requests to the relevant service.
     */
    Future callAsync(const RequestType &request) {
      std::promise<ResponseType> promise;
      Future future = promise.get_future();

      std::thread(
        [this](std::promise<ResponseType> promise, const RequestType request) {
        try {
          ServiceMap::Service::ServerReference reference = service.getServer();
          Server<RequestType, ResponseType> *server = reference;

          if (server == nullptr) {
            throw ServiceUnavailableException("Service is not available.", node.getLogger());
          }

          promise.set_value(server->handler_function(request, server->user_ptr));
        } catch (...) {
          promise.set_exception(std::current_exception());
        }
        },
        std::move(promise), request)
        .detach();

      return future;
    }

    /**
     * @brief Make a request to a service synchronously.
     * A call to this function will block.
     *
     * @param request Object containing the data to be processed by the corresponding server.
     * @return ResponseType Response from the server.
     * @throw ServiceUnavailableException When no server is handling requests to the relevant service.
     */
    ResponseType callSync(const RequestType &request) {
      Future future = callAsync(request);

      return future.get();
    }

    /**
     * @brief Make a request to a service synchronously.
     * A call to this function will block but is guaranteed to not exceed the specified timeout.
     *
     * @param request Object containing the data to be processed by the corresponding server.
     * @param timeout_duration Duration of the timeout after which an exception will be thrown.
     * @return ResponseType Response from the server.
     * @throw ServiceUnavailableException When no server is handling requests to the relevant service.
     * @throw ServiceTimeoutException When the timeout is exceeded.
     */
    template <typename R, typename P>
    ResponseType callSync(const RequestType &request, const std::chrono::duration<R, P> &timeout_duration) {
      Future future = callAsync(request);

      if (future.wait_for(timeout_duration) != std::future_status::ready) {
        throw ServiceTimeoutException("Service took too long to respond.", node.getLogger());
      }

      return future.get();
    }

    /**
     * @brief Get the name of the relevant service.
     *
     * @return const std::string& Name of the service.
     */
    inline const std::string &getServiceName() const {
      return service.name;
    }
  };

  /**
   * @brief Get the name of the node.
   *
   * @return const std::string& Name of the node.
   */
  const std::string &getName() {
    return environment.name;
  }

protected:
  /**
   * @brief Construct a new Node object.
   *
   * @param environment Node environment data for the node to copy internally.
   */
  Node(const Environment &environment) : environment(environment) {}

  /**
   * @brief Get a logger with the name of the node.
   *
   * @return Logger A logger with the name of the node.
   */
  inline Logger getLogger() const {
    return Logger(environment.name);
  }

  /**
   * @brief Construct and add a sender to the node.
   *
   * @tparam MessageType Type of the messages sent over the topic.
   * @tparam ContainerType Type of the objects provided by the user to be accepted by the sender.
   * @tparam Args Types of the arguments to be forwarded to the sender specific constructor.
   * @param args Arguments to be forwarded to the sender specific constructor.
   * @return Sender<MessageType, ContainerType>::Ptr Pointer to the sender.
   */
  template <typename MessageType, typename ContainerType = MessageType, typename... Args>
  typename Sender<MessageType, ContainerType>::Ptr addSender(const std::string &topic_name,
    Args &&...args) requires is_message<MessageType> {
    return std::unique_ptr<Sender<MessageType, ContainerType>>(
      new Sender<MessageType, ContainerType>(topic_name, *this, std::forward<Args>(args)...));
  }

  /**
   * @brief Construct and add a sender to the node, provided that the ContainerType satisfies the is_container concept.
   *
   * @tparam ContainerType Type of the objects provided by the user to be accepted by the sender.
   * @tparam Args Types of the arguments to be forwarded to the sender specific constructor.
   * @param args Arguments to be forwarded to the sender specific constructor.
   * @return Sender<MessageType, ContainerType>::Ptr Pointer to the sender.
   */
  template <typename ContainerType, typename... Args>
  typename ContainerSender<ContainerType>::Ptr addSender(const std::string &topic_name,
    Args &&...args) requires is_container<ContainerType> {
    return std::unique_ptr<ContainerSender<ContainerType>>(
      new ContainerSender<ContainerType>(topic_name, *this, ContainerType::toMessage, std::forward<Args>(args)...));
  }

  /**
   * @brief Construct and add a receiver to the node.
   *
   * @tparam MessageType Type of the messages sent over the topic.
   * @tparam ContainerType Type of the objects provided by the receiver to be used by the user.
   * @tparam Args Types of the arguments to be forwarded to the receiver specific constructor.
   * @param args Arguments to be forwarded to the receiver specific constructor.
   * @return Receiver<MessageType, ContainerType>::Ptr Pointer to the receiver.
   */
  template <typename MessageType, typename ContainerType = MessageType, typename... Args>
  typename Receiver<MessageType, ContainerType>::Ptr addReceiver(const std::string &topic_name,
    Args &&...args) requires is_message<MessageType> {
    return std::unique_ptr<Receiver<MessageType, ContainerType>>(
      new Receiver<MessageType, ContainerType>(topic_name, *this, std::forward<Args>(args)...));
  }

  /**
   * @brief Construct and add a receiver to the node, provided that the ContainerType satisfies the is_container concept.
   *
   * @tparam ContainerType Type of the objects provided by the receiver to be used by the user.
   * @tparam Args Types of the arguments to be forwarded to the receiver specific constructor.
   * @param args Arguments to be forwarded to the receiver specific constructor.
   * @return Receiver<MessageType, ContainerType>::Ptr Pointer to the receiver.
   */
  template <typename ContainerType, typename... Args>
  typename ContainerReceiver<ContainerType>::Ptr addReceiver(const std::string &topic_name,
    Args &&...args) requires is_container<ContainerType> {
    return std::unique_ptr<ContainerReceiver<ContainerType>>(
      new ContainerReceiver<ContainerType>(topic_name, *this, ContainerType::fromMessage, std::forward<Args>(args)...));
  }

  /**
   * @brief Construct and add a server to the node.
   *
   * @tparam RequestType Type of the request made to a service.
   * @tparam ResponseType Type of the response made to by a service.
   * @param args Arguments to be forwarded to the server specific constructor.
   * @return Receiver<MessageType, ContainerType>::Ptr Pointer to the server.
   */
  template <typename RequestType, typename ResponseType, typename... Args>
  typename Server<RequestType, ResponseType>::Ptr addServer(const std::string &service_name, Args &&...args) {
    return std::unique_ptr<Server<RequestType, ResponseType>>(
      new Server<RequestType, ResponseType>(service_name, *this, std::forward<Args>(args)...));
  }

  /**
   * @brief Construct and add a client to the node.
   *
   * @tparam RequestType Type of the request made to a service.
   * @tparam ResponseType Type of the response made to by a service.
   * @param args Arguments to be forwarded to the client specific constructor.
   * @return Receiver<MessageType, ContainerType>::Ptr Pointer to the client.
   */
  template <typename RequestType, typename ResponseType, typename... Args>
  typename Client<RequestType, ResponseType>::Ptr addClient(const std::string &service_name, Args &&...args) {
    return std::unique_ptr<Client<RequestType, ResponseType>>(
      new Client<RequestType, ResponseType>(service_name, *this, std::forward<Args>(args)...));
  }

  friend class Manager;

private:
  const Environment environment;
};

}  // namespace labrat::robot