#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
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

struct WorldIngressBatch {
  std::vector<BusMessage> messages{};

  [[nodiscard]] bool empty() const { return messages.empty(); }
  [[nodiscard]] std::size_t size() const { return messages.size(); }
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
