#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config/models.hpp"
#include "core/messages.hpp"
#include "shared/legacy/map_document.hpp"
#include "core/wheel_timer.hpp"
#include "world/game_object.hpp"
#include "world/legacy_map_environment.hpp"
#include "world/legacy_random.hpp"
#include "world/make_index_allocator.hpp"

namespace mir2 {

struct LegacyMagicTrainResult;

class MapActor {
 public:
  struct GroundItem {
    std::uint64_t id{0};
    bool is_gold{false};
    std::int32_t gold_amount{0};
    LegacyUserItem item{};
    std::string name{};
    std::int32_t count{1};
    std::int32_t looks{0};
    std::int32_t ani_count{0};
    std::int32_t x{0};
    std::int32_t y{0};
    std::uint64_t owner_actor_id{0};
    std::uint64_t drop_time_ms{0};
    std::uint64_t ownership_expire_ms{0};
    std::uint64_t expire_time_ms{0};
    std::uint64_t dropper_actor_id{0};
    std::string dropper_name{};
    bool death_drop{false};
  };

  struct LegacyGmCommandResult {
    RuntimeDispatch dispatch{};
    bool handled{false};
    bool success{false};
    std::string reason{};
    std::vector<std::string> messages{};
  };

  MapActor(MapConfig config, LogicBudgetConfig budgets,
           std::unordered_map<std::int32_t, ItemConfig> item_configs,
           std::unordered_map<std::int32_t, MagicConfig> magic_configs,
           std::vector<MapQuestConfig> map_quests = {},
           CastleDialogContext castle_dialog_context = {},
           std::unordered_map<std::string, MonsterDefConfig> monster_defs = {},
           std::unordered_map<std::string, MapEntryRuleConfig> map_entry_rules = {},
           MakeIndexAllocator* make_index_allocator = nullptr,
           std::string black_stone_name = "BlackStone",
           bool legacy_approval_mode = false,
           std::shared_ptr<std::array<std::int32_t, 10>> script_global_params = nullptr);

