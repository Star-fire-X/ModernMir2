/**
 * @file world_service.cpp
 * @brief 世界服务实现
 *
 * @details 实现 WorldService 类的全部接口，包括游戏主循环驱动、
 *          会话生命周期管理、消息路由、帧同步、持久化协调等核心功能。
 *
 *          游戏主循环采用 LegacyFrameDriver 驱动的帧同步机制，
 *          模拟旧版 Delphi 服务器的 RunSocket.Run → DecodeIdSocket →
 *          UserEngineExecuteRun → EventManagerRun → ServerMessageRun 流程。
 *
 * @note 安全保护机制：
 *       1. 服务器权威：客户端不直接设置坐标、属性—所有修改通过 LogicRuntime
 *       2. 序列号水位线：拒绝过期/重放消息(accept_ingress_sequence)
 *       3. 登录状态门控：只有在 in_game 状态才接受游戏操作
 *       4. 数据包边界检查：LegacyProtocolCodec 确保最大64KB帧
 *       5. 总线背压：队列深度超过阈值时暂停或断开客户端
 */

#include "services/world_service.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <thread>
#include <vector>

#include "protocol/canonical_legacy_command.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "util/string_utils.hpp"

namespace mir2 {

namespace {

// ── 安全与异常保护 ──────────────────────────────────────────────────
//
//  实施以下遗留协议兼容的保护措施：
//  1. 服务器权威：客户端从不直接设置坐标、属性、金币或物品所有权
//     所有变更通过 LogicRuntime 处理
//  2. Session_seq 水位线：拒绝过期/重放消息
//     (accept_ingress_sequence 执行单调递增的按会话序列检查)
//  3. 登录状态门控：仅在 in_game() 返回 true 时接受游戏操作指令
//     登录前的数据包被丢弃
//  4. 数据包边界：LegacyProtocolCodec 强制执行最大64KB帧
//     decode_6bit_buf 验证字符范围，decode_legacy_game_packet 检查最小负载长度
//  5. 总线背压：当 world_service 队列超过配置阈值时网关暂停或断开连接
//
//  PR-9 待完成的剩余差距：
//  - 每会话发送速率限制(匹配 Delphi RUNGate CheckSendLength)
//  - CI 中的数据包模糊测试
//  - 登录暴力破解检测
// ──────────────────────────────────────────────────────────────────────

/// @brief 遗留协议踢出关闭延迟(毫秒)
constexpr std::int32_t kLegacyKickCloseDelayMs = 50;

/**
 * @struct RunLoginPayload
 * @brief 解码后的运行登录负载
 *
 * @details 从客户端发送的 ** 前缀的特殊登录数据包中解析出的信息，
 *         包含账号、角色、认证凭据、客户端版本号等。
 */
struct RunLoginPayload {
  std::string account_id{};         ///< 账号ID
  std::string character_name{};     ///< 角色名
  std::int32_t certification{0};    ///< 认证凭据
  std::int32_t client_version{0};   ///< 客户端版本号
  std::int32_t client_checksum{0};  ///< 客户端校验和
  bool start_new{false};            ///< 是否新开始游戏
};

/**
 * @brief 生成账号:角色格式的键
 * @param account 账号ID
 * @param character 角色名
 * @return "账号:角色" 格式的字符串
 */
std::string make_key(const std::string& account, const std::string& character) {
  return account + ":" + character;
}

/**
 * @brief 将源 RuntimeDispatch 追加到目标中
 * @param target 目标分发
 * @param source 源分发(会被移动)
 */
void append_dispatch(RuntimeDispatch& target, RuntimeDispatch source) {
  target.session_events.insert(target.session_events.end(),
                               std::make_move_iterator(source.session_events.begin()),
                               std::make_move_iterator(source.session_events.end()));
  target.audit_events.insert(target.audit_events.end(),
                             std::make_move_iterator(source.audit_events.begin()),
                             std::make_move_iterator(source.audit_events.end()));
  target.persist_requests.insert(target.persist_requests.end(),
                                 std::make_move_iterator(source.persist_requests.begin()),
                                 std::make_move_iterator(source.persist_requests.end()));
  target.cross_map_mails.insert(target.cross_map_mails.end(),
                                std::make_move_iterator(source.cross_map_mails.begin()),
                                std::make_move_iterator(source.cross_map_mails.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

/**
 * @brief 将数据包体转换为字符串
 * @param packet 遗留数据包
 * @return 包体内容的字符串表示
 */
std::string body_to_string(const LegacyPacket& packet) {
  return std::string(packet.body.begin(), packet.body.end());
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
 * @brief 解码运行登录数据包
 *
 * @details 旧版客户端发送的密钥格式：
 *          "**<账号>/<角色>/<凭据>/<版本>/<异或1>/<校验和>/<异或2>/<新开始>"
 *          异或值用于验证凭据的合法性：
 *          凭据 == xor1 ^ 0xF2E44FFF 且 凭据 == xor2 ^ 0xA4A5B277
 *
 * @param packet 遗留数据包
 * @return 如果解码成功则返回 RunLoginPayload，否则返回 std::nullopt
 */
std::optional<RunLoginPayload> decode_run_login(const LegacyPacket& packet) {
  const auto decoded = legacy_decode_text(body_to_string(packet));
  if (!util::starts_with(decoded, "**")) {
    return std::nullopt;
  }

  auto fields = util::split(decoded.substr(2), '/');
  if (fields.size() < 8) {
    return std::nullopt;
  }

  RunLoginPayload payload;
  payload.account_id = fields[0];
  payload.character_name = fields[1];
  const auto certification = parse_i32(fields[2]);
  const auto client_version = parse_i32(fields[3]);
  const auto xor1 = parse_i32(fields[4]);
  const auto client_checksum = parse_i32(fields[5]);
  const auto xor2 = parse_i32(fields[6]);
  payload.start_new = fields[7] == "0";

  if (!certification.has_value() || !client_version.has_value() || !xor1.has_value() ||
      !client_checksum.has_value() || !xor2.has_value()) {
    return std::nullopt;
  }

  payload.certification = *certification;
  payload.client_version = *client_version;
  payload.client_checksum = *client_checksum;

  if (payload.account_id.empty() || payload.character_name.empty() || payload.certification < 2) {
    return std::nullopt;
  }
  if (((*xor1) ^ 0xF2E44FFF) != payload.certification ||
      ((*xor2) ^ static_cast<std::int32_t>(0xA4A5B277u)) != payload.certification) {
    return std::nullopt;
  }
  return payload;
}

/**
 * @brief 应用运行时配置的城堡对话框默认值
 * @param runtime_config 运行时配置
 * @param castle_dialog_context 城堡对话框上下文(会被修改)
 */
void apply_runtime_castle_defaults(const RuntimeConfig& runtime_config,
                                   CastleDialogContext& castle_dialog_context) {
  if (castle_dialog_context.castle_name.empty()) {
    castle_dialog_context.castle_name = runtime_config.castle_name;
  }
  if (castle_dialog_context.castle_war_date.empty()) {
    castle_dialog_context.castle_war_date = runtime_config.default_castle_war_date;
  }
  if (castle_dialog_context.no_active_wars_text.empty()) {
    castle_dialog_context.no_active_wars_text = runtime_config.no_active_wars_text;
  }
  if (castle_dialog_context.unclaimed_owner_label.empty()) {
    castle_dialog_context.unclaimed_owner_label = runtime_config.unclaimed_castle_owner;
  }
  if (castle_dialog_context.unclaimed_lord_label.empty()) {
    castle_dialog_context.unclaimed_lord_label = runtime_config.unclaimed_castle_lord;
  }
  if (castle_dialog_context.owner_role_label.empty()) {
    castle_dialog_context.owner_role_label = runtime_config.castle_owner_role_label;
  }
  if (castle_dialog_context.owner_guild_role_label.empty()) {
    castle_dialog_context.owner_guild_role_label = runtime_config.castle_owner_guild_role_label;
  }
  if (castle_dialog_context.challenger_role_label.empty()) {
    castle_dialog_context.challenger_role_label = runtime_config.castle_challenger_role_label;
  }
  if (castle_dialog_context.rival_role_label.empty()) {
    castle_dialog_context.rival_role_label = runtime_config.castle_rival_role_label;
  }
  if (castle_dialog_context.unknown_role_label.empty()) {
    castle_dialog_context.unknown_role_label = runtime_config.castle_unknown_role_label;
  }
  if (castle_dialog_context.war_entry_listed_label.empty()) {
    castle_dialog_context.war_entry_listed_label = runtime_config.castle_war_entry_listed_label;
  }
  if (castle_dialog_context.war_entry_unlisted_label.empty()) {
    castle_dialog_context.war_entry_unlisted_label = runtime_config.castle_war_entry_unlisted_label;
  }
  if (castle_dialog_context.war_status_active_label.empty()) {
    castle_dialog_context.war_status_active_label = runtime_config.castle_war_status_active_label;
  }
  if (castle_dialog_context.war_status_available_label.empty()) {
    castle_dialog_context.war_status_available_label =
        runtime_config.castle_war_status_available_label;
  }
  if (castle_dialog_context.role_change_owner_label.empty()) {
    castle_dialog_context.role_change_owner_label = runtime_config.castle_role_change_owner_label;
  }
  if (castle_dialog_context.role_change_challenger_label.empty()) {
    castle_dialog_context.role_change_challenger_label =
        runtime_config.castle_role_change_challenger_label;
  }
  if (castle_dialog_context.claim_summary_template.empty()) {
    castle_dialog_context.claim_summary_template = runtime_config.castle_claim_summary_template;
  }
  if (castle_dialog_context.war_summary_template.empty()) {
    castle_dialog_context.war_summary_template = runtime_config.castle_war_summary_template;
  }
  if (castle_dialog_context.claim_require_guild_template.empty()) {
    castle_dialog_context.claim_require_guild_template =
        runtime_config.castle_claim_require_guild_template;
  }
  if (castle_dialog_context.claim_missing_guild_template.empty()) {
    castle_dialog_context.claim_missing_guild_template =
        runtime_config.castle_claim_missing_guild_template;
  }
  if (castle_dialog_context.claim_only_lord_template.empty()) {
    castle_dialog_context.claim_only_lord_template =
        runtime_config.castle_claim_only_lord_template;
  }
  if (castle_dialog_context.war_require_guild_template.empty()) {
    castle_dialog_context.war_require_guild_template =
        runtime_config.castle_war_require_guild_template;
  }
  if (castle_dialog_context.war_missing_guild_template.empty()) {
    castle_dialog_context.war_missing_guild_template =
        runtime_config.castle_war_missing_guild_template;
  }
  if (castle_dialog_context.war_only_lord_template.empty()) {
    castle_dialog_context.war_only_lord_template =
        runtime_config.castle_war_only_lord_template;
  }
  if (castle_dialog_context.war_usage_template.empty()) {
    castle_dialog_context.war_usage_template = runtime_config.castle_war_usage_template;
  }
  if (castle_dialog_context.war_self_target_template.empty()) {
    castle_dialog_context.war_self_target_template =
        runtime_config.castle_war_self_target_template;
  }
  if (castle_dialog_context.war_target_missing_template.empty()) {
    castle_dialog_context.war_target_missing_template =
        runtime_config.castle_war_target_missing_template;
  }
  if (castle_dialog_context.war_already_registered_template.empty()) {
    castle_dialog_context.war_already_registered_template =
        runtime_config.castle_war_already_registered_template;
  }
  if (castle_dialog_context.war_need_gold_template.empty()) {
    castle_dialog_context.war_need_gold_template = runtime_config.castle_war_need_gold_template;
  }
  if (castle_dialog_context.guild_create_summary_template.empty()) {
    castle_dialog_context.guild_create_summary_template =
        runtime_config.guild_create_summary_template;
  }
  if (castle_dialog_context.guild_apply_summary_template.empty()) {
    castle_dialog_context.guild_apply_summary_template =
        runtime_config.guild_apply_summary_template;
  }
  if (castle_dialog_context.guild_withdraw_summary_template.empty()) {
    castle_dialog_context.guild_withdraw_summary_template =
        runtime_config.guild_withdraw_summary_template;
  }
  if (castle_dialog_context.guild_approve_summary_template.empty()) {
    castle_dialog_context.guild_approve_summary_template =
        runtime_config.guild_approve_summary_template;
  }
  if (castle_dialog_context.guild_reject_summary_template.empty()) {
    castle_dialog_context.guild_reject_summary_template =
        runtime_config.guild_reject_summary_template;
  }
  if (castle_dialog_context.guild_kick_summary_template.empty()) {
    castle_dialog_context.guild_kick_summary_template =
        runtime_config.guild_kick_summary_template;
  }
  if (castle_dialog_context.guild_title_summary_template.empty()) {
    castle_dialog_context.guild_title_summary_template =
        runtime_config.guild_title_summary_template;
  }
  if (castle_dialog_context.guild_transfer_summary_template.empty()) {
    castle_dialog_context.guild_transfer_summary_template =
        runtime_config.guild_transfer_summary_template;
  }
  if (castle_dialog_context.guild_leave_summary_template.empty()) {
    castle_dialog_context.guild_leave_summary_template =
        runtime_config.guild_leave_summary_template;
  }
  if (castle_dialog_context.guild_leave_transfer_summary_template.empty()) {
    castle_dialog_context.guild_leave_transfer_summary_template =
        runtime_config.guild_leave_transfer_summary_template;
  }
  if (castle_dialog_context.guild_disband_summary_template.empty()) {
    castle_dialog_context.guild_disband_summary_template =
        runtime_config.guild_disband_summary_template;
  }
  if (castle_dialog_context.guild_membership_cleared_summary_template.empty()) {
    castle_dialog_context.guild_membership_cleared_summary_template =
        runtime_config.guild_membership_cleared_summary_template;
  }
  if (castle_dialog_context.guild_apply_alert_template.empty()) {
    castle_dialog_context.guild_apply_alert_template = runtime_config.guild_apply_alert_template;
  }
  if (castle_dialog_context.guild_withdraw_alert_template.empty()) {
    castle_dialog_context.guild_withdraw_alert_template =
        runtime_config.guild_withdraw_alert_template;
  }
  if (castle_dialog_context.guild_approved_notice_template.empty()) {
    castle_dialog_context.guild_approved_notice_template =
        runtime_config.guild_approved_notice_template;
  }
  if (castle_dialog_context.guild_rejected_notice_template.empty()) {
    castle_dialog_context.guild_rejected_notice_template =
        runtime_config.guild_rejected_notice_template;
  }
  if (castle_dialog_context.guild_removed_notice_template.empty()) {
    castle_dialog_context.guild_removed_notice_template =
        runtime_config.guild_removed_notice_template;
  }
  if (castle_dialog_context.guild_new_lord_notice_template.empty()) {
    castle_dialog_context.guild_new_lord_notice_template =
        runtime_config.guild_new_lord_notice_template;
  }
  if (castle_dialog_context.guild_title_changed_notice_template.empty()) {
    castle_dialog_context.guild_title_changed_notice_template =
        runtime_config.guild_title_changed_notice_template;
  }
  if (castle_dialog_context.guild_create_leave_current_template.empty()) {
    castle_dialog_context.guild_create_leave_current_template =
        runtime_config.guild_create_leave_current_template;
  }
  if (castle_dialog_context.guild_create_choose_name_template.empty()) {
    castle_dialog_context.guild_create_choose_name_template =
        runtime_config.guild_create_choose_name_template;
  }
  if (castle_dialog_context.guild_create_name_unavailable_template.empty()) {
    castle_dialog_context.guild_create_name_unavailable_template =
        runtime_config.guild_create_name_unavailable_template;
  }
  if (castle_dialog_context.guild_create_need_gold_template.empty()) {
    castle_dialog_context.guild_create_need_gold_template =
        runtime_config.guild_create_need_gold_template;
  }
  if (castle_dialog_context.guild_apply_leave_current_template.empty()) {
    castle_dialog_context.guild_apply_leave_current_template =
        runtime_config.guild_apply_leave_current_template;
  }
  if (castle_dialog_context.guild_apply_choose_guild_template.empty()) {
    castle_dialog_context.guild_apply_choose_guild_template =
        runtime_config.guild_apply_choose_guild_template;
  }
  if (castle_dialog_context.guild_not_found_template.empty()) {
    castle_dialog_context.guild_not_found_template = runtime_config.guild_not_found_template;
  }
  if (castle_dialog_context.guild_apply_already_pending_template.empty()) {
    castle_dialog_context.guild_apply_already_pending_template =
        runtime_config.guild_apply_already_pending_template;
  }
  const auto owner_text = util::lower_copy(util::trim(castle_dialog_context.owner_guild));
  if (owner_text.empty() || owner_text == "none" || owner_text == "unclaimed" ||
      owner_text == "-" ||
      owner_text == util::lower_copy(castle_dialog_context.unclaimed_owner_label)) {
    castle_dialog_context.owner_guild.clear();
  }
  const auto lord_text = util::lower_copy(util::trim(castle_dialog_context.lord));
  if (castle_dialog_context.owner_guild.empty() || lord_text.empty() || lord_text == "none" ||
      lord_text == "unclaimed" || lord_text == "-" ||
      lord_text == util::lower_copy(castle_dialog_context.unclaimed_lord_label)) {
    castle_dialog_context.lord.clear();
  }
  const auto wars_text = util::trim(castle_dialog_context.list_of_war);
  if (wars_text.empty() ||
      util::lower_copy(wars_text) == "no active wars." ||
      util::lower_copy(wars_text) == util::lower_copy(castle_dialog_context.no_active_wars_text)) {
    castle_dialog_context.list_of_war.clear();
  }
  if (castle_dialog_context.guild_war_fee <= 0) {
    castle_dialog_context.guild_war_fee = runtime_config.guild_war_fee;
  }
  if (castle_dialog_context.upgrade_weapon_fee <= 0) {
    castle_dialog_context.upgrade_weapon_fee = runtime_config.upgrade_weapon_fee;
  }
  if (castle_dialog_context.guild_create_fee <= 0) {
    castle_dialog_context.guild_create_fee = runtime_config.guild_create_fee;
  }
}

/**
 * @brief 应用运行时配置的城堡默认值(基于快照的重载)
 * @param runtime_config 运行时配置
 * @param guild_castle_snapshot 公会城堡快照
 */
void apply_runtime_castle_defaults(const RuntimeConfig& runtime_config,
                                   GuildCastleSnapshot& guild_castle_snapshot) {
  apply_runtime_castle_defaults(runtime_config, guild_castle_snapshot.castle_dialog);
}

/**
 * @brief 获取城堡拥有者的显示名称
 * @param castle_dialog_context 城堡对话框上下文
 * @return 如果无拥有者则返回 unclaimed_owner_label，否则返回公会名
 */
std::string display_castle_owner(const CastleDialogContext& castle_dialog_context) {
  return util::trim(castle_dialog_context.owner_guild).empty()
             ? castle_dialog_context.unclaimed_owner_label
             : castle_dialog_context.owner_guild;
}

/**
 * @brief 获取城堡领主的显示名称
 * @param castle_dialog_context 城堡对话框上下文
 * @return 如果无领主则返回 unclaimed_lord_label，否则返回领主名
 */
std::string display_castle_lord(const CastleDialogContext& castle_dialog_context) {
  return util::trim(castle_dialog_context.owner_guild).empty() ||
                 util::trim(castle_dialog_context.lord).empty()
             ? castle_dialog_context.unclaimed_lord_label
             : castle_dialog_context.lord;
}

/**
 * @brief 构造断开连接数据包(SM_OUTOFCONNECTION)
 * @param session_id 会话ID
 * @return 遗留协议数据包
 */
LegacyPacket make_out_of_connection_packet(std::uint64_t session_id) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmOutOfConnection, 0, 0, 0, 0));
}

}  // namespace

/**
 * @brief 启动世界服务
 *
 * @details 初始化消息总线端点、LogicRuntime 游戏逻辑引擎、
 *          加载商家状态、请求城堡对话框上下文刷新，然后启动工作线程。
 *
 * @param context 宿主上下文
 */
void WorldService::start(HostContext& context) {
  context_ = &context;
  endpoint_ = context.bus->register_endpoint(name(), context.config.runtime.default_queue_capacity);
  auto runtime_config = context.config;
  if (runtime_config.runtime.legacy_admin_list.is_relative()) {
    runtime_config.runtime.legacy_admin_list =
        context.root_dir / runtime_config.runtime.legacy_admin_list;
  }
  runtime_ = std::make_unique<LogicRuntime>(std::move(runtime_config));
  runtime_->initialize();
  PersistRequest merchant_request;
  merchant_request.kind = PersistRequestKind::load_merchant_states;
  merchant_request.reply_to = name();
  context_->bus->post("persistence_service", std::move(merchant_request));
  next_castle_context_refresh_ = std::chrono::steady_clock::now();
  request_castle_dialog_context_refresh();
  running_.store(true, std::memory_order_relaxed);
  worker_ = std::thread([this] { run(); });
}

void WorldService::stop() { running_.store(false, std::memory_order_relaxed); }

void WorldService::join() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

#ifdef MIR2_ENABLE_TEST_HOOKS
void WorldService::attach_context_for_test(HostContext& context) { context_ = &context; }

void WorldService::initialize_runtime_for_test(const HostConfig& config) {
  runtime_ = std::make_unique<LogicRuntime>(config);
  runtime_->initialize();
}

void WorldService::enqueue_gate_event_for_test(SessionEvent event) {
  std::scoped_lock lock(gate_events_mutex_);
  pending_gate_events_.push_back(std::move(event));
}

void WorldService::seed_session_sequence_for_test(std::uint64_t session_id,
                                                  std::uint64_t session_seq) {
  session_sequence_watermarks_[session_id] = session_seq;
}

std::size_t WorldService::legacy_session_inbox_size_for_test(
    std::uint64_t session_id) const {
  return runtime_ != nullptr ? runtime_->legacy_session_inbox_size(session_id) : 0;
}

std::vector<std::uint64_t> WorldService::legacy_session_inbox_sequences_for_test(
    std::uint64_t session_id) const {
  return runtime_ != nullptr ? runtime_->legacy_session_inbox_sequences(session_id)
                             : std::vector<std::uint64_t>{};
}

RuntimeDispatch WorldService::tick_runtime_for_test(std::uint64_t now_ms) {
  return runtime_ != nullptr ? runtime_->tick(now_ms) : RuntimeDispatch{};
}

RuntimeDispatch WorldService::run_legacy_socket_stage_for_test(std::uint64_t now_ms) {
  return run_legacy_socket_stage(now_ms);
}

RuntimeDispatch WorldService::process_ingress_batch_for_test(WorldIngressBatch& batch) {
  return process_ingress_batch(batch);
}
#endif

std::unordered_map<std::string, std::string> WorldService::snapshot() const {
  if (runtime_ == nullptr) {
    return {{"running", "false"}};
  }
  LegacyFrameTrace legacy_trace;
  bool legacy_frame_seen = false;
  std::size_t pending_gate_events = 0;
  std::uint64_t run_socket_last_flushed = 0;
  std::uint64_t run_socket_last_remaining = 0;
  std::uint64_t run_socket_last_ms = 0;
  {
    std::scoped_lock lock(legacy_frame_mutex_);
    legacy_trace = legacy_frame_trace_;
    legacy_frame_seen = legacy_frame_seen_;
  }
  {
    std::scoped_lock lock(gate_events_mutex_);
    pending_gate_events = pending_gate_events_.size();
    run_socket_last_flushed = run_socket_last_flushed_;
    run_socket_last_remaining = run_socket_last_remaining_;
    run_socket_last_ms = run_socket_last_ms_;
  }
  return {{"running", running_.load(std::memory_order_relaxed) ? "true" : "false"},
          {"maps", std::to_string(runtime_->map_count())},
          {"sessions", std::to_string(runtime_->online_session_count())},
          {"tick", std::to_string(runtime_->current_tick())},
          {"legacy_frame", "enabled"},
          {"legacy_frame_index",
           legacy_frame_seen ? std::to_string(legacy_trace.frame_index) : "0"},
          {"legacy_last_stage", legacy_frame_seen ? legacy_trace.last_stage : ""},
          {"legacy_last_frame_ms",
           legacy_frame_seen ? std::to_string(legacy_trace.last_frame_ms) : "0"},
          {"guild_count", std::to_string(guild_castle_snapshot_.guilds.size())},
          {"castle_name", castle_dialog_context_.castle_name},
          {"castle_owner_guild", display_castle_owner(castle_dialog_context_)},
          {"castle_lord", display_castle_lord(castle_dialog_context_)},
          {"castle_war_date", castle_dialog_context_.castle_war_date},
          {"guild_war_fee", std::to_string(castle_dialog_context_.guild_war_fee)},
          {"upgrade_weapon_fee", std::to_string(castle_dialog_context_.upgrade_weapon_fee)},
          {"guild_create_fee", std::to_string(castle_dialog_context_.guild_create_fee)},
          {"castle_refreshes", std::to_string(castle_context_refresh_count_)},
          {"offline_guild_results", std::to_string(offline_guild_result_count_)},
          {"offline_guild_routes", std::to_string(offline_guild_route_count_)},
          {"offline_guild_errors", std::to_string(offline_guild_error_count_)},
          {"pending_gate_events", std::to_string(pending_gate_events)},
          {"run_socket_last_flushed", std::to_string(run_socket_last_flushed)},
          {"run_socket_last_remaining", std::to_string(run_socket_last_remaining)},
          {"run_socket_last_ms", std::to_string(run_socket_last_ms)},
          {"castle_refresh_interval_ms",
           std::to_string(context_ != nullptr ? context_->config.runtime.castle_context_refresh_ms : 0)}};
}

/**
 * @brief 帧排序保证：
 * 1. Socket 接收顺序 → 总线 FIFO 投递 → 单调增加的 ingress_seq
 * 2. 同会话 FIFO 通过 session_seq 水位线保证(accept_ingress_sequence)
 * 3. 同批次匹配 Delphi RunSocket.Run 的 drain-then-dispatch 模式
 * 4. 每玩家 inbox FIFO 通过 route_logic_command 的入队顺序保证
 * 5. flush_dispatch 在帧结束时保持分发顺序
 */
void WorldService::run() {
  auto next_tick = std::chrono::steady_clock::now();
  const auto tick_interval = std::chrono::milliseconds(context_->config.budgets.tick_ms);
  WorldIngressBatch pending_ingress;

  while (running_.load(std::memory_order_relaxed)) {
    while (true) {
      auto message = endpoint_->queue->try_pop();
      if (!message.has_value()) {
        break;
      }
      pending_ingress.push(std::move(*message), ++next_ingress_seq_);
    }

    const auto now = std::chrono::steady_clock::now();
    if (!castle_context_refresh_in_flight_ &&
        context_->config.runtime.castle_context_refresh_ms > 0 &&
        now >= next_castle_context_refresh_) {
      request_castle_dialog_context_refresh();
    }
    if (now >= next_tick) {
      WorldIngressBatch frame_ingress;
      frame_ingress.messages.swap(pending_ingress.messages);
      const auto now_ms = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
      current_frame_now_ms_ = now_ms;
      LegacyFrameCallbacks callbacks;
      callbacks.run_socket_run = [this, now_ms]() -> RuntimeDispatch {
        return run_legacy_socket_stage(now_ms);
      };
      callbacks.decode_id_socket = [this](WorldIngressBatch& batch) -> RuntimeDispatch {
        return process_ingress_batch(batch);
      };
      callbacks.user_engine_execute_run = [this, now_ms]() -> RuntimeDispatch {
        LegacyRuntimeContext runtime_context;
        runtime_context.persistence_overloaded =
            context_ != nullptr && context_->bus != nullptr &&
            context_->bus->queue_depth("persistence_service") >= 1000;
        return runtime_->tick(now_ms, runtime_context);
      };
      callbacks.event_manager_run = [this, now_ms]() -> RuntimeDispatch {
        return runtime_->run_legacy_event_manager(now_ms);
      };
      callbacks.server_message_run = [this, now_ms]() -> RuntimeDispatch {
        return run_server_message_stage(now_ms);
      };

      auto dispatch =
          legacy_frame_driver_.run_frame(now_ms, std::move(frame_ingress), callbacks);
      {
        std::scoped_lock lock(legacy_frame_mutex_);
        legacy_frame_trace_ = legacy_frame_driver_.last_trace();
        legacy_frame_seen_ = true;
      }
      if (context_->logger != nullptr && legacy_frame_driver_.frame_index() % 50 == 0) {
        context_->logger->debug("legacy frame {} stage={} frame_ms={}",
                                legacy_frame_driver_.frame_index(),
                                legacy_frame_driver_.last_trace().last_stage,
                                legacy_frame_driver_.last_trace().last_frame_ms);
      }
      flush_dispatch(std::move(dispatch));
      next_tick += tick_interval;
      const auto after_frame = std::chrono::steady_clock::now();
      if (after_frame >= next_tick + tick_interval) {
        next_tick = after_frame + tick_interval;
      }
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  /**
   * @brief 关闭时保存所有在线角色
   */
  if (runtime_ != nullptr) {
    RuntimeDispatch shutdown_dispatch;
    for (auto character : runtime_->snapshot_online_characters()) {
      PersistRequest request;
      request.kind = PersistRequestKind::save_character;
      request.account_id = character.account_id;
      request.character_name = character.character_name;
      request.character = std::move(character);
      shutdown_dispatch.persist_requests.push_back(std::move(request));
    }
    flush_dispatch(std::move(shutdown_dispatch));
  }
}

/**
 * @brief 请求刷新城堡对话框上下文
 *
 * @details 向持久化服务发送加载公会城堡快照的请求。
 *          castle_context_refresh_in_flight_ 标志防止重复请求。
 */
void WorldService::request_castle_dialog_context_refresh() {
  if (context_ == nullptr || castle_context_refresh_in_flight_) {
    return;
  }

  PersistRequest request;
  request.kind = PersistRequestKind::load_guild_castle_snapshot;
  request.reply_to = name();
  if (context_->bus->post("persistence_service", std::move(request))) {
    castle_context_refresh_in_flight_ = true;
    const auto interval_ms =
        std::max<std::uint32_t>(context_->config.runtime.castle_context_refresh_ms, 1);
    next_castle_context_refresh_ =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
  }
}

/**
 * @brief 处理消息总线的输入批次
 *
 * @details 遍历批次中的所有消息，根据类型分发给对应的处理函数：
 *          - SessionEvent: 会话事件(数据包接收等)
 *          - LogicCommand: 逻辑指令
 *          - PersistResult: 持久化结果
 *          - ActorMail: 演员邮件(跨地图通信)
 *
 * @param batch 输入批次
 * @return 合并后的运行时分发数据
 */
RuntimeDispatch WorldService::process_ingress_batch(WorldIngressBatch& batch) {
  RuntimeDispatch combined;
  for (auto& ingress : batch.messages) {
    if (!accept_ingress_sequence(ingress, combined)) {
      continue;
    }

    auto& message = ingress.message;
    if (auto event = std::get_if<SessionEvent>(&message)) {
      append_dispatch(combined, handle_session_event(*event));
    } else if (auto command = std::get_if<LogicCommand>(&message)) {
      append_dispatch(combined, handle_logic_command(*command));
    } else if (auto result = std::get_if<PersistResult>(&message)) {
      append_dispatch(combined, handle_persist_result(*result));
    } else if (auto mail = std::get_if<ActorMail>(&message)) {
      append_dispatch(combined, runtime_->route_actor_mail(*mail));
    }
  }
  batch.messages.clear();
  return combined;
}

RuntimeDispatch WorldService::run_server_message_stage(std::uint64_t) {
  return {};
}

/**
 * @brief 检查输入消息的序列号是否可接受
 *
 * @details 确保每个会话的消息序列号严格单调递增。
 *          如果消息序列号小于等于已有水位线，则判定为过期/重放消息，
 *          记录审计事件并拒绝处理。
 *
 * @param ingress 输入消息
 * @param dispatch 输出分发(记录拒绝审计)
 * @return true 如果序列号有效
 */
bool WorldService::accept_ingress_sequence(const WorldIngressMessage& ingress,
                                           RuntimeDispatch& dispatch) {
  std::uint64_t session_id = 0;
  std::uint64_t session_seq = 0;
  std::string kind = "unknown";

  if (const auto* event = std::get_if<SessionEvent>(&ingress.message)) {
    session_id = event->session_id;
    session_seq = event->session_seq;
    kind = "SessionEvent";
  } else if (const auto* command = std::get_if<LogicCommand>(&ingress.message)) {
    session_id = command->session_id;
    session_seq = command->session_seq;
    kind = "LogicCommand";
  } else if (const auto* mail = std::get_if<ActorMail>(&ingress.message)) {
    session_id = mail->session_id;
    session_seq = mail->session_seq;
    kind = "ActorMail";
  }

  if (session_id == 0 || session_seq == 0) {
    return true;
  }

  const auto last_it = session_sequence_watermarks_.find(session_id);
  const auto last_seq =
      last_it != session_sequence_watermarks_.end() ? last_it->second : 0;
  if (session_seq > last_seq) {
    session_sequence_watermarks_[session_id] = session_seq;
    return true;
  }

  const auto detail = "session=" + std::to_string(session_id) +
                      " seq=" + std::to_string(session_seq) +
                      " last=" + std::to_string(last_seq) +
                      " ingress=" + std::to_string(ingress.ingress_seq) +
                      " frame=" + std::to_string(ingress.frame_index);
  dispatch.audit_events.push_back(
      AuditEvent{"world.ingress_stale_sequence", kind + " " + detail, name()});

  LegacyRuntimeTrace trace;
  trace.stage = "DecodeIdSocket";
  trace.action = "stale_session_sequence";
  trace.actor_id = session_id;
  trace.cursor = static_cast<std::size_t>(ingress.ingress_seq);
  trace.sub_cursor = static_cast<std::size_t>(ingress.frame_index);
  trace.value = static_cast<std::int32_t>(
      std::min<std::uint64_t>(session_seq,
                              static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int32_t>::max())));
  trace.damage = static_cast<std::int32_t>(
      std::min<std::uint64_t>(last_seq,
                              static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int32_t>::max())));
  trace.command = std::move(kind);
  trace.label = detail;
  trace.success = false;
  dispatch.legacy_traces.push_back(std::move(trace));
  return false;
}

/**
 * @brief 将网关事件放入待发送队列
 *
 * @details 将 dispatch 中的 session_events 分类：
 *          - send_packet / send_packet_and_close / force_disconnect: 放入待发送队列
 *          - 其他类型: 保留在 dispatch 中由 flush_dispatch 处理
 *
 * @param dispatch 运行时分发数据
 */
void WorldService::queue_gate_events(RuntimeDispatch& dispatch) {
  if (dispatch.session_events.empty()) {
    return;
  }

  std::scoped_lock lock(gate_events_mutex_);
  auto out = dispatch.session_events.begin();
  for (auto it = dispatch.session_events.begin(); it != dispatch.session_events.end(); ++it) {
    if (it->kind == SessionEventKind::send_packet ||
        it->kind == SessionEventKind::send_packet_and_close ||
        it->kind == SessionEventKind::force_disconnect) {
      pending_gate_events_.push_back(std::move(*it));
    } else {
      if (out != it) {
        *out = std::move(*it);
      }
      ++out;
    }
  }
  dispatch.session_events.erase(out, dispatch.session_events.end());
}

/**
 * @brief 向网关发送事件
 *
 * @details 先尝试发送到 game_gateway，如果失败且事件未指定其他网关，
 *          则尝试发送到 client_v1_game_gateway(双协议兼容)。
 *
 * @param event 会话事件
 * @return true 发送成功
 */
bool WorldService::post_gate_event(SessionEvent& event) {
  if (context_ == nullptr || context_->bus == nullptr) {
    return false;
  }
  if (event.session_id != 0) {
    if (const auto gateway = session_gateways_.find(event.session_id);
        gateway != session_gateways_.end() &&
        (event.gateway.empty() || event.gateway == "game_gateway")) {
      event.gateway = gateway->second;
    }
  }
  auto target = event.gateway;
  if (context_->bus->post(target, event)) {
    return true;
  }
  if (target == "game_gateway") {
    event.gateway = "client_v1_game_gateway";
    return context_->bus->post(event.gateway, std::move(event));
  }
  return false;
}

/**
 * @brief 运行遗留 Socket 阶段
 *
 * @details 从待发送队列中取出网关事件并发送，
 *          在 net_flush_budget_ms 预算时间内尽力发送。
 *          记录每次刷新的数量、剩余数量和耗时用于监控。
 *
 * @param now_ms 当前时间戳(毫秒)
 * @return 跟踪数据
 */
RuntimeDispatch WorldService::run_legacy_socket_stage(std::uint64_t now_ms) {
  RuntimeDispatch dispatch;
  const auto started = std::chrono::steady_clock::now();
  const auto budget_ms =
      context_ != nullptr ? static_cast<std::int64_t>(context_->config.budgets.net_flush_budget_ms)
                          : 0;
  std::uint64_t flushed = 0;

  while (true) {
    std::optional<SessionEvent> event;
    {
      std::scoped_lock lock(gate_events_mutex_);
      if (pending_gate_events_.empty()) {
        break;
      }
      event = std::move(pending_gate_events_.front());
      pending_gate_events_.pop_front();
    }

    const auto session_id = event->session_id;
    const auto posted = post_gate_event(*event);
    ++flushed;

    LegacyRuntimeTrace trace;
    trace.stage = "RunSocketRun";
    trace.action = "flush_gate_event";
    trace.actor_id = session_id;
    trace.now_ms = now_ms;
    trace.value = static_cast<std::int32_t>(flushed);
    trace.success = posted;
    dispatch.legacy_traces.push_back(std::move(trace));

    if (budget_ms > 0) {
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
      if (elapsed >= budget_ms) {
        break;
      }
    }
  }

  {
    std::scoped_lock lock(gate_events_mutex_);
    run_socket_last_flushed_ = flushed;
    run_socket_last_remaining_ = pending_gate_events_.size();
    run_socket_last_ms_ = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
  }

  return dispatch;
}

/**
 * @brief 会话生命周期状态机
 *
 *  状态                    进入方式                    退出方式
 *  ─────────────────────  ─────────────────────────  ──────────────────
 *  Disconnected(已断开)     (初始)                     TCP accept
 *  Connected(已连接)        SessionEvent::connected    CM_IDPASSWORD ok
 *  LoginPending(登录中)     PersistRequest to DB       auth response
 *  LoggedInAccount(已登录)  SM_PASSOK_SELECTSERVER     CM_SELECTSERVER
 *  SelectingCharacter(选角色) SM_SELECTSERVER_OK       CM_SELCHR
 *  EnteringWorld(进入世界)   admission advance          SM_STARTPLAY
 *  InGame(游戏中)           character loaded + map join   disconnect/kick
 *  Disconnecting(断开中)    SessionEvent::disconnected     cleanup done
 *  Kicked(踢出)             SessionEvent::force_disconnect close socket
 *
 *  踢出序列(匹配 Delphi SendForcedClose):
 *    1. 投递 send_packet_and_close，延迟 kLegacyKickCloseDelayMs (50ms)
 *    2. 网关发送 SM_OUTOFCONNECTION 给客户端，等待50ms
 *    3. 网关关闭 TCP socket
 *    4. 网关通知 world_service SessionEvent::disconnected
 *    5. WorldService 撤销认证，清理会话
 *    PR-7 验收要求使用此确切的 send_packet_and_close 路径。
 *
 *  重连规则：新角色进入世界前，旧会话必须完全清理
 *  (actor 移除、session_gateways_ 擦除)。
 */
RuntimeDispatch WorldService::handle_session_event(const SessionEvent& event) {
  if (event.kind == SessionEventKind::packet_received) {
    if (const auto run_login = decode_run_login(event.packet); run_login.has_value()) {
      const auto admission = admissions_.find(run_login->certification);
      if (admission == admissions_.end() || admission->second.account_id != run_login->account_id ||
          admission->second.character_name != run_login->character_name ||
          !can_accept(admission->second.stage, CanonicalLoginRequest::enter_world)) {
        context_->bus->post(
            "log_service",
            AuditEvent{"world.admission_fail",
                        run_login->account_id + ":" + run_login->character_name,
                        std::to_string(event.session_id)});
        return {};
      }
      admission->second.stage =
          advance(admission->second.stage, CanonicalLoginTransition::enter_game);

      session_gateways_[event.session_id] = event.gateway.empty() ? "game_gateway" : event.gateway;
      pending_loads_[make_key(run_login->account_id, run_login->character_name)] =
          PendingLoad{event.session_id, event.gateway.empty() ? "game_gateway" : event.gateway,
                      run_login->account_id, run_login->character_name,
                      run_login->certification, admission->second.stage};
      PersistRequest request;
      request.kind = PersistRequestKind::load_character;
      request.reply_to = name();
      request.account_id = run_login->account_id;
      request.character_name = run_login->character_name;
      context_->bus->post("persistence_service", std::move(request));
      return {};
    }

    const auto canonical = decode_legacy_game_command(event.session_id, event.packet);
    if (canonical.status == CanonicalParseStatus::ok && canonical.command.has_value()) {
      auto command = to_logic_command(*canonical.command);
      command.gateway = event.gateway.empty() ? "game_gateway" : event.gateway;
      command.session_seq = event.session_seq;
      command.timestamp_ms = current_frame_now_ms_;
      session_gateways_[event.session_id] = command.gateway;
      return runtime_->route_logic_command(command);
    }

    const auto body = body_to_string(event.packet);
    auto tokens = util::split(body, ' ');
    if (tokens.empty()) {
      return {};
    }

    if (tokens[0] == "ENTER") {
      const auto account = tokens.size() > 1 ? tokens[1] : "guest";
      const auto character = tokens.size() > 2 ? tokens[2] : "Hero";
      session_gateways_[event.session_id] = event.gateway.empty() ? "game_gateway" : event.gateway;
      pending_loads_[make_key(account, character)] =
          PendingLoad{event.session_id, event.gateway.empty() ? "game_gateway" : event.gateway,
                      account, character, 0};
      PersistRequest request;
      request.kind = PersistRequestKind::load_character;
      request.reply_to = name();
      request.account_id = account;
      request.character_name = character;
      context_->bus->post("persistence_service", std::move(request));
      return {};
    }

    if (tokens[0] == "MOVE") {
      const auto x = tokens.size() > 1 ? parse_i32(tokens[1]) : std::optional<std::int32_t>{0};
      const auto y = tokens.size() > 2 ? parse_i32(tokens[2]) : std::optional<std::int32_t>{0};
      if (!x.has_value() || !y.has_value()) {
        return {};
      }

      LogicCommand command;
      command.kind = LogicCommandKind::walk;
      command.gateway = event.gateway.empty() ? "game_gateway" : event.gateway;
      command.session_id = event.session_id;
      command.session_seq = event.session_seq;
      command.x = *x;
      command.y = *y;
      command.timestamp_ms = current_frame_now_ms_;
      return runtime_->route_logic_command(command);
    }

    if (tokens[0] == "ATTACK") {
      const auto x = tokens.size() > 1 ? parse_i32(tokens[1]) : std::optional<std::int32_t>{0};
      if (!x.has_value()) {
        return {};
      }

      LogicCommand command;
      command.kind = LogicCommandKind::attack;
      command.gateway = event.gateway.empty() ? "game_gateway" : event.gateway;
      command.session_id = event.session_id;
      command.session_seq = event.session_seq;
      command.x = *x;
      command.timestamp_ms = current_frame_now_ms_;
      return runtime_->route_logic_command(command);
    }
  }

  if (event.kind == SessionEventKind::disconnected) {
    if (const auto active = active_sessions_.find(event.session_id); active != active_sessions_.end()) {
      LogicCommand revoke;
      revoke.kind = LogicCommandKind::revoke_authentication;
      revoke.account_id = active->second.account_id;
      revoke.character_name = active->second.character_name;
      revoke.certification = active->second.certification;
      context_->bus->post("auth_service", std::move(revoke));
      admissions_.erase(active->second.certification);
      if (const auto account = active_accounts_.find(active->second.account_id);
          account != active_accounts_.end() && account->second == event.session_id) {
        active_accounts_.erase(account);
      }
      active_sessions_.erase(active);
    }

    LogicCommand command;
    command.kind = LogicCommandKind::logout;
    command.gateway = event.gateway.empty() ? "game_gateway" : event.gateway;
    command.session_id = event.session_id;
    command.timestamp_ms = current_frame_now_ms_;
    auto dispatch = runtime_->route_logic_command(command);
    session_gateways_.erase(event.session_id);
    return dispatch;
  }
  return {};
}

/**
 * @brief 处理逻辑指令
 *
 * @details 处理以下类型的指令：
 *          - authenticate: 创建准入记录(由 AuthService 发送)
 *          - revoke_authentication: 撤销准入/清理活跃会话(踢出重复登录)
 *          - 其他: 路由到 LogicRuntime 执行实际逻辑
 *
 * @param command 逻辑指令
 * @return 执行结果
 */
RuntimeDispatch WorldService::handle_logic_command(const LogicCommand& command) {
  if (command.kind == LogicCommandKind::authenticate) {
    if (command.certification < 2) {
      return {};
    }
    admissions_[command.certification] =
        Admission{command.account_id, command.character_name, command.certification,
                  CanonicalLoginStage::character_selected};
    return {};
  }

  if (command.kind != LogicCommandKind::revoke_authentication) {
    if (command.session_id != 0) {
      session_gateways_[command.session_id] =
          command.gateway.empty() ? "game_gateway" : command.gateway;
    }

    auto routed = command;
    if (routed.timestamp_ms == 0) {
      routed.timestamp_ms = current_frame_now_ms_;
    }
    auto dispatch = runtime_->route_logic_command(routed);
    if (routed.kind == LogicCommandKind::logout) {
      session_gateways_.erase(command.session_id);
    }
    return dispatch;
  }

  if (command.certification > 0) {
    admissions_.erase(command.certification);
  }

  for (auto it = pending_loads_.begin(); it != pending_loads_.end();) {
    if ((!command.account_id.empty() && it->second.account_id == command.account_id) ||
        (command.certification > 0 && it->second.certification == command.certification)) {
      it = pending_loads_.erase(it);
    } else {
      ++it;
    }
  }

  std::vector<std::uint64_t> sessions_to_close;
  for (const auto& [session_id, admission] : active_sessions_) {
    if ((!command.account_id.empty() && admission.account_id == command.account_id) ||
        (command.certification > 0 && admission.certification == command.certification)) {
      sessions_to_close.push_back(session_id);
    }
  }

  RuntimeDispatch dispatch;
  for (const auto session_id : sessions_to_close) {
    if (const auto active = active_sessions_.find(session_id); active != active_sessions_.end()) {
      context_->bus->post(
          "game_gateway",
          SessionEvent{SessionEventKind::send_packet_and_close,
                       "game_gateway",
                       session_id,
                       {},
                       make_out_of_connection_packet(session_id),
                       "duplicate_login",
                       kLegacyKickCloseDelayMs});

      LogicCommand logout;
      logout.kind = LogicCommandKind::logout;
      logout.session_id = session_id;
      append_dispatch(dispatch, runtime_->route_logic_command(logout));

      if (const auto account = active_accounts_.find(active->second.account_id);
          account != active_accounts_.end() && account->second == session_id) {
        active_accounts_.erase(account);
      }
      admissions_.erase(active->second.certification);
      active_sessions_.erase(active);
    }
  }
  return dispatch;
}

/**
 * @brief 处理持久化结果
 *
 * @details 根据结果类型处理：
 *          - guild_castle_snapshot_loaded: 更新公会城堡快照
 *          - merchant_states_loaded: 应用商家状态
 *          - castle_dialog_context_loaded: 更新城堡对话框上下文
 *          - character_loaded: 处理角色加载完成(进入游戏或离线操作)
 *
 * @param result 持久化结果
 * @return 处理结果
 */
RuntimeDispatch WorldService::handle_persist_result(const PersistResult& result) {
  if (result.kind == PersistResultKind::error && util::starts_with(result.request_id, "guild_offline")) {
    ++offline_guild_error_count_;
  }
  if (result.kind == PersistResultKind::guild_castle_snapshot_loaded) {
    castle_context_refresh_in_flight_ = false;
    guild_castle_snapshot_ = result.guild_castle_snapshot;
    apply_runtime_castle_defaults(context_->config.runtime, guild_castle_snapshot_);
    castle_dialog_context_ = guild_castle_snapshot_.castle_dialog;
    ++castle_context_refresh_count_;
    runtime_->set_guild_castle_snapshot(guild_castle_snapshot_);
    return {};
  }

  if (result.kind == PersistResultKind::merchant_states_loaded) {
    runtime_->apply_merchant_states(result.merchant_states);
    return {};
  }

  if (result.kind == PersistResultKind::castle_dialog_context_loaded) {
    castle_context_refresh_in_flight_ = false;
    castle_dialog_context_ = result.castle_dialog_context;
    apply_runtime_castle_defaults(context_->config.runtime, castle_dialog_context_);
    guild_castle_snapshot_.castle_dialog = castle_dialog_context_;
    ++castle_context_refresh_count_;
    runtime_->set_castle_dialog_context(castle_dialog_context_);
    return {};
  }

  if (result.kind != PersistResultKind::character_loaded) {
    return {};
  }

  if (!result.account_id.empty() && !result.character_name.empty()) {
    const auto key = make_key(result.account_id, result.character_name);
    auto& version = character_save_versions_[key];
    version = std::max(version, result.character.save_version);
  }

  if (util::starts_with(result.request_id, "guild_offline")) {
    ++offline_guild_result_count_;
  }
  if (const auto offline_operation = decode_offline_guild_character_op(result.request_id);
      offline_operation.has_value()) {
    auto operation = *offline_operation;
    ++offline_guild_route_count_;
    ActorMail mail;
    mail.kind = ActorMailKind::persistence_loaded;
    mail.map_id = operation.map_id;
    mail.actor_id = operation.initiator_actor_id;
    mail.character = result.character;
    mail.name = result.character_name.empty() ? operation.target_name : result.character_name;
    if (runtime_ != nullptr) {
      if (const auto target_locator = runtime_->locate_character_actor(mail.name);
          target_locator.has_value()) {
        operation.target_map_id = target_locator->first;
        mail.target_actor_id = target_locator->second;
        if (const auto live_character = runtime_->snapshot_character_actor(mail.name);
            live_character.has_value()) {
          mail.character = *live_character;
        }
      }
    }
    mail.payload = encode_offline_guild_character_op(operation);
    return runtime_->route_actor_mail(mail);
  }

  const auto key = make_key(result.account_id, result.character_name);
  const auto pending = pending_loads_.find(key);
  if (pending == pending_loads_.end()) {
    return {};
  }

  LegacyReadyUser ready_user;
  ready_user.session_id = pending->second.session_id;
  ready_user.gateway = pending->second.gateway.empty() ? "game_gateway" : pending->second.gateway;
  ready_user.account_id = result.account_id;
  ready_user.character_name = result.character_name;
  ready_user.map_id = result.character.map_id;
  ready_user.x = result.character.x;
  ready_user.y = result.character.y;
  ready_user.character = result.character;
  auto dispatch = runtime_->enqueue_ready_user(std::move(ready_user));
  const auto rejected = std::any_of(
      dispatch.session_events.begin(), dispatch.session_events.end(),
      [&](const SessionEvent& event) {
        return event.session_id == pending->second.session_id &&
               (event.kind == SessionEventKind::force_disconnect ||
                event.kind == SessionEventKind::send_packet_and_close);
      });
  if (!rejected && pending->second.certification > 0) {
    if (auto admission = admissions_.find(pending->second.certification);
        admission != admissions_.end()) {
      admission->second.stage =
          advance(pending->second.stage, CanonicalLoginTransition::enter_game_complete);
    }
    active_sessions_[pending->second.session_id] =
        Admission{result.account_id, result.character_name, pending->second.certification,
                  CanonicalLoginStage::in_game};
    active_accounts_[result.account_id] = pending->second.session_id;
  }
  pending_loads_.erase(pending);
  return dispatch;
}

/**
 * @brief 分配角色保存版本号
 *
 * @details 遍历 dispatch 中的保存请求，为每个角色分配递增的
 *          保存版本号，确保并发保存的正确排序。
 */
void WorldService::assign_character_save_versions(RuntimeDispatch& dispatch) {
  for (auto& request : dispatch.persist_requests) {
    if (request.kind != PersistRequestKind::save_character) {
      continue;
    }
    const auto account_id =
        request.character.account_id.empty() ? request.account_id : request.character.account_id;
    const auto character_name = request.character.character_name.empty()
                                    ? request.character_name
                                    : request.character.character_name;
    if (account_id.empty() || character_name.empty()) {
      continue;
    }

    auto& version = character_save_versions_[make_key(account_id, character_name)];
    version = std::max(version, request.character.save_version);
    ++version;
    request.account_id = account_id;
    request.character_name = character_name;
    request.character.account_id = account_id;
    request.character.character_name = character_name;
    request.character.save_version = version;
  }
}

/**
 * @brief 刷新分发数据
 *
 * @details 按顺序执行以下操作：
 *          1. 分配角色保存版本号
 *          2. 将网关事件放入待发送队列
 *          3. 发送会话事件到网关(失败的尝试备用网关)
 *          4. 发送审计事件到日志服务
 *          5. 发送持久化请求
 *          6. 重新投递跨地图邮件
 *
 * @param dispatch 运行时分发数据
 */
void WorldService::flush_dispatch(RuntimeDispatch dispatch) {
  assign_character_save_versions(dispatch);
  queue_gate_events(dispatch);
  for (auto& event : dispatch.session_events) {
    if (event.session_id != 0) {
      if (const auto gateway = session_gateways_.find(event.session_id);
          gateway != session_gateways_.end() &&
          (event.gateway.empty() || event.gateway == "game_gateway")) {
        event.gateway = gateway->second;
      }
    }
    auto target = event.gateway;
    if (!context_->bus->post(target, event) && target == "game_gateway") {
      event.gateway = "client_v1_game_gateway";
      context_->bus->post(event.gateway, std::move(event));
    }
  }
  for (const auto& audit : dispatch.audit_events) {
    context_->bus->post("log_service", audit);
    PersistRequest request;
    request.kind = PersistRequestKind::record_audit;
    request.account_id = audit.session_key;
    request.text = audit.category + "|" + audit.message;
    context_->bus->post("persistence_service", std::move(request));
  }
  for (auto& request : dispatch.persist_requests) {
    context_->bus->post("persistence_service", std::move(request));
  }
  for (auto& mail : dispatch.cross_map_mails) {
    context_->bus->post(name(), std::move(mail));
  }
}

}  // namespace mir2
