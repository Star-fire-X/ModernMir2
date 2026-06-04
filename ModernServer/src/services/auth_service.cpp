/**
 * @file auth_service.cpp
 * @brief 用户认证服务实现
 *
 * @details 实现 AuthService 类的全部接口，包括与登录网关、持久化服务的
 *          异步消息通信、请求节流、重复登录检测、状态机驱动等核心逻辑。
 *          处理所有与账号认证相关的客户端请求，包括密码认证、注册、更新、
 *          密码修改、角色管理(查询/创建/删除/选择)等操作。
 *
 * @note 认证流程设计要点：
 *       1. 请求节流：同一会话的账号操作间隔不少于5秒
 *       2. 重复登录检测：同一账号不允许同时在线，会强制踢出旧会话
 *       3. 请求-响应匹配：使用 request_id 实现异步回调的精确匹配
 *       4. 审计日志：所有认证操作均记录审计事件
 */

#include "services/auth_service.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>

#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/canonical_login_error.hpp"
#include "protocol/legacy_string.hpp"
#include "util/string_utils.hpp"

namespace mir2 {

namespace {

/// @brief 最大角色槽位数
constexpr std::size_t kMaxCharacterSlots = 2;

/// @brief 账号指令节流间隔(毫秒)，同一会话的指令间隔不可小于此值
constexpr std::int64_t kAccountCommandThrottleMs = 5 * 1000;

/**
 * @brief 构建标准响应数据包
 * @param session_id 会话ID
 * @param ident 消息标识符
 * @param recog 识别码(默认为0)
 * @param param 参数(默认为0)
 * @param tag 标签(默认为0)
 * @param series 系列号(默认为0)
 * @param body 消息体(默认为空)
 * @return 构建好的 LegacyPacket 数据包
 */
LegacyPacket make_response_packet(std::uint64_t session_id, std::uint16_t ident,
                                  std::int32_t recog = 0, std::uint16_t param = 0,
                                  std::uint16_t tag = 0, std::uint16_t series = 0,
                                  const std::string& body = {}) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(ident, recog, param, tag, series), body);
}

/**
 * @brief 构建错误响应数据包
 * @param session_id 会话ID
 * @param kind 登录错误类型
 * @return 根据错误类型映射构建的错误响应数据包
 */
LegacyPacket make_error_response_packet(std::uint64_t session_id,
                                        CanonicalLoginErrorKind kind) {
  const auto response = canonical_login_error_mapping(kind).legacy;
  return make_response_packet(session_id, response.ident, response.recog, response.param,
                              response.tag, response.series);
}

/**
 * @brief 向登录网关发送数据包
 * @param context 宿主上下文，用于获取消息总线
 * @param session_id 目标会话ID
 * @param packet 要发送的 LegacyPacket 数据包
 */
void post_gateway_packet(HostContext& context, std::uint64_t session_id, LegacyPacket packet) {
  context.bus->post("login_gateway",
                    SessionEvent{SessionEventKind::send_packet, "login_gateway", session_id, {},
                                 std::move(packet), {}});
}

/**
 * @brief 记录审计事件
 * @param context 宿主上下文
 * @param category 审计类别
 * @param message 审计消息
 * @param session_key 会话标识键
 */
void audit(HostContext& context, std::string category, std::string message, std::string session_key) {
  context.bus->post("log_service",
                    AuditEvent{std::move(category), std::move(message), std::move(session_key)});
}

/**
 * @brief 分割遗留编码的字段字符串
 * @param text 输入的遗留编码文本视图
 * @param delimiter 分隔符
 * @return 分割后的字符串向量
 */
std::vector<std::string> split_fields(std::string_view text, char delimiter) {
  std::vector<std::string> fields;
  for (const auto& field : split_legacy_fields(LegacyStringView{text}, delimiter)) {
    fields.push_back(copy_legacy_bytes(field.bytes()));
  }
  return fields;
}

/**
 * @brief 将字符串解析为 int32_t
 * @param text 待解析的字符串
 * @return 如果解析成功则返回 int32_t 值，否则返回 std::nullopt
 */