  void enqueue_mail(ActorMail mail);
  void set_legacy_random(LegacyRandom* legacy_random);
  bool apply_merchant_state(const MerchantStateRecord& state);
  void set_castle_dialog_context(CastleDialogContext castle_dialog_context);
  void set_guild_castle_snapshot(GuildCastleSnapshot guild_castle_snapshot);
  [[nodiscard]] RuntimeDispatch drain_pending_mail(std::uint64_t current_tick,
                                                   std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch run_maintenance_tick(std::uint64_t current_tick,
                                                     std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch tick(std::uint64_t current_tick);
  [[nodiscard]] RuntimeDispatch tick(std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch close_expired_doors(std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch legacy_spawn_player(const ActorMail& mail,
                                                    std::uint64_t current_tick,
                                                    std::uint64_t now_ms,
                                                    bool fast_initialize);
  [[nodiscard]] RuntimeDispatch legacy_process_player(std::uint64_t actor_id,
                                                      std::uint64_t current_tick,
                                                      std::uint64_t now_ms,
                                                      bool persistence_overloaded,
                                                      std::size_t player_input_budget_per_tick = 0);
  [[nodiscard]] RuntimeDispatch legacy_process_monster(std::uint64_t actor_id,
                                                       std::uint64_t current_tick,
                                                       std::uint64_t now_ms,
                                                       std::size_t cursor,
                                                       std::size_t sub_cursor);
  [[nodiscard]] bool legacy_monster_alive(std::uint64_t actor_id) const;
  [[nodiscard]] bool legacy_monster_counts_for_spawn(std::uint64_t actor_id) const;
  [[nodiscard]] bool legacy_can_spawn_monster(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] RuntimeDispatch legacy_process_merchant(std::uint64_t actor_id,
                                                        std::uint64_t current_tick,
                                                        std::uint64_t now_ms,
                                                        std::size_t cursor);
  [[nodiscard]] RuntimeDispatch legacy_process_npc(std::uint64_t actor_id,
                                                   std::uint64_t current_tick,
                                                   std::uint64_t now_ms,
                                                   std::size_t cursor);
  [[nodiscard]] bool legacy_add_event_object(std::uint64_t event_id, std::int32_t x,
                                             std::int32_t y, std::uint64_t now_ms,
                                             bool blocks_walk = false,
                                             RuntimeDispatch* dispatch = nullptr,
                                             LegacyEventType type = LegacyEventType::stone_mine);
  [[nodiscard]] bool legacy_add_event_object(std::uint64_t event_id, std::int32_t x,
                                             std::int32_t y, std::uint64_t now_ms,
                                             RuntimeDispatch* dispatch);
  void legacy_remove_event_object(std::uint64_t event_id, std::int32_t x, std::int32_t y,
                                  RuntimeDispatch* dispatch = nullptr);
  [[nodiscard]] RuntimeDispatch legacy_apply_fire_burn_event(const LegacyEventRecord& event,
                                                             std::uint64_t current_tick,
                                                             std::uint64_t now_ms);
  [[nodiscard]] std::vector<std::uint64_t> legacy_active_holy_seize_actor_ids(
      const std::vector<std::uint64_t>& actor_ids, std::uint64_t now_ms) const;
  [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>>
  legacy_random_space_move_target(LegacyRandom& random) const;
  [[nodiscard]] RuntimeDispatch legacy_space_move_player(
      std::uint64_t actor_id, const std::string& target_map_id, std::int32_t target_x,
      std::int32_t target_y, bool show2, std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] LegacyGmCommandResult legacy_apply_gm_command(
      std::uint64_t actor_id, const std::string& command_name,
      const std::vector<std::string>& args, std::uint64_t current_tick,
      std::uint64_t now_ms);
  bool enqueue_legacy_player_command(const ActorMail& mail, std::uint64_t now_ms);
  bool mark_legacy_player_ghost(std::uint64_t actor_id, std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch legacy_disconnect_player(std::uint64_t actor_id,
                                                         std::uint64_t now_ms);
  [[nodiscard]] std::optional<LegacyPlayerState> legacy_player_state(
      std::uint64_t actor_id) const;
  [[nodiscard]] std::size_t legacy_player_inbox_size(std::uint64_t actor_id) const;
  [[nodiscard]] std::vector<std::uint64_t> legacy_player_inbox_session_sequences(
      std::uint64_t actor_id) const;
  [[nodiscard]] std::int64_t legacy_player_run_time_ms(std::uint64_t actor_id) const;
  [[nodiscard]] std::optional<CharacterRecord> snapshot_player(std::uint64_t actor_id) const;
  [[nodiscard]] std::optional<CharacterRecord> persistent_snapshot_player(
      std::uint64_t actor_id, std::uint64_t now_ms);
  [[nodiscard]] std::optional<MonsterSnapshot> legacy_monster_snapshot(
      std::uint64_t actor_id) const;
  [[nodiscard]] bool legacy_set_player_slave_relax(std::uint64_t actor_id, bool value);
  [[nodiscard]] bool legacy_player_tracks_event(std::uint64_t actor_id,
                                                std::uint64_t event_id) const;
  [[nodiscard]] const std::string& id() const { return config_.id; }
  [[nodiscard]] std::size_t object_count() const { return objects_.size(); }

 private:
  struct MonsterSpawnTemplate {
    ActorMail mail{};
  };

  struct PlayerVisibility {
    std::unordered_set<std::uint64_t> actors{};
    std::unordered_set<std::uint64_t> items{};
    std::unordered_set<std::uint64_t> events{};
  };

  struct TradeOffer {
    std::vector<LegacyUserItem> items{};
    std::int32_t gold{0};
    bool accepted{false};
    std::uint64_t last_change_time_ms{0};
  };

  struct TradeSession {
    std::uint64_t id{0};
    std::uint64_t first_actor_id{0};
    std::uint64_t second_actor_id{0};
    TradeOffer first{};
    TradeOffer second{};
  };

  void handle_mail(const ActorMail& mail, RuntimeDispatch& dispatch, std::uint64_t current_tick,
                   std::uint64_t now_ms, bool from_legacy_operate = false);
  void dispatch_legacy_run_notice(Player& player, RuntimeDispatch& dispatch,
                                  std::uint64_t now_ms);
  void dispatch_legacy_initialize(Player& player, RuntimeDispatch& dispatch,
                                  std::uint64_t now_ms);
  void dispatch_legacy_close(Player& player, RuntimeDispatch& dispatch);
  void legacy_operate_player_running(std::uint64_t actor_id, Player& player,
                                     RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick,
                                     std::uint64_t now_ms,
                                     bool persistence_overloaded,
                                     std::size_t player_input_budget_per_tick);
  void trace_player_operate(RuntimeDispatch& dispatch, const Player& player,
                            std::string action, std::uint64_t current_tick,
                            std::uint64_t now_ms, bool success = true,
                            std::int32_t value = 0,
                            std::string label = {}) const;
  void handle_player_health_spell_tick(Player& player, RuntimeDispatch& dispatch,
                                       std::uint64_t current_tick);
  void handle_player_status_effects(Player& player, RuntimeDispatch& dispatch,
                                    std::uint64_t current_tick);
  [[nodiscard]] bool handle_monster_status_effects(Monster& monster, RuntimeDispatch& dispatch,
                                                   std::uint64_t current_tick,
                                                   std::uint64_t now_ms);
  void handle_monster_ai(Monster& monster, RuntimeDispatch& dispatch, std::uint64_t current_tick,
                         std::uint64_t now_ms);
  [[nodiscard]] std::size_t legacy_dup_count(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] bool legacy_try_monster_walk(Monster& monster, std::uint8_t dir,
                                             RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms);
  [[nodiscard]] bool legacy_monster_think(Monster& monster, RuntimeDispatch& dispatch,
                                          std::uint64_t current_tick,
                                          std::uint64_t now_ms);
  void legacy_refresh_monster_visible_actors(Monster& monster);
  [[nodiscard]] bool legacy_monster_valid_target(const Monster& monster,
                                                 const GameObject& target,
                                                 std::uint64_t current_tick) const;
  [[nodiscard]] bool legacy_monster_search_candidate(const Monster& monster,
                                                     const GameObject& target,
                                                     std::uint64_t current_tick) const;
  void legacy_active_search(Monster& monster, RuntimeDispatch& dispatch,
                            std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] bool legacy_monster_normal_attack(Monster& monster,
                                                  RuntimeDispatch& dispatch,
                                                  std::uint64_t current_tick,
                                                  std::uint64_t now_ms);
  [[nodiscard]] bool legacy_attack_target(Monster& monster, RuntimeDispatch& dispatch,
                                          std::uint64_t current_tick,
                                          std::uint64_t now_ms);
  [[nodiscard]] bool legacy_goto_target_xy(Monster& monster, RuntimeDispatch& dispatch,
                                           std::uint64_t current_tick,
                                           std::uint64_t now_ms);
  void legacy_wondering(Monster& monster, RuntimeDispatch& dispatch,
                        std::uint64_t current_tick, std::uint64_t now_ms);
  void legacy_monster_temp_attack(Monster& monster, Player& target,
                                  RuntimeDispatch& dispatch,
                                  std::uint64_t current_tick, std::uint64_t now_ms);
  void legacy_monster_attack_monster(Monster& monster, Monster& target,
                                     RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick,
                                     std::uint64_t now_ms);
  [[nodiscard]] bool legacy_monster_special_run(Monster& monster,
                                                RuntimeDispatch& dispatch,
                                                std::uint64_t current_tick,
                                                std::uint64_t now_ms);
  [[nodiscard]] bool legacy_monster_special_attack_target(Monster& monster,
                                                          RuntimeDispatch& dispatch,
                                                          std::uint64_t current_tick,
                                                          std::uint64_t now_ms);
  void legacy_monster_summon_child(Monster& monster, RuntimeDispatch& dispatch,
                                   std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] Player* legacy_nearest_player_target(const Monster& monster,
                                                     std::uint64_t current_tick,
                                                     std::int32_t max_range,
                                                     bool guard_rules);
  void refresh_moving_object_state(const GameObject& object, std::uint64_t now_ms);
  void award_monster_kill(Player& attacker, const Monster& monster, RuntimeDispatch& dispatch);
  void schedule_monster_respawn(std::uint64_t monster_id, std::uint64_t current_tick);
  void schedule_legacy_magic_lvexp(Player& player, const LegacyMagicTrainResult& training,
                                   RuntimeDispatch& dispatch, const ActorMail& source_mail,
                                   std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] std::optional<ActorMail> build_slave_spawn_mail(
      const std::string& monster_name, Player& master, std::int32_t x, std::int32_t y,
      std::int32_t make_level, std::int32_t exp_level, std::int32_t slave_exp,
      std::uint64_t royalty_time_ms, std::uint64_t life_time_ms,
      std::uint64_t now_ms, std::optional<CharacterSlaveRecord> restored = std::nullopt);
  [[nodiscard]] bool summon_player_slave(Player& master, const std::string& monster_name,
                                         std::int32_t make_level, std::int32_t max_slaves,
                                         std::uint64_t royalty_seconds,
                                         RuntimeDispatch& dispatch,
                                         std::uint64_t current_tick,
                                         std::uint64_t now_ms,
                                         const ActorMail& source_mail);
  [[nodiscard]] bool tame_player_slave(Player& master, Monster& target,
                                       std::int32_t make_level, std::int32_t max_slaves,
                                       RuntimeDispatch& dispatch,
                                       std::uint64_t current_tick,
                                       std::uint64_t now_ms,
                                       const ActorMail& source_mail);
  [[nodiscard]] std::array<CharacterSlaveRecord, kMaxLegacySlaves> snapshot_owned_slaves(
      Player& player, std::uint64_t now_ms);
  [[nodiscard]] CharacterRecord snapshot_player_with_slaves(Player& player,
                                                            std::uint64_t now_ms);
  void queue_save_player_character(RuntimeDispatch& dispatch, Player& player,
                                   std::uint64_t now_ms);
  void restore_saved_slaves(Player& player, RuntimeDispatch& dispatch,
                            std::uint64_t current_tick, std::uint64_t now_ms);
  void detach_owned_slaves(Player& player, RuntimeDispatch& dispatch,
                           std::uint64_t now_ms, bool erase_objects);
  void recall_owned_slaves_to_master(Player& player, RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick,
                                     std::uint64_t now_ms);
  void notify_owned_slaves_target(Player& player, std::uint64_t target_actor_id,
                                  std::uint64_t now_ms);
  void remove_slave_from_master(Monster& slave);
  [[nodiscard]] bool handle_slave_lifecycle(Monster& monster, RuntimeDispatch& dispatch,
                                            std::uint64_t current_tick,
                                            std::uint64_t now_ms);
  [[nodiscard]] bool handle_slave_follow(Monster& monster, RuntimeDispatch& dispatch,
                                         std::uint64_t current_tick,
                                         std::uint64_t now_ms);
  void finalize_monster_death(std::uint64_t monster_id, std::uint64_t killer_actor_id,
                               RuntimeDispatch& dispatch, std::uint64_t current_tick);
  void finalize_monster_ghost(std::uint64_t monster_id, RuntimeDispatch& dispatch,
                              std::uint64_t current_tick, std::uint64_t now_ms);
  void notify_player_and_watchers(RuntimeDispatch& dispatch, const Player& player,
                                  const std::string& self_message,
                                  const std::string& watcher_message) const;
  void dispatch_player_status_tick_result(Player& player, const StatusTickResult& result,
                                          RuntimeDispatch& dispatch,
                                          bool include_health) const;
  void broadcast_legacy_char_status_changed(RuntimeDispatch& dispatch,
                                            const Player& player) const;
  void add_legacy_trace(RuntimeDispatch& dispatch,
                        std::string stage,
                        std::string action,
                        const ActorMail& mail,
                        std::uint64_t current_tick,
                        std::uint64_t now_ms,
                        bool success = true,
                        std::int32_t value = 0,
                        std::int32_t damage = 0,
                        std::string label = {}) const;
  void schedule_actor(std::uint64_t current_tick, const GameObject& object);
  void sync_player_visibility(Player& player, RuntimeDispatch& dispatch, bool force);
  void sync_all_player_visibility(RuntimeDispatch& dispatch);
  void sync_visibility_after_actor_move(const GameObject& actor, std::int32_t old_x,
                                        std::int32_t old_y, std::int32_t new_x,
                                        std::int32_t new_y, RuntimeDispatch& dispatch);
  void sync_visibility_after_item_change(std::int32_t item_x, std::int32_t item_y,
                                         RuntimeDispatch& dispatch,
                                         std::optional<std::uint64_t> refresh_item_id = std::nullopt);
  void sync_visibility_after_event_change(std::int32_t event_x, std::int32_t event_y,
                                          RuntimeDispatch& dispatch);
  void force_refresh_after_same_map_transfer(Player& player, std::int32_t old_x,
                                             std::int32_t old_y, RuntimeDispatch& dispatch,
                                             std::uint64_t now_ms);
  void remove_actor_from_visibility(std::uint64_t actor_id, RuntimeDispatch& dispatch);
  void remove_item_from_visibility(std::uint64_t item_id, RuntimeDispatch& dispatch);
  [[nodiscard]] std::vector<std::uint64_t> ordered_player_ids() const;
  [[nodiscard]] std::vector<std::uint64_t> ordered_visible_actor_ids(
      const Player& player) const;
  [[nodiscard]] std::vector<std::uint64_t> ordered_visible_item_ids(
      const Player& player) const;
  void refresh_ground_item_ownership(GroundItem& item, std::uint64_t now_ms);
  void remove_expired_ground_items(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  bool try_gate_transfer(Player& player, RuntimeDispatch& dispatch,
                         std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] bool target_entry_allowed(Player& player, const LegacyMapGateState& gate,
                                          RuntimeDispatch& dispatch,
                                          std::uint64_t current_tick,
                                          std::uint64_t now_ms);
  [[nodiscard]] bool target_map_can_enter(const MapEntryRuleConfig& rule,
                                          const LegacyMapGateState& gate) const;
  [[nodiscard]] bool has_event_at(std::int32_t x, std::int32_t y,
                                  LegacyEventType type) const;
  bool try_item_map_move(Player& player, std::string target_map_id, std::int32_t target_x,
                         std::int32_t target_y, RuntimeDispatch& dispatch,
                         std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] std::optional<std::pair<std::int32_t, std::int32_t>>
  random_item_scroll_target(RuntimeDispatch& dispatch, const Player& player,
                            std::uint64_t current_tick, std::uint64_t now_ms);
  void broadcast_open_doors(const std::vector<std::pair<std::int32_t, std::int32_t>>& tiles,
                            RuntimeDispatch& dispatch);
  void broadcast_close_doors(const std::vector<std::pair<std::int32_t, std::int32_t>>& tiles,
                             RuntimeDispatch& dispatch);
  [[nodiscard]] std::uint64_t budget_for(GameObjectKind kind) const;
  [[nodiscard]] Player* find_player(std::uint64_t actor_id);
  [[nodiscard]] const Player* find_player(std::uint64_t actor_id) const;
  [[nodiscard]] Player* find_player_by_name(std::string_view character_name);
  [[nodiscard]] TradeSession* trade_session_for(std::uint64_t actor_id);
  [[nodiscard]] TradeOffer* trade_offer_for(TradeSession& session, std::uint64_t actor_id);
  [[nodiscard]] TradeOffer* trade_peer_offer_for(TradeSession& session,
                                                 std::uint64_t actor_id);
  void cancel_trade_for(std::uint64_t actor_id, RuntimeDispatch& dispatch, bool notify);
  bool can_receive_trade_items(const Player& receiver,
                               const std::vector<LegacyUserItem>& items) const;
  bool commit_trade(TradeSession& session, RuntimeDispatch& dispatch);
  [[nodiscard]] std::int32_t movement_width() const;
  [[nodiscard]] std::int32_t movement_height() const;
  [[nodiscard]] bool can_walk_tile(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] LegacyMovingObjectState moving_state_for(const GameObject& object) const;
  [[nodiscard]] std::vector<const GroundItem*> ordered_ground_items() const;
  [[nodiscard]] std::int32_t allocate_make_index();
  bool apply_equipped_item_durability_loss(Player& player, std::size_t slot,
                                           std::int32_t loss,
                                           RuntimeDispatch& dispatch);
  [[nodiscard]] std::int32_t roll_legacy_weapon_durability_loss(
      const Player& attacker, const GameObject& target, RuntimeDispatch& dispatch,
      std::uint64_t current_tick, std::uint64_t now_ms);
  bool apply_legacy_weapon_durability_loss(Player& attacker, std::int32_t loss,
                                           RuntimeDispatch& dispatch);
  bool apply_legacy_struck_equipment_durability(Player& target, std::uint64_t hitter_id,
                                                RuntimeDispatch& dispatch,
                                                std::uint64_t current_tick,
                                                std::uint64_t now_ms,
                                                std::string stage);
  [[nodiscard]] std::int32_t roll_legacy_player_attack_power(
      const Player& attacker, const GameObject& target, std::uint16_t ident,
      RuntimeDispatch& dispatch, std::string stage, std::string command,
      std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] bool handle_legacy_rush_rush(Player& attacker, LegacyUseMagicInfo& user_magic,
                                             const MagicConfig& magic, const ActorMail& mail,
                                             RuntimeDispatch& dispatch,
                                             std::uint64_t current_tick,
                                             std::uint64_t now_ms);
  bool apply_legacy_physical_equipment_specials(Player& attacker, GameObject& target,
                                                std::int32_t hit_damage,
                                                std::int32_t suck_damage,
                                                RuntimeDispatch& dispatch,
                                                std::string stage,
                                                std::uint64_t current_tick,
                                                std::uint64_t now_ms);
  bool handle_weapon_upgrade_start(Player& player, Npc& npc, RuntimeDispatch& dispatch,
                                   std::uint64_t current_tick, std::uint64_t now_ms);
  bool handle_weapon_upgrade_get_back(Player& player, Npc& npc, RuntimeDispatch& dispatch,
                                      std::uint64_t current_tick, std::uint64_t now_ms);
  bool apply_pending_weapon_upgrade_result(Player& attacker, RuntimeDispatch& dispatch,
                                           std::uint64_t current_tick, std::uint64_t now_ms);
  bool apply_legacy_weapon_good_luck(Player& player, RuntimeDispatch& dispatch,
                                     std::uint64_t current_tick, std::uint64_t now_ms);
  bool apply_legacy_weapon_unlock(Player& player, RuntimeDispatch& dispatch,
                                  std::uint64_t current_tick, std::uint64_t now_ms,
                                  std::string stage);
  void apply_bad_kill_penalty(Player& killer, const Player& victim, RuntimeDispatch& dispatch,
                              std::uint64_t current_tick, std::uint64_t now_ms,
                              std::string stage);
  bool settle_player_death(Player& player, RuntimeDispatch& dispatch,
                           std::uint64_t current_tick, std::uint64_t now_ms);
  bool try_legacy_revival(Player& player, RuntimeDispatch& dispatch,
                          std::uint64_t current_tick, std::uint64_t now_ms);
  [[nodiscard]] std::int32_t legacy_random_value(RuntimeDispatch& dispatch,
                                                 std::string stage,
                                                 std::string action,
                                                 std::int32_t range,
                                                 std::uint64_t actor_id,
                                                 std::uint64_t target_actor_id,
                                                 std::string command,
                                                 std::uint64_t now_ms,
                                                 std::uint64_t current_tick);
  bool legacy_execute_npc_script(Player& player, const Npc& npc, std::string action,
                                 RuntimeDispatch& dispatch, std::uint64_t current_tick,
                                 std::uint64_t now_ms);
  bool legacy_execute_npc_script(Player& player, const Npc& npc, std::string action,
                                 RuntimeDispatch& dispatch, std::uint64_t current_tick,
                                 std::uint64_t now_ms, std::int32_t depth);
  bool trigger_map_quest(Player& player, std::string monster_name, std::string item_name,
                         bool group_call, std::string source, RuntimeDispatch& dispatch,
                         std::uint64_t current_tick, std::uint64_t now_ms);

  MapConfig config_{};
  LogicBudgetConfig budgets_{};
  std::unordered_map<std::int32_t, ItemConfig> item_configs_{};
  std::unordered_map<std::int32_t, MagicConfig> magic_configs_{};
  std::unordered_map<std::string, MonsterDefConfig> monster_defs_{};
  std::vector<MapQuestConfig> map_quests_{};
  std::unordered_map<std::string, MapEntryRuleConfig> map_entry_rules_{};
  std::string black_stone_name_{"BlackStone"};
  bool legacy_approval_mode_{false};
  std::shared_ptr<const legacy::MapDocument> movement_map_{};
  LegacyMapEnvironment environment_{};
  CastleDialogContext castle_dialog_context_{};
  GuildCastleSnapshot guild_castle_snapshot_{};
  std::deque<ActorMail> mailbox_{};
  WheelTimer<std::uint64_t> object_wheel_{1024};
  WheelTimer<ActorMail> delayed_mail_wheel_{1024};
  LegacyRandom* legacy_random_{nullptr};
  MakeIndexAllocator fallback_make_index_allocator_{};
  MakeIndexAllocator* make_index_allocator_{nullptr};
  std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>> objects_{};
  std::unordered_map<std::uint64_t, MonsterSpawnTemplate> monster_spawn_templates_{};
  std::unordered_map<std::uint64_t, GroundItem> ground_items_{};
  std::unordered_map<std::uint64_t, TradeSession> trade_sessions_{};
  std::unordered_map<std::uint64_t, std::uint64_t> trade_session_by_actor_{};
  std::unordered_map<std::uint64_t, std::pair<std::int32_t, std::int32_t>> event_objects_{};
  std::unordered_map<std::uint64_t, LegacyEventType> event_object_types_{};
  std::unordered_map<std::uint64_t, PlayerVisibility> visibility_{};
  std::unordered_map<std::string, std::unordered_set<std::string>> script_name_lists_{};
  std::shared_ptr<std::array<std::int32_t, 10>> script_global_params_{};
  std::uint64_t next_ground_item_id_{1};
  std::uint64_t next_trade_session_id_{1};
  std::uint64_t next_script_monster_id_{0x6000000000000000ULL};
};

}  // namespace mir2
