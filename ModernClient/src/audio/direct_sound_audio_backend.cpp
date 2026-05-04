#include "audio/direct_sound_audio_backend.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include "audio/wav_reader.hpp"

namespace mir2::client {
namespace {

template <typename T>
class ComPtr {
 public:
  ComPtr() = default;
  ~ComPtr() { reset(); }

  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;

  ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}

  ComPtr& operator=(ComPtr&& other) noexcept {
    if (this != &other) {
      reset(std::exchange(other.ptr_, nullptr));
    }
    return *this;
  }

  [[nodiscard]] T* get() const { return ptr_; }
  [[nodiscard]] T** put() {
    reset();
    return &ptr_;
  }
  [[nodiscard]] T* operator->() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }

  void reset(T* ptr = nullptr) {
    if (ptr_ != nullptr) {
      ptr_->Release();
    }
    ptr_ = ptr;
  }

 private:
  T* ptr_ = nullptr;
};

std::string hresult_error(const char* operation, HRESULT hr) {
  std::ostringstream output;
  output << operation << "_failed_0x" << std::hex << std::uppercase
         << static_cast<unsigned long>(hr);
  return output.str();
}

int clamp_volume_percent(const int volume_percent) {
  return std::clamp(volume_percent, 0, 100);
}

LONG direct_sound_volume_from_percent(const int volume_percent) {
  const int clamped = clamp_volume_percent(volume_percent);
  if (clamped <= 0) {
    return DSBVOLUME_MIN;
  }
  if (clamped >= 100) {
    return DSBVOLUME_MAX;
  }
  const auto scalar = static_cast<double>(clamped) / 100.0;
  const auto value = static_cast<LONG>(std::lround(2000.0 * std::log10(scalar)));
  return std::clamp(value, static_cast<LONG>(DSBVOLUME_MIN),
                    static_cast<LONG>(DSBVOLUME_MAX));
}

}  // namespace

class DirectSoundAudioBackend::Impl {
 public:
  explicit Impl(void* native_window_handle)
      : native_window_handle_(native_window_handle) {}

  bool initialize() {
    clear_error();
    if (native_window_handle_ == nullptr) {
      return set_error("missing_window_handle");
    }

    HRESULT hr = DirectSoundCreate8(nullptr, direct_sound_.put(), nullptr);
    if (FAILED(hr)) {
      return set_error(hresult_error("DirectSoundCreate8", hr));
    }

    hr = direct_sound_->SetCooperativeLevel(
        static_cast<HWND>(native_window_handle_), DSSCL_NORMAL);
    if (FAILED(hr)) {
      direct_sound_.reset();
      return set_error(hresult_error("SetCooperativeLevel", hr));
    }

    return true;
  }

  void shutdown() {
    silence();
    direct_sound_.reset();
  }

  bool play_sound(const std::filesystem::path& path) {
    clear_error();
    cleanup_finished_sounds();

    ComPtr<IDirectSoundBuffer> buffer;
    if (!load_buffer(path, buffer)) {
      return false;
    }

    HRESULT hr = buffer->SetCurrentPosition(0);
    if (FAILED(hr)) {
      return set_error(hresult_error("SetCurrentPosition", hr));
    }
    buffer->SetVolume(direct_sound_volume_from_percent(sound_volume_percent_));

    hr = buffer->Play(0, 0, 0);
    if (hr == DSERR_BUFFERLOST) {
      buffer->Restore();
      hr = buffer->Play(0, 0, 0);
    }
    if (FAILED(hr)) {
      return set_error(hresult_error("PlaySound", hr));
    }

    active_sounds_.push_back(std::move(buffer));
    return true;
  }

  bool play_bgm(const std::filesystem::path& path) {
    clear_error();

    ComPtr<IDirectSoundBuffer> buffer;
    if (!load_buffer(path, buffer)) {
      return false;
    }

    if (bgm_buffer_) {
      bgm_buffer_->Stop();
      bgm_buffer_.reset();
    }

    HRESULT hr = buffer->SetCurrentPosition(0);
    if (FAILED(hr)) {
      return set_error(hresult_error("SetBgmPosition", hr));
    }
    buffer->SetVolume(direct_sound_volume_from_percent(bgm_volume_percent_));

    hr = buffer->Play(0, 0, DSBPLAY_LOOPING);
    if (hr == DSERR_BUFFERLOST) {
      buffer->Restore();
      hr = buffer->Play(0, 0, DSBPLAY_LOOPING);
    }
    if (FAILED(hr)) {
      return set_error(hresult_error("PlayBgm", hr));
    }

    bgm_buffer_ = std::move(buffer);
    return true;
  }

  void silence() {
    if (bgm_buffer_) {
      bgm_buffer_->Stop();
      bgm_buffer_.reset();
    }

    for (auto& buffer : active_sounds_) {
      if (buffer) {
        buffer->Stop();
      }
    }
    active_sounds_.clear();
  }

