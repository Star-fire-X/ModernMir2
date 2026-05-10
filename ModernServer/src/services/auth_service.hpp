#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "core/module.hpp"
#include "protocol/canonical_login_state.hpp"

namespace mir2 {

class AuthService : public Module {
 public:
  AuthService() = default;
  ~AuthService() override {
    stop();
    join();
  }

  [[nodiscard]] std::string name() const override { return "auth_service"; }
  void start(HostContext& context) override;
  void stop() override;
  void join() override;
 [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

 private:
  struct SessionState {
    std::string account_id{};
    std::int32_t certification{0};
    std::int32_t client_version{0};
    std::int64_t last_command_at_ms{0};
    CanonicalLoginStage stage{CanonicalLoginStage::connected};
  };

  enum class PendingAuthRequestKind {
    authenticate_account,
    create_account,
    update_account,
    change_password,
    query_characters,
    create_precheck,
    create_commit,
    delete_character,
    select_character
  };

  struct PendingAuthRequest {
    PendingAuthRequestKind kind{PendingAuthRequestKind::query_characters};
    std::uint64_t session_id{0};
    std::string account_id{};
    std::string character_name{};
    std::int32_t certification{0};
    std::int32_t client_version{0};
    AccountRecord account{};
    CharacterRecord character{};
  };

  struct LoginAdmission {
    std::string account_id{};
    std::string selected_character{};
    std::int32_t certification{0};
    CanonicalLoginStage stage{CanonicalLoginStage::authenticated};
  };

  void run();
  void handle_session_event(const SessionEvent& event);
  void handle_logic_command(const LogicCommand& command);
  void handle_persist_result(const PersistResult& result);
  [[nodiscard]] std::string make_request_id();

  HostContext* context_{nullptr};
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};
  std::thread worker_{};
  std::atomic_bool running_{false};
  std::unordered_map<std::string, PendingAuthRequest> pending_requests_{};
  std::unordered_map<std::uint64_t, SessionState> session_states_{};
  std::unordered_map<std::int32_t, LoginAdmission> admissions_{};
  std::unordered_map<std::string, std::string> last_selected_character_{};
  std::atomic_int32_t next_certification_{1000};
  std::uint64_t next_request_id_{1};
};

}  // namespace mir2
