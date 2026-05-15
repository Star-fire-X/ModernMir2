#include "scene/legacy_auth_ui.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace auth = mir2::client::legacy_auth_ui;

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

std::vector<std::string> labels(const std::vector<auth::LegacyAuthUiTraceLabel>& trace) {
  std::vector<std::string> out;
  for (const auto label : trace) {
    out.emplace_back(auth::legacy_auth_ui_trace_label(label));
  }
  return out;
}

void test_login_trace(const std::map<std::string, std::vector<std::string>>& sections) {
  assert(labels({auth::LegacyAuthUiTraceLabel::account_enter_focus_password,
                 auth::LegacyAuthUiTraceLabel::password_enter_send_login,
                 auth::LegacyAuthUiTraceLabel::send_login,
                 auth::LegacyAuthUiTraceLabel::recv_login_success,
                 auth::LegacyAuthUiTraceLabel::recv_server_list,
                 auth::LegacyAuthUiTraceLabel::show_server_select}) ==
         sections.at("auth.login.submit"));
}

void test_server_select_trace(const std::map<std::string, std::vector<std::string>>& sections) {
  assert(labels({auth::LegacyAuthUiTraceLabel::click_server_select,
                 auth::LegacyAuthUiTraceLabel::send_select_server,
                 auth::LegacyAuthUiTraceLabel::recv_select_server_ok,
                 auth::LegacyAuthUiTraceLabel::connect_character_gateway,
                 auth::LegacyAuthUiTraceLabel::send_query_character,
                 auth::LegacyAuthUiTraceLabel::recv_query_character,
                 auth::LegacyAuthUiTraceLabel::show_character_select}) ==
         sections.at("auth.server_select.to_character_select"));
}

void test_character_start_trace(const std::map<std::string, std::vector<std::string>>& sections) {
  assert(labels({auth::LegacyAuthUiTraceLabel::select_character_slot,
                 auth::LegacyAuthUiTraceLabel::click_start_character,
                 auth::LegacyAuthUiTraceLabel::send_select_character,
                 auth::LegacyAuthUiTraceLabel::recv_start_play,
                 auth::LegacyAuthUiTraceLabel::connect_game_gateway,
                 auth::LegacyAuthUiTraceLabel::show_login_notice_or_loading}) ==
         sections.at("auth.character_select.start"));
}

void test_character_create_trace(const std::map<std::string, std::vector<std::string>>& sections) {
  assert(labels({auth::LegacyAuthUiTraceLabel::open_create_character_dialog,
                 auth::LegacyAuthUiTraceLabel::focus_create_character_name,
                 auth::LegacyAuthUiTraceLabel::send_new_character,
                 auth::LegacyAuthUiTraceLabel::recv_new_character_success,
                 auth::LegacyAuthUiTraceLabel::send_query_character,
                 auth::LegacyAuthUiTraceLabel::recv_query_character,
                 auth::LegacyAuthUiTraceLabel::refresh_character_slots}) ==
         sections.at("auth.character_create.success"));
}

void test_character_delete_trace(const std::map<std::string, std::vector<std::string>>& sections) {
  assert(labels({auth::LegacyAuthUiTraceLabel::click_delete_character,
                 auth::LegacyAuthUiTraceLabel::confirm_delete_character,
                 auth::LegacyAuthUiTraceLabel::send_delete_character,
                 auth::LegacyAuthUiTraceLabel::recv_delete_character_success,
                 auth::LegacyAuthUiTraceLabel::send_query_character,
                 auth::LegacyAuthUiTraceLabel::recv_query_character,
                 auth::LegacyAuthUiTraceLabel::refresh_character_slots}) ==
         sections.at("auth.character_delete.success"));
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto sections =
      read_trace_sections(source_dir / "tests" / "golden" / "legacy_auth_ui_expected_trace.txt");

  test_login_trace(sections);
  test_server_select_trace(sections);
  test_character_start_trace(sections);
  test_character_create_trace(sections);
  test_character_delete_trace(sections);
  return 0;
}
