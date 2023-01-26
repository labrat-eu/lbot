/**
 * @file message.hpp
 * @author Max Yvon Zimmermann
 *
 * @copyright GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)
 *
 */

#pragma once

#include <labrat/robot/utils/concepts.hpp>
#include <labrat/robot/utils/types.hpp>

#include <chrono>
#include <concepts>
#include <utility>
#include <string>

#include <flatbuffers/flatbuffers.h>

namespace labrat::robot {

/**
 * @brief Wrapper of a flatbuf message for use within this library.
 *
 * @tparam T
 */
template <typename T>
requires std::is_base_of_v<flatbuffers::Table, T>
class Message {
public:
  using Content = typename T::NativeTableType;

  /**
   * @brief Default constructor to only set the timestamp to the current time.
   *
   */
  Message() {
    timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
  }

  /**
   * @brief Construct a new Message object by specifying the contents stored within the message.
   * Also set the timestamp to the current time.
   *
   * @param content Contents of the message.
   */
  Message(const Content &content) : content(content) {
    timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
  }

  /**
   * @brief Construct a new Message object by specifying the contents stored within the message.
   * Also set the timestamp to the current time.
   *
   * @param content Contents of the message.
   */
  Message(Content &&content) : content(std::forward(content)) {
    timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
  }

  /**
   * @brief Get the stored flatbuf message object.
   *
   * @return Content& Reference to the stored flatbuf message object.
   */
  inline Content &get() {
    return content;
  };

  /**
   * @brief Get the stored flatbuf message object.
   *
   * @return Content& Reference to the stored flatbuf message object.
   */
  inline const Content &get() const {
    return content;
  };

  /**
   * @brief Alias of get().
   *
   * @return Content& Reference to the stored flatbuf message object.
   */
  inline Content &operator()() {
    return get();
  };

  /**
   * @brief Alias of get().
   *
   * @return Content& Reference to the stored flatbuf message object.
   */
  inline const Content &operator()() const {
    return get();
  };

  /**
   * @brief Get the timestamp of the object.
   *
   * @return std::chrono::nanoseconds
   */
  inline std::chrono::nanoseconds getTimestamp() const {
    return timestamp;
  }

  static inline constexpr std::string getName() {
    return std::string(Content::TableType::GetFullyQualifiedName());
  }

private:
  std::chrono::nanoseconds timestamp;
  Content content;
};

template <typename T>
concept is_message = utils::is_specialization_of_v<T, Message>;

template <typename T>
concept is_container = requires {
  typename T::MessageType;
};

class MessageReflection {
public:
  template <typename T>
  requires std::is_base_of_v<flatbuffers::Table, T>
  MessageReflection() : MessageReflection(Message<T>::getName()) {}

  MessageReflection(const std::string &name);

  inline const std::string &getBuffer() const {
    return buffer;
  }

  inline bool isValid() const {
    return valid;
  }

private:
  std::string buffer;
  bool valid;
};

/**
 * @brief Conversion function to convert one data type into another.
 *
 * @tparam OriginalType Type to be converted from.
 * @tparam ConvertedType Type to be converted to.
 */
template <typename OriginalType, typename ConvertedType>
class ConversionFunction {
public:
  template <typename DataType = void>
  using Function = void (*)(const OriginalType &, ConvertedType &, const DataType *);

  /**
   * @brief Construct a new conversion function.
   *
   * @tparam DataType Type of the data pointed to by the user pointer.
   * @param function Function to be used as a conversion function.
   */
  template <typename DataType = void>
  ConversionFunction(Function<DataType> function) : function(reinterpret_cast<Function<void>>(function)) {}

  /**
   * @brief Call the stored conversion function.
   *
   * @param source Object to be converted.
   * @param destination Object to be converted to.
   * @param user_ptr User pointer to access generic external data.
   */
  inline void operator()(const OriginalType &source, ConvertedType &destination, const void *user_ptr) const {
    function(source, destination, user_ptr);
  }

private:
  Function<void> function;
};

/**
 * @brief Conversion function to be used as a default by a Node::Sender object when the converted type equals the original type.
 *
 * @tparam OriginalType Type to be converted from.
 * @tparam ConvertedType Type to be converted to.
 * @param source Object to be converted.
 * @param destination Object to be converted to.
 */
template <typename OriginalType, typename ConvertedType>
inline void defaultSenderConversionFunction(const ConvertedType &source, OriginalType &destination,
  const void *) requires std::is_same_v<OriginalType, ConvertedType> {
  destination = source;
}

/**
 * @brief Conversion function to be used as a default by a Node::Sender object when a toMessage() function is provided by the converted
 * type.
 *
 * @tparam OriginalType Type to be converted from.
 * @tparam ConvertedType Type to be converted to.
 * @param source Object to be converted.
 * @param destination Object to be converted to.
 */
template <typename OriginalType, typename ConvertedType>
inline void defaultSenderConversionFunction(const ConvertedType &source, OriginalType &destination, const void *) {
  destination = OriginalType();
  ConvertedType::toMessage(source, destination);
}

/**
 * @brief Conversion function to be used as a default by a Node::Receiver object when the converted type equals the original type.
 *
 * @tparam OriginalType Type to be converted from.
 * @tparam ConvertedType Type to be converted to.
 * @param source Object to be converted.
 * @param destination Object to be converted to.
 */
template <typename OriginalType, typename ConvertedType>
inline void defaultReceiverConversionFunction(const OriginalType &source, ConvertedType &destination,
  const void *) requires std::is_same_v<OriginalType, ConvertedType> {
  destination = source;
}

/**
 * @brief Conversion function to be used as a default by a Node::Receiver object when a fromMessage() function is provided by the converted
 * type.
 *
 * @tparam OriginalType Type to be converted from.
 * @tparam ConvertedType Type to be converted to.
 * @param source Object to be converted.
 * @param destination Object to be converted to.
 */
template <typename OriginalType, typename ConvertedType>
inline void defaultReceiverConversionFunction(const OriginalType &source, ConvertedType &destination, const void *) {
  ConvertedType::fromMessage(source, destination);
}

}  // namespace labrat::robot
