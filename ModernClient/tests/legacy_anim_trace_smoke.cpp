#include "animation/legacy_animation.hpp"
#include "shared/legacy/action_ids.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> read_lines(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

bool has_stage(const std::vector<std::string>& lines, const std::string& stage) {
  const auto token = "stage=" + stage;
  for (const auto& line : lines) {
    if (line.find(token) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using namespace mir2::client;

  const auto trace_path =
      std::filesystem::temp_directory_path() / "mir2_legacy_anim_trace_smoke.log";
  std::error_code ec;
  std::filesystem::remove(trace_path, ec);

  const auto trace_path_string = trace_path.string();
  _putenv_s("MIR2_ANIM_TRACE", "1");
  _putenv_s("MIR2_ANIM_TRACE_FILE", trace_path_string.c_str());

  WorldViewState world;
  world.self_actor_id = 1;
  world.width = 100;
  world.height = 100;

  ActorState actor;
  actor.actor_id = 1;
  actor.actor_type = mir2::client_v1::ActorType::player;
  actor.x = 10;
  actor.y = 10;
  actor.from_x = 10;
  actor.from_y = 10;
  actor.dir = 2;
  actor.feature = make_legacy_feature(0, 1, 2, 3);
  actor.current_action = mir2::client_v1::ActorActionKind::turn;
  world.actors[actor.actor_id] = actor;

  AnimationManager animation;
  animation.reset(1000);
  animation.update(world, 1000);

  actor.current_action = mir2::client_v1::ActorActionKind::hit;
  actor.legacy_action_ident = mir2::legacy::kSmHit;
  actor.action_started_ms = 1100;
  world.actors[actor.actor_id] = actor;
  animation.update(world, 1100);
  animation.update(world, 1200);

  LegacyEffectManager::MagicCreate create;
  create.magic_id = 8;
  create.effect = 8;
  create.source_x = 10;
  create.source_y = 10;
  create.target_x = 20;
  create.target_y = 20;
  create.owner_actor_id = actor.actor_id;
  create.target_actor_id = 0;
  create.magic_type = LegacyMagicType::fly;
  create.repetition = true;
  create.now_ms = 2000;
  create.trace_frame_index = animation.trace_frame_index() + 1;
  animation.effects().spawn_magic_effect(create);
  animation.effects().update(2001, create.trace_frame_index);
  animation.effects().update(13050, create.trace_frame_index + 1);

  const auto lines = read_lines(trace_path);
  assert(!lines.empty());
  assert(has_stage(lines, "state_change"));
  assert(has_stage(lines, "anim"));
  assert(has_stage(lines, "effect_create"));
  assert(has_stage(lines, "effect_destroy"));

  constexpr const char* kRequiredFields[] = {
      "frame=",               "t=",                 "actor=",
      "action=",              "logical_pos=",       "render_offset=",
      "animation_start_time=","animation_frame_index=",
      "animation_frame_interval=",                  "effect_id=",
      "effect_type=",         "effect_pos=",        "effect_create_frame=",
      "effect_destroy_frame=","render_layer=",      "same_frame_visible=",
  };

  for (const auto& line : lines) {
    if (line.find("[mir2-anim]") == std::string::npos) {
      continue;
    }
    for (const auto* field : kRequiredFields) {
      assert(line.find(field) != std::string::npos);
    }
  }

  _putenv_s("MIR2_ANIM_TRACE_FILE", "");
  _putenv_s("MIR2_ANIM_TRACE", "");
  return 0;
}
