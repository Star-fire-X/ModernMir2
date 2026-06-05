/**
 * @file legacy_importer.cpp
 * @brief 遗留游戏资源导入器实现
 * @details 实现从旧版传奇服务端目录结构中解析各类资源文件（TXT 格式的 INI
 *          配置文件、MapInfo.txt、MonGen.txt、Merchant.txt、Npcs.txt、MapQuest.txt
 *          以及 Access 数据库），转换为新系统标准化的 TOML 配置文件格式。
 * @author mir2 Team
 * @date 2026-06-04
 */

#include "importer/legacy_importer.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef MIR2_ENABLE_ODBC
#include <windows.h>
#include <sqlext.h>
#endif

#include "util/legacy_text.hpp"
#include "util/string_utils.hpp"

namespace mir2 {

namespace {

/** @brief INI 配置文件的节（Section），键值对集合 */
using IniSection = std::unordered_map<std::string, std::string>;
/** @brief INI 配置文件，节名到 IniSection 的映射 */
using IniFile = std::unordered_map<std::string, IniSection>;

/**
 * @brief 将字符串中的非 ASCII 可打印字符替换为下划线
 * @param value 原始字符串
 * @return std::string 处理后的安全 ASCII 字符串
 * @details 将所有不在 32-126 范围内的字节替换为 '_'，
 *          确保输出文件名和 TOML 键值仅包含可打印 ASCII 字符。
 */
std::string ascii_safe(std::string value) {
  for (char& ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 32 || byte > 126) {
      ch = '_';
    }
  }
  return value;
}

/**
 * @brief 将字符串中的控制字符（ASCII < 32 或 == 127）替换为下划线
 * @param value 原始字符串
 * @return std::string 处理后的字符串
 * @details 比 ascii_safe 宽松，保留扩展 ASCII 字符，仅替换控制字符。
 */
std::string control_safe(std::string value) {
  for (char& ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 32 || byte == 127) {
      ch = '_';
    }
  }
  return value;
}

/**
 * @brief 为 TOML 格式转义并包裹字符串引号
 * @param value 原始字符串
 * @return std::string 转义后带双引号的 TOML 字符串值
 * @details 处理字符串中的反斜杠和双引号（添加转义符），
 *          确保生成的 TOML 文件格式正确。
 */
std::string quote(const std::string& value) {
  const auto normalized = control_safe(value);
  std::string escaped;
  escaped.reserve(normalized.size() + 8);
  for (const char ch : normalized) {
    if (ch == '\\' || ch == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return "\"" + escaped + "\"";
}

/**
 * @brief 读取文本文件的所有行并去除首尾空白
 * @param path 文件路径
 * @return std::vector<std::string> 处理后的行列表
 * @details 使用 util::read_legacy_text_lines 处理旧版文件的编码问题，
 *          然后对每行调用 util::trim 去除前后空白。
 */
std::vector<std::string> read_lines(const std::filesystem::path& path) {
  std::vector<std::string> lines;
  for (auto line : util::read_legacy_text_lines(path)) {
    lines.push_back(util::trim(std::move(line)));
  }
  return lines;
}

/**
 * @brief 解析旧版 INI 格式的配置文件
 * @param path INI 文件路径
 * @return IniFile 解析后的 INI 数据结构（节名 -> 键值对映射）
 * @details 支持标准 INI 格式：[节名] 和 key=value。
 *          以分号 ";" 开头的行视为注释被忽略。
 *          无节名的行归入 "default" 节。
 */
IniFile parse_ini(const std::filesystem::path& path) {
  IniFile result;
  std::string current = "default";
  for (const auto& line : read_lines(path)) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      current = line.substr(1, line.size() - 2);
      continue;
    }
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    const auto key = util::trim(line.substr(0, separator));
    const auto value = util::trim(line.substr(separator + 1));
    result[current][key] = value;
  }
  return result;
}

/**
 * @brief 按空白符分割字符串
 * @param line 待分割的字符串
 * @return std::vector<std::string> 分割后的 token 列表
 * @details 使用 istringstream 的 operator>> 进行分割，自动处理连续空白。
 */
std::vector<std::string> split_ws(const std::string& line) {
  std::istringstream stream(line);
  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

/**
 * @brief 解析旧版传奇风格的字段分隔格式
 * @param line 待解析的行
 * @return std::vector<std::string> 解析后的字段列表
 * @details 支持以下特性：
 *          - 使用空白字符作为字段分隔符
 *          - 支持双引号包裹的字段（引号内可包含空格）
 *          - 分号 ";" 作为行内注释起始标记
 *          此格式常见于旧版传奇的 MonGen.txt、Merchant.txt 等配置文件。
 */
std::vector<std::string> split_legacy_fields(std::string_view line) {
  std::vector<std::string> tokens;
  std::string current;
  bool quoted = false;
  for (const auto ch : line) {
    if (ch == '"') {
      quoted = !quoted;
      continue;
    }
    if (!quoted && ch == ';') {
      break;
    }
    if (!quoted && std::isspace(static_cast<unsigned char>(ch)) != 0) {
      if (!current.empty()) {
        tokens.push_back(std::move(current));
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    tokens.push_back(std::move(current));
  }
  return tokens;
}

/**
 * @brief 将 token 列表的指定范围拼接为字符串
 * @param tokens token 列表
 * @param first 起始索引（包含）
 * @param last_exclusive 结束索引（不包含）
 * @return std::string 拼接后的字符串，token 之间以空格分隔
 */
std::string join_tokens(const std::vector<std::string>& tokens, std::size_t first,
                        std::size_t last_exclusive) {
  std::string result;
  for (std::size_t index = first; index < tokens.size() && index < last_exclusive; ++index) {
    if (!result.empty()) {
      result.push_back(' ');
    }
    result += tokens[index];
  }
  return result;
}

/**
 * @brief 安全地将字符串解析为 32 位整数
 * @param text 待解析的文本
 * @return std::optional<std::int32_t> 解析成功时包含整数值，失败时为空
 * @details 使用 std::stoi 进行解析，要求整个字符串完全匹配数字格式。
 *          捕获所有异常并返回空 optional。
 */
std::optional<std::int32_t> parse_int32(std::string_view text) {
  try {
    std::size_t consumed = 0;
    const auto value = std::stoi(std::string(text), &consumed, 10);
    if (consumed == text.size()) {
      return value;
    }
  } catch (...) {
  }
  return std::nullopt;
}

/**
 * @brief 解析逗号分隔的 X,Y 坐标字符串
 * @param text 坐标文本（格式："x,y"）
 * @return std::optional<std::pair<std::int32_t, std::int32_t>> 解析成功时返回坐标对
 * @details 从 "330,270" 格式的字符串中提取 X 和 Y 值。
 *          要求逗号前后均有有效数字。
 */
std::optional<std::pair<std::int32_t, std::int32_t>> parse_xy_token(std::string_view text) {
  const auto comma = text.find(',');
  if (comma == std::string_view::npos || comma == 0 || comma + 1 >= text.size()) {
    return std::nullopt;
  }
  const auto x = parse_int32(text.substr(0, comma));
  const auto y = parse_int32(text.substr(comma + 1));
  if (!x.has_value() || !y.has_value()) {
    return std::nullopt;
  }
  return std::pair<std::int32_t, std::int32_t>{*x, *y};
}

/**
 * @brief 将怪物/物品名称转换为不区分大小写的键值
 * @param name 原始名称
 * @return std::string 归一化后的键值（ASCII 安全 + 小写）
 * @details 用于在集合中查找怪物或物品名称时忽略大小写差异。
 */
std::string legacy_name_key(std::string name) {
  return util::lower_copy(ascii_safe(std::move(name)));
}

/**
 * @brief 计算指定数量字段之后的字节偏移量
 * @param line 原始行文本
 * @param field_count 需要跳过的字段数量
 * @return std::size_t 字段后面的起始位置，如果字段不足则返回 npos
 * @details 用于在 MonGen.txt 解析中，跳过前 N 个字段后定位怪物名。
 *          正确处理多个连续空白字符。
 */
std::size_t position_after_fields(std::string_view line, std::size_t field_count) {
  std::size_t pos = 0;
  for (std::size_t field = 0; field < field_count; ++field) {
    while (pos < line.size() &&
           std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
      ++pos;
    }
    if (pos >= line.size()) {
      return std::string_view::npos;
    }
    while (pos < line.size() &&
           std::isspace(static_cast<unsigned char>(line[pos])) == 0) {
      ++pos;
    }
  }
  while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])) != 0) {
    ++pos;
  }
  return pos;
}

/**
 * @struct LegacyMonGenRecord
 * @brief 旧版 MonGen.txt 中的刷怪记录结构
 * @details 解析后的刷怪配置，包含地图ID、坐标、怪物名、刷新范围、
 *          刷新数量、刷新间隔和小爆率参数。
 */
struct LegacyMonGenRecord {
  std::string map_id;       ///< 刷怪地图ID
  std::string x;            ///< 刷怪 X 坐标
  std::string y;            ///< 刷怪 Y 坐标
  std::string name;         ///< 怪物名称
  std::string area = "0";   ///< 刷新范围半径（0 表示固定点刷新）
  std::string count = "1";  ///< 同时存在的最大怪物数量
  std::string zen_minutes = "1";  ///< 刷新间隔（分钟）
  std::string small_zen_rate = "0";  ///< 小爆率参数
};

