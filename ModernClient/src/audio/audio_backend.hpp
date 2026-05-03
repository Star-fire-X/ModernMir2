#pragma once

#include <filesystem>
#include <string>

namespace mir2::client {

class IAudioBackend {
 public:
  virtual ~IAudioBackend() = default;

  virtual bool initialize() = 0;
  virtual void shutdown() = 0;
  virtual bool play_sound(const std::filesystem::path& path) = 0;
  virtual bool play_bgm(const std::filesystem::path& path) = 0;
  virtual void silence() = 0;
  virtual void set_sound_volume(int volume_percent) = 0;
  virtual void set_bgm_volume(int volume_percent) = 0;
  virtual std::string last_error() const = 0;
};

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
