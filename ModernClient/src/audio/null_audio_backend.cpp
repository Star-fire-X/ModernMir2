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