std::optional<std::int32_t> parse_i32(std::string_view text) {
  std::int32_t value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

/**
 * @brief 获取当前时间戳(毫秒)
 * @return 当前系统时间的毫秒数
 */
std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

/**
 * @brief 判断是否应该对指令进行节流(限速)
 * @param last_command_at_ms 上一次指令的时间戳(引用，会被更新)
 * @param current_ms 当前时间戳
 * @return true 表示应节流(请求被忽略)，false 表示允许执行
 */
bool should_throttle(std::int64_t& last_command_at_ms, std::int64_t current_ms) {
  if (last_command_at_ms != 0 && current_ms - last_command_at_ms <= kAccountCommandThrottleMs) {
    return true;
  }
  last_command_at_ms = current_ms;
  return false;
}

/**
 * @brief 检查账号是否需要补充信息
 * @param account 账号记录
 * @return true 如果用户名为空或密保问题2为空，需要补充信息
 */
bool requires_account_update(const AccountRecord& account) {
  return account.user_name.empty() || account.quiz2.empty();
}

/**
 * @brief 获取指定类型的遗留编码缓冲区大小
 * @tparam T 要编码的结构体类型
 * @return 编码后的缓冲区大小(字节)
 */
template <typename T>
std::size_t encoded_buffer_size() {
  static const auto size = [] {
    const T value{};
    return legacy_encode_buffer(&value, sizeof(T)).size();
  }();
  return size;
}

/**
 * @brief 解码用户注册信息体
 * @param body 编码的消息体
 * @param info [out] 解码后的用户基本信息
 * @param add_info [out] 解码后的用户附加信息
 * @return true 解码成功，false 数据不足或解码失败
 */
bool decode_user_entry_body(std::string_view body, LegacyUserEntryInfo& info,
                            LegacyUserEntryAddInfo& add_info) {
  const auto info_size = encoded_buffer_size<LegacyUserEntryInfo>();
  if (body.size() < info_size) {
    return false;
  }
  const auto info_body = body.substr(0, info_size);
  const auto add_body = body.substr(info_size);
  return legacy_decode_buffer(info_body, &info, sizeof(info)) &&
         legacy_decode_buffer(add_body, &add_info, sizeof(add_info));
}

/**
 * @brief 从遗留编码的用户信息创建 AccountRecord
 * @param info 遗留编码的用户基本信息
 * @param add_info 遗留编码的用户附加信息
 * @return 转换后的 AccountRecord
 */
AccountRecord make_account_record(const LegacyUserEntryInfo& info, const LegacyUserEntryAddInfo& add_info) {
  AccountRecord account;
  account.account_id = to_string(info.login_id);
  account.password = to_string(info.password);
  account.user_name = to_string(info.user_name);
  account.display_name = account.user_name.empty() ? account.account_id : account.user_name;
  account.ss_no = to_string(info.ss_no);
  account.phone = to_string(info.phone);
  account.quiz = to_string(info.quiz);
  account.answer = to_string(info.answer);
  account.email = to_string(info.email);
  account.quiz2 = to_string(add_info.quiz2);
  account.answer2 = to_string(add_info.answer2);
  account.birthday = to_string(add_info.birthday);
  account.mobile_phone = to_string(add_info.mobile_phone);
  account.memo1 = to_string(add_info.memo1);
  account.memo2 = to_string(add_info.memo2);
  return account;
}

/**
 * @brief 从 AccountRecord 创建遗留编码的用户信息
 * @param account 账号记录
 * @return 填充好的 LegacyUserEntryInfo 结构体
 */
LegacyUserEntryInfo make_user_entry_info(const AccountRecord& account) {
  LegacyUserEntryInfo info;
  set_short_string(info.login_id, account.account_id);
  set_short_string(info.password, account.password);
  set_short_string(info.user_name, account.user_name);
  set_short_string(info.ss_no, account.ss_no);
  set_short_string(info.phone, account.phone);
  set_short_string(info.quiz, account.quiz);
  set_short_string(info.answer, account.answer);
  set_short_string(info.email, account.email);
  return info;
}

/**
 * @brief 创建新的角色记录(默认出生在比奇省 330:270)
 * @param account_id 所属账号ID
 * @param character_name 角色名
 * @param job 职业
 * @param sex 性别
 * @param hair 发型
 * @return 初始化好的角色记录(1级,默认属性)
 */
CharacterRecord make_new_character(const std::string& account_id, const std::string& character_name,
                                   std::uint8_t job, std::uint8_t sex, std::uint8_t hair) {
  CharacterRecord record;
  record.account_id = account_id;
  record.character_name = character_name;
  record.map_id = "0";
  record.x = 330;
  record.y = 270;
  record.dir = 0;
  record.light = 0;
  record.job = job;
  record.sex = sex;
  record.hair = hair;
  record.gold = 0;
  record.feature = 0;
  record.status = 0;
  record.ability.level = 1;
  record.ability.hp = 15;
  record.ability.mp = 15;
  record.ability.max_hp = 15;
  record.ability.max_mp = 15;
  record.ability.ac = 0;
  record.ability.mac = 0;
  record.ability.dc = make_word(1, 2);
  record.ability.mc = make_word(1, 2);
  record.ability.sc = make_word(1, 2);
  record.ability.max_exp = 100;
  record.ability.max_weight = 30;
  record.birth_items_granted = false;
  return record;
}

/**
 * @brief 编码选择服务器响应体
 * @param context 宿主上下文，获取游戏网关地址和端口
 * @param certification 认证凭据
 * @return 遗留编码的响应体字符串，格式 "地址/端口/凭据"
 */
std::string encode_select_server_body(const HostContext& context, std::int32_t certification) {
  return legacy_encode_string(context.config.ports.game_gateway.address + "/" +
                              std::to_string(context.config.ports.game_gateway.port) + "/" +
                              std::to_string(certification));
}

/**
 * @brief 编码开始游戏响应体
 * @param context 宿主上下文，获取游戏网关地址和端口
 * @return 遗留编码的响应体字符串，格式 "地址/端口"
 */
std::string encode_start_play_body(const HostContext& context) {
  return legacy_encode_string(context.config.ports.game_gateway.address + "/" +
                              std::to_string(context.config.ports.game_gateway.port));
}

/**
 * @brief 编码角色列表响应体
 * @param characters 角色记录列表
 * @param selected_character 已选择的角色名(留空则选第一个)
 * @return 遗留编码的角色列表体字符串
 * @note 每个角色用 "/" 分隔字段：名称/职业/发型/等级/性别
 *       选中的角色名前加 "*" 前缀
 */
std::string encode_character_list_body(const std::vector<CharacterRecord>& characters,
                                       std::string selected_character) {
  if (selected_character.empty() && !characters.empty()) {
    selected_character = characters.front().character_name;
  }

  std::string plain;
  for (std::size_t index = 0; index < kMaxCharacterSlots; ++index) {
    if (index < characters.size()) {
      auto name = characters[index].character_name;
      if (!selected_character.empty() && name == selected_character) {
        name.insert(name.begin(), '*');
      }
      plain += name + "/" + std::to_string(characters[index].job) + "/" +
               std::to_string(characters[index].hair) + "/" +
               std::to_string(characters[index].ability.level) + "/" +
               std::to_string(characters[index].sex) + "/";
    } else {
      plain += "/////";
    }
  }
  return legacy_encode_string(plain);
}

}  // namespace

