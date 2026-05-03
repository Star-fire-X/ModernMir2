#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>

#include "assets/asset_manager.hpp"
#include "audio/audio_service.hpp"
#include "audio/sound_constants.hpp"
#include "scene/scenes.hpp"

namespace {

std::filesystem::path asset_root() {
  const std::filesystem::path root = LR"(F:\mir2\Legend of Mir)";
  assert(std::filesystem::exists(root / L"Wav" / L"sound.lst"));
  return root;
}

bool has_bgm_path(const mir2::client::AudioService& audio,
                  const std::wstring& path) {
  const auto& events = audio.trace_events();
  return std::any_of(events.begin(), events.end(), [&path](const auto& event) {
    return event.kind == mir2::client::AudioTraceKind::bgm_request &&
           event.path == path;
  });
}

bool has_kind(const mir2::client::AudioService& audio,
              mir2::client::AudioTraceKind kind) {
  const auto& events = audio.trace_events();
  return std::any_of(events.begin(), events.end(), [kind](const auto& event) {
    return event.kind == kind;
  });
}

int count_sound_id(const mir2::client::AudioService& audio, const int sound_id) {
  const auto& events = audio.trace_events();
  return static_cast<int>(std::count_if(events.begin(), events.end(), [sound_id](const auto& event) {
    return event.sound_id.has_value() && *event.sound_id == sound_id;
  }));
}

void click_scene(mir2::client::SceneManager& scenes, mir2::client::ClientContext& context,
                 mir2::client::InputState& input, const int x, const int y) {
  input = {};
  input.mouse_x = x;
  input.mouse_y = y;
  input.left_pressed = true;
  input.left_down = true;
  context.input = &input;
  scenes.update(context, 0.016F);

  input = {};
  input.mouse_x = x;
  input.mouse_y = y;
  input.left_released = true;
  context.input = &input;
  scenes.update(context, 0.016F);

  input = {};
  context.input = &input;
  scenes.update(context, 0.016F);
}

bool click_first_button_in_rect(mir2::client::SceneManager& scenes,
                                mir2::client::ClientContext& context,
                                mir2::client::InputState& input,
                                const mir2::client::RectI rect) {
  auto* tree = scenes.current_ui_tree();
  if (tree == nullptr || tree->root() == nullptr) {
    return false;
  }
  tree->set_asset_manager(context.assets);
  for (auto y = rect.y; y < rect.y + rect.h; ++y) {
    for (auto x = rect.x; x < rect.x + rect.w; ++x) {
      auto* node = tree->root()->hit_test(x, y, context.assets);
      if (dynamic_cast<mir2::client::ui::Button*>(node) != nullptr) {
        click_scene(scenes, context, input, x, y);
        return true;
      }
    }
  }
  return false;
}

}  // namespace

int main() {
  using namespace mir2::client;

  const auto root = asset_root();

  ClientConfig config;
  config.asset_root = root.wstring();

  GameStateStore state;
  state.lobby.servers.push_back(mir2::client_v1::ServerEntry{"TestServer", "127.0.0.1", 7000});
  state.lobby.characters.push_back(mir2::client_v1::CharacterSummary{"Alpha", 1, 0, 0, 0, "0"});
  state.lobby.characters.push_back(mir2::client_v1::CharacterSummary{"Beta", 2, 1, 1, 1, "0"});
  state.lobby.selected_index = 0;
  AssetManager assets;
  assert(assets.initialize(config.asset_root));

  InputState input;
  AudioService audio(std::make_unique<NullAudioBackend>());
  assert(audio.initialize(root));

  SceneManager scenes;
  ClientContext context{nullptr, &config, &state, &assets, &audio, nullptr, &input};

  scenes.initialize(context);

  audio.clear_trace_events();
  scenes.change_scene(SceneId::login, context);
  assert(has_bgm_path(audio, bmg_intro));
  audio.clear_trace_events();
  assert(click_first_button_in_rect(scenes, context, input, RectI{200, 300, 400, 120}));
  assert(count_sound_id(audio, s_rock_button_click) > 0);

  audio.clear_trace_events();
  scenes.change_scene(SceneId::server_select, context);
  assert(has_kind(audio, AudioTraceKind::silence));
  assert(has_bgm_path(audio, bmg_intro));
  audio.clear_trace_events();
  assert(click_first_button_in_rect(scenes, context, input, RectI{300, 320, 220, 90}));
  assert(count_sound_id(audio, s_rock_button_click) > 0);

  audio.clear_trace_events();
  scenes.change_scene(SceneId::character_select, context);
  assert(has_bgm_path(audio, bmg_select));
  audio.clear_trace_events();
  assert(click_first_button_in_rect(scenes, context, input, RectI{660, 430, 120, 80}));
  assert(count_sound_id(audio, s_norm_button_click) > 0);
  assert(count_sound_id(audio, s_meltstone) > 0);
  audio.clear_trace_events();
  assert(click_first_button_in_rect(scenes, context, input, RectI{660, 430, 120, 80}));
  assert(count_sound_id(audio, s_norm_button_click) > 0);
  assert(count_sound_id(audio, s_meltstone) == 0);

  audio.clear_trace_events();
  scenes.change_scene(SceneId::world, context);
  assert(has_kind(audio, AudioTraceKind::silence));
  audio.clear_trace_events();
  input = {};
  context.input = &input;
  scenes.update(context, 0.0F);
  assert(count_sound_id(audio, s_main_theme) == 1);
  audio.clear_trace_events();
  scenes.update(context, 45.999F);
  assert(count_sound_id(audio, s_main_theme) == 0);
  audio.clear_trace_events();
  scenes.update(context, 0.001F);
  assert(count_sound_id(audio, s_main_theme) == 1);

  audio.clear_trace_events();
  scenes.change_scene(SceneId::login, context);
  assert(has_kind(audio, AudioTraceKind::silence));
  audio.clear_trace_events();
  scenes.change_scene(SceneId::world, context);
  assert(has_kind(audio, AudioTraceKind::silence));
  audio.clear_trace_events();
  input = {};
  context.input = &input;
  scenes.update(context, 0.0F);
  assert(count_sound_id(audio, s_main_theme) == 1);

  std::cout << "scene_audio_smoke ok\n";
  return 0;
}
