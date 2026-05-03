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

enum class AudioTraceKind {
  load_sound_list,
  sound_request,
  bgm_request,
  silence,
  ignored,
  missing_resource,
  backend_error,
};

struct AudioTraceEvent {
  AudioTraceKind kind = AudioTraceKind::ignored;
  std::optional<int> sound_id;
  std::wstring path;
  std::filesystem::path resolved_path;
  std::string reason;
};

class AudioService {
 public:
  AudioService();
  explicit AudioService(std::unique_ptr<IAudioBackend> backend);

  bool initialize();
  bool initialize(const std::filesystem::path& asset_root);
  bool initialize(const std::filesystem::path& asset_root,
                  void* native_window_handle);
  void shutdown();

  bool load_sound_list(const std::filesystem::path& sound_list_path);

  void play_sound(int sound_id);
  void play_sound(std::wstring_view relative_path);
  void play_sound(const wchar_t* relative_path);
  void queue_sound(int sound_id, std::uint64_t now_ms);
  void flush_queued_sounds(std::uint64_t now_ms);

  void play_bgm(std::wstring_view relative_path);
  void play_bgm(const wchar_t* relative_path);

  void silence();

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
  struct QueuedSoundRequest {
    int sound_id{0};
    std::uint64_t queued_ms{0};
  };

  void ensure_backend();
  void apply_backend_volume_settings();
  void emit_backend_error(std::wstring_view path = {},
                          const std::filesystem::path& resolved_path = {});
  void play_sound_path(std::wstring_view relative_path,
                       std::optional<int> sound_id);
  void emit_trace(AudioTraceEvent event);
  void emit_ignored(std::string reason,
                    std::optional<int> sound_id = std::nullopt,
                    std::wstring_view path = {});
  [[nodiscard]] bool resource_exists(const std::filesystem::path& path) const;

  std::filesystem::path asset_root_;
  void* native_window_handle_ = nullptr;
  AudioIdMapping mapping_;
  std::unique_ptr<IAudioBackend> backend_;
  bool initialized_ = false;
  bool sound_enabled_ = true;
  bool bgm_enabled_ = true;
  int sound_volume_ = 100;
  int bgm_volume_ = 100;
  std::vector<AudioTraceEvent> trace_events_;
  std::deque<QueuedSoundRequest> queued_sounds_;
  std::vector<std::filesystem::path> missing_resources_reported_;
};

}  // namespace mir2::client
