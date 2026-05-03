#include <cassert>
#include <iostream>

#include "audio/audio_backend.hpp"
#include "audio/direct_sound_audio_backend.hpp"

int main() {
  mir2::client::NullAudioBackend null_backend;
  assert(null_backend.initialize());
  null_backend.set_sound_volume(-100);
  null_backend.set_bgm_volume(1000);
  assert(null_backend.play_sound({}));
  assert(null_backend.play_bgm({}));
  null_backend.silence();
  null_backend.shutdown();

  mir2::client::DirectSoundAudioBackend backend(nullptr);
  backend.set_sound_volume(25);
  backend.set_bgm_volume(75);

  assert(!backend.initialize());
  assert(backend.last_error() == "missing_window_handle");

  backend.shutdown();

  std::cout << "direct_sound_backend_smoke ok\n";
  return 0;
}
