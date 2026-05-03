#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "asio.hpp"
#include "core/messages.hpp"

namespace mir2 {

class GatewayServiceBase;

class GameSession : public std::enable_shared_from_this<GameSession> {
 public:
  GameSession(asio::ip::tcp::socket socket, GatewayServiceBase& owner);

  void start(std::uint64_t session_id);
  void deliver(const LegacyPacket& packet);
  void deliver_and_close(const LegacyPacket& packet, std::chrono::milliseconds delay,
                         std::string reason);
  void close(const std::string& reason);
  void pause_for(std::chrono::milliseconds delay);
  [[nodiscard]] bool note_backpressure(std::size_t disconnect_threshold);

 private:
  void do_read();
  void do_write();
  void schedule_close();

  asio::ip::tcp::socket socket_;
  GatewayServiceBase& owner_;
  asio::steady_timer pause_timer_;
  asio::steady_timer close_timer_;
  std::uint64_t session_id_{0};
  std::string peer_address_{};
  std::array<std::uint8_t, 4096> read_buffer_{};
  std::vector<std::uint8_t> inbound_buffer_{};
  std::deque<std::vector<std::uint8_t>> outbound_frames_{};
  bool write_in_progress_{false};
  bool paused_{false};
  bool closed_{false};
  bool close_after_flush_{false};
  std::chrono::milliseconds close_delay_{0};
  std::string close_reason_{};
  std::size_t overload_strikes_{0};
};

}  // namespace mir2
