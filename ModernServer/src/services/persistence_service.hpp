#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <unordered_map>

#include "core/module.hpp"
#include "storage/repository.hpp"

namespace mir2 {

class PersistenceService : public Module {
 public:
  PersistenceService() = default;
  ~PersistenceService() override {
    stop();
    join();
  }

  [[nodiscard]] std::string name() const override { return "persistence_service"; }
  void start(HostContext& context) override;
  void stop() override;
  void join() override;
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

 private:
  void run();
  void handle_request(const PersistRequest& request);

  HostContext* context_{nullptr};
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};
  std::unique_ptr<Repository> repository_{};
  std::thread worker_{};
  std::atomic_bool running_{false};
  std::size_t handled_requests_{0};
  PersistRequestKind last_request_kind_{PersistRequestKind::ensure_schema};
  std::string last_request_reply_to_{};
  std::string last_request_id_{};
};

}  // namespace mir2
