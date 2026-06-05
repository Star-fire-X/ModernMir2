/**
 * @file wav_reader.hpp
 * @brief WAV 文件读取器 —— 解析 PCM 格式的 WAV 音频文件
 *
 * @details 读取并解析标准 RIFF WAV 音频文件，提取 PCM 音频数据
 *          和格式信息。支持常见的 PCM WAV 格式（8/16 位，单声道/立体声）。
 *
 * 解析流程：
 * 1. 验证 RIFF 头部和 WAVE 格式标识
 * 2. 查找 fmt 块，提取音频格式参数（声道数、采样率、位深度等）
 * 3. 查找 data 块，提取原始 PCM 采样数据
 *
 * @note 仅支持 PCM 格式（AudioFormat = 1）。不支持压缩格式（如 ADPCM、MP3）。
 *       传奇客户端的所有音效均为 PCM WAV 格式，无需支持其他编码。
 */

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace mir2::client {

/**
 * @struct PcmWavData
 * @brief PCM WAV 音频数据 —— 包含格式信息和原始采样数据
 */
struct PcmWavData {
  std::uint16_t channels = 0;       ///< 声道数（1=单声道, 2=立体声）
  std::uint32_t sample_rate = 0;    ///< 采样率（Hz，如 22050、44100）
  std::uint32_t byte_rate = 0;      ///< 字节率（采样率 × 声道数 × 位深/8）
  std::uint16_t block_align = 0;    ///< 块对齐（声道数 × 位深/8）
  std::uint16_t bits_per_sample = 0;///< 位深度（如 8、16）
  std::vector<std::uint8_t> samples;///< 原始 PCM 采样数据
};

/**
 * @struct WavReadResult
 * @brief WAV 文件读取结果
 */
struct WavReadResult {
  bool ok = false;           ///< 读取是否成功
  PcmWavData data;           ///< 解析出的 PCM 数据
  std::string error;         ///< 错误描述（ok=false 时有效）
};

/**
 * @brief 读取并解析 PCM WAV 文件
 *
 * @param path WAV 文件的路径
 * @return 读取结果（成功时包含完整的 PCM 数据，失败时包含错误描述）
 */
WavReadResult read_pcm_wav_file(const std::filesystem::path& path);

}  // namespace mir2::client
