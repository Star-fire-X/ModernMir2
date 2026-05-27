#pragma once

#include <chrono>
#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/module.hpp"
#include "protocol/canonical_login_state.hpp"
#include "world/legacy_frame_driver.hpp"
#include "world/logic_runtime.hpp"

namespace mir2 {

class WorldService : public Module {
 public:
  WorldService() = default;
  ~WorldService() override {
    stop();
    join();
  }

  [[nodiscard]] std::string name() const override { return "world_service"; }
  void start(HostContext& context) override;
  void stop() override;
  void join() override;
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

#ifdef MIR2_ENABLE_TEST_HOOKS
  void attach_context_for_test(HostContext& context);
  void initialize_runtime_for_test(const HostConfig& config);
  void enqueue_gate_event_for_test(SessionEvent event);
  void seed_session_sequence_for_test(std::uint64_t session_id, std::uint64_t session_seq);
  [[nodiscard]] std::size_t legacy_session_inbox_size_for_test(
      std::uint64_t session_id) const;
  [[nodiscard]] std::vector<std::uint64_t> legacy_session_inbox_sequences_for_test(
      std::uint64_t session_id) const;
  [[nodiscard]] RuntimeDispatch tick_runtime_for_test(std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch run_legacy_socket_stage_for_test(std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch process_ingress_batch_for_test(WorldIngressBatch& batch);
#endif

 private:
  struct PendingLoad {
    std::uint64_t session_id{0};
    std::string gateway{"game_gateway"};
    std::string account_id{};
    std::string character_name{};
    std::int32_t certification{0};
    CanonicalLoginStage stage{CanonicalLoginStage::entering_game};
  };

  struct Admission {
    std::string account_id{};
    std::string character_name{};
    std::int32_t certification{0};
    CanonicalLoginStage stage{CanonicalLoginStage::character_selected};
  };

  void run();
  void request_castle_dialog_context_refresh();
  [[nodiscard]] RuntimeDispatch process_ingress_batch(WorldIngressBatch& batch);
  [[nodiscard]] bool accept_ingress_sequence(const WorldIngressMessage& ingress,
                                             RuntimeDispatch& dispatch);
  [[nodiscard]] RuntimeDispatch handle_session_event(const SessionEvent& event);
  [[nodiscard]] RuntimeDispatch handle_logic_command(const LogicCommand& command);
  [[nodiscard]] RuntimeDispatch handle_persist_result(const PersistResult& result);
  void queue_gate_events(RuntimeDispatch& dispatch);
  [[nodiscard]] RuntimeDispatch run_legacy_socket_stage(std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch run_server_message_stage(std::uint64_t now_ms);
  bool post_gate_event(SessionEvent& event);
  void assign_character_save_versions(RuntimeDispatch& dispatch);
  void flush_dispatch(RuntimeDispatch dispatch);

  HostContext* context_{nullptr};
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};
  std::thread worker_{};
  std::atomic_bool running_{false};
  std::unique_ptr<LogicRuntime> runtime_{};
  LegacyFrameDriver legacy_frame_driver_{};
  mutable std::mutex legacy_frame_mutex_{};
  LegacyFrameTrace legacy_frame_trace_{};
  bool legacy_frame_seen_{false};
  std::unordered_map<std::string, PendingLoad> pending_loads_{};
  std::unordered_map<std::int32_t, Admission> admissions_{};
  std::unordered_map<std::uint64_t, Admission> active_sessions_{};
  std::unordered_map<std::string, std::uint64_t> active_accounts_{};
  std::unordered_map<std::string, std::uint64_t> character_save_versions_{};
  std::unordered_map<std::uint64_t, std::string> session_gateways_{};
  std::unordered_map<std::uint64_t, std::uint64_t> session_sequence_watermarks_{};
  std::uint64_t next_ingress_seq_{0};
  std::uint64_t current_frame_now_ms_{0};
  mutable std::mutex gate_events_mutex_{};
  std::deque<SessionEvent> pending_gate_events_{};
  std::uint64_t run_socket_last_flushed_{0};
  std::uint64_t run_socket_last_remaining_{0};
  std::uint64_t run_socket_last_ms_{0};
  CastleDialogContext castle_dialog_context_{};
  GuildCastleSnapshot guild_castle_snapshot_{};
  std::chrono::steady_clock::time_point next_castle_context_refresh_{};
  std::uint64_t castle_context_refresh_count_{0};
  std::uint64_t offline_guild_result_count_{0};
  std::uint64_t offline_guild_route_count_{0};
  std::uint64_t offline_guild_error_count_{0};
  bool castle_context_refresh_in_flight_{false};
};

}  // namespace mir2
