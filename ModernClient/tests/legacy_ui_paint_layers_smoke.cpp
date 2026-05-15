#include "ui/legacy_ui.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

using mir2::client::RectI;
namespace ui = mir2::client::ui;

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

class PaintProbe final : public ui::UiNode {
 public:
  PaintProbe(const RectI bounds, std::vector<std::string>& events, std::string label)
      : ui::UiNode(bounds), events_(events), label_(std::move(label)) {}

  void paint(mir2::client::SoftwareRenderer& renderer) override {
    events_.push_back(label_);
    ui::UiNode::paint(renderer);
  }

 private:
  std::vector<std::string>& events_;
  std::string label_;
};

void test_paint_trace_labels_match_golden(
    const std::map<std::string, std::vector<std::string>>& sections) {
  const std::vector<ui::LegacyUiPaintTraceLabel> labels{
      ui::LegacyUiPaintTraceLabel::map_tiles,
      ui::LegacyUiPaintTraceLabel::map_objects,
      ui::LegacyUiPaintTraceLabel::actors_monsters_npcs,
      ui::LegacyUiPaintTraceLabel::skill_effects,
      ui::LegacyUiPaintTraceLabel::scene_top_effects,
      ui::LegacyUiPaintTraceLabel::ui_windows_dwin_direct_paint,
      ui::LegacyUiPaintTraceLabel::top_system_messages_draw_screen_top,
      ui::LegacyUiPaintTraceLabel::hint_tooltip_draw_hint,
      ui::LegacyUiPaintTraceLabel::moving_item_cursor,
      ui::LegacyUiPaintTraceLabel::mouse_cursor,
      ui::LegacyUiPaintTraceLabel::present,
  };

  std::vector<std::string> actual;
  for (const auto label : labels) {
    actual.emplace_back(ui::legacy_ui_paint_layer_label(label));
  }
  assert(actual == sections.at("ui.paint.layers"));
}

void test_overlay_types_default_to_legacy_layers() {
  ui::Tooltip tooltip(RectI{0, 0, 80, 20});
  ui::DragSpriteOverlay drag(RectI{0, 0, 32, 32});

  assert(tooltip.paint_layer() == ui::UiPaintLayer::hint);
  assert(drag.paint_layer() == ui::UiPaintLayer::moving_item);
}

void test_direct_hint_and_moving_item_passes_are_split() {
  ui::UiTree tree;
  auto* root = tree.set_root<ui::UiNode>(RectI{0, 0, 800, 600});
  std::vector<std::string> events;

  root->emplace_child<PaintProbe>(RectI{0, 0, 10, 10}, events, "window");
  auto* hint = root->emplace_child<PaintProbe>(RectI{0, 0, 10, 10}, events, "hint");
  hint->set_paint_layer(ui::UiPaintLayer::hint);
  auto* top = root->emplace_child<PaintProbe>(RectI{0, 0, 10, 10}, events, "top");
  top->set_paint_layer(ui::UiPaintLayer::top);
  auto* moving =
      root->emplace_child<PaintProbe>(RectI{0, 0, 10, 10}, events, "moving_item");
  moving->set_paint_layer(ui::UiPaintLayer::moving_item);

  mir2::client::SoftwareRenderer renderer;
  tree.paint(renderer);
  assert((events == std::vector<std::string>{"window"}));

  events.clear();
  tree.paint_layer(renderer, ui::UiPaintLayer::top);
  assert((events == std::vector<std::string>{"top"}));

  events.clear();
  tree.paint_layer(renderer, ui::UiPaintLayer::hint);
  assert((events == std::vector<std::string>{"hint"}));

  events.clear();
  tree.paint_layer(renderer, ui::UiPaintLayer::moving_item);
  assert((events == std::vector<std::string>{"moving_item"}));
}

void test_legacy_manager_paint_trace_order(
    const std::map<std::string, std::vector<std::string>>& sections) {
  ui::LegacyUiManager manager;
  mir2::client::SoftwareRenderer renderer;

  manager.trace().emit(ui::legacy_ui_paint_layer_label(ui::LegacyUiPaintTraceLabel::map_tiles));
  manager.trace().emit(ui::legacy_ui_paint_layer_label(ui::LegacyUiPaintTraceLabel::map_objects));
  manager.trace().emit(
      ui::legacy_ui_paint_layer_label(ui::LegacyUiPaintTraceLabel::actors_monsters_npcs));
  manager.trace().emit(ui::legacy_ui_paint_layer_label(ui::LegacyUiPaintTraceLabel::skill_effects));
  manager.trace().emit(
      ui::legacy_ui_paint_layer_label(ui::LegacyUiPaintTraceLabel::scene_top_effects));
  manager.direct_paint(renderer);
  manager.trace().emit(
      ui::legacy_ui_paint_layer_label(ui::LegacyUiPaintTraceLabel::top_system_messages_draw_screen_top));
  manager.draw_hint(renderer);
  manager.draw_moving_item(renderer);
  manager.trace_mouse_cursor();
  manager.trace().emit(ui::legacy_ui_paint_layer_label(ui::LegacyUiPaintTraceLabel::present));

  assert(manager.trace().events() == sections.at("ui.paint.layers"));
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto sections =
      read_trace_sections(source_dir / "tests" / "golden" / "legacy_ui_expected_trace.txt");

  test_paint_trace_labels_match_golden(sections);
  test_overlay_types_default_to_legacy_layers();
  test_direct_hint_and_moving_item_passes_are_split();
  test_legacy_manager_paint_trace_order(sections);
  return 0;
}
