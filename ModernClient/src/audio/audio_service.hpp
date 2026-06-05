/**
 * @file audio_service.hpp
 * @brief 音频服务 —— 游戏音频系统的总入口，管理音效和背景音乐的播放
 *
 * @details AudioService 是客户端音频系统的核心组件，负责：
 *          - 管理音频后端（IAudioBackend）的生命周期
 *          - 加载音效列表文件（sound_list.txt），建立音效 ID 到文件路径的映射
 *          - 提供音效和背景音乐的播放接口
 *          - 支持音效排队（queue_sound + flush_queued_sounds）以避免同帧重复播放
 *          - 独立控制音效和 BGM 的启用/禁用状态和音量
 *          - 追踪音频事件用于调试和测试（trace_events）
 *
 * 音频工作流程：
 * 1. initialize(asset_root, native_window_handle) — 初始化后端和资源路径
 * 2. load_sound_list(sound_list_path) — 加载音效映射表
 * 3. play_sound(id) / play_bgm(path) — 播放音频
 * 4. apply_settings(settings) — 应用用户音量设置
 * 5. shutdown() — 释放音频后端资源
 *
 * 与经典传奇客户端的对应：
 * 传奇客户端使用 DirectSound 播放 WAV 音效和背景音乐。
 * 本实现通过 AudioIdMapping 将 Delphi 音效索引映射到 WAV 文件路径，
 * 通过 IAudioBackend 抽象支持 DirectSound 和空后端。
 *
 * @note 音效排队机制：同一声效在同一帧内多次触发时只播放一次，
 *       排队音效在 flush_queued_sounds() 中播放第一个请求。
 */

#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audio/audio_backend.hpp"
#include "audio/audio_id_mapping.hpp"
#include "audio/audio_settings.hpp"

namespace mir2::client {

/**
 * @enum AudioTraceKind
 * @brief 音频追踪事件类型
 */
enum class AudioTraceKind {
  load_sound_list,   ///< 加载音效列表文件
  sound_request,     ///< 请求播放音效
  bgm_request,       ///< 请求播放背景音乐
  silence,           ///< 静音请求
  ignored,           ///< 音效被忽略（如音效功能已禁用）
  missing_resource,  ///< 音效资源文件缺失
  backend_error,     ///< 音频后端错误
};

/**
 * @struct AudioTraceEvent
 * @brief 单个音频追踪事件
 */
struct AudioTraceEvent {
  AudioTraceKind kind = AudioTraceKind::ignored;  ///< 事件类型
  std::optional<int> sound_id;                     ///< 音效 ID（如果适用）
  std::wstring path;                               ///< 请求的路径
  std::filesystem::path resolved_path;             ///< 解析后的实际文件路径
  std::string reason;                              ///< 事件原因/详细描述
};

/**
 * @class AudioService
 * @brief 音频服务 —— 游戏音频系统的总入口
 *
 * @details 封装了音频后端的创建和管理。如果未指定后端，
 *          默认创建 DirectSound 后端（生产环境）。支持：
 *          - 播放音效（通过 ID 或文件路径）
 *          - 播放背景音乐（通过文件路径）
 *          - 音效排队（同帧去重）
 *          - 独立音量控制
 */
class AudioService {
 public:
  AudioService();
  /// 使用自定义后端构造（如测试时注入 NullAudioBackend）
  explicit AudioService(std::unique_ptr<IAudioBackend> backend);

  bool initialize();
  /// 初始化音频服务（指定资源根目录）
  bool initialize(const std::filesystem::path& asset_root);
  /// 初始化音频服务（指定资源根目录和原生窗口句柄，DirectSound 需要）
  bool initialize(const std::filesystem::path& asset_root,
                  void* native_window_handle);
  /// 关闭音频服务并释放后端资源
  void shutdown();

  /// 加载音效列表文件（建立 sound_id → WAV 文件路径的映射）
  bool load_sound_list(const std::filesystem::path& sound_list_path);

  /// 通过音效 ID 播放音效
  void play_sound(int sound_id);
  /// 通过相对路径播放音效
  void play_sound(std::wstring_view relative_path);
  void play_sound(const wchar_t* relative_path);

  /// 将音效加入队列（同帧去重，在 flush_queued_sounds 时播放）
  void queue_sound(int sound_id, std::uint64_t now_ms);
  /// 刷新音效队列（播放队首音效，清空队列）
  void flush_queued_sounds(std::uint64_t now_ms);

  /// 播放背景音乐
  void play_bgm(std::wstring_view relative_path);
  void play_bgm(const wchar_t* relative_path);

  /// 立即停止所有音频
  void silence();

  /// 应用音频设置（同时更新音效和 BGM 的启用/禁用和音量）
  void apply_settings(AudioSettings settings);
  void set_sound_enabled(bool enabled) { sound_enabled_ = enabled; }
  void set_bgm_enabled(bool enabled);
  void set_sound_volume(int volume_percent);
  void set_bgm_volume(int volume_percent);
  [[nodiscard]] bool sound_enabled() const { return sound_enabled_; }
  [[nodiscard]] bool bgm_enabled() const { return bgm_enabled_; }
  [[nodiscard]] int sound_volume() const { return sound_volume_; }
  [[nodiscard]] int bgm_volume() const { return bgm_volume_; }
  [[nodiscard]] AudioSettings settings() const;

  [[nodiscard]] const AudioIdMapping& mapping() const { return mapping_; }
  [[nodiscard]] const std::vector<AudioTraceEvent>& trace_events() const {
    return trace_events_;
  }
  void clear_trace_events() { trace_events_.clear(); }

 private:
  /// 排队音效请求结构
  struct QueuedSoundRequest {
    int sound_id{0};               ///< 音效 ID
    std::uint64_t queued_ms{0};    ///< 排队时间戳
  };

  void ensure_backend();           ///< 确保后端已创建（延迟创建）
  void apply_backend_volume_settings();  ///< 将当前音量设置应用到后端
  void emit_backend_error(std::wstring_view path = {},
                          const std::filesystem::path& resolved_path = {});
  void play_sound_path(std::wstring_view relative_path,
                       std::optional<int> sound_id);
  void emit_trace(AudioTraceEvent event);  ///< 记录追踪事件
  void emit_ignored(std::string reason,
                    std::optional<int> sound_id = std::nullopt,
                    std::wstring_view path = {});
  [[nodiscard]] bool resource_exists(const std::filesystem::path& path) const;

  std::filesystem::path asset_root_;       ///< 资源根目录（音效文件所在路径）
  void* native_window_handle_ = nullptr;   ///< 原生窗口句柄（DirectSound 需要）
  AudioIdMapping mapping_;                 ///< 音效 ID → 文件路径映射表
  std::unique_ptr<IAudioBackend> backend_; ///< 音频后端实现
  bool initialized_ = false;               ///< 是否已初始化
  bool sound_enabled_ = true;              ///< 音效是否启用
  bool bgm_enabled_ = true;                ///< BGM 是否启用
  int sound_volume_ = 100;                 ///< 音效音量（0-100）
  int bgm_volume_ = 100;                   ///< BGM 音量（0-100）
  std::vector<AudioTraceEvent> trace_events_;           ///< 追踪事件日志
  std::deque<QueuedSoundRequest> queued_sounds_;        ///< 音效排队队列
  std::vector<std::filesystem::path> missing_resources_reported_;  ///< 已报告的缺失资源（防止重复报告）
};

}  // namespace mir2::client
