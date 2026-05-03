#include <cassert>
#include <filesystem>
#include <iostream>

#include "audio/wav_reader.hpp"

namespace {

std::filesystem::path asset_root() {
  const std::filesystem::path root = LR"(F:\mir2\Legend of Mir)";
  assert(std::filesystem::exists(root / L"Wav" / L"sound.lst"));
  return root;
}

void assert_valid_pcm(const mir2::client::WavReadResult& result) {
  assert(result.ok);
  assert(result.error.empty());
  assert(result.data.channels == 1 || result.data.channels == 2);
  assert(result.data.sample_rate == 22050 ||
         result.data.sample_rate == 44100);
  assert(result.data.bits_per_sample == 16);
  assert(result.data.block_align > 0);
  assert(!result.data.samples.empty());
}

}  // namespace

int main() {
  using mir2::client::read_pcm_wav_file;

  const auto root = asset_root();

  const auto step = read_pcm_wav_file(root / L"Wav" / L"1.wav");
  assert_valid_pcm(step);

  const auto intro = read_pcm_wav_file(root / L"Wav" / L"102.wav");
  assert_valid_pcm(intro);
  assert(intro.data.samples.size() > step.data.samples.size());

  const auto missing = read_pcm_wav_file(root / L"Wav" / L"missing.wav");
  assert(!missing.ok);
  assert(missing.error == "missing_or_unreadable");

  const auto not_wav = read_pcm_wav_file(root / L"Wav" / L"sound.lst");
  assert(!not_wav.ok);

  std::cout << "wav_reader_smoke ok\n";
  return 0;
}
