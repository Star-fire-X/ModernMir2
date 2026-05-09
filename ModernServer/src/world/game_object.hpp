#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config/models.hpp"
#include "core/messages.hpp"

namespace mir2 {

enum class GameObjectKind {
  player,
  monster,
  npc,
  event_object
};

struct LegacyRuntimeTrace {
  std::string stage{};
  std::string action{};
  std::string map_id{};
  std::string object_name{};
  std::uint64_t actor_id{0};
  std::uint64_t now_ms{0};
  std::uint64_t current_tick{0};
  std::size_t cursor{0};
  std::size_t sub_cursor{0};
  std::uint64_t elapsed_ms{0};
  std::uint64_t target_actor_id{0};
  std::string command{};
  std::string label{};
  std::uint32_t rng_before{0};
  std::uint32_t rng_after{0};
  std::int32_t value{0};
  std::int32_t damage{0};
  bool success{false};
};

struct RuntimeDispatch {
  std::vector<SessionEvent> session_events{};
  std::vector<AuditEvent> audit_events{};
  std::vector<PersistRequest> persist_requests{};
  std::vector<ActorMail> cross_map_mails{};
  std::vector<LegacyRuntimeTrace> legacy_traces{};
};

struct ExperienceResult {
  std::int32_t gained{0};
  std::int32_t display_exp{0};
  bool leveled_up{false};
};

struct DamageResult {
  std::int32_t hp_damage{0};
  std::int32_t mp_damage{0};
  std::int32_t absorbed_damage{0};
  bool shield_broken{false};
  std::string shield_name{};
};

struct LegacyEquipmentSpecials {
  std::int32_t luck{0};
  std::int32_t unluck{0};
  std::int32_t anti_poison{0};
  std::int32_t undead_power{0};
  std::int32_t mana_to_health{0};
  std::int32_t suck_health_rate{0};
  bool make_stone{false};
  bool revival{false};
  bool magic_shield{false};
  bool equipment_transparent{false};
};

struct TimedStatusEffect {
  std::uint64_t source_actor_id{0};
  std::uint64_t expire_tick{0};
  std::uint64_t next_tick{0};
  std::uint64_t tick_interval{0};
  std::string effect_name{};
  std::int32_t damage_per_tick{0};
  std::int32_t slow_percent{0};
  std::int32_t heal_per_tick{0};
  std::int32_t shield_points{0};
};

struct StatusTickResult {
  std::int32_t damage{0};
  std::int32_t heal{0};
  std::int32_t absorbed_damage{0};
  std::uint64_t source_actor_id{0};
  bool shield_broken{false};
  bool shield_expired{false};
  bool ability_changed{false};
  bool legacy_status_changed{false};
  std::string shield_name{};
};

struct LegacyHealthSpellTickResult {
  std::int32_t hp{0};
  std::int32_t mp{0};
  bool changed{false};
};

enum class LegacyBuffKind : std::int32_t {
  poison_dechealth = 0,
  poison_damage_armor = 1,
  poison_dont_move = 4,
  poison_stone = 5,
  transparent = 8,
  defence_up = 9,
  magic_defence_up = 10,
  bubble_defence_up = 11,
  dc_up = 12
};

enum class LegacyBuffClearPolicy {
  death,
  leave_map,
  logout
};

struct LegacyBuffState {
  LegacyBuffKind kind{LegacyBuffKind::poison_dechealth};
  std::uint64_t expire_tick{0};
  std::uint64_t next_tick{0};
  std::uint64_t tick_interval{0};
  std::int32_t level{0};
  std::uint64_t source_actor_id{0};
  std::int32_t status_bit{0};
  bool affects_ability{false};
  bool negative{false};
  bool clear_on_death{true};
};

struct LegacyBuffClearResult {
  bool status_changed{false};
  bool ability_changed{false};
};

class LegacyBuffContainer {
 public:
  [[nodiscard]] bool activate_or_refresh(LegacyBuffState state, std::uint64_t current_tick);
  [[nodiscard]] bool active(LegacyBuffKind kind, std::uint64_t current_tick) const;
  [[nodiscard]] bool has(LegacyBuffKind kind) const;
  [[nodiscard]] bool clear(LegacyBuffKind kind);
  [[nodiscard]] LegacyBuffClearResult clear_by_policy(LegacyBuffClearPolicy policy);
  [[nodiscard]] LegacyBuffState* tick_due(LegacyBuffKind kind, std::uint64_t current_tick);
  [[nodiscard]] std::vector<LegacyBuffState> expire_due(std::uint64_t current_tick);
  [[nodiscard]] std::uint64_t remaining_ticks(LegacyBuffKind kind,
                                              std::uint64_t current_tick) const;
  [[nodiscard]] const LegacyBuffState* get(LegacyBuffKind kind) const;
  [[nodiscard]] LegacyBuffState* get(LegacyBuffKind kind);

 private:
  std::vector<LegacyBuffState> states_{};
};

struct LegacyMoveThrottleResult {
  bool allowed{true};
  bool disconnect{false};
};

struct LegacySpellThrottleResult {
  bool allowed{true};
  bool disconnect{false};
  std::int32_t over_count{0};
};

enum class LegacyPlayerState {
  loading,
  ready,
  notice_pending,
  initialize_pending,
  running,
  ghost,
  closed
};

enum class LegacyRepairMode {
  normal,
  special
};

struct LegacyQueuedCommand {
  ActorMail mail{};
  std::uint64_t received_ms{0};
  std::uint64_t sequence{0};
};

class MapContext {
 public:
  std::uint64_t tick{0};
  std::string map_id{};
  RuntimeDispatch* dispatch{nullptr};
  const std::unordered_map<std::int32_t, ItemConfig>* items{nullptr};
  const std::unordered_map<std::int32_t, MagicConfig>* magics{nullptr};

  void send_packet(std::uint64_t session_id, LegacyPacket packet) const;
  void emit_audit(std::string category, std::string message) const;
  void request_persist(PersistRequest request) const;
  void post_cross_map_mail(ActorMail mail) const;
};

class GameObject {
 public:
  GameObject(std::uint64_t id, GameObjectKind kind, std::string name, std::string map_id,
             std::int32_t x, std::int32_t y);
  virtual ~GameObject() = default;

