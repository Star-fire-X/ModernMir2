#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "asio.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2 {

class ClientV1SessionOwner {
 public:
  virtual ~ClientV1SessionOwner() = default;
  virtual void on_client_v1_connected(std::uint64_t session_id,
                                      const std::string& peer_address) = 0;
  virtual void on_client_v1_disconnected(std::uint64_t session_id, const std::string& peer_address,
                                         const std::string& reason) = 0;
  virtual void on_client_v1_message(std::uint64_t session_id, const std::string& peer_address,
                                    std::uint32_t sequence,
                                    const client_v1::Message& message) = 0;
};

class ClientV1Session : public std::enable_shared_from_this<ClientV1Session> {
 public:
  ClientV1Session(asio::ip::tcp::socket socket, ClientV1SessionOwner& owner);

  void start(std::uint64_t session_id);
  void send(const client_v1::Message& message);
  void send(const client_v1::Message& message, std::chrono::milliseconds delay);
  void send_frame(const client_v1::Frame& frame);
  void send_frames(const std::vector<client_v1::Frame>& frames);
  void send_frames(const std::vector<client_v1::Frame>& frames,
                   std::chrono::milliseconds delay);
  void send_disconnect_and_close(std::uint16_t code, std::string reason);
  void close(std::string reason);

 private:
  void do_read();
  void do_write();
  void queue_disconnect_and_close(std::uint16_t code, std::string reason);
  void notify_closed(const std::string& reason);

  asio::ip::tcp::socket socket_;
  asio::strand<asio::any_io_executor> strand_;
  ClientV1SessionOwner& owner_;
  asio::steady_timer close_timer_;
  std::uint64_t session_id_{0};
  std::string peer_address_{"unknown"};
  std::uint32_t next_sequence_{1};
  std::array<std::uint8_t, 8192> read_buffer_{};
  std::vector<std::uint8_t> inbound_buffer_{};
  std::deque<std::vector<std::uint8_t>> outbound_frames_{};
  bool writing_{false};
  bool closed_{false};
  bool close_after_write_{false};
  std::string close_reason_{};
};

}  // namespace mir2
