#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config/models.hpp"
#include "core/messages.hpp"
#include "world/game_object.hpp"
#include "world/legacy_chat_parser.hpp"
#include "world/legacy_event_manager.hpp"
#include "world/legacy_gm_commands.hpp"
#include "world/legacy_random.hpp"
#include "world/make_index_allocator.hpp"
#include "world/map_actor.hpp"

namespace mir2 {

struct LegacyReadyUser {
  std::uint64_t session_id{0};
  std::string gateway{"game_gateway"};
  std::string account_id{};
  std::string character_name{};
  std::string map_id{};
  std::int32_t x{0};
  std::int32_t y{0};
  CharacterRecord character{};
  bool fast_initialize{false};
};

struct LegacyRuntimeContext {
  bool persistence_overloaded{false};
  std::size_t player_process_limit{0};
  std::size_t player_input_budget_per_tick{0};
};

struct LegacyShutUpEntry {
  std::string character_name{};
  std::uint64_t expire_ms{0};
};

class LogicRuntime {
 public:
  explicit LogicRuntime(HostConfig config);

  void initialize();
  void set_legacy_random_seed(std::uint32_t seed);
  [[nodiscard]] std::uint32_t legacy_random_state() const;
  void set_merchant_states(std::vector<MerchantStateRecord> merchant_states);
  void apply_merchant_states(std::vector<MerchantStateRecord> merchant_states);
  void set_castle_dialog_context(CastleDialogContext castle_dialog_context);
  void set_guild_castle_snapshot(GuildCastleSnapshot guild_castle_snapshot);
  [[nodiscard]] RuntimeDispatch route_logic_command(const LogicCommand& command);
  [[nodiscard]] RuntimeDispatch route_actor_mail(const ActorMail& mail);
  [[nodiscard]] RuntimeDispatch enqueue_ready_user(LegacyReadyUser ready_user);
  [[nodiscard]] RuntimeDispatch mark_session_disconnected(std::uint64_t session_id,
                                                          std::string reason);
  [[nodiscard]] RuntimeDispatch tick();
  [[nodiscard]] RuntimeDispatch tick(std::uint64_t now_ms);
  [[nodiscard]] RuntimeDispatch tick(std::uint64_t now_ms, LegacyRuntimeContext context);
  [[nodiscard]] RuntimeDispatch run_legacy_event_manager(std::uint64_t now_ms);
  [[nodiscard]] std::uint64_t enqueue_legacy_event(LegacyEventRecord record);
  [[nodiscard]] std::optional<LegacyEventRecord> find_legacy_event(
      const std::string& map_id, std::int32_t x, std::int32_t y,
      LegacyEventType type) const;
  [[nodiscard]] std::optional<std::pair<std::string, std::uint64_t>> locate_character_actor(
      std::string_view character_name) const;
  [[nodiscard]] std::optional<CharacterRecord> snapshot_character_actor(
      std::string_view character_name) const;
  [[nodiscard]] std::vector<CharacterRecord> snapshot_online_characters();
  [[nodiscard]] std::optional<MonsterSnapshot> legacy_monster_snapshot(
      std::string_view map_id, std::uint64_t actor_id) const;
  void add_legacy_shut_up(std::string_view character_name, std::uint64_t duration_ms,
                          std::uint64_t now_ms);
  bool release_legacy_shut_up(std::string_view character_name);
  [[nodiscard]] std::vector<LegacyShutUpEntry> legacy_shut_up_entries() const;
  [[nodiscard]] std::size_t map_count() const { return maps_.size(); }
  [[nodiscard]] std::size_t online_session_count() const { return session_index_.size(); }
  [[nodiscard]] std::uint64_t current_tick() const { return current_tick_; }
  [[nodiscard]] const std::vector<std::string>& map_order() const { return map_order_; }
  [[nodiscard]] std::size_t legacy_ready_count() const { return ready_users_.size(); }
  [[nodiscard]] std::size_t legacy_run_user_count() const { return run_user_order_.size(); }
  [[nodiscard]] std::size_t legacy_close_record_count() const { return close_records_.size(); }
  [[nodiscard]] std::size_t legacy_monster_group_count() const { return monster_groups_.size(); }
  [[nodiscard]] std::size_t legacy_merchant_count() const { return merchant_order_.size(); }
  [[nodiscard]] std::size_t legacy_npc_count() const { return npc_order_.size(); }
  [[nodiscard]] std::size_t legacy_active_event_count() const {
    return legacy_event_manager_.active_count();
  }
  [[nodiscard]] std::size_t legacy_closed_event_count() const {
    return legacy_event_manager_.closed_count();
  }
  [[nodiscard]] std::size_t legacy_mon_cur() const { return mon_cur_; }
  [[nodiscard]] std::size_t legacy_mon_sub_cur() const { return mon_sub_cur_; }
  [[nodiscard]] std::size_t legacy_gen_cur() const { return gen_cur_; }
  [[nodiscard]] std::size_t legacy_mer_cur() const { return mer_cur_; }
  [[nodiscard]] std::size_t legacy_npc_cur() const { return npc_cur_; }
  [[nodiscard]] std::optional<LegacyPlayerState> legacy_session_state(
      std::uint64_t session_id) const;
  [[nodiscard]] std::size_t legacy_session_inbox_size(std::uint64_t session_id) const;
  [[nodiscard]] std::vector<std::uint64_t> legacy_session_inbox_sequences(
      std::uint64_t session_id) const;
  [[nodiscard]] std::int64_t legacy_session_run_time_ms(std::uint64_t session_id) const;

