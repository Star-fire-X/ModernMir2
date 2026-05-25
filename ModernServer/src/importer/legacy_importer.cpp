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

using IniSection = std::unordered_map<std::string, std::string>;
using IniFile = std::unordered_map<std::string, IniSection>;

std::string ascii_safe(std::string value) {
  for (char& ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 32 || byte > 126) {
      ch = '_';
    }
  }
  return value;
}

std::string control_safe(std::string value) {
  for (char& ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 32 || byte == 127) {
      ch = '_';
    }
  }
  return value;
}

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

std::vector<std::string> read_lines(const std::filesystem::path& path) {
  std::vector<std::string> lines;
  for (auto line : util::read_legacy_text_lines(path)) {
    lines.push_back(util::trim(std::move(line)));
  }
  return lines;
}

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

std::vector<std::string> split_ws(const std::string& line) {
  std::istringstream stream(line);
  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

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

std::string legacy_name_key(std::string name) {
  return util::lower_copy(ascii_safe(std::move(name)));
}

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

struct LegacyMonGenRecord {
  std::string map_id;
  std::string x;
  std::string y;
  std::string name;
  std::string area = "0";
  std::string count = "1";
  std::string zen_minutes = "1";
  std::string small_zen_rate = "0";
};

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

std::set<std::string> load_legacy_monitem_monster_names(const std::filesystem::path& legacy_root) {
  std::set<std::string> names;
  for (const auto& name : load_legacy_monitem_monster_name_values(legacy_root)) {
    names.insert(legacy_name_key(name));
  }
  return names;
}

std::filesystem::path first_existing_path(std::initializer_list<std::filesystem::path> paths) {
  for (const auto& path : paths) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }
  return {};
}

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

struct ImportedGate {
  std::int32_t x{0};
  std::int32_t y{0};
  std::string target_map_id{};
  std::int32_t target_x{0};
  std::int32_t target_y{0};
};

struct ImportedMapInfo {
  std::string id{};
  std::string title{};
  bool law_full{false};
  bool fight_zone{false};
  bool fight3_zone{false};
  bool daylight{false};
  bool darkness{false};
  bool no_reconnect{false};
  bool need_hole{false};
  bool no_recall{false};
  bool no_random_move{false};
  bool no_drug{false};
  bool no_position_move{false};
  std::int32_t need_level{0};
  std::int32_t mine_map{0};
  std::string back_map{};
  std::vector<ImportedGate> gates{};
  bool quiz_zone{false};
  std::string check_quest{};
  std::int32_t need_set_number{-1};
  std::int32_t need_set_value{-1};
};

