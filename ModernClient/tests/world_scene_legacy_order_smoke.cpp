#include "app/client_app.hpp"
#include "assets/asset_manager.hpp"
#include "audio/audio_service.hpp"
#include "audio/audio_backend.hpp"
#include "audio/sound_constants.hpp"
#include "render/software_renderer.hpp"
#include "scene/scenes.hpp"
#include "shared/legacy/map_render_math.hpp"
#include "shared/legacy/movement_rules.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool has_sound_id(const mir2::client::AudioService& audio, const int sound_id) {
  for (const auto& event : audio.trace_events()) {
    if (event.kind == mir2::client::AudioTraceKind::sound_request &&
        event.sound_id == sound_id) {
      return true;
    }
  }
  return false;
}

std::filesystem::path asset_root() {
  const std::filesystem::path root = LR"(F:\mir2\Legend of Mir)";
  assert(std::filesystem::exists(root / L"Wav" / L"sound.lst"));
  return root;
}

std::vector<std::string> read_draw_layers(const std::filesystem::path& trace_path) {
  std::ifstream input(trace_path);
  std::vector<std::string> layers;
  std::string line;
  while (std::getline(input, line)) {
    const auto marker = line.find("draw layer=");
    if (marker == std::string::npos) {
      continue;
    }
    auto layer = line.substr(marker + 11);
    if (const auto space = layer.find(' '); space != std::string::npos) {
      layer = layer.substr(0, space);
    }
    layers.push_back(layer);
  }
  return layers;
}

std::pair<int, int> mouse_for_tile(const int self_x, const int self_y, const int x, const int y) {
  return mir2::legacy::legacy_screen_from_map(
      mir2::legacy::make_legacy_map_viewport(self_x, self_y), x, y);
}

mir2::client::LegacyInputEvent left_down_event(const int x, const int y) {
  mir2::client::LegacyInputEvent event;
  event.kind = mir2::client::LegacyInputEventKind::left_down;
  event.mouse_x = x;
  event.mouse_y = y;
  event.left_down = true;
  return event;
}

mir2::client::LegacyInputEvent mouse_move_event(const int x, const int y, const bool left_down) {
  mir2::client::LegacyInputEvent event;
  event.kind = mir2::client::LegacyInputEventKind::mouse_move;
  event.mouse_x = x;
  event.mouse_y = y;
  event.left_down = left_down;
  return event;
}

mir2::client::LegacyInputEvent left_up_event(const int x, const int y) {
  mir2::client::LegacyInputEvent event;
  event.kind = mir2::client::LegacyInputEventKind::left_up;
  event.mouse_x = x;
  event.mouse_y = y;
  return event;
}

mir2::client::LegacyInputEvent right_down_event(const int x, const int y) {
  mir2::client::LegacyInputEvent event;
  event.kind = mir2::client::LegacyInputEventKind::right_down;
  event.mouse_x = x;
  event.mouse_y = y;
  event.right_down = true;
  return event;
}

mir2::client::LegacyInputEvent right_up_event(const int x, const int y) {
  mir2::client::LegacyInputEvent event;
  event.kind = mir2::client::LegacyInputEventKind::right_up;
  event.mouse_x = x;
  event.mouse_y = y;
  return event;
}

mir2::client::LegacyInputEvent mouse_wheel_event(const int x, const int y, const int delta) {
  mir2::client::LegacyInputEvent event;
  event.kind = mir2::client::LegacyInputEventKind::mouse_wheel;
  event.mouse_x = x;
  event.mouse_y = y;
  event.wheel_delta = delta;
  return event;
}

mir2::client::LegacyInputEvent left_double_click_event(const int x, const int y) {
  mir2::client::LegacyInputEvent event;
  event.kind = mir2::client::LegacyInputEventKind::left_double_click;
  event.mouse_x = x;
  event.mouse_y = y;
  event.left_down = true;
  return event;
}

mir2::client::LegacyInputEvent right_double_click_event(const int x, const int y) {
  mir2::client::LegacyInputEvent event;
  event.kind = mir2::client::LegacyInputEventKind::right_double_click;
  event.mouse_x = x;
  event.mouse_y = y;
  event.right_down = true;
  return event;
}

