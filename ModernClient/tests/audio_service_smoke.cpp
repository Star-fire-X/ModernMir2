#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <optional>

#include "audio/audio_service.hpp"
#include "audio/sound_constants.hpp"

namespace {

std::filesystem::path asset_root() {
  const std::filesystem::path root = LR"(F:\mir2\Legend of Mir)";
  assert(std::filesystem::exists(root / L"Wav" / L"sound.lst"));
  return root;
}

bool has_kind(const mir2::client::AudioService& audio,
              mir2::client::AudioTraceKind kind) {
  const auto& events = audio.trace_events();
  return std::any_of(events.begin(), events.end(), [kind](const auto& event) {
    return event.kind == kind;
  });
}

bool has_reason(const mir2::client::AudioService& audio,
                const std::string& reason) {
  const auto& events = audio.trace_events();
  return std::any_of(events.begin(), events.end(), [&reason](const auto& event) {
    return event.reason == reason;
  });
}

int count_kind(const mir2::client::AudioService& audio,
               mir2::client::AudioTraceKind kind) {
  const auto& events = audio.trace_events();
  return static_cast<int>(
      std::count_if(events.begin(), events.end(), [kind](const auto& event) {
        return event.kind == kind;
      }));
}

std::optional<int> first_sound_id(const mir2::client::AudioService& audio) {
  const auto& events = audio.trace_events();
  const auto it = std::find_if(events.begin(), events.end(), [](const auto& event) {
    return event.sound_id.has_value();
  });
  return it == events.end() ? std::optional<int>{} : it->sound_id;
}

}  // namespace

int main() {
  using namespace mir2::client;

  AudioService audio;
  const auto root = asset_root();
  assert(audio.initialize(root));
  assert(audio.mapping().dynamic_base() > 0);
  assert(audio.sound_enabled());
  assert(audio.bgm_enabled());
  assert(audio.sound_volume() == 100);
  assert(audio.bgm_volume() == 100);

  audio.clear_trace_events();
  audio.apply_settings(AudioSettings{false, false, -10, 150});
  assert(!audio.sound_enabled());
  assert(!audio.bgm_enabled());
  assert(audio.sound_volume() == 0);
  assert(audio.bgm_volume() == 100);
  assert(has_kind(audio, AudioTraceKind::silence));

  audio.clear_trace_events();
  audio.apply_settings(AudioSettings{true, true, 65, 45});
  assert(audio.sound_enabled());
  assert(audio.bgm_enabled());
  assert(audio.sound_volume() == 65);
  assert(audio.bgm_volume() == 45);

  audio.clear_trace_events();
  audio.play_sound(1);
  assert(has_kind(audio, AudioTraceKind::sound_request));

  audio.clear_trace_events();
  audio.queue_sound(1, 1000);
  assert(!has_kind(audio, AudioTraceKind::sound_request));
  audio.flush_queued_sounds(1099);
  assert(has_kind(audio, AudioTraceKind::sound_request));

  audio.clear_trace_events();
  audio.queue_sound(1, 2000);
  audio.flush_queued_sounds(2100);
  assert(has_reason(audio, "sound_queue_expired"));
  assert(count_kind(audio, AudioTraceKind::sound_request) == 0);

  audio.clear_trace_events();
  audio.queue_sound(1, 3000);
  audio.queue_sound(2, 3000);
  audio.flush_queued_sounds(3000);
  assert(first_sound_id(audio).has_value());
  assert(*first_sound_id(audio) == 2);

  audio.clear_trace_events();
  audio.play_sound(-1);
  assert(has_reason(audio, "sound_id_out_of_range"));

  audio.clear_trace_events();
  audio.play_sound(140);
  assert(has_reason(audio, "empty_sound_path"));

  audio.clear_trace_events();
  audio.set_sound_enabled(false);
  audio.play_sound(1);
  assert(has_reason(audio, "sound_disabled"));
  assert(!has_kind(audio, AudioTraceKind::silence));

  audio.clear_trace_events();
  audio.queue_sound(1, 4000);
  audio.flush_queued_sounds(4000);
  assert(has_reason(audio, "sound_disabled"));
  audio.set_sound_enabled(true);

  audio.clear_trace_events();
  audio.play_sound(10110);
  assert(has_kind(audio, AudioTraceKind::missing_resource));
  const auto first_missing_count = audio.trace_events().size();
  audio.play_sound(10110);
  assert(audio.trace_events().size() == first_missing_count);

  audio.clear_trace_events();
  audio.set_bgm_enabled(false);
  audio.play_bgm(bmg_intro);
  assert(has_kind(audio, AudioTraceKind::silence));
  assert(has_reason(audio, "bgm_disabled"));
  audio.set_bgm_enabled(true);

  audio.set_sound_volume(1000);
  audio.set_bgm_volume(-1000);
  assert(audio.sound_volume() == 100);
  assert(audio.bgm_volume() == 0);
  audio.set_bgm_volume(100);

  audio.clear_trace_events();
  audio.play_bgm(bmg_intro);
  assert(has_kind(audio, AudioTraceKind::bgm_request));

  audio.shutdown();
  std::cout << "audio_service_smoke ok\n";
  return 0;
}