  [[nodiscard]] std::uint64_t id() const { return id_; }
  [[nodiscard]] GameObjectKind kind() const { return kind_; }
  [[nodiscard]] const std::string& name() const { return name_; }
  [[nodiscard]] const std::string& map_id() const { return map_id_; }
  [[nodiscard]] std::int32_t x() const { return x_; }
  [[nodiscard]] std::int32_t y() const { return y_; }
  [[nodiscard]] std::uint64_t next_due_tick() const { return next_due_tick_; }

  virtual void on_mail(const ActorMail& mail, MapContext& context);
  virtual void on_tick(MapContext& context);

 protected:
  void set_position(std::int32_t x, std::int32_t y);
  void set_next_due_tick(std::uint64_t next_due_tick);

 private:
  std::uint64_t id_{0};
  GameObjectKind kind_{GameObjectKind::event_object};
  std::string name_{};
  std::string map_id_{};
  std::int32_t x_{0};
  std::int32_t y_{0};
  std::uint64_t next_due_tick_{1};
};

class Player : public GameObject {
 public:
  Player(std::uint64_t id, std::uint64_t session_id, CharacterRecord character);

  [[nodiscard]] std::uint64_t session_id() const { return session_id_; }
  [[nodiscard]] const CharacterRecord& character() const { return character_; }
  [[nodiscard]] CharacterRecord snapshot() const;
  [[nodiscard]] CharacterRecord persistent_snapshot() const;
  [[nodiscard]] bool is_dead() const;
  [[nodiscard]] bool in_safe_zone() const { return in_safe_zone_; }
  [[nodiscard]] bool has_free_bag_slot() const;
  [[nodiscard]] bool has_free_storage_slot() const;
  [[nodiscard]] bool can_add_bag_item(
      const LegacyUserItem& item,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const;
  [[nodiscard]] LegacyUserItem* bag_item_mutable(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  [[nodiscard]] const LegacyUserItem* bag_item(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) const;
  [[nodiscard]] const LegacyUserItem* equipped_item(std::size_t slot) const;
  [[nodiscard]] LegacyUserItem* equipped_item_mutable(std::size_t slot);
  [[nodiscard]] const LegacyUseMagicInfo* learned_magic(std::int32_t magic_id) const;
  [[nodiscard]] LegacyUseMagicInfo* learned_magic_mutable(std::int32_t magic_id);
  [[nodiscard]] bool add_legacy_magic(std::int32_t magic_id, char key, std::uint8_t level,
                                      std::int32_t cur_train);
  [[nodiscard]] bool remove_legacy_magic(std::int32_t magic_id);
  [[nodiscard]] bool can_spend_gold(std::int32_t amount) const;
  [[nodiscard]] std::int32_t accuracy_point() const { return accuracy_point_; }
  [[nodiscard]] std::int32_t speed_point() const { return speed_point_; }
  [[nodiscard]] const LegacyEquipmentSpecials& legacy_equipment_specials() const {
    return legacy_equipment_specials_;
  }
  [[nodiscard]] std::int32_t legacy_luck() const {
    return legacy_equipment_specials_.luck - legacy_equipment_specials_.unluck;
  }
  [[nodiscard]] std::int32_t legacy_anti_poison() const {
    return legacy_equipment_specials_.anti_poison;
  }
  [[nodiscard]] std::int32_t legacy_undead_power() const {
    return legacy_equipment_specials_.undead_power;
  }
  [[nodiscard]] bool legacy_make_stone() const {
    return legacy_equipment_specials_.make_stone;
  }
  [[nodiscard]] bool legacy_revival_active() const {
    return legacy_equipment_specials_.revival;
  }
  [[nodiscard]] bool legacy_magic_shield_active() const {
    return legacy_equipment_specials_.magic_shield;
  }
  [[nodiscard]] bool legacy_equipment_transparent_active() const {
    return legacy_equipment_specials_.equipment_transparent;
  }
  [[nodiscard]] bool legacy_revival_available(std::uint64_t now_ms) const;
  void mark_legacy_revival(std::uint64_t now_ms);
  [[nodiscard]] std::int32_t apply_legacy_suck_health(std::int32_t damage);
  [[nodiscard]] std::uint8_t attack_mode() const { return character_.attack_mode; }
  [[nodiscard]] std::int32_t pk_point() const { return character_.pk_point; }
  [[nodiscard]] std::int32_t pk_level() const;
  [[nodiscard]] std::int32_t body_luck_level() const;
  [[nodiscard]] std::uint64_t death_time_ms() const { return character_.death_time_ms; }
  [[nodiscard]] std::uint8_t quest_mark(std::int32_t index) const;
  [[nodiscard]] std::uint8_t quest_open_unit(std::int32_t index) const;
  [[nodiscard]] std::uint8_t quest_unit(std::int32_t index) const;
  [[nodiscard]] std::int32_t script_param(std::int32_t index) const;
  [[nodiscard]] std::uint32_t daily_quest() const { return character_.daily_quest; }
  [[nodiscard]] const std::vector<std::uint64_t>& slave_actor_ids() const {
    return slave_actor_ids_;
  }
  void add_slave_actor_id(std::uint64_t actor_id);
  void remove_slave_actor_id(std::uint64_t actor_id);
  void prune_slave_actor_ids(const std::unordered_set<std::uint64_t>& live_slave_ids);
  [[nodiscard]] std::optional<LegacyUserItem> remove_bag_item(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  [[nodiscard]] std::optional<LegacyUserItem> remove_bag_item_at(std::size_t slot);
  [[nodiscard]] std::optional<LegacyUserItem> remove_storage_item(
      std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  [[nodiscard]] std::optional<LegacyUserItem> remove_equipped_item(
      std::size_t slot, std::int32_t make_index, std::string_view expected_name,
      const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  [[nodiscard]] bool add_bag_item(const LegacyUserItem& item);
  [[nodiscard]] bool add_storage_item(const LegacyUserItem& item);
  [[nodiscard]] std::int32_t melee_power() const;
  [[nodiscard]] std::int32_t legacy_dc_up_bonus() const;
  [[nodiscard]] std::int32_t spell_power(std::int32_t base_power) const;
  [[nodiscard]] std::int32_t physical_defense() const;
  [[nodiscard]] std::int32_t magic_defense() const;
  [[nodiscard]] std::int32_t current_shield_points(std::uint64_t current_tick) const;
  [[nodiscard]] bool has_active_shield(std::uint64_t current_tick) const;
  [[nodiscard]] std::int32_t current_slow_percent(std::uint64_t current_tick) const;
  [[nodiscard]] bool can_move_at(std::uint64_t current_tick) const;
  [[nodiscard]] LegacyMoveThrottleResult begin_move_attempt(std::uint64_t current_tick,
                                                            std::uint32_t tick_ms);
  [[nodiscard]] LegacySpellThrottleResult begin_spell_attempt(std::uint64_t now_ms,
                                                              std::int32_t delay_time_ms,
                                                              bool sword_skill);
  void reset_move_throttle();
  [[nodiscard]] DamageResult apply_damage(std::int32_t amount, std::uint64_t current_tick);
  [[nodiscard]] std::int32_t apply_heal(std::int32_t amount);
  [[nodiscard]] std::int32_t apply_spell(std::int32_t amount);
  void queue_legacy_health_spell(std::int32_t hp, std::int32_t mp, std::int32_t healing,
                                 std::uint64_t current_tick,
                                 std::uint64_t tick_interval);
  void queue_legacy_healing(std::int32_t amount, std::uint64_t current_tick,
                            std::uint64_t tick_interval);
  [[nodiscard]] bool legacy_healing_pending() const;
  [[nodiscard]] LegacyHealthSpellTickResult tick_legacy_health_spell(std::uint64_t current_tick);
  [[nodiscard]] bool spend_mp(std::int32_t amount);
  [[nodiscard]] ExperienceResult gain_experience(std::int32_t amount);
  void add_status_effect(TimedStatusEffect effect);
  [[nodiscard]] bool apply_legacy_poison(std::int32_t poison_kind,
                                         std::uint64_t duration_ticks,
                                         std::int32_t poison_level,
                                         std::uint64_t poison_tick_interval,
                                         std::uint64_t source_actor_id,
                                         std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_defence_up(std::uint64_t duration_ticks,
                                                std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_magic_defence_up(std::uint64_t duration_ticks,
                                                      std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_dc_up(std::uint64_t duration_ticks,
                                           std::uint64_t current_tick,
                                           std::int32_t bonus);
  [[nodiscard]] bool legacy_transparent_active(std::uint64_t current_tick) const;
  [[nodiscard]] bool activate_legacy_transparent(std::uint64_t duration_ticks,
                                                 std::uint64_t current_tick);
  [[nodiscard]] bool clear_legacy_transparent(std::uint64_t current_tick);
  [[nodiscard]] bool legacy_poison_damage_armor_active(std::uint64_t current_tick) const;
  [[nodiscard]] std::size_t clear_negative_status_effects(std::uint64_t current_tick);
  [[nodiscard]] std::size_t clear_negative_legacy_buffs(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_death(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_leave_map(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_logout(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult tick_status_effects(std::uint64_t current_tick);
  void consume_move_action(std::uint64_t current_tick, bool running, std::uint32_t tick_ms);
  void restore_full_vitals();
  void equip_item(std::size_t slot, const LegacyUserItem& item);
  void apply_consumable(const ItemConfig& item_config);
  void add_gold(std::int32_t amount);
  void spend_gold(std::int32_t amount);
  void set_guild_membership(std::string guild_name, std::string guild_title);
  void clear_guild_membership();
  bool set_quest_mark(std::int32_t index, std::uint8_t value);
  bool set_quest_open_unit(std::int32_t index, std::uint8_t value);
  bool set_quest_unit(std::int32_t index, std::uint8_t value);
  bool set_script_param(std::int32_t index, std::int32_t value);
  void set_daily_quest(std::uint32_t value);
  void refresh_derived_state(const std::unordered_map<std::int32_t, ItemConfig>& item_configs);
  void mark_dead(std::uint64_t now_ms);
  [[nodiscard]] bool legacy_death_drop_settled() const { return legacy_death_drop_settled_; }
  void mark_legacy_death_drop_settled() { legacy_death_drop_settled_ = true; }
  void revive_at(std::string map_id, std::int32_t x, std::int32_t y,
                 std::uint16_t hp, std::uint16_t mp);
  void inc_pk_point(std::int32_t amount);
  void add_body_luck(double amount);
  void record_pk_hiter(std::uint64_t actor_id, std::uint64_t now_ms);
  [[nodiscard]] bool has_recent_pk_hiter(std::uint64_t actor_id, std::uint64_t now_ms) const;
  void set_in_safe_zone(bool value) { in_safe_zone_ = value; }
  [[nodiscard]] LegacyPlayerState legacy_state() const { return legacy_state_; }
  [[nodiscard]] bool legacy_login_sign() const { return login_sign_; }
  [[nodiscard]] bool legacy_ready_run() const { return ready_run_; }
  [[nodiscard]] bool legacy_ghost() const { return ghost_; }
  [[nodiscard]] std::int64_t legacy_run_time_ms() const { return run_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_run_next_tick_ms() const { return run_next_tick_ms_; }
  [[nodiscard]] std::uint64_t legacy_ghost_time_ms() const { return ghost_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_last_save_time_ms() const { return last_save_time_ms_; }
  [[nodiscard]] std::size_t legacy_inbox_size() const { return legacy_inbox_.size(); }
  [[nodiscard]] std::vector<std::uint64_t> legacy_inbox_session_sequences() const;
  [[nodiscard]] bool legacy_has_commands() const { return !legacy_inbox_.empty(); }
  [[nodiscard]] bool legacy_see_health_gauge() const { return legacy_see_health_gauge_; }
  void set_legacy_see_health_gauge(bool value) { legacy_see_health_gauge_ = value; }
  [[nodiscard]] bool legacy_slave_relax() const { return slave_relax_; }
  void set_legacy_slave_relax(bool value) { slave_relax_ = value; }
  [[nodiscard]] LegacyRepairMode legacy_repair_mode() const { return legacy_repair_mode_; }
  void set_legacy_repair_mode(LegacyRepairMode mode) { legacy_repair_mode_ = mode; }
  [[nodiscard]] bool legacy_magic_bubble_active(std::uint64_t current_tick) const;
  [[nodiscard]] std::int32_t legacy_magic_bubble_level() const;
  [[nodiscard]] bool activate_legacy_magic_bubble(std::int32_t level,
                                                  std::uint64_t current_tick,
                                                  std::uint64_t expire_tick);
  void damage_legacy_magic_bubble(std::uint64_t current_tick, std::uint64_t ticks);
  void prepare_legacy_sword_skill(std::int32_t magic_id, std::uint64_t expire_tick);
  [[nodiscard]] std::int32_t pending_legacy_sword_skill(std::uint64_t current_tick) const;
  [[nodiscard]] std::int32_t consume_legacy_sword_skill(std::uint64_t current_tick);
  void clear_legacy_sword_skill();
  [[nodiscard]] bool legacy_open_health_active(std::uint64_t current_tick) const;
  [[nodiscard]] std::uint64_t legacy_open_health_expire_tick() const {
    return legacy_open_health_expire_tick_;
  }
  void activate_legacy_open_health(std::uint64_t expire_tick);
  [[nodiscard]] std::uint32_t legacy_magic_lvexp_generation(std::int32_t magic_id) const;
  std::uint32_t advance_legacy_magic_lvexp_generation(std::int32_t magic_id);
  [[nodiscard]] bool legacy_due(std::uint64_t now_ms) const;
  void set_legacy_state(LegacyPlayerState state);
  void mark_legacy_notice_done(std::uint64_t now_ms);
  void mark_legacy_initialize_done(std::uint64_t now_ms);
  void mark_legacy_running_time(std::uint64_t now_ms);
  void mark_legacy_autosaved(std::uint64_t now_ms);
  void mark_legacy_ghost(std::uint64_t now_ms);
  void mark_legacy_closed();
  void rewind_legacy_run_time(std::uint64_t delta_ms);
  void enqueue_legacy_command(ActorMail mail, std::uint64_t now_ms);
  [[nodiscard]] std::optional<LegacyQueuedCommand> pop_legacy_command();

  void on_mail(const ActorMail& mail, MapContext& context) override;
  void on_tick(MapContext& context) override;

 private:
  struct PkHiterInfo {
    std::uint64_t actor_id{0};
    std::uint64_t hit_time_ms{0};
  };

  std::uint64_t session_id_{0};
  CharacterRecord character_{};
  LegacyAbility base_ability_{};
  bool in_safe_zone_{false};
  std::int32_t accuracy_point_{10};
  std::int32_t speed_point_{10};
  LegacyEquipmentSpecials legacy_equipment_specials_{};
  double legacy_suck_health_accumulator_{0.0};
  std::uint64_t latest_legacy_revival_time_ms_{0};
  std::vector<PkHiterInfo> pk_hiters_{};
  std::vector<TimedStatusEffect> status_effects_{};
  std::uint64_t next_move_tick_{0};
  std::uint64_t latest_walk_tick_{0};
  std::int32_t walk_time_over_count_{0};
  std::int32_t walk_time_over_sum_{0};
  std::int32_t speed_hack_timer_over_count_{0};
  std::uint64_t latest_spell_time_ms_{0};
  std::int32_t latest_spell_delay_ms_{0};
  std::int32_t spell_time_over_count_{0};
  std::int32_t spell_speed_hack_timer_over_count_{0};
  LegacyPlayerState legacy_state_{LegacyPlayerState::notice_pending};
  bool login_sign_{false};
  bool ready_run_{false};
  bool ghost_{false};
  bool legacy_death_drop_settled_{false};
  bool legacy_see_health_gauge_{false};
  bool slave_relax_{false};
  LegacyRepairMode legacy_repair_mode_{LegacyRepairMode::normal};
  LegacyBuffContainer legacy_buffs_{};
  std::int32_t legacy_prepared_sword_magic_id_{0};
  std::uint64_t legacy_prepared_sword_expire_tick_{0};
  std::uint64_t legacy_open_health_expire_tick_{0};
  std::int32_t legacy_inc_health_{0};
  std::int32_t legacy_inc_spell_{0};
  std::int32_t legacy_inc_healing_{0};
  std::uint64_t legacy_next_health_spell_tick_{0};
  std::uint64_t legacy_health_spell_tick_interval_{1};
  std::int64_t run_time_ms_{0};
  std::uint64_t run_next_tick_ms_{250};
  std::uint64_t last_save_time_ms_{0};
  std::uint64_t ghost_time_ms_{0};
  std::uint64_t legacy_command_sequence_{0};
  std::deque<LegacyQueuedCommand> legacy_inbox_{};
  std::unordered_map<std::int32_t, std::uint32_t> legacy_magic_lvexp_generations_{};
  std::vector<std::uint64_t> slave_actor_ids_{};
};

struct MonsterSnapshot {
  std::uint64_t id{0};
  std::string name{};
  std::string map_id{};
  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t level{1};
  std::int32_t hp{0};
  std::int32_t max_hp{0};
  std::int32_t mp{0};
  std::int32_t max_mp{0};
  std::int32_t dc_min{0};
  std::int32_t dc_max{0};
  std::int32_t attack_power{0};
  std::int32_t defense{0};
  std::int32_t magic_defense{0};
  std::int32_t mc{0};
  std::int32_t sc{0};
  std::int32_t exp_reward{0};
  std::int32_t life_attrib{0};
  std::int32_t race_server{0};
  std::int32_t race_image{0};
  std::int32_t appearance{0};
  std::int32_t cool_eye{0};
  std::int32_t speed_point{0};
  std::int32_t accuracy_point{0};
  std::int32_t walk_speed_ms{0};
  std::int32_t walk_step{0};
  std::int32_t walk_wait_ms{0};
  std::int32_t attack_speed_ms{0};
  std::uint64_t target_actor_id{0};
  std::uint64_t target_focus_time_ms{0};
  std::int32_t target_x{-1};
  std::int32_t target_y{-1};
  std::uint64_t walk_time_ms{0};
  std::uint64_t hit_time_ms{0};
  std::uint64_t search_enemy_time_ms{0};
  std::uint64_t think_time_ms{0};
  std::uint64_t last_hitter_id{0};
  std::uint64_t last_hit_time_ms{0};
  std::uint64_t exp_hitter_id{0};
  std::uint64_t exp_hit_time_ms{0};
  std::uint64_t death_time_ms{0};
  std::uint64_t ghost_time_ms{0};
  bool walk_wait_mode{false};
  bool dup_mode{false};
  bool ghosted{false};
  bool death_settled{false};
  std::int32_t chain_shot{0};
  std::int32_t chain_shot_count{0};
  bool hide_mode{false};
  bool stick_mode{false};
  std::int32_t dig_up_range{0};
  std::int32_t dig_down_range{0};
  std::uint64_t appear_time_ms{0};
  std::size_t child_actor_count{0};
  std::int32_t summon_limit{0};
  std::uint64_t master_actor_id{0};
  bool is_slave{false};
  std::int32_t slave_exp{0};
  std::int32_t slave_make_level{0};
  std::int32_t slave_exp_level{0};
  std::uint64_t master_royalty_time_ms{0};
  std::uint64_t slave_life_time_ms{0};
  bool no_item{false};
  bool tameable{true};
};

class Monster : public GameObject {
 public:
  Monster(std::uint64_t id, std::string name, std::string map_id, std::int32_t x, std::int32_t y,
          std::int32_t level, std::int32_t max_hp, std::int32_t attack_power,
          std::int32_t dc_min, std::int32_t dc_max, std::int32_t defense,
          std::int32_t magic_defense, std::int32_t mc, std::int32_t sc,
          std::int32_t exp_reward,
          std::int32_t life_attrib = 0, std::int32_t max_mp = 0,
          std::int32_t race_server = 0, std::int32_t race_image = 0,
          std::int32_t appearance = 0, std::int32_t cool_eye = 0,
          std::int32_t speed = 0, std::int32_t accuracy = 0,
          std::int32_t walk_speed_ms = 20, std::int32_t walk_step = 1,
          std::int32_t walk_wait_ms = 0, std::int32_t attack_speed_ms = 100,
          MonsterAiProfile ai_profile = MonsterAiProfile::basic,
          std::uint64_t search_rate_ms = 0,
          std::int32_t home_x = 0, std::int32_t home_y = 0, std::int32_t home_area = 0,
          bool legacy_spawn_group = false,
          std::uint64_t master_actor_id = 0, bool is_slave = false,
          std::int32_t slave_exp = 0, std::int32_t slave_make_level = 0,
          std::int32_t slave_exp_level = 0,
          std::uint64_t master_royalty_time_ms = 0,
          std::uint64_t slave_life_time_ms = 0,
          bool no_item = false,
          bool tameable = true,
          std::vector<LegacyUserItem> drop_items = {}, std::int32_t drop_gold = 0);

  [[nodiscard]] bool is_dead() const;
  [[nodiscard]] MonsterSnapshot snapshot() const;
  [[nodiscard]] std::int32_t hp() const { return hp_; }
  [[nodiscard]] std::int32_t max_hp() const { return max_hp_; }
  [[nodiscard]] std::int32_t mp() const { return mp_; }
  [[nodiscard]] std::int32_t max_mp() const { return max_mp_; }
  [[nodiscard]] std::int32_t level() const { return level_; }
  [[nodiscard]] std::int32_t attack_power() const { return attack_power_; }
  [[nodiscard]] std::int32_t dc_min() const { return dc_min_; }
  [[nodiscard]] std::int32_t dc_max() const { return dc_max_ + legacy_dc_up_bonus(); }
  [[nodiscard]] std::int32_t legacy_dc_up_bonus() const;
  [[nodiscard]] std::int32_t physical_defense() const;
  [[nodiscard]] std::int32_t magical_defense() const;
  [[nodiscard]] std::int32_t mc() const { return mc_; }
  [[nodiscard]] std::int32_t sc() const { return sc_; }
  [[nodiscard]] std::int32_t exp_reward() const { return exp_reward_; }
  [[nodiscard]] std::int32_t life_attrib() const { return life_attrib_; }
  [[nodiscard]] bool legacy_undead() const { return life_attrib_ == 1; }
  [[nodiscard]] std::int32_t race_server() const { return race_server_; }
  [[nodiscard]] std::int32_t race_image() const { return race_image_; }
  [[nodiscard]] std::int32_t appearance() const { return appearance_; }
  [[nodiscard]] std::int32_t cool_eye() const { return cool_eye_; }
  [[nodiscard]] std::uint8_t dir() const { return dir_; }
  [[nodiscard]] std::int32_t speed_point() const { return speed_point_; }
  [[nodiscard]] std::int32_t accuracy_point() const { return accuracy_point_; }
  [[nodiscard]] MonsterAiProfile ai_profile() const { return ai_profile_; }
  [[nodiscard]] std::int32_t walk_speed_ms() const { return walk_speed_ms_; }
  [[nodiscard]] std::int32_t walk_step() const { return walk_step_; }
  [[nodiscard]] std::int32_t walk_wait_ms() const { return walk_wait_ms_; }
  [[nodiscard]] std::int32_t attack_speed_ms() const { return attack_speed_ms_; }
  [[nodiscard]] std::int32_t home_x() const { return home_x_; }
  [[nodiscard]] std::int32_t home_y() const { return home_y_; }
  [[nodiscard]] std::int32_t home_area() const { return home_area_; }
  [[nodiscard]] bool legacy_spawn_group() const { return legacy_spawn_group_; }
  [[nodiscard]] std::int32_t drop_gold() const { return drop_gold_; }
  [[nodiscard]] const std::vector<LegacyUserItem>& drop_items() const { return drop_items_; }
  [[nodiscard]] bool legacy_open_health_active(std::uint64_t current_tick) const {
    return legacy_open_health_expire_tick_ != 0 && current_tick <= legacy_open_health_expire_tick_;
  }
  [[nodiscard]] std::uint64_t legacy_open_health_expire_tick() const {
    return legacy_open_health_expire_tick_;
  }
  void activate_legacy_open_health(std::uint64_t expire_tick) {
    legacy_open_health_expire_tick_ = std::max(legacy_open_health_expire_tick_, expire_tick);
  }
  [[nodiscard]] std::uint64_t last_hitter_id() const { return last_hitter_id_; }
  [[nodiscard]] std::uint64_t last_hit_time_ms() const { return last_hit_time_ms_; }
  [[nodiscard]] std::uint64_t exp_hitter_id() const { return exp_hitter_id_; }
  [[nodiscard]] std::uint64_t exp_hit_time_ms() const { return exp_hit_time_ms_; }
  [[nodiscard]] std::uint64_t death_time_ms() const { return death_time_ms_; }
  [[nodiscard]] std::uint64_t ghost_time_ms() const { return ghost_time_ms_; }
  [[nodiscard]] bool legacy_ghosted() const { return ghosted_; }
  [[nodiscard]] bool death_settled() const { return death_settled_; }
  [[nodiscard]] std::int32_t chain_shot() const { return chain_shot_; }
  [[nodiscard]] std::int32_t chain_shot_count() const { return chain_shot_count_; }
  [[nodiscard]] bool hide_mode() const { return hide_mode_; }
  [[nodiscard]] bool stick_mode() const { return stick_mode_; }
  [[nodiscard]] std::int32_t dig_up_range() const { return dig_up_range_; }
  [[nodiscard]] std::int32_t dig_down_range() const { return dig_down_range_; }
  [[nodiscard]] std::uint64_t appear_time_ms() const { return appear_time_ms_; }
  [[nodiscard]] const std::vector<std::uint64_t>& child_actor_ids() const {
    return child_actor_ids_;
  }
  [[nodiscard]] std::int32_t summon_limit() const { return summon_limit_; }
  [[nodiscard]] const std::string& summon_monster_name() const { return summon_monster_name_; }
  [[nodiscard]] std::uint64_t summon_delay_ms() const { return summon_delay_ms_; }
  [[nodiscard]] std::uint64_t master_actor_id() const { return master_actor_id_; }
  [[nodiscard]] bool is_slave() const { return is_slave_; }
  [[nodiscard]] std::int32_t slave_exp() const { return slave_exp_; }
  [[nodiscard]] std::int32_t slave_make_level() const { return slave_make_level_; }
  [[nodiscard]] std::int32_t slave_exp_level() const { return slave_exp_level_; }
  [[nodiscard]] std::uint64_t master_royalty_time_ms() const {
    return master_royalty_time_ms_;
  }
  [[nodiscard]] std::uint64_t slave_life_time_ms() const { return slave_life_time_ms_; }
  [[nodiscard]] bool no_item() const { return no_item_; }
  [[nodiscard]] bool tameable() const { return tameable_; }
  [[nodiscard]] std::uint64_t aggro_target_id() const { return aggro_target_id_; }
  [[nodiscard]] std::uint64_t target_actor_id() const { return aggro_target_id_; }
  [[nodiscard]] std::uint64_t target_focus_time_ms() const { return target_focus_time_ms_; }
  [[nodiscard]] std::int32_t target_x() const { return target_x_; }
  [[nodiscard]] std::int32_t target_y() const { return target_y_; }
  [[nodiscard]] bool has_target_xy() const { return target_x_ >= 0 && target_y_ >= 0; }
  [[nodiscard]] std::uint64_t walk_time_ms() const { return walk_time_ms_; }
  [[nodiscard]] std::uint64_t hit_time_ms() const { return hit_time_ms_; }
  [[nodiscard]] std::uint64_t search_enemy_time_ms() const { return search_enemy_time_ms_; }
  [[nodiscard]] std::uint64_t think_time_ms() const { return think_time_ms_; }
  [[nodiscard]] std::uint64_t walk_wait_cur_time_ms() const { return walk_wait_cur_time_ms_; }
  [[nodiscard]] std::int32_t walk_cur_step() const { return walk_cur_step_; }
  [[nodiscard]] bool walk_wait_mode() const { return walk_wait_mode_; }
  [[nodiscard]] bool dup_mode() const { return dup_mode_; }
  [[nodiscard]] std::int64_t legacy_run_time_ms() const { return run_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_run_next_tick_ms() const { return run_next_tick_ms_; }
  [[nodiscard]] std::uint64_t legacy_search_time_ms() const { return search_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_search_rate_ms() const { return search_rate_ms_; }
  [[nodiscard]] std::uint64_t legacy_ghost_time_ms() const { return ghost_time_ms_; }
  [[nodiscard]] bool legacy_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_search_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_walk_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_attack_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_walk_due_by_walk_time(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_attack_due_by_hit_time(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_walk_wait_elapsed(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_holy_seize_active(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_crazy_active(std::uint64_t now_ms) const;
  void make_legacy_holy_seize(std::uint64_t duration_ms, std::uint64_t now_ms);
  void break_legacy_holy_seize();
  void make_legacy_crazy(std::uint64_t duration_ms, std::uint64_t now_ms);
  void break_legacy_crazy();
  [[nodiscard]] std::int32_t apply_damage(std::int32_t amount, std::uint64_t attacker_id);
  [[nodiscard]] std::int32_t apply_damage(std::int32_t amount, std::uint64_t attacker_id,
                                          std::uint64_t now_ms);
  void add_status_effect(TimedStatusEffect effect);
  [[nodiscard]] bool apply_legacy_poison(std::int32_t poison_kind,
                                         std::uint64_t duration_ticks,
                                         std::int32_t poison_level,
                                         std::uint64_t poison_tick_interval,
                                         std::uint64_t source_actor_id,
                                         std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_defence_up(std::uint64_t duration_ticks,
                                                std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_magic_defence_up(std::uint64_t duration_ticks,
                                                      std::uint64_t current_tick);
  [[nodiscard]] bool activate_legacy_dc_up(std::uint64_t duration_ticks,
                                           std::uint64_t current_tick,
                                           std::int32_t bonus);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_death(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_leave_map(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult clear_legacy_buffs_on_logout(std::uint64_t current_tick);
  [[nodiscard]] StatusTickResult tick_status_effects(std::uint64_t current_tick);
  [[nodiscard]] std::uint64_t next_status_tick() const;
  [[nodiscard]] std::int32_t current_slow_percent(std::uint64_t current_tick) const;
  [[nodiscard]] bool ai_due(std::uint64_t current_tick) const { return current_tick >= next_ai_tick_; }
  void mark_legacy_run_time(std::uint64_t now_ms);
  void mark_legacy_search_time(std::uint64_t now_ms);
  void mark_legacy_attack_time(std::uint64_t now_ms);
  void mark_legacy_walk_time(std::uint64_t now_ms);
  void mark_legacy_hit_time(std::uint64_t now_ms);
  void mark_search_enemy_time(std::uint64_t now_ms);
  void mark_think_time(std::uint64_t now_ms);
  void mark_legacy_ghost_time(std::uint64_t now_ms);
  void record_legacy_hitter(std::uint64_t attacker_id, std::uint64_t now_ms,
                            bool exp_hitter = true);
  void clear_last_hitter();
  void clear_exp_hitter();
  void clear_legacy_hitters();
  void expire_legacy_hitters(std::uint64_t now_ms);
  void mark_legacy_death(std::uint64_t now_ms);
  void mark_legacy_ghost(std::uint64_t now_ms);
  [[nodiscard]] bool death_due_for_ghost(std::uint64_t now_ms,
                                         std::uint64_t corpse_ms) const;
  void mark_death_settled();
  void set_chain_shot(std::int32_t value);
  void increment_chain_shot();
  void set_chain_shot_count(std::int32_t value);
  void set_hide_mode(bool value);
  void set_stick_mode(bool value);
  void set_dig_ranges(std::int32_t up_range, std::int32_t down_range);
  void set_appear_time_ms(std::uint64_t now_ms);
  void add_child_actor_id(std::uint64_t actor_id);
  void prune_child_actor_ids(const std::unordered_set<std::uint64_t>& live_child_ids);
  void set_summon(std::string monster_name, std::int32_t limit, std::uint64_t delay_ms);
  void set_master_actor_id(std::uint64_t actor_id);
  void configure_slave(std::uint64_t master_actor_id, std::int32_t slave_exp,
                       std::int32_t slave_make_level, std::int32_t slave_exp_level,
                       std::uint64_t master_royalty_time_ms,
                       std::uint64_t slave_life_time_ms, bool no_item);
  void set_master_royalty_time_ms(std::uint64_t value) { master_royalty_time_ms_ = value; }
  void set_slave_life_time_ms(std::uint64_t value) { slave_life_time_ms_ = value; }
  void set_no_item(bool value) { no_item_ = value; }
  void set_hp_mp(std::int32_t hp, std::int32_t mp);
  void reduce_hp_to_loyalty_break_floor();
  [[nodiscard]] bool gain_slave_exp(std::int32_t slain_level);
  void schedule_next_ai_tick(std::uint64_t current_tick);
  void set_dir(std::uint8_t dir) { dir_ = static_cast<std::uint8_t>(dir % 8); }
  void select_target(std::uint64_t actor_id, std::uint64_t now_ms);
  void lose_target();
  void set_target_xy(std::int32_t x, std::int32_t y);
  void clear_target_xy();
  void begin_walk_wait(std::uint64_t now_ms);
  void set_walk_wait_mode(bool value);
  void set_dup_mode(bool value);
  void reset_walk_cur_step();
  void increment_walk_cur_step();
  void initialize_legacy_ai_timers(std::uint64_t now_ms,
                                   std::uint64_t walk_offset_ms,
                                   std::uint64_t hit_offset_ms);
  void set_aggro_target_id(std::uint64_t actor_id) { aggro_target_id_ = actor_id; }
  void clear_aggro_target() { lose_target(); }
  [[nodiscard]] bool inside_home_area() const;

  void on_tick(MapContext& context) override;

 private:
  void apply_slave_level_abilities();

  std::int32_t level_{1};
  std::int32_t hp_{12};
  std::int32_t max_hp_{12};
  std::int32_t mp_{0};
  std::int32_t max_mp_{0};
  std::int32_t attack_power_{3};
  std::int32_t dc_min_{0};
  std::int32_t dc_max_{3};
  std::int32_t defense_{0};
  std::int32_t magic_defense_{0};
  std::int32_t mc_{0};
  std::int32_t sc_{0};
  std::int32_t exp_reward_{12};
  std::int32_t life_attrib_{0};
  std::int32_t race_server_{0};
  std::int32_t race_image_{0};
  std::int32_t appearance_{0};
  std::int32_t cool_eye_{0};
  std::uint8_t dir_{4};
  std::int32_t speed_point_{0};
  std::int32_t accuracy_point_{0};
  std::int32_t walk_speed_ms_{20};
  std::int32_t walk_step_{1};
  std::int32_t walk_wait_ms_{0};
  std::int32_t attack_speed_ms_{100};
  MonsterAiProfile ai_profile_{MonsterAiProfile::basic};
  std::int32_t home_x_{0};
  std::int32_t home_y_{0};
  std::int32_t home_area_{0};
  bool legacy_spawn_group_{false};
  std::vector<LegacyUserItem> drop_items_{};
  std::int32_t drop_gold_{0};
  std::uint64_t legacy_open_health_expire_tick_{0};
  std::uint64_t last_hitter_id_{0};
  std::uint64_t last_hit_time_ms_{0};
  std::uint64_t exp_hitter_id_{0};
  std::uint64_t exp_hit_time_ms_{0};
  std::uint64_t death_time_ms_{0};
  bool ghosted_{false};
  bool death_settled_{false};
  std::uint64_t aggro_target_id_{0};
  std::uint64_t next_ai_tick_{0};
  std::int64_t run_time_ms_{0};
  std::uint64_t run_next_tick_ms_{250};
  std::uint64_t attack_time_ms_{0};
  std::uint64_t search_time_ms_{0};
  std::uint64_t search_rate_ms_{0};
  std::uint64_t walk_time_ms_{0};
  std::uint64_t hit_time_ms_{0};
  std::uint64_t search_enemy_time_ms_{0};
  std::uint64_t think_time_ms_{0};
  std::uint64_t walk_wait_cur_time_ms_{0};
  std::uint64_t target_focus_time_ms_{0};
  std::int32_t target_x_{-1};
  std::int32_t target_y_{-1};
  std::int32_t walk_cur_step_{0};
  bool walk_wait_mode_{false};
  bool dup_mode_{false};
  std::uint64_t ghost_time_ms_{0};
  std::int32_t chain_shot_{0};
  std::int32_t chain_shot_count_{0};
  bool hide_mode_{false};
  bool stick_mode_{false};
  std::int32_t dig_up_range_{0};
  std::int32_t dig_down_range_{0};
  std::uint64_t appear_time_ms_{0};
  std::vector<std::uint64_t> child_actor_ids_{};
  std::int32_t summon_limit_{0};
  std::string summon_monster_name_{};
  std::uint64_t summon_delay_ms_{0};
  std::uint64_t master_actor_id_{0};
  bool is_slave_{false};
  std::int32_t slave_exp_{0};
  std::int32_t slave_make_level_{0};
  std::int32_t slave_exp_level_{0};
  std::uint64_t master_royalty_time_ms_{0};
  std::uint64_t slave_life_time_ms_{0};
  bool no_item_{false};
  bool tameable_{true};
  std::uint64_t legacy_holy_seize_until_ms_{0};
  std::uint64_t legacy_crazy_until_ms_{0};
  std::int32_t base_max_hp_{12};
  std::int32_t base_dc_max_{3};
  std::int32_t base_magic_defense_{0};
  std::vector<TimedStatusEffect> status_effects_{};
  LegacyBuffContainer legacy_buffs_{};
};

class Npc : public GameObject {
 public:
  Npc(std::uint64_t id, std::string name, std::string map_id, std::int32_t x, std::int32_t y,
      std::string service, std::vector<LegacyUserItem> merchant_items,
      std::vector<NpcDialogSectionConfig> dialog_sections,
      std::int32_t price_rate_percent = 100, std::string merchant_key = {},
      std::vector<MerchantProductRuntimeConfig> merchant_products = {},
      std::unordered_map<std::int32_t, std::int32_t> merchant_prices = {},
      std::vector<std::int32_t> deal_std_modes = {},
      std::vector<LegacyWeaponUpgradeRecord> weapon_upgrades = {});

  [[nodiscard]] bool supports_buy() const;
  [[nodiscard]] bool supports_sell() const;
  [[nodiscard]] bool supports_repair() const;
  [[nodiscard]] bool supports_storage() const;
  [[nodiscard]] bool supports_guild() const;
  [[nodiscard]] bool supports_castle() const;
  [[nodiscard]] bool supports_weapon_upgrade() const;
  [[nodiscard]] bool legacy_due(std::uint64_t now_ms) const;
  [[nodiscard]] bool legacy_search_due(std::uint64_t now_ms) const;
  [[nodiscard]] std::int64_t legacy_run_time_ms() const { return run_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_run_next_tick_ms() const { return run_next_tick_ms_; }
  [[nodiscard]] std::uint64_t legacy_search_time_ms() const { return search_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_search_rate_ms() const { return search_rate_ms_; }
  [[nodiscard]] std::uint64_t legacy_ghost_time_ms() const { return ghost_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_refill_time_ms() const { return refill_time_ms_; }
  [[nodiscard]] std::uint64_t legacy_verify_time_ms() const { return verify_time_ms_; }
  [[nodiscard]] const std::string& service() const { return service_; }
  [[nodiscard]] const std::string& merchant_key() const { return merchant_key_; }
  [[nodiscard]] std::int32_t price_rate_percent() const { return price_rate_percent_; }
  [[nodiscard]] const std::vector<LegacyUserItem>& merchant_items() const { return merchant_items_; }
  [[nodiscard]] std::vector<LegacyUserItem>& merchant_items_mutable() { return merchant_items_; }
  [[nodiscard]] const std::vector<LegacyWeaponUpgradeRecord>& weapon_upgrades() const {
    return weapon_upgrades_;
  }
  [[nodiscard]] std::vector<LegacyWeaponUpgradeRecord>& weapon_upgrades_mutable() {
    return weapon_upgrades_;
  }
  [[nodiscard]] const std::vector<MerchantProductRuntimeConfig>& merchant_products() const {
    return merchant_products_;
  }
  [[nodiscard]] std::vector<MerchantProductRuntimeConfig>& merchant_products_mutable() {
    return merchant_products_;
  }
  [[nodiscard]] const std::unordered_map<std::int32_t, std::int32_t>& merchant_prices() const {
    return merchant_prices_;
  }
  [[nodiscard]] std::optional<std::int32_t> merchant_price(std::int32_t item_id) const;
  void set_merchant_price(std::int32_t item_id, std::int32_t price);
  [[nodiscard]] bool deals_std_mode(std::int32_t std_mode) const;
  void apply_merchant_state(const MerchantStateRecord& state);
  [[nodiscard]] MerchantStateRecord snapshot_merchant_state() const;
  [[nodiscard]] const std::vector<NpcDialogSectionConfig>& dialog_sections() const {
    return dialog_sections_;
  }
  void mark_legacy_run_time(std::uint64_t now_ms);
  void mark_legacy_search_time(std::uint64_t now_ms);
  void mark_legacy_ghost_time(std::uint64_t now_ms);
  void mark_legacy_refill_time(std::uint64_t now_ms);
  void mark_legacy_verify_time(std::uint64_t now_ms);

  void on_tick(MapContext& context) override;

 private:
  std::string service_{};
  std::string merchant_key_{};
  std::vector<LegacyUserItem> merchant_items_{};
  std::vector<LegacyWeaponUpgradeRecord> weapon_upgrades_{};
  std::vector<MerchantProductRuntimeConfig> merchant_products_{};
  std::unordered_map<std::int32_t, std::int32_t> merchant_prices_{};
  std::vector<std::int32_t> deal_std_modes_{};
  std::vector<NpcDialogSectionConfig> dialog_sections_{};
  std::int32_t price_rate_percent_{100};
  bool buy_enabled_{false};
  bool weapon_upgrade_enabled_{false};
  std::int64_t run_time_ms_{0};
  std::uint64_t run_next_tick_ms_{1000};
  std::uint64_t search_time_ms_{0};
  std::uint64_t search_rate_ms_{1000};
  std::uint64_t ghost_time_ms_{0};
  std::uint64_t refill_time_ms_{0};
  std::uint64_t verify_time_ms_{0};
};

class EventObject : public GameObject {
 public:
  EventObject(std::uint64_t id, std::string name, std::string map_id, std::int32_t x,
              std::int32_t y);

  void on_tick(MapContext& context) override;
};

}  // namespace mir2
