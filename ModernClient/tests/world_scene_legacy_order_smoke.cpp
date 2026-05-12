#include "audio/audio_service.hpp"
#include "audio/audio_backend.hpp"
#include "audio/sound_constants.hpp"
#include "scene/scenes.hpp"

#include <cassert>
#include <filesystem>
#include <memory>

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

}  // namespace

int main() {
  mir2::client::ClientConfig config;
  mir2::client::GameStateStore state;
  mir2::client::InputState input;
  mir2::client::AudioService audio(std::make_unique<mir2::client::NullAudioBackend>());
  assert(audio.initialize(asset_root()));

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
  world.magics.push_back(mir2::client::MagicShortcutState{7, 1, 0, 0, 0, "Fire", 0, 0, 0});

  mir2::client::SceneManager scenes;
  mir2::client::ClientContext context{nullptr, &config, &state, nullptr, &audio, nullptr, &input};
  scenes.initialize(context);
  scenes.change_scene(mir2::client::SceneId::world, context);
  audio.clear_trace_events();

  input.key_pressed[VK_F1] = true;
  scenes.capture_ui_input(context);
  scenes.process_key_messages(context);
  assert(world.action_key == 0);
  assert(audio.trace_events().empty());

  scenes.process_action_messages(context, 0.016F);
  assert(audio.trace_events().empty());

  scenes.scene_run(context, 0.0F);
  assert(has_sound_id(audio, mir2::client::s_main_theme));
  return 0;
}
