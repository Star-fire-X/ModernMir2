/**
 * @file audio_id_mapping.hpp
 * @brief 音效 ID 映射表 —— 建立音效 ID 到 WAV 文件路径的映射关系
 *
 * @details 加载经典传奇客户端的音效列表文件（sound_list.txt），
 *          将 Delphi 客户端的音效索引（sound_id）映射到对应的
 *          WAV 文件相对路径。同时处理固定编码的 Delphi 音效映射
 *          和动态音效 ID 分配。
 *
 * 映射逻辑：
 * 1. 从 sound_list.txt 加载音效路径列表（每行一条路径）
 * 2. 追加 Delphi 硬编码的特殊音效映射（append_delphi_hardcoded_extras）
 * 3. 分配动态音效 ID（assign_dynamic_sound_ids）
 *
 * @note 音效 ID 从 0 开始顺序分配。dynamic_base_ 标记动态 ID 的起始位置，
 *       用于区分从文件加载的固定映射和运行时动态分配的映射。
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace mir2::client {

/**
 * @class AudioIdMapping
 * @brief 音效 ID → WAV 文件路径的映射表
 *
 * @details 管理音效 ID 到文件路径的查找表。支持：
 *          - 从 sound_list.txt 批量加载映射
 *          - 根据音效 ID 查询对应的文件路径
 *          - 基于资源根目录解析相对路径为绝对路径
 */
class AudioIdMapping {
 public:
  /// 从音效列表文件加载映射表
  bool load_from_file(const std::filesystem::path& sound_list_path);
  /// 清空所有映射
  void clear();

  /// 根据音效 ID 查找对应的相对路径。不存在返回 nullptr
  [[nodiscard]] const std::wstring* path_for(int sound_id) const;
  /// 将相对路径解析为基于资源根目录的绝对路径
  [[nodiscard]] std::filesystem::path resolve_path(
      const std::filesystem::path& asset_root,
      std::wstring_view relative_path) const;

  [[nodiscard]] std::size_t size() const { return paths_.size(); }
  [[nodiscard]] int dynamic_base() const { return dynamic_base_; }
  [[nodiscard]] const std::vector<std::wstring>& paths() const {
    return paths_;
  }

 private:
  /// 追加 Delphi 客户端硬编码的特殊音效映射
  void append_delphi_hardcoded_extras();
  /// 分配动态音效 ID（用于运行时生成的音效）
  void assign_dynamic_sound_ids();
  /// 重置动态音效 ID 分配
  void reset_dynamic_sound_ids();

  std::vector<std::wstring> paths_;  ///< 音效相对路径列表（索引 = sound_id）
  int dynamic_base_ = -1;            ///< 动态音效 ID 起始位置（-1 表示未分配）
};

}  // namespace mir2::client