/**
 * @brief 解析 MonGen.txt 中的单行刷怪记录
 * @param line 刷怪配置行
 * @param known_monster_names 已知怪物名称集合（用于名称边界识别）
 * @return std::optional<LegacyMonGenRecord> 解析成功时返回刷怪记录
 * @details MonGen.txt 格式复杂，怪物名可能包含空格且不由引号包裹。
 *          本函数使用启发式算法：
 *          1. 优先尝试解析带引号的怪物名
 *          2. 从行尾反向识别数字字段来确定怪物名结束位置
 *          3. 使用已知怪物名称集合辅助判断名称边界
 *
 *          行格式：MAP X Y 怪物名 [区域 数量 间隔 小爆率]
 * @note 怪物名可能包含空格（如 "White Tiger"），字段边界需要通过上下文判断。
 */
std::optional<LegacyMonGenRecord> parse_mongen_record(
    std::string_view line,
    const std::set<std::string>& known_monster_names) {
  const auto tokens = split_legacy_fields(line);
  if (tokens.size() < 4) {
    return std::nullopt;
  }

  LegacyMonGenRecord record;
  record.map_id = tokens[0];
  record.x = tokens[1];
  record.y = tokens[2];

  /* 尝试解析带引号的怪物名 */
  const auto name_pos = position_after_fields(line, 3);
  if (name_pos != std::string_view::npos && name_pos < line.size() && line[name_pos] == '"') {
    std::string name;
    std::size_t index = name_pos + 1;
    for (; index < line.size(); ++index) {
      if (line[index] == '"') {
        ++index;
        break;
      }
      name.push_back(line[index]);
    }
    if (name.empty()) {
      return std::nullopt;
    }
    record.name = std::move(name);
    const auto tail_tokens = split_legacy_fields(line.substr(index));
    const std::size_t tail_count = std::min<std::size_t>(tail_tokens.size(), 4);
    if (tail_count > 0 && parse_int32(tail_tokens[0]).has_value()) {
      record.area = tail_tokens[0];
    }
    if (tail_count > 1 && parse_int32(tail_tokens[1]).has_value()) {
      record.count = tail_tokens[1];
    }
    if (tail_count > 2 && parse_int32(tail_tokens[2]).has_value()) {
      record.zen_minutes = tail_tokens[2];
    }
    if (tail_count > 3 && parse_int32(tail_tokens[3]).has_value()) {
      record.small_zen_rate = tail_tokens[3];
    }
    return record;
  }

  /* 启发式解析：从行尾反向匹配数字字段以确定怪物名边界 */
  std::size_t tail_start = tokens.size();
  std::size_t numeric_tail_count = 0;
  bool matched_known_name = false;
  const auto max_tail = std::min<std::size_t>(tokens.size() - 4, 4);
  for (std::size_t tail_count = 0; tail_count <= max_tail; ++tail_count) {
    const auto candidate_tail_start = tokens.size() - tail_count;
    auto numeric_tail = true;
    for (std::size_t index = candidate_tail_start; index < tokens.size(); ++index) {
      numeric_tail = numeric_tail && parse_int32(tokens[index]).has_value();
    }
    if (!numeric_tail) {
      continue;
    }
    const auto candidate_name = join_tokens(tokens, 3, candidate_tail_start);
    if (!candidate_name.empty() &&
        known_monster_names.find(legacy_name_key(candidate_name)) != known_monster_names.end()) {
      tail_start = candidate_tail_start;
      numeric_tail_count = tail_count;
      matched_known_name = true;
      break;
    }
  }
  /* 如果无法通过已知名称匹配，则假设从尾部开始的连续数字字段为数值参数 */
  if (!matched_known_name) {
    std::size_t run_start = tokens.size();
    while (run_start > 4 && parse_int32(tokens[run_start - 1]).has_value()) {
      --run_start;
    }
    const auto numeric_run_count = tokens.size() - run_start;
    numeric_tail_count = std::min<std::size_t>(numeric_run_count, 4);
    if (numeric_tail_count > 0) {
      tail_start = tokens.size() - numeric_tail_count;
    } else {
      tail_start = tokens.size();
    }
  }

  auto name = join_tokens(tokens, 3, tail_start);
  if (name.empty()) {
    return std::nullopt;
  }

  record.name = std::move(name);
  if (numeric_tail_count > 0) {
    record.area = tokens[tail_start];
  }
  if (numeric_tail_count > 1) {
    record.count = tokens[tail_start + 1];
  }
  if (numeric_tail_count > 2) {
    record.zen_minutes = tokens[tail_start + 2];
  }
  if (numeric_tail_count > 3) {
    record.small_zen_rate = tokens[tail_start + 3];
  }
  return record;
}

/**
 * @brief 查找旧版 MakeItem.txt 文件路径
 * @param legacy_root 旧版服务端根目录
 * @return std::filesystem::path MakeItem.txt 的完整路径，未找到时返回空路径
 * @details 按优先级搜索以下路径：
 *          1. Envir/MakeItem.txt
 *          2. Envir/MakeItem.TXT
 *          3. MakeItem.txt
 */
std::filesystem::path legacy_makeitem_path(const std::filesystem::path& legacy_root) {
  for (const auto& candidate : {
      legacy_root / "Envir" / "MakeItem.txt",
      legacy_root / "Envir" / "MakeItem.TXT",
      legacy_root / "MakeItem.txt",
  }) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

/**
 * @brief 解析 TOML 行中的 name 字段值
 * @param line TOML 格式的行（如 name = "SomeItem"）
 * @return std::optional<std::string> 解析成功时返回 name 的值（不含引号）
 * @details 支持转义字符处理，用于从已导入的 TOML 文件中读取名称字段。
 */
std::optional<std::string> parse_toml_name_field(std::string_view line) {
  const auto name_pos = line.find("name");
  if (name_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto equals_pos = line.find('=', name_pos);
  if (equals_pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto quote_pos = line.find('"', equals_pos);
  if (quote_pos == std::string_view::npos) {
    return std::nullopt;
  }
  std::string value;
  bool escaped = false;
  for (std::size_t index = quote_pos + 1; index < line.size(); ++index) {
    const auto ch = line[index];
    if (escaped) {
      value.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '"') {
      return value;
    }
    value.push_back(ch);
  }
  return std::nullopt;
}

/**
 * @brief 加载已导入 TOML 文件中的物品名称集合
 * @param output_root 新系统输出根目录
 * @return std::set<std::string> 已导入的物品名称（归一化键值）
 * @details 从 imported_items.toml 中解析所有记录的 name 字段，
 *          转换为 legacy_name_key 格式以便后续不区分大小写的匹配。
 */
std::set<std::string> load_imported_item_names(const std::filesystem::path& output_root) {
  std::set<std::string> names;
  for (const auto& line : read_lines(output_root / "items" / "imported_items.toml")) {
    const auto name = parse_toml_name_field(line);
    if (name.has_value() && !name->empty()) {
      names.insert(legacy_name_key(*name));
    }
  }
  return names;
}

/**
 * @brief 加载旧版 MakeItem.txt 中的物品名称集合
 * @param legacy_root 旧版服务端根目录
 * @return std::set<std::string> 旧版物品名称（归一化键值）
 * @details 读取 MakeItem.txt 中的物品名称行，每行包含一个物品名
 *          （可能带有多段空格分隔的描述，但物品名从行首开始）。
 */
std::set<std::string> load_legacy_makeitem_names(const std::filesystem::path& legacy_root) {
  std::set<std::string> names;
  for (const auto& line : read_lines(legacy_makeitem_path(legacy_root))) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    const auto tokens = split_legacy_fields(line);
    if (tokens.empty()) {
      continue;
    }
    const auto item_name = join_tokens(tokens, 0, tokens.size());
    if (!item_name.empty()) {
      names.insert(legacy_name_key(item_name));
    }
  }
  return names;
}

/**
 * @brief 加载已导入 TOML 文件中的怪物名称集合
 * @param output_root 新系统输出根目录
 * @return std::set<std::string> 已导入的怪物名称（归一化键值）
 * @details 从 imported_monsters.toml 中解析所有记录的 name 字段。
 * @see load_imported_item_names
 */
std::set<std::string> load_imported_monster_names(const std::filesystem::path& output_root) {
  std::set<std::string> names;
  for (const auto& line : read_lines(output_root / "monsters" / "imported_monsters.toml")) {
    const auto name = parse_toml_name_field(line);
    if (name.has_value() && !name->empty()) {
      names.insert(legacy_name_key(*name));
    }
  }
  return names;
}

/**
 * @brief 加载 MonItems 目录中所有 TXT 文件的怪物名（带排序）
 * @param legacy_root 旧版服务端根目录
 * @return std::vector<std::string> 怪物名称列表（按文件名排序）
 * @details 遍历 Envir/MonItems 目录下的所有 .txt 文件，
 *          按文件名的通用字符串排序，返回每个文件的 stem（不含扩展名）。
 *          MonItems 目录中的每个 .txt 文件对应一种怪物的掉落配置。
 */
std::vector<std::string> load_legacy_monitem_monster_name_values(
    const std::filesystem::path& legacy_root) {
  std::vector<std::string> names;
  const auto directory = legacy_root / "Envir" / "MonItems";
  if (!std::filesystem::exists(directory)) {
    return names;
  }
  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file() &&
        util::lower_copy(entry.path().extension().string()) == ".txt") {
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.generic_string() < rhs.generic_string();
  });
  for (const auto& path : paths) {
    names.push_back(path.stem().string());
  }
  return names;
}

/**
 * @brief 加载 MonItems 目录中所有怪物的名称集合
 * @param legacy_root 旧版服务端根目录
 * @return std::set<std::string> 怪物名称集合（归一化键值）
 * @see load_legacy_monitem_monster_name_values
 */
