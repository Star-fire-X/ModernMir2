#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  return std::string(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
}

std::map<std::string, std::vector<std::string>> read_trace_sections(
    const std::filesystem::path& path) {
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

void test_crlf_trace_sections_are_normalized() {
  const auto path = std::filesystem::temp_directory_path() / "legacy_ui_expected_trace_crlf.txt";
  {
    std::ofstream output(path, std::ios::binary);
    output << "[ui.frame.legacy_order]\r\nnetwork_drain\r\npresent\r\n";
  }
  const auto sections = read_trace_sections(path);
  std::filesystem::remove(path);
  assert(sections.at("ui.frame.legacy_order").front() == "network_drain");
  assert(sections.at("ui.frame.legacy_order").back() == "present");
}

}  // namespace

int main() {
  test_crlf_trace_sections_are_normalized();

  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto golden_dir = source_dir / "tests" / "golden";
  const auto trace_path = golden_dir / "legacy_ui_expected_trace.txt";
  const auto scene_trace_path = golden_dir / "legacy_scene_management_expected_trace.txt";
  const auto doc_path = source_dir / "docs" / "legacy_ui_pr1_semantics.md";

  assert(std::filesystem::exists(trace_path));
  assert(std::filesystem::exists(scene_trace_path));
  assert(std::filesystem::exists(doc_path));

  const auto sections = read_trace_sections(trace_path);
  const std::vector<std::string> required_sections{
      "ui.frame.legacy_order",
      "ui.input.mouse_down",
      "ui.input.mouse_move",
      "ui.input.mouse_up",
      "ui.input.keyboard",
      "ui.paint.layers",
      "ui.window.lifecycle",
      "ui.business.trade",
      "ui.business.npc",
      "ui.business.inventory",
  };
  for (const auto& section : required_sections) {
    const auto found = sections.find(section);
    assert(found != sections.end());
    assert(found->second.size() >= 3U);
  }

  assert(sections.at("ui.frame.legacy_order").front() == "network_drain");
  assert(sections.at("ui.frame.legacy_order").back() == "present");
  assert(sections.at("ui.paint.layers").back() == "present");

  const auto doc = read_file(doc_path);
  assert(doc.find("待源码核对") != std::string::npos);
  assert(doc.find("ModernClient/src/game/scenes.cpp") == std::string::npos);
  assert(doc.find("ModernClient/src/input") == std::string::npos);
  return 0;
}
