#include "audio/wav_reader.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <utility>

namespace mir2::client {
namespace {

constexpr std::uint16_t kWaveFormatPcm = 1;
constexpr std::uint32_t kMaxWavDataBytes = 64U * 1024U * 1024U;

bool read_exact(std::istream& input, void* data, std::size_t size) {
  input.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
  return input.good() || input.gcount() == static_cast<std::streamsize>(size);
}

std::uint16_t read_le16(const std::array<std::uint8_t, 16>& data,
                        std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(data[offset]) |
      (static_cast<std::uint16_t>(data[offset + 1]) << 8));
}

std::uint32_t read_le32(const std::array<std::uint8_t, 16>& data,
                        std::size_t offset) {
  return static_cast<std::uint32_t>(
      static_cast<std::uint32_t>(data[offset]) |
      (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
      (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
      (static_cast<std::uint32_t>(data[offset + 3]) << 24));
}

std::uint32_t read_le32_from_header(const std::array<char, 8>& data,
                                    std::size_t offset) {
  return static_cast<std::uint32_t>(
      static_cast<std::uint32_t>(
          static_cast<unsigned char>(data[offset])) |
      (static_cast<std::uint32_t>(
           static_cast<unsigned char>(data[offset + 1]))
       << 8) |
      (static_cast<std::uint32_t>(
           static_cast<unsigned char>(data[offset + 2]))
       << 16) |
      (static_cast<std::uint32_t>(
           static_cast<unsigned char>(data[offset + 3]))
       << 24));
}

bool matches_fourcc(const char* value, const char* expected) {
  return value[0] == expected[0] && value[1] == expected[1] &&
         value[2] == expected[2] && value[3] == expected[3];
}

WavReadResult fail(std::string error) {
  WavReadResult result;
  result.error = std::move(error);
  return result;
}

void skip_bytes(std::istream& input, std::uint32_t count) {
  if (count == 0) {
    return;
  }
  input.seekg(static_cast<std::streamoff>(count), std::ios::cur);
}

}  // namespace

WavReadResult read_pcm_wav_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return fail("missing_or_unreadable");
  }

  std::array<char, 12> riff_header{};
  if (!read_exact(input, riff_header.data(), riff_header.size())) {
    return fail("truncated_riff_header");
  }
  if (!matches_fourcc(riff_header.data(), "RIFF") ||
      !matches_fourcc(riff_header.data() + 8, "WAVE")) {
    return fail("not_riff_wave");
  }

  bool saw_format = false;
  bool saw_data = false;
  PcmWavData data;

  while (input) {
    std::array<char, 8> chunk_header{};
    if (!read_exact(input, chunk_header.data(), chunk_header.size())) {
      break;
    }

    const std::uint32_t chunk_size = read_le32_from_header(chunk_header, 4);
    const bool has_padding_byte = (chunk_size % 2U) != 0U;

    if (matches_fourcc(chunk_header.data(), "fmt ")) {
      if (chunk_size < 16U) {
        return fail("truncated_fmt_chunk");
      }

      std::array<std::uint8_t, 16> format{};
      if (!read_exact(input, format.data(), format.size())) {
        return fail("truncated_fmt_chunk");
      }
      skip_bytes(input, chunk_size - 16U);
      if (has_padding_byte) {
        skip_bytes(input, 1);
      }

      const std::uint16_t format_tag = read_le16(format, 0);
      if (format_tag != kWaveFormatPcm) {
        return fail("unsupported_wav_format");
      }

      data.channels = read_le16(format, 2);
      data.sample_rate = read_le32(format, 4);
      data.byte_rate = read_le32(format, 8);
      data.block_align = read_le16(format, 12);
      data.bits_per_sample = read_le16(format, 14);
      saw_format = true;
      continue;
    }

    if (matches_fourcc(chunk_header.data(), "data")) {
      if (chunk_size == 0U) {
        return fail("empty_data_chunk");
      }
      if (chunk_size > kMaxWavDataBytes) {
        return fail("data_chunk_too_large");
      }

      data.samples.resize(chunk_size);
      if (!read_exact(input, data.samples.data(), data.samples.size())) {
        return fail("truncated_data_chunk");
      }
      if (has_padding_byte) {
        skip_bytes(input, 1);
      }
      saw_data = true;
      continue;
    }

    skip_bytes(input, chunk_size);
    if (has_padding_byte) {
      skip_bytes(input, 1);
    }
  }

  if (!saw_format) {
    return fail("missing_fmt_chunk");
  }
  if (!saw_data) {
    return fail("missing_data_chunk");
  }
  if (data.channels == 0 || data.sample_rate == 0 ||
      data.block_align == 0 || data.byte_rate == 0) {
    return fail("invalid_wave_format");
  }
  if (data.bits_per_sample != 8 && data.bits_per_sample != 16) {
    return fail("unsupported_bits_per_sample");
  }
  if (data.samples.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    return fail("data_chunk_too_large");
  }

  WavReadResult result;
  result.ok = true;
  result.data = std::move(data);
  return result;
}

}  // namespace mir2::client