std::set<std::string> load_legacy_monitem_monster_names(const std::filesystem::path& legacy_root) {
  std::set<std::string> names;
  for (const auto& name : load_legacy_monitem_monster_name_values(legacy_root)) {
    names.insert(legacy_name_key(name));
  }
  return names;
}

/**
 * @brief 从候选路径列表中返回第一个存在的路径
 * @param paths 候选路径列表
 * @return std::filesystem::path 第一个存在的路径，都不存在时返回空路径
 * @details 用于搜索大小写变体（如 MapInfo.txt / mapinfo.txt / MAPINFO.TXT）。
 */
std::filesystem::path first_existing_path(std::initializer_list<std::filesystem::path> paths) {
  for (const auto& path : paths) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }
  return {};
}

/**
 * @brief 从旧版传奇的选项 token 中提取括号内的有效负载
 * @param token 选项 token（格式如 "option(value)" 或 "option=value"）
 * @return std::string 提取出的有效负载字符串
 * @details 支持两种格式：
 *          - 括号格式：noreconnect(0) -> "0"
 *          - 等号格式：option=value -> "value"
 */
std::string option_payload(const std::string& token) {
  const auto open = token.find('(');
  const auto close = token.rfind(')');
  if (open != std::string::npos && close != std::string::npos && close > open + 1) {
    return token.substr(open + 1, close - open - 1);
  }
  const auto equals = token.find('=');
  if (equals != std::string::npos && equals + 1 < token.size()) {
    return token.substr(equals + 1);
  }
  return {};
}

/**
 * @struct ImportedGate
 * @brief 已导入的地图传送门结构
 * @details 描述地图上的传送点，包含源坐标和目标坐标。
 *          对应 MapInfo.txt 中的 "->" 传送门定义行。
 */
struct ImportedGate {
  std::int32_t x{0};              ///< 传送门在源地图的 X 坐标
  std::int32_t y{0};              ///< 传送门在源地图的 Y 坐标
  std::string target_map_id{};    ///< 目标地图ID
  std::int32_t target_x{0};       ///< 目标地图 X 坐标
  std::int32_t target_y{0};       ///< 目标地图 Y 坐标
};

/**
 * @struct ImportedMapInfo
 * @brief 已导入的地图信息结构
 * @details 存储从 MapInfo.txt 解析出的地图配置，包括地图基本属性、
 *          各种区域标记、传送门列表等。地图标记控制角色在该地图中的行为规则。
 */
struct ImportedMapInfo {
  std::string id{};                   ///< 地图ID
  std::string title{};                ///< 地图显示名称
  bool law_full{false};               ///< 安全区标记（禁止PK）
  bool fight_zone{false};             ///< 战斗区域标记（允许自由PK）
  bool fight3_zone{false};            ///< 行会战争区域标记
  bool daylight{false};               ///< 总是白天标记
  bool darkness{false};               ///< 黑暗地图标记
  bool no_reconnect{false};           ///< 断线后不自动重连到此地图
  bool need_hole{false};              ///< 需要洞穴通行证
  bool no_recall{false};              ///< 禁止记忆召回
  bool no_random_move{false};         ///< 禁止随机传送
  bool no_drug{false};                ///< 禁止使用药品
  bool no_position_move{false};       ///< 禁止定位移动
  std::int32_t need_level{0};         ///< 进入所需最低等级
  std::int32_t mine_map{0};           ///< 矿区地图编号（1=矿区1，2=矿区2）
  std::string back_map{};             ///< 断线后返回的地图ID
  std::vector<ImportedGate> gates{};  ///< 地图传送门列表
  bool quiz_zone{false};              ///< 问答区域标记
  std::string check_quest{};          ///< 需要完成的任务检查
  std::int32_t need_set_number{-1};   ///< 需要设置的任务编号（配合 need_set_value）
  std::int32_t need_set_value{-1};    ///< 需要设置的任务值（配合 need_set_number）
};

/**
 * @brief 应用 MapInfo.txt 中的地图标记/标志
 * @param info 地图信息结构体引用
 * @param tail MapInfo.txt 行中括号后面的标志文本
 * @details 解析旧版 MapInfo.txt 中各种地图标志（如 SAFE、FIGHT、DARK 等），
 *          应用到 ImportedMapInfo 结构体的对应字段。
 *
 *          支持的主要标记：
 *          - SAFE / FIGHT / FIGHT3：PK 规则标记
 *          - DAY / DARK：光照标记
 *          - NORECALL / NORANDOMMOVE / NODRUG / NOPOSITIONMOVE：行为限制
 *          - NEEDHOLE：洞穴通行证需求
 *          - MINE / MINE2：矿区类型
 *          - QUIZ：问答区域
 *          - CHECKQUEST：任务检查
 *          - NEEDSET_ON / NEEDSET_OFF：任务状态需求
 *          - L{level}：等级需求（如 L30 表示需要 30 级）
 * @note 标记不区分大小写，支持方括号包裹（如 [SAFE]）。
 */
void apply_mapinfo_flags(ImportedMapInfo& info, const std::string& tail) {
  for (auto token : split_ws(tail)) {
    token = util::trim(std::move(token));
    /* 移除首尾的方括号（有些标记被 [] 包裹） */
    while (!token.empty() && (token.front() == '[' || token.front() == ']')) {
      token.erase(token.begin());
    }
    while (!token.empty() && (token.back() == '[' || token.back() == ']')) {
      token.pop_back();
    }
    const auto lower = util::lower_copy(token);
    if (lower == "safe") {
      info.law_full = true;
    } else if (lower == "fight") {
      info.fight_zone = true;
    } else if (lower == "fight3") {
      info.fight3_zone = true;
    } else if (lower == "day") {
      info.daylight = true;
    } else if (lower == "quiz") {
      info.quiz_zone = true;
    } else if (lower == "dark") {
      info.darkness = true;
    } else if (util::starts_with(lower, "noreconnect")) {
      info.no_reconnect = true;
      info.back_map = option_payload(token);
    } else if (util::starts_with(lower, "checkquest")) {
      info.check_quest = option_payload(token);
    } else if (util::starts_with(lower, "needset_on")) {
      info.need_set_number = parse_int32(option_payload(token)).value_or(-1);
      info.need_set_value = 1;
    } else if (util::starts_with(lower, "needset_off")) {
      info.need_set_number = parse_int32(option_payload(token)).value_or(-1);
      info.need_set_value = 0;
    } else if (lower == "needhole") {
      info.need_hole = true;
    } else if (lower == "norecall") {
      info.no_recall = true;
    } else if (lower == "norandommove") {
      info.no_random_move = true;
    } else if (lower == "nodrug") {
      info.no_drug = true;
    } else if (lower == "nopositionmove") {
      info.no_position_move = true;
    } else if (lower == "mine") {
      info.mine_map = 1;
    } else if (lower == "mine2") {
      info.mine_map = 2;
    } else if (lower.size() > 1 && lower.front() == 'l') {
      if (const auto value = parse_int32(lower.substr(1)); value.has_value()) {
        info.need_level = *value;
      }
    }
  }
}

/* 前向声明 */
std::string import_mapquest_script_asset(const std::filesystem::path& legacy_root,
                                         const std::filesystem::path& output_root,
                                         std::string_view qfile);

/**
 * @brief 创建新系统的输出目录结构
 * @param output_root 新系统输出根目录
 * @details 创建所有必需的子目录：
 *          runtime、maps、spawns、monsters、items、magic、
 *          npcs、map_quests、npc_scripts 及其子目录。
 */
void ensure_output_dirs(const std::filesystem::path& output_root) {
  std::filesystem::create_directories(output_root / "runtime");
  std::filesystem::create_directories(output_root / "maps");
  std::filesystem::create_directories(output_root / "spawns");
  std::filesystem::create_directories(output_root / "monsters");
  std::filesystem::create_directories(output_root / "items");
  std::filesystem::create_directories(output_root / "magic");
  std::filesystem::create_directories(output_root / "npcs");
  std::filesystem::create_directories(output_root / "map_quests");
  std::filesystem::create_directories(output_root / "npc_scripts" / "market_def");
  std::filesystem::create_directories(output_root / "npc_scripts" / "Npc_def");
  std::filesystem::create_directories(output_root / "npc_scripts" / "MapQuest_def");
  std::filesystem::create_directories(output_root / "npc_scripts" / "Defines");
  std::filesystem::create_directories(output_root / "npc_scripts" / "QuestDiary");
}

/**
 * @brief 递归复制旧版 NPC 脚本目录到新系统
 * @param legacy_root 旧版服务端根目录
 * @param output_root 新系统输出根目录
 * @param legacy_subdir 旧版中的子目录名（如 "Defines"）
 * @param output_subdir 新系统中的目标子目录名
 * @details 递归遍历旧版 Envir/{legacy_subdir} 目录中的所有文件和子目录，
 *          以 ASCII 安全路径名复制到 output_root/npc_scripts/{output_subdir}。
 *          如果源目录不存在则静默跳过。
 * @note 使用 std::error_code 忽略权限错误，确保不会因单个文件拷贝失败而中断整个流程。
 */
