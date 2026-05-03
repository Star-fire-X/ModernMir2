#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mir2::client {

struct PcmWavData {
  std::uint16_t channels = 0;
  std::uint32_t sample_rate = 0;
  std::uint32_t byte_rate = 0;
  std::uint16_t block_align = 0;
  std::uint16_t bits_per_sample = 0;
  std::vector<std::uint8_t> samples;
};

struct WavReadResult {
  bool ok = false;
  PcmWavData data;
  std::string error;
};

WavReadResult read_pcm_wav_file(const std::filesystem::path& path);

}  // namespace mir2::client
