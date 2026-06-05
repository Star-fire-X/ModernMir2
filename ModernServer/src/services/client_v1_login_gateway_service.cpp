/**
 * @file client_v1_login_gateway_service.cpp
 * @brief Client v1 登录网关服务实现
 *
 * @details 实现 ClientV1LoginGatewayService 的全部消息处理逻辑，
 *          包括账号认证、注册、修改密码、更新资料、角色管理等功能。
 *          所有认证操作直接通过 Repository 访问 SQLite 数据库，
 *          无需经过消息总线的异步持久化服务。
 *
 * @note 与旧版 AuthService 不同，Client v1 的登录认证采用同步数据库操作，
 *       直接在网关服务内完成，简化了架构。但账号和角色的核心数据格式
 *       保持与旧版兼容，确保数据互通。
 */

#include "services/client_v1_login_gateway_service.hpp"

#include <algorithm>
#include <chrono>

#include "protocol/canonical_login_error.hpp"
#include "protocol/legacy_string.hpp"
#include "protocol/legacy_types.hpp"

namespace mir2 {

namespace {

/// @brief 最大角色槽位数(与旧版保持一致)
constexpr std::size_t kMaxCharacterSlots = 2;

/**
 * @brief 从创建角色请求创建角色记录
 *
 * @details 设置默认出生地为 0 号地图(比奇省)，默认等级为1，
 *          属性与旧版 make_new_character() 保持一致。
 *
 * @param account_id 账号ID
 * @param request 客户端创建角色请求
 * @return 初始化后的角色记录
 */
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
  character.gold = 0;
  character.ability.level = 1;
  character.ability.hp = 15;
  character.ability.max_hp = 15;
  character.ability.mp = 15;
  character.ability.max_mp = 15;
  character.ability.ac = 0;
  character.ability.mac = 0;
  character.ability.dc = make_word(1, 2);
  character.ability.mc = make_word(1, 2);
  character.ability.sc = make_word(1, 2);
  character.ability.max_exp = 100;
  character.ability.max_weight = 30;
  character.birth_items_granted = false;
  return character;
}

/**
 * @brief 将 AccountRecord 转换为客户端需要的 AccountProfile
 * @param account 账号记录
 * @return 账号资料
 */
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

/**
 * @brief 将客户端提交的 AccountProfile 应用到 AccountRecord
 * @param account [in,out] 要更新的账号记录
 * @param profile 客户端提交的账号资料
 */
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

/**
 * @brief 判断账号是否需要补充资料
 * @param account 账号记录
 * @return true 如果缺少用户名、生日、密保问题等关键信息
 */
bool needs_account_update(const AccountRecord& account) {
  return account.user_name.empty() || account.birthday.empty() || account.quiz.empty() ||
         account.answer.empty() || account.quiz2.empty() || account.answer2.empty();
}

/**
 * @brief 获取 Client v1 协议的错误映射
 * @param kind 登录错误类型
 * @return 映射后的 Client v1 错误响应
 */
CanonicalClientV1LoginErrorResponse client_error(CanonicalLoginErrorKind kind) {
  return canonical_login_error_mapping(kind).client_v1;
}

/**
 * @brief 将标准错误类型应用到结果(带错误码)
 * @tparam Result 结果类型，需有 success、code、error_message 字段
 * @param result [out] 应用错误信息后的结果
 * @param kind 登录错误类型
 */
template <typename Result>
void apply_coded_error(Result& result, CanonicalLoginErrorKind kind) {
  const auto error = client_error(kind);
  result.success = false;
  result.code = error.code;
  result.error_message = std::string(error.text);
}

/**
 * @brief 将标准错误类型应用到结果(仅文本)
 * @tparam Result 结果类型，需有 success、error_message 字段
 * @param result [out] 应用错误信息后的结果
 * @param kind 登录错误类型
 */
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

/**
 * @brief 启动服务
 *
 * @details 初始化 SQLite 数据仓库、确保数据库表结构存在、填充运行时数据，
 *          然后调用基类的 start() 启动 TCP 服务器。
 *
 * @param context 宿主上下文
 */
void ClientV1LoginGatewayService::start(HostContext& context) {
  repository_ = std::make_unique<Repository>(context.root_dir / context.config.runtime.data_dir / "mir2.sqlite");
  repository_->ensure_schema(context.root_dir / "schema" / "mir2.sql");
  repository_->seed_runtime();
  ClientV1GatewayServiceBase::start(context);
}

PortBinding ClientV1LoginGatewayService::binding(const HostContext& context) const {
  return context.config.ports.client_v1_login_gateway;
}

/**
 * @brief 消息路由中心
 *
 * @details 根据消息类型将处理分发给对应的 handle_* 函数。
 *          使用 std::visit 和 constexpr if 在编译期确定消息类型。
 *
 * @param session_id 会话ID
 * @param peer_address 客户端地址
 * @param sequence 序列号
 * @param message 消息体
 */
void ClientV1LoginGatewayService::handle_message(std::uint64_t session_id,
                                                 const std::string& /*peer_address*/,
                                                 std::uint32_t /*sequence*/,
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

/**
 * @brief 处理客户端握手
 *
 * @details 检查协议版本是否匹配，如果不匹配则断开连接。
 *          握手成功后设置 greeted 标志，允许后续操作。
 *
 * @param session_id 会话ID
 * @param hello 客户端握手消息
 */
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

/**
 * @brief 处理登录请求
 *
 * @details 验证账号密码，如果成功则推进登录阶段，
 *          如果账号需要补充资料则发送 NeedUpdateAccount，
 *          否则直接发送服务器列表。
 *
 * @param session_id 会话ID
 * @param request 登录请求
 */
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

/**
 * @brief 处理创建账号请求
 * @param session_id 会话ID
 * @param request 创建账号请求
 */
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

/**
 * @brief 处理更新账号请求
 * @param session_id 会话ID
 * @param request 更新账号请求
 */
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

/**
 * @brief 处理修改密码请求
 * @param session_id 会话ID
 * @param request 修改密码请求
 */
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

/**
 * @brief 处理选择服务器请求
 *
 * @details 检查是否已认证且在正确的阶段，然后生成 Lobby 令牌
 *          并返回服务器地址和端口信息。
 *
 * @param session_id 会话ID
 * @param request 选择服务器请求
 */
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

/**
 * @brief 处理角色列表请求
 *
 * @details 如果已认证直接返回角色列表，否则尝试使用 lobby_token
 *          进行免认证访问(从服务器选择页面跳转回来的情况)。
 *
 * @param session_id 会话ID
 * @param request 角色列表请求
 */
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

/**
 * @brief 处理创建角色请求
 *
 * @details 检查角色名是否合规、是否已存在、角色槽位是否已满，
 *          然后创建角色记录。
 *
 * @param session_id 会话ID
 * @param request 创建角色请求
 */
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

/**
 * @brief 处理删除角色请求
 * @param session_id 会话ID
 * @param request 删除角色请求
 */
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

/**
 * @brief 处理选择角色请求
 *
 * @details 验证角色存在后，从准入注册表签发进入游戏世界的令牌，
 *          返回游戏网关的地址和端口给客户端。
 *
 * @param session_id 会话ID
 * @param request 选择角色请求
 */
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

/**
 * @brief 检查会话是否就绪(已握手)
 * @param session_id 会话ID
 * @return true 如果会话已收到 ClientHello 且 greeted 为 true
 */
bool ClientV1LoginGatewayService::session_ready(std::uint64_t session_id) const {
  const auto state = session(session_id);
  return state.has_value() && state->greeted;
}

/**
 * @brief 获取会话状态(线程安全)
 * @param session_id 会话ID
 * @return 会话状态的副本，如果不存在则返回 std::nullopt
 */
std::optional<ClientV1LoginGatewayService::SessionState> ClientV1LoginGatewayService::session(
    std::uint64_t session_id) const {
  std::scoped_lock lock(mutex_);
  const auto it = sessions_.find(session_id);
  if (it == sessions_.end()) {
    return std::nullopt;
  }
  return it->second;
}

/**
 * @brief 将 CharacterRecord 转换为客户端的 CharacterSummary
 * @param character 角色记录
 * @return 角色摘要信息
 */
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

/**
 * @brief 签发 Lobby 令牌(从服务器选择页面免认证返回)
 * @param state 会话状态
 * @param server_name 服务器名
 * @return Lobby 令牌字符串
 */
std::string ClientV1LoginGatewayService::issue_lobby_token(const SessionState& state,
                                                           const std::string& server_name) {
  std::scoped_lock lock(mutex_);
  auto token = std::to_string(next_lobby_token_++) + ":" + state.account_id + ":" + server_name;
  lobby_admissions_[token] =
      LobbyAdmission{state.account_id, state.display_name, server_name, state.stage};
  return token;
}

/**
 * @brief 使用 Lobby 令牌认证会话
 * @param session_id 会话ID
 * @param lobby_token Lobby 令牌
 * @return 如果令牌有效则返回更新后的会话状态
 */
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

/**
 * @brief 发送服务器列表给客户端
 * @param session_id 会话ID
 */
void ClientV1LoginGatewayService::send_server_list(std::uint64_t session_id) {
  client_v1::ServerList servers;
  servers.servers.push_back(client_v1::ServerEntry{
      "ModernServer", context().config.ports.client_v1_login_gateway.address,
      context().config.ports.client_v1_login_gateway.port});
  send_message(session_id, servers);
}

}  // namespace mir2
