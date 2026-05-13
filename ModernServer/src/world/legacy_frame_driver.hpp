#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/messages.hpp"
#include "world/game_object.hpp"

namespace mir2 {

enum class LegacyFrameStage {
  run_socket_run,
  decode_id_socket,
  user_engine_execute_run,
  event_manager_run,
  server_message_run
};

[[nodiscard]] std::string_view legacy_frame_stage_name(LegacyFrameStage stage);

struct WorldIngressMessage {
  BusMessage message{};
  std::uint64_t ingress_seq{0};
  std::uint64_t frame_index{0};

  WorldIngressMessage() = default;
  WorldIngressMessage(BusMessage value, std::uint64_t ingress = 0,
                      std::uint64_t frame = 0)
      : message(std::move(value)), ingress_seq(ingress), frame_index(frame) {}
  WorldIngressMessage(SessionEvent value) : message(std::move(value)) {}
  WorldIngressMessage(LogicCommand value) : message(std::move(value)) {}
  WorldIngressMessage(ActorMail value) : message(std::move(value)) {}
  WorldIngressMessage(PersistRequest value) : message(std::move(value)) {}
  WorldIngressMessage(PersistResult value) : message(std::move(value)) {}
  WorldIngressMessage(AuditEvent value) : message(std::move(value)) {}
};

struct WorldIngressBatch {
  std::vector<WorldIngressMessage> messages{};

  [[nodiscard]] bool empty() const { return messages.empty(); }
  [[nodiscard]] std::size_t size() const { return messages.size(); }
  void push(BusMessage message, std::uint64_t ingress_seq = 0) {
    messages.emplace_back(std::move(message), ingress_seq);
  }
  void mark_frame(std::uint64_t frame_index) {
    for (auto& message : messages) {
      message.frame_index = frame_index;
    }
  }
};

struct LegacyFrameStageTrace {
  LegacyFrameStage stage{LegacyFrameStage::run_socket_run};
  std::uint64_t now_ms{0};
  std::uint64_t elapsed_us{0};
  std::size_t input_count{0};
  std::size_t output_count{0};
};

struct LegacyFrameTrace {
  std::uint64_t frame_index{0};
  std::uint64_t now_ms{0};
  std::uint64_t elapsed_us{0};
  std::uint64_t last_frame_ms{0};
  std::string last_stage{};
  std::vector<LegacyFrameStageTrace> stages{};
};

struct LegacyFrameCallbacks {
  std::function<RuntimeDispatch()> run_socket_run{};
  std::function<RuntimeDispatch(WorldIngressBatch&)> decode_id_socket{};
  std::function<RuntimeDispatch()> user_engine_execute_run{};
  std::function<RuntimeDispatch()> event_manager_run{};
  std::function<RuntimeDispatch()> server_message_run{};
};

// CI trace & golden verification coverage:
// - legacy_frame_smoke: frame stage ordering, FIFO, trace output counts
// - legacy_protocol_command_golden_smoke: encode/decode golden vectors
// - core_smoke: codec unit tests, check-code strip, body round-trip
// - world_invalid_command_smoke: stale sequence rejection
// - legacy_frame_smoke check_session_fifo_ordering: per-frame action gate
class LegacyFrameDriver {
 public:
  [[nodiscard]] RuntimeDispatch run_frame(std::uint64_t now_ms, WorldIngressBatch ingress_batch,
                                          const LegacyFrameCallbacks& callbacks);

  [[nodiscard]] const LegacyFrameTrace& last_trace() const { return last_trace_; }
  [[nodiscard]] std::uint64_t frame_index() const { return frame_index_; }

 private:
  std::uint64_t frame_index_{0};
  LegacyFrameTrace last_trace_{};
};

}  // namespace mir2
