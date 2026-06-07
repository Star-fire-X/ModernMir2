/**
 * @file legacy_frame_driver.cpp
 * @brief 传统帧驱动器实现文件
 * @details 实现了五阶段帧处理流水线的核心逻辑。每帧按顺序执行五个阶段，
 *          收集各阶段的 RuntimeDispatch 结果并合并返回。提供帧级别的
 *          时序追踪和性能诊断功能。
 *
 *          Delphi RunSocket.Run 到 C++ 帧阶段的映射说明：
 *          1. RunSocketRun（接收网关缓冲区 + 刷新上一帧出站数据）
 *          2. DecodeIdSocket（解析消息头 + 路由到用户引擎）
 *          3. UserEngineExecuteRun（处理玩家、怪物、商人、NPC）
 *          4. EventManagerRun（定时事件处理）
 *          5. ServerMessageRun（广播系统消息）
 *
 * @note 阶段顺序固定不可重排，Delphi 脚本和客户端渲染依赖于处理顺序
 */

#include "world/legacy_frame_driver.hpp"

#include <chrono>
#include <iterator>
#include <type_traits>
#include <utility>

namespace mir2 {

namespace {

/**
 * @brief 计算 RuntimeDispatch 中所有输出项目的总数
 * @param dispatch 待统计的 dispatch 结构
 * @return 会话事件、审计事件、持久化请求、跨地图邮件等所有输出项目的总和
 */
[[nodiscard]] std::size_t dispatch_count(const RuntimeDispatch& dispatch) {
  return dispatch.session_events.size() + dispatch.audit_events.size() +
         dispatch.persist_requests.size() + dispatch.cross_map_mails.size() +
         dispatch.interserver_broadcasts.size() +
         dispatch.legacy_time_recall_requests.size() + dispatch.legacy_traces.size();
}

/**
 * @brief 将源 dispatch 的内容追加到目标 dispatch 中
 * @param target 目标 dispatch，接收所有合并结果
 * @param source 源 dispatch，使用移动语义将其内容转移到 target 中
 * @details 合并的内容包括：会话事件、审计事件、持久化请求、跨地图邮件、
 *          时间召回请求和追踪记录。使用 std::make_move_iterator 避免拷贝开销。
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
  target.interserver_broadcasts.insert(
      target.interserver_broadcasts.end(),
      std::make_move_iterator(source.interserver_broadcasts.begin()),
      std::make_move_iterator(source.interserver_broadcasts.end()));
  target.legacy_time_recall_requests.insert(
      target.legacy_time_recall_requests.end(),
      std::make_move_iterator(source.legacy_time_recall_requests.begin()),
      std::make_move_iterator(source.legacy_time_recall_requests.end()));
  target.legacy_traces.insert(target.legacy_traces.end(),
                              std::make_move_iterator(source.legacy_traces.begin()),
                              std::make_move_iterator(source.legacy_traces.end()));
}

}  // namespace

/**
 * @brief 获取帧阶段的名称字符串
 * @param stage 帧阶段枚举值
 * @return 阶段名称，驼峰式英文命名
 */
std::string_view legacy_frame_stage_name(LegacyFrameStage stage) {
  switch (stage) {
    case LegacyFrameStage::run_socket_run:
      return "RunSocketRun";
    case LegacyFrameStage::decode_id_socket:
      return "DecodeIdSocket";
    case LegacyFrameStage::user_engine_execute_run:
      return "UserEngineExecuteRun";
    case LegacyFrameStage::event_manager_run:
      return "EventManagerRun";
    case LegacyFrameStage::server_message_run:
      return "ServerMessageRun";
  }
  return "Unknown";
}

// ── Delphi RunSocket.Run → C++ 帧阶段映射 ──────────────────
//
//  Delphi (RunSock.pas)              C++ (LegacyFrameDriver)
//  ─────────────────────────────     ──────────────────────────
//  1. 接收网关缓冲区                RunSocketRun
//     + 刷新上一帧出站数据            (刷新上一帧 dispatch,
//                                     处理待决的网关事件)
//  2. DecodeIdSocket                DecodeIdSocket
//     (解析 TMsgHeader +              (处理入站批次:
//      路由到用户引擎)                 接受入站消息, 分发到处理器)
//  3. UserEngine.ExecuteRun         UserEngineExecuteRun
//     (ProcessUserHumans →            (runtime_->tick:
//      ProcessMonsters →              处理就绪用户、怪物、商人、NPC)
//      ProcessMerchants →
//      ProcessNpcs)
//  4. EventMan.Run                  EventManagerRun
//     (定时事件)                      (传统事件管理器 tick)
//  5. ServerMessage.Run             ServerMessageRun
//     (广播系统消息)                  (预留, 当前为空)
//
//  阶段顺序固定不可重排 — Delphi 脚本和客户端渲染依赖于处理顺序。
// ──────────────────────────────────────────────────────────────

/**
 * @brief 执行一帧的完整处理流水线
 * @param now_ms 当前系统时间（毫秒）
 * @param ingress_batch 本帧待处理的入站消息批次
 * @param callbacks 五个阶段的回调函数集合
 * @return 合并所有阶段输出的 RuntimeDispatch
 * @details 处理流程：
 *          1. 帧序号自增，标记入站批次帧索引
 *          2. 依次执行五个阶段，使用 lambda run_stage 统一计时和追踪
 *          3. 每个阶段记录输入/输出计数和执行耗时（微秒）
 *          4. decode_id_socket 后检查入站批次是否已清空
 *          5. 保存本帧追踪信息到 last_trace_
 * @note 使用 if constexpr 区分是否有 ingress_batch 参数的阶段回调
 */
RuntimeDispatch LegacyFrameDriver::run_frame(std::uint64_t now_ms, WorldIngressBatch ingress_batch,
                                             const LegacyFrameCallbacks& callbacks) {
  RuntimeDispatch combined;
  LegacyFrameTrace trace;
  trace.frame_index = ++frame_index_;
  trace.now_ms = now_ms;
  trace.stages.reserve(5);
  ingress_batch.mark_frame(trace.frame_index);

  const auto frame_start = std::chrono::steady_clock::now();

  /**
   * @brief 执行单个阶段的 lambda 辅助函数
   * @param stage 当前阶段枚举值
   * @param input_count 输入消息数量
   * @param callback 阶段回调函数（可能接受 ingress_batch 或不接受）
   * @details 使用 if constexpr 编译期判断回调签名，统一执行计时和追踪记录
   */
  const auto run_stage = [&](LegacyFrameStage stage, std::size_t input_count,
                             auto&& callback) {
    const auto stage_start = std::chrono::steady_clock::now();
    RuntimeDispatch dispatch;
    if constexpr (std::is_invocable_r_v<RuntimeDispatch, decltype(callback), WorldIngressBatch&>) {
      dispatch = callback(ingress_batch);
    } else {
      dispatch = callback();
    }
    const auto elapsed_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - stage_start)
            .count());