mir2::client::LegacyInputEvent key_down_event(const std::uint16_t key,
                                              const int x = 0,
                                              const int y = 0) {
  mir2::client::LegacyInputEvent event;
  event.kind = mir2::client::LegacyInputEventKind::key_down;
  event.key = key;
  event.mouse_x = x;
  event.mouse_y = y;
  return event;
}

class PointerEventProbe final : public mir2::client::ui::UiNode {
 public:
  explicit PointerEventProbe(const mir2::client::RectI bounds) : UiNode(bounds) {}

  bool on_mouse_wheel(mir2::client::ui::UiTree& /*tree*/,
                      const mir2::client::InputState& /*input*/,
                      const int wheel_delta) override {
    ++wheel_hits;
    last_wheel_delta = wheel_delta;
    return true;
  }

  bool on_double_click(mir2::client::ui::UiTree& /*tree*/,
                       const mir2::client::InputState& /*input*/,
                       const mir2::client::ui::UiMouseButton button) override {
    if (button == mir2::client::ui::UiMouseButton::left) {
      ++left_double_hits;
    } else if (button == mir2::client::ui::UiMouseButton::right) {
      ++right_double_hits;
    }
    return true;
  }

  int wheel_hits{0};
  int last_wheel_delta{0};
  int left_double_hits{0};
  int right_double_hits{0};
};

void reset_frame_input(mir2::client::InputState& input, mir2::client::ClientContext& context) {
  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  context.legacy_input_dispatched = false;
}

