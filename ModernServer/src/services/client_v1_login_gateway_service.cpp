#include "services/client_v1_login_gateway_service.hpp"

#include <algorithm>
#include <chrono>

#include "protocol/canonical_login_error.hpp"
#include "protocol/legacy_string.hpp"

namespace mir2 {

namespace {

constexpr std::size_t kMaxCharacterSlots = 2;

CharacterRecord make_character(const std::string& account_id,
                               const client_v1::CreateCharacterRequest& request) {
  CharacterRecord character;
  character.account_id = account_id;
  character.character_name = copy_legacy_bytes(request.name);
  character.map_id = "0";
  character.x = 330;
  character.y = 270;
  character.dir = 0;
  character.light = 0;
  character.job = request.job;
  character.sex = request.sex;
  character.hair = request.hair;
  character.feature =
      make_feature(0, request.sex, request.sex,
                   static_cast<std::uint8_t>(std::clamp(static_cast<int>(request.hair) * 2 +
                                                            static_cast<int>(request.sex),
                                                        0, 255)));
  character.gold = 2000;
  character.ability.level = 1;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = 15;
  character.ability.max_mp = 15;
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.ability.max_wear_weight = 100;
  character.ability.max_hand_weight = 100;
  return character;
}

client_v1::AccountProfile to_profile(const AccountRecord& account) {
  client_v1::AccountProfile profile;
  profile.display_name = account.display_name.empty() ? account.account_id : account.display_name;
  profile.user_name = account.user_name.empty() ? profile.display_name : account.user_name;
  profile.ss_no = account.ss_no;
  profile.birthday = account.birthday;
  profile.quiz = account.quiz;
  profile.answer = account.answer;
  profile.quiz2 = account.quiz2;
  profile.answer2 = account.answer2;
  profile.phone = account.phone;
  profile.mobile_phone = account.mobile_phone;
  profile.email = account.email;
  profile.memo1 = account.memo1;
  profile.memo2 = account.memo2;
  return profile;
}

void apply_profile(AccountRecord& account, const client_v1::AccountProfile& profile) {
  account.display_name = profile.display_name.empty() ? account.account_id : profile.display_name;
  account.user_name = profile.user_name.empty() ? account.display_name : profile.user_name;
  account.ss_no = profile.ss_no;
  account.birthday = profile.birthday;
  account.quiz = profile.quiz;
  account.answer = profile.answer;
  account.quiz2 = profile.quiz2;
  account.answer2 = profile.answer2;
  account.phone = profile.phone;
  account.mobile_phone = profile.mobile_phone;
  account.email = profile.email;
  account.memo1 = profile.memo1;
  account.memo2 = profile.memo2;
}

bool needs_account_update(const AccountRecord& account) {
  return account.user_name.empty() || account.birthday.empty() || account.quiz.empty() ||
         account.answer.empty() || account.quiz2.empty() || account.answer2.empty();
}

CanonicalClientV1LoginErrorResponse client_error(CanonicalLoginErrorKind kind) {
  return canonical_login_error_mapping(kind).client_v1;
}

template <typename Result>
void apply_coded_error(Result& result, CanonicalLoginErrorKind kind) {
  const auto error = client_error(kind);
  result.success = false;
  result.code = error.code;
  result.error_message = std::string(error.text);
}

template <typename Result>
void apply_text_error(Result& result, CanonicalLoginErrorKind kind) {
  const auto error = client_error(kind);
  result.success = false;
  result.error_message = std::string(error.text);
}

}  // namespace

ClientV1LoginGatewayService::ClientV1LoginGatewayService(
    std::shared_ptr<ClientV1AdmissionRegistry> admissions)
    : ClientV1GatewayServiceBase("client_v1_login_gateway"), admissions_(std::move(admissions)) {}

void ClientV1LoginGatewayService::start(HostContext& context) {
  repository_ = std::make_unique<Repository>(context.root_dir / context.config.runtime.data_dir / "mir2.sqlite");
  repository_->ensure_schema(context.root_dir / "schema" / "mir2.sql");
  repository_->seed_runtime();
  ClientV1GatewayServiceBase::start(context);
}

PortBinding ClientV1LoginGatewayService::binding(const HostContext& context) const {
  return context.config.ports.client_v1_login_gateway;
}

void ClientV1LoginGatewayService::handle_message(std::uint64_t session_id,
                                                 const std::string& /*peer_address*/,
                                                 const client_v1::Message& message) {
  std::visit(
      [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, client_v1::ClientHello>) {
          handle_client_hello(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::LoginRequest>) {
          handle_login_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::CreateAccountRequest>) {
          handle_create_account_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::ChangePasswordRequest>) {
          handle_change_password_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::UpdateAccountRequest>) {
          handle_update_account_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::SelectServerRequest>) {
          handle_select_server_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::CharacterListRequest>) {
          handle_character_list_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::CreateCharacterRequest>) {
          handle_create_character_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::DeleteCharacterRequest>) {
          handle_delete_character_request(session_id, value);
        } else if constexpr (std::is_same_v<T, client_v1::SelectCharacterRequest>) {
          handle_select_character_request(session_id, value);
        } else {
          const auto error = client_error(CanonicalLoginErrorKind::unsupported_login_message);
          disconnect(session_id, error.code, std::string(error.text));
        }
      },
      message);
}

void ClientV1LoginGatewayService::handle_connected(std::uint64_t session_id,
                                                   const std::string& /*peer_address*/) {
  std::scoped_lock lock(mutex_);
  sessions_[session_id] = SessionState{};
}

void ClientV1LoginGatewayService::handle_disconnected(std::uint64_t session_id,
                                                      const std::string& /*peer_address*/,
                                                      const std::string& /*reason*/) {
  std::scoped_lock lock(mutex_);
  sessions_.erase(session_id);
}

void ClientV1LoginGatewayService::handle_client_hello(std::uint64_t session_id,
                                                      const client_v1::ClientHello& hello) {
  if (hello.protocol_version != client_v1::kProtocolVersion) {
    const auto error = client_error(CanonicalLoginErrorKind::protocol_version_mismatch);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }
  std::scoped_lock lock(mutex_);
  sessions_[session_id].greeted = true;
}

void ClientV1LoginGatewayService::handle_login_request(std::uint64_t session_id,
                                                       const client_v1::LoginRequest& request) {
  if (!session_ready(session_id)) {
    const auto error = client_error(CanonicalLoginErrorKind::missing_client_hello);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  const auto account_id = copy_legacy_bytes(request.account_id);
  const auto password = copy_legacy_bytes(request.password);
  const auto result = repository_->authenticate_account(
      account_id, password,
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());

  client_v1::LoginResult response;
  response.success = result.status_code == 1 && result.account.has_value();
  response.code = result.status_code;
  if (result.account.has_value()) {
    response.account_id = result.account->account_id;
    response.display_name =
        result.account->display_name.empty() ? result.account->account_id : result.account->display_name;
  }
  if (!response.success) {
    apply_coded_error(response, canonical_login_error_from_login_result_code(result.status_code));
  }
  send_message(session_id, response);

  if (!response.success) {
    return;
  }

  {
    std::scoped_lock lock(mutex_);
    auto& session_state = sessions_[session_id];
    session_state.authenticated = true;
    session_state.account_id = response.account_id;
    session_state.display_name = response.display_name;
    session_state.stage =
        advance(session_state.stage, CanonicalLoginTransition::authenticate);
  }

  if (needs_account_update(*result.account)) {
    send_message(session_id, client_v1::NeedUpdateAccount{
                                 result.account->account_id, to_profile(*result.account),
                                 "account_profile_required"});
    return;
  }

  send_server_list(session_id);
}

void ClientV1LoginGatewayService::handle_create_account_request(
    std::uint64_t session_id, const client_v1::CreateAccountRequest& request) {
  if (!session_ready(session_id)) {
    const auto error = client_error(CanonicalLoginErrorKind::missing_client_hello);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  AccountRecord account;
  account.account_id = copy_legacy_bytes(request.account_id);
  account.password = copy_legacy_bytes(request.password);
  apply_profile(account, request.profile);

  client_v1::CreateAccountResult result;
  if (!is_valid_legacy_account_id(account.account_id)) {
    apply_coded_error(result, CanonicalLoginErrorKind::create_account_failed);
    send_message(session_id, result);
    return;
  }
  result.success = repository_->create_account(account);
  result.code = result.success ? 1 : 0;
  if (!result.success) {
    apply_coded_error(result, CanonicalLoginErrorKind::create_account_failed);
  }
  send_message(session_id, result);
}

void ClientV1LoginGatewayService::handle_update_account_request(
    std::uint64_t session_id, const client_v1::UpdateAccountRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->authenticated) {
    const auto error = client_error(CanonicalLoginErrorKind::not_authenticated);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }
  const auto account_id = copy_legacy_bytes(request.account_id);
  if (account_id != state->account_id) {
    const auto error = client_error(CanonicalLoginErrorKind::account_mismatch);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  client_v1::UpdateAccountResult result;
  auto account = repository_->load_account(account_id);
  if (!account.has_value()) {
    apply_coded_error(result, CanonicalLoginErrorKind::update_account_missing);
    send_message(session_id, result);
    return;
  }

  if (!request.password.empty()) {
    account->password = copy_legacy_bytes(request.password);
  }
  apply_profile(*account, request.profile);

  result.success = repository_->update_account(*account);
  result.code = result.success ? 1 : 0;
  if (!result.success) {
    apply_coded_error(result, CanonicalLoginErrorKind::update_account_failed);
    send_message(session_id, result);
    return;
  }

  {
    std::scoped_lock lock(mutex_);
    auto& session_state = sessions_[session_id];
    session_state.display_name =
        account->display_name.empty() ? account->account_id : account->display_name;
  }
  send_message(session_id, result);
  send_server_list(session_id);
}

void ClientV1LoginGatewayService::handle_change_password_request(
    std::uint64_t session_id, const client_v1::ChangePasswordRequest& request) {
  if (!session_ready(session_id)) {
    const auto error = client_error(CanonicalLoginErrorKind::missing_client_hello);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  client_v1::ChangePasswordResult result;
  result.code = repository_->change_password(
      copy_legacy_bytes(request.account_id), copy_legacy_bytes(request.password),
      copy_legacy_bytes(request.new_password),
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  result.success = result.code == 1;
  if (!result.success) {
    apply_coded_error(result,
                      canonical_login_error_from_change_password_result_code(result.code));
  }
  send_message(session_id, result);
}

void ClientV1LoginGatewayService::handle_select_server_request(
    std::uint64_t session_id, const client_v1::SelectServerRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->authenticated ||
      !can_accept(state->stage, CanonicalLoginRequest::select_server)) {
    const auto error = client_error(CanonicalLoginErrorKind::select_server_rejected);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  client_v1::SelectServerResult result;
  result.name = request.name;
  const client_v1::ServerEntry entry{
      "ModernServer", context().config.ports.client_v1_login_gateway.address,
      context().config.ports.client_v1_login_gateway.port};
  if (request.name.empty() || request.name == entry.name) {
    auto selected_state = *state;
    selected_state.stage = advance(selected_state.stage, CanonicalLoginTransition::select_server);
    {
      std::scoped_lock lock(mutex_);
      sessions_[session_id].stage = selected_state.stage;
    }
    result.success = true;
    result.name = entry.name;
    result.address = entry.address;
    result.port = entry.port;
    result.lobby_token = issue_lobby_token(selected_state, entry.name);
  } else {
    apply_text_error(result, CanonicalLoginErrorKind::server_not_found);
  }
  send_message(session_id, result);
}

void ClientV1LoginGatewayService::handle_character_list_request(
    std::uint64_t session_id, const client_v1::CharacterListRequest& request) {
  auto state = session(session_id);
  if ((!state.has_value() || !state->authenticated) && !request.lobby_token.empty()) {
    state = authenticate_lobby_session(session_id, request.lobby_token);
  }
  if (!state.has_value() || !state->authenticated ||
      !can_accept(state->stage, CanonicalLoginRequest::query_characters)) {
    const auto error = client_error(CanonicalLoginErrorKind::query_characters_rejected);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  client_v1::CharacterList response;
  const auto characters = repository_->list_characters(state->account_id);
  for (const auto& character : characters) {
    response.characters.push_back(to_summary(character));
  }
  if (!response.characters.empty()) {
    response.selected_name = response.characters.front().name;
  }
  send_message(session_id, response);
}

void ClientV1LoginGatewayService::handle_create_character_request(
    std::uint64_t session_id, const client_v1::CreateCharacterRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->authenticated ||
      !can_accept(state->stage, CanonicalLoginRequest::create_character)) {
    const auto error = client_error(CanonicalLoginErrorKind::create_character_rejected);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  client_v1::CreateCharacterResult result;
  if (!is_valid_legacy_character_name(request.name)) {
    apply_coded_error(result, CanonicalLoginErrorKind::invalid_character_name);
    send_message(session_id, result);
    return;
  }
  const auto characters = repository_->list_characters(state->account_id);
  const auto character_name = copy_legacy_bytes(request.name);
  const auto exists = std::any_of(characters.begin(), characters.end(),
                                  [&](const CharacterRecord& character) {
                                    return character.character_name == character_name;
                                  });
  if (exists) {
    apply_coded_error(result, CanonicalLoginErrorKind::create_character_duplicate);
    send_message(session_id, result);
    return;
  }
  if (characters.size() >= kMaxCharacterSlots) {
    apply_coded_error(result, CanonicalLoginErrorKind::character_slots_full);
    send_message(session_id, result);
    return;
  }

  auto character = make_character(state->account_id, request);
  result.success = repository_->create_character(character);
  result.code = result.success ? 1 : 0;
  result.character = to_summary(character);
  if (!result.success) {
    apply_coded_error(result, CanonicalLoginErrorKind::create_character_failed);
  }
  send_message(session_id, result);
}

void ClientV1LoginGatewayService::handle_delete_character_request(
    std::uint64_t session_id, const client_v1::DeleteCharacterRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->authenticated ||
      !can_accept(state->stage, CanonicalLoginRequest::delete_character)) {
    const auto error = client_error(CanonicalLoginErrorKind::delete_character_rejected);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  client_v1::DeleteCharacterResult result;
  const auto character_name = copy_legacy_bytes(request.name);
  result.success = repository_->delete_character(state->account_id, character_name);
  result.code = result.success ? 1 : 0;
  result.deleted_name = character_name;
  if (!result.success) {
    apply_coded_error(result, CanonicalLoginErrorKind::delete_character_failed);
  }
  send_message(session_id, result);
}

void ClientV1LoginGatewayService::handle_select_character_request(
    std::uint64_t session_id, const client_v1::SelectCharacterRequest& request) {
  const auto state = session(session_id);
  if (!state.has_value() || !state->authenticated ||
      !can_accept(state->stage, CanonicalLoginRequest::select_character)) {
    const auto error = client_error(CanonicalLoginErrorKind::select_character_rejected);
    disconnect(session_id, error.code, std::string(error.text));
    return;
  }

  const auto character_name = copy_legacy_bytes(request.name);
  const auto character = repository_->load_character(state->account_id, character_name);
  client_v1::SelectCharacterResult result;
  result.character_name = character_name;
  if (!character.has_value()) {
    apply_text_error(result, CanonicalLoginErrorKind::character_not_found);
    send_message(session_id, result);
    return;
  }

  result.success = true;
  result.enter_world_token = admissions_->issue(state->account_id, character_name);
  result.address = context().config.ports.client_v1_game_gateway.address;
  result.port = context().config.ports.client_v1_game_gateway.port;
  {
    std::scoped_lock lock(mutex_);
    sessions_[session_id].stage =
        advance(sessions_[session_id].stage, CanonicalLoginTransition::select_character);
  }
  send_message(session_id, result);
}

bool ClientV1LoginGatewayService::session_ready(std::uint64_t session_id) const {
  const auto state = session(session_id);
  return state.has_value() && state->greeted;
}

std::optional<ClientV1LoginGatewayService::SessionState> ClientV1LoginGatewayService::session(
    std::uint64_t session_id) const {
  std::scoped_lock lock(mutex_);
  const auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return std::nullopt;
  }
  return it->second;
}

client_v1::CharacterSummary ClientV1LoginGatewayService::to_summary(
    const CharacterRecord& character) {
  client_v1::CharacterSummary summary;
  summary.name = character.character_name;
  summary.level = character.ability.level;
  summary.job = character.job;
  summary.sex = character.sex;
  summary.hair = character.hair;
  summary.map_id = character.map_id;
  return summary;
}

std::string ClientV1LoginGatewayService::issue_lobby_token(const SessionState& state,
                                                           const std::string& server_name) {
  std::scoped_lock lock(mutex_);
  auto token = std::to_string(next_lobby_token_++) + ":" + state.account_id + ":" + server_name;
  lobby_admissions_[token] =
      LobbyAdmission{state.account_id, state.display_name, server_name, state.stage};
  return token;
}

std::optional<ClientV1LoginGatewayService::SessionState>
ClientV1LoginGatewayService::authenticate_lobby_session(std::uint64_t session_id,
                                                        const std::string& lobby_token) {
  std::scoped_lock lock(mutex_);
  auto session_it = sessions_.find(session_id);
  if (session_it == sessions_.end() || !session_it->second.greeted) {
    return std::nullopt;
  }
  const auto token_it = lobby_admissions_.find(lobby_token);
  if (token_it == lobby_admissions_.end()) {
    return std::nullopt;
  }

  auto admission = token_it->second;
  lobby_admissions_.erase(token_it);
  auto& state = session_it->second;
  state.authenticated = true;
  state.account_id = admission.account_id;
  state.display_name = admission.display_name;
  state.stage = admission.stage;
  return state;
}

void ClientV1LoginGatewayService::send_server_list(std::uint64_t session_id) {
  client_v1::ServerList servers;
  servers.servers.push_back(client_v1::ServerEntry{
      "ModernServer", context().config.ports.client_v1_login_gateway.address,
      context().config.ports.client_v1_login_gateway.port});
  send_message(session_id, servers);
}

}  // namespace mir2