void AuthService::start(HostContext& context) {
  context_ = &context;
  endpoint_ = context.bus->register_endpoint(name(), context.config.runtime.default_queue_capacity);
  running_.store(true, std::memory_order_relaxed);
  worker_ = std::thread([this] { run(); });
}

void AuthService::stop() { running_.store(false, std::memory_order_relaxed); }

void AuthService::join() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::unordered_map<std::string, std::string> AuthService::snapshot() const {
  return {{"running", running_.load(std::memory_order_relaxed) ? "true" : "false"},
          {"pending_requests", std::to_string(pending_requests_.size())},
          {"session_states", std::to_string(session_states_.size())},
          {"admissions", std::to_string(admissions_.size())}};
}

std::string AuthService::make_request_id() { return "auth:" + std::to_string(next_request_id_++); }

/**
 * @brief 工作线程主循环
 *
 * @details 从消息队列获取消息，根据消息类型分发给对应的处理函数：
 *          - SessionEvent: 处理会话事件(数据包接收、断开等)
 *          - LogicCommand: 处理逻辑指令(撤销认证等)
 *          - PersistResult: 处理持久化结果回调
 *
 *          使用 100ms 超时的等待弹出，避免忙等待消耗 CPU。
 */
void AuthService::run() {
  while (running_.load(std::memory_order_relaxed)) {
    auto message = endpoint_->queue->wait_pop_for(std::chrono::milliseconds(100));
    if (!message.has_value()) {
      continue;
    }
    if (auto event = std::get_if<SessionEvent>(&*message)) {
      handle_session_event(*event);
    } else if (auto command = std::get_if<LogicCommand>(&*message)) {
      handle_logic_command(*command);
    } else if (auto result = std::get_if<PersistResult>(&*message)) {
      handle_persist_result(*result);
    }
  }
}

