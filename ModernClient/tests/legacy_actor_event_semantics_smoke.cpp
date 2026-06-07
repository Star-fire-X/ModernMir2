#include "game/game_state.hpp"
#include "shared/legacy/action_ids.hpp"

#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

using namespace mir2::client;
using namespace mir2::client_v1;
namespace legacy = mir2::legacy;

namespace {

GameStateStore make_store() {
  GameStateStore state;
  WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.width = 100;
  snapshot.height = 100;
  snapshot.self_actor_id = 1;
  snapshot.actors.push_back(WorldActor{1, "Hero", 10, 10, 0, 100, 1, ActorType::player});
  snapshot.actors.push_back(WorldActor{2, "Target", 11, 10, 4, 200, 2, ActorType::monster});
  snapshot.actors.push_back(WorldActor{3, "Leaving", 12, 10, 4, 300, 3, ActorType::monster});
  state.apply(snapshot);
  return state;
}

std::size_t queued_death_message_count(const ActorState& actor) {
  std::size_t count = 0;
  for (const auto& message : actor.legacy_action_queue) {
    if (message.kind == LegacyActorMessage::Kind::death) {
      ++count;
    }
  }
  return count;
}

void test_action_order_is_event_driven() {
  auto state = make_store();
  state.apply(ActorAction{2, ActorActionKind::turn, 11, 10, 4, 0, 0, 10, 0, false, 0});
  state.apply(ActorAction{2, ActorActionKind::walk, 12, 10, 2, 0, 0, legacy::kSmWalk, 0, false, 0});
  state.apply(ActorVitals{2, 5, 12, -1, -1, 7, 1, false, 31});
  assert(state.world.actors[2].legacy_action_queue.size() == 2);
  state.process_legacy_actor_queues(1000);
  assert(state.world.actors[2].current_action == ActorActionKind::turn);
  state.world.actors[2].action_started_ms = 0;
  state.process_legacy_actor_queues(1200);
  assert(state.world.actors[2].current_action == ActorActionKind::walk);
  state.apply(ActorAction{2, ActorActionKind::struck, 0, 0, 0, 1, 7, 31, 0, false, 0});
  state.apply(ActorDeath{2, 12, 10, 2, 32});
  assert(state.world.actors[2].hp == 0);
  state.world.actors[2].action_started_ms = 0;
  state.process_legacy_actor_queues(1400);
  state.world.actors[2].action_started_ms = 0;
  state.process_legacy_actor_queues(1600);

  const auto& actor = state.world.actors[2];
  assert(actor.dead);
  assert(actor.legacy_death_mode == LegacyDeathMode::instant_corpse);
  assert(actor.current_action == ActorActionKind::turn);
  assert(actor.legacy_pending_actions.size() == 4);
  assert(actor.legacy_pending_actions[0].legacy_action_ident == 10);
  assert(actor.legacy_pending_actions[1].legacy_action_ident == legacy::kSmWalk);
  assert(actor.legacy_pending_actions[2].legacy_action_ident == 31);
  assert(actor.legacy_pending_actions[3].legacy_action_ident == 32);
}

void test_nowdeath_uses_play_death_mode() {
  auto state = make_store();
  state.apply(ActorDeath{2, 12, 10, 2, legacy::kSmNowDeath});
  state.process_legacy_actor_queues(1000);
  const auto& actor = state.world.actors[2];
  assert(actor.dead);
  assert(actor.legacy_death_mode == LegacyDeathMode::play_death_anim);
}

void test_vitals_revive_clears_dead_state() {
  auto state = make_store();
  state.apply(ActorDeath{2, 12, 10, 2, legacy_sm::kDeath});
  state.process_legacy_actor_queues(1000);
  state.world.actors[2].skeleton = true;
  state.apply(ActorVitals{2, 30, 40, 10, 20, 0, 0, false, 0});
  const auto& actor = state.world.actors[2];
  assert(!actor.dead);
  assert(!actor.skeleton);
  assert(actor.current_action == ActorActionKind::turn);
  assert(actor.legacy_action_ident == legacy_sm::kTurn);
}

void test_vitals_revive_clears_queued_death() {
  auto state = make_store();
  state.apply(ActorAction{2, ActorActionKind::hit, 11, 10, 4, 1, 0, legacy::kSmHit, 0, false, 0});
  state.process_legacy_actor_queues(1000);
  state.world.actors[2].action_started_ms = 2000;
  state.world.actors[2].action_duration_ms = 10000;
  state.apply(ActorDeath{2, 11, 10, 4, legacy_sm::kDeath});
  assert(queued_death_message_count(state.world.actors[2]) == 1);
  state.apply(ActorVitals{2, -1, -1, 10, 20, 0, 0, false, 0});
  assert(queued_death_message_count(state.world.actors[2]) == 1);
  state.apply(ActorVitals{2, 30, 40, 10, 20, 0, 0, false, 0});
  assert(queued_death_message_count(state.world.actors[2]) == 0);
  state.world.actors[2].action_started_ms = 0;
  state.world.actors[2].action_duration_ms = 0;
  state.process_legacy_actor_queues(3000);
  const auto& actor = state.world.actors[2];
  assert(!actor.dead);
  assert(actor.hp == 30);
}

void test_spell_starts_waiting_for_magic_fire() {
  auto state = make_store();
  state.apply(MagicList{{MagicEntry{9, 1, 0, 0, 1000, "Fireball", 1, 900, 1}}});
  state.apply(ActorAction{1, ActorActionKind::spell, 13, 10, 2, 2, 0, 17, 9, true, 1});
  state.process_legacy_actor_queues(1000);
  const auto& actor = state.world.actors[1];
  assert(actor.current_action == ActorActionKind::spell);
  assert(actor.action_magic);
  assert(actor.action_magic_effect == 1);
  assert(actor.action_magic_effect_type == -1);
  assert(!actor.action_magic_failed);
}

void test_magic_fire_is_hurry_event() {
  auto state = make_store();
  state.apply(MagicList{{MagicEntry{9, 1, 0, 0, 1000, "Fireball", 1, 900, 1}}});
  state.apply(ActorAction{1, ActorActionKind::spell, 13, 10, 2, 2, 0, 17, 9, true, 1});
  state.process_legacy_actor_queues(1000);
  assert(state.world.actors[1].action_magic_effect == 1);
  assert(state.world.actors[1].action_magic_effect_type == -1);
  const auto before = state.world.actors[1].legacy_pending_actions.size();
  state.apply(ActorMagicFire{1, 2, 13, 10, 1, 1, 638});
  state.process_legacy_actor_queues(1001);
  assert(state.world.actors[1].legacy_pending_actions.size() == before);
  assert(state.world.actors[1].action_magic_effect_type == -1);
  state.process_legacy_actor_hurry_queues(1001);
  assert(state.world.actors[1].legacy_pending_actions.size() == before + 1);
  assert(state.world.actors[1].legacy_pending_actions.back().legacy_event_priority ==
         LegacyEventPriority::hurry);
  assert(state.world.actors[1].action_magic);
  assert(state.world.actors[1].action_magic_effect_type == 5);
  state.apply(ActorMagicFireFail{1, 639});
  state.process_legacy_actor_queues(1002);
  assert(state.world.actors[1].action_magic);
  state.process_legacy_actor_hurry_queues(1002);
  assert(state.world.actors[1].legacy_pending_actions.back().legacy_event_priority ==
         LegacyEventPriority::hurry);
  assert(!state.world.actors[1].action_magic);
  assert(state.world.actors[1].action_magic_failed);
}

void test_magic_fire_waits_behind_unstarted_spell() {
  auto state = make_store();
  state.apply(MagicList{{MagicEntry{9, 1, 0, 0, 1000, "Fireball", 1, 900, 1}}});
  state.apply(ActorAction{1, ActorActionKind::hit, 10, 10, 2, 1, 0, legacy::kSmHit, 0, false, 0});
  state.process_legacy_actor_queues(1000);
  state.world.actors[1].action_started_ms = 2000;
  state.world.actors[1].action_duration_ms = 10000;

  state.apply(ActorAction{1, ActorActionKind::spell, 13, 10, 2, 2, 0, 17, 9, true, 1});
  state.apply(ActorMagicFire{1, 2, 13, 10, 1, 1, 638});
  assert(state.world.actors[1].legacy_action_queue.size() == 2);
  const auto before_events = state.world.actors[1].legacy_pending_actions.size();

  state.process_legacy_actor_queues(3000);
  state.process_legacy_actor_hurry_queues(3000);
  const auto& actor = state.world.actors[1];
  assert(actor.current_action == ActorActionKind::hit);
  assert(!actor.action_magic);
  assert(actor.magic_id == 0);
  assert(actor.action_magic_effect == 0);
  assert(actor.action_magic_effect_type == -1);
  assert(actor.legacy_action_queue.size() == 2);
  assert(actor.legacy_action_queue.front().kind == LegacyActorMessage::Kind::action);
  assert(actor.legacy_pending_actions.size() == before_events);
}

void test_active_spell_consumes_magic_fire_behind_normal_head() {
  auto state = make_store();
  state.apply(MagicList{{MagicEntry{9, 1, 0, 0, 1000, "Fireball", 1, 900, 1}}});
  state.apply(ActorAction{1, ActorActionKind::spell, 13, 10, 2, 2, 0, 17, 9, true, 1});
  state.process_legacy_actor_queues(1000);
  assert(state.world.actors[1].current_action == ActorActionKind::spell);
  assert(state.world.actors[1].action_magic_effect_type == -1);

  state.apply(ActorAction{1, ActorActionKind::turn, 10, 10, 2, 0, 0, 10, 0, false, 0});
  state.apply(ActorMagicFire{1, 2, 13, 10, 1, 1, 638});
  assert(state.world.actors[1].legacy_action_queue.size() == 2);
  assert(state.world.actors[1].legacy_action_queue.front().kind == LegacyActorMessage::Kind::action);
  const auto before_events = state.world.actors[1].legacy_pending_actions.size();

  state.process_legacy_actor_queues(1100);
  assert(state.world.actors[1].action_magic_effect_type == -1);
  assert(state.world.actors[1].legacy_action_queue.size() == 2);
  state.process_legacy_actor_hurry_queues(1100);
  const auto& actor = state.world.actors[1];
  assert(actor.current_action == ActorActionKind::spell);
  assert(actor.action_magic);
  assert(actor.action_magic_effect_type == 5);
  assert(actor.legacy_action_queue.size() == 1);
  assert(actor.legacy_action_queue.front().kind == LegacyActorMessage::Kind::action);
  assert(actor.legacy_pending_actions.size() == before_events + 1);
  assert(actor.legacy_pending_actions.back().legacy_event_priority == LegacyEventPriority::hurry);
}

void test_spell_and_magic_fire_share_queue_tick() {
  auto state = make_store();
  state.apply(MagicList{{MagicEntry{9, 1, 0, 0, 1000, "Fireball", 1, 900, 1}}});
  state.apply(ActorAction{1, ActorActionKind::spell, 13, 10, 2, 2, 0, 17, 9, true, 1});
  state.apply(ActorMagicFire{1, 2, 13, 10, 1, 1, 638});
  state.process_legacy_actor_queues(1000);
  assert(state.world.actors[1].current_action == ActorActionKind::spell);
  assert(state.world.actors[1].action_magic_effect_type == -1);
  state.process_legacy_actor_hurry_queues(1000);
  const auto& actor = state.world.actors[1];
  assert(actor.current_action == ActorActionKind::spell);
  assert(actor.action_magic);
  assert(actor.action_magic_effect == 1);
  assert(actor.action_magic_effect_type == 5);
}

void test_struck_cancels_plain_hit_only() {
  auto state = make_store();
  state.apply(ActorAction{2, ActorActionKind::hit, 11, 10, 4, 1, 0, legacy::kSmHit, 0, false, 0});
  state.process_legacy_actor_queues(1000);
  state.world.actors[2].action_started_ms = 1000;
  state.world.actors[2].action_duration_ms = 10000;

  state.apply(ActorAction{2, ActorActionKind::struck, 0, 0, 4, 1, 7, 31, 0, false, 0});
  state.process_legacy_actor_queues(1100);
  const auto& actor = state.world.actors[2];
  assert(actor.current_action == ActorActionKind::struck);
  assert(actor.legacy_struck_frame_ms == legacy_struck_frame_time_ms(actor.level));
  assert(actor.action_duration_ms == actor.legacy_struck_frame_ms * 3U);
  assert(actor.last_damage == 7);
  assert(actor.legacy_action_queue.empty());
}

void test_struck_does_not_cancel_spell_or_self_hit() {
  auto state = make_store();
  state.apply(MagicList{{MagicEntry{9, 1, 0, 0, 1000, "Fireball", 1, 900, 1}}});
  state.apply(ActorAction{1, ActorActionKind::spell, 13, 10, 2, 2, 0, 17, 9, true, 1});
  state.process_legacy_actor_queues(1000);
  state.world.actors[1].action_started_ms = 1000;
  state.world.actors[1].action_duration_ms = 10000;
  state.apply(ActorAction{1, ActorActionKind::struck, 0, 0, 2, 2, 5, 31, 0, false, 0});
  state.process_legacy_actor_queues(1100);
  assert(state.world.actors[1].current_action == ActorActionKind::spell);
  assert(state.world.actors[1].legacy_action_queue.size() == 1);

  state = make_store();
  state.world.actors[1].name_color = 249;
  state.apply(ActorAction{1, ActorActionKind::hit, 10, 10, 2, 1, 0, legacy::kSmHit, 0, false, 0});
  state.process_legacy_actor_queues(2000);
  state.world.actors[1].action_started_ms = 2000;
  state.world.actors[1].action_duration_ms = 10000;
  state.apply(ActorAction{1, ActorActionKind::struck, 0, 0, 2, 2, 5, 31, 0, false, 0});
  assert(state.world.latest_struck_ms != 0);
  state.process_legacy_actor_queues(2100);
  assert(state.world.actors[1].current_action == ActorActionKind::hit);
  assert(state.world.actors[1].legacy_action_queue.size() == 1);
}

void test_struck_cancels_structure_plain_hit() {
  auto state = make_store();
  state.world.actors[2].feature = 98;
  state.apply(ActorAction{2, ActorActionKind::hit, 11, 10, 4, 1, 0, legacy::kSmHit, 0, false, 0});
  state.process_legacy_actor_queues(2000);
  state.world.actors[2].action_started_ms = 2000;
  state.world.actors[2].action_duration_ms = 10000;
  state.apply(ActorAction{2, ActorActionKind::struck, 0, 0, 4, 1, 7, 31, 0, false, 0});
  state.process_legacy_actor_queues(2100);
  assert(state.world.actors[2].current_action == ActorActionKind::struck);
  assert(state.world.actors[2].legacy_action_queue.empty());
}

void test_struck_does_not_cancel_effect_hit() {
  auto state = make_store();
  state.world.actors[2].feature = 49;
  state.apply(ActorAction{2, ActorActionKind::hit, 11, 10, 4, 1, 0, legacy::kSmHit, 0, false, 0});
  state.process_legacy_actor_queues(2000);
  state.world.actors[2].action_started_ms = 2000;
  state.world.actors[2].action_duration_ms = 10000;
  state.apply(ActorAction{2, ActorActionKind::struck, 0, 0, 4, 1, 7, 31, 0, false, 0});
  state.process_legacy_actor_queues(2100);
  assert(state.world.actors[2].current_action == ActorActionKind::hit);
  assert(state.world.actors[2].legacy_action_queue.size() == 1);
}

void test_self_struck_frame_time_uses_level() {
  auto ordinary = make_store();
  ordinary.apply(ActorAction{1, ActorActionKind::struck, 0, 0, 2, 2, 5, 31, 0, false, 0});
  assert(ordinary.world.latest_struck_ms == 0);

  auto state = make_store();
  state.world.actors[1].name_color = 249;
  SelfAbility ability;
  ability.level = 20;
  state.apply(ability);
  state.apply(ActorAction{1, ActorActionKind::struck, 0, 0, 2, 2, 5, 31, 0, false, 0});
  assert(state.world.latest_struck_ms != 0);
  state.process_legacy_actor_queues(1000);
  const auto& actor = state.world.actors[1];
  assert(actor.current_action == ActorActionKind::struck);
  assert(actor.legacy_struck_frame_ms == 100);
  assert(actor.action_duration_ms == 300);
}

void test_self_struck_bundle_updates_latest_on_receive() {
  auto state = make_store();
  state.world.actors[1].name_color = 249;
  std::vector<Message> messages;
  messages.emplace_back(ActorAction{1, ActorActionKind::struck, 0, 0, 2, 2, 5, 31, 0, false, 0});
  state.enqueue_legacy_actor_bundle(std::move(messages));
  assert(state.world.latest_struck_ms != 0);
}

void test_remove_delay_matches_legacy_hide() {
  auto state = make_store();
  state.apply(ActorRemove{3, 30});
  assert(state.world.actors.find(3) == state.world.actors.end());

  state.apply(ActorAction{2, ActorActionKind::hit, 11, 10, 4, 1, 0, legacy::kSmHit, 0, false, 0});
  state.process_legacy_actor_queues(1000);
  state.world.actors[2].action_started_ms = detail::monotonic_ms();
  state.apply(ActorRemove{2, 30});
  assert(state.world.actors.find(2) != state.world.actors.end());
  assert(state.world.actors[2].pending_remove);
  state.world.actors[2].action_started_ms = 0;
  state.prune_pending_actor_removals(detail::monotonic_ms());
  assert(state.world.actors.find(2) == state.world.actors.end());
}

void test_grouped_flags_refresh_from_group_members() {
  auto state = make_store();
  state.apply(GroupState{true, true, {"Hero", "Target"}});
  state.refresh_grouped_actor_flags();
  assert(state.world.actors[1].grouped);
  assert(state.world.actors[2].grouped);
  assert(!state.world.actors[3].grouped);

  state.apply(GroupState{true, true, {"Hero"}});
  state.refresh_grouped_actor_flags();
  assert(state.world.actors[1].grouped);
  assert(!state.world.actors[2].grouped);
}

void test_pending_remove_expires_after_legacy_minute() {
  auto state = make_store();
  state.world.actors[2].pending_remove = true;
  state.world.actors[2].pending_remove_started_ms = 1000;
  state.world.actors[2].action_started_ms = 1000;
  state.world.actors[2].action_duration_ms = 120000;

  state.prune_pending_actor_removals(60999);
  assert(state.world.actors.find(2) != state.world.actors.end());
  state.prune_pending_actor_removals(61000);
  assert(state.world.actors.find(2) == state.world.actors.end());
}

void test_authoritative_actor_update_clears_pending_remove() {
  auto state = make_store();
  auto& actor = state.world.actors[2];
  actor.pending_remove = true;
  actor.pending_remove_started_ms = 1000;
  actor.action_started_ms = 1000;
  actor.action_duration_ms = 120000;
  state.apply(ActorVitals{2, 30, 40, 10, 20, 0, 0, false, 0});
  assert(!state.world.actors[2].pending_remove);
  state.prune_pending_actor_removals(61000);
  assert(state.world.actors.find(2) != state.world.actors.end());

  auto& refreshed = state.world.actors[2];
  refreshed.pending_remove = true;
  refreshed.pending_remove_started_ms = 1000;
  state.apply(ActorAction{2, ActorActionKind::turn, 11, 10, 4, 0, 0, 10, 0, false, 0});
  assert(!state.world.actors[2].pending_remove);

  state.world.actors[2].pending_remove = true;
  state.world.actors[2].pending_remove_started_ms = 1000;
  state.apply(ActorUpsert{WorldActor{2, "Target", 11, 10, 4, 200, 2, ActorType::monster}});
  assert(!state.world.actors[2].pending_remove);

  std::vector<Message> messages;
  state.world.actors[2].pending_remove = true;
  state.world.actors[2].pending_remove_started_ms = 1000;
  messages.emplace_back(ActorAction{2, ActorActionKind::turn, 11, 10, 4, 0, 0, 10, 0, false, 0});
  state.enqueue_legacy_actor_bundle(std::move(messages));
  assert(!state.world.actors[2].pending_remove);
  state.prune_pending_actor_removals(61000);
  assert(state.world.actors.find(2) != state.world.actors.end());
}

void test_actor_queue_remove_waits_for_digdown_action() {
  auto state = make_store();
  std::vector<Message> messages;
  messages.emplace_back(ActorAction{2, ActorActionKind::turn, 11, 10, 4, 0, 0,
                                    legacy::kSmDigDown, 0, false, 0});
  messages.emplace_back(ActorRemove{2, legacy::kSmDigDown});
  state.enqueue_legacy_actor_bundle(std::move(messages));

  state.process_legacy_actor_queues(1000);
  assert(state.world.actors.find(2) != state.world.actors.end());
  assert(state.world.actors[2].legacy_action_ident == legacy::kSmDigDown);
  assert(state.world.actors[2].action_duration_ms == 510);
  assert(state.world.actors[2].pending_remove);

  state.prune_pending_actor_removals(1509);
  assert(state.world.actors.find(2) != state.world.actors.end());
  state.prune_pending_actor_removals(1510);
  assert(state.world.actors.find(2) == state.world.actors.end());
}

void test_independent_visual_state_events() {
  auto state = make_store();
  state.apply(ActorIdentityUpdate{2,
                                  static_cast<std::uint8_t>(
                                      kActorIdentityName | kActorIdentityNameColor |
                                      kActorIdentityFeature | kActorIdentityStatus),
                                  "Renamed",
                                  0xFF00AA00U,
                                  777,
                                  888});
  assert(state.world.actors[2].name == "Renamed");
  assert(state.world.actors[2].feature == 777);
  assert(state.world.actors[2].status == 888);
  assert(state.world.actors[2].name_color == 0xFF00AA00U);

  state.apply(ActorIdentityUpdate{99, kActorIdentityName, "Ghost", 0, 0, 0});
  assert(state.world.actors.find(99) == state.world.actors.end());
}

void test_actor_action_queue_does_not_drop_fifo_after_64_messages() {
  auto state = make_store();
  for (int index = 0; index < 65; ++index) {
    state.apply(ActorAction{2, ActorActionKind::turn, 11, 10,
                            static_cast<std::uint8_t>(index % 8), 0, 0,
                            static_cast<std::uint16_t>(100 + index), 0, false, 0});
  }
  assert(state.world.actors[2].legacy_action_queue.size() == 65);
  state.process_legacy_actor_queues(1000);
  assert(state.world.actors[2].legacy_action_ident == 100);
}

}  // namespace

int main() {
  test_action_order_is_event_driven();
  test_nowdeath_uses_play_death_mode();
  test_vitals_revive_clears_dead_state();
  test_vitals_revive_clears_queued_death();
  test_spell_starts_waiting_for_magic_fire();
  test_magic_fire_is_hurry_event();
  test_magic_fire_waits_behind_unstarted_spell();
  test_active_spell_consumes_magic_fire_behind_normal_head();
  test_spell_and_magic_fire_share_queue_tick();
  test_struck_cancels_plain_hit_only();
  test_struck_does_not_cancel_spell_or_self_hit();
  test_struck_cancels_structure_plain_hit();
  test_struck_does_not_cancel_effect_hit();
  test_self_struck_frame_time_uses_level();
  test_self_struck_bundle_updates_latest_on_receive();
  test_remove_delay_matches_legacy_hide();
  test_grouped_flags_refresh_from_group_members();
  test_pending_remove_expires_after_legacy_minute();
  test_authoritative_actor_update_clears_pending_remove();
  test_actor_queue_remove_waits_for_digdown_action();
  test_independent_visual_state_events();
  test_actor_action_queue_does_not_drop_fifo_after_64_messages();
  return 0;
}
