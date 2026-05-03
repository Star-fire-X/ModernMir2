#pragma once

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "asio.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2::tests {

template <typename T>
void send_client_v1_message(asio::ip::tcp::socket& socket, const T& message,
                            std::uint32_t& sequence) {
  const auto bytes =
      client_v1::encode_frame(client_v1::make_frame(message, sequence++));
  asio::write(socket, asio::buffer(bytes));
}

class ClientV1SocketReader {
 public:
  explicit ClientV1SocketReader(asio::ip::tcp::socket& socket) : socket_(socket) {
    socket_.non_blocking(true);
  }

  template <typename T>
  std::optional<T> wait_for_message(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    if (auto pending = take_pending<T>(); pending.has_value()) {
      return pending;
    }

    std::array<std::uint8_t, 4096> read_buffer{};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      std::error_code error;
      const auto bytes_read = socket_.read_some(asio::buffer(read_buffer), error);
      if (!error) {
        buffer_.insert(buffer_.end(), read_buffer.begin(), read_buffer.begin() + bytes_read);
        auto frames = client_v1::drain_frames(buffer_);
        std::optional<T> matched;
        for (const auto& frame : frames) {
          const auto decoded = client_v1::decode_any(frame);
          if (!decoded.has_value()) {
            return std::nullopt;
          }
          if (const auto* value = std::get_if<T>(&*decoded); value != nullptr) {
            if (!matched.has_value()) {
              matched = *value;
              continue;
            }
          }
          pending_.push_back(*decoded);
        }
        if (matched.has_value()) {
          return matched;
        }
      } else if (error != asio::error::would_block && error != asio::error::try_again) {
        return std::nullopt;
      }

      if (auto pending = take_pending<T>(); pending.has_value()) {
        return pending;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return std::nullopt;
  }

  template <typename T, typename Predicate>
  std::optional<T> wait_for_matching(
      Predicate predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      auto message = wait_for_message<T>(std::chrono::milliseconds(250));
      if (message.has_value() && predicate(*message)) {
        return message;
      }
    }
    return std::nullopt;
  }

 private:
  template <typename T>
  std::optional<T> take_pending() {
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
      if (const auto* value = std::get_if<T>(&*it); value != nullptr) {
        auto copy = *value;
        pending_.erase(it);
        return copy;
      }
    }
    return std::nullopt;
  }

  asio::ip::tcp::socket& socket_;
  std::vector<std::uint8_t> buffer_{};
  std::vector<client_v1::Message> pending_{};
};

inline std::optional<asio::ip::tcp::socket> connect_socket(asio::io_context& io_context,
                                                           const std::string& host,
                                                           const std::uint16_t port) {
  asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host), port);
  asio::ip::tcp::socket socket(io_context);
  for (int attempt = 0; attempt < 50; ++attempt) {
    std::error_code connect_error;
    socket.connect(endpoint, connect_error);
    if (!connect_error) {
      return socket;
    }
    socket.close();
    socket = asio::ip::tcp::socket(io_context);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return std::nullopt;
}

}  // namespace mir2::tests