/**
 * @brief 处理会话事件(数据包接收/断开连接)
 *
 * @details 根据不同的消息标识符处理各类客户端请求：
 *          - kCmIdPassword: 账号密码认证
 *          - kCmAddNewUser: 创建新账号
 *          - kCmUpdateUser: 更新账号信息
 *          - kCmChangePassword: 修改密码
 *          - kCmSelectServer: 选择游戏服务器
 *          - kCmQueryChr: 查询角色列表
 *          - kCmNewChr: 创建新角色
 *          - kCmDelChr: 删除角色
 *          - kCmSelChr: 选择角色
 *
 *          所有请求都通过 request_id 与持久化服务异步通信。
 *          断开连接时清理该会话的所有待处理请求和相关状态。
 */
void AuthService::handle_session_event(const SessionEvent& event) {
  if (context_ == nullptr) {
    return;
  }

  if (event.kind == SessionEventKind::disconnected) {
    if (const auto session = session_states_.find(event.session_id); session != session_states_.end()) {
      if (session->second.certification > 0) {
        const auto admission = admissions_.find(session->second.certification);
        if (admission != admissions_.end() && admission->second.selected_character.empty()) {
          admissions_.erase(admission);
        }
      }
      session_states_.erase(session);
    }
    for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
      if (it->second.session_id == event.session_id) {
        it = pending_requests_.erase(it);
      } else {
        ++it;
      }
    }
    return;
  }

  if (event.kind != SessionEventKind::packet_received) {
    return;
  }

  const auto decoded = decode_legacy_game_packet(event.packet);
  if (!decoded.has_value()) {
    return;
  }

  switch (decoded->message.ident) {
    case kCmIdPassword: {
      const auto fields = split_fields(legacy_decode_string(decoded->body), '/');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto password = fields.size() > 1 ? fields[1] : "";
      if (account_id.empty() || password.empty()) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::login_empty_credentials));
        audit(*context_, "auth.login_fail", account_id, std::to_string(event.session_id));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::authenticate_account;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending.client_version = decoded->message.recog;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::authenticate_account;
      request.reply_to = name();
      request.account_id = account_id;
      request.password = password;
      request.request_id = request_id;
      request.timestamp_ms = now_ms();
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmAddNewUser: {
      auto& session = session_states_[event.session_id];
      const auto current_ms = now_ms();
      if (should_throttle(session.last_command_at_ms, current_ms)) {
        return;
      }

      LegacyUserEntryInfo info{};
      LegacyUserEntryAddInfo add_info{};
      if (!decode_user_entry_body(decoded->body, info, add_info)) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::create_account_failed));
        return;
      }

      auto account = make_account_record(info, add_info);
      if (!is_valid_legacy_account_id(account.account_id)) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::create_account_failed));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::create_account;
      pending.session_id = event.session_id;
      pending.account_id = account.account_id;
      pending.account = account;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::create_account;
      request.reply_to = name();
      request.account_id = account.account_id;
      request.account = std::move(account);
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmUpdateUser: {
      auto& session = session_states_[event.session_id];
      const auto current_ms = now_ms();
      if (should_throttle(session.last_command_at_ms, current_ms)) {
        return;
      }

      LegacyUserEntryInfo info{};
      LegacyUserEntryAddInfo add_info{};
      if (!decode_user_entry_body(decoded->body, info, add_info)) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::update_account_failed));
        return;
      }

      auto account = make_account_record(info, add_info);
      if (session.account_id.empty() || session.account_id != account.account_id ||
          !is_valid_legacy_account_id(account.account_id)) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::update_account_failed));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::update_account;
      pending.session_id = event.session_id;
      pending.account_id = account.account_id;
      pending.account = account;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::update_account;
      request.reply_to = name();
      request.account_id = account.account_id;
      request.account = std::move(account);
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmChangePassword: {
      auto& session = session_states_[event.session_id];
      if (!session.account_id.empty()) {
        return;
      }
      const auto current_ms = now_ms();
      if (should_throttle(session.last_command_at_ms, current_ms)) {
        return;
      }

      const auto fields = split_fields(legacy_decode_string(decoded->body), '\t');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto password = fields.size() > 1 ? fields[1] : "";
      const auto new_password = fields.size() > 2 ? fields[2] : "";

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::change_password;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::change_password;
      request.reply_to = name();
      request.account_id = account_id;
      request.password = password;
      request.new_password = new_password;
      request.request_id = request_id;
      request.timestamp_ms = current_ms;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmSelectServer: {
      auto session = session_states_.find(event.session_id);
      if (session == session_states_.end() || session->second.account_id.empty() ||
          !can_accept(session->second.stage, CanonicalLoginRequest::select_server) ||
          admissions_.find(session->second.certification) == admissions_.end()) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::select_server_rejected));
        return;
      }

      session->second.stage =
          advance(session->second.stage, CanonicalLoginTransition::select_server);
      if (auto admission = admissions_.find(session->second.certification);
          admission != admissions_.end()) {
        admission->second.stage = session->second.stage;
      }

      post_gateway_packet(
          *context_, event.session_id,
          make_response_packet(event.session_id, kSmSelectServerOk, session->second.certification, 0,
                               0, 0,
                               encode_select_server_body(*context_, session->second.certification)));
      return;
    }

    case kCmQueryChr: {
      const auto fields = split_fields(legacy_decode_string(decoded->body), '/');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto certification = fields.size() > 1 ? parse_i32(fields[1]) : std::nullopt;

      const auto admission =
          certification.has_value() ? admissions_.find(*certification) : admissions_.end();
      const auto session = session_states_.find(event.session_id);
      if (account_id.empty() || !certification.has_value() || session == session_states_.end() ||
          !can_accept(session->second.stage, CanonicalLoginRequest::query_characters) ||
          admission == admissions_.end() ||
          admission->second.account_id != account_id) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::query_characters_rejected));
        audit(*context_, "auth.query_chr_fail", account_id, std::to_string(event.session_id));
        return;
      }

      auto& session_state = session->second;
      session_state.account_id = account_id;
      session_state.certification = *certification;
      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::query_characters;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending.certification = *certification;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::list_characters;
      request.reply_to = name();
      request.account_id = account_id;
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmNewChr: {
      const auto session = session_states_.find(event.session_id);
      const auto fields = split_fields(legacy_decode_string(decoded->body), '/');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto character_name = fields.size() > 1 ? fields[1] : "";
      const auto hair = fields.size() > 2 ? parse_i32(fields[2]) : std::nullopt;
      const auto job = fields.size() > 3 ? parse_i32(fields[3]) : std::nullopt;
      const auto sex = fields.size() > 4 ? parse_i32(fields[4]) : std::nullopt;

      if (session == session_states_.end() || session->second.account_id != account_id ||
          !can_accept(session->second.stage, CanonicalLoginRequest::create_character) ||
          admissions_.find(session->second.certification) == admissions_.end() ||
          !hair.has_value() || !job.has_value() || !sex.has_value() ||
          !is_valid_legacy_character_name(character_name)) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::create_character_rejected));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::create_precheck;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending.character_name = character_name;
      pending.certification = session->second.certification;
      pending.character =
          make_new_character(account_id, character_name, static_cast<std::uint8_t>(*job),
                             static_cast<std::uint8_t>(*sex), static_cast<std::uint8_t>(*hair));
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::list_characters;
      request.reply_to = name();
      request.account_id = account_id;
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmDelChr: {
      const auto session = session_states_.find(event.session_id);
      const auto character_name = legacy_decode_string(decoded->body);
      if (session == session_states_.end() || character_name.empty() ||
          !can_accept(session->second.stage, CanonicalLoginRequest::delete_character) ||
          admissions_.find(session->second.certification) == admissions_.end()) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::delete_character_rejected));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::delete_character;
      pending.session_id = event.session_id;
      pending.account_id = session->second.account_id;
      pending.character_name = character_name;
      pending.certification = session->second.certification;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::delete_character;
      request.reply_to = name();
      request.account_id = session->second.account_id;
      request.character_name = character_name;
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    case kCmSelChr: {
      const auto session = session_states_.find(event.session_id);
      const auto fields = split_fields(legacy_decode_string(decoded->body), '/');
      const auto account_id = fields.size() > 0 ? fields[0] : "";
      const auto character_name = fields.size() > 1 ? fields[1] : "";

      if (session == session_states_.end() || session->second.account_id != account_id ||
          character_name.empty() ||
          !can_accept(session->second.stage, CanonicalLoginRequest::select_character) ||
          admissions_.find(session->second.certification) == admissions_.end()) {
        post_gateway_packet(*context_, event.session_id,
                            make_error_response_packet(
                                event.session_id,
                                CanonicalLoginErrorKind::select_character_rejected));
        return;
      }

      const auto request_id = make_request_id();
      PendingAuthRequest pending;
      pending.kind = PendingAuthRequestKind::select_character;
      pending.session_id = event.session_id;
      pending.account_id = account_id;
      pending.character_name = character_name;
      pending.certification = session->second.certification;
      pending_requests_[request_id] = pending;

      PersistRequest request;
      request.kind = PersistRequestKind::list_characters;
      request.reply_to = name();
      request.account_id = account_id;
      request.request_id = request_id;
      context_->bus->post("persistence_service", std::move(request));
      return;
    }

    default:
      return;
  }
}

