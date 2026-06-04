/**
 * @file audio_backend.hpp
 * @brief 音频后端抽象接口 —— 定义平台无关的音频播放 API
 *
 * @details 通过 IAudioBackend 抽象接口将音频播放与具体平台实现解耦。
 *          支持多种后端实现：
 *          - DirectSoundAudioBackend：Windows DirectSound 实现（生产环境使用）
 *          - NullAudioBackend：空实现（测试环境使用，不产生实际声音）
 *
 * 音频分类：
 * - 音效（Sound）：短促的 UI 音效和游戏音效（如攻击声、按钮点击声）
 * - 背景音乐（BGM）：循环播放的游戏背景音乐
 *
 * 音量控制：
 * - 音效和 BGM 的音量独立控制（0-100%）
 * - silence() 可以立即停止所有正在播放的音频
 *
 * @note 此接口设计参考了经典传奇客户端中的音频子系统，但使用现代 C++ 接口
 */

#pragma once

#include <filesystem>
#include <string>

namespace mir2::client {

/**
 * @class IAudioBackend
 * @brief 音频后端抽象接口 —— 所有音频后端实现的基类
 *
 * @details 定义了音频播放、音量控制和错误报告的标准接口。
 *          实现类需要处理平台相关的音频 API（如 DirectSound）。
 */
class IAudioBackend {
 public:
  virtual ~IAudioBackend() = default;

  /// 初始化音频后端（分配资源、创建播放设备）
  virtual bool initialize() = 0;
  /// 关闭音频后端（释放资源、停止所有播放）
  virtual void shutdown() = 0;
  /// 播放音效文件
  /// @param path WAV 文件的路径
  /// @return 播放成功返回 true
  virtual bool play_sound(const std::filesystem::path& path) = 0;
  /// 播放背景音乐文件（循环播放）
  /// @param path 音乐文件的路径
  /// @return 播放成功返回 true
  virtual bool play_bgm(const std::filesystem::path& path) = 0;
  /// 立即停止所有正在播放的音频
  virtual void silence() = 0;
  /// 设置音效音量
  /// @param volume_percent 音量百分比（0-100）
  virtual void set_sound_volume(int volume_percent) = 0;
  /// 设置背景音乐音量
  /// @param volume_percent 音量百分比（0-100）
  virtual void set_bgm_volume(int volume_percent) = 0;
  /// 获取最近一次错误的描述信息
  virtual std::string last_error() const = 0;
};

/**
 * @class NullAudioBackend
 * @brief 空音频后端 —— 所有操作均为空操作，用于测试环境
 *
 * @details 实现了 IAudioBackend 的所有接口，但不产生任何实际音频输出。
 *          适用于单元测试和无音频设备的环境。
 */
class NullAudioBackend final : public IAudioBackend {
 public:
  bool initialize() override;
  void shutdown() override;
  bool play_sound(const std::filesystem::path& path) override;
  bool play_bgm(const std::filesystem::path& path) override;
  void silence() override;
  void set_sound_volume(int volume_percent) override;
  void set_bgm_volume(int volume_percent) override;
  std::string last_error() const override;
};

}  // namespace mir2::client