void write_u16(std::vector<std::uint8_t>& bytes, const std::size_t offset,
               const std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_cell(std::vector<std::uint8_t>& bytes, const std::size_t header_size,
                const int height, const int x, const int y, const std::uint16_t bk,
                const std::uint16_t mid, const std::uint16_t fr,
                const std::uint8_t door_index = 0,
                const std::uint8_t door_offset = 0) {
  const auto offset = header_size +
      (static_cast<std::size_t>(x) * static_cast<std::size_t>(height) +
       static_cast<std::size_t>(y)) *
          12U;
  write_u16(bytes, offset, bk);
  write_u16(bytes, offset + 2U, mid);
  write_u16(bytes, offset + 4U, fr);
  bytes[offset + 6U] = door_index;
  bytes[offset + 7U] = door_offset;
}

std::filesystem::path write_dynamic_door_map_root() {
  const auto root = std::filesystem::temp_directory_path() / "mir2_dynamic_door_scene_smoke";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "Data");
  std::filesystem::create_directories(root / "Map");

  constexpr int width = 40;
  constexpr int height = 40;
  constexpr std::size_t header_size = 52U;
  std::vector<std::uint8_t> bytes(header_size + width * height * 12U);
  write_u16(bytes, 0U, width);
  write_u16(bytes, 2U, height);
  write_cell(bytes, header_size, height, 11, 20, 0, 0, 0, 0x80U | 3U, 0x80U);
  write_cell(bytes, header_size, height, 13, 20, 0, 0, 0, 0x80U | 3U, 0x00U);
  write_cell(bytes, header_size, height, 21, 20, 0, 0, 0, 0x80U | 3U, 0x80U);
  write_cell(bytes, header_size, height, 21, 19, 0, 0, 0x8000U);
  write_cell(bytes, header_size, height, 21, 21, 0, 0, 0x8000U);

  std::ofstream file(root / "Map" / "dynamicdoor.map", std::ios::binary);
  file.write(reinterpret_cast<const char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return root;
}

void test_legacy_raw_input_events() {
  mir2::client::ClientConfig config;
  mir2::client::GameStateStore state;
  mir2::client::InputState input;
  mir2::client::SceneManager scenes;
  mir2::client::ClientContext context{nullptr, &config, &state, nullptr, nullptr, nullptr, &input};

  auto& world = state.world;
  world.self_actor_id = 1;
  world.width = 100;
  world.height = 100;
  world.map_id = "0";
  mir2::client::ActorState self;
  self.actor_id = 1;
  self.x = 50;
  self.y = 50;
  world.actors.emplace(1, self);
  world.magics.push_back(mir2::client::MagicShortcutState{7, 1, 0, 0, 0, "Fire", 0, 0, 1});

  scenes.initialize(context);
  scenes.change_scene(mir2::client::SceneId::world, context);
  auto* ui_tree = scenes.current_ui_tree();
  assert(ui_tree != nullptr);

  auto [target_mouse_x, target_mouse_y] = mouse_for_tile(self.x, self.y, 52, 50);
  auto [stale_mouse_x, stale_mouse_y] = mouse_for_tile(self.x, self.y, 60, 50);
  input.left_pressed = true;
  input.left_down = true;
  input.mouse_x = stale_mouse_x;
  input.mouse_y = stale_mouse_y;
  input.events.push_back(left_down_event(target_mouse_x, target_mouse_y));
  world.action_locked = true;
  world.action_lock_started_ms = std::numeric_limits<std::uint64_t>::max();
  scenes.dispatch_legacy_input_events(context);
  assert(context.legacy_input_dispatched);
  assert(world.legacy_target_x == 52);
  assert(world.legacy_target_y == 50);
  input.mouse_x = stale_mouse_x;
  input.mouse_y = stale_mouse_y;
  scenes.process_action_messages(context, 0.016F);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  world.action_locked = false;
  world.action_lock_started_ms = 0;
  world.legacy_target_x = -1;
  world.legacy_target_y = -1;
  world.legacy_chr_action = mir2::client::LegacyChrAction::none;
  reset_frame_input(input, context);

  auto* ui_blocker = ui_tree->root()->emplace_child<mir2::client::ui::Button>(
      mir2::client::RectI{500, 40, 80, 40});
  input.left_pressed = true;
  input.left_down = true;
  input.mouse_x = 520;
  input.mouse_y = 60;
  input.events.push_back(left_down_event(input.mouse_x, input.mouse_y));
  scenes.dispatch_legacy_input_events(context);
  assert(context.legacy_input_dispatched);
  assert(context.ui_input.consumed);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.pending_pickup_item_id == 0);
  ui_blocker->set_visible(*ui_tree, false);
  reset_frame_input(input, context);

  auto* hover_blocker = ui_tree->root()->emplace_child<mir2::client::ui::UiNode>(
      mir2::client::RectI{500, 40, 80, 40});
  input.mouse_x = 520;
  input.mouse_y = 60;
  input.events.push_back(key_down_event(VK_F1, input.mouse_x, input.mouse_y));
  world.action_key = -1;
  world.focus_actor_id = 77;
  world.focus_ground_item_id = 88;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  scenes.dispatch_legacy_input_events(context);
  assert(context.legacy_input_dispatched);
  assert(context.ui_input.consumed);
  assert(context.ui_input.hover_consumed);
  assert(world.action_key == 0);
  assert(world.focus_actor_id == 0);
  assert(world.focus_ground_item_id == 0);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  hover_blocker->set_visible(*ui_tree, false);
  reset_frame_input(input, context);

  mir2::client_v1::ItemState moving_item;
  moving_item.make_index = 3001;
  moving_item.name = "Potion";
  world.bag_items[0] = mir2::client_v1::ItemState{};
  world.moving_item = mir2::client::MovingItemState{
      true, mir2::client::MovingItemSource::bag, 0, moving_item};
  input.events.push_back(key_down_event(VK_F1, 520, 60));
  world.action_key = 0;
  world.focus_actor_id = 77;
  world.focus_ground_item_id = 88;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  world.pending_pickup_item_id = 900;
  world.mouse_down_ms = 123;
  scenes.dispatch_legacy_input_events(context);
  assert(context.legacy_input_dispatched);
  assert(world.action_key == -1);
  assert(world.focus_actor_id == 0);
  assert(world.focus_ground_item_id == 0);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  assert(world.pending_pickup_item_id == 0);
  assert(world.mouse_down_ms == 0);
  assert(world.moving_item.active);
  reset_frame_input(input, context);

  input.key_pressed[VK_ESCAPE] = true;
  input.events.push_back(key_down_event(VK_ESCAPE, 520, 60));
  scenes.dispatch_legacy_input_events(context);
  assert(context.legacy_input_dispatched);
  assert(!world.moving_item.active);
  assert(world.bag_items[0].make_index == moving_item.make_index);
  world.bag_items[0] = mir2::client_v1::ItemState{};
  reset_frame_input(input, context);

  auto* drag_window = ui_tree->root()->emplace_child<mir2::client::ui::Window>(
      mir2::client::RectI{300, 100, 80, 40});
  drag_window->floating = true;
  input.mouse_x = 340;
  input.mouse_y = 130;
  input.left_released = true;
  input.events.push_back(left_down_event(310, 110));
  input.events.push_back(mouse_move_event(340, 130, true));
  input.events.push_back(left_up_event(340, 130));
  scenes.dispatch_legacy_input_events(context);
  assert(drag_window->bounds.x == 330);
  assert(drag_window->bounds.y == 120);
  drag_window->set_visible(*ui_tree, false);
  reset_frame_input(input, context);

  auto* pointer_probe = ui_tree->root()->emplace_child<PointerEventProbe>(
      mir2::client::RectI{100, 100, 80, 80});
  input.events.push_back(mouse_move_event(120, 120, false));
  input.events.push_back(mouse_wheel_event(130, 130, 120));
  scenes.dispatch_legacy_input_events(context);
  assert(context.legacy_input_dispatched);
  assert(pointer_probe->wheel_hits == 1);
  assert(pointer_probe->last_wheel_delta == 120);
  reset_frame_input(input, context);

  input.events.push_back(left_down_event(120, 120));
  input.events.push_back(left_double_click_event(120, 120));
  input.events.push_back(left_up_event(120, 120));
  scenes.dispatch_legacy_input_events(context);
  assert(context.legacy_input_dispatched);
  assert(pointer_probe->left_double_hits == 1);
  reset_frame_input(input, context);

  input.events.push_back(right_down_event(120, 120));
  input.events.push_back(right_double_click_event(120, 120));
  input.events.push_back(right_up_event(120, 120));
  scenes.dispatch_legacy_input_events(context);
  assert(context.legacy_input_dispatched);
  assert(pointer_probe->right_double_hits == 1);
  pointer_probe->set_visible(*ui_tree, false);
  reset_frame_input(input, context);

  world.focus_actor_id = 77;
  world.target_actor_id = 88;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  world.action_key = 0;
  input.key_pressed[VK_ESCAPE] = true;
  input.events.push_back(key_down_event(VK_ESCAPE));
  scenes.dispatch_legacy_input_events(context);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  assert(world.target_actor_id == 0);
  assert(world.action_key == -1);
  scenes.process_action_messages(context, 0.016F);
}