void copy_legacy_script_tree(const std::filesystem::path& legacy_root,
                             const std::filesystem::path& output_root,
                             const std::filesystem::path& legacy_subdir,
                             const std::filesystem::path& output_subdir) {
  const auto source_root = legacy_root / "Envir" / legacy_subdir;
  if (!std::filesystem::exists(source_root)) {
    return;
  }
  const auto target_root = output_root / "npc_scripts" / output_subdir;
  std::error_code ignored;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(source_root, ignored)) {
    if (ignored) {
      break;
    }
    const auto relative = std::filesystem::relative(entry.path(), source_root, ignored);
    if (ignored || relative.empty()) {
      ignored.clear();
      continue;
    }
    const auto target = target_root / util::ascii_path(relative);
    if (entry.is_directory(ignored)) {
      std::filesystem::create_directories(target, ignored);
      ignored.clear();
      continue;
    }
    if (!entry.is_regular_file(ignored)) {
      ignored.clear();
      continue;
    }
    std::filesystem::create_directories(target.parent_path(), ignored);
    ignored.clear();
    std::filesystem::copy_file(entry.path(), target,
                               std::filesystem::copy_options::overwrite_existing, ignored);
    ignored.clear();
  }
}

/**
 * @brief 写入服务端配置文件（server.toml、ports.toml、runtime/logic.toml）
 * @param setup 从 !SetUp.txt 解析出的 INI 配置
 * @param output_root 新系统输出根目录
 * @details 生成三个配置文件：
 *          - server.toml：服务端基本配置（日志、数据目录、IO 线程数等）
 *          - ports.toml：网关端口配置（登录网关 5500，游戏网关 7000）
 *          - runtime/logic.toml：运行时性能配置（各模块的时间预算，从旧版配置迁移）
 * @note 旧版 !SetUp.txt 中的 HumLimit、MonLimit、ZenLimit、NpcLimit、SocLimit
 *       被映射到新系统的 budget_ms 参数。
 */
void write_server_files(const IniFile& setup, const std::filesystem::path& output_root) {
  const auto& server = setup.contains("Server") ? setup.at("Server") : IniSection{};
  const auto& share = setup.contains("Share") ? setup.at("Share") : IniSection{};

  {
    std::ofstream file(output_root / "server.toml", std::ios::binary | std::ios::trunc);
    file << "log_dir = \"logs\"\n";
    file << "data_dir = \"data\"\n";
    file << "status_file = \"runtime/status.json\"\n";
    file << "default_queue_capacity = 4096\n";
    file << "io_threads = 2\n";
    file << "backpressure_threshold = 3072\n";
    file << "disconnect_threshold = 3\n";
  }

  {
    std::ofstream file(output_root / "ports.toml", std::ios::binary | std::ios::trunc);
    file << "[login_gateway]\n";
    file << "address = \"127.0.0.1\"\n";
    file << "port = 5500\n\n";
    file << "[game_gateway]\n";
    file << "address = \"127.0.0.1\"\n";
    file << "port = " << (server.contains("IDSPort") ? 7000 : 7000) << "\n";
  }

  {
    std::ofstream file(output_root / "runtime" / "logic.toml", std::ios::binary | std::ios::trunc);
    file << "tick_ms = 10\n";
    file << "player_budget_ms = " << (server.contains("HumLimit") ? server.at("HumLimit") : "30")
         << "\n";
    file << "monster_budget_ms = "
         << (server.contains("MonLimit") ? server.at("MonLimit") : "30") << "\n";
    file << "spawn_budget_ms = " << (server.contains("ZenLimit") ? server.at("ZenLimit") : "30")
         << "\n";
    file << "npc_budget_ms = " << (server.contains("NpcLimit") ? server.at("NpcLimit") : "5")
         << "\n";
    file << "net_flush_budget_ms = "
         << (server.contains("SocLimit") ? server.at("SocLimit") : "30") << "\n";
  }
}

/**
 * @brief 导入地图信息和刷怪配置
 * @param legacy_root 旧版服务端根目录
 * @param output_root 新系统输出根目录
 * @param setup 从 !SetUp.txt 解析出的 INI 配置
 * @return LegacyImportReport 导入结果报告（含地图和刷怪计数）
 * @details 导入流程：
 *          1. 从 !SetUp.txt 读取玩家出生点（HomeMap/HomeX/HomeY）
 *          2. 解析 StartPoint.txt 获取各地图安全区坐标
 *          3. 解析 MapInfo.txt 获取地图定义、标记和传送门
 *          4. 为每个地图生成 maps/{map_id}.toml 配置文件
 *          5. 解析 MonGen.txt/MonZen.txt 生成刷怪配置
 *          6. 输出 spawns/imported_monsters.toml
 * @note 地图行走门的解析支持两种格式：
 *          格式一：MAP X -> TARGET_MAP TARGET_X TARGET_Y
 *          格式二：MAP X Y -> TARGET_MAP TARGET_X TARGET_Y
 *       如果未导入任何地图，会创建一个使用 HomeMap 的默认地图。
 */