  void set_sound_volume(const int volume_percent) {
    sound_volume_percent_ = clamp_volume_percent(volume_percent);
    cleanup_finished_sounds();
    const auto volume = direct_sound_volume_from_percent(sound_volume_percent_);
    for (auto& buffer : active_sounds_) {
      if (buffer) {
        buffer->SetVolume(volume);
      }
    }
  }

  void set_bgm_volume(const int volume_percent) {
    bgm_volume_percent_ = clamp_volume_percent(volume_percent);
    if (bgm_buffer_) {
      bgm_buffer_->SetVolume(direct_sound_volume_from_percent(bgm_volume_percent_));
    }
  }

  [[nodiscard]] std::string last_error() const { return last_error_; }

 private:
  bool load_buffer(const std::filesystem::path& path,
                   ComPtr<IDirectSoundBuffer>& out_buffer) {
    if (!direct_sound_) {
      return set_error("directsound_not_initialized");
    }

    const WavReadResult wave = read_pcm_wav_file(path);
    if (!wave.ok) {
      return set_error("wav_" + wave.error);
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = wave.data.channels;
    format.nSamplesPerSec = wave.data.sample_rate;
    format.nAvgBytesPerSec = wave.data.byte_rate;
    format.nBlockAlign = wave.data.block_align;
    format.wBitsPerSample = wave.data.bits_per_sample;
    format.cbSize = 0;

    DSBUFFERDESC desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_CTRLVOLUME;
    desc.dwBufferBytes = static_cast<DWORD>(wave.data.samples.size());
    desc.lpwfxFormat = &format;

    HRESULT hr = direct_sound_->CreateSoundBuffer(&desc, out_buffer.put(), nullptr);
    if (FAILED(hr)) {
      return set_error(hresult_error("CreateSoundBuffer", hr));
    }

    void* audio1 = nullptr;
    void* audio2 = nullptr;
    DWORD audio1_size = 0;
    DWORD audio2_size = 0;
    hr = out_buffer->Lock(0, desc.dwBufferBytes, &audio1, &audio1_size,
                          &audio2, &audio2_size, 0);
    if (hr == DSERR_BUFFERLOST) {
      out_buffer->Restore();
      hr = out_buffer->Lock(0, desc.dwBufferBytes, &audio1, &audio1_size,
                            &audio2, &audio2_size, 0);
    }
    if (FAILED(hr)) {
      out_buffer.reset();
      return set_error(hresult_error("LockSoundBuffer", hr));
    }

    const auto* source = wave.data.samples.data();
    if (audio1 != nullptr && audio1_size > 0) {
      std::memcpy(audio1, source, audio1_size);
    }
    if (audio2 != nullptr && audio2_size > 0) {
      std::memcpy(audio2, source + audio1_size, audio2_size);
    }

    hr = out_buffer->Unlock(audio1, audio1_size, audio2, audio2_size);
    if (FAILED(hr)) {
      out_buffer.reset();
      return set_error(hresult_error("UnlockSoundBuffer", hr));
    }

    return true;
  }

  void cleanup_finished_sounds() {
    active_sounds_.erase(
        std::remove_if(active_sounds_.begin(), active_sounds_.end(),
                       [](const ComPtr<IDirectSoundBuffer>& buffer) {
                         if (!buffer) {
                           return true;
                         }
                         DWORD status = 0;
                         if (FAILED(buffer->GetStatus(&status))) {
                           return true;
                         }
                         return (status & DSBSTATUS_PLAYING) == 0;
                       }),
        active_sounds_.end());
  }

  bool set_error(std::string error) {
    last_error_ = std::move(error);
    return false;
  }

  void clear_error() { last_error_.clear(); }

  void* native_window_handle_ = nullptr;
  ComPtr<IDirectSound8> direct_sound_;
  ComPtr<IDirectSoundBuffer> bgm_buffer_;
  std::vector<ComPtr<IDirectSoundBuffer>> active_sounds_;
  int sound_volume_percent_ = 100;
  int bgm_volume_percent_ = 100;
  std::string last_error_;
};

DirectSoundAudioBackend::DirectSoundAudioBackend(void* native_window_handle)
    : impl_(std::make_unique<Impl>(native_window_handle)) {}

DirectSoundAudioBackend::~DirectSoundAudioBackend() = default;

bool DirectSoundAudioBackend::initialize() {
  return impl_->initialize();
}

void DirectSoundAudioBackend::shutdown() {
  impl_->shutdown();
}

bool DirectSoundAudioBackend::play_sound(const std::filesystem::path& path) {
  return impl_->play_sound(path);
}

bool DirectSoundAudioBackend::play_bgm(const std::filesystem::path& path) {
  return impl_->play_bgm(path);
}

void DirectSoundAudioBackend::silence() {
  impl_->silence();
}

void DirectSoundAudioBackend::set_sound_volume(const int volume_percent) {
  impl_->set_sound_volume(volume_percent);
}

void DirectSoundAudioBackend::set_bgm_volume(const int volume_percent) {
  impl_->set_bgm_volume(volume_percent);
}

std::string DirectSoundAudioBackend::last_error() const {
  return impl_->last_error();
}

}  // namespace mir2::client
