#include "scene/legacy_inventory_ui.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace inv = mir2::client::legacy_inventory_ui;

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
    const std::vector<inv::LegacyInventoryUiTraceLabel>& trace) {
  std::vector<std::string> out;
  for (const auto label : trace) {
    out.emplace_back(inv::legacy_inventory_ui_trace_label(label));
  }
  return out;
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto sections = read_trace_sections(
      source_dir / "tests" / "golden" / "legacy_inventory_ui_expected_trace.txt");

  assert(labels({inv::LegacyInventoryUiTraceLabel::legacy_inventory_windows_created,
                 inv::LegacyInventoryUiTraceLabel::item_bag_window_created,
                 inv::LegacyInventoryUiTraceLabel::item_grid_created,
                 inv::LegacyInventoryUiTraceLabel::state_window_created,
                 inv::LegacyInventoryUiTraceLabel::equipment_slots_created,
                 inv::LegacyInventoryUiTraceLabel::item_hint_created,
                 inv::LegacyInventoryUiTraceLabel::moving_item_overlay_created}) ==
         sections.at("inventory.window.initialize"));
  assert(labels({inv::LegacyInventoryUiTraceLabel::show_item_bag,
                 inv::LegacyInventoryUiTraceLabel::arrange_item_bag_origin,
                 inv::LegacyInventoryUiTraceLabel::hide_item_bag,
                 inv::LegacyInventoryUiTraceLabel::show_state_window,
                 inv::LegacyInventoryUiTraceLabel::hide_state_window}) ==
         sections.at("inventory.window.toggle"));
  assert(labels({inv::LegacyInventoryUiTraceLabel::mouse_down_bag_cell,
                 inv::LegacyInventoryUiTraceLabel::start_item_moving_from_bag,
                 inv::LegacyInventoryUiTraceLabel::clear_source_bag_slot,
                 inv::LegacyInventoryUiTraceLabel::draw_moving_item_overlay,
                 inv::LegacyInventoryUiTraceLabel::cancel_moving_item,
                 inv::LegacyInventoryUiTraceLabel::restore_item_to_source_slot}) ==
         sections.at("inventory.bag.drag"));
  assert(labels({inv::LegacyInventoryUiTraceLabel::double_click_bag_cell,
                 inv::LegacyInventoryUiTraceLabel::begin_pending_use_item,
                 inv::LegacyInventoryUiTraceLabel::send_use_item,
                 inv::LegacyInventoryUiTraceLabel::recv_use_item_result_fifo,
                 inv::LegacyInventoryUiTraceLabel::clear_or_restore_pending_item}) ==
         sections.at("inventory.bag.use"));
  assert(labels({inv::LegacyInventoryUiTraceLabel::mouse_down_equipment_slot,
                 inv::LegacyInventoryUiTraceLabel::start_item_moving_from_equipment,
                 inv::LegacyInventoryUiTraceLabel::clear_source_equipment_slot,
                 inv::LegacyInventoryUiTraceLabel::drop_equipment_item_on_bag_cell,
                 inv::LegacyInventoryUiTraceLabel::send_takeoff_item,
                 inv::LegacyInventoryUiTraceLabel::server_equipment_message_fifo,
                 inv::LegacyInventoryUiTraceLabel::refresh_bag_or_equipment}) ==
         sections.at("inventory.equipment.drag"));
  assert(labels({inv::LegacyInventoryUiTraceLabel::drop_bag_item_on_equipment_slot,
                 inv::LegacyInventoryUiTraceLabel::validate_equipment_slot,
                 inv::LegacyInventoryUiTraceLabel::begin_pending_equip_item,
                 inv::LegacyInventoryUiTraceLabel::send_takeon_item,
                 inv::LegacyInventoryUiTraceLabel::server_equipment_message_fifo,
                 inv::LegacyInventoryUiTraceLabel::refresh_bag_or_equipment}) ==
         sections.at("inventory.equipment.equip"));
  assert(labels({inv::LegacyInventoryUiTraceLabel::hover_bag_or_equipment_item,
                 inv::LegacyInventoryUiTraceLabel::format_item_hint_backslash_lines,
                 inv::LegacyInventoryUiTraceLabel::show_hint_layer,
                 inv::LegacyInventoryUiTraceLabel::hide_hint_when_item_missing_or_moving}) ==
         sections.at("inventory.tooltip"));
  assert(labels({inv::LegacyInventoryUiTraceLabel::recv_inventory_message_fifo,
                 inv::LegacyInventoryUiTraceLabel::refresh_bag_or_equipment,
                 inv::LegacyInventoryUiTraceLabel::refresh_gold_or_attributes,
                 inv::LegacyInventoryUiTraceLabel::append_system_message}) ==
         sections.at("inventory.protocol_fifo"));
  return 0;
}
