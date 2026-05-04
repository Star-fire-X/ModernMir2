#include "importer/legacy_importer.hpp"

#include <cctype>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>

#ifdef MIR2_ENABLE_ODBC
#include <windows.h>
#include <sqlext.h>
#endif

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

std::string quote(const std::string& value) {
  const auto normalized = ascii_safe(value);
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
  std::ifstream file(path, std::ios::binary);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(util::trim(line));
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
    } else if (lower == "dark") {
      info.darkness = true;
    } else if (util::starts_with(lower, "noreconnect")) {
      info.no_reconnect = true;
      info.back_map = option_payload(token);
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
    file << "tick_ms = 20\n";
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
    const auto tokens = split_ws(line);
    if (tokens.size() < 4) {
      continue;
    }
    if (!first) {
      spawns << ",\n";
    }
    first = false;
    spawns << "  { map_id = " << quote(tokens[0]) << ", actor_type = \"monster\", name = "
           << quote(tokens[3]) << ", x = " << tokens[1] << ", y = " << tokens[2]
           << ", area = " << (tokens.size() > 4 ? tokens[4] : "0")
           << ", count = " << (tokens.size() > 5 ? tokens[5] : "1")
           << ", zen_minutes = " << (tokens.size() > 6 ? tokens[6] : "1")
           << ", small_zen_rate = " << (tokens.size() > 7 ? tokens[7] : "0")
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
      const auto path = directory / candidate;
      if (std::filesystem::exists(path)) {
        return path;
      }
    }
  }
  return {};
}

bool is_ascii_text(std::string_view text) {
  return std::all_of(text.begin(), text.end(), [](unsigned char ch) { return ch >= 32 && ch <= 126; });
}

std::string import_npc_script_asset(const std::filesystem::path& legacy_root,
                                    const std::filesystem::path& output_root, std::string_view npc_id,
                                    std::string_view map_id) {
  if (!is_ascii_text(npc_id) || !is_ascii_text(map_id)) {
    return ascii_safe(std::string(npc_id) + ".txt");
  }
  try {
    const auto source = find_legacy_npc_script(legacy_root, npc_id, map_id);
    if (source.empty()) {
      return ascii_safe(std::string(npc_id) + ".txt");
    }

    const auto source_dir = util::lower_copy(source.parent_path().filename().string());
    const auto subdir = source_dir == "market_def" ? std::filesystem::path("market_def")
                                                   : std::filesystem::path("Npc_def");
    const auto target_name = ascii_safe(std::string(npc_id) + "-" + std::string(map_id)) +
                             source.extension().string();
    const auto relative = std::filesystem::path("npc_scripts") / subdir / target_name;
    std::filesystem::copy_file(source, output_root / relative,
                               std::filesystem::copy_options::overwrite_existing);
    return relative.generic_string();
  } catch (const std::exception&) {
    return ascii_safe(std::string(npc_id) + ".txt");
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
  const auto name = ascii_safe(std::filesystem::path(std::string(qfile)).filename().string());
  if (name.empty()) {
    return {};
  }
  const std::vector<std::filesystem::path> candidates = {
      legacy_root / "Envir" / "MapQuest_def" / name,
      legacy_root / "Envir" / "MapQuest_def" / (name + ".txt"),
      legacy_root / "Envir" / "MapQuest_def" / (name + ".TXT"),
  };
  for (const auto& source : candidates) {
    if (!std::filesystem::exists(source)) {
      continue;
    }
    const auto target = std::filesystem::path("npc_scripts") / "MapQuest_def" /
                        source.filename();
    try {
      std::filesystem::copy_file(source, output_root / target,
                                 std::filesystem::copy_options::overwrite_existing);
      return target.generic_string();
    } catch (const std::exception&) {
      return name;
    }
  }
  return name;
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

  for (const auto& line : read_lines(legacy_root / "Envir" / "MakeItem.txt")) {
    if (line.empty() || util::starts_with(line, ";")) {
      continue;
    }
    const auto tokens = split_ws(line);
    if (tokens.empty()) {
      continue;
    }
    if (!first) {
      items << ",\n";
    }
    first = false;
    items << "  { id = " << id++ << ", name = " << quote(tokens[0])
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
  if (!std::filesystem::exists(directory)) {
    drops << "\n]\n";
    return 0;
  }

  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (!entry.is_regular_file() ||
        util::lower_copy(entry.path().extension().string()) != ".txt") {
      continue;
    }
    const auto monster_name = entry.path().stem().string();
    for (const auto& line : read_lines(entry.path())) {
      if (line.empty() || util::starts_with(line, ";")) {
        continue;
      }
      const auto tokens = split_ws(line);
      if (tokens.size() < 2) {
        continue;
      }
      const auto slash = tokens[0].find('/');
      if (slash == std::string::npos) {
        continue;
      }
      const auto sel = parse_int32(tokens[0].substr(0, slash));
      const auto max = parse_int32(tokens[0].substr(slash + 1));
      if (!sel.has_value() || !max.has_value()) {
        continue;
      }
      if (!first) {
        drops << ",\n";
      }
      first = false;
      drops << "  { monster_name = " << quote(monster_name)
            << ", sel_point = " << (*sel - 1)
            << ", max_point = " << *max
            << ", item_name = " << quote(tokens[1])
            << ", count = " << (tokens.size() > 2 ? tokens[2] : "1") << " }";
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

}  // namespace

LegacyImportReport LegacyImporter::import_tree(const std::filesystem::path& legacy_root,
                                               const std::filesystem::path& output_root) const {
  ensure_output_dirs(output_root);
  const auto setup = parse_ini(legacy_root / "!SetUp.txt");

  write_server_files(setup, output_root);
  auto report = import_maps_and_spawns(legacy_root, output_root, setup);
  report.npc_count = import_npcs(legacy_root, output_root);
  report.map_quest_count = import_map_quests(legacy_root, output_root);
  report.item_count = import_items_from_makeitem(legacy_root, output_root);
  report.monster_drop_count = import_monitems(legacy_root, output_root);

#ifdef MIR2_ENABLE_ODBC
  report.item_count = import_table_as_toml(
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

  write_report(output_root, report);
  return report;
}

}  // namespace mir2