void test_dynamic_door_latest_propagation_blocks_walk() {
  const auto root = write_dynamic_door_map_root();
  mir2::client::ClientConfig config;
  config.asset_root = root.wstring();
  mir2::client::AssetManager assets;
  assert(assets.initialize(root));
  mir2::client::ClientApp app;
  app.set_config_for_test(config);
  app.enable_protocol_test_mode_for_test();
  auto& state = app.state_for_test();
  mir2::client::InputState input;
  mir2::client::SceneManager scenes;
  mir2::client::ClientContext context{&app, &config, &state, &assets, nullptr, nullptr, &input};

  auto& world = state.world;
  world.self_actor_id = 1;
  world.width = 40;
  world.height = 40;
  world.map_id = "dynamicdoor";
  mir2::client::ActorState self;
  self.actor_id = 1;
  self.x = 20;
  self.y = 20;
  self.dir = mir2::legacy::next_direction(self.x, self.y, 21, 20);
  world.actors.emplace(1, self);
  state.apply(mir2::client_v1::MapDoorState{11, 20, true});
  state.apply(mir2::client_v1::MapDoorState{13, 20, false});

  scenes.initialize(context);
  scenes.change_scene(mir2::client::SceneId::world, context);
  world.legacy_target_x = 21;
  world.legacy_target_y = 20;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  scenes.process_action_messages(context, 0.016F);

  const auto self_it = world.actors.find(1);
  assert(self_it != world.actors.end());
  assert(self_it->second.x == 20);
  assert(self_it->second.y == 20);
  assert(!world.action_locked);
  assert(world.last_sent_action_ident == 0);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);

  state.apply(mir2::client_v1::MapDoorState{11, 20, true});
  world.legacy_target_x = 21;
  world.legacy_target_y = 20;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  scenes.process_action_messages(context, 0.016F);

  const auto moved_self_it = world.actors.find(1);
  assert(moved_self_it != world.actors.end());
  assert(moved_self_it->second.x == 21);
  assert(moved_self_it->second.y == 20);
  assert(world.action_locked);
  assert(world.last_sent_action_ident != 0);
}

}  // namespace

