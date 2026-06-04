#pragma once

// Implementation detail for map_actor.cpp; included inside namespace mir2.
namespace {
constexpr std::uint8_t kDefaultNameColor = 255;
constexpr std::uint8_t kDefaultChatColor = 255;
constexpr std::uint8_t kDefaultChatShadow = 0;
constexpr std::int32_t kDefaultMerchantFace = 0;
constexpr std::uint8_t kCrossMapSyncRetryLimit = 100;
constexpr std::int32_t kLegacyViewRange = 12;
constexpr std::int32_t kAreaFight = 1;
constexpr std::int32_t kAreaSafe = 2;
constexpr std::int32_t kAreaFreePk = 4;
constexpr std::uint8_t kHamAll = 0;
constexpr std::uint8_t kHamPeace = 1;
constexpr std::uint8_t kHamGroup = 2;
constexpr std::uint8_t kHamGuild = 3;
constexpr std::uint8_t kHamPkAttack = 4;
constexpr std::uint64_t kMapChangeProtectMs = 3000;
constexpr std::uint64_t kPlayerCorpseMs = 180000;
constexpr std::uint64_t kMonsterCorpseMs = 180000;
constexpr std::uint64_t kLegacyDropOwnerMs = 120000;
constexpr std::uint64_t kLegacyGroundItemExpireMs = 10ULL * 60ULL * 1000ULL;
constexpr std::uint64_t kLegacyWeaponUpgradeExpireMs = 3ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
constexpr std::int32_t kLegacyMonsterGoldDropChunk = 2000;
constexpr std::int32_t kLegacyMonsterGoldDropMaxChunks = 17;
constexpr std::uint64_t kDoorAutoCloseMs = 5000;
constexpr std::uint64_t kStaticGateObjectBase = 0x7000000000000000ULL;
constexpr std::uint64_t kMapQuestNpcObjectBase = 0x7100000000000000ULL;
constexpr std::uint64_t kStartupQuestNpcObjectId = 0x71ffff0000000000ULL;
constexpr std::size_t kNpcDialogPageSize = 6;
constexpr std::int32_t kPoisonDecHealth = 0;
constexpr std::int32_t kLegacyPoisonStone = 5;
constexpr std::int32_t kRcDoorGuard = 11;
constexpr std::int32_t kRcArcherPolice = 20;
constexpr std::int32_t kRcWolf = 53;
constexpr std::int32_t kRcMonster = 80;
constexpr std::int32_t kRcOma = 81;
constexpr std::int32_t kRcSpitSpider = 82;
constexpr std::int32_t kRcSlowMonster = 83;
constexpr std::int32_t kRcKillingHerb = 85;
constexpr std::int32_t kRcSkeleton = 86;
constexpr std::int32_t kRcDualAxeSkeleton = 87;
constexpr std::int32_t kRcHeavyAxeSkeleton = 88;
constexpr std::int32_t kRcKnightSkeleton = 89;
constexpr std::int32_t kRcBigKudeki = 90;
constexpr std::int32_t kRcMagCowFaceMon = 91;
constexpr std::int32_t kRcThornDark = 93;
constexpr std::int32_t kRcDigOutZombi = 95;
constexpr std::int32_t kRcScultureKing = 102;
constexpr std::int32_t kRcBeeQueen = 103;
constexpr std::int32_t kRcArcherMon = 104;
constexpr std::int32_t kRcGasMoth = 105;
constexpr std::int32_t kRcGasDung = 106;
constexpr std::int32_t kRcCentipedeKing = 107;
constexpr std::int32_t kRcCastleDoor = 110;
constexpr std::int32_t kRcWall = 111;
constexpr std::int32_t kRcArcherGuard = 112;
constexpr std::int32_t kRcSpiderHouse = 116;
constexpr std::int32_t kRcHighRiskSpider = 118;
constexpr std::int32_t kRcBigPoisonSpider = 119;
constexpr std::int32_t kRcScultureKingNoFollower = 122;
constexpr std::int32_t kRcNoblePigKing = 124;
constexpr std::int32_t kRcToxicGhost = 127;

enum class LegacyMonsterRaceBehavior {
  normal,
  spit,
  front_gas,
  front_magic,
  fly_axe,
  stick_hide,
  digout_zombi,
  centipede,
  summoner,
  sculture_king,
  guard,
  structure
};

LegacyMonsterRaceBehavior legacy_monster_race_behavior(std::int32_t race_server) {
  switch (race_server) {
    case kRcSpitSpider:
    case kRcHighRiskSpider:
    case kRcBigPoisonSpider:
      return LegacyMonsterRaceBehavior::spit;
    case kRcBigKudeki:
    case kRcGasMoth:
    case kRcGasDung:
    case kRcToxicGhost:
      return LegacyMonsterRaceBehavior::front_gas;
    case kRcMagCowFaceMon:
      return LegacyMonsterRaceBehavior::front_magic;
    case kRcDualAxeSkeleton:
    case kRcThornDark:
    case kRcArcherMon:
      return LegacyMonsterRaceBehavior::fly_axe;
    case kRcKillingHerb:
      return LegacyMonsterRaceBehavior::stick_hide;
    case kRcDigOutZombi:
      return LegacyMonsterRaceBehavior::digout_zombi;
    case kRcCentipedeKing:
      return LegacyMonsterRaceBehavior::centipede;
    case kRcBeeQueen:
    case kRcSpiderHouse:
      return LegacyMonsterRaceBehavior::summoner;
    case kRcScultureKing:
    case kRcScultureKingNoFollower:
      return LegacyMonsterRaceBehavior::sculture_king;
    case kRcDoorGuard:
    case kRcArcherGuard:
    case kRcArcherPolice:
      return LegacyMonsterRaceBehavior::guard;
    case kRcCastleDoor:
    case kRcWall:
      return LegacyMonsterRaceBehavior::structure;
    default:
      return LegacyMonsterRaceBehavior::normal;
  }
}

bool legacy_monster_has_special_behavior(std::int32_t race_server) {
  return legacy_monster_race_behavior(race_server) != LegacyMonsterRaceBehavior::normal;
}

constexpr std::array<std::array<std::array<std::uint8_t, 5>, 5>, 8> kLegacySpitMap{{
    {{{0, 0, 1, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
    {{{0, 0, 0, 0, 1}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 1, 1}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 1}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 1, 0, 0}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {1, 0, 0, 0, 0}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {1, 1, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
    {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
}};
struct GuildTitlePage {
  std::string_view label;
  std::array<std::string_view, 3> titles;
};

constexpr std::array<GuildTitlePage, 2> kGuildTitlePages{{
    {"Core Roles", {"Member", "Deputy", "Elder"}},
    {"Field Roles", {"Vanguard", "Scout", "Quartermaster"}},
}};

std::int32_t compute_merchant_sell_price(
    const LegacyUserItem& item, const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
    std::int32_t price_rate_percent = 100);
LegacyPacket make_ack_packet(std::uint64_t session_id, bool ok);
LegacyPacket make_move_fail_packet(std::uint64_t session_id, const GameObject& object);
LegacyPacket make_system_notice_packet(std::uint64_t session_id, const std::string& message);
bool actor_undead(const GameObject& object);
std::pair<std::int32_t, std::int32_t> actor_magic_defense_range(const GameObject& object);

struct MerchantDialogEntry {
  std::string label;
  std::string action;
};

struct GuildMemberDialogTarget {
  std::size_t page{1};
  std::string member_name{};
};

struct GuildMemberTitleDialogTarget {
  std::size_t member_page{1};
  std::size_t title_page{1};
  std::string member_name{};
};

struct GuildApplicantDialogTarget {
  std::size_t page{1};
  std::string applicant_name{};
};

struct GuildBrowseTarget {
  std::string source{"directory"};
  std::size_t page{1};
  std::string guild_name{};
};

struct GuildBrowseListTarget {
  std::string source{"directory"};
  std::size_t browse_page{1};
  std::size_t list_page{1};
  std::string guild_name{};
};

struct GuildTitleConfirmTarget {
  std::size_t member_page{1};
  std::size_t title_page{1};
  std::string member_name{};
  std::string title_name{};
};

struct CastleWarConfirmTarget {
  std::size_t page{1};
  std::string guild_name{};
};

struct CastleGuildBrowseTarget {
  std::string source{"wars"};
  std::size_t page{1};
  std::string guild_name{};
};

struct CastleActionResult {
  bool handled{false};
  bool success{false};
  std::string summary{};
  std::vector<std::string> details{};
};

struct GuildActionResult {
  bool handled{false};
  std::string status{"Failed"};
  std::string summary{};
  std::vector<std::string> details{};
};

std::optional<std::int32_t> parse_int32(std::string_view text);
std::string join_tokens(const std::vector<std::string>& tokens, std::size_t start_index,
                        std::string_view separator = " ");

std::string normalize_guild_browse_source(std::string_view source) {
  const auto lowered = util::lower_copy(std::string(source));
  if (lowered == "applications" || lowered == "castle_show" || lowered == "castle_wars" ||
      lowered == "castle_targets") {
    return lowered;
  }
  return "directory";
}

std::string build_guild_browse_back_action(std::string_view source, std::size_t page,
                                           std::string_view guild_name = {}) {
  if (source == "applications") {
    return "@guild_my_applications " + std::to_string(static_cast<int>(page));
  }
  if (source == "castle_show") {
    return "@castle_show";
  }
  if (source == "castle_wars") {
    return "@castle_guild_browse wars " + std::to_string(static_cast<int>(page)) + " " +
           std::string(guild_name);
  }
  if (source == "castle_targets") {
    return "@castle_guild_browse targets " + std::to_string(static_cast<int>(page)) + " " +
           std::string(guild_name);
  }
  return "@guild_directory " + std::to_string(static_cast<int>(page));
}

std::string build_guild_browse_list_back_action(std::string_view source, std::size_t browse_page,
                                                std::string_view guild_name) {
  if (source == "applications" || source == "directory") {
    return "@guild_browse " + std::string(source) + " " +
           std::to_string(static_cast<int>(browse_page)) + " " + std::string(guild_name);
  }
  return build_guild_browse_back_action(source, browse_page, guild_name);
}

std::string summarize_name_list(const std::vector<std::string>& names, std::size_t preview_count = 3) {
  if (names.empty()) {
    return "None";
  }

  const auto count = std::min<std::size_t>(names.size(), preview_count);
  std::vector<std::string> preview;
  preview.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    preview.push_back(names[index]);
  }

  auto summary = join_tokens(preview, 0, ", ");
  if (names.size() > count) {
    summary += " +" + std::to_string(static_cast<int>(names.size() - count)) + " more";
  }
  return summary;
}

std::unique_ptr<GameObject> make_object(const ActorMail& mail) {
  switch (mail.kind) {
    case ActorMailKind::spawn_player:
      return std::make_unique<Player>(mail.actor_id, mail.session_id, mail.character);
    case ActorMailKind::spawn_monster:
      return std::make_unique<Monster>(mail.actor_id, mail.name, mail.map_id, mail.x, mail.y,
                                       mail.level, mail.max_hp, mail.attack_power, mail.dc_min,
                                       mail.dc_max, mail.defense, mail.magic_defense, mail.mc,
                                       mail.sc, mail.exp_reward, mail.life_attrib, mail.max_mp,
                                       mail.race_server, mail.race_image,
                                       mail.appearance, mail.cool_eye, mail.speed, mail.accuracy,
                                       mail.walk_speed_ms, mail.walk_step, mail.walk_wait_ms,
                                       mail.attack_speed_ms, mail.monster_ai_profile,
                                       mail.monster_search_rate_ms,
                                       mail.home_x, mail.home_y, mail.home_area,
                                       mail.legacy_spawn_group, mail.master_actor_id,
                                       mail.monster_is_slave, mail.slave_exp,
                                       mail.slave_make_level, mail.slave_exp_level,
                                       mail.master_royalty_time_ms, mail.slave_life_time_ms,
                                       mail.monster_no_item, mail.monster_tameable,
                                       mail.monster_drop_items,
                                       mail.monster_drop_gold);
    case ActorMailKind::spawn_npc:
      return std::make_unique<Npc>(mail.actor_id, mail.name, mail.map_id, mail.x, mail.y,
                                   mail.npc_service, mail.merchant_items,
                                   mail.npc_dialog_sections, mail.npc_price_rate_percent,
                                   mail.merchant_key, mail.merchant_products,
                                   mail.merchant_prices, mail.legacy_deal_std_modes,
                                   mail.weapon_upgrades);
    default:
      return std::make_unique<EventObject>(mail.actor_id, mail.name, mail.map_id, mail.x, mail.y);
  }
}

Player* as_player(GameObject* object) { return dynamic_cast<Player*>(object); }

const Player* as_player(const GameObject* object) { return dynamic_cast<const Player*>(object); }

Npc* as_npc(GameObject* object) { return dynamic_cast<Npc*>(object); }

const Npc* as_npc(const GameObject* object) { return dynamic_cast<const Npc*>(object); }

Monster* as_monster(GameObject* object) { return dynamic_cast<Monster*>(object); }

const Monster* as_monster(const GameObject* object) { return dynamic_cast<const Monster*>(object); }

bool is_legacy_player_command(ActorMailKind kind) {
  switch (kind) {
    case ActorMailKind::turn:
    case ActorMailKind::move:
    case ActorMailKind::run:
    case ActorMailKind::attack:
    case ActorMailKind::spell:
    case ActorMailKind::say:
    case ActorMailKind::click_npc:
    case ActorMailKind::merchant_select:
    case ActorMailKind::query_username:
    case ActorMailKind::query_bag_items:
    case ActorMailKind::query_storage_items:
    case ActorMailKind::query_detail_goods:
    case ActorMailKind::query_sell_price:
    case ActorMailKind::query_repair_cost:
    case ActorMailKind::drop_item:
    case ActorMailKind::pickup_item:
    case ActorMailKind::take_on_item:
    case ActorMailKind::take_off_item:
    case ActorMailKind::eat_item:
    case ActorMailKind::drop_gold:
    case ActorMailKind::revive:
    case ActorMailKind::buy_item:
    case ActorMailKind::sell_item:
    case ActorMailKind::repair_item:
    case ActorMailKind::storage_item:
    case ActorMailKind::take_back_storage_item:
    case ActorMailKind::trade_try:
    case ActorMailKind::trade_cancel:
    case ActorMailKind::trade_add_item:
    case ActorMailKind::trade_remove_item:
    case ActorMailKind::trade_set_gold:
    case ActorMailKind::trade_accept:
      return true;
    default:
      return false;
  }
}

bool is_legacy_response_compensated_command(ActorMailKind kind) {
  switch (kind) {
    case ActorMailKind::turn:
    case ActorMailKind::move:
    case ActorMailKind::run:
    case ActorMailKind::attack:
    case ActorMailKind::spell:
    case ActorMailKind::say:
    case ActorMailKind::click_npc:
    case ActorMailKind::merchant_select:
    case ActorMailKind::eat_item:
    case ActorMailKind::revive:
      return true;
    default:
      return false;
  }
}

std::int32_t tick_count_ms() {
  static const auto started = std::chrono::steady_clock::now();
  return static_cast<std::int32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                            started)
          .count());
}

std::uint8_t legacy_byte(std::int32_t value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::uint8_t actor_dir(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().dir;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->dir();
  }
  return 4;
}

std::uint8_t actor_light(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().light;
  }
  return 0;
}

std::int32_t actor_feature(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().feature;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return make_feature(legacy_byte(monster->race_image()), 0,
                        legacy_byte(monster->appearance()), 0);
  }
  return 0;
}

std::int32_t actor_status(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().status;
  }
  return 0;
}

std::int32_t actor_hit_speed(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->legacy_hit_speed();
  }
  return 0;
}

std::int32_t legacy_actor_anti_magic(const GameObject& object) {
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return std::max(monster->magical_defense(), 0);
  }
  return 0;
}

std::int32_t legacy_actor_anti_poison(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return std::max(player->legacy_anti_poison(), 0);
  }
  return 0;
}

std::uint8_t actor_name_color(const GameObject& object) {
  const auto* player = as_player(&object);
  if (player == nullptr) {
    return kDefaultNameColor;
  }
  if (player->pk_level() >= 2) {
    return 249;
  }
  if (player->pk_level() == 1) {
    return 251;
  }
  return player->legacy_name_color();
}

std::string actor_name(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().character_name;
  }
  return object.name();
}

bool actor_uses_skeleton_packet(const GameObject& object) {
  const auto* monster = as_monster(&object);
  if (monster == nullptr) {
    return false;
  }
  const auto lowered = util::lower_copy(monster->name());
  return lowered == "__whiteskeleton" || lowered == "__elf" || lowered == "__elfwarrior";
}

LegacyCharDesc make_char_desc(const GameObject& object) {
  LegacyCharDesc desc;
  desc.feature = actor_feature(object);
  desc.status = actor_status(object);
  return desc;
}

std::uint16_t legacy_map_darkness(const MapConfig& map_config) {
  if (map_config.daylight) {
    return 0;
  }
  if (map_config.darkness) {
    return 1;
  }
  return 2;
}

std::int32_t packed_min(std::uint16_t value) { return static_cast<std::int32_t>(value & 0xffu); }

std::int32_t packed_max(std::uint16_t value) {
  return static_cast<std::int32_t>((value >> 8) & 0xffu);
}

std::pair<std::int32_t, std::int32_t> direction_delta(std::uint8_t dir) {
  switch (dir % 8) {
    case 0:
      return {0, -1};
    case 1:
      return {1, -1};
    case 2:
      return {1, 0};
    case 3:
      return {1, 1};
    case 4:
      return {0, 1};
    case 5:
      return {-1, 1};
    case 6:
      return {-1, 0};
    case 7:
    default:
      return {-1, -1};
  }
}

std::int32_t resolve_attack_range(std::uint16_t ident) {
  switch (ident) {
    case kCmLongHit:
      return 2;
    default:
      return 1;
  }
}

bool legacy_p14_sword_skill(std::int32_t magic_id) {
  switch (magic_id) {
    case 3:
    case 4:
    case 7:
    case 12:
    case 25:
    case 26:
    case 27:
    case 34:
      return true;
    default:
      return false;
  }
}

std::uint16_t legacy_attack_ident_for_sword_skill(std::int32_t magic_id) {
  switch (magic_id) {
    case 4:
      return kCmHeavyHit;
    case 12:
      return kCmLongHit;
    case 25:
      return kCmWideHit;
    case 26:
      return kCmFireHit;
    case 34:
      return kCmCrossHit;
    case 3:
    default:
      return kCmHit;
  }
}

std::int32_t legacy_sword_skill_for_attack_ident(std::uint16_t ident) {
  switch (ident) {
    case kCmHeavyHit:
      return 4;
    case kCmPowerHit:
      return 7;
    case kCmLongHit:
      return 12;
    case kCmWideHit:
      return 25;
    case kCmFireHit:
      return 26;
    case kCmCrossHit:
      return 34;
    default:
      return 0;
  }
}

bool legacy_hit_roll_succeeds(std::int32_t accuracy_point, std::int32_t hit_roll) {
  return hit_roll < accuracy_point;
}

bool is_alive(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return !player->is_dead();
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return !monster->is_dead();
  }
  return false;
}

bool is_attackable_target(const GameObject& object) {
  return (as_player(&object) != nullptr || as_monster(&object) != nullptr) && is_alive(object);
}

bool is_safe_zone(const MapConfig& map_config, std::int32_t x, std::int32_t y);

bool is_legacy_monster_retaliation_source(const Monster& monster, const GameObject& source,
                                          const MapConfig& map_config,
                                          std::uint64_t current_tick) {
  if (const auto* source_player = as_player(&source); source_player != nullptr) {
    if (monster.is_slave() && source_player->id() == monster.master_actor_id()) {
      return false;
    }
    return !source_player->is_dead() && !source_player->legacy_ghost() &&
           !is_safe_zone(map_config, source_player->x(), source_player->y()) &&
           !source_player->legacy_transparent_active(current_tick);
  }
  const auto* source_monster = as_monster(&source);
  if (source_monster == nullptr || source_monster->id() == monster.id() ||
      source_monster->master_actor_id() == 0 || source_monster->is_dead() ||
      source_monster->legacy_ghosted() || source_monster->hide_mode()) {
    return false;
  }
  if (source_monster->id() == monster.master_actor_id() ||
      source_monster->master_actor_id() == monster.master_actor_id()) {
    return false;
  }
  return true;
}

