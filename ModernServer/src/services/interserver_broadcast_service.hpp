#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

#include "asio.hpp"
#include "core/module.hpp"

namespace mir2 {

class InterserverBroadcastService : public Module {
 public:
  [[nodiscard]] std::string name() const override { return "interserver_broadcast_service"; }
  void start(HostContext& context) override;
  void stop() override;
  void join() override;
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

 private:
  void bus_loop();
  void listener_loop();
  void forward_to_peers(const InterserverBroadcast& broadcast);

  HostContext* context_{nullptr};
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};
  std::atomic<bool> running_{false};
  std::thread bus_thread_{};
  std::thread listener_thread_{};
  asio::io_context io_context_{};
  std::unique_ptr<asio::ip::tcp::acceptor> acceptor_{};
  std::atomic<std::uint64_t> forwarded_count_{0};
  std::atomic<std::uint64_t> received_count_{0};
};

}  // namespace mir2
