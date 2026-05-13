#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "protocol/canonical_login_state.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_gateway_service_base.hpp"
#include "storage/repository.hpp"

namespace mir2 {

class ClientV1LoginGatewayService : public ClientV1GatewayServiceBase {
 public:
  explicit ClientV1LoginGatewayService(std::shared_ptr<ClientV1AdmissionRegistry> admissions);

  void start(HostContext& context) override;

 protected:
  PortBinding binding(const HostContext& context) const override;
  void handle_message(std::uint64_t session_id, const std::string& peer_address,
                      std::uint32_t sequence,
                      const client_v1::Message& message) override;
  void handle_connected(std::uint64_t session_id, const std::string& peer_address) override;
  void handle_disconnected(std::uint64_t session_id, const std::string& peer_address,
                           const std::string& reason) override;

 private:
  struct SessionState {
    bool greeted{false};
    bool authenticated{false};
    std::string account_id{};
    std::string display_name{};
    CanonicalLoginStage stage{CanonicalLoginStage::connected};
  };

  struct LobbyAdmission {
    std::string account_id{};
    std::string display_name{};
    std::string server_name{};
    CanonicalLoginStage stage{CanonicalLoginStage::server_selected};
  };

  void handle_client_hello(std::uint64_t session_id, const client_v1::ClientHello& hello);
  void handle_login_request(std::uint64_t session_id, const client_v1::LoginRequest& request);
  void handle_create_account_request(std::uint64_t session_id,
                                     const client_v1::CreateAccountRequest& request);
  void handle_change_password_request(std::uint64_t session_id,
                                      const client_v1::ChangePasswordRequest& request);
  void handle_update_account_request(std::uint64_t session_id,
                                     const client_v1::UpdateAccountRequest& request);
  void handle_select_server_request(std::uint64_t session_id,
                                    const client_v1::SelectServerRequest& request);
  void handle_character_list_request(std::uint64_t session_id,
                                     const client_v1::CharacterListRequest& request);
  void handle_create_character_request(std::uint64_t session_id,
                                       const client_v1::CreateCharacterRequest& request);
  void handle_delete_character_request(std::uint64_t session_id,
                                       const client_v1::DeleteCharacterRequest& request);
  void handle_select_character_request(std::uint64_t session_id,
                                       const client_v1::SelectCharacterRequest& request);

  [[nodiscard]] bool session_ready(std::uint64_t session_id) const;
  [[nodiscard]] std::optional<SessionState> session(std::uint64_t session_id) const;
  [[nodiscard]] static client_v1::CharacterSummary to_summary(const CharacterRecord& character);
  [[nodiscard]] std::string issue_lobby_token(const SessionState& state,
                                              const std::string& server_name);
  [[nodiscard]] std::optional<SessionState> authenticate_lobby_session(
      std::uint64_t session_id, const std::string& lobby_token);
  void send_server_list(std::uint64_t session_id);

  std::shared_ptr<ClientV1AdmissionRegistry> admissions_{};
  std::unique_ptr<Repository> repository_{};
  mutable std::mutex mutex_{};
  std::unordered_map<std::uint64_t, SessionState> sessions_{};
  std::unordered_map<std::string, LobbyAdmission> lobby_admissions_{};
  std::uint64_t next_lobby_token_{1};
};

}  // namespace mir2
