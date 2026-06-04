/**
 * @file config_loader.cpp
 * @brief 配置加载器实现
 * @details 本文件实现了 ModernServer 的完整配置加载流水线，是整个服务端
 *          启动时最先执行的模块之一。其核心流程如下：
 *
 *          配置加载流水线（ConfigLoader::load）：
 *          1. 解析 server.toml —— 读取运行时参数（日志、目录、城堡/公会文本模板等）
 *          2. 解析 ports.toml —— 读取四个网关（传统登录/游戏、v1 登录/游戏）的地址和端口
 *          3. 解析 runtime/logic.toml —— 读取各子系统的时间预算配置
 *          4. load_maps()     —— 遍历 maps/ 目录，解析每个地图 TOML 文件
 *          5. load_monsters() —— 遍历 monsters/ 目录，解析怪物定义和掉落表
 *          6. load_spawns()   —— 遍历 spawns/ 目录，解析刷怪配置
 *          7. load_items()    —— 遍历 items/ 目录，解析物品定义
 *          8. load_magics()   —— 遍历 magic/ 目录，解析魔法定义
 *          9. load_npcs()     —— 遍历 npcs/ 目录，解析 NPC 配置并加载遗留脚本
 *          10. load_map_quests() —— 遍历 map_quests/ 目录，解析地图任务
 *          11. load_startup_quest() —— 加载启动任务脚本
 *
 *          遗留 NPC 脚本处理（重点）：
 *          - 预处理器 preprocess_legacy_script_lines() 处理 #DEFINE、#INCLUDE、
 *            #CALL、#SETHOME 等指令，实现宏替换和脚本包含
 *          - 解析器 parse_legacy_npc_script() 将预处理后的文本行转换为结构化的
 *            NpcDialogSectionConfig 列表，同时提取商人价格、交易模式、商品信息
 *          - 支持 [@main]、[@buy] 等传统脚本段落标记
 *
 * @note 所有 TOML 解析失败均会抛出 std::runtime_error，并附带文件路径信息。
 *       如果 maps/ 目录下没有任何配置文件，load() 方法也会抛出异常。
 *
 * @see config_loader.hpp, models.hpp
 */

#include "config/config_loader.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "shared/legacy/map_document.hpp"
#include "toml++/toml.hpp"
#include "util/legacy_text.hpp"
#include "util/string_utils.hpp"

