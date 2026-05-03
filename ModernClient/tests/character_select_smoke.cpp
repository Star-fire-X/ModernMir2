#include "scene/character_select_state.hpp"

#include <cassert>

int main() {
  using namespace mir2::client;

  CharacterSelectVisualState visual;
  visual.reset(2, 0, 1000);

  assert(visual.slot(0).valid);
  assert(visual.slot(0).selected);
  assert(!visual.slot(0).freeze_state);
  assert(visual.can_delete(0, 2));
  assert(visual.pose_for(0).kind == CharacterSelectPoseKind::idle);
  assert(visual.pose_for(0).body_frame == 0);

  assert(visual.slot(1).valid);
  assert(!visual.slot(1).selected);
  assert(visual.slot(1).freeze_state);
  assert(!visual.can_delete(1, 2));
  assert(visual.pose_for(1).kind == CharacterSelectPoseKind::frozen);

  assert(!visual.select_slot(0, 2, 1500));
  assert(!visual.slot(0).unfreezing);
  assert(visual.pose_for(0).kind == CharacterSelectPoseKind::idle);

  assert(visual.select_slot(1, 2, 2000));
  assert(visual.slot(1).selected);
  assert(visual.slot(1).unfreezing);
  assert(visual.slot(1).freeze_state);
  assert(!visual.can_delete(1, 2));
  assert(visual.slot(0).freezing);
  assert(!visual.can_delete(0, 2));

  visual.update(2050);
  assert(visual.pose_for(0).kind == CharacterSelectPoseKind::freezing);
  assert(visual.pose_for(0).body_frame == 12);
  visual.update(2051);
  assert(visual.pose_for(0).body_frame == 11);

  visual.update(2110);
  assert(visual.pose_for(1).effect_frame == 0);
  visual.update(2111);
  assert(visual.pose_for(1).draw_effect);
  assert(visual.pose_for(1).effect_frame == 1);

  visual.update(2120);
  assert(visual.pose_for(1).body_frame == 0);
  visual.update(2121);
  assert(visual.pose_for(1).body_frame == 1);

  CharacterSelectVisualState freezing_visual;
  freezing_visual.reset(2, 0, 1000);
  assert(freezing_visual.select_slot(1, 2, 2000));
  auto now = 2000ULL;
  for (int i = 0; i < kCharacterSelectFreezeFrameCount; ++i) {
    now += kCharacterSelectFreezeFrameMs + 1U;
    freezing_visual.update(now);
  }
  assert(freezing_visual.slot(0).freeze_state);
  assert(!freezing_visual.slot(0).freezing);

  CharacterSelectVisualState unfreeze_visual;
  unfreeze_visual.reset(2, 0, 1000);
  assert(unfreeze_visual.select_slot(1, 2, 2000));
  now = 2000ULL;
  for (int i = 0; i < kCharacterSelectFreezeFrameCount; ++i) {
    now += kCharacterSelectUnfreezeFrameMs + 1U;
    unfreeze_visual.update(now);
  }
  assert(!unfreeze_visual.slot(1).freeze_state);
  assert(!unfreeze_visual.slot(1).unfreezing);
  assert(unfreeze_visual.can_delete(1, 2));
  assert(unfreeze_visual.pose_for(1).kind == CharacterSelectPoseKind::idle);
  assert(unfreeze_visual.pose_for(1).body_frame == 0);

  unfreeze_visual.update(now + kCharacterSelectIdleFrameMs);
  assert(unfreeze_visual.pose_for(1).body_frame == 0);
  unfreeze_visual.update(now + kCharacterSelectIdleFrameMs + 1U);
  assert(unfreeze_visual.pose_for(1).body_frame == 1);

  visual.reset(0, -1, 9000);
  assert(!visual.can_delete(-1, 0));
  assert(!visual.slot(0).valid);
  return 0;
}
