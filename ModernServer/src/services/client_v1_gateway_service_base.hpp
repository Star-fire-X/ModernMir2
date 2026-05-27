#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "asio.hpp"
#include "config/models.hpp"
#include "core/module.hpp"
#include "protocol/client_v1_session.hpp"
#include "shared/protocol/client_v1/protocol.hpp"

namespace mir2 {

class ClientV1GatewayServiceBase : public Module, public ClientV1SessionOwner {
 public:
  explicit ClientV1GatewayServiceBase(std::string module_name);
  ~ClientV1GatewayServiceBase() override;

  [[nodiscard]] std::string name() const override { return module_name_; }
  void start(HostContext& context) override;
  void stop() override;
  void join() override;
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

  void on_client_v1_connected(std::uint64_t session_id, const std::string& peer_address) override;
  void on_client_v1_disconnected(std::uint64_t session_id, const std::string& peer_address,
                                 const std::string& reason) override;
  void on_client_v1_message(std::uint64_t session_id, const std::string& peer_address,
                            std::uint32_t sequence,
                            const client_v1::Message& message) override;

 protected:
  virtual PortBinding binding(const HostContext& context) const = 0;
  virtual void handle_message(std::uint64_t session_id, const std::string& peer_address,
                              std::uint32_t sequence,
                              const client_v1::Message& message) = 0;
  virtual void handle_connected(std::uint64_t session_id, const std::string& peer_address) = 0;
  virtual void handle_disconnected(std::uint64_t session_id, const std::string& peer_address,
                                   const std::string& reason) = 0;

  void send_message(std::uint64_t session_id, const client_v1::Message& message);
  void send_message(std::uint64_t session_id, const client_v1::Message& message,
                    std::chrono::milliseconds delay);
  void send_frame(std::uint64_t session_id, const client_v1::Frame& frame);
  void send_frames(std::uint64_t session_id, const std::vector<client_v1::Frame>& frames);
  void send_frames(std::uint64_t session_id, const std::vector<client_v1::Frame>& frames,
                   std::chrono::milliseconds delay);
  void disconnect(std::uint64_t session_id, std::uint16_t code, const std::string& reason);
  [[nodiscard]] HostContext& context() const { return *context_; }

 private:
  void do_accept();

  std::string module_name_{};
  HostContext* context_{nullptr};
  asio::io_context io_context_{};
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_{};
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_{};
  std::vector<std::thread> io_threads_{};
  mutable std::mutex mutex_{};
  std::unordered_map<std::uint64_t, std::shared_ptr<ClientV1Session>> sessions_{};
  std::unordered_map<std::uint64_t, std::uint32_t> client_frame_sequences_{};
  std::atomic_bool running_{false};
  std::atomic_uint64_t next_session_id_{1};
};

}  // namespace mir2
