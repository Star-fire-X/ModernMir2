#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "asio.hpp"
#include "config/models.hpp"
#include "core/module.hpp"
#include "protocol/session_router.hpp"

namespace mir2 {

class GameSession;

class GatewayServiceBase : public Module {
 public:
  explicit GatewayServiceBase(std::string module_name);
  ~GatewayServiceBase() override;

  [[nodiscard]] std::string name() const override { return module_name_; }
  void start(HostContext& context) override;
  void stop() override;
  void join() override;
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

  void notify_connected(std::uint64_t session_id, const std::string& peer_address);
  void notify_disconnected(std::uint64_t session_id, const std::string& peer_address,
                           const std::string& reason);
  bool forward_packet(std::uint64_t session_id, const std::string& peer_address,
                      const LegacyPacket& packet, const std::shared_ptr<GameSession>& session);
  void remove_session(std::uint64_t session_id);

 protected:
  virtual PortBinding binding(const HostContext& context) const = 0;
  virtual std::string ingress_target() const = 0;
  [[nodiscard]] std::uint64_t allocate_session_id();

 private:
  void do_accept();
  void bus_loop();

  std::string module_name_{};
  HostContext* context_{nullptr};
  SessionRouter router_{};
  asio::io_context io_context_{};
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_{};
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_{};
  std::vector<std::thread> io_threads_{};
  std::thread bus_thread_{};
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};
  mutable std::mutex mutex_{};
  std::unordered_map<std::uint64_t, std::shared_ptr<GameSession>> sessions_{};
  std::atomic_bool running_{false};
  std::atomic_uint64_t next_session_id_{1};
};

}  // namespace mir2
