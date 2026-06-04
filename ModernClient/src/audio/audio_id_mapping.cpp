/**
 * @file audio_id_mapping.cpp
 * @brief 音效 ID 映射表实现 —— 加载 sound_list.txt 并建立音效 ID → 文件路径的映射
 * @details 实现从音效列表文件解析路径、追加 Delphi 硬编码音效、
 *          分配动态音效 ID 的完整逻辑。
 */

#include "audio/audio_id_mapping.hpp"

#include <charconv>
#include <fstream>
#include <string>
#include <string_view>

#include "audio/sound_constants.hpp"

namespace mir2::client {
namespace {

std::string_view trim_ascii(std::string_view text) {
  const auto is_space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };

  while (!text.empty() && is_space(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_space(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

std::wstring widen_ascii(std::string_view text) {
  return std::wstring{text.begin(), text.end()};
}

bool parse_int(std::string_view text, int& value) {
  text = trim_ascii(text);
  if (text.empty()) {
    return false;
  }

  int parsed = 0;
  const auto* first = text.data();
  const auto* last = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(first, last, parsed);
  if (ec != std::errc{} || ptr != last) {
    return false;
  }
  value = parsed;
  return true;
}

}  // namespace

bool AudioIdMapping::load_from_file(
    const std::filesystem::path& sound_list_path) {
  clear();

  std::ifstream input(sound_list_path, std::ios::binary);
  if (!input) {
    return false;
  }

  int last_id = 0;
  std::string line;
  while (std::getline(input, line)) {
    const std::string_view trimmed = trim_ascii(line);
    if (trimmed.empty() || trimmed.front() == ';') {
      continue;
    }

    const auto delimiter = trimmed.find(':');
    if (delimiter == std::string_view::npos) {
      continue;
    }

    int sound_id = 0;
    if (!parse_int(trimmed.substr(0, delimiter), sound_id)) {
      continue;
    }

    if (sound_id <= last_id) {
      continue;
    }

    const std::string_view relative_path =
        trim_ascii(trimmed.substr(delimiter + 1));
    if (static_cast<std::size_t>(sound_id) >= paths_.size()) {
      paths_.resize(static_cast<std::size_t>(sound_id) + 1);
    }
    paths_[static_cast<std::size_t>(sound_id)] = widen_ascii(relative_path);
    last_id = sound_id;
  }

  dynamic_base_ = static_cast<int>(paths_.size());
  append_delphi_hardcoded_extras();
  assign_dynamic_sound_ids();
  return true;
}

void AudioIdMapping::clear() {
  paths_.clear();
  dynamic_base_ = -1;
  reset_dynamic_sound_ids();
}

const std::wstring* AudioIdMapping::path_for(int sound_id) const {
  if (sound_id < 0) {
    return nullptr;
  }
  const auto index = static_cast<std::size_t>(sound_id);
  if (index >= paths_.size()) {
    return nullptr;
  }
  return &paths_[index];
}

std::filesystem::path AudioIdMapping::resolve_path(
    const std::filesystem::path& asset_root,
    std::wstring_view relative_path) const {
  if (relative_path.empty()) {
    return {};
  }

  std::filesystem::path path{std::wstring{relative_path}};
  if (path.is_absolute()) {
    return path;
  }
  if (asset_root.empty()) {
    return path;
  }
  return asset_root / path;
}

void AudioIdMapping::append_delphi_hardcoded_extras() {
  static constexpr const wchar_t* kExtras[] = {
      L"wav\\newysound1.wav",
      L"wav\\newysound2.wav",
      L"wav\\newysound-mix.wav",
      L"wav\\HeroLogin.wav",
      L"wav\\HeroLogout.wav",
      L"wav\\S1-1.wav",
      L"wav\\S1-2.wav",
      L"wav\\S1-3.wav",
      L"wav\\Openbox.wav",
      L"wav\\SelectBoxFlash.wav",
      L"wav\\Flashbox.wav",
      L"wav\\hero-shield.wav",
      L"wav\\powerup.wav",
      L"wav\\M56-0.wav",
      L"wav\\M56-3.wav",
      L"wav\\cboZs1_start_m.wav",
      L"wav\\cboZs1_start_w.wav",
      L"wav\\cboZs2_start.wav",
      L"wav\\cboZs3_start_m.wav",
      L"wav\\cboZs3_start_w.wav",
      L"wav\\cboZs4_start.wav",
      L"wav\\cboFs1_start.wav",
      L"wav\\cboFs1_target.wav",
      L"wav\\cboFs2_start.wav",
      L"wav\\cboFs2_target.wav",
      L"wav\\cboFs3_start.wav",
      L"wav\\cboFs3_target.wav",
      L"wav\\cboFs4_start.wav",
      L"wav\\cboFs4_target.wav",
      L"wav\\cboDs1_start.wav",
      L"wav\\cboDs1_target.wav",
      L"wav\\cboDs2_start.wav",
      L"wav\\cboDs2_target.wav",
      L"wav\\cboDs3_start.wav",
      L"wav\\cboDs3_target.wav",
      L"wav\\cboDs4_start.wav",
      L"wav\\cboDs4_target.wav",
  };

  for (const wchar_t* extra : kExtras) {
    paths_.emplace_back(extra);
  }
}

void AudioIdMapping::assign_dynamic_sound_ids() {
  if (dynamic_base_ < 0) {
    reset_dynamic_sound_ids();
    return;
  }

  s_FireFlower_1 = dynamic_base_;
  s_FireFlower_2 = dynamic_base_ + 1;
  s_FireFlower_3 = dynamic_base_ + 2;
  s_HeroLogIn = dynamic_base_ + 3;
  s_HeroLogOut = dynamic_base_ + 4;
  s_Openbox = dynamic_base_ + 8;
  s_SelectBoxFlash = dynamic_base_ + 9;
  s_Flashbox = dynamic_base_ + 10;
  s_hero_shield = dynamic_base_ + 11;
  s_powerup = dynamic_base_ + 12;
  s_hit_ZRJF_M = dynamic_base_ + 13;
  s_hit_ZRJF_w = dynamic_base_ + 14;
  s_cboZs1_start_m = dynamic_base_ + 15;
  s_cboZs1_start_w = dynamic_base_ + 16;
  s_cboZs2_start = dynamic_base_ + 17;
  s_cboZs3_start_m = dynamic_base_ + 18;
  s_cboZs3_start_w = dynamic_base_ + 19;
  s_cboZs4_start = dynamic_base_ + 20;
  s_cboFs1_start = dynamic_base_ + 21;
  s_cboFs1_target = dynamic_base_ + 22;
  s_cboFs2_start = dynamic_base_ + 23;
  s_cboFs2_target = dynamic_base_ + 24;
  s_cboFs3_start = dynamic_base_ + 25;
  s_cboFs3_target = dynamic_base_ + 26;
  s_cboFs4_start = dynamic_base_ + 27;
  s_cboFs4_target = dynamic_base_ + 28;
  s_cboDs1_start = dynamic_base_ + 29;
  s_cboDs1_target = dynamic_base_ + 30;
  s_cboDs2_start = dynamic_base_ + 31;
  s_cboDs2_target = dynamic_base_ + 32;
  s_cboDs3_start = dynamic_base_ + 33;
  s_cboDs3_target = dynamic_base_ + 34;
  s_cboDs4_start = dynamic_base_ + 35;
  s_cboDs4_target = dynamic_base_ + 36;
}

void AudioIdMapping::reset_dynamic_sound_ids() {
  s_FireFlower_1 = -1;
  s_FireFlower_2 = -1;
  s_FireFlower_3 = -1;
  s_HeroLogIn = -1;
  s_HeroLogOut = -1;
  s_hero_shield = -1;
  s_SelectBoxFlash = -1;
  s_Flashbox = -1;
  s_Openbox = -1;
  s_powerup = -1;
  s_hit_ZRJF_M = -1;
  s_hit_ZRJF_w = -1;
  s_cboZs1_start_m = -1;
  s_cboZs1_start_w = -1;
  s_cboZs2_start = -1;
  s_cboZs3_start_m = -1;
  s_cboZs3_start_w = -1;
  s_cboZs4_start = -1;
  s_cboFs1_start = -1;
  s_cboFs1_target = -1;
  s_cboFs2_start = -1;
  s_cboFs2_target = -1;
  s_cboFs3_start = -1;
  s_cboFs3_target = -1;
  s_cboFs4_start = -1;
  s_cboFs4_target = -1;
  s_cboDs1_start = -1;
  s_cboDs1_target = -1;
  s_cboDs2_start = -1;
  s_cboDs2_target = -1;
  s_cboDs3_start = -1;
  s_cboDs3_target = -1;
  s_cboDs4_start = -1;
  s_cboDs4_target = -1;
}

}  // namespace mir2::client