std::int32_t apply_legacy_monster_damage(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    Monster& monster,
    std::int32_t damage,
    std::uint64_t source_actor_id,
    const MapConfig& map_config,
    std::uint64_t current_tick,
    std::uint64_t now_ms) {
  const auto was_dead = monster.is_dead();
  const auto applied = monster.apply_damage(damage, source_actor_id, now_ms);
  if (applied <= 0) {
    return 0;
  }

  if (!monster.is_dead() &&
      (monster.ai_profile() == MonsterAiProfile::basic ||
       monster.ai_profile() == MonsterAiProfile::aggressive)) {
    const auto source_it = objects.find(source_actor_id);
    if (source_it != objects.end()) {
      if (is_legacy_monster_retaliation_source(monster, *source_it->second,
                                               map_config, current_tick)) {
        monster.select_target(source_actor_id, now_ms);
      }
    }
  }

  if (!was_dead && monster.is_dead() && monster.death_time_ms() == 0) {
    static_cast<void>(monster.mark_legacy_death(now_ms));
  }
  return applied;
}

std::int32_t actor_hp(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().ability.hp;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->hp();
  }
  return 0;
}

std::int32_t actor_max_hp(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().ability.max_hp;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->max_hp();
  }
  return 0;
}

std::int32_t actor_level(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().ability.level;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->level();
  }
  return 1;
}

std::int32_t actor_physical_defense(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->physical_defense();
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->physical_defense();
  }
  return 0;
}

std::int32_t actor_magic_defense(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->magic_defense();
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->magical_defense();
  }
  return 0;
}

void queue_packet(RuntimeDispatch& dispatch, std::uint64_t session_id, LegacyPacket packet,
                  std::int32_t delay_ms) {
  dispatch.session_events.push_back(SessionEvent{
      SessionEventKind::send_packet, "game_gateway", session_id, {}, std::move(packet), {},
      delay_ms});
}

void queue_packet(RuntimeDispatch& dispatch, std::uint64_t session_id, LegacyPacket packet) {
  queue_packet(dispatch, session_id, std::move(packet), 0);
}

void queue_force_disconnect(RuntimeDispatch& dispatch, std::uint64_t session_id,
                            std::string reason) {
  dispatch.session_events.push_back(SessionEvent{
      SessionEventKind::force_disconnect, "game_gateway", session_id, {}, {}, std::move(reason)});
}

template <typename Fn>
void for_each_player(const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
                     Fn&& fn) {
  for (const auto& [actor_id, object] : objects) {
    const auto* player = as_player(object.get());
    if (player == nullptr) {
      continue;
    }
    fn(actor_id, *player);
  }
}

const ItemConfig* find_item_config(const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                                   std::int32_t item_index) {
  const auto it = item_configs.find(item_index);
  return it != item_configs.end() ? &it->second : nullptr;
}

std::string item_name(const LegacyUserItem& item,
                      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (const auto* config = find_item_config(item_configs, item.index);
      config != nullptr && !config->name.empty()) {
    return config->name;
  }
  return "Item " + std::to_string(item.index);
}

std::int32_t item_looks(const LegacyUserItem& item,
                        const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return config->looks > 0 ? config->looks : item.index;
  }
  return item.index;
}

std::int32_t item_weight(const LegacyUserItem& item,
                         const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (is_empty(item)) {
    return 0;
  }
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return std::max(config->weight, 0);
  }
  return 0;
}

std::int32_t gold_looks(std::int32_t amount) {
  if (amount >= 1000) {
    return 116;
  }
  if (amount >= 300) {
    return 115;
  }
  if (amount >= 70) {
    return 114;
  }
  if (amount >= 30) {
    return 113;
  }
  return 112;
}

std::int32_t display_dura_units(std::uint16_t dura) {
  return static_cast<std::int32_t>((static_cast<std::uint32_t>(dura) + 500) / 1000);
}

std::uint16_t item_dura_max(const LegacyUserItem& item,
                            const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (item.dura_max > 0) {
    return item.dura_max;
  }
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return static_cast<std::uint16_t>(std::clamp(config->dura_max, 0, 65535));
  }
  return 0;
}

std::int32_t resolve_slot_from_std_mode(std::int32_t std_mode) {
  return legacy_resolve_slot_from_std_mode(std_mode);
}

bool item_fits_slot(const ItemConfig& item_config, std::int32_t slot) {
  return legacy_item_fits_slot(item_config, slot);
}

bool is_consumable(const ItemConfig& item_config) {
  return legacy_item_is_consumable(item_config);
}

bool requires_detail_goods_list(const ItemConfig& item_config) {
  return !(item_config.std_mode <= 4 || item_config.std_mode == 31 || item_config.std_mode == 42);
}

bool in_interaction_range(const GameObject& lhs, const GameObject& rhs) {
  return std::abs(lhs.x() - rhs.x()) <= 15 && std::abs(lhs.y() - rhs.y()) <= 15;
}

bool is_directly_in_front_of(const GameObject& viewer, const GameObject& target) {
  const auto [dx, dy] = direction_delta(actor_dir(viewer));
  return target.x() == viewer.x() + dx && target.y() == viewer.y() + dy;
}

bool mutually_facing(const GameObject& lhs, const GameObject& rhs) {
  return is_directly_in_front_of(lhs, rhs) && is_directly_in_front_of(rhs, lhs);
}

bool in_legacy_view_range(std::int32_t lhs_x, std::int32_t lhs_y,
                          std::int32_t rhs_x, std::int32_t rhs_y) {
  return std::abs(lhs_x - rhs_x) <= kLegacyViewRange &&
         std::abs(lhs_y - rhs_y) <= kLegacyViewRange;
}

bool in_legacy_view_range(const GameObject& lhs, const GameObject& rhs) {
  return in_legacy_view_range(lhs.x(), lhs.y(), rhs.x(), rhs.y());
}

bool is_legacy_visible_to(const Player& watcher, const GameObject& target) {
  if (const auto* monster = as_monster(&target); monster != nullptr && monster->hide_mode()) {
    return false;
  }
  return watcher.id() != target.id() && in_legacy_view_range(watcher, target);
}

template <typename MakePacket>
void queue_actor_origin_packet(
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    RuntimeDispatch& dispatch, const GameObject& origin, bool include_origin,
    MakePacket&& make_packet) {
  static_cast<void>(dispatch);
  for_each_player(objects, [&](std::uint64_t, const Player& watcher) {
    if (watcher.id() == origin.id()) {
      if (!include_origin) {
        return;
      }
    } else if (!is_legacy_visible_to(watcher, origin)) {
      return;
    }
    make_packet(watcher);
  });
}

bool in_legacy_view_range(const GameObject& watcher, const MapActor::GroundItem& item) {
  return in_legacy_view_range(watcher.x(), watcher.y(), item.x, item.y);
}

std::vector<MerchantDialogEntry> build_merchant_dialog_entries(const Npc& merchant) {
  std::vector<MerchantDialogEntry> entries;
  if (merchant.supports_guild()) {
    entries.push_back({"Guild", "@guild_menu"});
  }
  if (merchant.supports_castle()) {
    entries.push_back({"Castle", "@castle_menu"});
  }
  if (merchant.supports_buy()) {
    entries.push_back({"Buy", "@buy"});
  }
  if (merchant.supports_sell()) {
    entries.push_back({"Sell", "@sell"});
  }
  if (merchant.supports_repair()) {
    entries.push_back({"Repair", "@repair"});
  }
  if (merchant.supports_storage()) {
    entries.push_back({"Store", "@storage"});
    entries.push_back({"Retrieve", "@getback"});
  }
  return entries;
}

std::size_t merchant_service_group_count(const Npc& merchant) {
  std::size_t count = 0;
  if (merchant.supports_guild()) {
    ++count;
  }
  if (merchant.supports_castle()) {
    ++count;
  }
  if (merchant.supports_buy()) {
    ++count;
  }
  if (merchant.supports_sell()) {
    ++count;
  }
  if (merchant.supports_repair()) {
    ++count;
  }
  if (merchant.supports_storage()) {
    ++count;
  }
  return count;
}

const std::string* find_npc_dialog_text(const Npc& merchant, std::string_view action) {
  auto normalize_action = [](std::string_view value) {
    auto normalized = util::lower_copy(util::trim(std::string(value)));
    if (normalized == "@home") {
      return std::string{"@main"};
    }
    if (normalized == "~@home") {
      return std::string{"~@main"};
    }
    return normalized;
  };
  const auto wanted = normalize_action(action);
  const auto tilde_wanted =
      util::starts_with(wanted, "@") ? "~" + wanted : std::string{};
  const auto plain_wanted =
      util::starts_with(wanted, "~@") ? wanted.substr(1) : std::string{};
  for (const auto& section : merchant.dialog_sections()) {
    const auto current = normalize_action(section.action);
    if (current == wanted || (!tilde_wanted.empty() && current == tilde_wanted) ||
        (!plain_wanted.empty() && current == plain_wanted)) {
      return &section.text;
    }
  }
  return nullptr;
}

void replace_all(std::string& text, std::string_view needle, std::string_view replacement) {
  if (needle.empty()) {
    return;
  }
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

void append_dialog_entry(std::string& text, std::string label, std::string action) {
  if (!text.empty() && text.back() != '\\') {
    text.push_back('\\');
  }
  text += "<" + std::move(label) + "/" + std::move(action) + ">";
}

void append_dialog_line(std::string& text, std::string line) {
  if (!text.empty() && text.back() != '\\') {
    text.push_back('\\');
  }
  text += std::move(line);
}

std::size_t dialog_total_pages(std::size_t item_count) {
  return std::max<std::size_t>(1, (item_count + kNpcDialogPageSize - 1) / kNpcDialogPageSize);
}

std::size_t clamp_dialog_page(std::size_t requested_page, std::size_t item_count) {
  return std::clamp<std::size_t>(requested_page, 1, dialog_total_pages(item_count));
}

std::size_t parse_dialog_page(std::string_view payload, std::string_view prefix) {
  const auto lowered = util::lower_copy(payload);
  if (!util::starts_with(lowered, prefix)) {
    return 1;
  }
  const auto suffix = util::trim(lowered.substr(prefix.size()));
  if (suffix.empty()) {
    return 1;
  }
  const auto page = parse_int32(suffix);
  return page.has_value() && *page > 0 ? static_cast<std::size_t>(*page) : 1;
}

GuildMemberDialogTarget parse_guild_member_dialog_target(std::string_view payload) {
  GuildMemberDialogTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 2) {
    return target;
  }

  const auto parsed_page = parse_int32(tokens[1]);
  if (parsed_page.has_value() && *parsed_page > 0) {
    target.page = static_cast<std::size_t>(*parsed_page);
    target.member_name = util::trim(join_tokens(tokens, 2));
    return target;
  }

  target.member_name = util::trim(join_tokens(tokens, 1));
  return target;
}

GuildMemberTitleDialogTarget parse_guild_member_title_dialog_target(std::string_view payload) {
  GuildMemberTitleDialogTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 4) {
    return target;
  }

  const auto member_page = parse_int32(tokens[1]);
  if (member_page.has_value() && *member_page > 0) {
    target.member_page = static_cast<std::size_t>(*member_page);
  }

  const auto title_page = parse_int32(tokens[2]);
  if (title_page.has_value() && *title_page > 0) {
    target.title_page = static_cast<std::size_t>(*title_page);
  }

  target.member_name = util::trim(join_tokens(tokens, 3));
  return target;
}

GuildApplicantDialogTarget parse_guild_applicant_dialog_target(std::string_view payload) {
  GuildApplicantDialogTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 3) {
    return target;
  }

  const auto page = parse_int32(tokens[1]);
  if (page.has_value() && *page > 0) {
    target.page = static_cast<std::size_t>(*page);
  }
  target.applicant_name = util::trim(join_tokens(tokens, 2));
  return target;
}

GuildBrowseTarget parse_guild_browse_target(std::string_view payload) {
  GuildBrowseTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 4) {
    return target;
  }

  target.source = normalize_guild_browse_source(tokens[1]);

  const auto page = parse_int32(tokens[2]);
  if (page.has_value() && *page > 0) {
    target.page = static_cast<std::size_t>(*page);
  }

  target.guild_name = util::trim(join_tokens(tokens, 3));
  return target;
}

GuildBrowseListTarget parse_guild_browse_list_target(std::string_view payload) {
  GuildBrowseListTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 5) {
    return target;
  }

  target.source = normalize_guild_browse_source(tokens[1]);

  const auto browse_page = parse_int32(tokens[2]);
  if (browse_page.has_value() && *browse_page > 0) {
    target.browse_page = static_cast<std::size_t>(*browse_page);
  }

  const auto list_page = parse_int32(tokens[3]);
  if (list_page.has_value() && *list_page > 0) {
    target.list_page = static_cast<std::size_t>(*list_page);
  }

  target.guild_name = util::trim(join_tokens(tokens, 4));
  return target;
}

GuildTitleConfirmTarget parse_guild_title_confirm_target(std::string_view payload) {
  GuildTitleConfirmTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 5) {
    return target;
  }

  const auto member_page = parse_int32(tokens[1]);
  if (member_page.has_value() && *member_page > 0) {
    target.member_page = static_cast<std::size_t>(*member_page);
  }

  const auto title_page = parse_int32(tokens[2]);
  if (title_page.has_value() && *title_page > 0) {
    target.title_page = static_cast<std::size_t>(*title_page);
  }

  target.member_name = util::trim(tokens[3]);
  target.title_name = util::trim(join_tokens(tokens, 4));
  return target;
}

CastleWarConfirmTarget parse_castle_war_confirm_target(std::string_view payload) {
  CastleWarConfirmTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 3) {
    return target;
  }

  const auto page = parse_int32(tokens[1]);
  if (page.has_value() && *page > 0) {
    target.page = static_cast<std::size_t>(*page);
  }
  target.guild_name = util::trim(join_tokens(tokens, 2));
  return target;
}

CastleGuildBrowseTarget parse_castle_guild_browse_target(std::string_view payload) {
  CastleGuildBrowseTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 4) {
    return target;
  }

  target.source = util::lower_copy(tokens[1]);
  if (target.source != "targets") {
    target.source = "wars";
  }

  const auto page = parse_int32(tokens[2]);
  if (page.has_value() && *page > 0) {
    target.page = static_cast<std::size_t>(*page);
  }
  target.guild_name = util::trim(join_tokens(tokens, 3));
  return target;
}

void append_page_navigation(std::string& text, std::string action_root, std::size_t page,
                            std::size_t total_pages) {
  if (page > 1) {
    append_dialog_entry(text, "Prev",
                        action_root + " " + std::to_string(static_cast<int>(page - 1)));
  }
  if (page < total_pages) {
    append_dialog_entry(text, "Next",
                        action_root + " " + std::to_string(static_cast<int>(page + 1)));
  }
}

std::string equipped_weapon_name(const Player& player,
                                 const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto* weapon = player.equipped_item(1);
  if (weapon == nullptr || is_empty(*weapon)) {
    return "your weapon";
  }
  const auto name = item_name(*weapon, item_configs);
  return name.empty() ? std::string("your weapon") : name;
}

std::string default_castle_war_date(const CastleDialogContext& castle_dialog_context);
std::string display_castle_wars(const CastleDialogContext& castle_dialog_context);
std::string display_castle_owner(const CastleDialogContext& castle_dialog_context);
std::string display_castle_lord(const CastleDialogContext& castle_dialog_context);

std::optional<std::pair<char, std::int32_t>> parse_legacy_script_variable_token(
    std::string_view raw) {
  auto token = util::trim(std::string(raw));
  if (token.size() != 2 || std::isdigit(static_cast<unsigned char>(token[1])) == 0) {
    return std::nullopt;
  }
  const auto group = static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])));
  if (group != 'P' && group != 'G' && group != 'D') {
    return std::nullopt;
  }
  return std::pair{group, static_cast<std::int32_t>(token[1] - '0')};
}

std::string legacy_script_str_value(
    const Player& player, const std::array<std::int32_t, 10>& script_global_params,
    std::string_view raw) {
  const auto variable = parse_legacy_script_variable_token(raw);
  if (!variable.has_value()) {
    return "0";
  }
  const auto [group, index] = *variable;
  if (group == 'P') {
    return std::to_string(player.script_param(index));
  }
  if (group == 'G') {
    return std::to_string(script_global_params[static_cast<std::size_t>(index)]);
  }
  return std::to_string(player.script_dice_param(index));
}

void render_legacy_script_str_values(
    std::string& text, const Player& player,
    const std::array<std::int32_t, 10>& script_global_params) {
  std::size_t pos = 0;
  while ((pos = text.find("$STR(", pos)) != std::string::npos) {
    const auto close = text.find(')', pos + 5);
    if (close == std::string::npos) {
      break;
    }
    auto start = pos;
    auto length = close - pos + 1;
    if (pos > 0 && text[pos - 1] == '<' && close + 1 < text.size() &&
        text[close + 1] == '>') {
      start = pos - 1;
      length += 2;
    }
    const auto replacement = legacy_script_str_value(
        player, script_global_params, std::string_view(text).substr(pos + 5, close - pos - 5));
    text.replace(start, length, replacement);
    pos = start + replacement.size();
  }
}

const std::array<std::int32_t, 10>& empty_legacy_script_global_params() {
  static const std::array<std::int32_t, 10> params{};
  return params;
}

std::string render_npc_dialog_text(const Npc& merchant, const Player& requester,
                                   const MapConfig& map_config,
                                   const CastleDialogContext& castle_dialog_context,
                                   std::string text,
                                   const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                                   const std::array<std::int32_t, 10>& script_global_params =
                                       empty_legacy_script_global_params()) {
  replace_all(text, "<$USERNAME>", requester.character().character_name);
  replace_all(text, "<$NPCNAME>", merchant.name());
  replace_all(text, "<$MAPID>", map_config.id);
  replace_all(text, "<$MAPNAME>", map_config.title.empty() ? map_config.id : map_config.title);
  replace_all(text, "<$USERWEAPON>", equipped_weapon_name(requester, item_configs));
  replace_all(text, "<$OWNERGUILD>", display_castle_owner(castle_dialog_context));
  replace_all(text, "<$LORD>", display_castle_lord(castle_dialog_context));
  replace_all(text, "<$GUILDWARFEE>", std::to_string(castle_dialog_context.guild_war_fee));
  replace_all(text, "<$UPGRADEWEAPONFEE>",
              std::to_string(castle_dialog_context.upgrade_weapon_fee));
  replace_all(text, "<$CASTLEWARDATE>", default_castle_war_date(castle_dialog_context));
  replace_all(text, "<$LISTOFWAR>", display_castle_wars(castle_dialog_context));
  render_legacy_script_str_values(text, requester, script_global_params);
  return text;
}

bool should_open_merchant_dialog(const Npc& merchant) {
  return find_npc_dialog_text(merchant, "@main") != nullptr || merchant.supports_guild() ||
         merchant.supports_castle() || merchant_service_group_count(merchant) > 1;
}

struct LegacyScriptProc {
  std::vector<std::string> say_lines{};
  std::vector<std::string> conditions{};
  std::vector<std::string> act_lines{};
  std::vector<std::string> else_say_lines{};
  std::vector<std::string> else_act_lines{};
};

struct LegacyScriptBlock {
  std::vector<LegacyScriptProc> procs{};
};

enum class LegacyScriptParseMode {
  say,
  condition,
  act,
  else_say,
  else_act
};