void apply_mapinfo_flags(ImportedMapInfo& info, const std::string& tail) {
  for (auto token : split_ws(tail)) {
    token = util::trim(std::move(token));
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

std::string import_mapquest_script_asset(const std::filesystem::path& legacy_root,
                                         const std::filesystem::path& output_root,
                                         std::string_view qfile);

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

LegacyImportReport import_maps_and_spawns(const std::filesystem::path& legacy_root,
                                          const std::filesystem::path& output_root, const IniFile& setup) {
  LegacyImportReport report;
  std::set<std::string> maps;

  std::string home_map = "0";
  std::string home_x = "330";
  std::string home_y = "270";
  if (setup.contains("Setup")) {
    const auto& section = setup.at("Setup");
    if (section.contains("HomeMap")) home_map = section.at("HomeMap");
    if (section.contains("HomeX")) home_x = section.at("HomeX");
    if (section.contains("HomeY")) home_y = section.at("HomeY");
  }
  maps.insert(home_map);

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

    const auto open = line.find('[');
    const auto close = line.find(']', open == std::string::npos ? 0 : open + 1);
    if (open != std::string::npos && close != std::string::npos && close > open + 1) {
      const auto header = line.substr(open + 1, close - open - 1);
      const auto tokens = split_ws(header);
      if (!tokens.empty()) {
        const auto map_id = tokens.front();
        ImportedMapInfo info;
        info.id = map_id;
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

    const auto tokens = split_ws(line);
    if (tokens.size() >= 7 && tokens[3] == "->") {
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
    }
  }

  for (const auto& [map_id, points] : start_points) {
    if (!map_infos.contains(map_id)) {
      ImportedMapInfo info;
      info.id = map_id;
      info.title = map_id;
      map_infos[map_id] = std::move(info);
      map_order.push_back(map_id);
    }
  }

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
    file << "allow_pk = " << (info.law_full ? "false" : "true") << "\n";
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

  if (report.map_count == 0) {
    std::ofstream file(output_root / "maps" / (home_map + ".toml"), std::ios::binary | std::ios::trunc);
    file << "id = " << quote(home_map) << "\n";
    file << "title = " << quote("ImportedHome") << "\n";
    file << "source_map = " << quote((legacy_root / "Map" / (home_map + ".map")).string()) << "\n";
    file << "width = 0\n";
    file << "height = 0\n";
    file << "home_x = " << home_x << "\n";
    file << "home_y = " << home_y << "\n";
    report.map_count = 1;
  }

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

std::size_t import_npcs(const std::filesystem::path& legacy_root, const std::filesystem::path& output_root) {
  std::ofstream npcs(output_root / "npcs" / "imported_npcs.toml", std::ios::binary | std::ios::trunc);
  npcs << "npcs = [\n";
  bool first = true;
  std::size_t count = 0;

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
    file << "  { map_id = " << quote(tokens[0])
         << ", set_number = " << *set_number
         << ", value = " << *value
         << ", monster_name = " << quote(tokens[3] == "*" ? std::string{} : tokens[3])
         << ", item_name = " << quote(tokens[4] == "*" ? std::string{} : tokens[4])
         << ", qfile = " << quote(qfile)
         << ", enable_group = " << (group ? "true" : "false") << " }";
    ++count;
  }
  file << "\n]\n";
  return count;
}

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
  ensure_output_dirs(output_root);
  const auto setup = parse_ini(legacy_root / "!SetUp.txt");

  write_server_files(setup, output_root);
  copy_legacy_script_tree(legacy_root, output_root, "Defines", "Defines");
  copy_legacy_script_tree(legacy_root, output_root, "QuestDiary", "QuestDiary");
  LegacyImportReport report;
  report.npc_count = import_npcs(legacy_root, output_root);
  report.map_quest_count = import_map_quests(legacy_root, output_root);
  report.item_count = import_items_from_makeitem(legacy_root, output_root);

#ifdef MIR2_ENABLE_ODBC
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
    report.item_count = import_items_from_makeitem(legacy_root, output_root);
    report.warnings.push_back(
        "ODBC StdItems import returned no rows, MakeItem.txt fallback items were used.");
  }
  report.magic_count = import_table_as_toml(legacy_root / "Data.mdb", output_root / "magic" / "imported_magic.toml",
                                            "SELECT ID, Name, DefSpell, DefPower FROM Magic", "magic",
                                            {"ID", "Name", "DefSpell", "DefPower"},
                                            {"id", "name", "mp_cost", "power"}, report.warnings);
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
    report.monster_count = write_fallback_monster_defs(legacy_root, output_root);
    report.warnings.push_back(
        "ODBC Monster import returned no rows, MonItems monster names were used as placeholder monster definitions.");
  }
#else
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

  auto map_report = import_maps_and_spawns(legacy_root, output_root, setup);
  report.map_count = map_report.map_count;
  report.spawn_count = map_report.spawn_count;
  report.warnings.insert(report.warnings.end(), map_report.warnings.begin(),
                         map_report.warnings.end());
  report.monster_drop_count = import_monitems(legacy_root, output_root);

  write_report(output_root, report);
  return report;
}

}  // namespace mir2