    LegacyFrameStageTrace stage_trace;
    stage_trace.stage = stage;
    stage_trace.now_ms = now_ms;
    stage_trace.elapsed_us = elapsed_us;
    stage_trace.input_count = input_count;
    stage_trace.output_count = dispatch_count(dispatch);
    trace.last_stage = std::string(legacy_frame_stage_name(stage));
    trace.stages.push_back(stage_trace);
    append_dispatch(combined, std::move(dispatch));
  };

  // 阶段 1: 套接字运行 — 处理网络 I/O
  run_stage(LegacyFrameStage::run_socket_run, 0, [&]() -> RuntimeDispatch {
    if (callbacks.run_socket_run) {
      return callbacks.run_socket_run();
    }
    return {};
  });

  // 阶段 2: 解码与标识 — 解析入站消息
  run_stage(LegacyFrameStage::decode_id_socket, ingress_batch.size(),
            [&](WorldIngressBatch& batch) -> RuntimeDispatch {
              if (callbacks.decode_id_socket) {
                return callbacks.decode_id_socket(batch);
              }
              return {};
            });

  // 安全检查：解码阶段后入站批次应已被清空
  if (!ingress_batch.empty()) {
    LegacyRuntimeTrace guard;
    guard.stage = std::string(legacy_frame_stage_name(LegacyFrameStage::decode_id_socket));
    guard.action = "ingress_batch_not_cleared";
    guard.now_ms = now_ms;
    guard.current_tick = trace.frame_index;
    guard.cursor = ingress_batch.size();
    guard.success = false;
    combined.legacy_traces.push_back(std::move(guard));
    ingress_batch.messages.clear();
  }

  // 阶段 3: 用户引擎执行 — 游戏逻辑处理
  run_stage(LegacyFrameStage::user_engine_execute_run, 0, [&]() -> RuntimeDispatch {
    if (callbacks.user_engine_execute_run) {
      return callbacks.user_engine_execute_run();
    }
    return {};
  });

  // 阶段 4: 事件管理器 — 定时事件处理
  run_stage(LegacyFrameStage::event_manager_run, 0, [&]() -> RuntimeDispatch {
    if (callbacks.event_manager_run) {
      return callbacks.event_manager_run();
    }
    return {};
  });

  // 阶段 5: 服务端消息 — 发送出站消息
  run_stage(LegacyFrameStage::server_message_run, 0, [&]() -> RuntimeDispatch {
    if (callbacks.server_message_run) {
      return callbacks.server_message_run();
    }
    return {};
  });

  // 计算整帧总耗时和帧间隔
  trace.elapsed_us = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                            frame_start)
          .count());
  trace.last_frame_ms = trace.elapsed_us / 1000;
  last_trace_ = std::move(trace);
  return combined;
}

}  // namespace mir2