std::vector<std::string> split_legacy_script_lines(std::string_view text) {
  std::vector<std::string> lines;
  std::string current;
  for (const auto ch : text) {
    if (ch == '\n' || ch == '\r') {
      auto line = util::trim(current);
      if (!line.empty() && !util::starts_with(line, ";") && !util::starts_with(line, "/")) {
        lines.push_back(std::move(line));
      }
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  auto line = util::trim(current);
  if (!line.empty() && !util::starts_with(line, ";") && !util::starts_with(line, "/")) {
    lines.push_back(std::move(line));
  }
  return lines;
}

std::string strip_script_hash(std::string line) {
  line = util::trim(std::move(line));
  while (!line.empty() && line.front() == '#') {
    line.erase(line.begin());
    line = util::trim(std::move(line));
  }
  return line;
}

std::string script_upper_copy(std::string_view text) {
  std::string upper{text};
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return upper;
}

std::string script_command_name(std::string_view line) {
  auto command = strip_script_hash(std::string(line));
  const auto space = command.find(' ');
  if (space != std::string::npos) {
    command.resize(space);
  }
  return script_upper_copy(command);
}

std::string script_command_payload(std::string_view line) {
  auto command = strip_script_hash(std::string(line));
  const auto space = command.find(' ');
  if (space == std::string::npos) {
    return {};
  }
  return util::trim(command.substr(space + 1));
}

bool is_legacy_script_condition(std::string_view command_name) {
  static constexpr std::string_view kConditions[]{
      "CHECK", "CHECKOPEN", "CHECKUNIT", "RANDOM", "GENDER", "DAYTIME", "CHECKLEVEL",
      "CHECKJOB", "CHECKITEM", "CHECKITEMW", "CHECKGOLD", "ISTAKEITEM", "CHECKDURA",
      "CHECKDURAEVA", "DAYOFWEEK", "HOUR", "MIN", "CHECKPKPOINT", "CHECKLUCKYPOINT",
      "CHECKMONMAP", "CHECKMONAREA", "CHECKHUM", "CHECKBAGGAGE", "CHECKNAMELIST",
      "CHECKIDLIST", "CHECK_DELETE_NAMELIST", "CHECK_DELETE_IDLIST", "IFGETDAILYQUEST",
      "CHECKDAILYQUEST", "RANDOMEX", "EQUAL", "LARGE", "SMALL"};
  return std::find(std::begin(kConditions), std::end(kConditions), command_name) !=
         std::end(kConditions);
}

LegacyScriptBlock parse_legacy_script_block(std::string_view text) {
  LegacyScriptBlock block;
  LegacyScriptProc current;
  auto mode = LegacyScriptParseMode::say;
  auto has_content = [](const LegacyScriptProc& proc) {
    return !proc.say_lines.empty() || !proc.conditions.empty() || !proc.act_lines.empty() ||
           !proc.else_say_lines.empty() || !proc.else_act_lines.empty();
  };
  auto flush = [&]() {
    if (has_content(current)) {
      block.procs.push_back(std::move(current));
      current = LegacyScriptProc{};
      mode = LegacyScriptParseMode::say;
    }
  };
  for (auto line : split_legacy_script_lines(text)) {
    const auto command_name = script_command_name(line);
    const auto payload = script_command_payload(line);
    const auto hashed = !line.empty() && line.front() == '#';

    if (hashed && command_name == "IF") {
      if (has_content(current)) {
        flush();
      }
      mode = LegacyScriptParseMode::condition;
      if (!payload.empty()) {
        current.conditions.push_back(payload);
      }
      continue;
    }
    if (hashed && command_name == "ACT") {
      mode = LegacyScriptParseMode::act;
      if (!payload.empty()) {
        current.act_lines.push_back(payload);
      }
      continue;
    }
    if (hashed && (command_name == "ELSEACT" || command_name == "ELESACT")) {
      mode = LegacyScriptParseMode::else_act;
      if (!payload.empty()) {
        current.else_act_lines.push_back(payload);
      }
      continue;
    }
    if (hashed && command_name == "SAY") {
      mode = LegacyScriptParseMode::say;
      if (!payload.empty()) {
        current.say_lines.push_back(payload);
      }
      continue;
    }
    if (hashed && command_name == "ELSESAY") {
      mode = LegacyScriptParseMode::else_say;
      if (!payload.empty()) {
        current.else_say_lines.push_back(payload);
      }
      continue;
    }

    auto normalized_line = hashed ? strip_script_hash(std::move(line)) : std::move(line);
    const auto normalized_command = script_command_name(normalized_line);
    if (mode == LegacyScriptParseMode::condition || is_legacy_script_condition(normalized_command)) {
      current.conditions.push_back(std::move(normalized_line));
      continue;
    }
    if (mode == LegacyScriptParseMode::act) {
      current.act_lines.push_back(std::move(normalized_line));
    } else if (mode == LegacyScriptParseMode::else_act) {
      current.else_act_lines.push_back(std::move(normalized_line));
    } else if (mode == LegacyScriptParseMode::else_say) {
      current.else_say_lines.push_back(std::move(normalized_line));
    } else {
      current.say_lines.push_back(std::move(normalized_line));
    }
  }
  flush();
  return block;
}

std::string join_dialog_lines(const std::vector<std::string>& lines) {
  std::string text;
  for (const auto& line : lines) {
    append_dialog_line(text, line);
  }
  return text;
}

bool legacy_script_action_uses_existing_business(std::string_view lowered_payload, const Npc& npc) {
  if ((lowered_payload == "@buy" && npc.supports_buy()) ||
      (lowered_payload == "@sell" && npc.supports_sell()) ||
      ((lowered_payload == "@repair" || lowered_payload == "@s_repair") &&
       npc.supports_repair()) ||
      ((lowered_payload == "@storage" || lowered_payload == "@getback") &&
       npc.supports_storage()) ||
      ((lowered_payload == "@upgradenow" || lowered_payload == "@getbackupgnow") &&
       npc.supports_weapon_upgrade())) {
    return true;
  }
  if (util::starts_with(lowered_payload, "@guild_") && npc.supports_guild()) {
    return true;
  }
  if (util::starts_with(lowered_payload, "@castle_") && npc.supports_castle()) {
    return true;
  }
  return false;
}

const ItemConfig* find_item_config_by_name_or_id(
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs, std::string_view value) {
  const auto maybe_id = parse_int32(util::trim(std::string(value)));
  if (maybe_id.has_value()) {
    return find_item_config(item_configs, *maybe_id);
  }

  const auto wanted = util::lower_copy(util::trim(std::string(value)));
  const ItemConfig* best = nullptr;
  for (const auto& [id, item_config] : item_configs) {
    if (util::lower_copy(item_config.name) != wanted) {
      continue;
    }
    if (best == nullptr || id < best->id) {
      best = &item_config;
    }
  }
  return best;
}

struct LegacyScriptAmountTarget {
  std::string target{};
  std::int32_t amount{1};
};

constexpr std::string_view kLegacyGoldTokenUtf8 = "\xE9\x87\x91\xE5\xB8\x81";
constexpr std::string_view kLegacyGoldTokenUtf8Traditional = "\xE9\x87\x91\xE5\xB9\xA3";
constexpr std::string_view kLegacyGoldTokenGbk = "\xBD\xF0\xB1\xD2";

std::vector<std::string> split_script_tokens(std::string_view payload) {
  std::vector<std::string> tokens;
  std::string current;
  bool quoted = false;
  for (const auto ch : payload) {
    if (ch == '"') {
      quoted = !quoted;
      continue;
    }
    if (!quoted && std::isspace(static_cast<unsigned char>(ch)) != 0) {
      if (!current.empty()) {
        tokens.push_back(std::move(current));
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    tokens.push_back(std::move(current));
  }
  return tokens;
}

bool is_legacy_script_gold_token(std::string_view token) {
  const auto normalized = util::lower_copy(util::trim(std::string(token)));
  return normalized == "gold" || normalized == kLegacyGoldTokenUtf8 ||
         normalized == kLegacyGoldTokenUtf8Traditional || normalized == kLegacyGoldTokenGbk;
}

std::optional<LegacyScriptAmountTarget> parse_attached_gold_amount(std::string_view payload) {
  const auto normalized = util::lower_copy(util::trim(std::string(payload)));
  for (const auto coin : {std::string_view{"gold"}, kLegacyGoldTokenUtf8,
                          kLegacyGoldTokenUtf8Traditional, kLegacyGoldTokenGbk}) {
    if (normalized.size() <= coin.size() || !util::starts_with(normalized, coin)) {
      continue;
    }
    const auto suffix = util::trim(std::string(normalized.substr(coin.size())));
    const auto amount = parse_int32(suffix);
    if (!amount.has_value()) {
      continue;
    }
    LegacyScriptAmountTarget target;
    target.target = std::string(coin);
    target.amount = std::max(*amount, 0);
    return target;
  }
  return std::nullopt;
}

std::string strip_legacy_mark_token(std::string token) {
  token = util::trim(std::move(token));
  if (token.size() >= 2 && token.front() == '[' && token.back() == ']') {
    token = token.substr(1, token.size() - 2);
  }
  return util::trim(std::move(token));
}

std::optional<std::int32_t> parse_script_index(std::string_view token) {
  return parse_int32(strip_legacy_mark_token(std::string(token)));
}

LegacyScriptAmountTarget parse_script_amount_target(std::string_view payload) {
  LegacyScriptAmountTarget target;
  const auto tokens = split_script_tokens(payload);
  if (tokens.empty()) {
    return target;
  }
  if (tokens.size() > 1) {
    const auto maybe_amount = parse_int32(tokens.back());
    if (maybe_amount.has_value()) {
      target.amount = std::max(*maybe_amount, 0);
      target.target = util::trim(join_tokens(tokens, 0, " "));
      const auto suffix = std::string(" ") + tokens.back();
      if (target.target.size() >= suffix.size() &&
          target.target.substr(target.target.size() - suffix.size()) == suffix) {
        target.target.resize(target.target.size() - suffix.size());
        target.target = util::trim(std::move(target.target));
      }
      return target;
    }
  }
  if (tokens.size() == 1) {
    if (auto attached = parse_attached_gold_amount(tokens.front()); attached.has_value()) {
      return *attached;
    }
  }
  target.target = util::trim(std::string(payload));
  return target;
}

std::int32_t count_player_bag_items_by_name(
    const Player& player, std::string_view item_name_text,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto wanted = util::lower_copy(util::trim(std::string(item_name_text)));
  std::int32_t count = 0;
  for (const auto& item : player.character().bag_items) {
    if (!is_empty(item) && util::lower_copy(item_name(item, item_configs)) == wanted) {
      ++count;
    }
  }
  return count;
}

std::int32_t count_player_equipped_items_by_name(
    const Player& player, std::string_view item_name_text,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto wanted = util::lower_copy(util::trim(std::string(item_name_text)));
  std::int32_t count = 0;
  for (const auto& item : player.character().equipped_items) {
    if (!is_empty(item) && util::lower_copy(item_name(item, item_configs)) == wanted) {
      ++count;
    }
  }
  return count;
}

std::vector<std::size_t> legacy_equipment_slots_for_alias(std::string_view alias_text) {
  const auto alias = script_upper_copy(strip_legacy_mark_token(std::string(alias_text)));
  if (alias == "DRESS" || alias == "ARMOUR" || alias == "ARMOR") {
    return {kEquipDress};
  }
  if (alias == "WEAPON") {
    return {kEquipWeapon};
  }
  if (alias == "RIGHTHAND" || alias == "RIGHT" || alias == "TORCH") {
    return {kEquipRightHand};
  }
  if (alias == "NECKLACE" || alias == "NECK") {
    return {kEquipNecklace};
  }
  if (alias == "HELMET" || alias == "HELM") {
    return {kEquipHelmet};
  }
  if (alias == "ARMRING" || alias == "BRACELET") {
    return {kEquipArmRingLeft, kEquipArmRingRight};
  }
  if (alias == "RING") {
    return {kEquipRingLeft, kEquipRingRight};
  }
  if (alias == "BUJUK") {
    return {kEquipBujuk};
  }
  if (alias == "BELT") {
    return {kEquipBelt};
  }
  if (alias == "BOOTS" || alias == "BOOT") {
    return {kEquipBoots};
  }
  if (alias == "CHARM") {
    return {kEquipCharm};
  }
  return {};
}

std::string json_escape(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const auto ch : text) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

std::string join_tokens(const std::vector<std::string>& tokens, std::size_t start_index,
                        std::string_view separator) {
  std::string joined;
  for (std::size_t index = start_index; index < tokens.size(); ++index) {
    if (!joined.empty()) {
      joined += separator;
    }
    joined += tokens[index];
  }
  return joined;
}

std::optional<std::int32_t> parse_int32(std::string_view text) {
  std::int32_t value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

bool is_admin_account(std::string_view account_id) {
  const auto lowered = util::lower_copy(account_id);
  return lowered == "guest" || lowered == "admin" || util::starts_with(lowered, "gm");
}

std::string default_unclaimed_castle_owner(const CastleDialogContext& castle_dialog_context);

std::string normalize_castle_owner(const CastleDialogContext& castle_dialog_context,
                                   std::string owner) {
  const auto lowered = util::lower_copy(owner);
  if (lowered.empty() || lowered == "none" || lowered == "unclaimed" || lowered == "-" ||
      lowered == util::lower_copy(default_unclaimed_castle_owner(castle_dialog_context))) {
    return {};
  }
  return owner;
}

std::vector<std::string> parse_castle_war_list(const CastleDialogContext& castle_dialog_context);
std::string summarize_castle_wars(const CastleDialogContext& castle_dialog_context);
std::string describe_castle_owner_role(const CastleDialogContext& castle_dialog_context);
std::string display_castle_owner(const CastleDialogContext& castle_dialog_context);
std::string display_castle_lord(const CastleDialogContext& castle_dialog_context);

std::string default_castle_name(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.castle_name.empty() ? std::string("Sabuk")
                                                   : castle_dialog_context.castle_name;
}

std::string default_unclaimed_castle_owner(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.unclaimed_owner_label.empty() ? std::string("Unclaimed")
                                                             : castle_dialog_context.unclaimed_owner_label;
}

std::string default_unclaimed_castle_lord(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.unclaimed_lord_label.empty() ? std::string("Unclaimed")
                                                            : castle_dialog_context.unclaimed_lord_label;
}

std::string default_castle_owner_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.owner_role_label.empty() ? std::string("Castle Owner")
                                                        : castle_dialog_context.owner_role_label;
}

std::string default_castle_owner_guild_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.owner_guild_role_label.empty() ? std::string("Owner")
                                                              : castle_dialog_context.owner_guild_role_label;
}

std::string default_castle_challenger_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.challenger_role_label.empty() ? std::string("Challenger")
                                                             : castle_dialog_context.challenger_role_label;
}

std::string default_castle_rival_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.rival_role_label.empty() ? std::string("Rival")
                                                        : castle_dialog_context.rival_role_label;
}

std::string default_castle_unknown_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.unknown_role_label.empty() ? std::string("Unknown")
                                                          : castle_dialog_context.unknown_role_label;
}

std::string default_castle_war_entry_listed_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_entry_listed_label.empty() ? std::string("Listed")
                                                              : castle_dialog_context.war_entry_listed_label;
}

std::string default_castle_war_entry_unlisted_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_entry_unlisted_label.empty() ? std::string("Not Listed")
                                                                : castle_dialog_context.war_entry_unlisted_label;
}

std::string default_castle_war_status_active_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_status_active_label.empty() ? std::string("Active")
                                                               : castle_dialog_context.war_status_active_label;
}

std::string default_castle_war_status_available_label(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_status_available_label.empty() ? std::string("Available")
                                                                  : castle_dialog_context.war_status_available_label;
}

std::string default_castle_role_change_owner_label(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.role_change_owner_label.empty() ? std::string("Castle Owner")
                                                               : castle_dialog_context.role_change_owner_label;
}

std::string default_castle_role_change_challenger_label(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.role_change_challenger_label.empty()
             ? std::string("Castle Challenger")
             : castle_dialog_context.role_change_challenger_label;
}

std::string default_castle_claim_summary_template(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.claim_summary_template.empty()
             ? std::string("Castle claimed for guild <$GUILD>.")
             : castle_dialog_context.claim_summary_template;
}

std::string default_castle_war_summary_template(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_summary_template.empty()
             ? std::string("Castle war declared against <$TARGETGUILD> for <$GOLD> gold.")
             : castle_dialog_context.war_summary_template;
}

std::string default_castle_war_date(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.castle_war_date.empty() ? std::string("Unknown")
                                                       : castle_dialog_context.castle_war_date;
}

std::string render_castle_summary_template(std::string text,
                                           const CastleDialogContext& castle_dialog_context,
                                           std::string_view guild_name = {},
                                           std::string_view target_guild_name = {},
                                           std::int32_t gold = 0) {
  replace_all(text, "<$CASTLE>", default_castle_name(castle_dialog_context));
  replace_all(text, "<$GUILD>", std::string(guild_name));
  replace_all(text, "<$TARGETGUILD>", std::string(target_guild_name));
  replace_all(text, "<$GOLD>", std::to_string(gold));
  replace_all(text, "<$OWNERGUILD>", display_castle_owner(castle_dialog_context));
  replace_all(text, "<$LORD>", display_castle_lord(castle_dialog_context));
  return text;
}

std::string configured_summary_template(std::string_view configured, std::string_view fallback) {
  return configured.empty() ? std::string(fallback) : std::string(configured);
}

std::string render_guild_summary_template(std::string text, std::string_view guild_name = {},
                                          std::string_view target_name = {},
                                          std::string_view title_name = {},
                                          std::string_view new_lord = {},
                                          std::int32_t gold = 0) {
  replace_all(text, "<$GUILD>", std::string(guild_name));
  replace_all(text, "<$TARGET>", std::string(target_name));
  replace_all(text, "<$TITLE>", std::string(title_name));
  replace_all(text, "<$NEWLORD>", std::string(new_lord));
  replace_all(text, "<$GOLD>", std::to_string(gold));
  return text;
}

std::string render_guild_notice_template(std::string text, std::string_view guild_name = {},
                                         std::string_view target_name = {},
                                         std::string_view title_name = {}) {
  return render_guild_summary_template(std::move(text), guild_name, target_name, title_name);
}

std::string no_active_wars_text(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.no_active_wars_text.empty() ? std::string("No active wars.")
                                                           : castle_dialog_context.no_active_wars_text;
}

std::string display_castle_wars(const CastleDialogContext& castle_dialog_context) {
  return parse_castle_war_list(castle_dialog_context).empty() ? no_active_wars_text(castle_dialog_context)
                                                              : castle_dialog_context.list_of_war;
}

bool is_unclaimed_castle_owner(const CastleDialogContext& castle_dialog_context) {
  return util::trim(castle_dialog_context.owner_guild).empty();
}

std::string display_castle_owner(const CastleDialogContext& castle_dialog_context) {
  return is_unclaimed_castle_owner(castle_dialog_context)
             ? default_unclaimed_castle_owner(castle_dialog_context)
             : castle_dialog_context.owner_guild;
}

std::string display_castle_lord(const CastleDialogContext& castle_dialog_context) {
  return is_unclaimed_castle_owner(castle_dialog_context) ||
                 util::trim(castle_dialog_context.lord).empty()
             ? default_unclaimed_castle_lord(castle_dialog_context)
             : castle_dialog_context.lord;
}

std::string build_castle_payload(const CastleDialogContext& castle_dialog_context,
                                 std::optional<std::string> owner_guild_override = std::nullopt,
                                 std::optional<std::string> war_date_override = std::nullopt,
                                 std::optional<std::string> wars_override = std::nullopt,
                                 std::optional<std::int32_t> guild_fee_override = std::nullopt,
                                 std::optional<std::int32_t> upgrade_fee_override = std::nullopt,
                                 std::optional<std::int32_t> guild_create_fee_override =
                                     std::nullopt) {
  const auto owner_guild = owner_guild_override.value_or(castle_dialog_context.owner_guild);
  const auto war_date = war_date_override.value_or(castle_dialog_context.castle_war_date);
  const auto wars = wars_override.value_or(castle_dialog_context.list_of_war);
  const auto guild_fee = guild_fee_override.value_or(castle_dialog_context.guild_war_fee);
  const auto upgrade_fee = upgrade_fee_override.value_or(castle_dialog_context.upgrade_weapon_fee);
  const auto guild_create_fee =
      guild_create_fee_override.value_or(castle_dialog_context.guild_create_fee);

  std::ostringstream payload;
  payload << "{\"owner_guild\":\"" << json_escape(owner_guild) << "\""
          << ",\"castle_war_date\":\"" << json_escape(war_date) << "\""
          << ",\"list_of_war\":\"" << json_escape(wars) << "\""
          << ",\"guild_war_fee\":" << guild_fee
          << ",\"upgrade_weapon_fee\":" << upgrade_fee
          << ",\"guild_create_fee\":" << guild_create_fee << "}";
  return payload.str();
}

std::string build_castle_show_line(const CastleDialogContext& castle_dialog_context) {
  const auto wars = parse_castle_war_list(castle_dialog_context);
  std::ostringstream line;
  line << "Castle=" << default_castle_name(castle_dialog_context)
       << " Owner=" << display_castle_owner(castle_dialog_context)
       << " Lord=" << display_castle_lord(castle_dialog_context)
       << " OwnerRole=" << describe_castle_owner_role(castle_dialog_context)
       << " WarDate=" << default_castle_war_date(castle_dialog_context)
       << " WarCount=" << wars.size()
       << " WarPreview=" << summarize_castle_wars(castle_dialog_context)
       << " Fees=" << castle_dialog_context.guild_war_fee << "/"
       << castle_dialog_context.upgrade_weapon_fee;
  return line.str();
}

bool equals_ignore_case(std::string_view lhs, std::string_view rhs) {
  return util::lower_copy(lhs) == util::lower_copy(rhs);
}

void queue_system_notice(RuntimeDispatch& dispatch, const Player& player, std::string message) {
  queue_packet(dispatch, player.session_id(),
               make_system_notice_packet(player.session_id(), std::move(message)));
}

void queue_save_character(RuntimeDispatch& dispatch, const Player& player) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_character;
  request.account_id = player.character().account_id;
  request.character_name = player.character().character_name;
  request.character = player.persistent_snapshot();
  dispatch.persist_requests.push_back(std::move(request));
}

