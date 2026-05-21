#include "delphi_ui_manifest_helpers.hpp"
#include "scene/character_select_state.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

using mir2::client::CharacterSelectPoseKind;
using mir2::client::CharacterSelectVisualState;
using mir2::client::kCharacterSelectEffectFrameCount;
using mir2::client::kCharacterSelectFreezeFrameCount;
using mir2::client::kCharacterSelectIdleFrameMs;
using mir2::client::kCharacterSelectSelectedFrameCount;
using mir2::client::kCharacterSelectUnfreezeFrameMs;
using mir2::client::tests::json_bool_after;
using mir2::client::tests::json_int_after;
using mir2::client::tests::json_string_after;
using mir2::client::tests::manifest_slice;
using mir2::client::tests::read_text_file;

std::string join(const std::vector<int>& values) {
  std::ostringstream out;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      out << ',';
    }
    out << values[index];
  }
  return out.str();
}

std::string bool_text(const bool value) { return value ? "1" : "0"; }

std::vector<int> range(const int first, const int count) {
  std::vector<int> values;
  for (int index = 0; index < count; ++index) {
    values.push_back(first + index);
  }
  return values;
}

std::vector<int> reverse_range(const int last, const int count) {
  std::vector<int> values;
  for (int index = 0; index < count; ++index) {
    values.push_back(last - index);
  }
  return values;
}

struct DoorState {
  int cur_frame{0};
  std::uint64_t frame_time_ms{0};
  bool do_fade_out{false};
  bool do_fade_in{false};
  int fade_index{0};
};

