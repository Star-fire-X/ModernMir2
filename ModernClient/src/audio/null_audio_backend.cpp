/**
 * @file null_audio_backend.cpp
 * @brief 空音频后端实现 —— 所有操作为空操作，用于测试和无音频环境
 * @details 实现 IAudioBackend 接口的所有方法为空操作。
 *          适用于单元测试和 CI 环境（无音频设备）。
 */

#include "audio/audio_backend.hpp"

namespace mir2::client {

bool NullAudioBackend::initialize() {
  return true;
}

void NullAudioBackend::shutdown() {}

bool NullAudioBackend::play_sound(const std::filesystem::path&) {
  return true;
}

bool NullAudioBackend::play_bgm(const std::filesystem::path&) {
  return true;
}

void NullAudioBackend::silence() {}

void NullAudioBackend::set_sound_volume(int) {}

void NullAudioBackend::set_bgm_volume(int) {}

std::string NullAudioBackend::last_error() const {
  return {};
}

}  // namespace mir2::client