void queue_save_character(RuntimeDispatch& dispatch, const CharacterRecord& character) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_character;
  request.account_id = character.account_id;
  request.character_name = character.character_name;
  request.character = character;
  dispatch.persist_requests.push_back(std::move(request));
}

void queue_save_guild_state(RuntimeDispatch& dispatch, const GuildState& guild_state) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_guild_state;
  request.reply_to = "world_service";
  request.guild_name = guild_state.guild_name;
  request.guild_state = guild_state;
  dispatch.persist_requests.push_back(std::move(request));
}

void queue_delete_guild(RuntimeDispatch& dispatch, std::string guild_name) {
  PersistRequest request;
  request.kind = PersistRequestKind::delete_guild;
  request.reply_to = "world_service";
  request.guild_name = std::move(guild_name);
  dispatch.persist_requests.push_back(std::move(request));
}

void queue_save_castle_state(RuntimeDispatch& dispatch,
                             const CastleDialogContext& castle_dialog_context) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_castle_state;
  request.reply_to = "world_service";
  request.castle_name = default_castle_name(castle_dialog_context);
  request.payload_json = build_castle_payload(castle_dialog_context);
  dispatch.persist_requests.push_back(std::move(request));
}

void queue_load_offline_guild_character(RuntimeDispatch& dispatch,
                                        const OfflineGuildCharacterOp& operation) {
  PersistRequest request;
  request.kind = PersistRequestKind::load_character_by_name;
  request.reply_to = "world_service";
  request.character_name = operation.target_name;
  request.request_id = encode_offline_guild_character_op(operation);
  dispatch.persist_requests.push_back(std::move(request));
}

void queue_cross_map_notice(RuntimeDispatch& dispatch, std::string map_id, std::uint64_t actor_id,
                            std::string message) {
  if (map_id.empty() || actor_id == 0 || message.empty()) {
    return;
  }
  ActorMail mail;
  mail.kind = ActorMailKind::system_notice;
  mail.map_id = std::move(map_id);
  mail.actor_id = actor_id;
  mail.payload = std::move(message);
  dispatch.cross_map_mails.push_back(std::move(mail));
}

void queue_cross_map_guild_membership_sync(RuntimeDispatch& dispatch, std::string map_id,
                                           std::uint64_t actor_id, CharacterRecord character,
                                           std::string notice) {
  if (map_id.empty() || actor_id == 0) {
    return;
  }
  ActorMail mail;
  mail.kind = ActorMailKind::guild_membership_sync;
  mail.map_id = std::move(map_id);
  mail.actor_id = actor_id;
  mail.character = std::move(character);
  mail.payload = std::move(notice);
  dispatch.cross_map_mails.push_back(std::move(mail));
}

void set_character_guild_membership(CharacterRecord& character, std::string guild_name,
                                    std::string guild_title) {
  character.guild_name = std::move(guild_name);
  character.guild_title = std::move(guild_title);
}

void clear_character_guild_membership(CharacterRecord& character) {
  character.guild_name.clear();
  character.guild_title.clear();
}

GuildState* find_guild_state(GuildCastleSnapshot& guild_castle_snapshot, std::string_view guild_name) {
  for (auto& guild_state : guild_castle_snapshot.guilds) {
    if (equals_ignore_case(guild_state.guild_name, guild_name)) {
      return &guild_state;
    }
  }
  return nullptr;
}

const GuildState* find_guild_state(const GuildCastleSnapshot& guild_castle_snapshot,
                                   std::string_view guild_name) {
  for (const auto& guild_state : guild_castle_snapshot.guilds) {
    if (equals_ignore_case(guild_state.guild_name, guild_name)) {
      return &guild_state;
    }
  }
  return nullptr;
}

Player* find_online_player_by_name(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    std::string_view character_name) {
  for (auto& [_, object] : objects) {
    auto* player = as_player(object.get());
    if (player != nullptr &&
        equals_ignore_case(player->character().character_name, character_name)) {
      return player;
    }
  }
  return nullptr;
}

const Player* find_online_player_by_name(
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    std::string_view character_name) {
  for (const auto& [_, object] : objects) {
    const auto* player = as_player(object.get());
    if (player != nullptr &&
        equals_ignore_case(player->character().character_name, character_name)) {
      return player;
    }
  }
  return nullptr;
}

bool guild_has_member(const GuildState& guild_state, std::string_view member_name) {
  return std::any_of(guild_state.members.begin(), guild_state.members.end(),
                     [&](const std::string& member) {
                       return equals_ignore_case(member, member_name);
                     });
}

bool guild_has_applicant(const GuildState& guild_state, std::string_view applicant_name) {
  return std::any_of(guild_state.applicants.begin(), guild_state.applicants.end(),
                     [&](const std::string& applicant) {
                       return equals_ignore_case(applicant, applicant_name);
                     });
}

void add_guild_member(GuildState& guild_state, std::string member_name) {
  member_name = util::trim(std::move(member_name));
  if (member_name.empty() || guild_has_member(guild_state, member_name)) {
    return;
  }
  guild_state.members.push_back(std::move(member_name));
}

void add_guild_applicant(GuildState& guild_state, std::string applicant_name) {
  applicant_name = util::trim(std::move(applicant_name));
  if (applicant_name.empty() || guild_has_member(guild_state, applicant_name) ||
      guild_has_applicant(guild_state, applicant_name)) {
    return;
  }
  guild_state.applicants.push_back(std::move(applicant_name));
}

void remove_guild_member(GuildState& guild_state, std::string_view member_name) {
  guild_state.members.erase(
      std::remove_if(guild_state.members.begin(), guild_state.members.end(),
                     [&](const std::string& member) {
                       return equals_ignore_case(member, member_name);
                     }),
      guild_state.members.end());
}

void remove_guild_applicant(GuildState& guild_state, std::string_view applicant_name) {
  guild_state.applicants.erase(
      std::remove_if(guild_state.applicants.begin(), guild_state.applicants.end(),
                     [&](const std::string& applicant) {
                       return equals_ignore_case(applicant, applicant_name);
                     }),
      guild_state.applicants.end());
}

void sync_castle_lord_from_owner(GuildCastleSnapshot& guild_castle_snapshot) {
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  castle_dialog_context.owner_guild =
      normalize_castle_owner(castle_dialog_context, castle_dialog_context.owner_guild);
  if (is_unclaimed_castle_owner(castle_dialog_context)) {
    castle_dialog_context.lord.clear();
    return;
  }
  if (const auto* owner_guild =
          find_guild_state(guild_castle_snapshot, castle_dialog_context.owner_guild);
      owner_guild != nullptr && !owner_guild->lord.empty()) {
    castle_dialog_context.lord = owner_guild->lord;
  } else {
    castle_dialog_context.lord.clear();
  }
}

std::vector<std::string> parse_castle_war_list(const CastleDialogContext& castle_dialog_context) {
  const auto trimmed = util::trim(castle_dialog_context.list_of_war);
  if (trimmed.empty() || equals_ignore_case(trimmed, "No active wars.") ||
      equals_ignore_case(trimmed, no_active_wars_text(castle_dialog_context))) {
    return {};
  }
  auto wars = util::split(trimmed, ',');
  wars.erase(std::remove_if(wars.begin(), wars.end(), [](const std::string& name) {
               return name.empty();
             }),
             wars.end());
  return wars;
}

std::string describe_castle_guild_role(const CastleDialogContext& castle_dialog_context,
                                       std::string_view guild_name) {
  const auto normalized_name = util::trim(std::string(guild_name));
  if (normalized_name.empty()) {
    return "Unaffiliated";
  }
  if (equals_ignore_case(castle_dialog_context.owner_guild, normalized_name)) {
    return default_castle_owner_guild_role_label(castle_dialog_context);
  }

  const auto wars = parse_castle_war_list(castle_dialog_context);
  const auto listed =
      std::any_of(wars.begin(), wars.end(), [&](const std::string& war_name) {
        return equals_ignore_case(war_name, normalized_name);
      });
  return listed ? default_castle_challenger_role_label(castle_dialog_context)
                : default_castle_rival_role_label(castle_dialog_context);
}

std::string summarize_castle_wars(const CastleDialogContext& castle_dialog_context) {
  const auto wars = parse_castle_war_list(castle_dialog_context);
  return wars.empty() ? no_active_wars_text(castle_dialog_context) : summarize_name_list(wars);
}

std::string describe_castle_owner_role(const CastleDialogContext& castle_dialog_context) {
  return is_unclaimed_castle_owner(castle_dialog_context)
             ? default_unclaimed_castle_owner(castle_dialog_context)
             : default_castle_owner_role_label(castle_dialog_context);
}

void append_castle_guild_list_summary(std::string& text,
                                      const GuildCastleSnapshot& guild_castle_snapshot,
                                      std::string_view guild_name) {
  append_dialog_line(
      text, "Role: " + describe_castle_guild_role(guild_castle_snapshot.castle_dialog, guild_name));
  if (const auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
      guild_state != nullptr) {
    append_dialog_line(text, "Members/Applicants: " +
                                 std::to_string(guild_state->members.size()) + "/" +
                                 std::to_string(guild_state->applicants.size()));
    append_dialog_line(text, "Preview: " + summarize_name_list(guild_state->members, 2));
  } else {
    append_dialog_line(text, "Guild Data: Unknown");
  }
}

void append_guild_directory_summary(std::string& text,
                                    const GuildCastleSnapshot& guild_castle_snapshot,
                                    const GuildState& guild_state) {
  append_dialog_line(
      text, "Castle Role: " +
                describe_castle_guild_role(guild_castle_snapshot.castle_dialog,
                                           guild_state.guild_name));
  append_dialog_line(text, "Members/Applicants: " +
                               std::to_string(guild_state.members.size()) + "/" +
                               std::to_string(guild_state.applicants.size()));
  append_dialog_line(text, "Preview: " + summarize_name_list(guild_state.members, 2));
}

void append_guild_browse_summary(std::string& text,
                                 const GuildCastleSnapshot& guild_castle_snapshot,
                                 const GuildState& guild_state) {
  append_dialog_line(
      text, "Castle Role: " +
                describe_castle_guild_role(guild_castle_snapshot.castle_dialog,
                                           guild_state.guild_name));
  append_dialog_line(text, "Members/Applicants: " +
                               std::to_string(guild_state.members.size()) + "/" +
                               std::to_string(guild_state.applicants.size()));
  append_dialog_line(text, "Preview: " + summarize_name_list(guild_state.members));
  append_dialog_line(text, "Applicant Preview: " + summarize_name_list(guild_state.applicants));
}

std::string build_guild_info_line(const Player& speaker,
                                  const GuildCastleSnapshot& guild_castle_snapshot,
                                  const GuildState* guild_state) {
  const auto& character = speaker.character();
  if (guild_state == nullptr) {
    std::ostringstream line;
    line << "Guild=" << character.guild_name << " Role=" << character.guild_title
         << " CastleRole=Unknown Members/Applicants=unknown";
    return line.str();
  }

  std::ostringstream line;
  line << "Guild=" << guild_state->guild_name << " Role=" << character.guild_title
       << " Lord=" << guild_state->lord
       << " CastleRole="
       << describe_castle_guild_role(guild_castle_snapshot.castle_dialog, guild_state->guild_name)
       << " Members/Applicants=" << guild_state->members.size() << "/"
       << guild_state->applicants.size()
       << " Preview=" << summarize_name_list(guild_state->members)
       << " ApplicantPreview=" << summarize_name_list(guild_state->applicants);
  return line.str();
}

