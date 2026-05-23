#include "render/software_renderer.hpp"
#include "scene/scenes.hpp"
#include "shared/legacy/map_render_order.hpp"
#include "ui/legacy_ui.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::map<std::string, std::vector<std::string>> read_trace_sections() {
  const auto path = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR} / "tests" / "golden" /
                    "client_migration_pr1_expected_trace.txt";
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

std::vector<std::string> run_render_order_trace() {
  using mir2::client::ui::LegacyUiPaintTraceLabel;
  using mir2::client::ui::legacy_ui_paint_layer_label;
  using mir2::legacy::LegacyMapDrawLayer;
  using mir2::legacy::legacy_map_draw_layer_name;

  std::vector<std::string> trace;
  const std::vector<LegacyMapDrawLayer> base_layers{
      LegacyMapDrawLayer::background_tiles,
      LegacyMapDrawLayer::middle_tiles,
      LegacyMapDrawLayer::small_objects,
      LegacyMapDrawLayer::ground_effects,
  };
  for (const auto layer : base_layers) {
    trace.emplace_back(legacy_map_draw_layer_name(layer));
  }
  for (const auto layer : mir2::legacy::kLegacyMapRowDrawOrder) {
    trace.emplace_back(legacy_map_draw_layer_name(layer));
  }
  trace.emplace_back(legacy_map_draw_layer_name(LegacyMapDrawLayer::actor_overlay));
  trace.emplace_back(legacy_map_draw_layer_name(LegacyMapDrawLayer::selection_blend));
  trace.emplace_back(legacy_map_draw_layer_name(LegacyMapDrawLayer::debug_overlay));
  trace.emplace_back(legacy_map_draw_layer_name(LegacyMapDrawLayer::overlay_effects));
  trace.emplace_back(legacy_map_draw_layer_name(LegacyMapDrawLayer::actor_screen_overlay));
  trace.emplace_back(legacy_ui_paint_layer_label(
      LegacyUiPaintTraceLabel::ui_windows_dwin_direct_paint));
  trace.emplace_back(legacy_ui_paint_layer_label(
      LegacyUiPaintTraceLabel::top_system_messages_draw_screen_top));
  trace.emplace_back(
      legacy_ui_paint_layer_label(LegacyUiPaintTraceLabel::hint_tooltip_draw_hint));
  trace.emplace_back(legacy_ui_paint_layer_label(LegacyUiPaintTraceLabel::moving_item_cursor));
  trace.emplace_back(legacy_ui_paint_layer_label(LegacyUiPaintTraceLabel::present));
  return trace;
}

std::vector<std::string> run_ui_input_gate_trace() {
  using namespace mir2::client;

  ClientConfig config;
  GameStateStore state;
  InputState input;
  SceneManager scenes;
  ClientContext context{nullptr, &config, &state, nullptr, nullptr, nullptr, &input};

  auto& world = state.world;
  world.self_actor_id = 1;
  world.width = 100;
  world.height = 100;
  world.map_id = "0";
  ActorState self;
  self.actor_id = 1;
  self.x = 50;
  self.y = 50;
  self.mp = 0;
  world.actors.emplace(1, self);
  world.magics.push_back(MagicShortcutState{7, 1, 0, 0, 0, "Fire", 0, 0, 0});

  scenes.initialize(context);
  scenes.change_scene(SceneId::world, context);
  auto* ui_tree = scenes.current_ui_tree();
  assert(ui_tree != nullptr);

  std::vector<std::string> trace;
  ui_tree->set_trace_callback(
      [&trace](const std::string_view label) { trace.emplace_back(label); });

  input.key_pressed[VK_F1] = true;
  scenes.capture_ui_input(context);
  scenes.process_key_messages(context);
  assert(world.action_key == 0);
  trace.emplace_back("world_shortcut_fallback_sets_action_key");

  input = InputState{};
  context.ui_input = ui::UiInputResult{false, true, false};
  input.key_pressed[VK_F1] = true;
  world.action_key = -1;
  scenes.process_key_messages(context);
  assert(world.action_key == -1);
  trace.emplace_back("text_focus_blocks_world_shortcut");

  input = InputState{};
  input.left_pressed = true;
  input.left_down = true;
  context.ui_input = ui::UiInputResult{true, false, false};
  world.focus_actor_id = 1;
  world.focus_ground_item_id = 900;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = LegacyChrAction::walk;
  world.pending_pickup_item_id = 900;
  world.action_key = 0;
  world.mouse_down_ms = 123;
  scenes.process_action_messages(context, 0.016F);
  assert(world.focus_actor_id == 0);
  assert(world.focus_ground_item_id == 0);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == LegacyChrAction::none);
  assert(world.pending_pickup_item_id == 0);
  assert(world.action_key == -1);
  assert(world.mouse_down_ms == 0);
  trace.emplace_back("consumed_mouse_clears_world_targets");

  return trace;
}

}  // namespace

int main() {
  const auto sections = read_trace_sections();
  assert(run_render_order_trace() == sections.at("render.world_order"));
  assert(run_ui_input_gate_trace() == sections.at("ui.input_gate"));
  return 0;
}