 private:
  struct ActorLocator {
    std::string map_id{};
    std::uint64_t actor_id{0};
    std::string account_id{};
    std::string character_name{};
    std::string latest_say_text{};
    std::uint64_t bomb_say_time_ms{0};
    std::int32_t bomb_say_count{0};
    std::uint64_t auto_shut_up_until_ms{0};
    bool has_latest_cry_time{false};
    std::uint64_t latest_cry_time_ms{0};
    bool hear_whisper{true};
    bool hear_cry{true};
    bool hear_guild_msg{true};
    std::vector<std::string> whisper_block_list{};
    std::uint64_t legacy_group_id{0};
    LegacyUserDegree user_degree{LegacyUserDegree::normal};
    bool legacy_sysop_mode{false};
    bool legacy_supervisor_mode{false};
    bool legacy_superman_mode{false};
    bool legacy_sys_mission{false};
    std::string legacy_sys_mission_map{};
    std::int32_t legacy_sys_mission_x{0};
    std::int32_t legacy_sys_mission_y{0};
  };

  struct CloseRecord {
    std::uint64_t session_id{0};
    std::string account_id{};
    std::string character_name{};
    std::uint64_t closed_ms{0};
    std::string reason{};
  };

  struct ActorRef {
    std::string map_id{};
    std::uint64_t actor_id{0};
    std::string name{};
  };

  struct MonsterGroup {
    std::string name{};
    std::string map_id{};
    SpawnConfig spawn{};
    std::int32_t x{0};
    std::int32_t y{0};
    std::int32_t area{0};
    std::int32_t count{1};
    std::int32_t small_zen_rate{0};
    bool legacy_group{false};
    std::uint32_t respawn_ms{0};
    std::uint32_t zen_time_ms{0};
    std::uint64_t start_time_ms{0};
    std::vector<ActorRef> monsters{};
  };

  struct LegacyGroupState {
    std::vector<std::uint64_t> members{};
  };