std::string build_guild_info_dialog_text(const Player& requester,
                                         const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Guild Info\\";
  const auto& character = requester.character();
  if (character.guild_name.empty()) {
    append_dialog_line(text, "You are not in a guild.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto* guild_state = find_guild_state(guild_castle_snapshot, character.guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  append_dialog_line(text, "Guild: " + guild_state->guild_name);
  append_dialog_line(text, "Your Role: " + character.guild_title);
  append_dialog_line(text, "Lord: " + guild_state->lord);
  append_guild_browse_summary(text, guild_castle_snapshot, *guild_state);
  append_dialog_entry(text, "Members", "@guild_members");
  if (equals_ignore_case(guild_state->lord, character.character_name)) {
    append_dialog_entry(text, "Applicants", "@guild_applicants");
  }
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string resolve_guild_member_title(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildState& guild_state, std::string_view member_name) {
  if (equals_ignore_case(member_name, guild_state.lord)) {
    return "Lord";
  }
  if (equals_ignore_case(member_name, requester.character().character_name) &&
      !requester.character().guild_title.empty()) {
    return requester.character().guild_title;
  }
  if (const auto* member = find_online_player_by_name(objects, member_name);
      member != nullptr &&
      equals_ignore_case(member->character().guild_name, guild_state.guild_name) &&
      !member->character().guild_title.empty()) {
    return member->character().guild_title;
  }
  return "Member";
}

std::string build_guild_service_dialog_text(const Player& requester,
                                            const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Guild Office\\";
  const auto& character = requester.character();
  if (character.guild_name.empty()) {
    append_dialog_entry(text, "Create Guild", "@guild_create_menu");
    if (guild_castle_snapshot.guilds.empty()) {
      append_dialog_line(text, "No guilds are currently registered.");
    } else {
      append_dialog_entry(text, "Guild Directory", "@guild_directory");
      if (std::any_of(guild_castle_snapshot.guilds.begin(), guild_castle_snapshot.guilds.end(),
                      [&](const GuildState& guild_state) {
                        return guild_has_applicant(guild_state, character.character_name);
                      })) {
        append_dialog_entry(text, "My Applications", "@guild_my_applications");
      }
    }
  } else {
    append_dialog_entry(text, "Info", "@guild_info");
    append_dialog_entry(text, "Members", "@guild_members");
    append_dialog_entry(text, "Leave", "@guild_leave_confirm");

    const auto* guild_state = find_guild_state(guild_castle_snapshot, character.guild_name);
    if (guild_state != nullptr &&
        equals_ignore_case(guild_state->lord, character.character_name)) {
      append_dialog_entry(text, "Applicants", "@guild_applicants");
    }
  }

  append_dialog_entry(text, "Back", "@main");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::vector<std::string> build_guild_create_suggestions(
    const Player& requester, const GuildCastleSnapshot& guild_castle_snapshot) {
  std::vector<std::string> suggestions;
  auto add_suggestion = [&](std::string name) {
    name = util::trim(std::move(name));
    if (name.empty() || find_guild_state(guild_castle_snapshot, name) != nullptr) {
      return;
    }
    if (std::any_of(suggestions.begin(), suggestions.end(), [&](const std::string& existing) {
          return equals_ignore_case(existing, name);
        })) {
      return;
    }
    suggestions.push_back(std::move(name));
  };

  const auto base_name = util::trim(requester.character().character_name);
  add_suggestion(base_name + "Guild");
  add_suggestion(base_name + "Hall");
  add_suggestion(base_name + "Legion");
  for (int suffix = 2; suggestions.size() < 3 && suffix <= 9; ++suffix) {
    add_suggestion(base_name + "Guild" + std::to_string(suffix));
  }
  return suggestions;
}

std::string build_guild_create_menu_dialog_text(const Player& requester,
                                                const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Guild Charter\\";
  if (!requester.character().guild_name.empty()) {
    append_dialog_line(text, "You already belong to a guild.");
    append_dialog_entry(text, "Guild", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  append_dialog_line(text, "Founder: " + requester.character().character_name);
  append_dialog_line(text, "Founding Fee: " +
                               std::to_string(guild_castle_snapshot.castle_dialog.guild_create_fee));
  append_dialog_line(text, "Gold: " + std::to_string(requester.character().gold));
  append_dialog_line(text, "Choose a charter name below or use @guild_create <name> in chat.");
  const auto suggestions = build_guild_create_suggestions(requester, guild_castle_snapshot);
  if (suggestions.empty()) {
    append_dialog_line(text, "No charter suggestions are currently available.");
  } else {
    for (const auto& suggestion : suggestions) {
      append_dialog_entry(text, "Create " + suggestion, "@guild_create_confirm " + suggestion);
    }
  }
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_create_confirm_dialog_text(const Player& requester,
                                                   const GuildCastleSnapshot& guild_castle_snapshot,
                                                   std::string guild_name) {
  std::string text = "Create Guild\\";
  guild_name = util::trim(std::move(guild_name));
  const auto guild_create_fee = guild_castle_snapshot.castle_dialog.guild_create_fee;
  append_dialog_line(text, "Founder: " + requester.character().character_name);
  append_dialog_line(text, "Guild: " + guild_name);
  append_dialog_line(text, "Founding Fee: " + std::to_string(guild_create_fee));
  append_dialog_line(text, "Gold: " + std::to_string(requester.character().gold));

  std::string status = "Ready";
  if (!requester.character().guild_name.empty()) {
    status = "Already In Guild";
  } else if (guild_name.empty()) {
    status = "Missing Name";
  } else if (find_guild_state(guild_castle_snapshot, guild_name) != nullptr) {
    status = "Name Unavailable";
  } else if (guild_create_fee > 0 && !requester.can_spend_gold(guild_create_fee)) {
    status = "Need " + std::to_string(guild_create_fee) + " Gold";
  }
  append_dialog_line(text, "Status: " + status);
  if (status == "Ready") {
    append_dialog_entry(text, "Confirm", "@guild_create_exec " + guild_name);
  } else if (!guild_name.empty()) {
    append_dialog_entry(text, "View Result", "@guild_create_exec " + guild_name);
  }
  append_dialog_entry(text, "Back", "@guild_create_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_members_dialog_text(const Player& requester,
                                            const GuildCastleSnapshot& guild_castle_snapshot,
                                            std::size_t requested_page) {
  const auto* guild_state = find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  std::string text = "Guild Members\\";
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_state->members.size());
  const auto page = clamp_dialog_page(requested_page, guild_state->members.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(guild_state->members.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, guild_state->guild_name + " (" + std::to_string(static_cast<int>(page)) +
                               "/" + std::to_string(static_cast<int>(total_pages)) + ")");

  const auto is_lord = equals_ignore_case(guild_state->lord, requester.character().character_name);
  for (std::size_t index = start; index < end; ++index) {
    const auto& member = guild_state->members[index];
    append_dialog_line(text, "Member: " + member);
    if (is_lord && !equals_ignore_case(member, requester.character().character_name)) {
      append_dialog_entry(text, "Manage " + member,
                          "@guild_member " + std::to_string(static_cast<int>(page)) + " " +
                              member);
    }
  }

  append_page_navigation(text, "@guild_members", page, total_pages);
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_directory_dialog_text(const Player& requester,
                                              const GuildCastleSnapshot& guild_castle_snapshot,
                                              std::size_t requested_page) {
  std::string text = "Guild Directory\\";
  if (guild_castle_snapshot.guilds.empty()) {
    append_dialog_line(text, "No guilds are currently registered.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_castle_snapshot.guilds.size());
  const auto page = clamp_dialog_page(requested_page, guild_castle_snapshot.guilds.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end =
      std::min<std::size_t>(guild_castle_snapshot.guilds.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, "Guilds (" + std::to_string(static_cast<int>(page)) + "/" +
                               std::to_string(static_cast<int>(total_pages)) + ")");

  for (std::size_t index = start; index < end; ++index) {
    const auto& guild_state = guild_castle_snapshot.guilds[index];
    append_dialog_line(text, "Guild: " + guild_state.guild_name);
    append_dialog_line(text, "Lord: " + guild_state.lord);
    append_guild_directory_summary(text, guild_castle_snapshot, guild_state);
    append_dialog_entry(text, "View " + guild_state.guild_name,
                        "@guild_browse directory " +
                            std::to_string(static_cast<int>(page)) + " " + guild_state.guild_name);
    if (guild_has_applicant(guild_state, requester.character().character_name)) {
      append_dialog_entry(text, "Pending " + guild_state.guild_name,
                          "@guild_apply_status " + guild_state.guild_name);
    } else {
      append_dialog_entry(text, "Apply " + guild_state.guild_name,
                          "@guild_apply_confirm " + guild_state.guild_name);
    }
  }

  append_page_navigation(text, "@guild_directory", page, total_pages);
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_browse_dialog_text(const Player& requester,
                                           const GuildCastleSnapshot& guild_castle_snapshot,
                                           const GuildBrowseTarget& target) {
  std::string text = "Guild Detail\\";
  const auto* guild_state = find_guild_state(guild_castle_snapshot, target.guild_name);
  const auto back_action =
      build_guild_browse_back_action(target.source, target.page, target.guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
    append_dialog_entry(text, "Back", back_action);
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  append_dialog_line(text, "Guild: " + guild_state->guild_name);
  append_dialog_line(text, "Lord: " + guild_state->lord);
  append_guild_browse_summary(text, guild_castle_snapshot, *guild_state);
  append_dialog_entry(text, "View Members",
                      "@guild_roster " + target.source + " " +
                          std::to_string(static_cast<int>(target.page)) + " 1 " +
                          guild_state->guild_name);
  if (!guild_state->applicants.empty()) {
    append_dialog_entry(text, "View Applicants",
                        "@guild_applicant_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.page)) + " 1 " +
                            guild_state->guild_name);
  }
  if (equals_ignore_case(guild_castle_snapshot.castle_dialog.owner_guild, guild_state->guild_name)) {
    append_dialog_line(text, "Castle: Owner of " +
                                 default_castle_name(guild_castle_snapshot.castle_dialog));
    append_dialog_line(text, "Castle Lord: " + display_castle_lord(guild_castle_snapshot.castle_dialog));
  } else {
    append_dialog_line(text, "Castle: None");
  }

  if (guild_has_applicant(*guild_state, requester.character().character_name)) {
    append_dialog_line(text, "Status: Pending");
    append_dialog_entry(text, "Pending Application",
                        "@guild_apply_status " + guild_state->guild_name);
  } else if (requester.character().guild_name.empty()) {
    append_dialog_line(text, "Status: Ready");
    append_dialog_entry(text, "Apply to Guild",
                        "@guild_apply_confirm " + guild_state->guild_name);
  }

  append_dialog_entry(text, "Back", back_action);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_roster_dialog_text(const GuildCastleSnapshot& guild_castle_snapshot,
                                           const GuildBrowseListTarget& target) {
  std::string text = "Guild Members\\";
  const auto* guild_state = find_guild_state(guild_castle_snapshot, target.guild_name);
  const auto back_action =
      build_guild_browse_list_back_action(target.source, target.browse_page, target.guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
    append_dialog_entry(text, "Back", back_action);
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_state->members.size());
  const auto page = clamp_dialog_page(target.list_page, guild_state->members.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(guild_state->members.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, guild_state->guild_name + " (" + std::to_string(static_cast<int>(page)) +
                               "/" + std::to_string(static_cast<int>(total_pages)) + ")");

  for (std::size_t index = start; index < end; ++index) {
    append_dialog_line(text, "Member: " + guild_state->members[index]);
  }

  if (page > 1) {
    append_dialog_entry(text, "Prev",
                        "@guild_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.browse_page)) + " " +
                            std::to_string(static_cast<int>(page - 1)) + " " +
                            guild_state->guild_name);
  }
  if (page < total_pages) {
    append_dialog_entry(text, "Next",
                        "@guild_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.browse_page)) + " " +
                            std::to_string(static_cast<int>(page + 1)) + " " +
                            guild_state->guild_name);
  }
  append_dialog_entry(text, "Back", back_action);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_applicant_roster_dialog_text(const GuildCastleSnapshot& guild_castle_snapshot,
                                                     const GuildBrowseListTarget& target) {
  std::string text = "Guild Applicants\\";
  const auto* guild_state = find_guild_state(guild_castle_snapshot, target.guild_name);
  const auto back_action =
      build_guild_browse_list_back_action(target.source, target.browse_page, target.guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
    append_dialog_entry(text, "Back", back_action);
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_state->applicants.size());
  const auto page = clamp_dialog_page(target.list_page, guild_state->applicants.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end =
      std::min<std::size_t>(guild_state->applicants.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, guild_state->guild_name + " (" + std::to_string(static_cast<int>(page)) +
                               "/" + std::to_string(static_cast<int>(total_pages)) + ")");

  if (guild_state->applicants.empty()) {
    append_dialog_line(text, "No pending applications.");
  } else {
    for (std::size_t index = start; index < end; ++index) {
      append_dialog_line(text, "Applicant: " + guild_state->applicants[index]);
    }
  }

  if (page > 1) {
    append_dialog_entry(text, "Prev",
                        "@guild_applicant_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.browse_page)) + " " +
                            std::to_string(static_cast<int>(page - 1)) + " " +
                            guild_state->guild_name);
  }
  if (page < total_pages) {
    append_dialog_entry(text, "Next",
                        "@guild_applicant_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.browse_page)) + " " +
                            std::to_string(static_cast<int>(page + 1)) + " " +
                            guild_state->guild_name);
  }
  append_dialog_entry(text, "Back", back_action);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_my_applications_dialog_text(const Player& requester,
                                                    const GuildCastleSnapshot& guild_castle_snapshot,
                                                    std::size_t requested_page) {
  std::string text = "My Guild Applications\\";
  std::vector<const GuildState*> pending_guilds;
  pending_guilds.reserve(guild_castle_snapshot.guilds.size());
  for (const auto& guild_state : guild_castle_snapshot.guilds) {
    if (guild_has_applicant(guild_state, requester.character().character_name)) {
      pending_guilds.push_back(&guild_state);
    }
  }

  const auto total_pages = dialog_total_pages(pending_guilds.size());
  const auto page = clamp_dialog_page(requested_page, pending_guilds.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(pending_guilds.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, "Applications (" + std::to_string(static_cast<int>(page)) + "/" +
                               std::to_string(static_cast<int>(total_pages)) + ")");

  if (pending_guilds.empty()) {
    append_dialog_line(text, "You have no pending applications.");
  } else {
    for (std::size_t index = start; index < end; ++index) {
      const auto* guild_state = pending_guilds[index];
      append_dialog_line(text, "Guild: " + guild_state->guild_name);
      append_dialog_line(text, "Lord: " + guild_state->lord);
      append_guild_directory_summary(text, guild_castle_snapshot, *guild_state);
      append_dialog_entry(text, "View " + guild_state->guild_name,
                          "@guild_browse applications " +
                              std::to_string(static_cast<int>(page)) + " " +
                              guild_state->guild_name);
    }
  }

  append_page_navigation(text, "@guild_my_applications", page, total_pages);
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_member_manage_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildMemberDialogTarget& target) {
  std::string text = "Guild Member\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back",
                        "@guild_members " + std::to_string(static_cast<int>(target.page)));
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto member_name = util::trim(target.member_name);
  const auto is_lord = equals_ignore_case(guild_state->lord, requester.character().character_name);
  append_dialog_line(text, guild_state->guild_name + " / " + member_name);

  if (!is_lord) {
    append_dialog_line(text, "Only the guild lord can manage members.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That guild member is unavailable.");
  } else if (equals_ignore_case(member_name, requester.character().character_name)) {
    append_dialog_line(text, "Choose another member to manage.");
  } else {
    const auto current_title =
        resolve_guild_member_title(requester, objects, *guild_state, member_name);
    append_dialog_line(text, "Title: " + current_title);
    append_dialog_entry(text, "Titles",
                        "@guild_member_titles " + std::to_string(static_cast<int>(target.page)) +
                            " 1 " + member_name);
    append_dialog_entry(text, "Transfer Leadership",
                        "@guild_transfer_confirm " + std::to_string(static_cast<int>(target.page)) +
                            " " + member_name);
    append_dialog_entry(text, "Kick Member",
                        "@guild_kick_confirm " + std::to_string(static_cast<int>(target.page)) +
                            " " + member_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_members " + std::to_string(static_cast<int>(target.page)));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_kick_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildMemberDialogTarget& target) {
  std::string text = "Kick Guild Member\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto member_name = util::trim(target.member_name);
  append_dialog_line(text, requester.character().guild_name + " / " + member_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can kick members.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That character is not a guild member.");
  } else if (equals_ignore_case(member_name, requester.character().character_name)) {
    append_dialog_line(text, "Use guild leave to remove yourself.");
  } else {
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, member_name) != nullptr ? "Online"
                                                                                : "Offline"));
    append_dialog_line(
        text, "Title: " + resolve_guild_member_title(requester, objects, *guild_state, member_name));
    append_dialog_line(text, "Confirm member removal?");
    append_dialog_entry(text, "Confirm",
                        "@guild_kick_exec " + std::to_string(static_cast<int>(target.page)) + " " +
                            member_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_member " + std::to_string(static_cast<int>(target.page)) + " " +
                          member_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_transfer_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildMemberDialogTarget& target) {
  std::string text = "Transfer Leadership\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto member_name = util::trim(target.member_name);
  append_dialog_line(text, requester.character().guild_name + " / " + member_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can transfer leadership.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That character is not a guild member.");
  } else if (equals_ignore_case(member_name, requester.character().character_name)) {
    append_dialog_line(text, "You already lead this guild.");
  } else {
    append_dialog_line(text, "Current Lord: " + guild_state->lord);
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, member_name) != nullptr ? "Online"
                                                                                : "Offline"));
    append_dialog_line(text, "New Lord: " + member_name);
    append_dialog_line(text, "Confirm leadership transfer?");
    append_dialog_entry(text, "Confirm",
                        "@guild_transfer_exec " + std::to_string(static_cast<int>(target.page)) +
                            " " + member_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_member " + std::to_string(static_cast<int>(target.page)) + " " +
                          member_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_member_titles_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildMemberTitleDialogTarget& target) {
  std::string text = "Guild Titles\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back",
                        "@guild_member " + std::to_string(static_cast<int>(target.member_page)) +
                            " " + target.member_name);
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto member_name = util::trim(target.member_name);
  const auto is_lord = equals_ignore_case(guild_state->lord, requester.character().character_name);
  const auto page =
      std::clamp<std::size_t>(target.title_page, 1, kGuildTitlePages.size());
  const auto& title_page = kGuildTitlePages[page - 1];
  append_dialog_line(text, guild_state->guild_name + " / " + member_name);

  if (!is_lord) {
    append_dialog_line(text, "Only the guild lord can manage titles.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That guild member is unavailable.");
  } else if (equals_ignore_case(member_name, requester.character().character_name)) {
    append_dialog_line(text, "Use leadership transfer to change your own role.");
  } else {
    const auto current_title =
        resolve_guild_member_title(requester, objects, *guild_state, member_name);
    append_dialog_line(text, "Roles (" + std::to_string(static_cast<int>(page)) + "/" +
                                 std::to_string(static_cast<int>(kGuildTitlePages.size())) +
                                 ") " + std::string(title_page.label));
    append_dialog_line(text, "Current: " + current_title);
    for (const auto title_template : title_page.titles) {
      std::string label(title_template);
      if (equals_ignore_case(label, current_title)) {
        label += " (Current)";
      }
      append_dialog_entry(text, label,
                          "@guild_title_confirm " +
                              std::to_string(static_cast<int>(target.member_page)) + " " +
                              std::to_string(static_cast<int>(page)) + " " + member_name + " " +
                              std::string(title_template));
    }
    if (page > 1) {
      append_dialog_entry(text, "Prev",
                          "@guild_member_titles " +
                              std::to_string(static_cast<int>(target.member_page)) + " " +
                              std::to_string(static_cast<int>(page - 1)) + " " + member_name);
    }
    if (page < kGuildTitlePages.size()) {
      append_dialog_entry(text, "Next",
                          "@guild_member_titles " +
                              std::to_string(static_cast<int>(target.member_page)) + " " +
                              std::to_string(static_cast<int>(page + 1)) + " " + member_name);
    }
  }

  append_dialog_entry(text, "Back",
                      "@guild_member " + std::to_string(static_cast<int>(target.member_page)) +
                          " " + member_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_applicants_dialog_text(const Player& requester,
                                               const GuildCastleSnapshot& guild_castle_snapshot,
                                               std::size_t requested_page) {
  const auto* guild_state = find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  std::string text = "Guild Applicants\\";
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_state->applicants.size());
  const auto page = clamp_dialog_page(requested_page, guild_state->applicants.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end =
      std::min<std::size_t>(guild_state->applicants.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, guild_state->guild_name + " (" + std::to_string(static_cast<int>(page)) +
                               "/" + std::to_string(static_cast<int>(total_pages)) + ")");

  if (guild_state->applicants.empty()) {
    append_dialog_line(text, "No pending applications.");
  } else {
    for (std::size_t index = start; index < end; ++index) {
      const auto& applicant = guild_state->applicants[index];
      append_dialog_line(text, "Applicant: " + applicant);
      append_dialog_entry(text, "Review " + applicant,
                          "@guild_applicant " + std::to_string(static_cast<int>(page)) + " " +
                              applicant);
    }
  }

  append_page_navigation(text, "@guild_applicants", page, total_pages);
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_applicant_review_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildApplicantDialogTarget& target) {
  std::string text = "Guild Applicant\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back",
                        "@guild_applicants " + std::to_string(static_cast<int>(target.page)));
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto applicant_name = util::trim(target.applicant_name);
  append_dialog_line(text, guild_state->guild_name + " / " + applicant_name);

  if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can review applications.");
  } else if (applicant_name.empty() || !guild_has_applicant(*guild_state, applicant_name)) {
    append_dialog_line(text, "That application is no longer pending.");
  } else {
    append_dialog_line(text, "Application: " + applicant_name);
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, applicant_name) != nullptr ? "Online"
                                                                                  : "Offline"));
    append_dialog_entry(text, "Approve",
                        "@guild_approve_confirm " + std::to_string(static_cast<int>(target.page)) +
                            " " + applicant_name);
    append_dialog_entry(text, "Reject",
                        "@guild_reject_confirm " +
                            std::to_string(static_cast<int>(target.page)) + " " + applicant_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_applicants " + std::to_string(static_cast<int>(target.page)));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_apply_confirm_dialog_text(const Player& requester,
                                                  const GuildCastleSnapshot& guild_castle_snapshot,
                                                  std::string target_guild_name) {
  std::string text = "Guild Application\\";
  target_guild_name = util::trim(std::move(target_guild_name));
  append_dialog_line(text, "Applicant: " + requester.character().character_name);

  if (!requester.character().guild_name.empty()) {
    append_dialog_line(text, "Leave your current guild before joining another.");
  } else if (target_guild_name.empty()) {
    append_dialog_line(text, "Choose a guild first.");
  } else if (const auto* guild_state = find_guild_state(guild_castle_snapshot, target_guild_name);
             guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
  } else {
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Lord: " + guild_state->lord);
    append_dialog_line(text, "Members: " + std::to_string(guild_state->members.size()));
    append_dialog_line(text, "Applicants: " + std::to_string(guild_state->applicants.size()));
    if (guild_has_applicant(*guild_state, requester.character().character_name)) {
      append_dialog_line(text, "Status: Already Pending");
    } else {
      append_dialog_line(text, "Status: Ready");
    }
    append_dialog_line(text, "Confirm guild application?");
    append_dialog_entry(text, "Confirm", "@guild_apply_exec " + guild_state->guild_name);
  }

  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_apply_status_dialog_text(const Player& requester,
                                                 const GuildCastleSnapshot& guild_castle_snapshot,
                                                 std::string target_guild_name) {
  std::string text = "Guild Application Status\\";
  target_guild_name = util::trim(std::move(target_guild_name));
  append_dialog_line(text, "Applicant: " + requester.character().character_name);

  if (target_guild_name.empty()) {
    append_dialog_line(text, "Choose a guild first.");
  } else if (const auto* guild_state = find_guild_state(guild_castle_snapshot, target_guild_name);
             guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
  } else {
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Lord: " + guild_state->lord);
    append_dialog_line(text, "Members: " + std::to_string(guild_state->members.size()));
    if (guild_has_applicant(*guild_state, requester.character().character_name)) {
      append_dialog_line(text, "Status: Pending");
      append_dialog_entry(text, "Withdraw",
                          "@guild_withdraw_confirm " + guild_state->guild_name);
    } else {
      append_dialog_line(text, "Status: No Pending Application");
      append_dialog_entry(text, "Apply",
                          "@guild_apply_confirm " + guild_state->guild_name);
    }
  }

  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_withdraw_confirm_dialog_text(const Player& requester,
                                                     const GuildCastleSnapshot& guild_castle_snapshot,
                                                     std::string target_guild_name) {
  std::string text = "Withdraw Guild Application\\";
  target_guild_name = util::trim(std::move(target_guild_name));
  append_dialog_line(text, "Applicant: " + requester.character().character_name);

  if (target_guild_name.empty()) {
    append_dialog_line(text, "Choose a guild first.");
  } else if (const auto* guild_state = find_guild_state(guild_castle_snapshot, target_guild_name);
             guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
  } else {
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Lord: " + guild_state->lord);
    if (guild_has_applicant(*guild_state, requester.character().character_name)) {
      append_dialog_line(text, "Status: Pending");
      append_dialog_line(text, "Confirm application withdrawal?");
      append_dialog_entry(text, "Confirm",
                          "@guild_withdraw_exec " + guild_state->guild_name);
    } else {
      append_dialog_line(text, "Status: No Pending Application");
    }
  }

  append_dialog_entry(text, "Back", "@guild_apply_status " + target_guild_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_approve_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildApplicantDialogTarget& target) {
  std::string text = "Approve Applicant\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto applicant_name = util::trim(target.applicant_name);
  append_dialog_line(text, requester.character().guild_name + " / " + applicant_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can review applications.");
  } else if (applicant_name.empty() || !guild_has_applicant(*guild_state, applicant_name)) {
    append_dialog_line(text, "That application is no longer pending.");
  } else {
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, applicant_name) != nullptr ? "Online"
                                                                                   : "Offline"));
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Role: Member");
    append_dialog_line(text, "Confirm guild approval?");
    append_dialog_entry(text, "Confirm",
                        "@guild_approve_exec " + std::to_string(static_cast<int>(target.page)) +
                            " " + applicant_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_applicant " + std::to_string(static_cast<int>(target.page)) + " " +
                          applicant_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_reject_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildApplicantDialogTarget& target) {
  std::string text = "Reject Applicant\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto applicant_name = util::trim(target.applicant_name);
  append_dialog_line(text, requester.character().guild_name + " / " + applicant_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can review applications.");
  } else if (applicant_name.empty() || !guild_has_applicant(*guild_state, applicant_name)) {
    append_dialog_line(text, "That application is no longer pending.");
  } else {
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, applicant_name) != nullptr ? "Online"
                                                                                   : "Offline"));
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Confirm guild rejection?");
    append_dialog_entry(text, "Confirm",
                        "@guild_reject_exec " + std::to_string(static_cast<int>(target.page)) +
                            " " + applicant_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_applicant " + std::to_string(static_cast<int>(target.page)) + " " +
                          applicant_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_title_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildTitleConfirmTarget& target) {
  std::string text = "Set Guild Title\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto member_name = util::trim(target.member_name);
  const auto title_name = util::trim(target.title_name);
  append_dialog_line(text, requester.character().guild_name + " / " + member_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can manage titles.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That guild member is unavailable.");
  } else if (equals_ignore_case(member_name, guild_state->lord)) {
    append_dialog_line(text, "Use leadership transfer to change the guild lord.");
  } else if (title_name.empty()) {
    append_dialog_line(text, "Choose a guild title first.");
  } else {
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, member_name) != nullptr ? "Online"
                                                                                : "Offline"));
    append_dialog_line(
        text, "Current: " + resolve_guild_member_title(requester, objects, *guild_state, member_name));
    append_dialog_line(text, "New Title: " + title_name);
    append_dialog_line(text, "Confirm guild title change?");
    append_dialog_entry(text, "Confirm",
                        "@guild_title_exec " +
                            std::to_string(static_cast<int>(target.member_page)) + " " +
                            std::to_string(static_cast<int>(target.title_page)) + " " +
                            member_name + " " + title_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_member_titles " +
                          std::to_string(static_cast<int>(target.member_page)) + " " +
                          std::to_string(static_cast<int>(target.title_page)) + " " + member_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_guild_leave_confirm_dialog_text(const Player& requester,
                                                  const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Leave Guild\\";
  const auto& character = requester.character();
  append_dialog_line(text, "Character: " + character.character_name);

  if (character.guild_name.empty()) {
    append_dialog_line(text, "You are not in a guild.");
  } else {
    append_dialog_line(text, "Guild: " + character.guild_name);
    append_dialog_line(text, "Role: " + character.guild_title);
    if (const auto* guild_state = find_guild_state(guild_castle_snapshot, character.guild_name);
        guild_state != nullptr &&
        equals_ignore_case(guild_state->lord, character.character_name)) {
      if (guild_state->members.size() <= 1) {
        append_dialog_line(text, "Leaving will disband the guild.");
      } else {
        std::string next_lord;
        for (const auto& member_name : guild_state->members) {
          if (!equals_ignore_case(member_name, character.character_name)) {
            next_lord = member_name;
            break;
          }
        }
        append_dialog_line(text, "Leaving will transfer leadership.");
        if (!next_lord.empty()) {
          append_dialog_line(text, "Next Lord: " + next_lord);
        }
      }
    }
    append_dialog_line(text, "Confirm leaving the guild?");
    append_dialog_entry(text, "Confirm", "@guild_leave_exec");
  }

  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_castle_show_dialog_text(const Player& requester,
                                          const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Castle Info\\";
  const auto& castle = guild_castle_snapshot.castle_dialog;
  const auto wars = parse_castle_war_list(castle);
  append_dialog_line(text, "Castle: " + default_castle_name(castle));
  append_dialog_line(text, "Owner Guild: " + display_castle_owner(castle));
  append_dialog_line(text, "Owner Lord: " + display_castle_lord(castle));
  append_dialog_line(text, "Owner Role: " + describe_castle_owner_role(castle));
  append_dialog_line(text, "War Date: " + default_castle_war_date(castle));
  append_dialog_line(text, "War Count: " + std::to_string(wars.size()));
  append_dialog_line(text, "War Preview: " + summarize_castle_wars(castle));
  append_dialog_line(text, "Fees: " + std::to_string(castle.guild_war_fee) + "/" +
                               std::to_string(castle.upgrade_weapon_fee));
  if (const auto* owner_guild = find_guild_state(guild_castle_snapshot, castle.owner_guild);
      owner_guild != nullptr) {
    append_dialog_entry(text, "View Owner Guild",
                        "@guild_browse castle_show 1 " + owner_guild->guild_name);
    append_dialog_entry(text, "View Owner Members",
                        "@guild_roster castle_show 1 1 " + owner_guild->guild_name);
    if (!owner_guild->applicants.empty()) {
      append_dialog_entry(text, "View Owner Applicants",
                          "@guild_applicant_roster castle_show 1 1 " +
                              owner_guild->guild_name);
    }
  }
  append_dialog_entry(text, "Active Wars", "@castle_wars");

  if (!requester.character().guild_name.empty()) {
    const auto* own_guild =
        find_guild_state(guild_castle_snapshot, requester.character().guild_name);
    if (own_guild != nullptr &&
        equals_ignore_case(own_guild->lord, requester.character().character_name)) {
      append_dialog_entry(text, "Claim Castle", "@castle_claim_confirm");
      append_dialog_entry(text, "Declare War", "@castle_war_targets");
    }
  }

  append_dialog_entry(text, "Back", "@castle_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

CastleActionResult execute_castle_claim(Player& speaker, GuildCastleSnapshot& guild_castle_snapshot,
                                        RuntimeDispatch& dispatch) {
  CastleActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;

  if (speaker.character().guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.claim_require_guild_template,
                                                 "Join a guild before claiming the castle.");
    result.details.push_back("Requirement: Guild lord with a registered guild.");
    return result;
  }

  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.claim_missing_guild_template,
                                    "Guild data is unavailable. Try again in a moment."),
        castle_dialog_context, speaker.character().guild_name);
    result.details.push_back("Guild snapshot is missing for " + speaker.character().guild_name + ".");
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.claim_only_lord_template,
                                    "Only the guild lord can claim the castle."),
        castle_dialog_context, guild_state->guild_name);
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }

  const auto previous_owner = castle_dialog_context.owner_guild;
  const auto previous_role =
      describe_castle_guild_role(castle_dialog_context, guild_state->guild_name);
  castle_dialog_context.owner_guild = guild_state->guild_name;
  castle_dialog_context.lord = guild_state->lord;
  queue_save_castle_state(dispatch, castle_dialog_context);

  result.success = true;
  result.summary = render_castle_summary_template(
      default_castle_claim_summary_template(castle_dialog_context), castle_dialog_context,
      guild_state->guild_name);
  result.details.push_back("Castle: " + default_castle_name(castle_dialog_context));
  result.details.push_back("Previous Owner: " +
                           (previous_owner.empty()
                                ? default_unclaimed_castle_owner(castle_dialog_context)
                                : previous_owner));
  result.details.push_back("New Owner: " + display_castle_owner(castle_dialog_context));
  result.details.push_back("Lord: " + display_castle_lord(castle_dialog_context));
  result.details.push_back("Owner Guild: " + display_castle_owner(castle_dialog_context));
  result.details.push_back("Owner Lord: " + display_castle_lord(castle_dialog_context));
  result.details.push_back("War Count: " +
                           std::to_string(parse_castle_war_list(castle_dialog_context).size()));
  result.details.push_back("War Preview: " + summarize_castle_wars(castle_dialog_context));
  result.details.push_back("Guild Role Change: " + previous_role + " -> " +
                           default_castle_role_change_owner_label(castle_dialog_context));
  return result;
}

CastleActionResult execute_castle_war(Player& speaker, GuildCastleSnapshot& guild_castle_snapshot,
                                      RuntimeDispatch& dispatch, std::string target_guild_name) {
  CastleActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;

  if (speaker.character().guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.war_require_guild_template,
                                                 "Join a guild before declaring war.");
    result.details.push_back("Requirement: Guild lord with enough gold.");
    return result;
  }

  const auto* own_guild =
      find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (own_guild == nullptr) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_missing_guild_template,
                                    "Guild data is unavailable. Try again in a moment."),
        castle_dialog_context, speaker.character().guild_name);
    result.details.push_back("Guild snapshot is missing for " + speaker.character().guild_name + ".");
    return result;
  }
  if (!equals_ignore_case(own_guild->lord, speaker.character().character_name)) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_only_lord_template,
                                    "Only the guild lord can declare war."),
        castle_dialog_context, own_guild->guild_name);
    result.details.push_back("Guild lord: " + own_guild->lord);
    return result;
  }

  target_guild_name = util::trim(std::move(target_guild_name));
  if (target_guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.war_usage_template,
                                                 "Usage: @castle war <guild_name>");
    result.details.push_back("Choose a rival guild from the war target list.");
    return result;
  }
  if (equals_ignore_case(target_guild_name, own_guild->guild_name)) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_self_target_template,
                                    "Your guild cannot declare war on itself."),
        castle_dialog_context, own_guild->guild_name, target_guild_name);
    result.details.push_back("Your Guild: " + own_guild->guild_name);
    return result;
  }
  if (find_guild_state(guild_castle_snapshot, target_guild_name) == nullptr) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_target_missing_template,
                                    "Target guild not found."),
        castle_dialog_context, own_guild->guild_name, target_guild_name);
    result.details.push_back("Target Guild: " + target_guild_name);
    return result;
  }

  auto wars = parse_castle_war_list(castle_dialog_context);
  const auto already_listed =
      std::any_of(wars.begin(), wars.end(), [&](const std::string& guild_name) {
        return equals_ignore_case(guild_name, target_guild_name);
      });
  if (already_listed) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_already_registered_template,
                                    "Castle war against <$TARGETGUILD> is already registered."),
        castle_dialog_context, own_guild->guild_name, target_guild_name);
    result.details.push_back("Wars: " + display_castle_wars(castle_dialog_context));
    return result;
  }
  if (!speaker.can_spend_gold(castle_dialog_context.guild_war_fee)) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_need_gold_template,
                                    "You need <$GOLD> gold to declare war."),
        castle_dialog_context, own_guild->guild_name, target_guild_name,
        castle_dialog_context.guild_war_fee);
    result.details.push_back("Gold: " + std::to_string(speaker.character().gold));
    return result;
  }

  speaker.spend_gold(castle_dialog_context.guild_war_fee);
  queue_save_character(dispatch, speaker);
  wars.push_back(target_guild_name);
  castle_dialog_context.list_of_war = join_tokens(wars, 0, ", ");
  queue_save_castle_state(dispatch, castle_dialog_context);

  result.success = true;
  result.summary = render_castle_summary_template(
      default_castle_war_summary_template(castle_dialog_context), castle_dialog_context,
      own_guild->guild_name, target_guild_name, castle_dialog_context.guild_war_fee);
  result.details.push_back("Castle: " + default_castle_name(castle_dialog_context));
  result.details.push_back("Target Guild: " + target_guild_name);
  result.details.push_back("War Fee: " + std::to_string(castle_dialog_context.guild_war_fee));
  result.details.push_back("Gold: " + std::to_string(speaker.character().gold));
  result.details.push_back("Wars: " + display_castle_wars(castle_dialog_context));
  result.details.push_back("Owner Guild: " + display_castle_owner(castle_dialog_context));
  result.details.push_back("Owner Lord: " + display_castle_lord(castle_dialog_context));
  result.details.push_back("War Count: " + std::to_string(wars.size()));
  result.details.push_back("War Preview: " + summarize_castle_wars(castle_dialog_context));
  result.details.push_back("Guild Role Change: " + own_guild->guild_name + " -> " +
                           default_castle_role_change_challenger_label(castle_dialog_context));
  return result;
}