int main() {
  mir2::client::ClientConfig config;
  const auto root = asset_root();
  config.asset_root = root.wstring();
  const auto trace_path =
      std::filesystem::temp_directory_path() / "mir2_world_scene_legacy_order_trace.txt";
  std::filesystem::remove(trace_path);
  _putenv_s("MIR2_LEGACY_TRACE", "1");
  _putenv_s("MIR2_LEGACY_TRACE_FILE", trace_path.string().c_str());

  test_legacy_raw_input_events();
  test_dynamic_door_latest_propagation_blocks_walk();
  std::filesystem::remove(trace_path);

  mir2::client::GameStateStore state;
  mir2::client::InputState input;
  mir2::client::AudioService audio(std::make_unique<mir2::client::NullAudioBackend>());
  assert(audio.initialize(root));

  auto& world = state.world;
  world.self_actor_id = 1;
  world.width = 100;
  world.height = 100;
  world.map_id = "0";
  mir2::client::ActorState self;
  self.actor_id = 1;
  self.x = 50;
  self.y = 50;
  self.mp = 0;
  world.actors.emplace(1, self);
  world.magics.push_back(mir2::client::MagicShortcutState{7, 1, 0, 0, 0, "Fire", 0, 0, 1, 4, 1, 3});

  mir2::client::SceneManager scenes;
  mir2::client::ClientContext context{nullptr, &config, &state, nullptr, &audio, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(mir2::client::SceneId::world, context);
  auto* ui_tree = scenes.current_ui_tree();
  assert(ui_tree != nullptr);
  std::vector<std::string> ui_trace;
  ui_tree->set_trace_callback(
      [&ui_trace](const std::string_view label) { ui_trace.emplace_back(label); });
  audio.clear_trace_events();

  input.key_pressed[VK_F1] = true;
  scenes.capture_ui_input(context);
  scenes.process_key_messages(context);
  assert(world.action_key == 0);
  assert((ui_trace == std::vector<std::string>{
                          "active_menu_key_first",
                          "modal_window_key_blocks_lower_windows",
                          "focused_edit_or_chat_key",
                          "chat_enter_submit_or_open",
                          "chat_escape_cancel",
                          "legacy_shortcut_fallback"}));
  assert(audio.trace_events().empty());

  scenes.process_action_messages(context, 0.016F);
  assert(audio.trace_events().empty());

  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.key_pressed[VK_RETURN] = true;
  input.enter_pressed = true;
  scenes.capture_ui_input(context);
  scenes.process_key_messages(context);

  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.key_pressed[VK_F9] = true;
  scenes.capture_ui_input(context);
  assert(context.ui_input.text_focus);
  world.action_key = -1;
  scenes.process_key_messages(context);
  assert(world.action_key == -1);

  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.text_input = L"hello";
  scenes.capture_ui_input(context);
  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.key_pressed[VK_RETURN] = true;
  input.enter_pressed = true;
  scenes.capture_ui_input(context);
  scenes.dwin_process(context);

  world.whisper_name = "Al";
  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.text_input = L"/";
  scenes.capture_ui_input(context);
  scenes.process_key_messages(context);
  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.key_pressed[VK_ESCAPE] = true;
  scenes.capture_ui_input(context);
  assert(context.ui_input.text_focus);
  scenes.dwin_process(context);
  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.key_pressed[VK_F9] = true;
  scenes.capture_ui_input(context);
  assert(!context.ui_input.text_focus);

  scenes.process_key_messages(context);
  mir2::client_v1::ItemState potion;
  potion.name = "Potion";
  potion.make_index = 2001;
  potion.looks = 1;
  potion.std_mode = 0;
  world.bag_items[6] = potion;
  world.moving_item = mir2::client::MovingItemState{
      true, mir2::client::MovingItemSource::bag, 6, potion};
  world.bag_items[6] = mir2::client_v1::ItemState{};
  input = mir2::client::InputState{};
  input.mouse_x = 799;
  input.mouse_y = 599;
  input.key_pressed[VK_ESCAPE] = true;
  scenes.capture_ui_input(context);
  scenes.process_key_messages(context);
  assert(!world.moving_item.active);
  assert(world.bag_items[6].make_index == potion.make_index);

  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.key_pressed[VK_F11] = true;
  scenes.capture_ui_input(context);
  scenes.process_key_messages(context);
  assert(ui_tree->modal() == nullptr);

  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.mouse_x = 590;
  input.mouse_y = 60;
  input.left_pressed = true;
  input.left_down = true;
  scenes.capture_ui_input(context);
  scenes.dwin_process(context);

  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.mouse_x = 590;
  input.mouse_y = 60;
  input.left_released = true;
  scenes.capture_ui_input(context);
  scenes.dwin_process(context);
  assert(ui_tree->modal() != nullptr);

  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.key_pressed[VK_ESCAPE] = true;
  scenes.capture_ui_input(context);
  scenes.process_key_messages(context);
  assert(ui_tree->modal() == nullptr);

  scenes.scene_run(context, 0.0F);
  assert(has_sound_id(audio, mir2::client::s_main_theme));

  mir2::client::AssetManager assets;
  assert(assets.initialize(config.asset_root));
  mir2::client::SoftwareRenderer renderer;
  context.assets = &assets;
  context.renderer = &renderer;
  std::filesystem::remove(trace_path);
  scenes.scene_run(context, 0.0F);
  scenes.render_scene(context);
  const auto draw_layers = read_draw_layers(trace_path);
  int actor_last = -1;
  int fly_last = -1;
  int actor_overlay_first = -1;
  int selection_blend_first = -1;
  for (int index = 0; index < static_cast<int>(draw_layers.size()); ++index) {
    if (draw_layers[static_cast<std::size_t>(index)] == "actor") {
      actor_last = index;
    } else if (draw_layers[static_cast<std::size_t>(index)] == "fly_effect") {
      fly_last = index;
    } else if (draw_layers[static_cast<std::size_t>(index)] == "actor_overlay" &&
               actor_overlay_first < 0) {
      actor_overlay_first = index;
    } else if (draw_layers[static_cast<std::size_t>(index)] == "selection_blend" &&
               selection_blend_first < 0) {
      selection_blend_first = index;
    }
  }
  assert(actor_last >= 0);
  assert(fly_last >= 0);
  assert(actor_overlay_first >= 0);
  assert(selection_blend_first >= 0);
  assert(actor_last < selection_blend_first);
  assert(fly_last < selection_blend_first);
  assert(selection_blend_first < actor_overlay_first);

  input = mir2::client::InputState{};
  input.key_pressed[VK_F1] = true;
  context.ui_input = mir2::client::ui::UiInputResult{true, false, false};
  world.action_key = -1;
  scenes.process_key_messages(context);
  assert(world.action_key == -1);

  input = mir2::client::InputState{};
  input.key_pressed[VK_F1] = true;
  context.ui_input = mir2::client::ui::UiInputResult{true, false, false, false, true};
  world.action_key = -1;
  scenes.process_key_messages(context);
  assert(world.action_key == 0);
  world.focus_actor_id = 1;
  world.focus_ground_item_id = 900;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  world.pending_pickup_item_id = 900;
  world.mouse_down_ms = 123;
  scenes.process_action_messages(context, 0.016F);
  assert(world.focus_actor_id == 0);
  assert(world.focus_ground_item_id == 0);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  assert(world.pending_pickup_item_id == 0);
  assert(world.action_key == 0);
  assert(world.mouse_down_ms == 0);

  input = mir2::client::InputState{};
  input.key_pressed[VK_F1] = true;
  context.ui_input = mir2::client::ui::UiInputResult{false, true, false};
  world.action_key = -1;
  scenes.process_key_messages(context);
  assert(world.action_key == -1);

  input = mir2::client::InputState{};
  input.key_pressed[VK_F1] = true;
  context.ui_input = mir2::client::ui::UiInputResult{false, false, true};
  world.action_key = -1;
  scenes.process_key_messages(context);
  assert(world.action_key == -1);

  input = mir2::client::InputState{};
  input.key_pressed[VK_F1] = true;
  context.ui_input = mir2::client::ui::UiInputResult{false, false, false, true};
  world.action_key = -1;
  scenes.process_key_messages(context);
  assert(world.action_key == -1);

  input = mir2::client::InputState{};
  input.left_pressed = true;
  input.left_down = true;
  context.ui_input = mir2::client::ui::UiInputResult{true, false, false};
  world.focus_actor_id = 1;
  world.focus_ground_item_id = 900;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  world.pending_pickup_item_id = 900;
  world.action_key = 0;
  world.mouse_down_ms = 123;
  scenes.process_action_messages(context, 0.016F);
  assert(world.focus_actor_id == 0);
  assert(world.focus_ground_item_id == 0);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  assert(world.pending_pickup_item_id == 0);
  assert(world.action_key == -1);
  assert(world.mouse_down_ms == 0);

  input = mir2::client::InputState{};
  input.left_pressed = true;
  input.left_down = true;
  context.ui_input = mir2::client::ui::UiInputResult{false, true, false};
  world.focus_actor_id = 1;
  world.focus_ground_item_id = 900;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  world.pending_pickup_item_id = 900;
  world.action_key = 0;
  world.mouse_down_ms = 123;
  scenes.process_action_messages(context, 0.016F);
  assert(world.focus_actor_id == 0);
  assert(world.focus_ground_item_id == 0);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  assert(world.pending_pickup_item_id == 0);
  assert(world.action_key == -1);
  assert(world.mouse_down_ms == 0);

  input = mir2::client::InputState{};
  context.legacy_input_dispatched = true;
  context.ui_input = mir2::client::ui::UiInputResult{true, false, false, true};
  world.focus_actor_id = 1;
  world.focus_ground_item_id = 900;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  world.pending_pickup_item_id = 900;
  world.action_key = 0;
  world.mouse_down_ms = 123;
  scenes.process_action_messages(context, 0.016F);
  assert(world.focus_actor_id == 0);
  assert(world.focus_ground_item_id == 0);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  assert(world.pending_pickup_item_id == 0);
  assert(world.action_key == -1);
  assert(world.mouse_down_ms == 0);
  context.legacy_input_dispatched = false;

  input = mir2::client::InputState{};
  context.ui_input = mir2::client::ui::UiInputResult{};
  input.mouse_x = -9999;
  input.mouse_y = -9999;
  input.left_pressed = true;
  input.left_down = true;
  world.action_locked = true;
  world.action_lock_started_ms = std::numeric_limits<std::uint64_t>::max();
  world.focus_actor_id = 0;
  world.focus_ground_item_id = 0;
  world.legacy_target_x = -1;
  world.legacy_target_y = -1;
  world.legacy_chr_action = mir2::client::LegacyChrAction::none;
  scenes.process_action_messages(context, 0.016F);
  assert(world.legacy_target_x < 0);
  assert(world.legacy_target_y < 0);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  world.action_locked = false;
  world.action_lock_started_ms = 0;
  world.legacy_target_x = -1;
  world.legacy_target_y = -1;
  world.legacy_chr_action = mir2::client::LegacyChrAction::none;

  world.map_id = "3";
  world.width = 400;
  world.height = 300;
  world.self_actor_id = 1;
  world.ground_items.emplace(900, mir2::client_v1::GroundItemState{900, 51, 50, 1, "Gold"});
  world.ground_item_draw_order.push_back(900);
  world.focus_actor_id = 1;
  world.target_actor_id = 1;
  world.action_locked = true;
  world.legacy_target_x = 52;
  world.legacy_target_y = 50;
  world.legacy_chr_action = mir2::client::LegacyChrAction::walk;
  world.npc_dialog.visible = true;
  world.trade.visible = true;
  world.minimap.visible = true;
  world.moving_item = mir2::client::MovingItemState{
      true, mir2::client::MovingItemSource::bag, 6, potion};
  world.pending_item_action.active = true;
  world.pending_pickup_item_id = 900;

  scenes.change_scene(mir2::client::SceneId::loading, context);
  assert(world.map_id == "0");
  assert(world.width == 800);
  assert(world.height == 600);
  assert(world.self_actor_id == 0);
  assert(world.actors.empty());
  assert(world.ground_items.empty());
  assert(world.actor_draw_order.empty());
  assert(world.ground_item_draw_order.empty());
  assert(world.focus_actor_id == 0);
  assert(world.target_actor_id == 0);
  assert(!world.npc_dialog.visible);
  assert(!world.trade.visible);
  assert(!world.minimap.visible);
  assert(!world.moving_item.active);
  assert(!world.pending_item_action.active);
  assert(world.pending_pickup_item_id == 0);
  assert(!world.action_locked);
  assert(world.legacy_target_x == -1);
  assert(world.legacy_target_y == -1);
  assert(world.legacy_chr_action == mir2::client::LegacyChrAction::none);
  return 0;
}
