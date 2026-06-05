/**
 * @file legacy_frame_driver.hpp
 * @brief 传统帧驱动器的头文件，定义了帧处理流水线的各个阶段及其数据结构
 * @details 该文件实现了游戏服务器主循环的帧驱动机制，将传统的 Delphi 游戏服务端单帧处理
 *          分解为五个连续的阶段（run_socket_run -> decode_id_socket -> user_engine_execute_run
 *          -> event_manager_run -> server_message_run），并为每个阶段提供时序追踪能力。
 *          帧驱动器负责接收网络消息批次、调度到各个处理阶段、并收集各阶段的输出结果。
 */

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

/**
 * @enum LegacyFrameStage
 * @brief 传统游戏帧处理的五个连续阶段枚举
 * @details 对应 Delphi 服务端 RunSocket.Run 方法的五阶段分解：
 *          run_socket_run - 网络套接字 I/O 处理
 *          decode_id_socket - 协议解码与会话标识
 *          user_engine_execute_run - 用户引擎逻辑执行
 *          event_manager_run - 事件管理器处理
 *          server_message_run - 服务端消息发送
 */
enum class LegacyFrameStage {
  run_socket_run,          ///< 套接字运行阶段：处理网络收发
  decode_id_socket,        ///< 解码与标识阶段：解析协议数据
  user_engine_execute_run, ///< 用户引擎执行阶段：处理玩家逻辑
  event_manager_run,       ///< 事件管理器运行阶段：驱动游戏事件
  server_message_run       ///< 服务端消息发送阶段：发送结果数据
};

/**
 * @brief 获取帧阶段名称的字符串表示
 * @param stage 帧阶段枚举值
 * @return 阶段名称的字符串视图
 */
[[nodiscard]] std::string_view legacy_frame_stage_name(LegacyFrameStage stage);

/**
 * @struct WorldIngressMessage
 * @brief 世界入口消息的封装结构
 * @details 将原始总线消息与入站序列号和帧索引关联，用于消息追踪和 FIFO 顺序保证。
 *          支持从 SessionEvent、LogicCommand、ActorMail、PersistRequest、
 *          PersistResult、AuditEvent 等多种消息类型构造。
 */
struct WorldIngressMessage {
  BusMessage message{};       ///< 原始总线消息
  std::uint64_t ingress_seq{0};  ///< 入站序列号，用于消息顺序追踪
  std::uint64_t frame_index{0};  ///< 处理该消息的帧索引

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

/**
 * @struct WorldIngressBatch
 * @brief 单帧内处理的消息批次集合
 * @details 每一帧处理开始前，将从网络接收的所有消息打包为一个批次。
 *          批次内的消息会统一标记相同的帧索引，确保因果顺序。
 */
struct WorldIngressBatch {
  std::vector<WorldIngressMessage> messages{};  ///< 批次内的消息列表

  [[nodiscard]] bool empty() const { return messages.empty(); }
  [[nodiscard]] std::size_t size() const { return messages.size(); }

  /// @brief 向批次中添加一条总线消息
  void push(BusMessage message, std::uint64_t ingress_seq = 0) {
    messages.emplace_back(std::move(message), ingress_seq);
  }