  [[nodiscard]] std::string resolve_map_id(const std::string& requested_map) const;
  [[nodiscard]] bool has_live_or_closing_character(std::string_view character_name) const;
  [[nodiscard]] LegacyUserDegree resolve_legacy_user_degree(
      std::string_view account_id, std::string_view character_name) const;
  [[nodiscard]] std::optional<std::uint64_t> find_actor_session_by_name(
      std::string_view character_name) const;
  void create_legacy_group(std::uint64_t owner_session_id, std::string_view target_name);
  void add_legacy_group_member(std::uint64_t owner_session_id, std::string_view target_name);
  void remove_legacy_group_member_by_name(std::uint64_t owner_session_id,
                                          std::string_view target_name);
  void remove_legacy_group_member(std::uint64_t session_id);
  [[nodiscard]] ActorMail make_player_mail(const LogicCommand& command,
                                           const ActorLocator& locator) const;
  [[nodiscard]] bool handle_legacy_chat_command(const LogicCommand& command,
                                                ActorLocator& locator,
                                                const LegacyChatParseResult& parsed,
                                                std::uint64_t now_ms,
                                                RuntimeDispatch& dispatch);
  [[nodiscard]] bool route_legacy_chat_command(const LogicCommand& command,
                                               ActorLocator& locator,
                                               std::uint64_t now_ms,
                                               RuntimeDispatch& dispatch);
  [[nodiscard]] bool is_merchant_npc_config(const NpcConfig& npc,
                                            const ActorMail& mail) const;
  void add_stage_trace(RuntimeDispatch& dispatch, std::string stage, std::string action,
                       std::uint64_t now_ms, std::size_t cursor = 0,
                       std::size_t sub_cursor = 0) const;
  void process_ready_users(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  void process_user_humans(std::uint64_t now_ms, const LegacyRuntimeContext& context,
                           RuntimeDispatch& dispatch);
  void process_monsters(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  void process_monster_spawn_group(std::size_t group_index, std::uint64_t now_ms,
                                    RuntimeDispatch& dispatch);
  [[nodiscard]] std::int32_t legacy_monster_live_count_for_spawn(const MonsterGroup& group) const;
  [[nodiscard]] std::uint64_t legacy_zen_time_ms(std::uint32_t zen_time_ms) const;
  [[nodiscard]] ActorMail make_monster_spawn_mail(const MonsterGroup& group,
                                                   std::uint64_t actor_id,
                                                   std::int32_t x,
                                                   std::int32_t y,
                                                   std::uint64_t now_ms,
                                                   RuntimeDispatch* dispatch);
  [[nodiscard]] std::int32_t spawn_legacy_gm_monsters(
      RuntimeDispatch& dispatch, std::string map_id, std::int32_t x, std::int32_t y,
      std::string monster_name, std::int32_t count, std::uint64_t now_ms,
      std::uint64_t master_actor_id = 0, std::uint8_t slave_exp_level = 0,
      std::optional<std::pair<std::int32_t, std::int32_t>> target_xy = std::nullopt);
  void roll_legacy_monster_items_for_spawn(const MonsterGroup& group, ActorMail& mail,
                                           std::uint64_t now_ms, RuntimeDispatch* dispatch);
  void prune_monster_group(MonsterGroup& group);
  void process_merchants(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  void process_npcs(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  void process_user_engine_timers(std::uint64_t now_ms, RuntimeDispatch& dispatch);
  void process_legacy_event_creates(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  void process_legacy_random_space_moves(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  void process_cross_map_mails(RuntimeDispatch& dispatch);
  void refresh_legacy_holy_curtain_groups(RuntimeDispatch& dispatch, std::uint64_t now_ms);
  void cleanup_close_records(std::uint64_t now_ms);
  void append_dispatch(RuntimeDispatch& target, RuntimeDispatch source);

  HostConfig config_{};
  std::unordered_map<std::int32_t, ItemConfig> item_configs_{};
  std::unordered_map<std::int32_t, MagicConfig> magic_configs_{};
  std::unordered_map<std::string, MonsterDefConfig> monster_defs_{};
  std::unordered_map<std::string, std::vector<MonsterDropConfig>> monster_drops_{};
  std::unordered_map<std::string, ItemConfig> item_configs_by_name_{};
  CastleDialogContext castle_dialog_context_{};
  GuildCastleSnapshot guild_castle_snapshot_{};
  std::unordered_map<std::string, MerchantStateRecord> merchant_states_{};
  std::unordered_map<std::string, LegacyShutUpEntry> legacy_shut_up_list_{};
  std::unordered_map<std::string, LegacyUserDegree> legacy_admin_degrees_{};
  std::uint64_t next_legacy_group_id_{1};
  std::unordered_map<std::uint64_t, LegacyGroupState> legacy_groups_{};
  std::unordered_map<std::string, std::unique_ptr<MapActor>> maps_{};
  std::vector<std::string> map_order_{};
  std::unordered_map<std::uint64_t, ActorLocator> session_index_{};
  std::deque<LegacyReadyUser> ready_users_{};
  std::vector<std::uint64_t> run_user_order_{};
  std::unordered_map<std::string, CloseRecord> close_records_{};
  std::vector<MonsterGroup> monster_groups_{};
  std::vector<ActorRef> merchant_order_{};
  std::vector<ActorRef> npc_order_{};
  std::size_t hum_cur_{0};
  std::size_t mon_cur_{0};
  std::size_t mon_sub_cur_{0};
  std::size_t gen_cur_{0};
  std::size_t mer_cur_{0};
  std::size_t npc_cur_{0};
  std::uint64_t last_ready_process_ms_{0};
  std::uint64_t one_zen_time_ms_{0};
  bool one_zen_time_initialized_{false};
  std::uint64_t mission_time_ms_{0};
  std::uint64_t open_door_check_ms_{0};
  std::uint64_t timer10min_ms_{0};
  std::uint64_t timer10sec_ms_{0};
  bool user_engine_timers_initialized_{false};
  std::uint64_t last_now_ms_{0};
  std::uint64_t current_tick_{0};
  LegacyRandom legacy_random_{1};
  LegacyEventManager legacy_event_manager_{};
  MakeIndexAllocator make_index_allocator_{};
  std::uint64_t next_actor_id_{1};
  std::string default_map_id_{};
};

}  // namespace mir2