LegacyImportReport import_maps_and_spawns(const std::filesystem::path& legacy_root,
                                          const std::filesystem::path& output_root, const IniFile& setup) {
  LegacyImportReport report;
  std::set<std::string> maps;

  /* 读取默认玩家出生点配置 */
  std::string home_map = "0";
  std::string home_x = "330";
  std::string home_y = "270";
  if (setup.contains("Setup")) {
    const auto& section = setup.at("Setup");
    if (section.contains("HomeMap")) home_map = section.at("HomeMap");
    if (section.contains("HomeX")) home_x = section.at("HomeX");
    if (section.contains("HomeY")) home_y = section.at("HomeY");
  }
  const auto setup_home_x = parse_int32(home_x).value_or(0);
  const auto setup_home_y = parse_int32(home_y).value_or(0);
  maps.insert(home_map);

  /* 解析 StartPoint.txt 获取安全区 */
  std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> start_points;
  const auto start_point_path = first_existing_path({
      legacy_root / "Envir" / "StartPoint.txt",
      legacy_root / "Envir" / "startpoint.txt",
      legacy_root / "Envir" / "STARTPOINT.TXT",
  });
  for (const auto& line : read_lines(start_point_path)) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    const auto tokens = split_ws(line);
    if (tokens.size() >= 3) {
      maps.insert(tokens[0]);
      start_points[tokens[0]].push_back({tokens[1], tokens[2]});
    }
  }

  /* 解析 MapInfo.txt 获取地图定义 */
  std::vector<std::string> map_order;
  std::unordered_map<std::string, ImportedMapInfo> map_infos;
  const auto mapinfo_path = first_existing_path({
      legacy_root / "Envir" / "MapInfo.txt",
      legacy_root / "Envir" / "mapinfo.txt",
      legacy_root / "Envir" / "MAPINFO.TXT",
  });
  for (const auto& line : read_lines(mapinfo_path)) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }

    /* 解析方括号格式的地图定义：[id title](flags) */
    const auto open = line.find('[');
    const auto close = line.find(']', open == std::string::npos ? 0 : open + 1);
    if (open != std::string::npos && close != std::string::npos && close > open + 1) {
      const auto header = line.substr(open + 1, close - open - 1);
      const auto tokens = split_ws(header);
      if (!tokens.empty()) {
        const auto map_id = tokens.front();
        ImportedMapInfo info;
        info.id = map_id;
        /* 标题可能由多个 token 组成，但若最后一个 token 是数字则排除（旧版可能包含无效数字字段） */
        const auto title_last = tokens.size() >= 2 && parse_int32(tokens.back()).has_value()
                                    ? tokens.size() - 1
                                    : tokens.size();
        info.title = title_last > 1 ? join_tokens(tokens, 1, title_last) : map_id;
        apply_mapinfo_flags(info, line.substr(close + 1));
        maps.insert(map_id);
        map_order.push_back(map_id);
        map_infos[map_id] = std::move(info);
      }
      continue;
    }

    /* 解析传送门定义（两种格式） */
    const auto tokens = split_ws(line);
    bool gate_parsed = false;
    /* 格式一：MAP X -> TARGET_MAP TARGET_X TARGET_Y */
    if (tokens.size() >= 5 && tokens[2] == "->") {
      const auto source = parse_xy_token(tokens[1]);
      const auto target = parse_xy_token(tokens[4]);
      if (source.has_value() && target.has_value()) {
        const auto map_id = tokens[0];
        auto& info = map_infos[map_id];
        if (info.id.empty()) {
          info.id = map_id;
          info.title = map_id;
          map_order.push_back(map_id);
        }
        info.gates.push_back(
            ImportedGate{source->first, source->second, tokens[3], target->first, target->second});
        maps.insert(map_id);
        gate_parsed = true;
      }
    }

    /* 格式二：MAP X Y -> TARGET_MAP TARGET_X TARGET_Y */
    if (!gate_parsed && tokens.size() >= 7 && tokens[3] == "->") {
      const auto map_id = tokens[0];
      const auto x = parse_int32(tokens[1]);
      const auto y = parse_int32(tokens[2]);
      const auto target_x = parse_int32(tokens[5]);
      const auto target_y = parse_int32(tokens[6]);
      if (!x.has_value() || !y.has_value() || !target_x.has_value() || !target_y.has_value()) {
        report.warnings.push_back("Skipped invalid gate line: " + line);
        continue;
      }
      auto& info = map_infos[map_id];
      if (info.id.empty()) {
        info.id = map_id;
        info.title = map_id;
        map_order.push_back(map_id);
      }
      info.gates.push_back(ImportedGate{*x, *y, tokens[4], *target_x, *target_y});
      maps.insert(map_id);
      gate_parsed = true;
    }

    /* 记录无法解析的传送门行 */
    if (!gate_parsed &&
        ((tokens.size() >= 5 && tokens[2] == "->") || (tokens.size() >= 7 && tokens[3] == "->"))) {
      report.warnings.push_back("Skipped invalid gate line: " + line);
    }
  }

  /* 为只有安全区定义但没有 MapInfo 的地图补全空地图信息 */
  for (const auto& [map_id, points] : start_points) {
    if (!map_infos.contains(map_id)) {
      ImportedMapInfo info;
      info.id = map_id;
      info.title = map_id;
      map_infos[map_id] = std::move(info);
      map_order.push_back(map_id);
    }
  }

  /* 为每个地图生成 TOML 配置文件 */
  for (const auto& map_id : map_order) {
    const auto& info = map_infos.at(map_id);
    const auto point = start_points.contains(map_id) && !start_points[map_id].empty()
                           ? start_points[map_id].front()
                           : std::pair{home_x, home_y};
    std::ofstream file(output_root / "maps" / (map_id + ".toml"), std::ios::binary | std::ios::trunc);
    file << "id = " << quote(map_id) << "\n";
    file << "title = " << quote(info.title.empty() ? map_id : info.title) << "\n";
    file << "source_map = " << quote((legacy_root / "Map" / (map_id + ".map")).string()) << "\n";
    file << "width = 0\n";
    file << "height = 0\n";
    file << "home_x = " << point.first << "\n";
    file << "home_y = " << point.second << "\n";
    file << "law_full = " << (info.law_full ? "true" : "false") << "\n";
    file << "fight_zone = " << (info.fight_zone ? "true" : "false") << "\n";
    file << "fight3_zone = " << (info.fight3_zone ? "true" : "false") << "\n";
    file << "daylight = " << (info.daylight ? "true" : "false") << "\n";
    file << "darkness = " << (info.darkness ? "true" : "false") << "\n";
    file << "no_reconnect = " << (info.no_reconnect ? "true" : "false") << "\n";
    file << "need_hole = " << (info.need_hole ? "true" : "false") << "\n";
    file << "no_recall = " << (info.no_recall ? "true" : "false") << "\n";
    file << "no_random_move = " << (info.no_random_move ? "true" : "false") << "\n";
    file << "no_drug = " << (info.no_drug ? "true" : "false") << "\n";
    file << "no_position_move = " << (info.no_position_move ? "true" : "false") << "\n";
    file << "need_level = " << info.need_level << "\n";
    file << "mine_map = " << info.mine_map << "\n";
    file << "back_map = " << quote(info.back_map) << "\n";
    file << "quiz_zone = " << (info.quiz_zone ? "true" : "false") << "\n";
    file << "need_set_number = " << info.need_set_number << "\n";
    file << "need_set_value = " << info.need_set_value << "\n";
    if (!info.check_quest.empty()) {
      file << "check_quest = "
           << quote(import_mapquest_script_asset(legacy_root, output_root, info.check_quest))
           << "\n";
    }
    /* 安全区地图禁止PK */
    file << "allow_pk = " << (info.law_full ? "false" : "true") << "\n";
    if (map_id == home_map && setup_home_x > 0 && setup_home_y > 0) {
      file << "badman_zones = [\n";
      file << "  { x = " << (setup_home_x - 10) << ", y = " << (setup_home_y - 10)
           << ", width = 21, height = 21 }\n";
      file << "]\n";
    }
    const auto point_it = start_points.find(map_id);
    if (point_it != start_points.end() && !point_it->second.empty()) {
      file << "safe_zones = [\n";
      for (std::size_t index = 0; index < point_it->second.size(); ++index) {
        const auto x = parse_int32(point_it->second[index].first).value_or(0);
        const auto y = parse_int32(point_it->second[index].second).value_or(0);
        file << "  { x = " << (x - 10) << ", y = " << (y - 10)
             << ", width = 21, height = 21 }";
        file << (index + 1 < point_it->second.size() ? ",\n" : "\n");
      }
      file << "]\n";
    }
    if (!info.gates.empty()) {
      file << "gates = [\n";
      for (std::size_t index = 0; index < info.gates.size(); ++index) {
        const auto& gate = info.gates[index];
        file << "  { x = " << gate.x << ", y = " << gate.y
             << ", target_map_id = " << quote(gate.target_map_id)
             << ", target_x = " << gate.target_x << ", target_y = " << gate.target_y
             << " }";
        file << (index + 1 < info.gates.size() ? ",\n" : "\n");
      }
      file << "]\n";
    }
    ++report.map_count;
  }

  /* 如果没有导入任何地图，创建一个使用默认出生点的基本地图 */
  if (report.map_count == 0) {
    std::ofstream file(output_root / "maps" / (home_map + ".toml"), std::ios::binary | std::ios::trunc);
    file << "id = " << quote(home_map) << "\n";
    file << "title = " << quote("ImportedHome") << "\n";
    file << "source_map = " << quote((legacy_root / "Map" / (home_map + ".map")).string()) << "\n";
    file << "width = 0\n";
    file << "height = 0\n";
    file << "home_x = " << home_x << "\n";
    file << "home_y = " << home_y << "\n";
    if (setup_home_x > 0 && setup_home_y > 0) {
      file << "badman_zones = [\n";
      file << "  { x = " << (setup_home_x - 10) << ", y = " << (setup_home_y - 10)
           << ", width = 21, height = 21 }\n";
      file << "]\n";
    }
    report.map_count = 1;
  }

  /* 解析 MonGen.txt / MonZen.txt 生成刷怪配置 */
  std::ofstream spawns(output_root / "spawns" / "imported_monsters.toml",
                       std::ios::binary | std::ios::trunc);
  spawns << "spawns = [\n";
  bool first = true;
  auto known_monster_names = load_imported_monster_names(output_root);
  const auto drop_monster_names = load_legacy_monitem_monster_names(legacy_root);
  known_monster_names.insert(drop_monster_names.begin(), drop_monster_names.end());
  const auto mongen_path = first_existing_path({
      legacy_root / "Envir" / "MonGen.txt",
      legacy_root / "Envir" / "mongen.txt",
      legacy_root / "Envir" / "MONGEN.TXT",
      legacy_root / "Envir" / "MonZen.txt",
      legacy_root / "Envir" / "monzen.txt",
      legacy_root / "Envir" / "MONZEN.TXT",
  });
  for (const auto& line : read_lines(mongen_path)) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    const auto record = parse_mongen_record(line, known_monster_names);
    if (!record.has_value()) {
      continue;
    }
    if (!first) {
      spawns << ",\n";
    }
    first = false;
    spawns << "  { map_id = " << quote(record->map_id)
           << ", actor_type = \"monster\", name = " << quote(record->name)
           << ", x = " << record->x << ", y = " << record->y
           << ", area = " << record->area << ", count = " << record->count
           << ", zen_minutes = " << record->zen_minutes
           << ", small_zen_rate = " << record->small_zen_rate
           << ", legacy_group = true }";
    ++report.spawn_count;
  }
  spawns << "\n]\n";
  return report;
}

/**
 * @brief 在旧版服务端目录中查找 NPC 脚本文件
 * @param legacy_root 旧版服务端根目录
 * @param npc_id NPC 标识符
 * @param map_id 所在地图ID
 * @return std::filesystem::path 找到的脚本文件路径，未找到时返回空路径
 * @details 按以下优先级搜索脚本文件：
 *          1. Envir/market_def/{npc_id}-{map_id}.txt
 *          2. Envir/market_def/{npc_id}.txt
 *          3. Envir/Npc_def/{npc_id}-{map_id}.txt
 *          4. Envir/Npc_def/{npc_id}.txt
 *          每种路径同时尝试 .txt 和 .TXT 扩展名变体。
 */
std::filesystem::path find_legacy_npc_script(const std::filesystem::path& legacy_root,
                                             std::string_view npc_id,
                                             std::string_view map_id) {
  const auto base_name = std::string(npc_id);
  const auto map_name = std::string(map_id);
  const std::vector<std::filesystem::path> directories = {
      legacy_root / "Envir" / "market_def",
      legacy_root / "Envir" / "Npc_def",
  };
  const std::vector<std::string> candidates = {
      base_name + "-" + map_name + ".txt",
      base_name + "-" + map_name + ".TXT",
      base_name + ".txt",
      base_name + ".TXT",
  };

  for (const auto& directory : directories) {
    for (const auto& candidate : candidates) {
      const auto path = directory / util::path_from_utf8(candidate);
      if (std::filesystem::exists(path)) {
        return path;
      }
    }
  }
  return {};
}

/**
 * @brief 导入单个 NPC 脚本文件到新系统
 * @param legacy_root 旧版服务端根目录
 * @param output_root 新系统输出根目录
 * @param npc_id NPC 标识符
 * @param map_id 所在地图ID
 * @return std::string 脚本在新系统中的相对路径
 * @details 查找 NPC 脚本文件并复制到新系统的 npc_scripts 目录。
 *          目标文件名格式：{npc_id}-{map_id}.txt。
 *          如果找不到脚本文件，返回一个基于 NPC ID 的默认文件名。
 *          源目录名为 "market_def" 的脚本复制到 npc_scripts/market_def，
 *          否则复制到 npc_scripts/Npc_def。
 * @note 文件名经过 ASCII 安全化处理以兼容跨平台文件系统。
 */
std::string import_npc_script_asset(const std::filesystem::path& legacy_root,
                                    const std::filesystem::path& output_root, std::string_view npc_id,
                                    std::string_view map_id) {
  try {
    const auto source = find_legacy_npc_script(legacy_root, npc_id, map_id);
    if (source.empty()) {
      return util::ascii_path_component(std::string(npc_id) + ".txt");
    }

    const auto source_dir = util::lower_copy(source.parent_path().filename().string());
    const auto subdir = source_dir == "market_def" ? std::filesystem::path("market_def")
                                                   : std::filesystem::path("Npc_def");
    const auto target_name = util::ascii_path_component(
        std::string(npc_id) + "-" + std::string(map_id) +
        util::path_to_utf8_string(source.extension()));
    const auto relative = std::filesystem::path("npc_scripts") / subdir / target_name;
    std::filesystem::copy_file(source, output_root / relative,
                               std::filesystem::copy_options::overwrite_existing);
    return relative.generic_string();
  } catch (const std::exception&) {
    return util::ascii_path_component(std::string(npc_id) + ".txt");
  }
}