std::string render_timeline_text(const std::string& manifest) {
  const auto login_door = manifest_slice(manifest, "\"login_door\": {", 2600);
  const auto fade = manifest_slice(manifest, "\"fade\": {", 2600);
  const auto character = manifest_slice(manifest, "\"character_select\": {", 5200);

  const auto door_frame_start = json_int_after(login_door, "resolved_frame_start");
  const auto door_frame_end = json_int_after(login_door, "resolved_frame_end");
  const auto door_frame_count = json_int_after(login_door, "frame_count");
  const auto door_advance_ms = json_int_after(login_door, "advance_ms");
  const auto door_fade_trigger_frame = json_int_after(login_door, "fade_trigger_frame");
  const auto door_initial_fade_index = json_int_after(login_door, "fade_index");
  assert(json_string_after(login_door, "advance_operator") == ">");
  assert(json_bool_after(login_door, "advance_before_draw"));
  assert(door_frame_start == 23);
  assert(door_frame_end == 32);
  assert(json_int_after(manifest_slice(login_door, "\"door_draw_origin\""), "x") == 152);
  assert(json_int_after(manifest_slice(login_door, "\"door_draw_origin\""), "y") == 96);
  assert(json_string_after(login_door, "sound") == "s_rock_door_open");

  const auto main_paint_fade_index =
      json_int_after(fade, "main_paint_do_fade_out_fade_index");
  const auto scene_change_fade_index = json_int_after(fade, "scene_change_when_fade_index_lte");
  assert(json_string_after(fade, "gradient_branch") == "commented_out_in_ClMain_FormPaint");

  DoorState door;
  const std::vector<std::uint64_t> door_times{0,    230,  231,  461,  462,  692,  693,
                                             923,  924,  1154, 1155, 1385, 1386, 1616,
                                             1617, 1847, 1848, 2078, 2079, 2080};

  std::ostringstream out;
  out << "# asset-free Delphi auth animation timeline\n";
  for (const auto now_ms : door_times) {
    if (now_ms >= door.frame_time_ms &&
        now_ms - door.frame_time_ms > static_cast<std::uint64_t>(door_advance_ms)) {
      door.frame_time_ms = now_ms;
      ++door.cur_frame;
    }
    if (door.cur_frame >= door_frame_count - 1) {
      door.cur_frame = door_frame_count - 1;
      if (!door.do_fade_out && !door.do_fade_in) {
        door.do_fade_out = true;
        door.do_fade_in = true;
        door.fade_index = door_initial_fade_index;
      }
    }
    const auto fade_before_main = door.fade_index;
    const auto scene_ready =
        door.do_fade_out && door.fade_index <= scene_change_fade_index;
    if (door.do_fade_out) {
      door.fade_index = main_paint_fade_index;
    }
    out << "login_door t=" << now_ms << " cur=" << door.cur_frame
        << " sprite=" << door_frame_start + door.cur_frame
        << " fade_out=" << bool_text(door.do_fade_out)
        << " fade_in=" << bool_text(door.do_fade_in)
        << " fade_before_main=" << fade_before_main
        << " fade_after_main=" << door.fade_index
        << " scene_ready=" << bool_text(scene_ready) << '\n';
  }
  assert(door.cur_frame == door_fade_trigger_frame);

  out << "door_fade trigger=frame9 flags=DoFadeOut+DoFadeIn initial=29 main_paint=1 "
         "next_login_scene=stSelectChr\n";
  out << "start_character_fast_fade trigger=SelChrStartClick flags=DoFastFadeOut initial=29 "
         "main_paint=1 scene_change=server_driven\n";

  const auto idle_start = json_int_after(character, "idle_frame_start");
  const auto freeze_start = json_int_after(character, "freeze_frame_start");
  const auto effect_start = json_int_after(character, "effect_frame_start");
  const auto effect_end = json_int_after(character, "effect_frame_end");
  const auto selected_count = json_int_after(character, "selected_frame_count");
  const auto freeze_count = json_int_after(character, "freeze_frame_count");
  const auto effect_count = json_int_after(character, "effect_frame_count");
  assert(json_string_after(character, "advance_operator") == ">");
  assert(json_bool_after(character, "draw_before_advance"));
  assert(json_int_after(character, "selected_advance_ms") == 300);
  assert(json_int_after(character, "unfreeze_advance_ms") == 120);
  assert(json_int_after(character, "effect_advance_ms") == 110);
  assert(json_int_after(character, "freeze_advance_ms") == 50);

  const auto job = 1;
  const auto sex = 0;
  const auto idle_base = idle_start + job * 40 + sex * 120;
  const auto freeze_base = freeze_start + job * 40 + sex * 120;

  const auto unfreeze_body = range(freeze_base, freeze_count);
  const auto freeze_body = reverse_range(freeze_base + freeze_count - 1, freeze_count);
  const auto effect_frames = range(effect_start, effect_count);
  auto idle_body = range(idle_base, selected_count);
  idle_body.push_back(idle_base);

  assert((unfreeze_body == std::vector<int>{100, 101, 102, 103, 104, 105, 106, 107, 108,
                                            109, 110, 111, 112}));
  assert((freeze_body == std::vector<int>{112, 111, 110, 109, 108, 107, 106, 105, 104,
                                          103, 102, 101, 100}));
  assert(effect_frames.front() == 4);
  assert(effect_frames.back() == 17);
  assert(effect_end == 17);
  assert(idle_body.front() == 80);
  assert(idle_body[15] == 95);
  assert(idle_body.back() == 80);

  out << "character_unfreeze job=1 sex=0 slot=1 origin=417,48 body_sprites="
      << join(unfreeze_body) << " threshold=>120 complete=idle0 draw_before_advance=1\n";
  out << "character_unfreeze_effect slot=1 origin=430,60 effect_sprites="
      << join(effect_frames) << " threshold=>110 draw_blend=1\n";
  out << "character_freeze job=1 sex=0 slot=0 origin=77,46 body_sprites="
      << join(freeze_body) << " threshold=>50 complete=frozen0 draw_before_advance=1\n";
  out << "character_idle job=1 sex=0 slot=0 origin=77,46 body_sprites=" << join(idle_body)
      << " threshold=>300 wrap=1 draw_before_advance=1\n";

  CharacterSelectVisualState cxx_visual;
  cxx_visual.reset(2, 0, 1000);
  assert(cxx_visual.select_slot(1, 2, 2000));
  cxx_visual.update(2000 + kCharacterSelectUnfreezeFrameMs);
  assert(cxx_visual.pose_for(1).body_frame == 0);
  cxx_visual.update(2000 + kCharacterSelectUnfreezeFrameMs + 1U);
  assert(cxx_visual.pose_for(1).body_frame == 1);
  assert(cxx_visual.pose_for(1).kind == CharacterSelectPoseKind::unfreezing);
  assert(kCharacterSelectSelectedFrameCount == selected_count);
  assert(kCharacterSelectFreezeFrameCount == freeze_count);
  assert(kCharacterSelectEffectFrameCount == effect_count);
  assert(kCharacterSelectIdleFrameMs == 300U);
  return out.str();
}

}  // namespace

int main() {
  const auto source_dir = std::filesystem::path{MIR2_CLIENT_SOURCE_DIR};
  const auto manifest =
      read_text_file(source_dir / "tests" / "golden" / "delphi_ui" / "auth_ui_manifest.json");
  const auto expected = read_text_file(
      source_dir / "tests" / "golden" / "delphi_ui" / "auth_animation_timeline_golden.txt");
  assert(render_timeline_text(manifest) == expected);

  const auto deltas = read_text_file(
      source_dir / "tests" / "golden" / "delphi_ui" / "auth_animation_known_deltas.txt");
  assert(deltas.find("login_door_runtime=not_implemented") != std::string::npos);
  assert(deltas.find("door_fade_runtime=not_implemented") != std::string::npos);
  assert(deltas.find("character_select_timing=modern_update_before_render") != std::string::npos);
  assert(deltas.find("character_select_effect_after_frame_17=modern_wraps") !=
         std::string::npos);
  return 0;
}
