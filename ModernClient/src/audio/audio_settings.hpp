/**
 * @file audio_settings.hpp
 * @brief 音频设置数据结构 —— 音效和背景音乐的开关与音量配置
 *
 * @details 定义客户端音频系统的用户设置参数。对应传奇客户端中
 *          的音频选项配置（通常保存在 client.ini 中）。
 *
 * 设置项：
 * - sound_enabled：音效开关（攻击声、UI 点击声等）
 * - bgm_enabled：背景音乐开关
 * - sound_volume：音效音量（0-100%）
 * - bgm_volume：背景音乐音量（0-100%）
 */

#pragma once

namespace mir2::client {

/**
 * @struct AudioSettings
 * @brief 音频设置 —— 音效和 BGM 的开关与音量配置
 */
struct AudioSettings {
  bool sound_enabled = true;   ///< 音效是否启用（默认开启）
  bool bgm_enabled = true;     ///< 背景音乐是否启用（默认开启）
  int sound_volume = 100;      ///< 音效音量（0-100%，默认 100%）
  int bgm_volume = 100;        ///< 背景音乐音量（0-100%，默认 100%）
};

}  // namespace mir2::client