/**
 * @brief 处理逻辑指令
 *
 * @details 目前仅处理撤销认证指令(LogicCommandKind::revoke_authentication)，
 *          根据凭据值或账号ID清除对应的准入记录。
 *
 * @param command 逻辑指令
 */
void AuthService::handle_logic_command(const LogicCommand& command) {
  if (command.kind != LogicCommandKind::revoke_authentication) {
    return;
  }

  if (command.certification > 0) {
    admissions_.erase(command.certification);
  } else if (!command.account_id.empty()) {
    for (auto it = admissions_.begin(); it != admissions_.end();) {
      if (it->second.account_id == command.account_id) {
        it = admissions_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

/**
 * @brief 处理持久化结果回调
 *
 * @details 根据持久化结果类型和待请求类型执行不同的响应逻辑：
 *          - account_authenticated: 处理认证成功/失败，进行重复登录检测
 *          - account_created/updated/password_changed: 返回操作结果给客户端
 *          - characters_listed: 处理角色查询/创建预检查/角色选择
 *          - character_created/deleted: 返回操作结果
 *          - error: 统一错误处理
 *
 * @param result 持久化操作结果
 */
void AuthService::handle_persist_result(const PersistResult& result) {
  if (context_ == nullptr || result.request_id.empty()) {
    return;
  }

  const auto pending_it = pending_requests_.find(result.request_id);
  if (pending_it == pending_requests_.end()) {
    return;
  }

  auto pending = pending_it->second;
  switch (result.kind) {
    case PersistResultKind::account_loaded:
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::account_authenticated: {
      if (pending.kind != PendingAuthRequestKind::authenticate_account) {
        pending_requests_.erase(pending_it);
        return;
      }

      if (result.result_code == 1 && result.account.account_id == pending.account_id) {
        const auto duplicate =
            std::any_of(session_states_.begin(), session_states_.end(), [&](const auto& item) {
              return item.first != pending.session_id && item.second.account_id == pending.account_id &&
                     !item.second.account_id.empty();
            }) ||
            std::any_of(admissions_.begin(), admissions_.end(), [&](const auto& item) {
              return item.second.account_id == pending.account_id;
            });
        if (duplicate) {
          std::unordered_map<std::int32_t, LoginAdmission> revoked_admissions;
          std::vector<std::uint64_t> revoked_login_sessions;

          for (auto it = session_states_.begin(); it != session_states_.end();) {
            if (it->first != pending.session_id && it->second.account_id == pending.account_id &&
                !it->second.account_id.empty()) {
              revoked_login_sessions.push_back(it->first);
              if (it->second.certification > 0) {
                if (const auto admission = admissions_.find(it->second.certification);
                    admission != admissions_.end()) {
                  revoked_admissions[it->second.certification] = admission->second;
                } else {
                  revoked_admissions[it->second.certification] =
                      LoginAdmission{pending.account_id, {}, it->second.certification,
                                     CanonicalLoginStage::authenticated};
                }
              }
              it = session_states_.erase(it);
            } else {
              ++it;
            }
          }

          for (const auto& [certification, admission] : admissions_) {
            if (admission.account_id == pending.account_id) {
              revoked_admissions[certification] = admission;
            }
          }
          for (const auto& [certification, _] : revoked_admissions) {
            admissions_.erase(certification);
          }

          for (const auto session_id : revoked_login_sessions) {
            context_->bus->post(
                "login_gateway",
                SessionEvent{SessionEventKind::force_disconnect, "login_gateway", session_id, {}, {},
                             "duplicate_login"});
          }
          for (const auto& [_, admission] : revoked_admissions) {
            LogicCommand revoke;
            revoke.kind = LogicCommandKind::revoke_authentication;
            revoke.account_id = admission.account_id;
            revoke.character_name = admission.selected_character;
            revoke.certification = admission.certification;
            context_->bus->post("world_service", std::move(revoke));
          }

          post_gateway_packet(*context_, pending.session_id,
                              make_error_response_packet(
                                  pending.session_id,
                                  CanonicalLoginErrorKind::login_duplicate));
          audit(*context_, "auth.login_duplicate", pending.account_id,
                std::to_string(pending.session_id));
          pending_requests_.erase(pending_it);
          return;
        }

        if (requires_account_update(result.account)) {
          const auto info = make_user_entry_info(result.account);
          post_gateway_packet(
              *context_, pending.session_id,
              make_response_packet(pending.session_id, kSmNeedUpdateAccount, 0, 0, 0, 0,
                                   legacy_encode_buffer(&info, sizeof(info))));
        }

        const auto certification = std::max(next_certification_.fetch_add(1), 2);
        auto& session = session_states_[pending.session_id];
        session.account_id = pending.account_id;
        session.certification = certification;
        session.client_version = pending.client_version;
        session.stage = advance(session.stage, CanonicalLoginTransition::authenticate);
        admissions_[certification] =
            LoginAdmission{pending.account_id, {}, certification, session.stage};

        post_gateway_packet(
            *context_, pending.session_id,
            make_response_packet(pending.session_id, kSmPassOkSelectServer, 0, 0, 0, 0));
        audit(*context_, "auth.login_ok", pending.account_id, std::to_string(pending.session_id));
      } else {
        post_gateway_packet(*context_, pending.session_id,
                            make_error_response_packet(
                                pending.session_id,
                                canonical_login_error_from_login_result_code(
                                    result.result_code)));
        audit(*context_, "auth.login_fail", pending.account_id, std::to_string(pending.session_id));
      }
      pending_requests_.erase(pending_it);
      return;
    }

    case PersistResultKind::account_created:
      if (result.result_code == 1) {
        post_gateway_packet(*context_, pending.session_id,
                            make_response_packet(pending.session_id, kSmNewIdSuccess, 0));
      } else {
        post_gateway_packet(*context_, pending.session_id,
                            make_error_response_packet(
                                pending.session_id,
                                CanonicalLoginErrorKind::create_account_failed));
      }
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::account_updated:
      if (result.result_code == 1) {
        post_gateway_packet(*context_, pending.session_id,
                            make_response_packet(pending.session_id, kSmUpdateIdSuccess, 0));
      } else {
        post_gateway_packet(*context_, pending.session_id,
                            make_error_response_packet(
                                pending.session_id,
                                CanonicalLoginErrorKind::update_account_failed));
      }
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::password_changed:
      if (result.result_code == 1) {
        post_gateway_packet(*context_, pending.session_id,
                            make_response_packet(pending.session_id, kSmChgPasswdSuccess, 0));
      } else {
        post_gateway_packet(*context_, pending.session_id,
                            make_error_response_packet(
                                pending.session_id,
                                canonical_login_error_from_change_password_result_code(
                                    result.result_code)));
      }
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::characters_listed:
      switch (pending.kind) {
        case PendingAuthRequestKind::query_characters: {
          const auto selected_it = last_selected_character_.find(pending.account_id);
          const auto selected =
              selected_it != last_selected_character_.end() ? selected_it->second : std::string{};
          post_gateway_packet(
              *context_, pending.session_id,
              make_response_packet(pending.session_id, kSmQueryChr,
                                   static_cast<std::int32_t>(result.characters.size()), 0, 0, 1,
                                   encode_character_list_body(result.characters, selected)));
          audit(*context_, "auth.query_chr_ok", pending.account_id,
                std::to_string(pending.session_id));
          pending_requests_.erase(pending_it);
          return;
        }

        case PendingAuthRequestKind::create_precheck: {
          const auto exists = std::any_of(result.characters.begin(), result.characters.end(),
                                          [&](const CharacterRecord& character) {
                                            return character.character_name == pending.character_name;
          });
          if (exists) {
            post_gateway_packet(*context_, pending.session_id,
                                make_error_response_packet(
                                    pending.session_id,
                                    CanonicalLoginErrorKind::create_character_duplicate));
            pending_requests_.erase(pending_it);
            return;
          }
          if (result.characters.size() >= kMaxCharacterSlots) {
            post_gateway_packet(*context_, pending.session_id,
                                make_error_response_packet(
                                    pending.session_id,
                                    CanonicalLoginErrorKind::character_slots_full));
            pending_requests_.erase(pending_it);
            return;
          }

          pending_requests_[result.request_id].kind = PendingAuthRequestKind::create_commit;
          PersistRequest request;
          request.kind = PersistRequestKind::create_character;
          request.reply_to = name();
          request.account_id = pending.character.account_id;
          request.character_name = pending.character.character_name;
          request.character = pending.character;
          request.request_id = result.request_id;
          context_->bus->post("persistence_service", std::move(request));
          return;
        }

        case PendingAuthRequestKind::select_character: {
          const auto found = std::find_if(result.characters.begin(), result.characters.end(),
                                          [&](const CharacterRecord& character) {
                                            return character.character_name == pending.character_name;
                                          });
          if (found == result.characters.end()) {
            post_gateway_packet(*context_, pending.session_id,
                                make_error_response_packet(
                                    pending.session_id,
                                    CanonicalLoginErrorKind::character_not_found));
            pending_requests_.erase(pending_it);
            return;
          }

          admissions_[pending.certification] =
              LoginAdmission{pending.account_id, pending.character_name, pending.certification,
                             CanonicalLoginStage::character_selected};
          if (auto session = session_states_.find(pending.session_id);
              session != session_states_.end()) {
            session->second.stage =
                advance(session->second.stage, CanonicalLoginTransition::select_character);
          }
          last_selected_character_[pending.account_id] = pending.character_name;

          LogicCommand admission;
          admission.kind = LogicCommandKind::authenticate;
          admission.account_id = pending.account_id;
          admission.character_name = pending.character_name;
          admission.certification = pending.certification;
          context_->bus->post("world_service", std::move(admission));

          post_gateway_packet(*context_, pending.session_id,
                              make_response_packet(pending.session_id, kSmStartPlay, 0, 0, 0, 0,
                                                   encode_start_play_body(*context_)));
          audit(*context_, "auth.start_play", pending.account_id + ":" + pending.character_name,
                std::to_string(pending.session_id));
          pending_requests_.erase(pending_it);
          return;
        }

        case PendingAuthRequestKind::authenticate_account:
        case PendingAuthRequestKind::create_account:
        case PendingAuthRequestKind::update_account:
        case PendingAuthRequestKind::change_password:
        case PendingAuthRequestKind::create_commit:
        case PendingAuthRequestKind::delete_character:
          break;
      }
      break;

    case PersistResultKind::character_created:
      post_gateway_packet(*context_, pending.session_id,
                          make_response_packet(pending.session_id, kSmNewChrSuccess, 1));
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::character_deleted:
      post_gateway_packet(*context_, pending.session_id,
                          make_response_packet(pending.session_id, kSmDelChrSuccess, 1));
      if (const auto selected = last_selected_character_.find(pending.account_id);
          selected != last_selected_character_.end() &&
          selected->second == pending.character_name) {
        last_selected_character_.erase(pending.account_id);
      }
      pending_requests_.erase(pending_it);
      return;

    case PersistResultKind::error:
      switch (pending.kind) {
        case PendingAuthRequestKind::authenticate_account:
          post_gateway_packet(*context_, pending.session_id,
                              make_error_response_packet(
                                  pending.session_id,
                                  CanonicalLoginErrorKind::login_account_missing));
          break;
        case PendingAuthRequestKind::create_account:
          post_gateway_packet(*context_, pending.session_id,
                              make_error_response_packet(
                                  pending.session_id,
                                  CanonicalLoginErrorKind::create_account_failed));
          break;
        case PendingAuthRequestKind::update_account:
          post_gateway_packet(*context_, pending.session_id,
                              make_error_response_packet(
                                  pending.session_id,
                                  CanonicalLoginErrorKind::update_account_failed));
          break;
        case PendingAuthRequestKind::change_password:
          post_gateway_packet(*context_, pending.session_id,
                              make_error_response_packet(
                                  pending.session_id,
                                  CanonicalLoginErrorKind::change_password_failed));
          break;
        case PendingAuthRequestKind::query_characters:
          post_gateway_packet(*context_, pending.session_id,
                              make_error_response_packet(
                                  pending.session_id,
                                  CanonicalLoginErrorKind::query_characters_rejected));
          break;
        case PendingAuthRequestKind::create_precheck:
        case PendingAuthRequestKind::create_commit:
          post_gateway_packet(*context_, pending.session_id,
                              make_error_response_packet(
                                  pending.session_id,
                                  CanonicalLoginErrorKind::create_character_failed));
          break;
        case PendingAuthRequestKind::delete_character:
          post_gateway_packet(*context_, pending.session_id,
                              make_error_response_packet(
                                  pending.session_id,
                                  CanonicalLoginErrorKind::delete_character_failed));
          break;
        case PendingAuthRequestKind::select_character:
          post_gateway_packet(*context_, pending.session_id,
                              make_error_response_packet(
                                  pending.session_id,
                                  CanonicalLoginErrorKind::character_not_found));
          break;
      }
      pending_requests_.erase(pending_it);
      audit(*context_, "auth.persist_error", result.error, std::to_string(pending.session_id));
      return;

    case PersistResultKind::schema_ready:
    case PersistResultKind::character_loaded:
    case PersistResultKind::character_saved:
    case PersistResultKind::audit_recorded:
    case PersistResultKind::seeded:
      return;
  }
}

}  // namespace mir2
