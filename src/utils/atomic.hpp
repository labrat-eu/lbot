/**
 * @file atomic.hpp
 * @author Max Yvon Zimmermann
 *
 * @copyright GNU Lesser General Public License v2.1 or later (LGPL-2.1-or-later)
 *
 */

#pragma once

#include <labrat/lbot/base.hpp>

#include <atomic>

/** @cond INTERNAL */
inline namespace labrat {
/** @endcond */
namespace lbot {
/** @cond INTERNAL */
inline namespace utils {
/** @endcond */

/**
 * @brief RAII guard of an atomic value to increment it on construction and decrement it upon destruction.
 * Use it alongside the waitUntil or SpinUntil classes to guarantee no alteration of a resource once the guard exists in a consumer.
 *
 * @tparam T Type of the underlying atomic object.
 */
template <typename T>
class ConsumerGuard
{
public:
  /**
   * @brief Increment the atomic object.
   *
   * @param object The atomic object to be guarded.
   */
  explicit inline ConsumerGuard(std::atomic<T> &object) :
    object(object)
  {
    ++object;
  }

  /**
   * @brief Increment the atomic object.
   *
   * @param object The atomic object to be guarded.
   * @param pred Predicate guarateed to be false on return.
   */
  explicit inline ConsumerGuard(std::atomic<T> &object, std::atomic_flag &pred) :
    object(object)
  {
    while (true) {
      ++object;

      if (!pred.test()) {
        break;
      }

      --object;
      object.notify_all();

      pred.wait(true);
    }
  }

  /**
   * @brief Decrement the atomic object and notify all threads waiting on it.
   *
   */
  inline ~ConsumerGuard()
  {
    --object;
    object.notify_all();
  }

private:
  std::atomic<T> &object;
};

/**
 * @brief RAII guard of an atomic flag to wait for it to become unset and set it on construction and unset it upon destruction.
 * Use it as a mutex with information of third party threads on whether the resource is currently locked.
 *
 */
class FlagGuard
{
public:
  /**
   * @brief Atomically wait for the atomic flag to become unset and set it.
   *
   * @param flag The atomic flag
   */
  explicit inline FlagGuard(std::atomic_flag &flag) :
    flag(flag)
  {
    while (flag.test_and_set()) {
      flag.wait(true);
    }
  }

  /**
   * @brief Unset the atomic flag and notify all threads waiting on it.
   *
   */
  inline ~FlagGuard()
  {
    flag.clear();
    flag.notify_all();
  }

private:
  std::atomic_flag &flag;
};

/**
 * @brief Wait for an atomic flag to have a specified value.
 *
 * @tparam RequiredValue The required boolean value.
 * @param flag The atomic flag object.
 */
template <bool RequiredValue = true>
inline void flagBlock(std::atomic_flag &flag)
{
  while (flag.test() != RequiredValue) {
    flag.wait(!RequiredValue);
  }
}

/**
 * @brief Wait for an atomic object to have a specified value.
 *
 * @tparam T Type of the underlying atomic object.
 * @param value The atomic object.
 * @param required The required value.
 */
template <typename T>
inline void waitUntil(std::atomic<T> &value, T required)
{
  while (true) {
    const std::size_t result = value.load();

    if (result == required) {
      break;
    }

    value.wait(result);
  }
}

/**
 * @brief Wait for an atomic object to have a specified value by spinning the current thread.
 *
 * @tparam T Type of the underlying atomic object.
 * @param value The atomic object.
 * @param required The required value.
 */
template <typename T>
inline void spinUntil(std::atomic<T> &value, T required)
{
  while (true) {
    const std::size_t result = value.load();

    if (result == required) {
      break;
    }
  }
}

/** @cond INTERNAL */
}  // namespace utils
/** @endcond */
}  // namespace lbot
/** @cond INTERNAL */
}  // namespace labrat
/** @endcond */
