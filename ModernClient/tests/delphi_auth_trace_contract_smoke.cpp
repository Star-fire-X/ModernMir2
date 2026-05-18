#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

std::map<std::string, std::vector<std::string>> read_sections(const std::filesystem::path& path) {
  std::ifstream input(path);
  assert(input);

  std::map<std::string, std::vector<std::string>> sections;
  std::string current;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      current = line.substr(1, line.size() - 2);
      sections[current] = {};
      continue;
    }
    assert(!current.empty());
    sections[current].push_back(line);
  }
  return sections;
}

std::string field_value(const std::string& line, const std::string& key) {
  const auto prefix = key + "=";
  auto pos = line.find(prefix);
  if (pos == std::string::npos) {
    return {};
  }
  pos += prefix.size();
  const auto end = line.find('|', pos);
  if (end == std::string::npos) {
    return line.substr(pos);
  }
  return line.substr(pos, end - pos);
}

std::vector<std::string> read_normalized_events(const std::filesystem::path& path) {
  std::ifstream input(path);
  assert(input);

  std::vector<std::string> events;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }
    assert(line.find("tick=") == std::string::npos);
    const auto event = field_value(line, "event");
    assert(!event.empty());
    events.push_back(event);
  }
  return events;
}

bool is_subsequence(const std::vector<std::string>& expected,
                    const std::vector<std::string>& actual) {
  auto it = actual.begin();
  for (const auto& label : expected) {
    it = std::find(it, actual.end(), label);
    if (it == actual.end()) {
      return false;
    }
    ++it;
  }
  return true;
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto contract =
      read_sections(source_dir / "tests" / "golden" / "delphi_auth_trace_contract.txt");

  const std::set<std::string> required_sections{
      "startup_only",
      "login_success",
      "login_failure_bad_password",
      "select_server_success",
      "select_server_failure",
      "select_character_failure",
      "login_notice_accept",
      "disconnect_login",
      "disconnect_select_character",
      "disconnect_play",
  };

  for (const auto& section : required_sections) {
    assert(contract.contains(section));
    assert(!contract.at(section).empty());
    for (const auto& label : contract.at(section)) {
      assert(label.find('|') == std::string::npos);
      assert(label.find('#') == std::string::npos);
      assert(label.find("client_v1") == std::string::npos);
    }
  }

  const auto fixture_dir = source_dir / "tests" / "golden" / "delphi_auth";
  for (const auto& section : required_sections) {
    const auto fixture = fixture_dir / (section + ".trace");
    if (!std::filesystem::exists(fixture)) {
      continue;
    }
    const auto events = read_normalized_events(fixture);
    assert(is_subsequence(contract.at(section), events));
  }

  return 0;
}
