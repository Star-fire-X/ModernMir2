/**
 * @file direct_sound_audio_backend.hpp
 * @brief DirectSound 音频后端 —— 基于 Windows DirectSound API 的音频播放实现
 *
 * @details 实现了 IAudioBackend 接口，使用 DirectSound 进行低延迟
 *          音频播放。DirectSound 是传奇客户端原版使用的音频 API，
 *          本实现保持与原版相同的音频行为。
 *
 * 特性：
 * - 支持同时播放多个音效（多缓冲区混合）
 * - 独立的音效和 BGM 音量控制
 * - 通过 PIMPL 模式隐藏 DirectSound 实现细节
 * - 需要原生窗口句柄（HWND）用于创建 DirectSound 设备
 *
 * @note 在无 DirectSound 设备或测试环境中，使用 NullAudioBackend 替代
 */

#pragma once

#include <memory>
#include <string>

#include "audio/audio_backend.hpp"

namespace mir2::client {

/**
 * @class DirectSoundAudioBackend
 * @brief DirectSound 音频后端实现
 *
 * @details 封装 DirectSound 的初始化和播放逻辑。
 *          使用 PIMPL（Pointer to Implementation）模式隐藏
 *          Windows 特有头文件和 DirectSound COM 接口。
 */
class DirectSoundAudioBackend final : public IAudioBackend {
 public:
  /// 构造函数
  /// @param native_window_handle 原生窗口句柄（HWND），用于创建 DirectSound 设备
  explicit DirectSoundAudioBackend(void* native_window_handle);
  ~DirectSoundAudioBackend() override;

  bool initialize() override;
  void shutdown() override;
  bool play_sound(const std::filesystem::path& path) override;
  bool play_bgm(const std::filesystem::path& path) override;
  void silence() override;
  void set_sound_volume(int volume_percent) override;
  void set_bgm_volume(int volume_percent) override;
  std::string last_error() const override;

 private:
  class Impl;                       ///< 前向声明的实现类（包含 DirectSound 具体逻辑）
  std::unique_ptr<Impl> impl_;      ///< PIMPL 指针
};

}  // namespace mir2::client