  /// @brief 为批次中所有消息标记统一的帧索引
  void mark_frame(std::uint64_t frame_index) {
    for (auto& message : messages) {
      message.frame_index = frame_index;
    }
  }
};

/**
 * @struct LegacyFrameStageTrace
 * @brief 帧内单个阶段的执行追踪信息
 * @details 记录每阶段开始时间、耗时、输入/输出消息数量，用于性能分析和问题诊断。
 */
struct LegacyFrameStageTrace {
  LegacyFrameStage stage{LegacyFrameStage::run_socket_run}; ///< 阶段标识
  std::uint64_t now_ms{0};  ///< 阶段开始时的系统时间（毫秒）
  std::uint64_t elapsed_us{0};  ///< 阶段执行耗时（微秒）
  std::size_t input_count{0};   ///< 输入消息数量
  std::size_t output_count{0};  ///< 输出消息数量
};

/**
 * @struct LegacyFrameTrace
 * @brief 整帧执行的完整追踪信息
 * @details 包含帧索引、时间戳、总耗时、上一帧间隔以及各阶段的详细追踪数据。
 *          用于帧率监控和性能瓶颈分析。
 */
struct LegacyFrameTrace {
  std::uint64_t frame_index{0};   ///< 帧序号，单调递增
  std::uint64_t now_ms{0};        ///< 帧开始时间（毫秒）
  std::uint64_t elapsed_us{0};    ///< 整帧执行耗时（微秒）
  std::uint64_t last_frame_ms{0}; ///< 距离上一帧的间隔（毫秒）
  std::string last_stage{};       ///< 上一帧最后执行的阶段名称
  std::vector<LegacyFrameStageTrace> stages{};  ///< 各阶段的详细追踪数据
};

/**
 * @struct LegacyFrameCallbacks
 * @brief 帧驱动器的五个阶段回调函数集合
 * @details 每个阶段对应一个回调函数，由帧驱动器在 run_frame 中依次调用。
 *          各回调返回 RuntimeDispatch 结构，帧驱动器将所有阶段的 dispatch
 *          结果合并后返回。
 */
struct LegacyFrameCallbacks {
  std::function<RuntimeDispatch()> run_socket_run{};           ///< 套接字运行回调
  std::function<RuntimeDispatch(WorldIngressBatch&)> decode_id_socket{}; ///< 解码与标识回调
  std::function<RuntimeDispatch()> user_engine_execute_run{};  ///< 用户引擎执行回调
  std::function<RuntimeDispatch()> event_manager_run{};        ///< 事件管理器回调
  std::function<RuntimeDispatch()> server_message_run{};       ///< 服务端消息发送回调
};

// CI 测试覆盖说明：
// - legacy_frame_smoke: 帧阶段顺序、FIFO 保证、追踪输出计数
// - legacy_protocol_command_golden_smoke: 编码/解码黄金向量测试
// - core_smoke: 编解码单元测试、校验码剥离、消息体往返测试
// - world_invalid_command_smoke: 过期序列号拒绝测试
// - legacy_frame_smoke check_session_fifo_ordering: 按会话 FIFO 准入

/**
 * @class LegacyFrameDriver
 * @brief 传统帧驱动器主类
 * @details 负责驱动游戏服务器主循环的帧处理过程。每帧按五阶段流水线执行：
 *          1. run_socket_run - 处理网络 I/O
 *          2. decode_id_socket - 解码入站消息
 *          3. user_engine_execute_run - 执行游戏逻辑
 *          4. event_manager_run - 驱动游戏事件
 *          5. server_message_run - 发送出站消息
 *
 *          帧驱动器不关心各阶段的具体实现逻辑，仅负责按序调度并收集结果。
 */
class LegacyFrameDriver {
 public:
  /**
   * @brief 执行一帧处理
   * @param now_ms 当前系统时间（毫秒）
   * @param ingress_batch 本帧待处理的入站消息批次
   * @param callbacks 五个阶段的回调函数集合
   * @return 合并后的 RuntimeDispatch，包含所有阶段产生的会话事件、审计事件等
   */
  [[nodiscard]] RuntimeDispatch run_frame(std::uint64_t now_ms, WorldIngressBatch ingress_batch,
                                          const LegacyFrameCallbacks& callbacks);

  /// @brief 获取上一帧的执行追踪信息
  [[nodiscard]] const LegacyFrameTrace& last_trace() const { return last_trace_; }

  /// @brief 获取当前帧序号
  [[nodiscard]] std::uint64_t frame_index() const { return frame_index_; }

 private:
  std::uint64_t frame_index_{0};  ///< 帧序号计数器
  LegacyFrameTrace last_trace_{}; ///< 上一帧的追踪信息
};

}  // namespace mir2