/**
 * @brief 导入 NPC 定义
 * @param legacy_root 旧版服务端根目录
 * @param output_root 新系统输出根目录
 * @return std::size_t 导入的 NPC 数量
 * @details 从两个源文件导入 NPC：
 *          1. Envir/merchant.txt：商人 NPC（提供交易、修理等服务）
 *          2. Envir/Npcs.txt：功能 NPC（如行会管理员、仓库管理员等）
 *
 *          merchant.txt 格式：ID 地图 X Y 名称 [其他字段...]
 *          Npcs.txt 格式：ID 名称 地图 X Y [其他字段...]
 *
 *          服务类型分类（基于名称关键字识别）：
 *          - guild_castle：行会/城堡相关
 *          - storage：仓库管理员
 *          - sell_repair：买卖/修理（默认）
 * @note 输出文件为 npcs/imported_npcs.toml，包含所有 NPC 的完整配置。
 */
std::size_t import_npcs(const std::filesystem::path& legacy_root, const std::filesystem::path& output_root) {
  std::ofstream npcs(output_root / "npcs" / "imported_npcs.toml", std::ios::binary | std::ios::trunc);
  npcs << "npcs = [\n";
  bool first = true;
  std::size_t count = 0;

  /**
   * @brief 根据 NPC 的 ID 和名称自动分类服务类型
   * @param id NPC 标识符
   * @param name NPC 显示名称
   * @return std::string 服务类型（guild_castle / storage / sell_repair）
   */
  auto classify_service = [](std::string_view id, std::string_view name) {
      const auto haystack = util::lower_copy(std::string(id) + " " + std::string(name));
      if (haystack.find("guild") != std::string::npos || haystack.find("castle") != std::string::npos ||
          haystack.find("sabuk") != std::string::npos || haystack.find("chamberlain") != std::string::npos) {
        return std::string("guild_castle");
      }
      if (haystack.find("storage") != std::string::npos || haystack.find("warehouse") != std::string::npos ||
          haystack.find("keeper") != std::string::npos || haystack.find("depot") != std::string::npos) {
        return std::string("storage");
      }
    return std::string("sell_repair");
  };

  /** @brief 追加单条 NPC 记录到 TOML 文件 */
  auto append_npc = [&](const std::string& id, const std::string& map_id, const std::string& name,
                        const std::string& x, const std::string& y, const std::string& script,
                        const std::string& service) {
    if (!first) {
      npcs << ",\n";
    }
    first = false;
    npcs << "  { id = " << quote(id) << ", map_id = " << quote(map_id) << ", name = "
         << quote(name) << ", x = " << x << ", y = " << y << ", script = " << quote(script)
         << ", service = " << quote(service) << " }";
    ++count;
  };

  /* 处理 merchant.txt（商人 NPC） */
  for (const auto& line : read_lines(legacy_root / "Envir" / "merchant.txt")) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    const auto tokens = split_ws(line);
    if (tokens.size() >= 5) {
      append_npc(tokens[0], tokens[1], tokens[4], tokens[2], tokens[3],
                 import_npc_script_asset(legacy_root, output_root, tokens[0], tokens[1]),
                 classify_service(tokens[0], tokens[4]));
    }
  }

  /* 处理 Npcs.txt（功能 NPC） */
  for (const auto& line : read_lines(legacy_root / "Envir" / "Npcs.txt")) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    const auto tokens = split_ws(line);
    if (tokens.size() >= 7) {
      append_npc(tokens[0], tokens[2], tokens[0], tokens[3], tokens[4],
                 import_npc_script_asset(legacy_root, output_root, tokens[0], tokens[2]),
                 "none");
    }
  }

  npcs << "\n]\n";
  return count;
}

/**
 * @brief 导入地图任务脚本文件
 * @param legacy_root 旧版服务端根目录
 * @param output_root 新系统输出根目录
 * @param qfile 任务文件名（来自 MapQuest.txt 中的 qfile 字段）
 * @return std::string 任务脚本在新系统中的相对路径
 * @details 在旧版 Envir/MapQuest_def 目录中查找对应的任务脚本文件，
 *          复制到新系统的 npc_scripts/MapQuest_def 目录。
 *          如果文件不存在，仅返回一个安全的默认文件名（不创建拷贝）。
 */
std::string import_mapquest_script_asset(const std::filesystem::path& legacy_root,
                                         const std::filesystem::path& output_root,
                                         std::string_view qfile) {
  const auto name = util::path_to_utf8_string(util::path_from_utf8(qfile).filename());
  const auto safe_name = util::ascii_path_component(name);
  if (name.empty()) {
    return {};
  }
  const std::vector<std::filesystem::path> candidates = {
      legacy_root / "Envir" / "MapQuest_def" / util::path_from_utf8(name),
      legacy_root / "Envir" / "MapQuest_def" / util::path_from_utf8(name + ".txt"),
      legacy_root / "Envir" / "MapQuest_def" / util::path_from_utf8(name + ".TXT"),
  };
  for (const auto& source : candidates) {
    if (!std::filesystem::exists(source)) {
      continue;
    }
    const auto target_name = util::ascii_path_component(util::path_to_utf8_string(source.filename()));
    const auto target = std::filesystem::path("npc_scripts") / "MapQuest_def" /
                        target_name;
    try {
      std::filesystem::copy_file(source, output_root / target,
                                 std::filesystem::copy_options::overwrite_existing);
      return target.generic_string();
    } catch (const std::exception&) {
      return safe_name;
    }
  }
  return safe_name;
}

/**
 * @brief 导入地图任务配置（MapQuest.txt）
 * @param legacy_root 旧版服务端根目录
 * @param output_root 新系统输出根目录
 * @return std::size_t 导入的地图任务数量
 * @details 解析 MapQuest.txt，每行包含以下字段：
 *          地图ID [任务编号] 值 怪物名 物品名 任务脚本 [group]
 *
 *          其中：
 *          - 任务编号可能被方括号包裹（如 [123]）
 *          - 怪物名和物品名的 "*" 表示不限制
 *          - group 标记表示该任务启用组队完成模式
 * @note 输出文件为 map_quests/imported_map_quests.toml。
 */
std::size_t import_map_quests(const std::filesystem::path& legacy_root,
                              const std::filesystem::path& output_root) {
  const auto path = first_existing_path({
      legacy_root / "Envir" / "MapQuest.txt",
      legacy_root / "Envir" / "mapquest.txt",
      legacy_root / "Envir" / "MAPQUEST.TXT",
  });
  std::ofstream file(output_root / "map_quests" / "imported_map_quests.toml",
                     std::ios::binary | std::ios::trunc);
  file << "map_quests = [\n";
  bool first = true;
  std::size_t count = 0;
  for (const auto& line : read_lines(path)) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    const auto tokens = split_legacy_fields(line);
    if (tokens.size() < 6) {
      continue;
    }
    auto set_number_text = tokens[1];
    /* 移除任务编号的方括号包裹 */
    if (set_number_text.size() >= 2 && set_number_text.front() == '[' &&
        set_number_text.back() == ']') {
      set_number_text = set_number_text.substr(1, set_number_text.size() - 2);
    }
    const auto set_number = parse_int32(set_number_text);
    const auto value = parse_int32(tokens[2]);
    if (!set_number.has_value() || !value.has_value()) {
      continue;
    }
    if (!first) {
      file << ",\n";
    }
    first = false;
    const auto qfile = import_mapquest_script_asset(legacy_root, output_root, tokens[5]);
    const auto group = tokens.size() > 6 && util::lower_copy(tokens[6]) == "group";
    /* 限制任务值范围为 0-1（开启/关闭） */
    const auto clamped_value = std::clamp(*value, 0, 1);
    file << "  { map_id = " << quote(tokens[0])
         << ", set_number = " << *set_number
         << ", value = " << clamped_value
         << ", monster_name = " << quote(tokens[3] == "*" ? std::string{} : tokens[3])
         << ", item_name = " << quote(tokens[4] == "*" ? std::string{} : tokens[4])
         << ", qfile = " << quote(qfile)
         << ", enable_group = " << (group ? "true" : "false") << " }";
    ++count;
  }
  file << "\n]\n";
  return count;
}

/**
 * @brief 从 MakeItem.txt 导入物品定义
 * @param legacy_root 旧版服务端根目录
 * @param output_root 新系统输出根目录
 * @return std::size_t 导入的物品数量
 * @details MakeItem.txt 中每行包含一个物品名称（可能由多个 token 组成）。
 *          为每个物品生成一个基本的 TOML 配置条目，包含自动生成的 ID、
 *          默认属性值和使用 Count 作为外观索引。
 * @note 如果 MakeItem.txt 为空，会创建一个默认的 "Wooden Sword" 木剑物品作为占位。
 *       输出文件为 items/imported_items.toml。
 */