std::string build_castle_action_result_dialog_text(std::string title,
                                                   const CastleActionResult& result,
                                                   std::string back_action) {
  const auto find_detail = [&](std::string_view prefix) -> std::string {
    for (const auto& line : result.details) {
      if (line.rfind(prefix, 0) == 0) {
        return line.substr(prefix.size());
      }
    }
    return {};
  };

  std::string text = std::move(title);
  text.push_back('\\');
  append_dialog_line(text, result.success ? "Result: Success" : "Result: Failed");
  append_dialog_line(text, "Summary: " + result.summary);
  const auto castle_name = find_detail("Castle: ");
  if (!castle_name.empty()) {
    append_dialog_line(text, "Castle Snapshot: " + castle_name);
  }
  const auto owner_guild = find_detail("Owner Guild: ");
  const auto owner_lord = find_detail("Owner Lord: ");
  if (!owner_guild.empty() || !owner_lord.empty()) {
    append_dialog_line(text,
                       "Owner Snapshot: " +
                           (owner_guild.empty() ? std::string("-") : owner_guild) + " / " +
                           (owner_lord.empty() ? std::string("-") : owner_lord));
  }
  const auto target_guild = find_detail("Target Guild: ");
  if (!target_guild.empty()) {
    append_dialog_line(text, "Target Snapshot: " + target_guild);
  }
  const auto war_count = find_detail("War Count: ");
  const auto war_preview = find_detail("War Preview: ");
  if (!war_count.empty() || !war_preview.empty()) {
    append_dialog_line(text, "War Snapshot: " +
                                 (war_count.empty() ? std::string("-") : war_count) + " / " +
                                 (war_preview.empty() ? std::string("None") : war_preview));
  }
  const auto role_change = find_detail("Guild Role Change: ");
  if (!role_change.empty()) {
    append_dialog_line(text, "Role Change: " + role_change);
  }
  for (const auto& line : result.details) {
    append_dialog_line(text, line);
  }
  append_dialog_entry(text, "Castle", "@castle_show");
  append_dialog_entry(text, "Back", std::move(back_action));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

GuildActionResult execute_guild_apply_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string guild_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  guild_name = util::trim(std::move(guild_name));

  if (!speaker.character().guild_name.empty()) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_apply_leave_current_template,
                                    "Leave your current guild before joining another."),
        speaker.character().guild_name);
    result.details.push_back("Current Guild: " + speaker.character().guild_name);
    return result;
  }
  if (guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.guild_apply_choose_guild_template,
                                                 "Choose a guild first.");
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
  if (guild_state == nullptr) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_not_found_template,
                                    "Guild not found."),
        guild_name);
    result.details.push_back("Requested Guild: " + guild_name);
    return result;
  }
  if (guild_has_applicant(*guild_state, speaker.character().character_name)) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_apply_already_pending_template,
                                    "Your application to <$GUILD> is already pending."),
        guild_state->guild_name);
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Lord: " + guild_state->lord);
    return result;
  }

  add_guild_applicant(*guild_state, speaker.character().character_name);
  queue_save_guild_state(dispatch, *guild_state);
  if (auto* guild_lord = find_online_player_by_name(objects, guild_state->lord);
      guild_lord != nullptr) {
    queue_system_notice(dispatch, *guild_lord,
                        render_guild_notice_template(
                            configured_summary_template(
                                castle_dialog_context.guild_apply_alert_template,
                                "<$TARGET> applied to join <$GUILD>."),
                            guild_state->guild_name, speaker.character().character_name));
  }

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_apply_summary_template,
                                  "Application sent to guild <$GUILD>."),
      guild_state->guild_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Lord: " + guild_state->lord);
  result.details.push_back("Applicants Pending: " +
                           std::to_string(guild_state->applicants.size()));
  return result;
}

GuildActionResult execute_guild_create_action(
    Player& speaker, GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch,
    std::string guild_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  guild_name = util::trim(std::move(guild_name));
  const auto guild_create_fee = castle_dialog_context.guild_create_fee;

  if (!speaker.character().guild_name.empty()) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_create_leave_current_template,
                                    "Leave your current guild before creating a new one."),
        speaker.character().guild_name);
    result.details.push_back("Current Guild: " + speaker.character().guild_name);
    return result;
  }
  if (guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.guild_create_choose_name_template,
                                                 "Choose a guild name first.");
    return result;
  }
  if (find_guild_state(guild_castle_snapshot, guild_name) != nullptr) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_create_name_unavailable_template,
                                    "That guild already exists."),
        guild_name);
    result.details.push_back("Guild: " + guild_name);
    result.details.push_back("Status: Name Unavailable");
    return result;
  }
  if (guild_create_fee > 0 && !speaker.can_spend_gold(guild_create_fee)) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_create_need_gold_template,
                                    "You need <$GOLD> gold to found a guild."),
        guild_name, {}, {}, {}, guild_create_fee);
    result.details.push_back("Guild: " + guild_name);
    result.details.push_back("Status: Need Gold");
    result.details.push_back("Creation Fee: " + std::to_string(guild_create_fee));
    result.details.push_back("Gold: " + std::to_string(speaker.character().gold));
    return result;
  }

  GuildState guild_state;
  guild_state.guild_name = guild_name;
  guild_state.lord = speaker.character().character_name;
  guild_state.members.push_back(speaker.character().character_name);
  guild_castle_snapshot.guilds.push_back(guild_state);
  if (guild_create_fee > 0) {
    speaker.spend_gold(guild_create_fee);
  }
  speaker.set_guild_membership(guild_name, "Lord");
  if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
    castle_dialog_context.lord = guild_state.lord;
  }

  queue_save_guild_state(dispatch, guild_state);
  queue_save_character(dispatch, speaker);

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_create_summary_template,
                                  "Guild <$GUILD> created."),
      guild_name);
  result.details.push_back("Guild: " + guild_name);
  result.details.push_back("Status: Founded");
  result.details.push_back("Lord: " + guild_state.lord);
  result.details.push_back("Role: Lord");
  result.details.push_back("Creation Fee: " + std::to_string(guild_create_fee));
  result.details.push_back("Gold: " + std::to_string(speaker.character().gold));
  result.details.push_back("Members Remaining: 1");
  result.details.push_back("Applicants Remaining: 0");
  return result;
}

GuildActionResult execute_guild_withdraw_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string guild_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  guild_name = util::trim(std::move(guild_name));

  if (guild_name.empty()) {
    result.summary = "Choose a guild first.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild not found.";
    result.details.push_back("Requested Guild: " + guild_name);
    return result;
  }
  if (!guild_has_applicant(*guild_state, speaker.character().character_name)) {
    result.summary = "No pending application for guild " + guild_state->guild_name + ".";
    result.details.push_back("Applicant: " + speaker.character().character_name);
    return result;
  }

  remove_guild_applicant(*guild_state, speaker.character().character_name);
  queue_save_guild_state(dispatch, *guild_state);
  if (auto* guild_lord = find_online_player_by_name(objects, guild_state->lord);
      guild_lord != nullptr) {
    queue_system_notice(dispatch, *guild_lord,
                        render_guild_notice_template(
                            configured_summary_template(
                                castle_dialog_context.guild_withdraw_alert_template,
                                "<$TARGET> withdrew the application to <$GUILD>."),
                            guild_state->guild_name, speaker.character().character_name));
  }

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_withdraw_summary_template,
                                  "Withdrew application from guild <$GUILD>."),
      guild_state->guild_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Lord: " + guild_state->lord);
  result.details.push_back("Applicants Pending: " +
                           std::to_string(guild_state->applicants.size()));
  return result;
}

GuildActionResult execute_guild_approve_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch,
    std::string applicant_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  applicant_name = util::trim(std::move(applicant_name));

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can manage applications.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (!guild_has_applicant(*guild_state, applicant_name)) {
    result.summary = "That character has no pending application.";
    return result;
  }

  auto* applicant = find_online_player_by_name(objects, applicant_name);
  if (applicant == nullptr) {
    queue_load_offline_guild_character(
        dispatch, OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::approve, speaker.map_id(),
                                          speaker.id(), guild_state->guild_name, applicant_name, {}});
    result.status = "Pending";
    result.summary = "Queued guild approval for offline applicant " + applicant_name + ".";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Status: Offline");
    return result;
  }

  if (!applicant->character().guild_name.empty()) {
    remove_guild_applicant(*guild_state, applicant_name);
    queue_save_guild_state(dispatch, *guild_state);
    result.status = "Success";
    result.summary = applicant_name + " is already in another guild. Application cleared.";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Status: Online");
    return result;
  }

  remove_guild_applicant(*guild_state, applicant_name);
  add_guild_member(*guild_state, applicant->character().character_name);
  applicant->set_guild_membership(guild_state->guild_name, "Member");
  queue_save_guild_state(dispatch, *guild_state);
  queue_save_character(dispatch, *applicant);
  queue_system_notice(dispatch, *applicant,
                      render_guild_notice_template(
                          configured_summary_template(
                              castle_dialog_context.guild_approved_notice_template,
                              "Your application to <$GUILD> was approved."),
                          guild_state->guild_name));

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_approve_summary_template,
                                  "Approved guild application for <$TARGET>."),
      guild_state->guild_name, applicant_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Status: Online");
  result.details.push_back("Role: Member");
  result.details.push_back("Applicants Remaining: " +
                           std::to_string(guild_state->applicants.size()));
  return result;
}

GuildActionResult execute_guild_reject_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch,
    std::string applicant_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  applicant_name = util::trim(std::move(applicant_name));

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can manage applications.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (!guild_has_applicant(*guild_state, applicant_name)) {
    result.summary = "That character has no pending application.";
    return result;
  }

  const auto applicant_online = find_online_player_by_name(objects, applicant_name) != nullptr;
  remove_guild_applicant(*guild_state, applicant_name);
  queue_save_guild_state(dispatch, *guild_state);
  if (auto* applicant = find_online_player_by_name(objects, applicant_name); applicant != nullptr) {
    queue_system_notice(dispatch, *applicant,
                        render_guild_notice_template(
                            configured_summary_template(
                                castle_dialog_context.guild_rejected_notice_template,
                                "Your application to <$GUILD> was rejected."),
                            guild_state->guild_name));
  }

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_reject_summary_template,
                                  "Rejected guild application for <$TARGET>."),
      guild_state->guild_name, applicant_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back(std::string("Status: ") + (applicant_online ? "Online" : "Offline"));
  result.details.push_back("Applicants Remaining: " +
                           std::to_string(guild_state->applicants.size()));
  return result;
}

GuildActionResult execute_guild_kick_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string member_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  member_name = util::trim(std::move(member_name));

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can kick members.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (member_name.empty()) {
    result.summary = "Usage: @guild kick <member_name>";
    return result;
  }
  if (equals_ignore_case(member_name, speaker.character().character_name)) {
    result.summary = "Use @guild leave to remove yourself from the guild.";
    return result;
  }
  if (!guild_has_member(*guild_state, member_name)) {
    result.summary = "That character is not a guild member.";
    return result;
  }

  auto* member = find_online_player_by_name(objects, member_name);
  if (member == nullptr ||
      !equals_ignore_case(member->character().guild_name, guild_state->guild_name)) {
    queue_load_offline_guild_character(
        dispatch, OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::kick, speaker.map_id(),
                                          speaker.id(), guild_state->guild_name, member_name, {}});
    result.status = "Pending";
    result.summary = "Queued member removal for offline guild member " + member_name + ".";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Status: Offline");
    return result;
  }

  remove_guild_member(*guild_state, member_name);
  member->clear_guild_membership();
  queue_save_guild_state(dispatch, *guild_state);
  queue_save_character(dispatch, *member);
  queue_system_notice(dispatch, *member,
                      render_guild_notice_template(
                          configured_summary_template(
                              castle_dialog_context.guild_removed_notice_template,
                              "You were removed from guild <$GUILD>."),
                          guild_state->guild_name));

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_kick_summary_template,
                                  "Kicked guild member <$TARGET>."),
      guild_state->guild_name, member_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Status: Online");
  result.details.push_back("Members Remaining: " +
                           std::to_string(guild_state->members.size()));
  return result;
}

