/**
 * @file serial_connection.hpp
 * @author Max Yvon Zimmermann
 *
 * @copyright GNU Lesser General Public License v3.0 (LGPL-3.0-or-later)
 *
 */

#pragma once

#include <labrat/robot/plugins/mavlink/connection.hpp>
#include <labrat/robot/utils/types.hpp>

#include <string>

#include <netinet/in.h>
#include <signal.h>

namespace labrat::robot::plugins {

/**
 * @brief MavlinkConnection implementation for serial ports.
 *
 */
class MavlinkSerialConnection : public MavlinkConnection {
public:
  /**
   * @brief Construct a new Mavlink Serial Connection object.
   *
   * @param port Path to the serial port.
   */
  MavlinkSerialConnection(const std::string &port = "/dev/ttyUSB0", u64 baud_rate = 921600);

  /**
   * @brief Destroy the Mavlink Serial Connection object.
   *
   */
  virtual ~MavlinkSerialConnection();

  /**
   * @brief Write bytes to the serial port.
   *
   * @param buffer Buffer to be read from.
   * @param size Size of the buffer.
   * @return std::size_t Number of bytes written.
   */
  virtual std::size_t write(const u8 *buffer, std::size_t size);

  /**
   * @brief Read bytes from the serial port.
   *
   * @param buffer Buffer to be written to.
   * @param size Size of the buffer.
   * @return std::size_t Number of bytes read.
   */
  virtual std::size_t read(u8 *buffer, std::size_t size);

private:
  ssize_t file_descriptor;
  ssize_t epoll_handle;

  static constexpr i32 timeout = 1000;
  sigset_t signal_mask;
};

}  // namespace labrat::robot::plugins