std::size_t import_items_from_makeitem(const std::filesystem::path& legacy_root,
                                       const std::filesystem::path& output_root) {
  std::ofstream items(output_root / "items" / "imported_items.toml", std::ios::binary | std::ios::trunc);
  items << "items = [\n";
  bool first = true;
  std::size_t id = 1;
  std::size_t count = 0;

  const auto makeitem_path = legacy_makeitem_path(legacy_root);
  for (const auto& line : read_lines(makeitem_path)) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    const auto tokens = split_legacy_fields(line);
    if (tokens.empty()) {
      continue;
    }
    const auto item_name = join_tokens(tokens, 0, tokens.size());
    if (item_name.empty()) {
      continue;
    }
    if (!first) {
      items << ",\n";
    }
    first = false;
    items << "  { id = " << id++ << ", name = " << quote(item_name)
          << ", weight = 1, price = 0, std_mode = 0, shape = 0, looks = " << count + 1
          << ", dura_max = 1000, equip_slot = -1, hp_add = 0, mp_add = 0 }";
    ++count;
  }

  if (count == 0) {
    items << "  { id = 1, name = \"Wooden Sword\", weight = 3, price = 50, std_mode = 5,"
          << " shape = 1, looks = 1, dura_max = 1000, equip_slot = 1, hp_add = 0,"
          << " mp_add = 0 }";
    count = 1;
  }
  items << "\n]\n";
  return count;
}

/**
 * @brief 导入怪物掉落配置（MonItems 目录）
 * @param legacy_root 旧版服务端根目录
 * @param output_root 新系统输出根目录
 * @return std::size_t 导入的掉落配置条目数量
 * @details 遍历 Envir/MonItems 目录下的所有 .txt 文件（每个文件对应一种怪物），
 *          解析其内部的掉落规则。每行格式支持两种：
 *
 *          格式一（推荐）：sel/max 物品名 [数量]
 *          格式二：sel max 物品名 [数量]
 *
 *          其中 sel 为选择点数，max 为最大点数（共同决定掉落概率），
 *          可选的数量字段表示一次掉落几个。
 *
 *          物品名称边界智能识别：如果最后一个 token 是数字且剥离后
 *          的剩余部分在已知物品列表中，则将数字识别为数量字段。
 * @note 输出文件为 monsters/imported_drops.toml。
 */
std::size_t import_monitems(const std::filesystem::path& legacy_root,
                            const std::filesystem::path& output_root) {
  const auto directory = legacy_root / "Envir" / "MonItems";
  std::ofstream drops(output_root / "monsters" / "imported_drops.toml",
                      std::ios::binary | std::ios::trunc);
  drops << "monster_drops = [\n";
  bool first = true;
  std::size_t count = 0;
  auto known_item_names = load_imported_item_names(output_root);
  const auto makeitem_names = load_legacy_makeitem_names(legacy_root);
  known_item_names.insert(makeitem_names.begin(), makeitem_names.end());
  if (!std::filesystem::exists(directory)) {
    drops << "\n]\n";
    return 0;
  }

  /* 收集并排序 MonItems 目录中的所有掉落配置文件 */
  std::vector<std::filesystem::path> monitem_files;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() ||
        util::lower_copy(entry.path().extension().string()) != ".txt") {
      continue;
    }
    monitem_files.push_back(entry.path());
  }
  std::sort(monitem_files.begin(), monitem_files.end(),
            [](const auto& lhs, const auto& rhs) {
              return lhs.generic_string() < rhs.generic_string();
            });

  for (const auto& path : monitem_files) {
    const auto monster_name = path.stem().string();
    for (const auto& line : read_lines(path)) {
      if (line.empty() || util::starts_with(line, ";")) {
        continue;
      }
      const auto tokens = split_legacy_fields(line);
      if (tokens.size() < 2) {
        continue;
      }
      std::optional<std::int32_t> sel;
      std::optional<std::int32_t> max;
      std::size_t item_start = 1;
      /* 解析选择点数/最大点数字段（支持 "sel/max" 或 "sel max" 两种格式） */
      if (const auto slash = tokens[0].find('/'); slash != std::string::npos) {
        sel = parse_int32(tokens[0].substr(0, slash));
        max = parse_int32(tokens[0].substr(slash + 1));
      } else if (tokens.size() >= 3) {
        sel = parse_int32(tokens[0]);
        max = parse_int32(tokens[1]);
        item_start = 2;
      }
      if (!sel.has_value() || !max.has_value() || item_start >= tokens.size()) {
        continue;
      }
      auto item_end = tokens.size();
      auto count_text = std::string("1");
      /* 智能识别末尾的数量字段 */
      if (tokens.size() > item_start + 1) {
        if (const auto count_value = parse_int32(tokens.back()); count_value.has_value()) {
          const auto full_name = join_tokens(tokens, item_start, tokens.size());
          const auto count_candidate_name = join_tokens(tokens, item_start, tokens.size() - 1);
          const auto full_key = legacy_name_key(full_name);
          const auto count_candidate_key = legacy_name_key(count_candidate_name);
          const auto full_known = known_item_names.find(full_key) != known_item_names.end();
          const auto count_candidate_known =
              known_item_names.find(count_candidate_key) != known_item_names.end() ||
              count_candidate_key == "gold";
          if (!full_known && count_candidate_known) {
            count_text = tokens.back();
            item_end = tokens.size() - 1;
          }
        }
      }
      const auto item_name = join_tokens(tokens, item_start, item_end);
      if (item_name.empty()) {
        continue;
      }
      if (!first) {
        drops << ",\n";
      }
      first = false;
      /* sel_point 存储为 0-based 值以符合新系统格式 */
      drops << "  { monster_name = " << quote(monster_name)
            << ", sel_point = " << (*sel - 1)
            << ", max_point = " << *max
            << ", item_name = " << quote(item_name)
            << ", count = " << count_text << " }";
      ++count;
    }
  }
  drops << "\n]\n";
  return count;
}

#ifdef MIR2_ENABLE_ODBC
/**
 * @brief 通过 ODBC 从 Access 数据库（.mdb）导入表数据并输出为 TOML 格式
 * @param database_path .mdb 数据库文件路径
 * @param file_path 输出 TOML 文件路径
 * @param query SQL 查询语句
 * @param array_name TOML 顶层数组名称
 * @param field_names SQL 查询返回的字段名列表
 * @param toml_field_names TOML 输出对应的字段名列表
 * @param warnings 警告信息列表引用
 * @return std::size_t 导入的记录数量
 * @details 使用 ODBC 3.0 驱动连接 Microsoft Access 数据库，
 *          执行 SQL 查询并将结果逐行写入 TOML 格式文件。
 *          字符串类型的字段名（以 _name 结尾或特定字段）使用引号包裹，
 *          数值类型直接输出。
 * @note 仅在编译时启用 MIR2_ENABLE_ODBC 宏时可用。
 *       连接字符串使用 Microsoft Access Driver (*.mdb, *.accdb)。
 * @warning 需要 Windows 平台和已安装的 Access ODBC 驱动。
 */
std::size_t import_table_as_toml(const std::filesystem::path& database_path, const std::filesystem::path& file_path,
                                 const std::string& query, const std::string& array_name,
                                 const std::vector<std::string>& field_names,
                                 const std::vector<std::string>& toml_field_names,
                                 std::vector<std::string>& warnings) {
  SQLHENV environment = SQL_NULL_HENV;
  SQLHDBC connection = SQL_NULL_HDBC;
  SQLHSTMT statement = SQL_NULL_HSTMT;
  std::size_t count = 0;

  if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &environment) != SQL_SUCCESS) {
    warnings.push_back("ODBC environment allocation failed.");
    return 0;
  }
  SQLSetEnvAttr(environment, SQL_ATTR_ODBC_VERSION, reinterpret_cast<void*>(SQL_OV_ODBC3), 0);
  SQLAllocHandle(SQL_HANDLE_DBC, environment, &connection);

  const auto connection_string =
      "Driver={Microsoft Access Driver (*.mdb, *.accdb)};DBQ=" + database_path.string() + ";";
  SQLCHAR out_connection[1024];
  SQLSMALLINT out_length = 0;
  if (SQLDriverConnect(connection, nullptr,
                       reinterpret_cast<SQLCHAR*>(const_cast<char*>(connection_string.c_str())),
                       SQL_NTS, out_connection, sizeof(out_connection), &out_length,
                       SQL_DRIVER_COMPLETE) != SQL_SUCCESS) {
    warnings.push_back("ODBC connection to Data.mdb failed, static DB import skipped.");
    SQLFreeHandle(SQL_HANDLE_DBC, connection);
    SQLFreeHandle(SQL_HANDLE_ENV, environment);
    return 0;
  }

  SQLAllocHandle(SQL_HANDLE_STMT, connection, &statement);
  if (SQLExecDirect(statement, reinterpret_cast<SQLCHAR*>(const_cast<char*>(query.c_str())), SQL_NTS) !=
      SQL_SUCCESS) {
    warnings.push_back("ODBC query failed: " + query);
    SQLFreeHandle(SQL_HANDLE_STMT, statement);
    SQLDisconnect(connection);
    SQLFreeHandle(SQL_HANDLE_DBC, connection);
    SQLFreeHandle(SQL_HANDLE_ENV, environment);
    return 0;
  }

  std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
  file << array_name << " = [\n";
  bool first = true;

  while (SQLFetch(statement) == SQL_SUCCESS) {
    if (!first) {
      file << ",\n";
    }
    first = false;
    file << "  { ";
    for (std::size_t index = 0; index < field_names.size(); ++index) {
      char buffer[512] = {};
      SQLLEN indicator = 0;
      SQLGetData(statement, static_cast<SQLUSMALLINT>(index + 1), SQL_C_CHAR, buffer,
                 sizeof(buffer), &indicator);
      const auto value =
          indicator == SQL_NULL_DATA ? std::string{} : std::string(buffer);
      if (index > 0) {
        file << ", ";
      }
      file << toml_field_names[index] << " = ";
      const auto field_name = util::lower_copy(toml_field_names[index]);
      /* 根据字段名判断是否需要引号包裹 */
      if (field_name == "name" || field_name == "item_name" ||
          field_name == "monster_name" || field_name.find("_name") != std::string::npos) {
        file << quote(value);
      } else {
        file << (value.empty() ? "0" : value);
      }
    }
    file << " }";
    ++count;
  }
  file << "\n]\n";

  SQLFreeHandle(SQL_HANDLE_STMT, statement);
  SQLDisconnect(connection);
  SQLFreeHandle(SQL_HANDLE_DBC, connection);
  SQLFreeHandle(SQL_HANDLE_ENV, environment);
  return count;
}
#endif