GuildActionResult execute_guild_title_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string target_name,
    std::string title_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  target_name = util::trim(std::move(target_name));
  title_name = util::trim(std::move(title_name));

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  const auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can change member titles.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (title_name.empty()) {
    result.summary = "Usage: @guild title <member_name> <title>";
    return result;
  }
  if (!guild_has_member(*guild_state, target_name)) {
    result.summary = "That character is not a guild member.";
    return result;
  }
  if (equals_ignore_case(target_name, guild_state->lord)) {
    result.summary = "Use @guild transfer to change the guild lord.";
    return result;
  }

  auto* target = find_online_player_by_name(objects, target_name);
  if (target == nullptr ||
      !equals_ignore_case(target->character().guild_name, guild_state->guild_name)) {
    queue_load_offline_guild_character(
        dispatch, OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::title, speaker.map_id(),
                                          speaker.id(), guild_state->guild_name, target_name,
                                          title_name});
    result.status = "Pending";
    result.summary = "Queued guild title change for offline member " + target_name + ".";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Status: Offline");
    result.details.push_back("New Title: " + title_name);
    return result;
  }

  target->set_guild_membership(guild_state->guild_name, title_name);
  queue_save_character(dispatch, *target);
  queue_system_notice(dispatch, *target,
                      render_guild_notice_template(
                          configured_summary_template(
                              castle_dialog_context.guild_title_changed_notice_template,
                              "Your guild title is now <$TITLE>."),
                          guild_state->guild_name, {}, title_name));

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_title_summary_template,
                                  "Set guild title for <$TARGET> to <$TITLE>."),
      guild_state->guild_name, target->character().character_name, title_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Status: Online");
  result.details.push_back("New Title: " + title_name);
  return result;
}

GuildActionResult execute_guild_transfer_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string target_name) {
  GuildActionResult result;
  result.handled = true;
  target_name = util::trim(std::move(target_name));
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can transfer leadership.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (target_name.empty()) {
    result.summary = "Usage: @guild transfer <member_name>";
    return result;
  }
  if (equals_ignore_case(target_name, speaker.character().character_name) ||
      equals_ignore_case(target_name, guild_state->lord)) {
    result.summary = "You already lead this guild.";
    return result;
  }
  if (!guild_has_member(*guild_state, target_name)) {
    result.summary = "That character is not a guild member.";
    return result;
  }

  auto* target = find_online_player_by_name(objects, target_name);
  if (target == nullptr ||
      !equals_ignore_case(target->character().guild_name, guild_state->guild_name)) {
    queue_load_offline_guild_character(
        dispatch, OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::transfer, speaker.map_id(),
                                          speaker.id(), guild_state->guild_name, target_name, {}});
    result.status = "Pending";
    result.summary = "Queued leadership transfer to offline member " + target_name + ".";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Current Lord: " + guild_state->lord);
    result.details.push_back("Status: Offline");
    return result;
  }

  const auto previous_lord = guild_state->lord;
  guild_state->lord = target->character().character_name;
  speaker.set_guild_membership(guild_state->guild_name, "Member");
  target->set_guild_membership(guild_state->guild_name, "Lord");
  queue_save_guild_state(dispatch, *guild_state);
  queue_save_character(dispatch, speaker);
  queue_save_character(dispatch, *target);
  if (equals_ignore_case(castle_dialog_context.owner_guild, guild_state->guild_name)) {
    castle_dialog_context.lord = guild_state->lord;
    queue_save_castle_state(dispatch, castle_dialog_context);
  }
  queue_system_notice(dispatch, *target,
                      render_guild_notice_template(
                          configured_summary_template(
                              castle_dialog_context.guild_new_lord_notice_template,
                              "You are now the guild lord of <$GUILD>."),
                          guild_state->guild_name));

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_transfer_summary_template,
                                  "Transferred guild leadership to <$TARGET>."),
      guild_state->guild_name, target_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Previous Lord: " + previous_lord);
  result.details.push_back("New Lord: " + guild_state->lord);
  result.details.push_back("Status: Online");
  return result;
}

GuildActionResult execute_guild_leave_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch) {
  GuildActionResult result;
  result.handled = true;

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  const auto guild_name = speaker.character().guild_name;
  const auto character_name = speaker.character().character_name;
  auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
  const auto was_lord = guild_state != nullptr && equals_ignore_case(guild_state->lord, character_name);

  speaker.clear_guild_membership();
  queue_save_character(dispatch, speaker);

  if (guild_state == nullptr) {
    result.status = "Success";
    result.summary = configured_summary_template(
        castle_dialog_context.guild_membership_cleared_summary_template,
        "Guild membership cleared.");
    result.details.push_back("Former Guild: " + guild_name);
    return result;
  }

  remove_guild_member(*guild_state, character_name);
  if (guild_state->members.empty()) {
    guild_castle_snapshot.guilds.erase(
        std::remove_if(guild_castle_snapshot.guilds.begin(), guild_castle_snapshot.guilds.end(),
                       [&](const GuildState& entry) {
                         return equals_ignore_case(entry.guild_name, guild_name);
                       }),
        guild_castle_snapshot.guilds.end());
    queue_delete_guild(dispatch, guild_name);
    if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
      castle_dialog_context.owner_guild.clear();
      castle_dialog_context.lord.clear();
      castle_dialog_context.list_of_war.clear();
      queue_save_castle_state(dispatch, castle_dialog_context);
    }

    result.status = "Success";
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_disband_summary_template,
                                    "Guild <$GUILD> has been disbanded."),
        guild_name);
    result.details.push_back("Former Guild: " + guild_name);
    result.details.push_back("Members Remaining: 0");
    return result;
  }

  if (was_lord) {
    guild_state->lord = guild_state->members.front();
    if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
      castle_dialog_context.lord = guild_state->lord;
      queue_save_castle_state(dispatch, castle_dialog_context);
    }
      if (auto* next_lord = find_online_player_by_name(objects, guild_state->lord);
          next_lord != nullptr && equals_ignore_case(next_lord->character().guild_name, guild_name)) {
        next_lord->set_guild_membership(guild_name, "Lord");
        queue_save_character(dispatch, *next_lord);
        queue_system_notice(dispatch, *next_lord,
                            render_guild_notice_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_new_lord_notice_template,
                                    "You are now the guild lord of <$GUILD>."),
                                guild_name));
      }
    }

  queue_save_guild_state(dispatch, *guild_state);
  result.status = "Success";
  if (was_lord) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_leave_transfer_summary_template,
                                    "You left <$GUILD>. New lord: <$NEWLORD>."),
        guild_name, {}, {}, guild_state->lord);
    result.details.push_back("New Lord: " + guild_state->lord);
  } else {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_leave_summary_template,
                                    "You left <$GUILD>."),
        guild_name);
  }
  result.details.push_back("Former Guild: " + guild_name);
  result.details.push_back("Members Remaining: " +
                           std::to_string(guild_state->members.size()));
  return result;
}

std::string build_guild_action_result_dialog_text(std::string title,
                                                  const GuildActionResult& result,
                                                  std::string back_action,
                                                  std::string guild_action = "@guild_info") {
  const auto find_detail = [&](std::string_view prefix) -> std::string {
    for (const auto& line : result.details) {
      if (line.rfind(prefix, 0) == 0) {
        return line.substr(prefix.size());
      }
    }
    return {};
  };

  std::string text = std::move(title);
  text.push_back('\\');
  append_dialog_line(text, "Result: " + result.status);
  append_dialog_line(text, "Summary: " + result.summary);
  const auto guild_name = [&]() -> std::string {
    for (const auto prefix : {std::string_view{"Guild: "}, std::string_view{"Former Guild: "},
                              std::string_view{"Current Guild: "}}) {
      const auto value = find_detail(prefix);
      if (!value.empty()) {
        return value;
      }
    }
    return {};
  }();
  if (!guild_name.empty()) {
    append_dialog_line(text, "Guild Snapshot: " + guild_name);
  }
  const auto status = find_detail("Status: ");
  if (!status.empty()) {
    append_dialog_line(text, "Guild Status: " + status);
  }
  const auto members_remaining = find_detail("Members Remaining: ");
  const auto applicants_remaining = find_detail("Applicants Remaining: ");
  if (!members_remaining.empty() || !applicants_remaining.empty()) {
    append_dialog_line(text,
                       "Counts: " + (members_remaining.empty() ? std::string("-")
                                                               : members_remaining) +
                           "/" +
                           (applicants_remaining.empty() ? std::string("-")
                                                         : applicants_remaining));
  }
  const auto previous_lord = find_detail("Previous Lord: ");
  const auto new_lord = find_detail("New Lord: ");
  if (!previous_lord.empty() || !new_lord.empty()) {
    append_dialog_line(text, "Leadership: " +
                                 (previous_lord.empty() ? std::string("-") : previous_lord) +
                                 " -> " +
                                 (new_lord.empty() ? std::string("-") : new_lord));
  }
  const auto new_title = find_detail("New Title: ");
  if (!new_title.empty()) {
    append_dialog_line(text, "Title Update: " + new_title);
  }
  const auto creation_fee = find_detail("Creation Fee: ");
  const auto gold = find_detail("Gold: ");
  if (!creation_fee.empty() || !gold.empty()) {
    append_dialog_line(text, "Treasury: " +
                                 (creation_fee.empty() ? std::string("-") : creation_fee) +
                                 " / " +
                                 (gold.empty() ? std::string("-") : gold));
  }
  for (const auto& line : result.details) {
    append_dialog_line(text, line);
  }
  append_dialog_entry(text, "Guild", std::move(guild_action));
  append_dialog_entry(text, "Back", std::move(back_action));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_castle_service_dialog_text(const Player& requester,
                                             const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Castle Office\\";
  append_dialog_entry(text, "Show Castle", "@castle_show");
  append_dialog_entry(text, "Active Wars", "@castle_wars");

  const auto& character = requester.character();
  if (!character.guild_name.empty()) {
    const auto* own_guild = find_guild_state(guild_castle_snapshot, character.guild_name);
    if (own_guild != nullptr &&
        equals_ignore_case(own_guild->lord, character.character_name)) {
      append_dialog_entry(text, "Claim Castle", "@castle_claim_confirm");
      append_dialog_entry(text, "Declare War", "@castle_war_targets");
    }
  }

  append_dialog_entry(text, "Back", "@main");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_castle_wars_dialog_text(const GuildCastleSnapshot& guild_castle_snapshot,
                                          std::size_t requested_page) {
  std::string text = "Castle Wars\\";
  const auto wars = parse_castle_war_list(guild_castle_snapshot.castle_dialog);
  const auto total_pages = dialog_total_pages(wars.size());
  const auto page = clamp_dialog_page(requested_page, wars.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(wars.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, default_castle_name(guild_castle_snapshot.castle_dialog) + " (" +
                               std::to_string(static_cast<int>(page)) + "/" +
                               std::to_string(static_cast<int>(total_pages)) + ")");

  if (wars.empty()) {
    append_dialog_line(text, no_active_wars_text(guild_castle_snapshot.castle_dialog));
  } else {
    for (std::size_t index = start; index < end; ++index) {
      append_dialog_line(text, "War: " + wars[index]);
      append_castle_guild_list_summary(text, guild_castle_snapshot, wars[index]);
      append_dialog_entry(text, "View " + wars[index],
                          "@castle_guild_browse wars " +
                              std::to_string(static_cast<int>(page)) + " " + wars[index]);
    }
  }

  append_page_navigation(text, "@castle_wars", page, total_pages);
  append_dialog_entry(text, "Back", "@castle_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_castle_war_targets_dialog_text(const Player& requester,
                                                 const GuildCastleSnapshot& guild_castle_snapshot,
                                                 std::size_t requested_page) {
  std::string text = "Declare Castle War\\";
  std::vector<std::string> targets;
  for (const auto& guild_state : guild_castle_snapshot.guilds) {
    if (!equals_ignore_case(guild_state.guild_name, requester.character().guild_name)) {
      targets.push_back(guild_state.guild_name);
    }
  }

  const auto total_pages = dialog_total_pages(targets.size());
  const auto page = clamp_dialog_page(requested_page, targets.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(targets.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, default_castle_name(guild_castle_snapshot.castle_dialog) + " (" +
                               std::to_string(static_cast<int>(page)) + "/" +
                               std::to_string(static_cast<int>(total_pages)) + ")");

  if (targets.empty()) {
    append_dialog_line(text, "No rival guilds are available.");
  } else {
    for (std::size_t index = start; index < end; ++index) {
      append_dialog_line(text, "Guild: " + targets[index]);
      append_castle_guild_list_summary(text, guild_castle_snapshot, targets[index]);
      append_dialog_entry(text, "View " + targets[index],
                          "@castle_guild_browse targets " +
                              std::to_string(static_cast<int>(page)) + " " + targets[index]);
      append_dialog_entry(text, "War " + targets[index],
                          "@castle_war_confirm " + std::to_string(static_cast<int>(page)) + " " +
                              targets[index]);
    }
  }

  append_page_navigation(text, "@castle_war_targets", page, total_pages);
  append_dialog_entry(text, "Back", "@castle_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_castle_guild_browse_dialog_text(const Player& requester,
                                                  const GuildCastleSnapshot& guild_castle_snapshot,
                                                  const CastleGuildBrowseTarget& target) {
  std::string text = target.source == "targets" ? "Castle War Target\\" : "Castle War Detail\\";
  const auto back_action =
      target.source == "targets"
          ? "@castle_war_targets " + std::to_string(static_cast<int>(target.page))
          : "@castle_wars " + std::to_string(static_cast<int>(target.page));
  const auto wars = parse_castle_war_list(guild_castle_snapshot.castle_dialog);
  const auto active_war =
      std::any_of(wars.begin(), wars.end(), [&](const std::string& guild_name) {
        return equals_ignore_case(guild_name, target.guild_name);
      });

  append_dialog_line(text, "Castle: " + default_castle_name(guild_castle_snapshot.castle_dialog));
  append_dialog_line(text, "Target Guild: " + target.guild_name);
  append_dialog_line(text, "War Entry: " +
                               (active_war
                                    ? default_castle_war_entry_listed_label(
                                          guild_castle_snapshot.castle_dialog)
                                    : default_castle_war_entry_unlisted_label(
                                          guild_castle_snapshot.castle_dialog)));

  if (const auto* guild_state = find_guild_state(guild_castle_snapshot, target.guild_name);
      guild_state != nullptr) {
    append_dialog_line(text, "Lord: " + guild_state->lord);
    append_dialog_line(text, "Members: " + std::to_string(guild_state->members.size()));
    append_dialog_line(text, "Applicants: " + std::to_string(guild_state->applicants.size()));
    append_dialog_line(text, "Roster Preview: " + summarize_name_list(guild_state->members));
    append_dialog_line(text, "Applicant Preview: " + summarize_name_list(guild_state->applicants));
    append_dialog_entry(text, "View Members",
                        "@guild_roster castle_" + target.source + " " +
                            std::to_string(static_cast<int>(target.page)) + " 1 " +
                            guild_state->guild_name);
    if (!guild_state->applicants.empty()) {
      append_dialog_entry(text, "View Applicants",
                          "@guild_applicant_roster castle_" + target.source + " " +
                              std::to_string(static_cast<int>(target.page)) + " 1 " +
                              guild_state->guild_name);
    }
    append_dialog_entry(text, "Browse Guild",
                        "@guild_browse castle_" + target.source + " " +
                            std::to_string(static_cast<int>(target.page)) + " " +
                            guild_state->guild_name);
    if (equals_ignore_case(guild_castle_snapshot.castle_dialog.owner_guild, guild_state->guild_name)) {
      append_dialog_line(text, "Castle Role: " +
                                   default_castle_owner_guild_role_label(
                                       guild_castle_snapshot.castle_dialog));
      append_dialog_line(text, "Castle: Owner of " +
                                   default_castle_name(guild_castle_snapshot.castle_dialog));
      append_dialog_line(text, "Castle Lord: " + display_castle_lord(guild_castle_snapshot.castle_dialog));
    } else if (active_war) {
      append_dialog_line(text, "Castle Role: " +
                                   default_castle_challenger_role_label(
                                       guild_castle_snapshot.castle_dialog));
      append_dialog_line(text, "Castle: None");
    } else {
      append_dialog_line(text, "Castle Role: " +
                                   default_castle_rival_role_label(
                                       guild_castle_snapshot.castle_dialog));
      append_dialog_line(text, "Castle: None");
    }
  } else {
    append_dialog_line(text, "Guild Data: Unknown");
    append_dialog_line(text, "Castle Role: " +
                                 (active_war
                                      ? default_castle_challenger_role_label(
                                            guild_castle_snapshot.castle_dialog)
                                      : default_castle_unknown_role_label(
                                            guild_castle_snapshot.castle_dialog)));
    append_dialog_line(text, "Castle: None");
  }

  append_dialog_line(text, "War Status: " +
                               (active_war
                                    ? default_castle_war_status_active_label(
                                          guild_castle_snapshot.castle_dialog)
                                    : default_castle_war_status_available_label(
                                          guild_castle_snapshot.castle_dialog)));
  if (target.source == "targets") {
    append_dialog_line(text,
                       "War Fee: " + std::to_string(guild_castle_snapshot.castle_dialog.guild_war_fee));
    append_dialog_line(text, "Gold: " + std::to_string(requester.character().gold));
    append_dialog_entry(text, "Confirm War",
                        "@castle_war_confirm " + std::to_string(static_cast<int>(target.page)) +
                            " " + target.guild_name);
  }

  append_dialog_entry(text, "Back", back_action);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_castle_claim_confirm_dialog_text(
    const Player& requester, const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Claim Castle\\";
  const auto& castle = guild_castle_snapshot.castle_dialog;
  append_dialog_line(text, "Castle: " + default_castle_name(castle));
  append_dialog_line(text, "Current Owner: " + display_castle_owner(castle));
  append_dialog_line(text, "New Owner: " + requester.character().guild_name);
  append_dialog_line(text, "Lord: " + requester.character().character_name);
  append_dialog_line(text, "Confirm castle ownership transfer?");
  append_dialog_entry(text, "Confirm", "@castle_claim");
  append_dialog_entry(text, "Back", "@castle_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

std::string build_castle_war_confirm_dialog_text(
    const Player& requester, const GuildCastleSnapshot& guild_castle_snapshot,
    const CastleWarConfirmTarget& target) {
  std::string text = "Declare Castle War\\";
  const auto& castle = guild_castle_snapshot.castle_dialog;
  append_dialog_line(text, "Castle: " + default_castle_name(castle));
  append_dialog_line(text, "Your Guild: " + requester.character().guild_name);
  append_dialog_line(text, "Target Guild: " + target.guild_name);
  append_dialog_line(text, "War Fee: " + std::to_string(castle.guild_war_fee));
  append_dialog_line(text, "Gold: " + std::to_string(requester.character().gold));
  append_dialog_line(text, "Confirm castle war registration?");
  append_dialog_entry(text, "Confirm", "@castle_war " + target.guild_name);
  append_dialog_entry(text, "Back",
                      "@castle_war_targets " + std::to_string(static_cast<int>(target.page)));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

bool handle_guild_castle_business_command(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const std::string& payload,
                                          GuildCastleSnapshot& guild_castle_snapshot,
                                          RuntimeDispatch& dispatch) {
  if (!util::starts_with(payload, "@")) {
    return false;
  }

  const auto tokens = util::split(payload, ' ');
  if (tokens.empty()) {
    return false;
  }

  std::vector<std::string> normalized_tokens;
  const auto command_root = util::lower_copy(tokens[0]);
  if (command_root == "@guild_create") {
    normalized_tokens = {"@guild", "create"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_join") {
    normalized_tokens = {"@guild", "join"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_apply") {
    normalized_tokens = {"@guild", "apply"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_leave") {
    normalized_tokens = {"@guild", "leave"};
  } else if (command_root == "@guild_kick") {
    normalized_tokens = {"@guild", "kick"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_transfer") {
    normalized_tokens = {"@guild", "transfer"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_title") {
    normalized_tokens = {"@guild", "title"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_title_template") {
    normalized_tokens = {"@guild", "title"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_approve") {
    normalized_tokens = {"@guild", "approve"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_reject") {
    normalized_tokens = {"@guild", "reject"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_applicants") {
    normalized_tokens = {"@guild", "applicants"};
  } else if (command_root == "@guild_info" || command_root == "@guild_show") {
    normalized_tokens = {"@guild", "info"};
  } else if (command_root == "@castle_claim") {
    normalized_tokens = {"@castle", "claim"};
  } else if (command_root == "@castle_war") {
    normalized_tokens = {"@castle", "war"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@castle_show") {
    normalized_tokens = {"@castle", "show"};
  } else {
    normalized_tokens = tokens;
  }

  if (normalized_tokens.empty()) {
    return false;
  }

  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  const auto normalized_root = util::lower_copy(normalized_tokens[0]);
  if (normalized_root == "@guild") {
    auto subcommand =
        normalized_tokens.size() >= 2 ? util::lower_copy(normalized_tokens[1]) : std::string{};
    if (subcommand == "create") {
      const auto result =
          execute_guild_create_action(speaker, guild_castle_snapshot, dispatch,
                                      normalized_tokens.size() > 2 ? join_tokens(normalized_tokens, 2)
                                                                   : std::string{});
      queue_system_notice(dispatch, speaker, result.summary);
      return true;
    }

    if (subcommand == "join") {
      normalized_tokens[1] = "apply";
      subcommand = "apply";
    }

    if (subcommand == "apply") {
      const auto result = execute_guild_apply_action(
          speaker, objects, guild_castle_snapshot, dispatch,
          normalized_tokens.size() > 2 ? join_tokens(normalized_tokens, 2) : std::string{});
      queue_system_notice(dispatch, speaker, result.summary);
      return true;
    }

    if (subcommand == "applicants") {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      const auto* guild_state =
          find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can review applications.");
        return true;
      }
      if (guild_state->applicants.empty()) {
        queue_system_notice(dispatch, speaker, "No pending guild applications.");
        return true;
      }
      queue_system_notice(dispatch, speaker,
                          "GuildApplicants=" + join_tokens(guild_state->applicants, 0, ", "));
      return true;
    }

    if (subcommand == "kick" && normalized_tokens.size() >= 3) {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can kick members.");
        return true;
      }

      const auto member_name = util::trim(join_tokens(normalized_tokens, 2));
      if (member_name.empty()) {
        queue_system_notice(dispatch, speaker, "Usage: @guild kick <member_name>");
        return true;
      }
      if (equals_ignore_case(member_name, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Use @guild leave to remove yourself from the guild.");
        return true;
      }
      if (!guild_has_member(*guild_state, member_name)) {
        queue_system_notice(dispatch, speaker, "That character is not a guild member.");
        return true;
      }

      auto* member = find_online_player_by_name(objects, member_name);
      if (member == nullptr ||
          !equals_ignore_case(member->character().guild_name, guild_state->guild_name)) {
        queue_load_offline_guild_character(
            dispatch,
            OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::kick,
                                    speaker.map_id(),
                                    speaker.id(),
                                    guild_state->guild_name,
                                    member_name,
                                    {}});
        return true;
      }

      remove_guild_member(*guild_state, member_name);
      member->clear_guild_membership();
      queue_save_guild_state(dispatch, *guild_state);
      queue_save_character(dispatch, *member);
      queue_system_notice(dispatch, speaker,
                          render_guild_summary_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_kick_summary_template,
                                  "Kicked guild member <$TARGET>."),
                              guild_state->guild_name, member_name));
      queue_system_notice(dispatch, *member,
                          render_guild_notice_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_removed_notice_template,
                                  "You were removed from guild <$GUILD>."),
                              guild_state->guild_name));
      return true;
    }

    if (subcommand == "transfer" && normalized_tokens.size() >= 3) {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can transfer leadership.");
        return true;
      }

      const auto target_name = util::trim(join_tokens(normalized_tokens, 2));
      if (target_name.empty()) {
        queue_system_notice(dispatch, speaker, "Usage: @guild transfer <member_name>");
        return true;
      }
      if (equals_ignore_case(target_name, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "You already lead this guild.");
        return true;
      }
      if (!guild_has_member(*guild_state, target_name)) {
        queue_system_notice(dispatch, speaker, "That character is not a guild member.");
        return true;
      }
      if (equals_ignore_case(target_name, guild_state->lord)) {
        queue_system_notice(dispatch, speaker, "You already lead this guild.");
        return true;
      }

      auto* target = find_online_player_by_name(objects, target_name);
      if (target == nullptr ||
          !equals_ignore_case(target->character().guild_name, guild_state->guild_name)) {
        queue_load_offline_guild_character(
            dispatch,
            OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::transfer,
                                    speaker.map_id(),
                                    speaker.id(),
                                    guild_state->guild_name,
                                    target_name,
                                    {}});
        return true;
      }

      guild_state->lord = target->character().character_name;
      speaker.set_guild_membership(guild_state->guild_name, "Member");
      target->set_guild_membership(guild_state->guild_name, "Lord");
      queue_save_guild_state(dispatch, *guild_state);
      queue_save_character(dispatch, speaker);
      queue_save_character(dispatch, *target);
      if (equals_ignore_case(castle_dialog_context.owner_guild, guild_state->guild_name)) {
        castle_dialog_context.lord = guild_state->lord;
        queue_save_castle_state(dispatch, castle_dialog_context);
      }
      queue_system_notice(dispatch, speaker,
                          render_guild_summary_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_transfer_summary_template,
                                  "Transferred guild leadership to <$TARGET>."),
                              guild_state->guild_name, target_name));
      queue_system_notice(dispatch, *target,
                          render_guild_notice_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_new_lord_notice_template,
                                  "You are now the guild lord of <$GUILD>."),
                              guild_state->guild_name));
      return true;
    }

    if (subcommand == "title" && normalized_tokens.size() >= 4) {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      const auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can change member titles.");
        return true;
      }

      const auto target_name = normalized_tokens[2];
      const auto title_name = util::trim(join_tokens(normalized_tokens, 3));
      if (title_name.empty()) {
        queue_system_notice(dispatch, speaker, "Usage: @guild title <member_name> <title>");
        return true;
      }
      if (!guild_has_member(*guild_state, target_name)) {
        queue_system_notice(dispatch, speaker, "That character is not a guild member.");
        return true;
      }
      if (equals_ignore_case(target_name, guild_state->lord)) {
        queue_system_notice(dispatch, speaker, "Use @guild transfer to change the guild lord.");
        return true;
      }

      auto* target = find_online_player_by_name(objects, target_name);
      if (target == nullptr ||
          !equals_ignore_case(target->character().guild_name, guild_state->guild_name)) {
        queue_load_offline_guild_character(
            dispatch,
            OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::title,
                                    speaker.map_id(),
                                    speaker.id(),
                                    guild_state->guild_name,
                                    target_name,
                                    title_name});
        return true;
      }
      if (equals_ignore_case(target->character().character_name, guild_state->lord)) {
        queue_system_notice(dispatch, speaker, "Use @guild transfer to change the guild lord.");
        return true;
      }

      target->set_guild_membership(guild_state->guild_name, title_name);
      queue_save_character(dispatch, *target);
      queue_system_notice(dispatch, speaker,
                          render_guild_summary_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_title_summary_template,
                                  "Set guild title for <$TARGET> to <$TITLE>."),
                              guild_state->guild_name, target->character().character_name,
                              title_name));
      queue_system_notice(dispatch, *target,
                          render_guild_notice_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_title_changed_notice_template,
                                  "Your guild title is now <$TITLE>."),
                              guild_state->guild_name, {}, title_name));
      return true;
    }

    if ((subcommand == "approve" || subcommand == "reject") && normalized_tokens.size() >= 3) {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can manage applications.");
        return true;
      }

      const auto applicant_name = util::trim(join_tokens(normalized_tokens, 2));
      if (!guild_has_applicant(*guild_state, applicant_name)) {
        queue_system_notice(dispatch, speaker, "That character has no pending application.");
        return true;
      }

      if (subcommand == "reject") {
        remove_guild_applicant(*guild_state, applicant_name);
        queue_save_guild_state(dispatch, *guild_state);
        queue_system_notice(dispatch, speaker,
                            render_guild_summary_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_reject_summary_template,
                                    "Rejected guild application for <$TARGET>."),
                                guild_state->guild_name, applicant_name));
        if (auto* applicant = find_online_player_by_name(objects, applicant_name); applicant != nullptr) {
          queue_system_notice(dispatch, *applicant,
                              render_guild_notice_template(
                                  configured_summary_template(
                                      castle_dialog_context.guild_rejected_notice_template,
                                      "Your application to <$GUILD> was rejected."),
                                  guild_state->guild_name));
        }
        return true;
      }

      auto* applicant = find_online_player_by_name(objects, applicant_name);
      if (applicant == nullptr) {
        queue_load_offline_guild_character(
            dispatch,
            OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::approve,
                                    speaker.map_id(),
                                    speaker.id(),
                                    guild_state->guild_name,
                                    applicant_name,
                                    {}});
        return true;
      }
      if (!applicant->character().guild_name.empty()) {
        remove_guild_applicant(*guild_state, applicant_name);
        queue_save_guild_state(dispatch, *guild_state);
        queue_system_notice(dispatch, speaker,
                            applicant_name + " is already in another guild. Application cleared.");
        return true;
      }

      remove_guild_applicant(*guild_state, applicant_name);
      add_guild_member(*guild_state, applicant->character().character_name);
      applicant->set_guild_membership(guild_state->guild_name, "Member");
      queue_save_guild_state(dispatch, *guild_state);
      queue_save_character(dispatch, *applicant);
      queue_system_notice(dispatch, speaker,
                          render_guild_summary_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_approve_summary_template,
                                  "Approved guild application for <$TARGET>."),
                              guild_state->guild_name, applicant_name));
      queue_system_notice(dispatch, *applicant,
                          render_guild_notice_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_approved_notice_template,
                                  "Your application to <$GUILD> was approved."),
                              guild_state->guild_name));
      return true;
    }

    if (subcommand == "leave") {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }

      const auto guild_name = speaker.character().guild_name;
      const auto character_name = speaker.character().character_name;
      auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
      const auto was_lord = guild_state != nullptr && equals_ignore_case(guild_state->lord, character_name);

      speaker.clear_guild_membership();
      queue_save_character(dispatch, speaker);

      if (guild_state == nullptr) {
        queue_system_notice(
            dispatch, speaker,
            configured_summary_template(castle_dialog_context.guild_membership_cleared_summary_template,
                                        "Guild membership cleared."));
        return true;
      }

      remove_guild_member(*guild_state, character_name);
      if (guild_state->members.empty()) {
        guild_castle_snapshot.guilds.erase(
            std::remove_if(guild_castle_snapshot.guilds.begin(), guild_castle_snapshot.guilds.end(),
                           [&](const GuildState& entry) {
                             return equals_ignore_case(entry.guild_name, guild_name);
                           }),
            guild_castle_snapshot.guilds.end());
        queue_delete_guild(dispatch, guild_name);
        if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
          castle_dialog_context.owner_guild.clear();
          castle_dialog_context.lord.clear();
          castle_dialog_context.list_of_war.clear();
          queue_save_castle_state(dispatch, castle_dialog_context);
        }
        queue_system_notice(dispatch, speaker,
                            render_guild_summary_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_disband_summary_template,
                                    "Guild <$GUILD> has been disbanded."),
                                guild_name));
        return true;
      }

      if (was_lord) {
        guild_state->lord = guild_state->members.front();
        if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
          castle_dialog_context.lord = guild_state->lord;
        }
      }

      queue_save_guild_state(dispatch, *guild_state);
      if (was_lord) {
        queue_system_notice(dispatch, speaker,
                            render_guild_summary_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_leave_transfer_summary_template,
                                    "You left <$GUILD>. New lord: <$NEWLORD>."),
                                guild_name, {}, {}, guild_state->lord));
      } else {
        queue_system_notice(dispatch, speaker,
                            render_guild_summary_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_leave_summary_template,
                                    "You left <$GUILD>."),
                                guild_name));
      }
      return true;
    }

    if (subcommand == "info" || subcommand == "show") {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      queue_system_notice(
          dispatch, speaker,
          build_guild_info_line(
              speaker, guild_castle_snapshot,
              find_guild_state(guild_castle_snapshot, speaker.character().guild_name)));
      return true;
    }

    return false;
  }

  if (normalized_root == "@castle") {
    const auto subcommand =
        normalized_tokens.size() >= 2 ? util::lower_copy(normalized_tokens[1]) : std::string{};
    if (subcommand == "show") {
      queue_system_notice(dispatch, speaker, build_castle_show_line(castle_dialog_context));
      return true;
    }

    if (subcommand == "claim") {
      const auto result = execute_castle_claim(speaker, guild_castle_snapshot, dispatch);
      queue_system_notice(dispatch, speaker, result.summary);
      return true;
    }

    if (subcommand == "war") {
      const auto result = execute_castle_war(speaker, guild_castle_snapshot, dispatch,
                                             normalized_tokens.size() > 2 ? join_tokens(normalized_tokens, 2)
                                                                          : std::string{});
      queue_system_notice(dispatch, speaker, result.summary);
      return true;
    }

    return false;
  }

  return false;
}

