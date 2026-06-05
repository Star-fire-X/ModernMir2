/**
 * @file audio_service.cpp
 * @brief 音频服务实现 —— 音效/BGM 播放、设置管理和事件追踪
 * @details 实现 AudioService 的核心逻辑：后端管理、音效 ID 映射、
 *          音效排队去重、音量控制和追踪事件记录。
 */

#include "audio/audio_service.hpp"

#include <algorithm>
#include <utility>

#include "audio/direct_sound_audio_backend.hpp"

namespace mir2::client {

namespace {

constexpr std::size_t kMaxTraceEvents = 256;
constexpr std::uint64_t kQueuedSoundTtlMs = 100;

int clamp_volume(const int volume_percent) {
  return std::clamp(volume_percent, 0, 100);
}

std::wstring_view safe_view(const wchar_t* text) {
  return text == nullptr ? std::wstring_view{} : std::wstring_view{text};
}

}  // namespace

AudioService::AudioService() = default;

AudioService::AudioService(std::unique_ptr<IAudioBackend> backend)
    : backend_(std::move(backend)) {}

bool AudioService::initialize() {
  return initialize({}, nullptr);
}

bool AudioService::initialize(const std::filesystem::path& asset_root) {
  return initialize(asset_root, nullptr);
}

bool AudioService::initialize(const std::filesystem::path& asset_root,
                              void* native_window_handle) {
  asset_root_ = asset_root;
  native_window_handle_ = native_window_handle;
  queued_sounds_.clear();

  bool created_platform_backend = false;
  if (!backend_) {
    if (native_window_handle_ != nullptr) {
      backend_ = std::make_unique<DirectSoundAudioBackend>(native_window_handle_);
      created_platform_backend = true;
    } else {
      backend_ = std::make_unique<NullAudioBackend>();
    }
  }

  initialized_ = backend_->initialize();
  if (!initialized_ && created_platform_backend) {
    emit_backend_error();
    backend_ = std::make_unique<NullAudioBackend>();
    initialized_ = backend_->initialize();
  }
  apply_backend_volume_settings();

  if (!asset_root_.empty()) {
    load_sound_list(asset_root_ / L"Wav" / L"sound.lst");
  }

  return initialized_;
}

void AudioService::shutdown() {
  if (backend_) {
    backend_->shutdown();
  }
  queued_sounds_.clear();
  initialized_ = false;
}

bool AudioService::load_sound_list(
    const std::filesystem::path& sound_list_path) {
  missing_resources_reported_.clear();
  const bool loaded = mapping_.load_from_file(sound_list_path);
  emit_trace(AudioTraceEvent{
      AudioTraceKind::load_sound_list,
      std::nullopt,
      {},
      sound_list_path,
      loaded ? "loaded" : "missing_or_unreadable",
  });
  return loaded;
}

void AudioService::play_sound(int sound_id) {
  if (!sound_enabled_) {
    emit_ignored("sound_disabled", sound_id);
    return;
  }

  const std::wstring* relative_path = mapping_.path_for(sound_id);
  if (relative_path == nullptr) {
    emit_ignored("sound_id_out_of_range", sound_id);
    return;
  }
  if (relative_path->empty()) {
    emit_ignored("empty_sound_path", sound_id);
    return;
  }

  play_sound_path(*relative_path, sound_id);
}

void AudioService::play_sound(std::wstring_view relative_path) {
  if (!sound_enabled_) {
    emit_ignored("sound_disabled", std::nullopt, relative_path);
    return;
  }
  play_sound_path(relative_path, std::nullopt);
}

void AudioService::play_sound(const wchar_t* relative_path) {
  play_sound(safe_view(relative_path));
}

void AudioService::queue_sound(const int sound_id, const std::uint64_t now_ms) {
  queued_sounds_.push_front(QueuedSoundRequest{sound_id, now_ms});
}

void AudioService::flush_queued_sounds(const std::uint64_t now_ms) {
  auto queued = std::move(queued_sounds_);
  queued_sounds_.clear();

  for (const auto& request : queued) {
    const auto age_ms =
        now_ms >= request.queued_ms ? now_ms - request.queued_ms : 0;
    if (age_ms >= kQueuedSoundTtlMs) {
      emit_ignored("sound_queue_expired", request.sound_id);
      continue;
    }
    play_sound(request.sound_id);
  }
}

void AudioService::play_bgm(std::wstring_view relative_path) {
  if (!bgm_enabled_) {
    silence();
    emit_ignored("bgm_disabled", std::nullopt, relative_path);
    return;
  }

  if (relative_path.empty()) {
    emit_ignored("empty_bgm_path");
    return;
  }

  const auto resolved = mapping_.resolve_path(asset_root_, relative_path);
  if (!resource_exists(resolved)) {
    if (std::find(missing_resources_reported_.begin(),
                  missing_resources_reported_.end(),
                  resolved) == missing_resources_reported_.end()) {
      missing_resources_reported_.push_back(resolved);
      emit_trace(AudioTraceEvent{
          AudioTraceKind::missing_resource,
          std::nullopt,
          std::wstring{relative_path},
          resolved,
          "bgm_file_missing",
      });
    }
    return;
  }

  emit_trace(AudioTraceEvent{
      AudioTraceKind::bgm_request,
      std::nullopt,
      std::wstring{relative_path},
      resolved,
      "queued",
  });
  ensure_backend();
  if (!backend_->play_bgm(resolved)) {
    emit_backend_error(relative_path, resolved);
  }
}

void AudioService::play_bgm(const wchar_t* relative_path) {
  play_bgm(safe_view(relative_path));
}

void AudioService::silence() {
  emit_trace(AudioTraceEvent{
      AudioTraceKind::silence,
      std::nullopt,
      {},
      {},
      "silence",
  });
  ensure_backend();
  backend_->silence();
}

void AudioService::apply_settings(AudioSettings settings) {
  settings.sound_volume = clamp_volume(settings.sound_volume);
  settings.bgm_volume = clamp_volume(settings.bgm_volume);
  sound_enabled_ = settings.sound_enabled;
  sound_volume_ = settings.sound_volume;
  bgm_volume_ = settings.bgm_volume;
  apply_backend_volume_settings();
  set_bgm_enabled(settings.bgm_enabled);
}

void AudioService::set_bgm_enabled(bool enabled) {
  bgm_enabled_ = enabled;
  if (!enabled) {
    silence();
  }
}

void AudioService::set_sound_volume(int volume_percent) {
  sound_volume_ = clamp_volume(volume_percent);
  if (backend_) {
    backend_->set_sound_volume(sound_volume_);
  }
}

void AudioService::set_bgm_volume(int volume_percent) {
  bgm_volume_ = clamp_volume(volume_percent);
  if (backend_) {
    backend_->set_bgm_volume(bgm_volume_);
  }
}

AudioSettings AudioService::settings() const {
  return AudioSettings{
      sound_enabled_,
      bgm_enabled_,
      sound_volume_,
      bgm_volume_,
  };
}

void AudioService::ensure_backend() {
  if (!backend_) {
    backend_ = std::make_unique<NullAudioBackend>();
  }
}

void AudioService::apply_backend_volume_settings() {
  if (!backend_) {
    return;
  }
  backend_->set_sound_volume(sound_volume_);
  backend_->set_bgm_volume(bgm_volume_);
}

void AudioService::emit_backend_error(
    std::wstring_view path,
    const std::filesystem::path& resolved_path) {
  std::string reason = "backend_error";
  if (backend_) {
    const std::string backend_error = backend_->last_error();
    if (!backend_error.empty()) {
      reason = backend_error;
    }
  }

  emit_trace(AudioTraceEvent{
      AudioTraceKind::backend_error,
      std::nullopt,
      std::wstring{path},
      resolved_path,
      std::move(reason),
  });
}

void AudioService::play_sound_path(std::wstring_view relative_path,
                                   std::optional<int> sound_id) {
  if (relative_path.empty()) {
    emit_ignored("empty_sound_path", sound_id);
    return;
  }

  const auto resolved = mapping_.resolve_path(asset_root_, relative_path);
  if (!resource_exists(resolved)) {
    if (std::find(missing_resources_reported_.begin(),
                  missing_resources_reported_.end(),
                  resolved) == missing_resources_reported_.end()) {
      missing_resources_reported_.push_back(resolved);
      emit_trace(AudioTraceEvent{
          AudioTraceKind::missing_resource,
          sound_id,
          std::wstring{relative_path},
          resolved,
          "sound_file_missing",
      });
    }
    return;
  }

  emit_trace(AudioTraceEvent{
      AudioTraceKind::sound_request,
      sound_id,
      std::wstring{relative_path},
      resolved,
      "queued",
  });
  ensure_backend();
  if (!backend_->play_sound(resolved)) {
    emit_backend_error(relative_path, resolved);
  }
}

void AudioService::emit_trace(AudioTraceEvent event) {
  if (trace_events_.size() >= kMaxTraceEvents) {
    trace_events_.erase(trace_events_.begin());
  }
  trace_events_.push_back(std::move(event));
}

void AudioService::emit_ignored(std::string reason,
                                std::optional<int> sound_id,
                                std::wstring_view path) {
  emit_trace(AudioTraceEvent{
      AudioTraceKind::ignored,
      sound_id,
      std::wstring{path},
      {},
      std::move(reason),
  });
}

bool AudioService::resource_exists(const std::filesystem::path& path) const {
  if (path.empty()) {
    return false;
  }

  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec);
}

}  // namespace mir2::client