/**
 * @brief 写入导入报告 Markdown 文档
 * @param output_root 新系统输出根目录
 * @param report 导入结果报告
 * @details 生成 docs/legacy_import_report.md 文件，汇总所有导入资源的统计数据。
 *          如果导入过程中有警告，会一并列出。
 */
void write_report(const std::filesystem::path& output_root, const LegacyImportReport& report) {
  std::ofstream file(output_root / ".." / "docs" / "legacy_import_report.md",
                     std::ios::binary | std::ios::trunc);
  file << "# Legacy Import Report\n\n";
  file << "- maps: " << report.map_count << "\n";
  file << "- spawns: " << report.spawn_count << "\n";
  file << "- monsters: " << report.monster_count << "\n";
  file << "- monster drops: " << report.monster_drop_count << "\n";
  file << "- npcs: " << report.npc_count << "\n";
  file << "- map quests: " << report.map_quest_count << "\n";
  file << "- items: " << report.item_count << "\n";
  file << "- magic: " << report.magic_count << "\n";
  if (!report.warnings.empty()) {
    file << "\n## Warnings\n";
    for (const auto& warning : report.warnings) {
      file << "- " << warning << "\n";
    }
  }
}

/**
 * @brief 写入后备怪物定义（当 ODBC 导入失败时使用）
 * @param legacy_root 旧版服务端根目录
 * @param output_root 新系统输出根目录
 * @return std::size_t 写入的怪物定义数量
 * @details 当 ODBC 怪物导入返回 0 行时，使用此后备方案：
 *          1. 始终包含 "Deer"（鹿）和 "Oma"（奥马）两个基础怪物
 *          2. 添加 MonItems 目录中所有有掉落配置的怪物名
 *          3. 不包含具体属性值，仅定义名称占位
 */
std::size_t write_fallback_monster_defs(const std::filesystem::path& legacy_root,
                                        const std::filesystem::path& output_root) {
  std::vector<std::string> names{"Deer", "Oma"};
  std::set<std::string> seen;
  for (const auto& name : names) {
    seen.insert(legacy_name_key(name));
  }
  for (const auto& name : load_legacy_monitem_monster_name_values(legacy_root)) {
    if (seen.insert(legacy_name_key(name)).second) {
      names.push_back(name);
    }
  }

  std::ofstream monsters(output_root / "monsters" / "imported_monsters.toml",
                         std::ios::binary | std::ios::trunc);
  monsters << "monsters = [\n";
  for (std::size_t index = 0; index < names.size(); ++index) {
    if (index > 0) {
      monsters << ",\n";
    }
    monsters << "  { name = " << quote(names[index]) << " }";
  }
  monsters << "\n]\n";
  return names.size();
}

}  // namespace

LegacyImportReport LegacyImporter::import_tree(const std::filesystem::path& legacy_root,
                                               const std::filesystem::path& output_root) const {
  /* 创建输出目录结构 */
  ensure_output_dirs(output_root);
  /* 解析旧版 !SetUp.txt 配置 */
  const auto setup = parse_ini(legacy_root / "!SetUp.txt");

  /* 写入服务端基础配置文件 */
  write_server_files(setup, output_root);
  /* 复制 NPC 脚本目录 */
  copy_legacy_script_tree(legacy_root, output_root, "Defines", "Defines");
  copy_legacy_script_tree(legacy_root, output_root, "QuestDiary", "QuestDiary");
  LegacyImportReport report;
  /* 导入 NPC、地图任务和物品定义 */
  report.npc_count = import_npcs(legacy_root, output_root);
  report.map_quest_count = import_map_quests(legacy_root, output_root);
  report.item_count = import_items_from_makeitem(legacy_root, output_root);

#ifdef MIR2_ENABLE_ODBC
  /* 通过 ODBC 从 Data.mdb 导入物品定义 */
  const auto odbc_item_count = import_table_as_toml(
      legacy_root / "Data.mdb", output_root / "items" / "imported_items.toml",
      "SELECT Idx, Name, StdMode, Shape, ImgIndex, DuraMax, Weight, Need, NeedLevel, "
      "Price, Stock, AtkSpd, Agility, Accurate, MgAvoid, Strong, Undead, HPADD, MPADD, "
      "ExpAdd, EffType1, EffRate1, EffValue1, EffType2, EffRate2, EffValue2 FROM StdItems",
      "items",
      {"Idx", "Name", "StdMode", "Shape", "ImgIndex", "DuraMax", "Weight", "Need",
       "NeedLevel", "Price", "Stock", "AtkSpd", "Agility", "Accurate", "MgAvoid",
       "Strong", "Undead", "HPADD", "MPADD", "ExpAdd", "EffType1", "EffRate1",
       "EffValue1", "EffType2", "EffRate2", "EffValue2"},
      {"id", "name", "std_mode", "shape", "looks", "dura_max", "weight", "need",
       "need_level", "price", "stock", "atk_spd", "agility", "accurate", "mg_avoid",
       "strong", "undead", "hp_add", "mp_add", "exp_add", "eff_type1", "eff_rate1",
       "eff_value1", "eff_type2", "eff_rate2", "eff_value2"},
      report.warnings);
  if (odbc_item_count > 0) {
    report.item_count = odbc_item_count;
  } else {
    /* ODBC 导入失败时使用 MakeItem.txt 后备方案 */
    report.item_count = import_items_from_makeitem(legacy_root, output_root);
    report.warnings.push_back(
        "ODBC StdItems import returned no rows, MakeItem.txt fallback items were used.");
  }
  /* 通过 ODBC 导入魔法定义 */
  report.magic_count = import_table_as_toml(legacy_root / "Data.mdb", output_root / "magic" / "imported_magic.toml",
                                            "SELECT ID, Name, DefSpell, DefPower FROM Magic", "magic",
                                            {"ID", "Name", "DefSpell", "DefPower"},
                                            {"id", "name", "mp_cost", "power"}, report.warnings);
  /* 通过 ODBC 导入怪物定义 */
  report.monster_count = import_table_as_toml(
      legacy_root / "Data.mdb", output_root / "monsters" / "imported_monsters.toml",
      "SELECT Name, Race, RaceImg, ImgIndex, Lv, Undead, CoolEye, Exp, HP, MP, AC, MAC, DC, DCMAX, MC, SC, AGILITY, ACCURATE, WALK_SPD, WalkStep, WalkWait, ATTACK_SPD FROM Monster",
      "monsters",
      {"Name", "Race", "RaceImg", "ImgIndex", "Lv", "Undead", "CoolEye", "Exp", "HP", "MP",
       "AC", "MAC", "DC", "DCMAX", "MC", "SC", "AGILITY", "ACCURATE", "WALK_SPD",
       "WalkStep", "WalkWait", "ATTACK_SPD"},
      {"name", "race_server", "race_image", "appearance", "level", "undead", "cool_eye",
       "exp", "hp", "mp", "ac", "mac", "dc", "dc_max", "mc", "sc", "agility",
       "accurate", "walk_speed_ms", "walk_step", "walk_wait_ms", "attack_speed_ms"},
      report.warnings);
  if (report.monster_count == 0) {
    /* ODBC 怪物导入失败时使用 MonItems 怪物名作为占位定义 */
    report.monster_count = write_fallback_monster_defs(legacy_root, output_root);
    report.warnings.push_back(
        "ODBC Monster import returned no rows, MonItems monster names were used as placeholder monster definitions.");
  }
#else
  /* 未启用 ODBC 时的后备方案 */
  {
    std::ofstream monsters(output_root / "monsters" / "imported_monsters.toml",
                           std::ios::binary | std::ios::trunc);
    monsters << "monsters = [\n"
             << "  { name = \"Deer\", race_server = 0, level = 1, hp = 8, dc = 1, exp = 5, "
                "ai_profile = \"passive_animal\" },\n"
             << "  { name = \"Oma\", race_server = 81, level = 3, hp = 20, dc = 4, exp = 20, "
                "ai_profile = \"aggressive\" }\n"
             << "]\n";
    report.monster_count = 2;
  }
  {
    std::ofstream magic(output_root / "magic" / "imported_magic.toml",
                        std::ios::binary | std::ios::trunc);
    magic << "magic = [\n  { id = 1, name = \"Basic Fireball\", mp_cost = 4, power = 8 }\n]\n";
    report.magic_count = 1;
  }
  report.warnings.push_back("ODBC support was not enabled, Data.mdb import used placeholder magic data.");
#endif

  /* 导入地图和刷怪配置 */
  auto map_report = import_maps_and_spawns(legacy_root, output_root, setup);
  report.map_count = map_report.map_count;
  report.spawn_count = map_report.spawn_count;
  report.warnings.insert(report.warnings.end(), map_report.warnings.begin(),
                         map_report.warnings.end());
  /* 导入怪物掉落配置 */
  report.monster_drop_count = import_monitems(legacy_root, output_root);

  /* 写入最终导入报告 */
  write_report(output_root, report);
  return report;
}

}  // namespace mir2
