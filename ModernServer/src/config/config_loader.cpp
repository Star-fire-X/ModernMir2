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
#include "util/string_utils.hpp"

namespace mir2 {

namespace {

toml::table parse_file_checked(const std::filesystem::path& path) {
  try {
    return toml::parse_file(path.string());
  } catch (const std::exception& ex) {
    throw std::runtime_error("Failed to parse TOML file '" + path.string() + "': " + ex.what());
  }
}

template <typename T>
T value_or(const toml::table& table, const std::string& key, T fallback) {
  if (auto value = table[key].value<T>()) {
    return *value;
  }
  return fallback;
}

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

std::filesystem::path path_or(const toml::table& table, const std::string& key,
                              const std::filesystem::path& fallback) {
  if (auto value = table[key].value<std::string>()) {
    return std::filesystem::path(*value);
  }
  return fallback;
}

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

bool contains_any(std::string_view text, std::initializer_list<std::string_view> needles) {
  for (const auto needle : needles) {
    if (text.find(needle) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

bool looks_like_merchant_code(std::string_view text) {
  if (text.size() < 3 || text.front() < '1' || text.front() > '8') {
    return false;
  }
  return contains_any(text, {"me", "we", "dr", "du", "dm", "st", "wh", "bo", "ri", "br", "ne", "ac"});
}

std::vector<std::string> read_text_lines(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

std::string strip_utf8_bom(std::string line) {
  if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xef &&
      static_cast<unsigned char>(line[1]) == 0xbb &&
      static_cast<unsigned char>(line[2]) == 0xbf) {
    line.erase(0, 3);
  }
  return line;
}

std::string upper_copy(std::string_view text) {
  std::string upper{text};
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return upper;
}

std::string first_token(std::string_view text) {
  std::istringstream stream{std::string(text)};
  std::string token;
  stream >> token;
  return token;
}

std::vector<std::string> split_tokens(std::string_view text) {
  std::istringstream stream{std::string(text)};
  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

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

std::string normalize_dialog_text(const std::vector<std::string>& lines) {
  std::string text;
  for (auto line : lines) {
    line = strip_utf8_bom(std::move(line));
    line = util::trim(std::move(line));
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    if (!text.empty() && text.back() != '\\') {
      text.push_back('\\');
    }
    text += line;
  }
  return text;
}

struct LegacyNpcScriptParseResult {
  std::vector<NpcDialogSectionConfig> dialog_sections{};
  std::optional<std::int32_t> price_rate_percent{};
  std::vector<std::int32_t> deal_std_modes{};
  std::vector<MerchantProductConfig> merchant_products{};
};

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

std::optional<std::int32_t> parse_first_int32(std::string_view value) {
  std::istringstream stream{std::string(value)};
  std::string token;
  if (!(stream >> token)) {
    return std::nullopt;
  }
  return parse_int32_token(token);
}

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

std::filesystem::path resolve_legacy_include_path(const std::filesystem::path& current_path,
                                                  std::string_view raw_name) {
  auto name = std::string(raw_name);
  if (name.size() >= 2 && name.front() == '[' && name.back() == ']') {
    name = name.substr(1, name.size() - 2);
  }
  const auto requested = std::filesystem::path(util::trim(std::move(name)));
  if (requested.empty()) {
    return {};
  }
  if (requested.is_absolute() && std::filesystem::exists(requested)) {
    return requested;
  }
  const auto base = current_path.parent_path();
  const auto root = base.parent_path();
  const std::vector<std::filesystem::path> candidates{
      base / requested,
      root / requested,
      root / "Defines" / requested,
      root / "QuestDiary" / requested,
      root / "Npc_def" / requested,
      root / "market_def" / requested,
  };
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

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

std::vector<std::string> preprocess_legacy_script_lines(const std::filesystem::path& path,
                                                        std::int32_t depth = 0) {
  if (depth > 8 || !std::filesystem::exists(path)) {
    return {};
  }

  std::vector<std::string> output;
  std::unordered_map<std::string, std::string> defines;
  auto set_define = [&](std::string name, std::string value) {
    name = upper_copy(util::trim(std::move(name)));
    if (!name.empty()) {
      defines[std::move(name)] = util::trim(std::move(value));
    }
  };

  auto collect_defines = [&](auto&& self, const std::filesystem::path& define_path,
                             std::int32_t define_depth) -> void {
    if (define_depth > 8 || !std::filesystem::exists(define_path)) {
      return;
    }
    for (auto line : read_text_lines(define_path)) {
      line = strip_utf8_bom(std::move(line));
      const auto trimmed = util::trim(line);
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

  for (auto line : read_text_lines(path)) {
    line = strip_utf8_bom(std::move(line));
    const auto trimmed = util::trim(line);
    const auto upper = upper_copy(trimmed);
    if (util::starts_with(upper, "#SETHOME")) {
      const auto marker = first_token(trimmed);
      const auto value_pos = trimmed.find(marker);
      const auto value = value_pos != std::string::npos
                             ? util::trim(trimmed.substr(value_pos + marker.size()))
                             : std::string{};
      set_define("@HOME", value);
      continue;
    }
    if (util::starts_with(upper, "#DEFINE")) {
      auto tokens = split_tokens(trimmed);
      if (tokens.size() >= 3) {
        const auto value_pos = trimmed.find(tokens[2]);
        set_define(tokens[1], value_pos != std::string::npos ? trimmed.substr(value_pos) : tokens[2]);
      }
      continue;
    }
    if (util::starts_with(upper, "#INCLUDE")) {
      auto tokens = split_tokens(trimmed);
      if (tokens.size() >= 2) {
        collect_defines(collect_defines, resolve_legacy_include_path(path, tokens[1]), depth + 1);
      }
      continue;
    }
    if (util::starts_with(upper, "#CALL")) {
      const auto open = trimmed.find('[');
      const auto close = trimmed.find(']', open == std::string::npos ? 0 : open + 1);
      if (open != std::string::npos && close != std::string::npos) {
        const auto file_name = trimmed.substr(open + 1, close - open - 1);
        const auto section = util::trim(trimmed.substr(close + 1));
        const auto call_path = resolve_legacy_include_path(path, file_name);
        auto called_lines = extract_legacy_section(call_path, section);
        output.push_back("#ACT");
        output.push_back("GOTO " + section);
        output.insert(output.end(), std::make_move_iterator(called_lines.begin()),
                      std::make_move_iterator(called_lines.end()));
        continue;
      }
    }
    output.push_back(std::move(line));
  }

  for (auto& line : output) {
    for (const auto& [name, value] : defines) {
      line = replace_case_insensitive(std::move(line), name, value);
    }
  }
  return output;
}

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
      it->text = std::move(section.text);
    }
  }
}

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
      it->text = std::move(section.text);
    }
  }
}

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

LegacyNpcScriptParseResult parse_legacy_npc_script(const std::filesystem::path& path) {
  LegacyNpcScriptParseResult result;
  if (!std::filesystem::exists(path)) {
    return result;
  }

  std::string current_action;
  std::vector<std::string> current_lines;
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

  for (auto line : preprocess_legacy_script_lines(path)) {
    line = strip_utf8_bom(std::move(line));
    const auto trimmed = util::trim(line);
    if (trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']') {
      flush();
      current_action = normalize_action(trimmed.substr(1, trimmed.size() - 2));
      continue;
    }
    const auto current_action_key = normalize_action(current_action);
    if (current_action.empty()) {
      if (!trimmed.empty() && trimmed.front() == '%') {
        if (const auto rate = parse_first_int32(std::string_view(trimmed).substr(1));
            rate.has_value()) {
          result.price_rate_percent = *rate;
        }
      } else if (!trimmed.empty() && trimmed.front() == '+') {
        if (const auto std_mode = parse_first_int32(std::string_view(trimmed).substr(1));
            std_mode.has_value()) {
          result.deal_std_modes.push_back(*std_mode);
        }
      }
      continue;
    }
    if (current_action_key == "goods") {
      if (!trimmed.empty() && !util::starts_with(trimmed, ";")) {
        if (auto product = parse_merchant_product_line(trimmed); product.has_value()) {
          result.merchant_products.push_back(std::move(*product));
        }
      }
      continue;
    }
    if (!current_action.empty()) {
      current_lines.push_back(line);
    }
  }
  flush();
  return result;
}

std::vector<NpcDialogSectionConfig> parse_npc_dialog_script(const std::filesystem::path& path) {
  return parse_legacy_npc_script(path).dialog_sections;
}

std::filesystem::path with_map_suffix(const std::filesystem::path& path, const std::string& map_id) {
  if (path.empty()) {
    return {};
  }
  return path.parent_path() / (path.stem().string() + "-" + map_id + path.extension().string());
}

std::filesystem::path resolve_npc_script_path(const std::filesystem::path& root, const NpcConfig& npc) {
  if (npc.script.empty()) {
    return {};
  }

  const auto script_path = std::filesystem::path(npc.script);
  const auto map_specific = with_map_suffix(script_path, npc.map_id);
  const auto filename = script_path.filename();
  const auto map_specific_filename = map_specific.filename();

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

std::filesystem::path resolve_map_quest_script_path(const std::filesystem::path& root,
                                                    const MapQuestConfig& quest) {
  if (quest.qfile.empty()) {
    return {};
  }

  const auto script_path = std::filesystem::path(quest.qfile);
  const auto map_specific = with_map_suffix(script_path, quest.map_id);
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

void load_maps(const std::filesystem::path& directory, HostConfig& config,
               const std::filesystem::path& config_root) {
  if (!std::filesystem::exists(directory)) {
    return;
  }

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
    map.allow_pk = value_or<bool>(table, "allow_pk", !map.law_full);
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
        if (spawn.zen_time_ms == 0) {
          const auto zen_minutes = value_or<int>(spawn_table, "zen_minutes", 0);
          if (zen_minutes > 0) {
            spawn.zen_time_ms = static_cast<std::uint32_t>(zen_minutes * 60000);
          }
        }
        spawn.small_zen_rate =
            std::clamp(value_or<int>(spawn_table, "small_zen_rate", 0), 0, 100);
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

void load_monsters(const std::filesystem::path& directory, HostConfig& config) {
  if (!std::filesystem::exists(directory)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
      continue;
    }
    const auto table = parse_file_checked(entry.path());
    if (auto monsters = table["monsters"].as_array()) {
      for (const auto& node : *monsters) {
        if (!node.is_table()) {
          continue;
        }
        const auto& monster_table = *node.as_table();
        MonsterDefConfig monster;
        monster.name = value_or<std::string>(monster_table, "name", {});
        monster.race_server = value_or<int>(monster_table, "race_server",
                                            value_or<int>(monster_table, "race", 0));
        monster.race_image = value_or<int>(monster_table, "race_image",
                                           value_or<int>(monster_table, "race_img", 0));
        monster.appearance = value_or<int>(monster_table, "appearance",
                                           value_or<int>(monster_table, "img_index", 0));
        monster.level = value_or<int>(monster_table, "level", value_or<int>(monster_table, "lv", 1));
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
        monster.walk_speed_ms = std::max(monster.walk_speed_ms, 200);
        monster.walk_step = value_or<int>(monster_table, "walk_step", monster.walk_step);
        monster.walk_wait_ms =
            value_or<int>(monster_table, "walk_wait_ms",
                          value_or<int>(monster_table, "walk_wait", monster.walk_wait_ms));
        monster.attack_speed_ms =
            value_or<int>(monster_table, "attack_speed_ms",
                          value_or<int>(monster_table, "attack_spd", monster.attack_speed_ms));
        monster.attack_speed_ms = std::max(monster.attack_speed_ms, 200);
        monster.ai_profile =
            monster_ai_profile_or(monster_table, "ai_profile", monster.ai_profile);
        if (!monster.name.empty()) {
          config.monsters.push_back(std::move(monster));
        }
      }
    }

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
        item.looks = value_or<int>(item_table, "looks", item.id);
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
        if (auto goods = npc_table["merchant_goods"].as_array()) {
          for (const auto& node : *goods) {
            if (auto item_id = node.value<int>()) {
              npc.merchant_goods.push_back(*item_id);
            }
          }
        }
        if (auto deal_std_modes = npc_table["legacy_deal_std_modes"].as_array()) {
          for (const auto& node : *deal_std_modes) {
            if (auto std_mode = node.value<int>()) {
              npc.legacy_deal_std_modes.push_back(*std_mode);
            }
          }
        }
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
        if (const auto script_path = resolve_npc_script_path(root, npc); !script_path.empty()) {
          merge_legacy_npc_script(npc, parse_legacy_npc_script(script_path));
        }
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
        quest.value = value_or<int>(quest_table, "value", 0);
        quest.monster_name = value_or<std::string>(quest_table, "monster_name", {});
        if (quest.monster_name.empty()) {
          quest.monster_name = value_or<std::string>(quest_table, "mon_name", {});
        }
        quest.item_name = value_or<std::string>(quest_table, "item_name", {});
        quest.qfile = value_or<std::string>(quest_table, "qfile", {});
        quest.enable_group = value_or<bool>(quest_table, "enable_group", false);
        if (auto dialog_sections = quest_table["dialog_sections"].as_array()) {
          for (const auto& section_node : *dialog_sections) {
            if (!section_node.is_table()) {
              continue;
            }
            const auto& section_table = *section_node.as_table();
            auto action = value_or<std::string>(section_table, "action", {});
            auto text = value_or<std::string>(section_table, "text", {});
            action = util::lower_copy(util::trim(std::move(action)));
            if (!action.empty() && !text.empty()) {
              quest.dialog_sections.push_back({std::move(action), std::move(text)});
            }
          }
        }
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

}  // namespace

HostConfig ConfigLoader::load(const std::filesystem::path& root) const {
  HostConfig config;

  const auto server = parse_file_checked(root / "server.toml");
  const auto ports = parse_file_checked(root / "ports.toml");
  const auto logic = parse_file_checked(root / "runtime" / "logic.toml");

  config.runtime.log_dir = path_or(server, "log_dir", "logs");
  config.runtime.data_dir = path_or(server, "data_dir", "data");
  config.runtime.asset_root = path_or(server, "asset_root", root / ".." / ".." / "Legend of Mir");
  config.runtime.legacy_admin_list = path_or(server, "legacy_admin_list", "Envir/AdminList.txt");
  config.runtime.status_file = path_or(server, "status_file", "runtime/status.json");
  config.runtime.default_queue_capacity =
      value_or<std::size_t>(server, "default_queue_capacity", 4096);
  config.runtime.io_threads = value_or<std::size_t>(server, "io_threads", 2);
  config.runtime.enable_legacy_gateways = value_or<bool>(server, "enable_legacy_gateways", true);
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

  config.budgets.tick_ms = value_or<int>(logic, "tick_ms", 20);
  config.budgets.player_budget_ms = value_or<int>(logic, "player_budget_ms", 30);
  config.budgets.player_input_budget_per_tick =
      std::max(1, value_or<int>(logic, "player_input_budget_per_tick", 1));
  config.budgets.monster_budget_ms = value_or<int>(logic, "monster_budget_ms", 30);
  config.budgets.spawn_budget_ms = value_or<int>(logic, "spawn_budget_ms", 30);
  config.budgets.npc_budget_ms = value_or<int>(logic, "npc_budget_ms", 5);
  config.budgets.net_flush_budget_ms = value_or<int>(logic, "net_flush_budget_ms", 30);

  load_maps(root / "maps", config, root);
  load_monsters(root / "monsters", config);
  load_spawns(root / "spawns", config);
  load_items(root / "items", config);
  load_magics(root / "magic", config);
  load_npcs(root / "npcs", config);
  load_map_quests(root / "map_quests", config);

  if (config.maps.empty()) {
    throw std::runtime_error("No map configuration files were found.");
  }

  return config;
}

}  // namespace mir2