namespace mir2 {

namespace {

/**
 * @brief 遗留 NPC 脚本 #INCLUDE 递归深度上限
 * @details 防止由于循环包含导致的栈溢出。当包含嵌套深度超过此值时，
 *          预处理器会静默停止展开。
 */
constexpr std::int32_t kLegacyScriptIncludeDepthLimit = 64;

/**
 * @brief 解析 TOML 文件，失败时抛出带文件路径的异常
 * @param path TOML 文件路径
 * @return 解析成功的 toml::table
 * @throws std::runtime_error 如果文件不存在、格式错误或 IO 失败
 */
toml::table parse_file_checked(const std::filesystem::path& path) {
  try {
    return toml::parse_file(path.string());
  } catch (const std::exception& ex) {
    throw std::runtime_error("Failed to parse TOML file '" + path.string() + "': " + ex.what());
  }
}

/**
 * @brief 从 TOML 表中读取键值，若不存在则返回默认值
 * @tparam T 值的类型（自动推导）
 * @param table TOML 表
 * @param key 键名
 * @param fallback 默认值
 * @return 键对应的值，或 fallback
 */
template <typename T>
T value_or(const toml::table& table, const std::string& key, T fallback) {
  if (auto value = table[key].value<T>()) {
    return *value;
  }
  return fallback;
}

/**
 * @brief 从 TOML 表中读取长度为 4 的整数数组，若不存在则返回默认值
 * @details 用于读取魔法的每级需求等级和每级最大训练值。
 *          数组元素数量以 min(result.size(), array->size()) 为准。
 * @param table TOML 表
 * @param key 键名
 * @param fallback 默认数组
 * @return 解析后的数组，缺失的元素保持 fallback 的对应值
 */
std::array<std::int32_t, 4> int4_or(const toml::table& table, const std::string& key,
                                    std::array<std::int32_t, 4> fallback) {
  const auto* array = table[key].as_array();
  if (array == nullptr) {
    return fallback;
  }
  auto result = fallback;
  for (std::size_t index = 0; index < result.size() && index < array->size(); ++index) {
    if (auto value = array->get(index)->value<std::int32_t>()) {
      result[index] = *value;
    }
  }
  return result;
}

/**
 * @brief 从 TOML 表中读取路径值（字符串转为 path），若不存在则返回默认值
 * @param table TOML 表
 * @param key 键名
 * @param fallback 默认路径
 * @return 解析后的路径，或 fallback
 */
std::filesystem::path path_or(const toml::table& table, const std::string& key,
                              const std::filesystem::path& fallback) {
  if (auto value = table[key].value<std::string>()) {
    return std::filesystem::path(*value);
  }
  return fallback;
}

/**
 * @brief 从 TOML 表中读取怪物 AI 配置字符串并转换为枚举值
 * @details 支持多种别名，例如 "aggressive" 和 "active" 都映射到
 *          MonsterAiProfile::aggressive。不区分大小写，会先做
 *          trim 和小写转换。
 * @param table TOML 表
 * @param key 键名
 * @param fallback 默认 AI 类型
 * @return 匹配到的 MonsterAiProfile 枚举值，或 fallback
 */
MonsterAiProfile monster_ai_profile_or(const toml::table& table, const std::string& key,
                                       MonsterAiProfile fallback) {
  if (auto value = table[key].value<std::string>()) {
    const auto lowered = util::lower_copy(util::trim(*value));
    if (lowered == "passive_animal" || lowered == "passive" || lowered == "animal") {
      return MonsterAiProfile::passive_animal;
    }
    if (lowered == "aggressive" || lowered == "active") {
      return MonsterAiProfile::aggressive;
    }
    if (lowered == "slow") {
      return MonsterAiProfile::slow;
    }
    if (lowered == "ranged" || lowered == "range") {
      return MonsterAiProfile::ranged;
    }
    if (lowered == "stationary" || lowered == "fixed") {
      return MonsterAiProfile::stationary;
    }
    if (lowered == "basic") {
      return MonsterAiProfile::basic;
    }
  }
  return fallback;
}

/**
 * @brief 检查文本是否包含任意一个指定的子串
 * @param text 待检查的文本
 * @param needles 要搜索的子串列表
 * @return 如果找到任意一个子串则返回 true
 */
bool contains_any(std::string_view text, std::initializer_list<std::string_view> needles) {
  for (const auto needle : needles) {
    if (text.find(needle) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 检查文本是否看起来像商人代码
 * @details 商人代码是一种遗留脚本中的简短标识符，如 "1me"、"2we" 等。
 *          满足条件：长度 >= 3，首字符为 '1'-'8'，剩余部分为已知商人
 *          操作码（me=普通、we=武器、dr=药品、du=耐久、dm=魔法、
 *          st=时装、wh=仓库、bo=书、ri=戒指、br=手镯、ne=项链、ac=饰品）。
 * @param text 待检查的文本
 * @return 如果看起来像商人代码则返回 true
 */
bool looks_like_merchant_code(std::string_view text) {
  if (text.size() < 3 || text.front() < '1' || text.front() > '8') {
    return false;
  }
  return contains_any(text, {"me", "we", "dr", "du", "dm", "st", "wh", "bo", "ri", "br", "ne", "ac"});
}

/**
 * @brief 从文件读取所有文本行
 * @details 使用遗留文本格式读取器，自动处理编码转换。
 * @param path 文件路径
 * @return 文本行的字符串向量
 */
std::vector<std::string> read_text_lines(const std::filesystem::path& path) {
  return util::read_legacy_text_lines(path);
}

/**
 * @brief 去除字符串开头的 UTF-8 BOM 标记
 * @details UTF-8 BOM 为三个字节：EF BB BF。
 *          某些遗留脚本文件以 BOM 开头，需要在处理前去除。
 * @param line 输入字符串
 * @return 去除 BOM 后的字符串
 */
std::string strip_utf8_bom(std::string line) {
  if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef &&
      static_cast<unsigned char>(line[1]) == 0xbb &&
      static_cast<unsigned char>(line[2]) == 0xbf) {
    line.erase(0, 3);
  }
  return line;
}

/**
 * @brief 判断一行文本是否为遗留脚本注释
 * @details 遗留脚本中以 ";" 或 "/" 开头的行被视为注释，在预处理中会被跳过。
 * @param trimmed 已去除首尾空格的文本行
 * @return 如果是注释行则返回 true
 */
bool is_legacy_script_comment_line(std::string_view trimmed) {
  return util::starts_with(trimmed, ";") || util::starts_with(trimmed, "/");
}

/**
 * @brief 将字符串转换为大写
 * @param text 输入文本
 * @return 全大写的字符串
 */
std::string upper_copy(std::string_view text) {
  std::string upper{text};
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return upper;
}

/**
 * @brief 提取字符串的第一个空白分隔的 token
 * @param text 输入文本
 * @return 第一个 token 字符串
 */
std::string first_token(std::string_view text) {
  std::istringstream stream{std::string(text)};
  std::string token;
  stream >> token;
  return token;
}

/**
 * @brief 将字符串按空白分割为 token 列表
 * @param text 输入文本
 * @return token 字符串向量
 */
std::vector<std::string> split_tokens(std::string_view text) {
  std::istringstream stream{std::string(text)};
  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

/**
 * @brief 大小写不敏感的字符串替换
 * @details 在 text 中查找 needle（不区分大小写），将所有匹配项替换为 replacement。
 *          同时维护原始大小写的文本和全大写的搜索文本以保持查找位置同步。
 * @param text 原始文本
 * @param needle 要搜索的子串
 * @param replacement 替换文本
 * @return 替换后的文本
 */
std::string replace_case_insensitive(std::string text, std::string_view needle,
                                     std::string_view replacement) {
  if (needle.empty()) {
    return text;
  }
  auto haystack = upper_copy(text);
  const auto wanted = upper_copy(needle);
  std::size_t pos = 0;
  while ((pos = haystack.find(wanted, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    haystack.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
  return text;
}

/**
 * @brief 规范化对话文本
 * @details 将多行文本合并为单一字符串，行间以换行符分隔。
 *          过程中会去除每行的 BOM 和首尾空格，跳过空行和注释行。
 * @param lines 原始文本行列表
 * @return 规范化后的对话文本字符串
 */
std::string normalize_dialog_text(const std::vector<std::string>& lines) {
  std::string text;
  for (auto line : lines) {
    line = strip_utf8_bom(std::move(line));
    line = util::trim(std::move(line));
    if (line.empty() || is_legacy_script_comment_line(line)) {
      continue;
    }
    if (!text.empty()) {
      text.push_back('\n');
    }
    text += line;
  }
  return text;
}

/**
 * @brief 向目标字符串追加对话段落文本
 * @details 如果目标不为空，会在追加前插入换行符。
 * @param target 目标字符串
 * @param text 要追加的文本
 */
void append_dialog_section_text(std::string& target, std::string text) {
  if (text.empty()) {
    return;
  }
  if (!target.empty()) {
    target.push_back('\n');
  }
  target += std::move(text);
}

/**
 * @brief 遗留 NPC 脚本解析结果
 * @details 存储解析遗留 NPC 脚本后的结构化输出，包括对话片段、
 *          商人价格倍率、交易标准模式和商品列表。
 * @see parse_legacy_npc_script()
 */
struct LegacyNpcScriptParseResult {
  std::vector<NpcDialogSectionConfig> dialog_sections{};      ///< @brief 对话片段列表
  std::optional<std::int32_t> price_rate_percent{};            ///< @brief 价格倍率（百分比，可选）
  std::vector<std::int32_t> deal_std_modes{};                  ///< @brief 遗留交易标准模式列表
  std::vector<MerchantProductConfig> merchant_products{};      ///< @brief 商品列表
};

/**
 * @brief 解析整数字符串（严格模式）
 * @details 使用 std::from_chars 进行严格解析，要求整个字符串均为有效数字。
 *          不接受前导/尾随空白或非数字字符（调用者需先 trim）。
 * @param value 待解析的字符串
 * @return 如果解析成功则返回整数值，否则返回 std::nullopt
 */
std::optional<std::int32_t> parse_int32_token(std::string_view value) {
  auto text = util::trim(std::string(value));
  if (text.empty()) {
    return std::nullopt;
  }
  std::int32_t result{0};
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto parsed = std::from_chars(begin, end, result);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return std::nullopt;
  }
  return result;
}

/**
 * @brief 解析字符串中第一个 token 为整数
 * @details 先提取第一个空白分隔的 token，再将其作为整数解析。
 *          用于处理如 "%150" 或 "+15 特殊格式" 等场景。
 * @param value 输入字符串
 * @return 解析成功的整数值，或 std::nullopt
 */
std::optional<std::int32_t> parse_first_int32(std::string_view value) {
  std::istringstream stream{std::string(value)};
  std::string token;
  if (!(stream >> token)) {
    return std::nullopt;
  }
  return parse_int32_token(token);
}

/**
 * @brief 解析遗留商人商品行
 * @details 解析格式为 "item_name count refresh_hours" 的行。
 *          物品名称可以用引号包裹。支持行内注释（以 ; 开头）。
 *          例如: "金创药(小) 100 2" 表示每 2 小时刷新 100 个。
 *
 * @param value 原始行文本
 * @return 解析成功则返回 MerchantProductConfig，否则返回 std::nullopt
 * @note 解析从行尾反向查找分隔符，以正确处理带空格的物品名
 */
std::optional<MerchantProductConfig> parse_merchant_product_line(std::string_view value) {
  auto line = util::trim(std::string(value));
  if (const auto comment = line.find(';'); comment != std::string::npos) {
    line = util::trim(line.substr(0, comment));
  }
  MerchantProductConfig product;
  const auto refresh_separator = line.find_last_of(" \t");
  if (refresh_separator == std::string::npos) {
    return std::nullopt;
  }
  auto refresh_hours = util::trim(line.substr(refresh_separator + 1));
  line = util::trim(line.substr(0, refresh_separator));
  const auto count_separator = line.find_last_of(" \t");
  if (count_separator == std::string::npos) {
    return std::nullopt;
  }
  auto count = util::trim(line.substr(count_separator + 1));
  product.item_name = util::trim(line.substr(0, count_separator));
  if (product.item_name.size() >= 2 && product.item_name.front() == '"' &&
      product.item_name.back() == '"') {
    product.item_name = product.item_name.substr(1, product.item_name.size() - 2);
  }
  const auto parsed_count = parse_int32_token(count);
  const auto parsed_refresh_hours = parse_int32_token(refresh_hours);
  if (product.item_name.empty() || !parsed_count.has_value() || *parsed_count <= 0 ||
      !parsed_refresh_hours.has_value()) {
    return std::nullopt;
  }
  product.count = *parsed_count;
  product.refresh_hours = *parsed_refresh_hours;
  return product;
}

/**
 * @brief 解析遗留脚本的 #INCLUDE 路径
 * @details 根据当前脚本路径和原始包含名，按优先级顺序搜索多个候选目录：
 *          当前目录 -> 父目录 -> Defines/ -> QuestDiary/ -> Npc_def/ -> market_def/
 *          同时尝试原始名称和 ASCII 编码名称。
 *          如果文件名被方括号包裹（如 "[test.txt]"），会先去除方括号。
 *
 * @param current_path 当前脚本文件的完整路径
 * @param raw_name 原始包含文件名（可能带方括号）
 * @return 解析找到的文件路径，如果未找到则返回空路径
 */
std::filesystem::path resolve_legacy_include_path(const std::filesystem::path& current_path,
                                                  std::string_view raw_name) {
  auto name = std::string(raw_name);
  if (name.size() >= 2 && name.front() == '[' && name.back() == ']') {
    name = name.substr(1, name.size() - 2);
  }
  const auto requested = util::path_from_utf8(util::trim(std::move(name)));
  if (requested.empty()) {
    return {};
  }
  if (requested.is_absolute() && std::filesystem::exists(requested)) {
    return requested;
  }
  const auto base = current_path.parent_path();
  const auto root = base.parent_path();
  const auto encoded = util::ascii_path(requested);
  std::vector<std::filesystem::path> candidates;
  for (const auto& directory : {
           base,
           root,
           root / "Defines",
           root / "QuestDiary",
           root / "Npc_def",
           root / "market_def",
       }) {
    candidates.push_back(directory / requested);
    candidates.push_back(directory / encoded);
  }
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

/**
 * @brief 从遗留脚本中提取指定段落的文本行
 * @details 遗留脚本使用 [SectionName] 标记段落。本函数从文件中找到
 *          匹配的段落标记后开始收集文本行，直到遇到下一个段落标记
 *          或文件结束。注释行（; 或 / 开头）会被跳过。
 *
 * @param path 脚本文件路径
 * @param section 要提取的段落名称（不区分大小写）
 * @return 段落内所有有效文本行（不含段落标记行）
 */
std::vector<std::string> extract_legacy_section(const std::filesystem::path& path,
                                                std::string_view section) {
  std::vector<std::string> extracted;
  const auto wanted = util::lower_copy(util::trim(std::string(section)));
  if (wanted.empty() || !std::filesystem::exists(path)) {
    return extracted;
  }
  bool capturing = false;
  for (auto line : read_text_lines(path)) {
    line = strip_utf8_bom(std::move(line));
    const auto trimmed = util::trim(line);
    if (is_legacy_script_comment_line(trimmed)) {
      continue;
    }
    if (trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']') {
      const auto action = util::lower_copy(util::trim(trimmed.substr(1, trimmed.size() - 2)));
      if (capturing && action != wanted) {
        break;
      }
      capturing = action == wanted;
    }
    if (capturing) {
      extracted.push_back(std::move(line));
    }
  }
  return extracted;
}

/**
 * @brief 遗留脚本预处理块类型
 * @details 在遗留脚本预处理中，文本被分为不同的逻辑块。
 *          不同类型的块中是否进行宏替换的逻辑不同。
 */
enum class LegacyPreprocessBlock {
  say,       ///< @brief 对话文本块（不进行宏替换）
  condition, ///< @brief 条件判断块（#IF，进行宏替换）
  act,       ///< @brief 动作执行块（#ACT，进行宏替换）
  else_say,  ///< @brief 否则对话块（#ELSESAY，不进行宏替换）
  else_act   ///< @brief 否则动作块（#ELSEACT，进行宏替换）
};

/**
 * @brief 判断指定块类型中是否需要进行宏替换
 * @details 根据遗留脚本的惯例，只有条件（condition）、动作（act）
 *          和否则动作（else_act）块中需要进行 #DEFINE 定义的宏替换。
 *          对话文本块（say、else_say）中保持原样。
 * @param block 当前块类型
 * @return 如果需要进行宏替换则返回 true
 */
bool should_replace_define_in_block(LegacyPreprocessBlock block) {
  return block == LegacyPreprocessBlock::condition || block == LegacyPreprocessBlock::act ||
         block == LegacyPreprocessBlock::else_act;
}

/**
 * @brief 根据当前行内容推断下一个预处理块类型
 * @details 根据以大写形式表示的文本行前缀判断当前进入哪种逻辑块。
 *          - [#IF] 开始条件块
 *          - [#ACT] 开始动作块
 *          - [#ELSEACT] 或 [#ELESACT]（兼容拼写错误）开始否则动作块
 *          - [#SAY] 开始对话块
 *          - [#ELSESAY] 开始否则对话块
 *          - [section]（方括号段落标记）返回对话块
 * @param current 当前块类型
 * @param trimmed_upper 已转换为大写的文本行
 * @return 推断出的下一块类型，如果未识别则保持当前类型
 */
LegacyPreprocessBlock next_preprocess_block(LegacyPreprocessBlock current,
                                            std::string_view trimmed_upper) {
  if (!trimmed_upper.empty() && trimmed_upper.front() == '[') {
    return LegacyPreprocessBlock::say;
  }
  if (util::starts_with(trimmed_upper, "#IF")) {
    return LegacyPreprocessBlock::condition;
  }
  if (util::starts_with(trimmed_upper, "#ACT")) {
    return LegacyPreprocessBlock::act;
  }
  if (util::starts_with(trimmed_upper, "#ELSEACT") ||
      util::starts_with(trimmed_upper, "#ELESACT")) {
    return LegacyPreprocessBlock::else_act;
  }
  if (util::starts_with(trimmed_upper, "#SAY")) {
    return LegacyPreprocessBlock::say;
  }
  if (util::starts_with(trimmed_upper, "#ELSESAY")) {
    return LegacyPreprocessBlock::else_say;
  }
  return current;
}

/** @brief 遗留脚本宏定义表：宏名 -> 替换文本 */
using LegacyDefines = std::unordered_map<std::string, std::string>;

/**
 * @brief 预处理遗留脚本的前向声明
 * @see preprocess_legacy_script_lines() 完整实现
 */
std::vector<std::string> preprocess_legacy_script_lines(const std::filesystem::path& path,
                                                        std::int32_t depth,
                                                        LegacyDefines defines,
                                                        std::optional<std::string_view> section);

/**
 * @brief 预处理遗留脚本文件（核心实现）
 * @details 这是遗留 NPC 脚本处理系统的核心函数。它读取脚本文件并执行：
 *
 *          1. #SETHOME 指令：设置 @HOME 宏的值为指定文本
 *          2. #DEFINE 指令：定义或覆盖宏
 *          3. #INCLUDE 指令：递归读取被包含文件中的宏定义
 *          4. #CALL 指令：调用其他脚本文件的指定段落，注入 GOTO 和子脚本内容
 *          5. 宏替换：在 condition/act/else_act 块中将宏名替换为对应值
 *
 *          预处理结果是一个扁平化的文本行列表，后续由 parse_legacy_npc_script()
 *          进一步处理为结构化数据。
 *
 * @param path 脚本文件路径
 * @param depth 当前递归深度（用于防止无限递归）
 * @param defines 当前的宏定义表
 * @param section 可选参数，指定只处理脚本中的特定段落
 * @return 预处理后的文本行列表
 */
std::vector<std::string> preprocess_legacy_script_lines(const std::filesystem::path& path,
                                                        std::int32_t depth,
                                                        LegacyDefines defines,
                                                        std::optional<std::string_view> section) {
  if (depth > kLegacyScriptIncludeDepthLimit || !std::filesystem::exists(path)) {
    return {};
  }

  std::vector<std::string> output;

  /**
   * @brief 设置宏定义（将宏名转换为大写后存储）
   * @param name 宏名
   * @param value 替换值
   */
  auto set_define = [&](std::string name, std::string value) {
    name = upper_copy(util::trim(std::move(name)));
    if (!name.empty()) {
      defines[std::move(name)] = util::trim(std::move(value));
    }
  };

  /**
   * @brief 递归收集 #INCLUDE 文件中的宏定义
   * @details 被 #INCLUDE 的文件可能包含更多的 #DEFINE 和 #INCLUDE 指令，
   *          需要递归展开。结构与主处理循环类似但更轻量（只处理宏定义收集）。
   * @param self 自引用 lambda 参数用于递归
   * @param define_path 被包含的文件路径
   * @param define_depth 当前递归深度
   */
  auto collect_defines = [&](auto&& self, const std::filesystem::path& define_path,
                             std::int32_t define_depth) -> void {
    if (define_depth > kLegacyScriptIncludeDepthLimit || !std::filesystem::exists(define_path)) {
      return;
    }
    for (auto line : read_text_lines(define_path)) {
      line = strip_utf8_bom(std::move(line));
      const auto trimmed = util::trim(line);
      if (is_legacy_script_comment_line(trimmed)) {
        continue;
      }
      const auto upper = upper_copy(trimmed);
      if (util::starts_with(upper, "#DEFINE")) {
        auto tokens = split_tokens(trimmed);
        if (tokens.size() >= 3) {
          const auto value_pos = trimmed.find(tokens[2]);
          set_define(tokens[1], value_pos != std::string::npos ? trimmed.substr(value_pos) : tokens[2]);
        }
      } else if (util::starts_with(upper, "#INCLUDE")) {
        auto tokens = split_tokens(trimmed);
        if (tokens.size() >= 2) {
          const auto nested = resolve_legacy_include_path(define_path, tokens[1]);
          self(self, nested, define_depth + 1);
        }
      }
    }
  };

  // 根据是否指定段落选择数据源：指定段落则提取特定段落，否则读取全文
  auto source_lines = section.has_value() ? extract_legacy_section(path, *section)
                                          : read_text_lines(path);
  auto block = LegacyPreprocessBlock::say;

  for (auto line : source_lines) {
    line = strip_utf8_bom(std::move(line));
    const auto trimmed = util::trim(line);
    if (trimmed.empty() || is_legacy_script_comment_line(trimmed)) {
      continue;
    }
    const auto upper = upper_copy(trimmed);

    // 处理 #SETHOME：设置 @HOME 宏的值
    if (util::starts_with(upper, "#SETHOME")) {
      const auto marker = first_token(trimmed);
      const auto value_pos = trimmed.find(marker);
      const auto value = value_pos != std::string::npos
                             ? util::trim(trimmed.substr(value_pos + marker.size()))
                             : std::string{};
      set_define("@HOME", value);
      continue;
    }

    // 处理 #DEFINE：定义新宏或覆盖已有的宏
    if (util::starts_with(upper, "#DEFINE")) {
      auto tokens = split_tokens(trimmed);
      if (tokens.size() >= 3) {
        const auto value_pos = trimmed.find(tokens[2]);
        set_define(tokens[1], value_pos != std::string::npos ? trimmed.substr(value_pos) : tokens[2]);
      }
      continue;
    }

    // 处理 #INCLUDE：递归收集被包含文件中的宏定义
    if (util::starts_with(upper, "#INCLUDE")) {
      auto tokens = split_tokens(trimmed);
      if (tokens.size() >= 2) {
        collect_defines(collect_defines, resolve_legacy_include_path(path, tokens[1]), depth + 1);
      }
      continue;
    }

    // 处理 #CALL：调用其他脚本文件的段落
    // 输出 #ACT + GOTO 指令，然后递归预处理被调用段落并追加到输出
    if (util::starts_with(upper, "#CALL")) {
      const auto open = trimmed.find('[');
      const auto close = trimmed.find(']', open == std::string::npos ? 0 : open + 1);
      if (open != std::string::npos && close != std::string::npos) {
        const auto file_name = trimmed.substr(open + 1, close - open - 1);
        const auto call_section = util::trim(trimmed.substr(close + 1));
        const auto call_path = resolve_legacy_include_path(path, file_name);
        output.push_back("#ACT");
        output.push_back("GOTO " + call_section);
        auto preprocessed_called =
            preprocess_legacy_script_lines(call_path, depth + 1, defines, call_section);
        output.insert(output.end(), std::make_move_iterator(preprocessed_called.begin()),
                      std::make_move_iterator(preprocessed_called.end()));
        continue;
      }
    }

    // 更新当前块类型，并根据块类型决定是否进行宏替换
    block = next_preprocess_block(block, upper);
    if (should_replace_define_in_block(block)) {
      // 在条件/动作块中，将所有宏名称大小写不敏感地替换为对应的值
      for (const auto& [name, value] : defines) {
        line = replace_case_insensitive(std::move(line), name, value);
      }
    }
    output.push_back(std::move(line));
  }
  return output;
}

/**
 * @brief 预处理遗留脚本文件（简化重载）
 * @details 从深度 0、空宏定义表、全文件范围开始预处理。
 * @param path 脚本文件路径
 * @param depth 递归深度，默认 0
 * @return 预处理后的文本行列表
 */
std::vector<std::string> preprocess_legacy_script_lines(const std::filesystem::path& path,
                                                        std::int32_t depth = 0) {
  return preprocess_legacy_script_lines(path, depth, {}, std::nullopt);
}

/**
 * @brief 将对话片段合并到 NPC 配置中
 * @details 按 action 字段（大小写不敏感）合并。如果 NPC 中已存在
 *          相同 action 的片段，则追加文本；否则添加新的片段。
 *          合并后的 action 统一保存为小写格式。
 * @param npc 目标 NPC 配置
 * @param sections 待合并的对话片段列表
 */
void merge_dialog_sections(NpcConfig& npc, std::vector<NpcDialogSectionConfig> sections) {
  for (auto& section : sections) {
    const auto action = util::lower_copy(section.action);
    auto it = std::find_if(npc.dialog_sections.begin(), npc.dialog_sections.end(),
                           [&](const NpcDialogSectionConfig& existing) {
                             return util::lower_copy(existing.action) == action;
                           });
    if (it == npc.dialog_sections.end()) {
      section.action = action;
      npc.dialog_sections.push_back(std::move(section));
    } else {
      it->action = action;
      append_dialog_section_text(it->text, std::move(section.text));
    }
  }
}

/**
 * @brief 将对话片段合并到目标向量中（泛型版本）
 * @details 与 NpcConfig 版本的逻辑相同，但目标为 vector 容器。
 *          用于合并地图任务的对话片段。
 * @param target 目标对话片段向量
 * @param sections 待合并的对话片段列表
 */
void merge_dialog_sections(std::vector<NpcDialogSectionConfig>& target,
                           std::vector<NpcDialogSectionConfig> sections) {
  for (auto& section : sections) {
    const auto action = util::lower_copy(section.action);
    auto it = std::find_if(target.begin(), target.end(),
                           [&](const NpcDialogSectionConfig& existing) {
                             return util::lower_copy(existing.action) == action;
                           });
    if (it == target.end()) {
      section.action = action;
      target.push_back(std::move(section));
    } else {
      it->action = action;
      append_dialog_section_text(it->text, std::move(section.text));
    }
  }
}

/**
 * @brief 解析 TOML 数组格式的对话片段
 * @details 从 TOML 配置文件中解析 "dialog_sections" 数组，每个元素
 *          必须是一个包含 "action" 和 "text" 字段的 table。
 *          action 会自动转换为小写并去除前后空格。
 * @param dialog_sections TOML 数组指针（可能为 nullptr）
 * @return 解析后的 NpcDialogSectionConfig 列表
 */
std::vector<NpcDialogSectionConfig> parse_dialog_sections_array(const toml::array* dialog_sections) {
  std::vector<NpcDialogSectionConfig> sections;
  if (dialog_sections == nullptr) {
    return sections;
  }
  for (const auto& section_node : *dialog_sections) {
    if (!section_node.is_table()) {
      continue;
    }
    const auto& section_table = *section_node.as_table();
    auto action = value_or<std::string>(section_table, "action", {});
    auto text = value_or<std::string>(section_table, "text", {});
    action = util::lower_copy(util::trim(std::move(action)));
    if (!action.empty() && !text.empty()) {
      sections.push_back({std::move(action), std::move(text)});
    }
  }
  return sections;
}

/**
 * @brief 将遗留 NPC 脚本解析结果合并到 NPC 配置中
 * @details 将 parse_legacy_npc_script() 的解析结果合并到 NpcConfig 中：
 *          - 合并对话片段
 *          - 如果有价格倍率则覆盖
 *          - 去重添加交易标准模式
 *          - 追加商品列表
 * @param npc 目标 NPC 配置（会被修改）
 * @param result 遗留脚本解析结果
 */
void merge_legacy_npc_script(NpcConfig& npc, LegacyNpcScriptParseResult result) {
  merge_dialog_sections(npc, std::move(result.dialog_sections));
  if (result.price_rate_percent.has_value()) {
    npc.price_rate_percent = *result.price_rate_percent;
  }
  for (const auto std_mode : result.deal_std_modes) {
    if (std::find(npc.legacy_deal_std_modes.begin(), npc.legacy_deal_std_modes.end(),
                  std_mode) == npc.legacy_deal_std_modes.end()) {
      npc.legacy_deal_std_modes.push_back(std_mode);
    }
  }
  npc.merchant_products.insert(npc.merchant_products.end(),
                               std::make_move_iterator(result.merchant_products.begin()),
                               std::make_move_iterator(result.merchant_products.end()));
}

/**
 * @brief 解析遗留 NPC 脚本文件
 * @details 本函数是遗留脚本处理系统的第二阶段。在第一阶段（预处理）展开
 *          #DEFINE/#INCLUDE/#CALL 等指令后，本函数将文本行解析为结构化的
 *          对话片段和商人信息。
 *
 *          解析逻辑：
 *          1. 识别 [SectionName] 段落标记，刷新当前对话片段
 *          2. 对话行归入当前段落（@ 开头或 ~@ 开头的段落）
 *          3. 段落外的 % 行解析为价格倍率
 *          4. 段落外的 + 行解析为交易标准模式
 *          5. [goods] 段落中的行解析为商品定义
 *          6. @home 段落自动映射为 @main
 *
 * @param path 遗留脚本文件路径
 * @return LegacyNpcScriptParseResult 包含解析后的对话片段和商品信息
 */
LegacyNpcScriptParseResult parse_legacy_npc_script(const std::filesystem::path& path) {
  LegacyNpcScriptParseResult result;
  if (!std::filesystem::exists(path)) {
    return result;
  }

  std::string current_action;
  std::vector<std::string> current_lines;

  /**
   * @brief 规范化动作标识
   * @details 将 @home 映射为 @main，这是遗留脚本的常见惯例。
   *         ~@home 同样映射为 ~@main（~ 前缀表示隐藏段落）。
   */
  auto normalize_action = [](std::string action) {
    action = util::lower_copy(util::trim(std::move(action)));
    if (action == "@home") {
      return std::string{"@main"};
    }
    if (action == "~@home") {
      return std::string{"~@main"};
    }
    return action;
  };

  /**
   * @brief 刷新当前对话片段
   * @details 将累积的 current_lines 规范化后作为一个对话片段加入结果列表。
   *          只处理 @ 开头或 ~@ 开头的动作标识。清空当前累积状态。
   */
  auto flush = [&]() {
    auto action = normalize_action(current_action);
    if (!action.empty() && (action.front() == '@' || util::starts_with(action, "~@"))) {
      const auto text = normalize_dialog_text(current_lines);
      if (!text.empty()) {
        result.dialog_sections.push_back(NpcDialogSectionConfig{std::move(action), text});
      }
    }
    current_action.clear();
    current_lines.clear();
  };

  // 主解析循环：遍历预处理后的每一行
  for (auto line : preprocess_legacy_script_lines(path)) {
    line = strip_utf8_bom(std::move(line));
    const auto trimmed = util::trim(line);

    // 检测段落标记 [section_name]
    if (trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']') {
      flush();
      current_action = normalize_action(trimmed.substr(1, trimmed.size() - 2));
      continue;
    }

    const auto current_action_key = normalize_action(current_action);

    // 如果不在任何段落中，检查是否有 % 价格倍率或 + 交易模式标记
    if (current_action.empty()) {
      if (!trimmed.empty() && trimmed.front() == '%') {
        // % 行：解析价格倍率，例如 "%150" 表示 150% 价格
        if (const auto rate = parse_first_int32(std::string_view(trimmed).substr(1));
            rate.has_value()) {
          result.price_rate_percent = *rate;
        }
      } else if (!trimmed.empty() && trimmed.front() == '+') {
        // + 行：解析交易标准模式，例如 "+15" 表示支持 std_mode=15 的物品交易
        if (const auto std_mode = parse_first_int32(std::string_view(trimmed).substr(1));
            std_mode.has_value()) {
          result.deal_std_modes.push_back(*std_mode);
        }
      }
      continue;
    }

    // 处理 [goods] 段落：解析商品定义行
    if (current_action_key == "goods") {
      if (!trimmed.empty() && !util::starts_with(trimmed, ";")) {
        if (auto product = parse_merchant_product_line(trimmed); product.has_value()) {
          result.merchant_products.push_back(std::move(*product));
        }
      }
      continue;
    }

    // 其他段落：累积对话文本行
    if (!current_action.empty()) {
      current_lines.push_back(line);
    }
  }
  flush();
  return result;
}

/**
 * @brief 解析 NPC 对话脚本（仅提取对话片段）
 * @details 封装 parse_legacy_npc_script()，只返回对话片段部分，
 *          丢弃商品信息等。用于只需要对话文本的场景。
 * @param path 脚本文件路径
 * @return 对话片段列表
 */
std::vector<NpcDialogSectionConfig> parse_npc_dialog_script(const std::filesystem::path& path) {
  return parse_legacy_npc_script(path).dialog_sections;
}

/**
 * @brief 在地图脚本文件名中插入地图 ID
 * @details 用于生成地图特定版本的脚本文件名。
 *          例如: 原路径为 "scripts/npc.txt"，map_id="3"，
 *          则返回 "scripts/npc-3.txt"。
 * @param path 原始脚本文件路径
 * @param map_id 地图 ID
 * @return 插入地图 ID 后的文件路径，若输入为空则返回空路径
 */
std::filesystem::path with_map_suffix(const std::filesystem::path& path, const std::string& map_id) {
  if (path.empty()) {
    return {};
  }
  return path.parent_path() / util::path_from_utf8(
      util::path_to_utf8_string(path.stem()) + "-" + map_id +
      util::path_to_utf8_string(path.extension()));
}

/**
 * @brief 解析 NPC 脚本文件的完整路径
 * @details 根据 NPC 配置中的脚本文件名和地图 ID，在多个候选目录中搜索
 *          实际的脚本文件。搜索优先级：
 *          1. 配置路径下的原始脚本文件
 *          2. 地图特定版本（在文件名中插入地图 ID）
 *          3. npc_scripts/ 子目录
 *          4. market_def/ 和 Npc_def/ 子目录
 *
 *          同时搜索常规文件和地图特定文件（文件名含 "-map_id" 后缀）。
 *
 * @param root 配置根目录
 * @param npc NPC 配置
 * @return 找到的脚本文件完整路径，如果未找到则返回空路径
 */
std::filesystem::path resolve_npc_script_path(const std::filesystem::path& root, const NpcConfig& npc) {
  if (npc.script.empty()) {
    return {};
  }

  const auto script_path = util::path_from_utf8(npc.script);
  const auto map_specific = with_map_suffix(script_path, npc.map_id);
  const auto filename = script_path.filename();
  const auto map_specific_filename = map_specific.filename();

  // 按优先级顺序列出所有候选路径
  const std::vector<std::filesystem::path> candidates = {
      root / script_path,
      root / map_specific,
      root / "npc_scripts" / script_path,
      root / "npc_scripts" / map_specific,
      root / "npc_scripts" / filename,
      root / "npc_scripts" / map_specific_filename,
      root / "npc_scripts" / "market_def" / filename,
      root / "npc_scripts" / "market_def" / map_specific_filename,
      root / "npc_scripts" / "Npc_def" / filename,
      root / "npc_scripts" / "Npc_def" / map_specific_filename,
      root / "market_def" / filename,
      root / "market_def" / map_specific_filename,
      root / "Npc_def" / filename,
      root / "Npc_def" / map_specific_filename,
  };

  for (const auto& candidate : candidates) {
    if (!candidate.empty() && std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

/**
 * @brief 解析地图任务脚本文件的完整路径（基于配置根目录、地图 ID 和 qfile）
 * @details 与 resolve_npc_script_path() 类似，但专门用于 MapQuest 脚本。
 *          搜索 MapQuest_def/ 子目录以及 npc_scripts/MapQuest_def/ 子目录。
 *          同样支持地图特定版本（带地图 ID 后缀的文件名）。
 *
 * @param root 配置根目录
 * @param map_id 地图 ID
 * @param qfile 任务脚本文件名
 * @return 找到的脚本文件完整路径，如果未找到则返回空路径
 */
std::filesystem::path resolve_map_quest_script_path(const std::filesystem::path& root,
                                                    const std::string& map_id,
                                                    const std::string& qfile) {
  if (qfile.empty()) {
    return {};
  }

  const auto script_path = util::path_from_utf8(qfile);
  const auto map_specific = with_map_suffix(script_path, map_id);
  const auto filename = script_path.filename();
  const auto map_specific_filename = map_specific.filename();

  const std::vector<std::filesystem::path> candidates = {
      root / script_path,
      root / map_specific,
      root / "npc_scripts" / "MapQuest_def" / script_path,
      root / "npc_scripts" / "MapQuest_def" / map_specific,
      root / "npc_scripts" / "MapQuest_def" / filename,
      root / "npc_scripts" / "MapQuest_def" / map_specific_filename,
      root / "MapQuest_def" / script_path,
      root / "MapQuest_def" / map_specific,
      root / "MapQuest_def" / filename,
      root / "MapQuest_def" / map_specific_filename,
  };

  for (const auto& candidate : candidates) {
    if (!candidate.empty() && std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

/**
 * @brief 解析地图任务脚本文件的完整路径（基于 MapQuestConfig 对象）
 * @details 重载版本，从 MapQuestConfig 对象中自动提取 map_id 和 qfile。
 * @param root 配置根目录
 * @param quest 地图任务配置
 * @return 找到的脚本文件完整路径，如果未找到则返回空路径
 */
std::filesystem::path resolve_map_quest_script_path(const std::filesystem::path& root,
                                                    const MapQuestConfig& quest) {
  return resolve_map_quest_script_path(root, quest.map_id, quest.qfile);
}

/**
 * @brief 解析启动任务脚本文件的完整路径
 * @details 启动任务脚本是服务端启动时自动执行的任务脚本，固定名为
 *          "StartupQuest.txt"。在多个候选目录中搜索：
 *          - npc_scripts/Startup/
 *          - npc_scripts/QuestDiary/Startup/
 *          - Startup/
 *          - QuestDiary/Startup/
 *          - 根目录
 * @param root 配置根目录
 * @return 找到的启动脚本文件完整路径，如果未找到则返回空路径
 */
std::filesystem::path resolve_startup_quest_script_path(const std::filesystem::path& root) {
  const std::vector<std::filesystem::path> candidates = {
      root / "npc_scripts" / "Startup" / "StartupQuest.txt",
      root / "npc_scripts" / "QuestDiary" / "Startup" / "StartupQuest.txt",
      root / "Startup" / "StartupQuest.txt",
      root / "QuestDiary" / "Startup" / "StartupQuest.txt",
      root / "StartupQuest.txt",
  };
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

/**
 * @brief 推断 NPC 的服务类型
 * @details 当 NPC 配置中没有显式指定 service 字段时，通过分析 NPC 的
 *          ID、名称、脚本文件名和已有数据来推断其服务类型。
 *          推断优先级：
 *          1. 如果有商品数据，则为 "sell_repair"
 *          2. 名称/ID 包含公会/城堡关键词，则为 "guild_castle"
 *          3. 名称包含守卫/导师/传送等关键词，则为 "none"（功能性 NPC）
 *          4. 名称包含仓库关键词，则为 "storage"
 *          5. 名称包含商人/铁匠/商店关键词，则为 "sell_repair"
 *          6. ID 或脚本名看起来像商人代码，则为 "sell_repair"
 *          7. 以上都不是，则为 "none"
 *
 * @param npc NPC 配置
 * @return 推断出的服务类型字符串
 */
std::string infer_npc_service(const NpcConfig& npc) {
  if (!npc.merchant_products.empty() || !npc.merchant_goods.empty()) {
    return "sell_repair";
  }
  const auto haystack = util::lower_copy(npc.id + " " + npc.name + " " + npc.script);
  if (contains_any(haystack, {"guild", "castle", "sabuk", "chamberlain"})) {
    return "guild_castle";
  }
  if (contains_any(haystack, {"guard", "helper", "teacher", "sender", "oldman", "boss", "marry",
                              "lovegirl", "gman", "match", "npc"})) {
    return "none";
  }
  if (contains_any(haystack, {"storage", "warehouse", "depot", "keeper", "storeman"})) {
    return "storage";
  }
  if (contains_any(haystack, {"merchant", "trader", "blacksmith", "smith", "repair", "store", "shop"})) {
    return "sell_repair";
  }
  if (looks_like_merchant_code(util::lower_copy(npc.id)) || looks_like_merchant_code(util::lower_copy(npc.script))) {
    return "sell_repair";
  }
  return "none";
}

/**
 * @brief 解析地图源文件的完整路径
 * @details 根据配置中的 source_map 字段和默认规则确定地图文件的
 *          实际路径。优先级：
 *          1. 如果配置了 source_map：
 *             a. 绝对路径直接使用
 *             b. 相对于 maps/ 目录
 *             c. 相对于配置根目录
 *             d. 相对于资源根目录
 *          2. 未配置时默认到 asset_root/Map/<map_id>.map
 *
 * @param config_root 配置根目录
 * @param asset_root 资源根目录
 * @param maps_directory 地图配置目录
 * @param map_id 地图 ID
 * @param configured_path 配置文件中指定的 source_map 路径
 * @return 解析后的地图文件完整路径
 */
std::filesystem::path resolve_map_source_path(const std::filesystem::path& config_root,
                                              const std::filesystem::path& asset_root,
                                              const std::filesystem::path& maps_directory,
                                              const std::string& map_id,
                                              const std::filesystem::path& configured_path) {
  if (!configured_path.empty()) {
    if (configured_path.is_absolute()) {
      return configured_path;
    }
    const auto map_relative = maps_directory / configured_path;
    if (std::filesystem::exists(map_relative)) {
      return map_relative;
    }
    const auto config_relative = config_root / configured_path;
    if (std::filesystem::exists(config_relative)) {
      return config_relative;
    }
    return asset_root / configured_path;
  }
  return asset_root / "Map" / (map_id + ".map");
}

/**
 * @brief 加载地图配置
 * @details 遍历 maps/ 目录下的所有 TOML 文件，解析每张地图的完整配置。
 *          支持从 TOML 文件直接读取地图尺寸，也支持通过解码 .map 文件
 *          自动获取尺寸（如果未在 TOML 中指定）。
 *
 *          每张地图可配置：
 *          - 基本属性：ID、标题、尺寸、源文件
 *          - 区域：安全区（safe_zones）、红名区（badman_zones）
 *          - 规则：PK 允许、等级限制、物品限制、任务检查（check_quest）
 *          - 传送门（gates）：坐标、目标地图、门状态要求
 *          - 显示：白天/黑夜模式、战斗区域标识
 *
 *          支持多种别名（如 "safe" 是 "law_full" 的别名，"level" 是 "need_level" 的别名）。
 *
 * @param directory maps/ 配置目录路径
 * @param config 宿主配置（maps 列表会被追加）
 * @param config_root 配置根目录
 */
void load_maps(const std::filesystem::path& directory, HostConfig& config,
               const std::filesystem::path& config_root) {
  if (!std::filesystem::exists(directory)) {
    return;
  }
  const auto root = directory.parent_path();

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
      continue;
    }
    const auto table = parse_file_checked(entry.path());
    MapConfig map;
    map.id = value_or<std::string>(table, "id", entry.path().stem().string());
    map.title = value_or<std::string>(table, "title", map.id);
    map.source_map = resolve_map_source_path(config_root, config.runtime.asset_root, directory,
                                             map.id, path_or(table, "source_map", {}));
    map.width = value_or<int>(table, "width", 0);
    map.height = value_or<int>(table, "height", 0);
    // 如果尺寸未在 TOML 中指定，尝试从 .map 文件中解码获取
    if ((map.width <= 0 || map.height <= 0) && !map.source_map.empty()) {
      if (const auto decoded = legacy::decode_map_file(map.source_map); decoded != nullptr) {
        if (map.width <= 0) {
          map.width = decoded->width;
        }
        if (map.height <= 0) {
          map.height = decoded->height;
        }
      }
    }
    map.home_x = value_or<int>(table, "home_x", 0);
    map.home_y = value_or<int>(table, "home_y", 0);
    // 支持别名：safe -> law_full, fight -> fight_zone, day -> daylight, 等
    map.law_full = value_or<bool>(table, "law_full", value_or<bool>(table, "safe", false));
    map.fight_zone = value_or<bool>(table, "fight_zone", value_or<bool>(table, "fight", false));
    map.fight3_zone = value_or<bool>(table, "fight3_zone", value_or<bool>(table, "fight3", false));
    map.daylight = value_or<bool>(table, "daylight", value_or<bool>(table, "day", false));
    map.darkness = value_or<bool>(table, "darkness", value_or<bool>(table, "dark", false));
    map.no_reconnect = value_or<bool>(table, "no_reconnect", false);
    map.need_hole = value_or<bool>(table, "need_hole", false);
    map.no_recall = value_or<bool>(table, "no_recall", false);
    map.no_random_move = value_or<bool>(table, "no_random_move", false);
    map.no_drug = value_or<bool>(table, "no_drug", false);
    map.no_position_move = value_or<bool>(table, "no_position_move", false);
    map.need_level = value_or<int>(table, "need_level", value_or<int>(table, "level", 0));
    map.mine_map = value_or<int>(table, "mine_map", value_or<int>(table, "mine", 0));
    map.back_map = value_or<std::string>(table, "back_map", {});
    map.quiz_zone = value_or<bool>(table, "quiz_zone", value_or<bool>(table, "quiz", false));
    map.need_set_number = value_or<int>(
        table, "need_set_number", value_or<int>(table, "need_set", -1));
    map.need_set_value = value_or<int>(table, "need_set_value", -1);
    // PK 默认在非完全法治地图上允许
    map.allow_pk = value_or<bool>(table, "allow_pk", !map.law_full);

    // 解析 check_quest 字段：可以是字符串（qfile 路径）或 table（完整配置）
    if (auto qfile = table["check_quest"].value<std::string>()) {
      MapEntryQuestConfig quest;
      quest.qfile = *qfile;
      if (const auto script_path = resolve_map_quest_script_path(root, map.id, quest.qfile);
          !script_path.empty()) {
        merge_dialog_sections(quest.dialog_sections, parse_npc_dialog_script(script_path));
      }
      map.check_quest = std::move(quest);
    } else if (auto check_quest = table["check_quest"].as_table()) {
      MapEntryQuestConfig quest;
      quest.qfile = value_or<std::string>(*check_quest, "qfile", {});
      quest.dialog_sections =
          parse_dialog_sections_array((*check_quest)["dialog_sections"].as_array());
      if (const auto script_path = resolve_map_quest_script_path(root, map.id, quest.qfile);
          !script_path.empty()) {
        merge_dialog_sections(quest.dialog_sections, parse_npc_dialog_script(script_path));
      }
      if (!quest.qfile.empty() || !quest.dialog_sections.empty()) {
        map.check_quest = std::move(quest);
      }
    }

    // 解析安全区列表
    if (auto safe_zones = table["safe_zones"].as_array()) {
      for (const auto& zone_node : *safe_zones) {
        if (!zone_node.is_table()) {
          continue;
        }
        const auto& zone_table = *zone_node.as_table();
        MapZoneConfig zone;
        zone.x = value_or<int>(zone_table, "x", value_or<int>(zone_table, "left", 0));
        zone.y = value_or<int>(zone_table, "y", value_or<int>(zone_table, "top", 0));
        zone.width = value_or<int>(zone_table, "width", 0);
        zone.height = value_or<int>(zone_table, "height", 0);
        // 如果 width/height 为 0，尝试通过 right/bottom 计算
        if (zone.width <= 0) {
          const auto right = value_or<int>(zone_table, "right", zone.x - 1);
          zone.width = std::max(0, right - zone.x + 1);
        }
        if (zone.height <= 0) {
          const auto bottom = value_or<int>(zone_table, "bottom", zone.y - 1);
          zone.height = std::max(0, bottom - zone.y + 1);
        }
        if (zone.width > 0 && zone.height > 0) {
          map.safe_zones.push_back(zone);
        }
      }
    }

    // 解析红名区列表（解析逻辑与安全区相同）
    if (auto badman_zones = table["badman_zones"].as_array()) {
      for (const auto& zone_node : *badman_zones) {
        if (!zone_node.is_table()) {
          continue;
        }
        const auto& zone_table = *zone_node.as_table();
        MapZoneConfig zone;
        zone.x = value_or<int>(zone_table, "x", value_or<int>(zone_table, "left", 0));
        zone.y = value_or<int>(zone_table, "y", value_or<int>(zone_table, "top", 0));
        zone.width = value_or<int>(zone_table, "width", 0);
        zone.height = value_or<int>(zone_table, "height", 0);
        if (zone.width <= 0) {
          const auto right = value_or<int>(zone_table, "right", zone.x - 1);
          zone.width = std::max(0, right - zone.x + 1);
        }
        if (zone.height <= 0) {
          const auto bottom = value_or<int>(zone_table, "bottom", zone.y - 1);
          zone.height = std::max(0, bottom - zone.y + 1);
        }
        if (zone.width > 0 && zone.height > 0) {
          map.badman_zones.push_back(zone);
        }
      }
    }

    // 解析传送门列表
    if (auto gates = table["gates"].as_array()) {
      for (const auto& gate_node : *gates) {
        if (!gate_node.is_table()) {
          continue;
        }
        const auto& gate_table = *gate_node.as_table();
        MapGateConfig gate;
        gate.x = value_or<int>(gate_table, "x", 0);
        gate.y = value_or<int>(gate_table, "y", 0);
        gate.target_map_id = value_or<std::string>(
            gate_table, "target_map_id", value_or<std::string>(gate_table, "target_map", {}));
        gate.target_x = value_or<int>(gate_table, "target_x", 0);
        gate.target_y = value_or<int>(gate_table, "target_y", 0);
        gate.require_doors_open = value_or<bool>(gate_table, "require_doors_open", true);
        if (!gate.target_map_id.empty()) {
          map.gates.push_back(std::move(gate));
        }
      }
    }
    config.maps.push_back(std::move(map));
  }
}

/**
 * @brief 加载刷怪配置
 * @details 遍历 spawns/ 目录下的所有 TOML 文件，解析每个刷怪点的配置。
 *          每个文件包含一个 "spawns" 数组，每个元素定义：
 *          - 所属地图、怪物类型、名称、位置
 *          - 属性（等级、HP、攻击、防御等）
 *          - 重生参数（间隔、范围、数量）
 *          - 金币掉落参数（持续时间、小额概率）
 *          - legacy_group 标记：当 count>1 或 area>0 或设置了金币时自动启用
 *
 * @param directory spawns/ 配置目录路径
 * @param config 宿主配置（spawns 列表会被追加）
 */
void load_spawns(const std::filesystem::path& directory, HostConfig& config) {
  if (!std::filesystem::exists(directory)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
      continue;
    }
    const auto table = parse_file_checked(entry.path());
    if (auto spawns = table["spawns"].as_array()) {
      for (const auto& node : *spawns) {
        if (!node.is_table()) {
          continue;
        }
        const auto& spawn_table = *node.as_table();
        SpawnConfig spawn;
        spawn.map_id = value_or<std::string>(spawn_table, "map_id", {});
        spawn.actor_type = value_or<std::string>(spawn_table, "actor_type", {});
        spawn.name = value_or<std::string>(spawn_table, "name", {});
        spawn.x = value_or<int>(spawn_table, "x", 0);
        spawn.y = value_or<int>(spawn_table, "y", 0);
        spawn.respawn_ms = value_or<int>(spawn_table, "respawn_ms", 0);
        spawn.level = value_or<int>(spawn_table, "level", 1);
        spawn.max_hp = value_or<int>(spawn_table, "max_hp", 12);
        spawn.attack_power = value_or<int>(spawn_table, "attack_power", 3);
        spawn.defense = value_or<int>(spawn_table, "defense", 0);
        spawn.magic_defense = value_or<int>(spawn_table, "magic_defense", 0);
        spawn.exp_reward = value_or<int>(spawn_table, "exp_reward", 12);
        spawn.life_attrib = value_or<int>(spawn_table, "life_attrib", 0);
        spawn.tameable = value_or<bool>(spawn_table, "tameable",
                                        value_or<bool>(spawn_table, "can_tame", true));
        spawn.area = value_or<int>(spawn_table, "area", 0);
        spawn.count = std::max(value_or<int>(spawn_table, "count", 1), 1);
        spawn.zen_time_ms = value_or<int>(spawn_table, "zen_time_ms", 0);
        // 支持以分钟为单位的金币持续时间（zen_minutes），自动转换为毫秒
        if (spawn.zen_time_ms == 0) {
          const auto zen_minutes = value_or<int>(spawn_table, "zen_minutes", 0);
          if (zen_minutes > 0) {
            spawn.zen_time_ms = static_cast<std::uint32_t>(zen_minutes * 60000);
          }
        }
        spawn.small_zen_rate =
            std::clamp(value_or<int>(spawn_table, "small_zen_rate", 0), 0, 100);
        // 自动启用 legacy_group：当多只、范围刷怪或涉及金币时
        spawn.legacy_group = value_or<bool>(spawn_table, "legacy_group", false) ||
                             spawn.count > 1 || spawn.area > 0 || spawn.zen_time_ms > 0 ||
                             spawn.small_zen_rate > 0;
        if (value_or<bool>(spawn_table, "undead", false)) {
          spawn.life_attrib = 1;
        }
        config.spawns.push_back(std::move(spawn));
      }
    }
  }
}

/**
 * @brief 加载怪物定义和掉落配置
 * @details 遍历 monsters/ 目录下的所有 TOML 文件，每个文件可包含：
 *          1. "monsters" 数组：定义怪物模版（属性、AI、外观等）
 *          2. "monster_drops" 数组：定义怪物的掉落表
 *
 *          支持多种字段别名（race/race_server、race_img/race_image、
 *          img_index/appearance、lv/level 等），以兼容不同配置风格。
 *
 *          行走速度和攻击速度有最低限制（200ms），防止怪物行为过快。
 *
 * @param directory monsters/ 配置目录路径
 * @param config 宿主配置（monsters 和 monster_drops 列表会被追加）
 */
void load_monsters(const std::filesystem::path& directory, HostConfig& config) {
  if (!std::filesystem::exists(directory)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
      continue;
    }
    const auto table = parse_file_checked(entry.path());

    // 解析怪物定义
    if (auto monsters = table["monsters"].as_array()) {
      for (const auto& node : *monsters) {
        if (!node.is_table()) {
          continue;
        }
        const auto& monster_table = *node.as_table();
        MonsterDefConfig monster;
        monster.name = value_or<std::string>(monster_table, "name", {});
        // 支持字段别名
        monster.race_server = value_or<int>(monster_table, "race_server",
                                            value_or<int>(monster_table, "race", 0));
        monster.race_image = value_or<int>(monster_table, "race_image",
                                           value_or<int>(monster_table, "race_img", 0));
        monster.appearance = value_or<int>(monster_table, "appearance",
                                           value_or<int>(monster_table, "img_index", 0));
        monster.level = value_or<int>(monster_table, "level", value_or<int>(monster_table, "lv", 1));
        // undead 同时支持 bool 和 int 类型
        monster.undead = value_or<bool>(monster_table, "undead", false) ||
                         value_or<int>(monster_table, "undead", 0) != 0;
        monster.tameable = value_or<bool>(monster_table, "tameable",
                                          value_or<bool>(monster_table, "can_tame", true));
        monster.cool_eye = value_or<int>(monster_table, "cool_eye", 0);
        monster.exp = value_or<int>(monster_table, "exp", 12);
        monster.hp = value_or<int>(monster_table, "hp", 12);
        monster.mp = value_or<int>(monster_table, "mp", 0);
        monster.ac = value_or<int>(monster_table, "ac", 0);
        monster.mac = value_or<int>(monster_table, "mac", 0);
        monster.dc = value_or<int>(monster_table, "dc", 3);
        monster.dc_max = value_or<int>(monster_table, "dc_max",
                                       value_or<int>(monster_table, "dcmax", 0));
        monster.mc = value_or<int>(monster_table, "mc", 0);
        monster.sc = value_or<int>(monster_table, "sc", 0);
        monster.agility = value_or<int>(monster_table, "agility", 0);
        monster.accurate = value_or<int>(monster_table, "accurate", 0);
        monster.walk_speed_ms =
            value_or<int>(monster_table, "walk_speed_ms",
                          value_or<int>(monster_table, "walk_spd", monster.walk_speed_ms));
        monster.walk_speed_ms = std::max(monster.walk_speed_ms, 200);  // 下限 200ms
        monster.walk_step = value_or<int>(monster_table, "walk_step", monster.walk_step);
        monster.walk_wait_ms =
            value_or<int>(monster_table, "walk_wait_ms",
                          value_or<int>(monster_table, "walk_wait", monster.walk_wait_ms));
        monster.attack_speed_ms =
            value_or<int>(monster_table, "attack_speed_ms",
                          value_or<int>(monster_table, "attack_spd", monster.attack_speed_ms));
        monster.attack_speed_ms = std::max(monster.attack_speed_ms, 200);  // 下限 200ms
        monster.ai_profile =
            monster_ai_profile_or(monster_table, "ai_profile", monster.ai_profile);
        if (!monster.name.empty()) {
          config.monsters.push_back(std::move(monster));
        }
      }
    }

    // 解析怪物掉落表
    if (auto drops = table["monster_drops"].as_array()) {
      for (const auto& node : *drops) {
        if (!node.is_table()) {
          continue;
        }
        const auto& drop_table = *node.as_table();
        MonsterDropConfig drop;
        drop.monster_name = value_or<std::string>(drop_table, "monster_name", {});
        drop.sel_point = value_or<int>(drop_table, "sel_point", 0);
        drop.max_point = value_or<int>(drop_table, "max_point", 0);
        drop.item_name = value_or<std::string>(drop_table, "item_name", {});
        drop.count = std::max(value_or<int>(drop_table, "count", 1), 1);
        if (!drop.monster_name.empty() && !drop.item_name.empty()) {
          config.monster_drops.push_back(std::move(drop));
        }
      }
    }
  }
}

/**
 * @brief 加载物品配置
 * @details 遍历 items/ 目录下的所有 TOML 文件，解析 "items" 数组中的
 *          每个物品定义。物品包含完整的装备属性（AC/MAC/DC/MC/SC）、
 *          需求（等级/职业/性别）、特殊效果（效果类型/触发概率/数值）、
 *          以及绑定/解绑信息。
 *
 *          AC/MAC/DC/MC/SC 字段被 clamp 到 0-65535 范围内以适应 uint16_t。
 *          looks 字段默认为物品 ID（无专门配置时使用 ID 作为外观索引）。
 *
 * @param directory items/ 配置目录路径
 * @param config 宿主配置（items 列表会被追加）
 */
void load_items(const std::filesystem::path& directory, HostConfig& config) {
  if (!std::filesystem::exists(directory)) {
    return;
  }
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
      continue;
    }
    const auto table = parse_file_checked(entry.path());
    if (auto items = table["items"].as_array()) {
      for (const auto& node : *items) {
        if (!node.is_table()) {
          continue;
        }
        const auto& item_table = *node.as_table();
        ItemConfig item;
        item.id = value_or<int>(item_table, "id", 0);
        item.name = value_or<std::string>(item_table, "name", {});
        item.weight = value_or<int>(item_table, "weight", 0);
        item.price = value_or<int>(item_table, "price", 0);
        item.std_mode = value_or<int>(item_table, "std_mode", 0);
        item.shape = value_or<int>(item_table, "shape", 0);
        item.looks = value_or<int>(item_table, "looks", item.id);  // 默认使用 ID 作为外观
        item.dura_max = value_or<int>(item_table, "dura_max", 0);
        item.equip_slot = value_or<int>(item_table, "equip_slot", -1);
        item.hp_add = value_or<int>(item_table, "hp_add", 0);
        item.mp_add = value_or<int>(item_table, "mp_add", 0);
        item.need = value_or<int>(item_table, "need", 0);
        item.need_level =
            value_or<int>(item_table, "need_level", value_or<int>(item_table, "required_level", 0));
        item.job = value_or<int>(item_table, "job", value_or<int>(item_table, "required_job", -1));
        item.sex = value_or<int>(item_table, "sex", value_or<int>(item_table, "required_sex", -1));
        item.stock = value_or<int>(item_table, "stock", 0);
        item.item_desc = value_or<int>(item_table, "item_desc", 0);
        item.special_pwr = value_or<int>(item_table, "special_pwr", 0);
        // 将 AC/MAC/DC/MC/SC 限制在 uint16_t 范围内
        item.ac = static_cast<std::uint16_t>(std::clamp(value_or<int>(item_table, "ac", 0), 0, 65535));
        item.mac =
            static_cast<std::uint16_t>(std::clamp(value_or<int>(item_table, "mac", 0), 0, 65535));
        item.dc = static_cast<std::uint16_t>(std::clamp(value_or<int>(item_table, "dc", 0), 0, 65535));
        item.mc = static_cast<std::uint16_t>(std::clamp(value_or<int>(item_table, "mc", 0), 0, 65535));
        item.sc = static_cast<std::uint16_t>(std::clamp(value_or<int>(item_table, "sc", 0), 0, 65535));
        item.accurate =
            value_or<int>(item_table, "accurate", value_or<int>(item_table, "accuracy", 0));
        item.agility = value_or<int>(item_table, "agility", 0);
        item.atk_spd = value_or<int>(item_table, "atk_spd", 0);
        item.mg_avoid = value_or<int>(item_table, "mg_avoid", 0);
        item.strong = value_or<int>(item_table, "strong", 0);
        item.undead = value_or<int>(item_table, "undead", 0);
        item.exp_add = value_or<int>(item_table, "exp_add", 0);
        // 两个特殊效果槽位，每个包含类型、触发概率和数值
        item.eff_type1 = value_or<int>(item_table, "eff_type1", 0);
        item.eff_rate1 = value_or<int>(item_table, "eff_rate1", 0);
        item.eff_value1 = value_or<int>(item_table, "eff_value1", 0);
        item.eff_type2 = value_or<int>(item_table, "eff_type2", 0);
        item.eff_rate2 = value_or<int>(item_table, "eff_rate2", 0);
        item.eff_value2 = value_or<int>(item_table, "eff_value2", 0);
        item.scroll_kind = value_or<std::string>(item_table, "scroll_kind", {});
        item.unbind_item = value_or<std::string>(item_table, "unbind_item", {});
        item.unbind_count = value_or<int>(item_table, "unbind_count", 0);
        item.ani_count = value_or<int>(item_table, "ani_count", 0);
        config.items.push_back(std::move(item));
      }
    }
  }
}

/**
 * @brief 加载魔法配置
 * @details 遍历 magic/ 目录下的所有 TOML 文件，解析 "magic" 数组中的
 *          每个魔法定义。支持完整的战斗魔法属性（伤害、治疗、护盾、
 *          减速、持续伤害等），以及可选的 LegacyMagicDefinition 子表
 *          用于兼容旧版客户端协议。
 *
 *          遗留子表（legacy）包含旧版客户端所需的效果类型、符文、
 *          技能等级训练参数等。如果 TOML 中存在 [magic.legacy] 表，
 *          则会自动解析。
 *
 * @param directory magic/ 配置目录路径
 * @param config 宿主配置（magics 列表会被追加）
 */
void load_magics(const std::filesystem::path& directory, HostConfig& config) {
  if (!std::filesystem::exists(directory)) {
    return;
  }
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
      continue;
    }
    const auto table = parse_file_checked(entry.path());
    if (auto magics = table["magic"].as_array()) {
      for (const auto& node : *magics) {
        if (!node.is_table()) {
          continue;
        }
        const auto& magic_table = *node.as_table();
        MagicConfig magic;
        magic.id = value_or<int>(magic_table, "id", 0);
        magic.name = value_or<std::string>(magic_table, "name", {});
        magic.mp_cost = value_or<int>(magic_table, "mp_cost", 0);
        magic.power = value_or<int>(magic_table, "power", 0);
        magic.radius = value_or<int>(magic_table, "radius", 0);
        magic.affect_players = value_or<bool>(magic_table, "affect_players", false);
        magic.affect_monsters = value_or<bool>(magic_table, "affect_monsters", true);
        magic.instant_heal = value_or<int>(magic_table, "instant_heal", 0);
        magic.heal_per_tick = value_or<int>(magic_table, "heal_per_tick", 0);
        magic.dispel_negative = value_or<bool>(magic_table, "dispel_negative", false);
        magic.dot_damage = value_or<int>(magic_table, "dot_damage", 0);
        magic.effect_duration_ms = value_or<int>(magic_table, "effect_duration_ms", 0);
        magic.effect_tick_ms = value_or<int>(magic_table, "effect_tick_ms", 0);
        magic.slow_percent = value_or<int>(magic_table, "slow_percent", 0);
        magic.shield_amount = value_or<int>(magic_table, "shield_amount", 0);
        // 解析可选的遗留魔法定义子表
        if (const auto* legacy_table = magic_table["legacy"].as_table(); legacy_table != nullptr) {
          magic.legacy.legacy_present = value_or<bool>(*legacy_table, "legacy_present", true);
          magic.legacy.effect_type = value_or<int>(*legacy_table, "effect_type", 0);
          magic.legacy.effect = value_or<int>(*legacy_table, "effect", 0);
          magic.legacy.spell = value_or<int>(*legacy_table, "spell", 0);
          magic.legacy.min_power = value_or<int>(*legacy_table, "min_power", 0);
          magic.legacy.max_power = value_or<int>(*legacy_table, "max_power", 0);
          magic.legacy.job = value_or<int>(*legacy_table, "job", 0);
          magic.legacy.need_level = int4_or(*legacy_table, "need_level", {});
          magic.legacy.max_train = int4_or(*legacy_table, "max_train", {});
          magic.legacy.max_train_level = value_or<int>(*legacy_table, "max_train_level", 0);
          magic.legacy.delay_time = value_or<int>(*legacy_table, "delay_time", 0);
          magic.legacy.def_spell = value_or<int>(*legacy_table, "def_spell", 0);
          magic.legacy.def_min_power = value_or<int>(*legacy_table, "def_min_power", 0);
          magic.legacy.def_max_power = value_or<int>(*legacy_table, "def_max_power", 0);
          magic.legacy.desc = value_or<std::string>(*legacy_table, "desc", {});
          magic.legacy.is_sword_skill = value_or<bool>(*legacy_table, "is_sword_skill", false);
        }
        config.magics.push_back(std::move(magic));
      }
    }
  }
}

/**
 * @brief 加载 NPC 配置
 * @details 遍历 npcs/ 目录下的所有 TOML 文件，解析 "npcs" 数组中的
 *          每个 NPC 定义。加载流程：
 *          1. 从 TOML 中读取 NPC 基本属性（位置、名称、脚本等）
 *          2. 读取 menchant_goods、legacy_deal_std_modes、merchant_products 列表
 *          3. 如果 TOML 中定义了 dialog_sections，直接读取
 *          4. 根据 script 字段解析遗留 NPC 脚本文件，合并对话片段和商品信息
 *          5. 如果未指定 service，自动推断 NPC 的服务类型（商人/仓库/公会等）
 *
 *          脚本文件搜索使用 resolve_npc_script_path()，支持多个候选目录。
 *          地图特定脚本（文件名带 -map_id 后缀）具有更高优先级。
 *
 * @param directory npcs/ 配置目录路径
 * @param config 宿主配置（npcs 列表会被追加）
 */
void load_npcs(const std::filesystem::path& directory, HostConfig& config) {
  if (!std::filesystem::exists(directory)) {
    return;
  }
  const auto root = directory.parent_path();
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
      continue;
    }
    const auto table = parse_file_checked(entry.path());
    if (auto npcs = table["npcs"].as_array()) {
      for (const auto& node : *npcs) {
        if (!node.is_table()) {
          continue;
        }
        const auto& npc_table = *node.as_table();
        NpcConfig npc;
        npc.id = value_or<std::string>(npc_table, "id", {});
        npc.map_id = value_or<std::string>(npc_table, "map_id", {});
        npc.name = value_or<std::string>(npc_table, "name", {});
        npc.x = value_or<int>(npc_table, "x", 0);
        npc.y = value_or<int>(npc_table, "y", 0);
        npc.script = value_or<std::string>(npc_table, "script", {});
        npc.service = value_or<std::string>(npc_table, "service", {});
        npc.price_rate_percent =
            value_or<int>(npc_table, "price_rate_percent", npc.price_rate_percent);

        // 解析 merchant_goods 数组（物品 ID 列表）
        if (auto goods = npc_table["merchant_goods"].as_array()) {
          for (const auto& node : *goods) {
            if (auto item_id = node.value<int>()) {
              npc.merchant_goods.push_back(*item_id);
            }
          }
        }

        // 解析 legacy_deal_std_modes 数组
        if (auto deal_std_modes = npc_table["legacy_deal_std_modes"].as_array()) {
          for (const auto& node : *deal_std_modes) {
            if (auto std_mode = node.value<int>()) {
              npc.legacy_deal_std_modes.push_back(*std_mode);
            }
          }
        }

        // 解析 merchant_products 数组（商品定义，含刷新周期）
        if (auto products = npc_table["merchant_products"].as_array()) {
          for (const auto& node : *products) {
            if (!node.is_table()) {
              continue;
            }
            const auto& product_table = *node.as_table();
            MerchantProductConfig product;
            product.item_name = value_or<std::string>(product_table, "item_name", {});
            product.count = value_or<int>(product_table, "count", 0);
            product.refresh_hours = value_or<int>(product_table, "refresh_hours", 0);
            if (!product.item_name.empty() && product.count > 0) {
              npc.merchant_products.push_back(std::move(product));
            }
          }
        }

        // 解析 TOML 中直接定义的 dialog_sections
        if (auto dialog_sections = npc_table["dialog_sections"].as_array()) {
          for (const auto& node : *dialog_sections) {
            if (!node.is_table()) {
              continue;
            }
            const auto& section_table = *node.as_table();
            auto action = value_or<std::string>(section_table, "action", {});
            auto text = value_or<std::string>(section_table, "text", {});
            action = util::lower_copy(util::trim(std::move(action)));
            if (!action.empty() && !text.empty()) {
              npc.dialog_sections.push_back({std::move(action), std::move(text)});
            }
          }
        }

        // 加载遗留 NPC 脚本文件并合并结果
        if (const auto script_path = resolve_npc_script_path(root, npc); !script_path.empty()) {
          merge_legacy_npc_script(npc, parse_legacy_npc_script(script_path));
        }

        // 推断或规范化服务类型
        if (npc.service.empty()) {
          npc.service = infer_npc_service(npc);
        } else {
          npc.service = util::lower_copy(npc.service);
        }

        config.npcs.push_back(std::move(npc));
      }
    }
  }
}

/**
 * @brief 加载地图任务配置
 * @details 遍历 map_quests/ 目录下的所有 TOML 文件，解析 "map_quests"
 *          数组中的每个地图任务定义。地图任务在地图上击杀特定怪物时
 *          触发，将任务集（set_number）设为指定值。
 *
 *          每个任务可配置：
 *          - 触发条件：指定地图、怪物名、任务物品
 *          - 任务集操作：set_number 和 value（0 或 1）
 *          - 对话脚本：通过 qfile 引用遗留脚本，或直接定义 dialog_sections
 *          - 队伍共享：enable_group 控制是否全队共享任务进度
 *
 *          如果指定了 qfile，会尝试加载对应的遗留脚本并合并对话片段。
 *
 * @param directory map_quests/ 配置目录路径
 * @param config 宿主配置（map_quests 列表会被追加）
 */
void load_map_quests(const std::filesystem::path& directory, HostConfig& config) {
  if (!std::filesystem::exists(directory)) {
    return;
  }
  const auto root = directory.parent_path();
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
      continue;
    }
    const auto table = parse_file_checked(entry.path());
    if (auto quests = table["map_quests"].as_array()) {
      for (const auto& node : *quests) {
        if (!node.is_table()) {
          continue;
        }
        const auto& quest_table = *node.as_table();
        MapQuestConfig quest;
        quest.map_id = value_or<std::string>(quest_table, "map_id", {});
        quest.set_number = value_or<int>(quest_table, "set_number", 0);
        quest.value = std::clamp(value_or<int>(quest_table, "value", 0), 0, 1);
        quest.monster_name = value_or<std::string>(quest_table, "monster_name", {});
        if (quest.monster_name.empty()) {
          quest.monster_name = value_or<std::string>(quest_table, "mon_name", {});
        }
        quest.item_name = value_or<std::string>(quest_table, "item_name", {});
        quest.qfile = value_or<std::string>(quest_table, "qfile", {});
        quest.enable_group = value_or<bool>(quest_table, "enable_group", false);
        quest.dialog_sections =
            parse_dialog_sections_array(quest_table["dialog_sections"].as_array());
        // 加载 qfile 指定的遗留脚本并合并对话片段
        if (const auto script_path = resolve_map_quest_script_path(root, quest);
            !script_path.empty()) {
          merge_dialog_sections(quest.dialog_sections, parse_npc_dialog_script(script_path));
        }
        if (!quest.map_id.empty()) {
          config.map_quests.push_back(std::move(quest));
        }
      }
    }
  }
}

/**
 * @brief 加载启动任务脚本
 * @details 寻找并解析启动任务脚本（StartupQuest.txt），该脚本在服务端
 *          启动时自动执行。脚本内容解析为对话片段列表存储到配置中。
 *          脚本文件不存在时静默跳过。
 * @param root 配置根目录
 * @param config 宿主配置（startup_quest_dialog_sections 会被设置）
 */
void load_startup_quest(const std::filesystem::path& root, HostConfig& config) {
  const auto script_path = resolve_startup_quest_script_path(root);
  if (script_path.empty()) {
    return;
  }
  config.startup_quest_dialog_sections = parse_npc_dialog_script(script_path);
}

}  // namespace

/**
 * @brief ConfigLoader::load 的实现 - 配置加载总入口
 * @details 按以下顺序执行完整的配置加载流程：
 *
 *          阶段一 —— 解析核心配置文件：
 *          1. server.toml：所有运行时参数（日志、目录、城堡/公会文本模板、经济参数）
 *          2. ports.toml：四个网关的地址和端口配置
 *          3. runtime/logic.toml：各子系统的时间预算分配
 *
 *          阶段二 —— 加载游戏数据：
 *          4. maps/       -> 地图配置（尺寸、区域、传送门、规则）
 *          5. monsters/   -> 怪物定义 + 掉落表
 *          6. spawns/     -> 刷怪配置（位置、数量、重生、金币）
 *          7. items/      -> 物品定义（属性、需求、效果）
 *          8. magic/      -> 魔法定义（效果、治疗、DoT、护盾）
 *          9. npcs/       -> NPC 配置 + 遗留脚本解析
 *          10. map_quests/ -> 地图任务配置
 *          11. StartupQuest.txt -> 启动任务脚本
 *
 *          阶段三 —— 验证：
 *          - 检查是否至少加载了一个地图配置，否则抛出异常
 *
 * @param root 配置文件根目录路径
 * @return 完整的 HostConfig 对象
 * @throws std::runtime_error 如果 maps/ 目录下没有任何配置文件
 * @see load_maps(), load_monsters(), load_spawns(), load_items(),
 *      load_magics(), load_npcs(), load_map_quests(), load_startup_quest()
 */
HostConfig ConfigLoader::load(const std::filesystem::path& root) const {
  HostConfig config;

  // ---- 阶段一：解析核心配置文件 ----
  const auto server = parse_file_checked(root / "server.toml");
  const auto ports = parse_file_checked(root / "ports.toml");
  const auto logic = parse_file_checked(root / "runtime" / "logic.toml");

  // 读取 server.toml —— 运行时参数
  config.runtime.log_dir = path_or(server, "log_dir", "logs");
  config.runtime.data_dir = path_or(server, "data_dir", "data");
  config.runtime.asset_root = path_or(server, "asset_root", root / ".." / ".." / "Legend of Mir");
  config.runtime.legacy_admin_list = path_or(server, "legacy_admin_list", "Envir/AdminList.txt");
  config.runtime.status_file = path_or(server, "status_file", "runtime/status.json");
  config.runtime.default_queue_capacity =
      value_or<std::size_t>(server, "default_queue_capacity", 4096);
  config.runtime.io_threads = value_or<std::size_t>(server, "io_threads", 2);
  config.runtime.enable_legacy_gateways = value_or<bool>(server, "enable_legacy_gateways", false);
  config.runtime.enable_client_v1_gateways =
      value_or<bool>(server, "enable_client_v1_gateways", true);
  config.runtime.legacy_approval_mode =
      value_or<bool>(server, "legacy_approval_mode", false);
  config.runtime.backpressure_threshold =
      value_or<std::size_t>(server, "backpressure_threshold", 3072);
  config.runtime.disconnect_threshold =
      value_or<std::size_t>(server, "disconnect_threshold", 3);
  config.runtime.castle_context_refresh_ms =
      value_or<std::uint32_t>(server, "castle_context_refresh_ms", 5000);
  config.runtime.login_notice_title =
      value_or<std::string>(server, "login_notice_title", "Notice");
  config.runtime.login_notice_text =
      value_or<std::string>(server, "login_notice_text", "");
  config.runtime.castle_name =
      value_or<std::string>(server, "castle_name", "Sabuk");
  config.runtime.default_castle_war_date =
      value_or<std::string>(server, "default_castle_war_date", "Unknown");
  config.runtime.no_active_wars_text =
      value_or<std::string>(server, "no_active_wars_text", "No active wars.");
  config.runtime.unclaimed_castle_owner =
      value_or<std::string>(server, "unclaimed_castle_owner", "Unclaimed");
  config.runtime.unclaimed_castle_lord =
      value_or<std::string>(server, "unclaimed_castle_lord", "Unclaimed");
  config.runtime.castle_owner_role_label =
      value_or<std::string>(server, "castle_owner_role_label", "Castle Owner");
  config.runtime.castle_owner_guild_role_label =
      value_or<std::string>(server, "castle_owner_guild_role_label", "Owner");
  config.runtime.castle_challenger_role_label =
      value_or<std::string>(server, "castle_challenger_role_label", "Challenger");
  config.runtime.castle_rival_role_label =
      value_or<std::string>(server, "castle_rival_role_label", "Rival");
  config.runtime.castle_unknown_role_label =
      value_or<std::string>(server, "castle_unknown_role_label", "Unknown");
  config.runtime.castle_war_entry_listed_label =
      value_or<std::string>(server, "castle_war_entry_listed_label", "Listed");
  config.runtime.castle_war_entry_unlisted_label =
      value_or<std::string>(server, "castle_war_entry_unlisted_label", "Not Listed");
  config.runtime.castle_war_status_active_label =
      value_or<std::string>(server, "castle_war_status_active_label", "Active");
  config.runtime.castle_war_status_available_label =
      value_or<std::string>(server, "castle_war_status_available_label", "Available");
  config.runtime.castle_role_change_owner_label =
      value_or<std::string>(server, "castle_role_change_owner_label", "Castle Owner");
  config.runtime.castle_role_change_challenger_label =
      value_or<std::string>(server, "castle_role_change_challenger_label", "Castle Challenger");
  config.runtime.castle_claim_summary_template =
      value_or<std::string>(server, "castle_claim_summary_template",
                            "Castle claimed for guild <$GUILD>.");
  config.runtime.castle_war_summary_template =
      value_or<std::string>(server, "castle_war_summary_template",
                            "Castle war declared against <$TARGETGUILD> for <$GOLD> gold.");
  config.runtime.castle_claim_require_guild_template = value_or<std::string>(
      server, "castle_claim_require_guild_template", "Join a guild before claiming the castle.");
  config.runtime.castle_claim_missing_guild_template = value_or<std::string>(
      server, "castle_claim_missing_guild_template",
      "Guild data is unavailable. Try again in a moment.");
  config.runtime.castle_claim_only_lord_template = value_or<std::string>(
      server, "castle_claim_only_lord_template", "Only the guild lord can claim the castle.");
  config.runtime.castle_war_require_guild_template = value_or<std::string>(
      server, "castle_war_require_guild_template", "Join a guild before declaring war.");
  config.runtime.castle_war_missing_guild_template = value_or<std::string>(
      server, "castle_war_missing_guild_template",
      "Guild data is unavailable. Try again in a moment.");
  config.runtime.castle_war_only_lord_template = value_or<std::string>(
      server, "castle_war_only_lord_template", "Only the guild lord can declare war.");
  config.runtime.castle_war_usage_template =
      value_or<std::string>(server, "castle_war_usage_template", "Usage: @castle war <guild_name>");
  config.runtime.castle_war_self_target_template = value_or<std::string>(
      server, "castle_war_self_target_template", "Your guild cannot declare war on itself.");
  config.runtime.castle_war_target_missing_template =
      value_or<std::string>(server, "castle_war_target_missing_template", "Target guild not found.");
  config.runtime.castle_war_already_registered_template = value_or<std::string>(
      server, "castle_war_already_registered_template",
      "Castle war against <$TARGETGUILD> is already registered.");
  config.runtime.castle_war_need_gold_template = value_or<std::string>(
      server, "castle_war_need_gold_template", "You need <$GOLD> gold to declare war.");
  config.runtime.guild_create_summary_template =
      value_or<std::string>(server, "guild_create_summary_template", "Guild <$GUILD> created.");
  config.runtime.guild_apply_summary_template = value_or<std::string>(
      server, "guild_apply_summary_template", "Application sent to guild <$GUILD>.");
  config.runtime.guild_withdraw_summary_template = value_or<std::string>(
      server, "guild_withdraw_summary_template", "Withdrew application from guild <$GUILD>.");
  config.runtime.guild_approve_summary_template = value_or<std::string>(
      server, "guild_approve_summary_template", "Approved guild application for <$TARGET>.");
  config.runtime.guild_reject_summary_template = value_or<std::string>(
      server, "guild_reject_summary_template", "Rejected guild application for <$TARGET>.");
  config.runtime.guild_kick_summary_template = value_or<std::string>(
      server, "guild_kick_summary_template", "Kicked guild member <$TARGET>.");
  config.runtime.guild_title_summary_template = value_or<std::string>(
      server, "guild_title_summary_template", "Set guild title for <$TARGET> to <$TITLE>.");
  config.runtime.guild_transfer_summary_template = value_or<std::string>(
      server, "guild_transfer_summary_template",
      "Transferred guild leadership to <$TARGET>.");
  config.runtime.guild_leave_summary_template =
      value_or<std::string>(server, "guild_leave_summary_template", "You left <$GUILD>.");
  config.runtime.guild_leave_transfer_summary_template = value_or<std::string>(
      server, "guild_leave_transfer_summary_template",
      "You left <$GUILD>. New lord: <$NEWLORD>.");
  config.runtime.guild_disband_summary_template = value_or<std::string>(
      server, "guild_disband_summary_template", "Guild <$GUILD> has been disbanded.");
  config.runtime.guild_membership_cleared_summary_template = value_or<std::string>(
      server, "guild_membership_cleared_summary_template", "Guild membership cleared.");
  config.runtime.guild_apply_alert_template =
      value_or<std::string>(server, "guild_apply_alert_template",
                            "<$TARGET> applied to join <$GUILD>.");
  config.runtime.guild_withdraw_alert_template =
      value_or<std::string>(server, "guild_withdraw_alert_template",
                            "<$TARGET> withdrew the application to <$GUILD>.");
  config.runtime.guild_approved_notice_template =
      value_or<std::string>(server, "guild_approved_notice_template",
                            "Your application to <$GUILD> was approved.");
  config.runtime.guild_rejected_notice_template =
      value_or<std::string>(server, "guild_rejected_notice_template",
                            "Your application to <$GUILD> was rejected.");
  config.runtime.guild_removed_notice_template =
      value_or<std::string>(server, "guild_removed_notice_template",
                            "You were removed from guild <$GUILD>.");
  config.runtime.guild_new_lord_notice_template =
      value_or<std::string>(server, "guild_new_lord_notice_template",
                            "You are now the guild lord of <$GUILD>.");
  config.runtime.guild_title_changed_notice_template =
      value_or<std::string>(server, "guild_title_changed_notice_template",
                            "Your guild title is now <$TITLE>.");
  config.runtime.guild_create_leave_current_template = value_or<std::string>(
      server, "guild_create_leave_current_template",
      "Leave your current guild before creating a new one.");
  config.runtime.guild_create_choose_name_template =
      value_or<std::string>(server, "guild_create_choose_name_template", "Choose a guild name first.");
  config.runtime.guild_create_name_unavailable_template = value_or<std::string>(
      server, "guild_create_name_unavailable_template", "That guild already exists.");
  config.runtime.guild_create_need_gold_template = value_or<std::string>(
      server, "guild_create_need_gold_template", "You need <$GOLD> gold to found a guild.");
  config.runtime.guild_apply_leave_current_template = value_or<std::string>(
      server, "guild_apply_leave_current_template",
      "Leave your current guild before joining another.");
  config.runtime.guild_apply_choose_guild_template =
      value_or<std::string>(server, "guild_apply_choose_guild_template", "Choose a guild first.");
  config.runtime.guild_not_found_template =
      value_or<std::string>(server, "guild_not_found_template", "Guild not found.");
  config.runtime.guild_apply_already_pending_template = value_or<std::string>(
      server, "guild_apply_already_pending_template",
      "Your application to <$GUILD> is already pending.");
  config.runtime.guild_war_fee =
      value_or<std::int32_t>(server, "guild_war_fee", 30000);
  config.runtime.upgrade_weapon_fee =
      value_or<std::int32_t>(server, "upgrade_weapon_fee", 10000);
  config.runtime.guild_create_fee =
      value_or<std::int32_t>(server, "guild_create_fee", 10000);
  config.runtime.black_stone_name =
      value_or<std::string>(server, "black_stone_name", "BlackStone");
  config.runtime.legacy_user_full_count =
      value_or<std::int32_t>(server, "legacy_user_full_count", 500);
  config.runtime.legacy_zen_fast_step =
      value_or<std::int32_t>(server, "legacy_zen_fast_step", 300);
  if (auto seed = server["legacy_random_seed"].value<std::uint32_t>()) {
    config.runtime.legacy_random_seed = *seed;
  }

  // 读取 ports.toml —— 网关端口配置
  if (auto login = ports["login_gateway"].as_table()) {
    config.ports.login_gateway.address = value_or<std::string>(*login, "address", "127.0.0.1");
    config.ports.login_gateway.port = value_or<int>(*login, "port", 5500);
  }
  if (auto game = ports["game_gateway"].as_table()) {
    config.ports.game_gateway.address = value_or<std::string>(*game, "address", "127.0.0.1");
    config.ports.game_gateway.port = value_or<int>(*game, "port", 7000);
  }
  if (auto login = ports["client_v1_login_gateway"].as_table()) {
    config.ports.client_v1_login_gateway.address =
        value_or<std::string>(*login, "address", "127.0.0.1");
    config.ports.client_v1_login_gateway.port = value_or<int>(*login, "port", 5600);
  }
  if (auto game = ports["client_v1_game_gateway"].as_table()) {
    config.ports.client_v1_game_gateway.address =
        value_or<std::string>(*game, "address", "127.0.0.1");
    config.ports.client_v1_game_gateway.port = value_or<int>(*game, "port", 7100);
  }

  // 读取 runtime/logic.toml —— 逻辑预算配置
  config.budgets.tick_ms = value_or<int>(logic, "tick_ms", 10);
  config.budgets.player_budget_ms = value_or<int>(logic, "player_budget_ms", 30);
  config.budgets.player_input_budget_per_tick =
      std::max(0, value_or<int>(logic, "player_input_budget_per_tick", 0));
  config.budgets.monster_budget_ms = value_or<int>(logic, "monster_budget_ms", 30);
  config.budgets.spawn_budget_ms = value_or<int>(logic, "spawn_budget_ms", 30);
  config.budgets.npc_budget_ms = value_or<int>(logic, "npc_budget_ms", 5);
  config.budgets.net_flush_budget_ms = value_or<int>(logic, "net_flush_budget_ms", 30);

  // ---- 阶段二：加载游戏数据 ----
  load_maps(root / "maps", config, root);
  load_monsters(root / "monsters", config);
  load_spawns(root / "spawns", config);
  load_items(root / "items", config);
  load_magics(root / "magic", config);
  load_npcs(root / "npcs", config);
  load_map_quests(root / "map_quests", config);
  load_startup_quest(root, config);

  // ---- 阶段三：验证 ----
  if (config.maps.empty()) {
    throw std::runtime_error("No map configuration files were found.");
  }

  return config;
}

}  // namespace mir2