bool handle_castle_admin_command(const Player& speaker, const std::string& payload,
                                 GuildCastleSnapshot& guild_castle_snapshot,
                                 RuntimeDispatch& dispatch) {
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  if (!util::starts_with(payload, "@")) {
    return false;
  }

  const auto tokens = util::split(payload, ' ');
  if (tokens.empty()) {
    return false;
  }

  const auto command_root = util::lower_copy(tokens[0]);
  std::vector<std::string> normalized_tokens;
  if (command_root == "@castle_show") {
    normalized_tokens = {"@castle", "show"};
  } else if (command_root == "@castle_owner") {
    normalized_tokens = {"@castle", "owner"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@castle_wardate") {
    normalized_tokens = {"@castle", "wardate"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@castle_wars") {
    normalized_tokens = {"@castle", "wars"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@castle_fees") {
    normalized_tokens = {"@castle", "fees"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_lord" || command_root == "@guild_master") {
    normalized_tokens = {"@guild", "lord"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else {
    normalized_tokens = tokens;
  }

  const auto normalized_root = util::lower_copy(normalized_tokens[0]);
  if (normalized_root != "@castle" && normalized_root != "@guild") {
    return false;
  }

  if (!is_admin_account(speaker.character().account_id)) {
    queue_system_notice(dispatch, speaker, "GM castle commands are not allowed for this account.");
    return true;
  }

  if (normalized_root == "@castle") {
    if (normalized_tokens.size() >= 2 && util::lower_copy(normalized_tokens[1]) == "show") {
      queue_system_notice(dispatch, speaker, build_castle_show_line(castle_dialog_context));
      return true;
    }

    const auto subcommand =
        normalized_tokens.size() >= 2 ? util::lower_copy(normalized_tokens[1]) : std::string{};
    if (subcommand == "owner" && normalized_tokens.size() >= 3) {
      castle_dialog_context.owner_guild =
          normalize_castle_owner(castle_dialog_context, normalized_tokens[2]);
      sync_castle_lord_from_owner(guild_castle_snapshot);
      queue_save_castle_state(dispatch, castle_dialog_context);
      queue_system_notice(dispatch, speaker, "Castle owner update queued.");
      return true;
    }
    if (subcommand == "wardate" && normalized_tokens.size() >= 3) {
      castle_dialog_context.castle_war_date = join_tokens(normalized_tokens, 2);
      queue_save_castle_state(dispatch, castle_dialog_context);
      queue_system_notice(dispatch, speaker, "Castle war date update queued.");
      return true;
    }
    if (subcommand == "wars" && normalized_tokens.size() >= 3) {
      castle_dialog_context.list_of_war = join_tokens(normalized_tokens, 2);
      queue_save_castle_state(dispatch, castle_dialog_context);
      queue_system_notice(dispatch, speaker, "Castle rival list update queued.");
      return true;
    }
    if (subcommand == "fees" && normalized_tokens.size() >= 4) {
      const auto guild_fee = parse_int32(normalized_tokens[2]);
      const auto upgrade_fee = parse_int32(normalized_tokens[3]);
      if (!guild_fee.has_value() || !upgrade_fee.has_value()) {
        queue_system_notice(dispatch, speaker, "Usage: @castle fees <guild_fee> <upgrade_fee>");
        return true;
      }
      castle_dialog_context.guild_war_fee = *guild_fee;
      castle_dialog_context.upgrade_weapon_fee = *upgrade_fee;
      queue_save_castle_state(dispatch, castle_dialog_context);
      queue_system_notice(dispatch, speaker, "Castle fee update queued.");
      return true;
    }

    queue_system_notice(dispatch, speaker,
                        "Usage: @castle show|owner <guild>|wardate <text>|wars <text>|fees <a> <b>");
    return true;
  }

  if (normalized_root == "@guild") {
    const auto subcommand =
        normalized_tokens.size() >= 2 ? util::lower_copy(normalized_tokens[1]) : std::string{};
    if ((subcommand == "lord" || subcommand == "master") && normalized_tokens.size() >= 4) {
      const auto guild_name = normalized_tokens[2];
      const auto lord_name = join_tokens(normalized_tokens, 3);
      auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
      if (guild_state == nullptr) {
        guild_castle_snapshot.guilds.push_back(GuildState{guild_name, lord_name, {lord_name}});
        guild_state = &guild_castle_snapshot.guilds.back();
      } else {
        guild_state->lord = lord_name;
        add_guild_member(*guild_state, lord_name);
      }
      queue_save_guild_state(dispatch, *guild_state);
      if (equals_ignore_case(guild_name, castle_dialog_context.owner_guild)) {
        castle_dialog_context.lord = lord_name;
      }
      queue_system_notice(dispatch, speaker, "Guild lord update queued.");
      return true;
    }

    queue_system_notice(dispatch, speaker, "Usage: @guild lord <guild> <lord_name>");
    return true;
  }

  return false;
}

std::string build_merchant_dialog_text(const Npc& merchant) {
  if (const auto* scripted = find_npc_dialog_text(merchant, "@main"); scripted != nullptr) {
    return *scripted;
  }
  const auto entries = build_merchant_dialog_entries(merchant);
  if (entries.empty()) {
    return "Nothing is available right now.\\<Leave/@exit>";
  }

  std::string text = "How can I help you?\\";
  for (const auto& entry : entries) {
    text += "<" + entry.label + "/" + entry.action + ">\\";
  }
  text += "<Leave/@exit>";
  return text;
}

bool point_in_zone(const MapZoneConfig& zone, std::int32_t x, std::int32_t y) {
  return zone.width > 0 && zone.height > 0 && x >= zone.x && y >= zone.y &&
         x < zone.x + zone.width && y < zone.y + zone.height;
}

bool is_safe_zone(const MapConfig& map_config, std::int32_t x, std::int32_t y) {
  if (map_config.law_full) {
    return true;
  }
  if (std::any_of(map_config.badman_zones.begin(), map_config.badman_zones.end(),
                  [&](const MapZoneConfig& zone) { return point_in_zone(zone, x, y); })) {
    return true;
  }
  return std::any_of(map_config.safe_zones.begin(), map_config.safe_zones.end(),
                     [&](const MapZoneConfig& zone) { return point_in_zone(zone, x, y); });
}

std::int32_t area_state_mask(const MapConfig& map_config, std::int32_t x, std::int32_t y) {
  std::int32_t mask = 0;
  if (map_config.law_full) {
    mask |= kAreaSafe;
  }
  if (map_config.fight_zone || map_config.fight3_zone) {
    mask |= kAreaFight;
  }
  if (map_config.fight3_zone) {
    mask |= kAreaFreePk;
  }
  return mask;
}

std::string resolve_pk_block_reason(const MapConfig& map_config, const Player& attacker,
                                    const Player& target, std::uint64_t now_ms = 0) {
  const auto fight_map = map_config.fight_zone || map_config.fight3_zone;
  if (!map_config.allow_pk && !fight_map) {
    return "This map forbids PK.";
  }
  if (is_safe_zone(map_config, attacker.x(), attacker.y()) ||
      is_safe_zone(map_config, target.x(), target.y())) {
    return "Safe zone forbids combat.";
  }
  if (attacker.id() == target.id()) {
    return "Cannot attack self.";
  }
  if (!fight_map &&
      (attacker.character().ability.level < 10 || target.character().ability.level < 10)) {
    return "Newbie protection forbids PK.";
  }
  if (target.death_time_ms() != 0 || target.is_dead()) {
    return "Target is already dead.";
  }
  if (!fight_map && now_ms > 0 && target.legacy_run_time_ms() > 0 &&
      now_ms < static_cast<std::uint64_t>(target.legacy_run_time_ms()) + kMapChangeProtectMs) {
    return "Map change protection forbids PK.";
  }
  const auto mode = attacker.attack_mode();
  if (mode == kHamAll) {
    return {};
  }
  if (mode == kHamGroup) {
    if (attacker.legacy_group_id() != 0 &&
        attacker.legacy_group_id() == target.legacy_group_id()) {
      return "Group mode protects group members.";
    }
    return {};
  }
  if (mode == kHamPeace) {
    return "Peace mode forbids PK.";
  }
  if (mode == kHamPkAttack && target.pk_level() < 2 &&
      !target.has_recent_pk_hiter(attacker.id(), now_ms)) {
    return "Red-name mode can only attack PK targets.";
  }
  if (mode == kHamGuild) {
    if (!attacker.character().guild_name.empty() &&
        equals_ignore_case(attacker.character().guild_name, target.character().guild_name)) {
      return "Guild mode protects guild members.";
    }
  }
  return {};
}


}  // namespace
