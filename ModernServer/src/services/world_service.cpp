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

// ── Security & anomaly protection ───────────────────────────────────
//
//  The following legacy-compatible protections are enforced:
//  1. Server authority: client never directly sets coordinates, stats,
//     gold, or item ownership — all mutations go through LogicRuntime.
//  2. Session_seq watermark: stale/replayed messages are rejected
//     (accept_ingress_sequence enforces monotonic per-session sequence).
//  3. Login-state gate: gameplay commands are only accepted when
//     in_game() returns true; pre-login packets are dropped.
//  4. Packet bounds: LegacyProtocolCodec enforces 64KB max frame,
//     decode_6bit_buf validates character range, decode_legacy_game_packet
//     checks minimum payload length.
//  5. Bus backpressure: gateway pauses or disconnects sessions when
//     the world_service queue exceeds the configured threshold.
//
//  Remaining gaps tracked for PR-9 completion:
//  - Per-session send rate limit (matching Delphi RUNGate CheckSendLength)
//  - Packet fuzz harness in CI
//  - Login brute-force detection
// ──────────────────────────────────────────────────────────────────────
constexpr std::int32_t kLegacyKickCloseDelayMs = 50;

struct RunLoginPayload {
  std::string account_id{};
  std::string character_name{};
  std::int32_t certification{0};
  std::int32_t client_version{0};
  std::int32_t client_checksum{0};
  bool start_new{false};
};

std::string make_key(const std::string& account, const std::string& character) {
  return account + ":" + character;
}

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

std::string body_to_string(const LegacyPacket& packet) {
  return std::string(packet.body.begin(), packet.body.end());
}

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

void apply_runtime_castle_defaults(const RuntimeConfig& runtime_config,
                                   GuildCastleSnapshot& guild_castle_snapshot) {
  apply_runtime_castle_defaults(runtime_config, guild_castle_snapshot.castle_dialog);
}

std::string display_castle_owner(const CastleDialogContext& castle_dialog_context) {
  return util::trim(castle_dialog_context.owner_guild).empty()
             ? castle_dialog_context.unclaimed_owner_label
             : castle_dialog_context.owner_guild;
}

std::string display_castle_lord(const CastleDialogContext& castle_dialog_context) {
  return util::trim(castle_dialog_context.owner_guild).empty() ||
                 util::trim(castle_dialog_context.lord).empty()
             ? castle_dialog_context.unclaimed_lord_label
             : castle_dialog_context.lord;
}

LegacyPacket make_out_of_connection_packet(std::uint64_t session_id) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmOutOfConnection, 0, 0, 0, 0));
}

}  // namespace

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

void WorldService::clear_session_actions_for_test() {
}

std::size_t WorldService::session_action_count_for_test() const {
  return 0;
}

std::size_t WorldService::session_action_reject_count_for_test() const {
  return 0;
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

// Legacy frame ordering guarantees:
// 1. Socket receive order → Bus FIFO post → monotonic ingress_seq
// 2. Same-session FIFO via session_seq watermark (accept_ingress_sequence)
// 3. Same-frame batch matches Delphi RunSocket.Run drain-then-dispatch
// 4. Per-player inbox FIFO via route_logic_command enqueue order
// 5. Dispatch order preserved by flush_dispatch at frame end
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

// ── Session lifecycle state machine ──────────────────────────────────
//
//  State                  Entered by                     Exited by
//  ─────────────────────  ─────────────────────────────  ──────────────────
//  Disconnected           (initial)                      TCP accept
//  Connected              SessionEvent::connected         CM_IDPASSWORD ok
//  LoginPending           PersistRequest to DB           auth response
//  LoggedInAccount        SM_PASSOK_SELECTSERVER          CM_SELECTSERVER
//  SelectingCharacter     SM_SELECTSERVER_OK              CM_SELCHR
//  EnteringWorld          admission advance               SM_STARTPLAY
//  InGame                 character loaded + map joined    disconnect/kick
//  Disconnecting          SessionEvent::disconnected       cleanup done
//  Kicked                 SessionEvent::force_disconnect   close socket
//
//  Kick sequence (matches Delphi SendForcedClose):
//    1. Post send_packet_and_close with kLegacyKickCloseDelayMs (50ms)
//    2. Gateway sends SM_OUTOFCONNECTION to client, waits 50ms
//    3. Gateway closes TCP socket
//    4. Gateway notifies world_service with SessionEvent::disconnected
//    5. WorldService revokes authentication, cleans up session
//    PR-7 acceptance requires this exact send_packet_and_close path.
//
//  Reconnect rule: old session must be fully cleaned (actor removed,
//  session_gateways_ erased) before a new session for the same character
//  can enter the world.
// ──────────────────────────────────────────────────────────────────────
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
