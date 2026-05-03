#pragma once

#include <memory>
#include <string>

#include "audio/audio_backend.hpp"

namespace mir2::client {

class DirectSoundAudioBackend final : public IAudioBackend {
 public:
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
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace mir2::client
