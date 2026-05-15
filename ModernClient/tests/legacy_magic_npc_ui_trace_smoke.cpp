#include "scene/legacy_magic_npc_ui.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace mn = mir2::client::legacy_magic_npc_ui;

std::map<std::string, std::vector<std::string>> read_trace_sections(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  assert(input);

  std::map<std::string, std::vector<std::string>> sections;
  std::string current;
  std::string line;
  while (std::getline(input, line)) {
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

std::vector<std::string> labels(
    const std::vector<mn::LegacyMagicNpcUiTraceLabel>& trace) {
  std::vector<std::string> out;
  for (const auto label : trace) {
    out.emplace_back(mn::legacy_magic_npc_ui_trace_label(label));
  }
  return out;
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto sections = read_trace_sections(
      source_dir / "tests" / "golden" / "legacy_magic_npc_ui_expected_trace.txt");

  assert(labels({mn::LegacyMagicNpcUiTraceLabel::magic_page_controls_created,
                 mn::LegacyMagicNpcUiTraceLabel::magic_key_dialog_created,
                 mn::LegacyMagicNpcUiTraceLabel::magic_key_buttons_created}) ==
         sections.at("magic.window.initialize"));
  assert(labels({mn::LegacyMagicNpcUiTraceLabel::shortcut_open_magic_page,
                 mn::LegacyMagicNpcUiTraceLabel::show_state_window_magic_page,
                 mn::LegacyMagicNpcUiTraceLabel::magic_page_controls_visible,
                 mn::LegacyMagicNpcUiTraceLabel::draw_magic_page_rows}) ==
         sections.at("magic.page.open"));
  assert(labels({mn::LegacyMagicNpcUiTraceLabel::click_magic_row,
                 mn::LegacyMagicNpcUiTraceLabel::show_magic_key_modal,
                 mn::LegacyMagicNpcUiTraceLabel::select_magic_key,
                 mn::LegacyMagicNpcUiTraceLabel::send_clear_conflicting_magic_key,
                 mn::LegacyMagicNpcUiTraceLabel::send_assign_magic_key,
                 mn::LegacyMagicNpcUiTraceLabel::hide_magic_key_modal,
                 mn::LegacyMagicNpcUiTraceLabel::recv_magic_list_fifo,
                 mn::LegacyMagicNpcUiTraceLabel::refresh_magic_page}) ==
         sections.at("magic.key.assign"));
  assert(labels({mn::LegacyMagicNpcUiTraceLabel::click_npc_from_scene_when_ui_not_consumed,
                 mn::LegacyMagicNpcUiTraceLabel::send_npc_click_request,
                 mn::LegacyMagicNpcUiTraceLabel::recv_npc_dialog_fifo,
                 mn::LegacyMagicNpcUiTraceLabel::show_merchant_dialog,
                 mn::LegacyMagicNpcUiTraceLabel::parse_npc_links,
                 mn::LegacyMagicNpcUiTraceLabel::click_npc_link,
                 mn::LegacyMagicNpcUiTraceLabel::send_npc_dialog_select,
                 mn::LegacyMagicNpcUiTraceLabel::wait_server_refresh}) ==
         sections.at("npc.dialog.open_select"));
  assert(labels({mn::LegacyMagicNpcUiTraceLabel::close_npc_dialog_local,
                 mn::LegacyMagicNpcUiTraceLabel::hide_merchant_dialog,
                 mn::LegacyMagicNpcUiTraceLabel::restore_item_bag_origin,
                 mn::LegacyMagicNpcUiTraceLabel::recv_npc_close_fifo}) ==
         sections.at("npc.dialog.close"));
  assert(labels({mn::LegacyMagicNpcUiTraceLabel::recv_merchant_goods_fifo,
                 mn::LegacyMagicNpcUiTraceLabel::show_shop_menu,
                 mn::LegacyMagicNpcUiTraceLabel::move_item_bag_for_shop,
                 mn::LegacyMagicNpcUiTraceLabel::select_shop_row,
                 mn::LegacyMagicNpcUiTraceLabel::send_merchant_buy,
                 mn::LegacyMagicNpcUiTraceLabel::recv_inventory_or_gold_fifo,
                 mn::LegacyMagicNpcUiTraceLabel::refresh_shop_or_bag}) ==
         sections.at("merchant.shop"));
  assert(labels({mn::LegacyMagicNpcUiTraceLabel::open_sell_or_repair_selecting,
                 mn::LegacyMagicNpcUiTraceLabel::move_item_bag_for_shop,
                 mn::LegacyMagicNpcUiTraceLabel::select_bag_item_for_price,
                 mn::LegacyMagicNpcUiTraceLabel::send_price_request,
                 mn::LegacyMagicNpcUiTraceLabel::recv_price_result_fifo,
                 mn::LegacyMagicNpcUiTraceLabel::show_sell_dialog,
                 mn::LegacyMagicNpcUiTraceLabel::confirm_sell_or_repair,
                 mn::LegacyMagicNpcUiTraceLabel::send_sell_or_repair,
                 mn::LegacyMagicNpcUiTraceLabel::refresh_bag_or_gold_fifo}) ==
         sections.at("merchant.sell_repair"));
  assert(labels({mn::LegacyMagicNpcUiTraceLabel::recv_storage_list_fifo,
                 mn::LegacyMagicNpcUiTraceLabel::show_storage_menu,
                 mn::LegacyMagicNpcUiTraceLabel::select_storage_row,
                 mn::LegacyMagicNpcUiTraceLabel::send_storage_withdraw,
                 mn::LegacyMagicNpcUiTraceLabel::open_storage_deposit_selecting,
                 mn::LegacyMagicNpcUiTraceLabel::select_bag_item_for_storage,
                 mn::LegacyMagicNpcUiTraceLabel::send_storage_deposit,
                 mn::LegacyMagicNpcUiTraceLabel::recv_storage_list_fifo,
                 mn::LegacyMagicNpcUiTraceLabel::refresh_storage_or_bag}) ==
         sections.at("storage.window"));
  return 0;
}
