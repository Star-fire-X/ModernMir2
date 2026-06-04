/**
 * @file map_actor_packets.hpp
 * @brief 传统网络数据包构造实现 - MapActor 的客户端通信包生成
 * @details 该文件是 map_actor.cpp 的实现细节部分，包含所有客户端通信数据包的
 *          构造函数。实现在匿名命名空间中，作为 map_actor.cpp 的内部实现细节。
 *
 *          核心功能：
 *          - 玩家数据包：登录、能力值、背包、装备、魔法列表
 *          - 移动数据包：行走、跑步、传送、空间移动
 *          - 战斗数据包：攻击、受击、魔法释放、死亡
 *          - NPC 数据包：对话、商店、修理、仓库
 *          - 交易数据包：发起、出价、确认、提交
 *          - 物品数据包：拾取、丢弃、更新、耐久变化
 *          - 可见性数据包：演员出现/消失、物品显示/隐藏、事件显示/隐藏
 *          - 门控制数据包：开门、关门
 *          - 杂项数据包：聊天、系统消息、健康值变更、等级提升
 *
 * @warning 这些包的格式必须与 Delphi 客户端严格兼容，
 *          任何结构体布局或标识符的改变都会导致客户端通信异常。
 */

#pragma once

// Implementation detail for map_actor.cpp; included inside namespace mir2.
namespace {

/**
 * @brief 创建 ACK 响应包
 * @param session_id 目标会话 ID
 * @param ok 操作是否成功
 * @return 原始文本格式的 ACK 包
 * @details 返回 "+GOOD/timestamp" 或 "+FAIL/timestamp" 格式的文本包。
 *          用于简单的成功/失败响应确认。
 */
LegacyPacket make_ack_packet(std::uint64_t session_id, bool ok) {
  return make_legacy_raw_packet(session_id,
                                std::string(ok ? "+GOOD/" : "+FAIL/") +
                                    std::to_string(tick_count_ms()));
}

/**
 * @brief 创建转身类动作包
 * @param session_id 目标会话 ID
 * @param ident 消息标识符
 * @param object 执行转身动作的游戏对象
 * @param include_name 是否在数据体中包含角色名称
 * @return 游戏数据包
 * @details 构造包含角色描述块和可选名称的转身动作包。
 *          用于客户端播放转身动画。
 */
LegacyPacket make_turn_like_packet(std::uint64_t session_id, std::uint16_t ident,
                                   const GameObject& object, bool include_name) {
  const auto desc = make_char_desc(object);
  auto body = legacy_encode_buffer(&desc, sizeof(desc));
  if (include_name) {
    body += legacy_encode_text(actor_name(object) + "/" + std::to_string(actor_name_color(object)));
  }

  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ident, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()),
                           make_word(actor_dir(object), actor_light(object))),
      body);
}

/**
 * @brief 将客户端攻击标识符解析为服务端对应的标识符
 * @param cm_ident 客户端到服务端的攻击标识符
 * @return 服务端到客户端的攻击标识符
 */
std::uint16_t resolve_hit_ident(std::uint16_t cm_ident) {
  return legacy::cm_attack_ident_to_sm(cm_ident);
}

/**
 * @brief 创建攻击命中包
 * @param session_id 目标会话 ID
 * @param object 发动攻击的游戏对象
 * @param cm_ident 客户端攻击标识符
 * @return 游戏数据包
 * @details 将客户端攻击标识符转换为服务端标识符后构造命中包。
 *          用于向客户端广播攻击动画。
 */
LegacyPacket make_hit_packet(std::uint64_t session_id, const GameObject& object,
                             std::uint16_t cm_ident) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(resolve_hit_ident(cm_ident), static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()), actor_dir(object)));
}

/**
 * @brief 创建冲刺（Rush）包
 * @param session_id 目标会话 ID
 * @param object 执行冲刺动作的游戏对象
 * @return 游戏数据包
 * @details 用于战士的冲刺技能，向客户端发送冲刺动画。
 */
LegacyPacket make_rush_packet(std::uint64_t session_id, const GameObject& object) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmRush, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()), actor_dir(object)));
}

/**
 * @brief 创建冲刺攻击（RushKung）包
 * @param session_id 目标会话 ID
 * @param object 执行冲刺攻击的游戏对象
 * @param x 目标 X 坐标
 * @param y 目标 Y 坐标
 * @return 游戏数据包
 * @details 包含冲刺攻击的目标位置信息。
 */
LegacyPacket make_rush_kung_packet(std::uint64_t session_id, const GameObject& object,
                                   std::int32_t x, std::int32_t y) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmRushKung, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(x),
                           static_cast<std::uint16_t>(y), actor_dir(object)));
}

/**
 * @brief 创建剑法状态包
 * @param session_id 目标会话 ID
 * @param state 剑法状态描述字符串
 * @return 原始文本格式包
 * @details 用于通知客户端剑法状态的变更，如"开启"/"关闭"等。
 */
LegacyPacket make_sword_state_packet(std::uint64_t session_id, std::string state) {
  return make_legacy_raw_packet(session_id, std::move(state) + "/" +
                                                std::to_string(tick_count_ms()));
}

/**
 * @brief 从 LegacyUserItem 构造 LegacyStdItem（标准物品描述结构）
 * @param item 用户物品实例
 * @param item_configs 物品配置表
 * @return 标准物品描述结构
 * @details 将用户持有的物品实例转换为网络传输用的标准格式。
 *          使用 upgraded_item_config 获取装备升级后的属性值，
 *          所有值都经过 std::clamp 确保在有效范围内。
 *          如果配置不存在，使用物品自身索引作为外观。
 */
LegacyStdItem make_std_item(const LegacyUserItem& item,
                            const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  LegacyStdItem std_item;
  const auto* config = find_item_config(item_configs, item.index);
  const auto name =
      config != nullptr && !config->name.empty() ? config->name : "Item " + std::to_string(item.index);
  set_short_string(std_item.name, name);
  if (config != nullptr) {
    const auto upgraded = legacy_upgraded_item_config(*config, item);
    std_item.std_mode = static_cast<std::uint8_t>(std::clamp(upgraded.std_mode, 0, 255));
    std_item.shape = static_cast<std::uint8_t>(std::clamp(upgraded.shape, 0, 255));
    std_item.weight = static_cast<std::uint8_t>(std::clamp(upgraded.weight, 0, 255));
    std_item.ani_count = static_cast<std::uint8_t>(std::clamp(upgraded.ani_count, 0, 255));
    std_item.looks =
        static_cast<std::uint16_t>(std::clamp(upgraded.looks > 0 ? upgraded.looks : item.index, 0, 65535));
    std_item.price = upgraded.price;
    std_item.dura_max =
        static_cast<std::uint16_t>(std::clamp(upgraded.dura_max > 0 ? upgraded.dura_max : item.dura_max,
                                              0, 65535));
    std_item.hp_add = upgraded.hp_add;
    std_item.mp_add = upgraded.mp_add;
    std_item.ac = upgraded.ac;
    std_item.mac = upgraded.mac;
    std_item.dc = upgraded.dc;
    std_item.mc = upgraded.mc;
    std_item.sc = upgraded.sc;
    std_item.need = static_cast<std::uint8_t>(std::clamp(upgraded.need, 0, 255));
    std_item.need_level = static_cast<std::uint8_t>(std::clamp(upgraded.need_level, 0, 255));
    std_item.stock = upgraded.stock;
    std_item.special_pwr =
        static_cast<std::int8_t>(std::clamp(upgraded.special_pwr, -128, 127));
    std_item.item_desc = static_cast<std::uint8_t>(std::clamp(upgraded.item_desc, 0, 255));
    std_item.accurate = static_cast<std::uint8_t>(std::clamp(upgraded.accurate, 0, 255));
    std_item.agility = static_cast<std::uint8_t>(std::clamp(upgraded.agility, 0, 255));
    std_item.atk_spd = static_cast<std::uint8_t>(std::clamp(upgraded.atk_spd, 0, 255));
    std_item.mg_avoid = static_cast<std::uint8_t>(std::clamp(upgraded.mg_avoid, 0, 255));
    std_item.strong = static_cast<std::uint8_t>(std::clamp(upgraded.strong, 0, 255));
    std_item.undead = static_cast<std::uint8_t>(std::clamp(upgraded.undead, 0, 255));
    std_item.exp_add = upgraded.exp_add;
    std_item.eff_type1 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_type1, 0, 255));
    std_item.eff_rate1 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_rate1, 0, 255));
    std_item.eff_value1 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_value1, 0, 255));
    std_item.eff_type2 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_type2, 0, 255));
    std_item.eff_rate2 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_rate2, 0, 255));
    std_item.eff_value2 = static_cast<std::uint8_t>(std::clamp(upgraded.eff_value2, 0, 255));
  } else {
    std_item.looks =
        static_cast<std::uint16_t>(std::clamp<std::int32_t>(item.index, 0, 65535));
    std_item.dura_max = item.dura_max;
  }
  return std_item;
}

/**
 * @brief 创建客户端物品描述（标准物品描述 + 实例数据）
 * @param item 用户物品实例
 * @param item_configs 物品配置表
 * @return 客户端物品描述
 * @details 在标准物品描述基础上附加制造索引、当前耐久和最大耐久。
 *          用于网络传输完整的物品信息给客户端。
 * @see make_std_item
 */
LegacyClientItem make_client_item(const LegacyUserItem& item,
                                  const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  LegacyClientItem client_item;
  client_item.item = make_std_item(item, item_configs);
  client_item.make_index = item.make_index;
  client_item.dura = item.dura;
  client_item.dura_max = item_dura_max(item, item_configs);
  return client_item;
}

/**
 * @brief 创建标准魔法定义（DefMagic）
 * @param magic 已学习的魔法信息
 * @param magic_configs 魔法配置表
 * @return 标准魔法定义
 * @details 将 LegacyUseMagicInfo 转换为网络传输用的标准魔法定义。
 *          如果魔法配置存在 legacy 字段，使用 legacy 字段值；
 *          否则使用基础配置值推导。
 *          包含效果类型、消耗、威力、职业要求、等级需求、训练值等。
 */
LegacyDefMagic make_def_magic(const LegacyUseMagicInfo& magic,
                              const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  LegacyDefMagic def;
  const auto config_it = magic_configs.find(magic.magic_id);
  const auto name = config_it != magic_configs.end() && !config_it->second.name.empty()
                        ? config_it->second.name
                        : "Magic " + std::to_string(magic.magic_id);
  set_short_string(def.magic_name, name);
  set_short_string(def.desc, name);
  def.magic_id = magic.magic_id;
  def.effect = static_cast<std::uint8_t>(magic.magic_id);
  def.delay_time = 1000;
  def.max_train_level = 3;
  def.need_level = {1, 1, 1, 1};
  def.max_train = {100, 300, 600, 900};
  if (config_it != magic_configs.end()) {
    const auto& config = config_it->second;
    if (config.legacy.legacy_present) {
      def.effect_type = static_cast<std::uint8_t>(std::clamp(config.legacy.effect_type, 0, 255));
      def.effect = static_cast<std::uint8_t>(std::clamp(config.legacy.effect, 0, 255));
      def.spell = static_cast<std::uint16_t>(std::clamp(config.legacy.spell, 0, 65535));
      def.min_power =
          static_cast<std::uint16_t>(std::clamp(config.legacy.min_power, 0, 65535));
      def.max_power =
          static_cast<std::uint16_t>(std::clamp(config.legacy.max_power, 0, 65535));
      def.job = static_cast<std::uint8_t>(std::clamp(config.legacy.job, 0, 255));
      for (std::size_t index = 0; index < def.need_level.size(); ++index) {
        def.need_level[index] =
            static_cast<std::uint8_t>(std::clamp(config.legacy.need_level[index], 0, 255));
        def.max_train[index] = config.legacy.max_train[index];
      }
      def.max_train_level =
          static_cast<std::uint8_t>(std::clamp(config.legacy.max_train_level, 0, 255));
      def.delay_time = std::max(config.legacy.delay_time, 0);
      def.def_spell = static_cast<std::uint8_t>(std::clamp(config.legacy.def_spell, 0, 255));
      def.def_min_power =
          static_cast<std::uint8_t>(std::clamp(config.legacy.def_min_power, 0, 255));
      def.def_max_power =
          static_cast<std::uint8_t>(std::clamp(config.legacy.def_max_power, 0, 255));
      set_short_string(def.desc, config.legacy.desc.empty() ? name : config.legacy.desc);
    } else {
      def.spell = static_cast<std::uint16_t>(std::max(config.mp_cost, 0));
      def.min_power = static_cast<std::uint16_t>(std::max(config.power, 0));
      def.max_power = static_cast<std::uint16_t>(std::max(config.power, 0));
      def.effect = static_cast<std::uint8_t>(std::clamp(config.id, 0, 255));
    }
  }
  return def;
}

/**
 * @brief 创建客户端魔法描述（标准定义 + 实例数据）
 * @param magic 已学习的魔法信息
 * @param magic_configs 魔法配置表
 * @return 客户端魔法描述
 * @details 在标准魔法定义基础上附加快捷键、等级和当前训练值。
 */
LegacyClientMagic make_client_magic(
    const LegacyUseMagicInfo& magic,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  LegacyClientMagic client_magic;
  client_magic.key = magic.key;
  client_magic.level = magic.level;
  client_magic.cur_train = magic.cur_train;
  client_magic.def = make_def_magic(magic, magic_configs);
  return client_magic;
}

/// @name 玩家登录/初始化包
/// @{

/**
 * @brief 创建新地图包
 * @param session_id 目标会话 ID
 * @param player 目标玩家
 * @param map_config 地图配置
 * @param x 出生 X 坐标
 * @param y 出生 Y 坐标
 * @param darkness 地图黑暗度
 * @return 游戏数据包
 * @details 通知客户端切换到新地图，设置出生坐标和地图 ID。
 *          包含地图的黑暗度信息。
 */
LegacyPacket make_new_map_packet(std::uint64_t session_id, const Player& player,
                                 const MapConfig& map_config,
                                 std::int32_t x, std::int32_t y,
                                 std::uint16_t darkness) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmNewMap, static_cast<std::int32_t>(player.id()),
                           static_cast<std::uint16_t>(x),
                           static_cast<std::uint16_t>(y), darkness),
      legacy_encode_text(map_config.id));
}

/**
 * @brief 创建登录包
 * @param session_id 目标会话 ID
 * @param player 登录的玩家
 * @return 游戏数据包
 * @details 包含玩家的外观特征、状态、位置、方向和光照信息。
 *          该包是登录序列中的关键包，客户端据此在屏幕上显示角色。
 */
LegacyPacket make_logon_packet(std::uint64_t session_id, const Player& player) {
  LegacyMessageBodyWL body;
  body.lparam1 = player.character().feature;
  body.lparam2 = player.character().status;
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmLogon, static_cast<std::int32_t>(player.id()),
                           static_cast<std::uint16_t>(player.x()),
                           static_cast<std::uint16_t>(player.y()),
                           make_word(player.character().dir, player.character().light)),
      legacy_encode_buffer(&body, sizeof(body)));
}

/**
 * @brief 创建区域状态包
 * @param session_id 目标会话 ID
 * @param area_state 区域状态掩码（安全区/战斗区等）
 * @return 游戏数据包
 * @details 通知客户端当前位置的区域类型（安全区、战斗区、边境等）。
 */
LegacyPacket make_area_state_packet(std::uint64_t session_id, std::int32_t area_state) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmAreaState, area_state, 0, 0, 0));
}

/**
 * @brief 创建地图描述包
 * @param session_id 目标会话 ID
 * @param map_config 地图配置
 * @return 游戏数据包
 * @details 发送地图标题给客户端，用于在地图界面右上角显示地图名称。
 */
LegacyPacket make_map_description_packet(std::uint64_t session_id, const MapConfig& map_config) {
  const auto title = map_config.title.empty() ? map_config.id : map_config.title;
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmMapDescription, 0, 0, 0, 0),
                                 legacy_encode_text(title));
}

/**
 * @brief 创建用户名包
 * @param session_id 目标会话 ID
 * @param actor_id 角色 ID
 * @param user_name 角色名称
 * @param color 名称颜色（默认 kDefaultNameColor）
 * @return 游戏数据包
 * @details 用于在角色头顶显示名称和颜色。
 *          颜色值控制名称的显示颜色（红名、GM 名字等）。
 */
LegacyPacket make_username_packet(std::uint64_t session_id, std::uint64_t actor_id,
                                  std::string user_name, std::uint8_t color = kDefaultNameColor) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmUsername, static_cast<std::int32_t>(actor_id), color, 0, 0),
      legacy_encode_text(std::move(user_name)));
}

/**
 * @brief 创建能力值包
 * @param session_id 目标会话 ID
 * @param character 角色记录
 * @return 游戏数据包
 * @details 包含角色的核心属性（等级、HP、MP、攻击、防御、魔法等）。
 *          客户端据此更新角色面板。
 */
LegacyPacket make_ability_packet(std::uint64_t session_id, const CharacterRecord& character) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmAbility, character.gold, character.job, 0, 0),
      legacy_encode_buffer(&character.ability, sizeof(character.ability)));
}

/**
 * @brief 创建所有背包物品包
 * @param session_id 目标会话 ID
 * @param player 目标玩家
 * @param item_configs 物品配置表
 * @return 游戏数据包
 * @details 发送玩家背包中所有非空物品的完整列表。
 *          每个物品编码为 LegacyClientItem 格式并序列化。
 */
LegacyPacket make_bag_items_packet(
    std::uint64_t session_id, const Player& player,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  std::string body;
  std::uint16_t count = 0;
  for (const auto& item : player.character().bag_items) {
    if (is_empty(item)) {
      continue;
    }
    const auto client_item = make_client_item(item, item_configs);
    body += legacy_encode_buffer(&client_item, sizeof(client_item));
    body.push_back('/');
    ++count;
  }
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmBagItems, static_cast<std::int32_t>(player.id()), 0, 0, count), body);
}

/**
 * @brief 创建所有已装备物品包
 * @param session_id 目标会话 ID
 * @param player 目标玩家
 * @param item_configs 物品配置表
 * @return 游戏数据包
 * @details 发送玩家所有已装备槽位的物品列表，格式为 "槽位索引/ClientItem/..."。
 */
LegacyPacket make_use_items_packet(
    std::uint64_t session_id, const Player& player,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  std::string body;
  for (std::size_t index = 0; index < player.character().equipped_items.size(); ++index) {
    const auto& item = player.character().equipped_items[index];
    if (is_empty(item)) {
      continue;
    }
    const auto client_item = make_client_item(item, item_configs);
    body += std::to_string(index);
    body.push_back('/');
    body += legacy_encode_buffer(&client_item, sizeof(client_item));
    body.push_back('/');
  }
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmSendUseItems, 0, 0, 0, 0), body);
}

/**
 * @brief 创建所有已学魔法包
 * @param session_id 目标会话 ID
 * @param player 目标玩家
 * @param magic_configs 魔法配置表
 * @return 游戏数据包
 * @details 发送玩家所有已学魔法的完整列表。
 *          计算延迟时间校验和用于客户端验证数据完整性。
 */
LegacyPacket make_my_magic_packet(
    std::uint64_t session_id, const Player& player,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  std::string body;
  std::int32_t total_delay = 0;
  std::uint16_t count = 0;
  for (const auto& magic : player.character().magics) {
    if (is_empty(magic)) {
      continue;
    }
    const auto client_magic = make_client_magic(magic, magic_configs);
    total_delay += client_magic.def.delay_time;
    body += legacy_encode_buffer(&client_magic, sizeof(client_magic));
    body.push_back('/');
    ++count;
  }
  const auto checksum = (total_delay ^ 0x773F1A34) ^ 0x4BBC2255;
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendMyMagic, checksum, 0, 0, count), body);
}

/// @}

/// @name 魔法学习/升级包
/// @{

/**
 * @brief 创建添加魔法包
 * @param session_id 目标会话 ID
 * @param magic 要添加的魔法信息
 * @param magic_configs 魔法配置表
 * @return 游戏数据包
 * @details 当玩家学习新技能时发送，客户端会更新技能栏。
 */
LegacyPacket make_add_magic_packet(
    std::uint64_t session_id, const LegacyUseMagicInfo& magic,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  const auto client_magic = make_client_magic(magic, magic_configs);
  return make_legacy_game_packet(
      session_id, 0, 0, make_default_message(kSmAddMagic, 0, 0, 0, 1),
      legacy_encode_buffer(&client_magic, sizeof(client_magic)));
}

/**
 * @brief 创建删除魔法包
 * @param session_id 目标会话 ID
 * @param magic_id 要删除的魔法 ID
 * @return 游戏数据包
 * @details 当玩家遗忘或删除技能时发送，客户端会从技能栏移除。
 */
LegacyPacket make_del_magic_packet(std::uint64_t session_id, std::int32_t magic_id) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDelMagic, magic_id, 0, 0, 1));
}

/**
 * @brief 创建魔法等级/经验变更包
 * @param session_id 目标会话 ID
 * @param magic_id 魔法 ID
 * @param level 新等级（0-3）
 * @param cur_train 当前训练值
 * @return 游戏数据包
 * @details 通知客户端技能等级或熟练度发生变化。
 */
LegacyPacket make_magic_lvexp_packet(std::uint64_t session_id, std::int32_t magic_id,
                                     std::int32_t level, std::int32_t cur_train) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmMagicLvExp, magic_id, static_cast<std::uint16_t>(level),
                           low_word(cur_train), high_word(cur_train)));
}

/// @}

/// @name 聊天/消息包
/// @{

/**
 * @brief 创建听到消息包
 * @param session_id 目标会话 ID
 * @param actor_id 发言者角色 ID
 * @param message 消息内容
 * @return 游戏数据包
 * @details 普通聊天消息包，使用默认颜色显示在聊天框中。
 */
LegacyPacket make_hear_packet(std::uint64_t session_id, std::uint64_t actor_id,
                              const std::string& message) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmHear, static_cast<std::int32_t>(actor_id),
                           make_word(kDefaultChatColor, kDefaultChatShadow), 0, 0),
      legacy_encode_text(message));
}

/**
 * @brief 创建传统聊天包（支持多种聊天类型）
 * @param session_id 目标会话 ID
 * @param kind 聊天投递类型（普通/私聊/公会/组队/喊话/系统）
 * @param recog_actor_id 相关角色 ID
 * @param message 消息内容
 * @return 游戏数据包
 * @details 根据聊天类型选择对应的消息标识符和颜色：
 *          - whisper: 私聊（颜色 252）
 *          - guild: 公会聊天（颜色 212）
 *          - group: 组队聊天（颜色 196）
 *          - shout/shout_direct: 喊话（颜色 151）
 *          - system: 系统消息（颜色 56）
 */
LegacyPacket make_legacy_chat_packet(std::uint64_t session_id,
                                      LegacyChatDeliveryKind kind,
                                      std::uint64_t recog_actor_id,
                                      const std::string& message) {
  std::uint16_t ident = kSmHear;
  std::uint16_t color = make_word(0, 255);
  std::int32_t recog = static_cast<std::int32_t>(recog_actor_id);
  switch (kind) {
    case LegacyChatDeliveryKind::whisper:
      ident = kSmWhisper;
      color = make_word(252, 255);
      break;
    case LegacyChatDeliveryKind::guild:
      ident = kSmGuildMessage;
      color = make_word(212, 255);
      break;
    case LegacyChatDeliveryKind::group:
      ident = kSmSysMessage;
      color = make_word(196, 255);
      break;
    case LegacyChatDeliveryKind::shout:
    case LegacyChatDeliveryKind::shout_direct:
      recog = 0;
      color = make_word(0, 151);
      break;
    case LegacyChatDeliveryKind::system:
      ident = kSmSysMessage;
      color = make_word(255, 56);
      break;
    case LegacyChatDeliveryKind::normal:
    case LegacyChatDeliveryKind::none:
      break;
  }
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ident, recog, color, 0, 1),
      legacy_encode_text(message));
}

/**
 * @brief 创建系统公告包
 * @param session_id 目标会话 ID
 * @param message 公告内容
 * @return 游戏数据包
 * @details 使用 hear 包格式发送系统公告消息。
 */
LegacyPacket make_system_notice_packet(std::uint64_t session_id, const std::string& message) {
  return make_hear_packet(session_id, 0, message);
}

/// @}

/// @name 护盾通知文本生成
/// @{

/// 生成护盾施加的自身通知文本
std::string make_shield_apply_self_notice(const std::string& shield_name) {
  return (shield_name.empty() ? "A magical shield" : shield_name) + " surrounds you.";
}

/// 生成护盾施加的观察者通知文本
std::string make_shield_apply_watcher_notice(const Player& target, const std::string& shield_name) {
  return actor_name(target) + " is surrounded by " +
         (shield_name.empty() ? std::string("a magical shield") : shield_name) + ".";
}

/// 生成护盾破碎的自身通知文本
std::string make_shield_break_self_notice(const std::string& shield_name) {
  return "Your " + (shield_name.empty() ? std::string("magical shield") : shield_name) +
         " shatters.";
}

/// 生成护盾破碎的观察者通知文本
std::string make_shield_break_watcher_notice(const Player& target, const std::string& shield_name) {
  return actor_name(target) + "'s " +
         (shield_name.empty() ? std::string("magical shield") : shield_name) + " shatters.";
}

/// 生成护盾消失的自身通知文本
std::string make_shield_fade_self_notice(const std::string& shield_name) {
  return "Your " + (shield_name.empty() ? std::string("magical shield") : shield_name) +
         " fades.";
}

/// 生成护盾消失的观察者通知文本
std::string make_shield_fade_watcher_notice(const Player& target, const std::string& shield_name) {
  return actor_name(target) + "'s " +
         (shield_name.empty() ? std::string("magical shield") : shield_name) + " fades.";
}

/// @}

/// @name 移动/传送包
/// @{

/**
 * @brief 创建移动失败包
 * @param session_id 目标会话 ID
 * @param object 移动失败的游戏对象
 * @return 游戏数据包
 * @details 当玩家尝试移动到不可行走位置时发送，客户端将角色拉回原位。
 */
LegacyPacket make_move_fail_packet(std::uint64_t session_id, const GameObject& object) {
  const auto desc = make_char_desc(object);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmMoveFail, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()), actor_dir(object)),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

/**
 * @brief 创建对象消失包
 * @param session_id 目标会话 ID
 * @param actor_id 要消失的角色 ID
 * @return 游戏数据包
 * @details 通知客户端指定角色从视野中消失（离开视野范围或断开连接）。
 */
LegacyPacket make_disappear_packet(std::uint64_t session_id, std::uint64_t actor_id) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmDisappear,
                                                      static_cast<std::int32_t>(actor_id),
                                                      0, 0, 0));
}

/**
 * @brief 创建开门包
 * @param session_id 目标会话 ID
 * @param x 门 X 坐标
 * @param y 门 Y 坐标
 * @return 游戏数据包
 * @details 通知客户端指定坐标的门已打开。
 */
LegacyPacket make_open_door_packet(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmOpenDoorOk, 0,
                                                      static_cast<std::uint16_t>(x),
                                                      static_cast<std::uint16_t>(y), 0));
}

/**
 * @brief 创建关门包
 * @param session_id 目标会话 ID
 * @param x 门 X 坐标
 * @param y 门 Y 坐标
 * @return 游戏数据包
 * @details 通知客户端指定坐标的门已关闭。
 */
LegacyPacket make_close_door_packet(std::uint64_t session_id, std::int32_t x, std::int32_t y) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmCloseDoor, 0,
                                                      static_cast<std::uint16_t>(x),
                                                      static_cast<std::uint16_t>(y), 0));
}

/**
 * @brief 创建清空对象包
 * @param session_id 目标会话 ID
 * @return 游戏数据包
 * @details 通知客户端清空当前地图上的所有可见对象。
 *          通常在地图切换时发送。
 */
LegacyPacket make_clear_objects_packet(std::uint64_t session_id) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmClearObjects, 0, 0, 0, 0));
}

/**
 * @brief 创建切换地图包
 * @param session_id 目标会话 ID
 * @param map_id 目标地图 ID
 * @param x 目标 X 坐标
 * @param y 目标 Y 坐标
 * @param darkness 地图黑暗度
 * @return 游戏数据包
 * @details 通知客户端加载新地图并传送到指定坐标。
 */
LegacyPacket make_change_map_packet(std::uint64_t session_id, std::string map_id,
                                    std::int32_t x, std::int32_t y,
                                    std::uint16_t darkness) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmChangeMap, 0,
                                                      static_cast<std::uint16_t>(x),
                                                      static_cast<std::uint16_t>(y), darkness),
                                 legacy_encode_text(std::move(map_id)));
}

/// @}

/// @name 魔法/技能包
/// @{

/**
 * @brief 创建魔法释放包
 * @param session_id 目标会话 ID
 * @param object 施法者对象
 * @param mail 包含施法信息的邮件
 * @param magic_configs 魔法配置表
 * @return 游戏数据包
 * @details 根据邮件中的魔法 ID 查询配置，确定视觉效果索引后发送给客户端。
 *          如果配置中有 legacy.effect 字段则使用该值，否则使用魔法 ID。
 */
LegacyPacket make_spell_packet(
    std::uint64_t session_id, const GameObject& object, const ActorMail& mail,
    const std::unordered_map<std::int32_t, MagicConfig>& magic_configs) {
  const auto magic_id = static_cast<std::int32_t>(mail.game_message.tag);
  auto effect = static_cast<std::uint16_t>(magic_id);
  if (const auto it = magic_configs.find(magic_id); it != magic_configs.end()) {
    effect = static_cast<std::uint16_t>(std::clamp(
        it->second.legacy.legacy_present ? it->second.legacy.effect : it->second.id, 0, 65535));
  }
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSpell, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(mail.x), static_cast<std::uint16_t>(mail.y),
                           effect),
      std::to_string(magic_id));
}

/**
 * @brief 创建魔法火焰包（攻击类魔法）
 * @param session_id 目标会话 ID
 * @param caster 施法者对象
 * @param x 目标 X 坐标
 * @param y 目标 Y 坐标
 * @param magic 魔法配置
 * @param target_actor_id 目标角色 ID
 * @return 游戏数据包
 * @details 用于火球术、雷电术等指向性攻击魔法的效果包。
 *          包含施法者位置、目标位置、效果类型和目标 ID。
 */
LegacyPacket make_magic_fire_packet(std::uint64_t session_id, const GameObject& caster,
                                    std::int32_t x, std::int32_t y,
                                    const MagicConfig& magic,
                                    std::uint64_t target_actor_id) {
  const auto effect_type = magic.legacy.legacy_present ? magic.legacy.effect_type : 0;
  const auto effect = magic.legacy.legacy_present ? magic.legacy.effect : magic.id;
  auto target = static_cast<std::int32_t>(target_actor_id);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmMagicFire, static_cast<std::int32_t>(caster.id()),
                           static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                           make_word(static_cast<std::uint8_t>(std::clamp(effect_type, 0, 255)),
                                     static_cast<std::uint8_t>(std::clamp(effect, 0, 255)))),
      legacy_encode_buffer(&target, sizeof(target)));
}

/**
 * @brief 创建魔法释放失败包
 * @param session_id 目标会话 ID
 * @param caster 施法者对象
 * @return 游戏数据包
 * @details 当魔法释放条件不满足（如 MP 不足、冷却中）时发送。
 */
LegacyPacket make_magic_fire_fail_packet(std::uint64_t session_id, const GameObject& caster) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmMagicFireFail,
                                                      static_cast<std::int32_t>(caster.id()), 0,
                                                      0, 0));
}

/// @}

/// @name 物品操作包
/// @{

/**
 * @brief 创建添加物品到背包包
 * @param session_id 目标会话 ID
 * @param actor_id 角色 ID
 * @param item 要添加的物品
 * @param item_configs 物品配置表
 * @return 游戏数据包
 */
LegacyPacket make_add_item_packet(
    std::uint64_t session_id, std::uint64_t actor_id, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmAddItem, static_cast<std::int32_t>(actor_id), 0, 0, 1),
      legacy_encode_buffer(&client_item, sizeof(client_item)));
}

/**
 * @brief 创建更新物品包
 * @param session_id 目标会话 ID
 * @param actor_id 角色 ID
 * @param item 更新后的物品
 * @param item_configs 物品配置表
 * @return 游戏数据包
 * @details 当背包中的物品属性发生变化时发送（如装备升级后）。
 */
LegacyPacket make_update_item_packet(
    std::uint64_t session_id, std::uint64_t actor_id, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmUpdateItem, static_cast<std::int32_t>(actor_id), 0, 0, 1),
      legacy_encode_buffer(&client_item, sizeof(client_item)));
}

/**
 * @brief 创建删除物品包
 * @param session_id 目标会话 ID
 * @param actor_id 角色 ID
 * @param item 被删除的物品
 * @param item_configs 物品配置表
 * @return 游戏数据包
 */
LegacyPacket make_del_item_packet(
    std::uint64_t session_id, std::uint64_t actor_id, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDelItem, static_cast<std::int32_t>(actor_id), 0, 0, 1),
      legacy_encode_buffer(&client_item, sizeof(client_item)));
}

/**
 * @brief 创建物品显示包（地面物品出现）
 * @param session_id 目标会话 ID
 * @param item 地面物品
 * @return 游戏数据包
 * @details 通知客户端在指定坐标显示一个地面物品，包含外观和名称。
 */
LegacyPacket make_item_show_packet(std::uint64_t session_id, const MapActor::GroundItem& item) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmItemShow, static_cast<std::int32_t>(item.id),
                           static_cast<std::uint16_t>(item.x), static_cast<std::uint16_t>(item.y),
                           static_cast<std::uint16_t>(std::clamp(item.looks, 0, 65535))),
      legacy_encode_text(item.name));
}

/**
 * @brief 创建物品隐藏包（地面物品消失）
 * @param session_id 目标会话 ID
 * @param item 要隐藏的地面物品
 * @return 游戏数据包
 */
LegacyPacket make_item_hide_packet(std::uint64_t session_id, const MapActor::GroundItem& item) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmItemHide, static_cast<std::int32_t>(item.id),
                           static_cast<std::uint16_t>(item.x), static_cast<std::uint16_t>(item.y), 0));
}

/// @}

/// @name 事件对象包
/// @{

/**
 * @brief 创建显示事件包
 * @param session_id 目标会话 ID
 * @param event_id 事件对象 ID
 * @param x 事件 X 坐标
 * @param y 事件 Y 坐标
 * @param type 事件类型
 * @return 游戏数据包
 * @details 用于在地面上显示火墙、毒雾、圣言区域等事件效果。
 */
LegacyPacket make_show_event_packet(std::uint64_t session_id, std::uint64_t event_id,
                                    std::int32_t x, std::int32_t y,
                                    LegacyEventType type) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmShowEvent, static_cast<std::int32_t>(event_id),
                           static_cast<std::uint16_t>(x),
                           static_cast<std::uint16_t>(y),
                           static_cast<std::uint16_t>(type)));
}

/**
 * @brief 创建隐藏事件包
 * @param session_id 目标会话 ID
 * @param event_id 要隐藏的事件对象 ID
 * @param x 事件 X 坐标
 * @param y 事件 Y 坐标
 * @return 游戏数据包
 */
LegacyPacket make_hide_event_packet(std::uint64_t session_id, std::uint64_t event_id,
                                    std::int32_t x, std::int32_t y) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmHideEvent, static_cast<std::int32_t>(event_id),
                           static_cast<std::uint16_t>(x),
                           static_cast<std::uint16_t>(y), 0));
}

/// @}

/// @name 物品操作结果包
/// @{

/**
 * @brief 创建丢弃物品结果包
 * @param session_id 目标会话 ID
 * @param ok 丢弃是否成功
 * @param make_index 被丢弃物品的制造索引
 * @param item_name 物品名称
 * @return 游戏数据包
 */
LegacyPacket make_drop_result_packet(std::uint64_t session_id, bool ok, std::int32_t make_index,
                                     const std::string& item_name) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmDropItemSuccess : kSmDropItemFail, make_index, 0, 0, 0),
      legacy_encode_text(item_name));
}

/**
 * @brief 创建穿戴物品结果包
 * @param session_id 目标会话 ID
 * @param ok 穿戴是否成功
 * @param feature 穿戴后的外观特征
 * @return 游戏数据包
 */
LegacyPacket make_take_on_result_packet(std::uint64_t session_id, bool ok, std::int32_t feature) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmTakeOnOk : kSmTakeOnFail, ok ? feature : 0, 0, 0, 0));
}

/**
 * @brief 创建卸下装备结果包
 * @param session_id 目标会话 ID
 * @param ok 卸下是否成功
 * @param feature 卸下后的外观特征
 * @return 游戏数据包
 */
LegacyPacket make_take_off_result_packet(std::uint64_t session_id, bool ok, std::int32_t feature) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmTakeOffOk : kSmTakeOffFail, ok ? feature : 0, 0, 0, 0));
}

/**
 * @brief 创建使用物品结果包
 * @param session_id 目标会话 ID
 * @param ok 使用是否成功
 * @return 游戏数据包
 */
LegacyPacket make_eat_result_packet(std::uint64_t session_id, bool ok) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmEatOk : kSmEatFail, 0, 0, 0, 0));
}

/// @}

/// @name 外观/状态变更包
/// @{

/**
 * @brief 创建外观特征变更包
 * @param session_id 目标会话 ID
 * @param actor_id 变更外观的角色 ID
 * @param feature 新的外观特征值
 * @return 游戏数据包
 * @details 用于发型变更、装备外观变化时的广播通知。
 */
LegacyPacket make_feature_changed_packet(std::uint64_t session_id, std::uint64_t actor_id,
                                         std::int32_t feature) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmFeatureChanged, static_cast<std::int32_t>(actor_id), low_word(feature),
                           high_word(feature), 0));
}

/**
 * @brief 创建角色状态变更包
 * @param session_id 目标会话 ID
 * @param player 状态变更的玩家
 * @return 游戏数据包
 * @details 包含隐身、中毒、火墙等状态信息的变更通知。
 */
LegacyPacket make_char_status_changed_packet(std::uint64_t session_id, const Player& player) {
  const auto status = player.character().status;
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmCharStatusChanged, static_cast<std::int32_t>(player.id()),
                           low_word(status), high_word(status),
                           low_word(actor_hit_speed(player))));
}

/// @}

/// @name 属性/资源变更包
/// @{

/**
 * @brief 创建负重变更包
 * @param session_id 目标会话 ID
 * @param character 角色记录
 * @return 游戏数据包
 * @details 包含总负重、穿戴负重和手持负重的值，以及用于客户端验证的校验和。
 *          校验和 = ((total + wear + hand) ^ 0x3A5F ^ 0x1F35 ^ 0xAA21)
 */
LegacyPacket make_weight_changed_packet(std::uint64_t session_id, const CharacterRecord& character) {
  const auto total_weight = static_cast<std::uint16_t>(
      std::clamp<std::int32_t>(character.ability.weight, 0, 65535));
  const auto wear_weight = static_cast<std::uint16_t>(character.ability.wear_weight);
  const auto hand_weight = static_cast<std::uint16_t>(character.ability.hand_weight);
  const auto checksum = static_cast<std::uint16_t>(
      (((total_weight + wear_weight + hand_weight) ^ 0x3A5F) ^ 0x1F35) ^ 0xAA21);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmWeightChanged, total_weight, static_cast<std::uint16_t>(wear_weight),
                           static_cast<std::uint16_t>(hand_weight), checksum));
}

/**
 * @brief 创建金币变更包
 * @param session_id 目标会话 ID
 * @param gold 新的金币数量
 * @return 游戏数据包
 */
LegacyPacket make_gold_changed_packet(std::uint64_t session_id, std::int32_t gold) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmGoldChanged, gold, 0, 0, 0));
}

/// @}

/// @name 交易系统包
/// @{

/**
 * @brief 创建交易菜单包
 * @param session_id 目标会话 ID
 * @param peer_name 交易对方名称
 * @return 游戏数据包
 * @details 通知客户端打开交易界面，并显示对方名称。
 */
LegacyPacket make_deal_menu_packet(std::uint64_t session_id, std::string_view peer_name) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmDealMenu, 0, 0, 0, 0),
                                 legacy_encode_text(std::string(peer_name)));
}

/**
 * @brief 创建简单交易操作包（确认、取消等）
 * @param session_id 目标会话 ID
 * @param ident 交易相关的消息标识符
 * @return 游戏数据包
 */
LegacyPacket make_deal_simple_packet(std::uint64_t session_id, std::uint16_t ident) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(ident, 0, 0, 0, 0));
}

/**
 * @brief 创建交易金币变更包
 * @param session_id 目标会话 ID
 * @param ident 消息标识符
 * @param deal_gold 交易中的金币数量
 * @param bag_gold 背包中的金币数量
 * @return 游戏数据包
 */
LegacyPacket make_deal_change_gold_packet(std::uint64_t session_id, std::uint16_t ident,
                                          std::int32_t deal_gold, std::int32_t bag_gold) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ident, deal_gold, low_word(bag_gold), high_word(bag_gold), 0));
}

/**
 * @brief 创建交易对方金币变更包
 * @param session_id 目标会话 ID
 * @param deal_gold 对方交易中的金币数量
 * @return 游戏数据包
 * @details 通知当前玩家交易对方修改了出价金币数量。
 */
LegacyPacket make_deal_remote_change_gold_packet(std::uint64_t session_id,
                                                 std::int32_t deal_gold) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDealRemoteChangeGold, deal_gold, 0, 0, 0));
}

/**
 * @brief 创建交易对方添加物品包
 * @param session_id 目标会话 ID
 * @param actor_id 对方角色 ID
 * @param item 添加的物品
 * @param item_configs 物品配置表
 * @return 游戏数据包
 * @details 通知当前玩家交易对方将指定物品放入了交易栏。
 */
LegacyPacket make_deal_remote_add_item_packet(
    std::uint64_t session_id, std::uint64_t actor_id, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDealRemoteAddItem, static_cast<std::int32_t>(actor_id),
                           0, 0, 1),
      legacy_encode_buffer(&client_item, sizeof(client_item)));
}

/**
 * @brief 创建交易对方删除物品包
 * @param session_id 目标会话 ID
 * @param actor_id 对方角色 ID
 * @param item 被删除的物品
 * @param item_configs 物品配置表
 * @return 游戏数据包
 * @details 通知当前玩家交易对方从交易栏移除了指定物品。
 */
LegacyPacket make_deal_remote_del_item_packet(std::uint64_t session_id,
                                              std::uint64_t actor_id,
                                              const LegacyUserItem& item,
                                              const std::unordered_map<std::int32_t, ItemConfig>&
                                                  item_configs) {
  const auto client_item = make_client_item(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDealRemoteDelItem, static_cast<std::int32_t>(actor_id),
                           0, 0, 1),
      legacy_encode_buffer(&client_item, sizeof(client_item)));
}

/// @}

/// @name 商人/NPC/商店包
/// @{

/**
 * @brief 创建出售界面包
 * @param session_id 目标会话 ID
 * @param merchant_actor_id 商人角色 ID
 * @return 游戏数据包
 * @details 通知客户端打开向商人出售物品的界面。
 */
LegacyPacket make_send_user_sell_packet(std::uint64_t session_id, std::uint64_t merchant_actor_id) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendUserSell, static_cast<std::int32_t>(merchant_actor_id), 0, 0, 0));
}

/**
 * @brief 计算商人售价
 * @param merchant 商人 NPC
 * @param item 待售物品
 * @param item_configs 物品配置表
 * @return 售价
 */
std::int32_t compute_merchant_sell_price(
    const Npc& merchant, const LegacyUserItem& item,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs);

/**
 * @brief 创建商品列表包
 * @param session_id 目标会话 ID
 * @param merchant_actor_id 商人角色 ID
 * @param merchant 商人 NPC 对象
 * @param item_configs 物品配置表
 * @return 游戏数据包
 * @details 发送商人出售的物品列表，每个条目包含名称、子菜单标记、价格和库存量。
 *          同种物品合并为一条记录并统计库存。
 */
LegacyPacket make_send_goods_list_packet(
    std::uint64_t session_id, std::uint64_t merchant_actor_id, const Npc& merchant,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  struct GoodsEntry {
    std::int32_t item_index{0};
    std::string name{};
    std::int32_t submenu{0};
    std::int32_t price{0};
    std::int32_t stock{0};
  };

  std::vector<GoodsEntry> entries;
  for (const auto& item : merchant.merchant_items()) {
    if (is_empty(item)) {
      continue;
    }
    const auto* config = find_item_config(item_configs, item.index);
    if (config == nullptr) {
      continue;
    }
    auto it = std::find_if(entries.begin(), entries.end(), [&](const GoodsEntry& entry) {
      return entry.item_index == item.index;
    });
    if (it == entries.end()) {
      entries.push_back(GoodsEntry{item.index, config->name,
                                   requires_detail_goods_list(*config) ? 1 : 0,
                                   compute_merchant_sell_price(merchant, item, item_configs),
                                   1});
    } else {
      ++it->stock;
    }
  }

  std::string body;
  for (const auto& entry : entries) {
    body += entry.name + "/" + std::to_string(entry.submenu) + "/" + std::to_string(entry.price) +
            "/" + std::to_string(entry.stock) + "/";
  }

  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendGoodsList, static_cast<std::int32_t>(merchant_actor_id),
                           static_cast<std::uint16_t>(entries.size()), 0, 0),
      legacy_encode_text(body));
}

/**
 * @brief 创建商人对话包
 * @param session_id 目标会话 ID
 * @param merchant_actor_id 商人角色 ID
 * @param merchant 商人 NPC 对象
 * @param text 对话文本内容
 * @return 游戏数据包
 * @details 发送商人 NPC 的对话文本，包含说话者名称和内容。
 *          使用默认的面部表情索引。
 */
LegacyPacket make_merchant_say_packet(std::uint64_t session_id, std::uint64_t merchant_actor_id,
                                      const Npc& merchant, std::string_view text) {
  const auto body = actor_name(merchant) + "/" + std::string(text);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmMerchantSay, static_cast<std::int32_t>(merchant_actor_id),
                           kDefaultMerchantFace, 0, 0),
      legacy_encode_text(body));
}

/**
 * @brief 创建掷骰子包
 * @param session_id 目标会话 ID
 * @param merchant_actor_id 商人角色 ID
 * @param dice_count 骰子数量
 * @param dice_params 骰子参数数组（10 个整型参数）
 * @param target_label 目标跳转标签
 * @return 游戏数据包
 * @details 用于 NPC 脚本中的随机掷骰子事件，客户端播放掷骰子动画和结果。
 *          10 个参数编码到消息体的两个长整型和标签字段中。
 */
LegacyPacket make_play_dice_packet(std::uint64_t session_id, std::uint64_t merchant_actor_id,
                                   std::int32_t dice_count,
                                   const std::array<std::int32_t, 10>& dice_params,
                                   std::string_view target_label) {
  auto byte_value = [](std::int32_t value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
  };
  LegacyMessageBodyWL body;
  body.lparam1 = make_long(make_word(byte_value(dice_params[0]), byte_value(dice_params[1])),
                           make_word(byte_value(dice_params[2]), byte_value(dice_params[3])));
  body.lparam2 = make_long(make_word(byte_value(dice_params[4]), byte_value(dice_params[5])),
                           make_word(byte_value(dice_params[6]), byte_value(dice_params[7])));
  body.ltag1 = make_long(make_word(byte_value(dice_params[8]), byte_value(dice_params[9])), 0);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmPlayDice, static_cast<std::int32_t>(merchant_actor_id),
                           static_cast<std::uint16_t>(std::clamp(dice_count, 0, 65535)),
                           0, 0),
      legacy_encode_buffer(&body, sizeof(body)) + legacy_encode_text(target_label));
}

/**
 * @brief 创建关闭商人对话包
 * @param session_id 目标会话 ID
 * @return 游戏数据包
 * @details 通知客户端关闭当前打开的 NPC 对话界面。
 */
LegacyPacket make_merchant_dlg_close_packet(std::uint64_t session_id) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmMerchantDlgClose, 0, 0, 0, 0));
}

/**
 * @brief 创建发送收购价格包
 * @param session_id 目标会话 ID
 * @param price 系统给出的收购价格
 * @return 游戏数据包
 */
LegacyPacket make_send_buy_price_packet(std::uint64_t session_id, std::int32_t price) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmSendBuyPrice, price, 0, 0, 0));
}

/**
 * @brief 创建详细商品列表包
 * @param session_id 目标会话 ID
 * @param merchant_actor_id 商人角色 ID
 * @param top_line 顶部行号（滚动位置）
 * @param merchant_items 商人物品列表
 * @param item_configs 物品配置表
 * @param merchant 商人 NPC 对象
 * @return 游戏数据包
 * @details 发送需要详细展示的商品列表（如查看具体属性）。
 *          每个物品的 dura_max 字段被重写为售卖价格。
 */
LegacyPacket make_send_detail_goods_list_packet(
    std::uint64_t session_id, std::uint64_t merchant_actor_id, std::int32_t top_line,
    const std::vector<LegacyUserItem>& merchant_items,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs, const Npc& merchant) {
  std::string body;
  std::uint16_t count = 0;
  for (const auto& item : merchant_items) {
    if (is_empty(item)) {
      continue;
    }
    auto client_item = make_client_item(item, item_configs);
    client_item.dura_max = static_cast<std::uint16_t>(
        std::clamp(compute_merchant_sell_price(merchant, item, item_configs), 0, 65535));
    body += legacy_encode_buffer(&client_item, sizeof(client_item));
    body.push_back('/');
    ++count;
  }

  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendDetailGoodsList, static_cast<std::int32_t>(merchant_actor_id),
                           count, static_cast<std::uint16_t>(std::clamp(top_line, 0, 65535)), 0),
      legacy_encode_text(body));
}

/**
 * @brief 创建玩家出售物品结果包
 * @param session_id 目标会话 ID
 * @param ok 出售是否成功
 * @param gold 获得的（或扣减的）金币数量
 * @return 游戏数据包
 */
LegacyPacket make_user_sell_result_packet(std::uint64_t session_id, bool ok, std::int32_t gold) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmUserSellItemOk : kSmUserSellItemFail, ok ? gold : 0, 0, 0, 0));
}

/**
 * @brief 创建购买物品结果包
 * @param session_id 目标会话 ID
 * @param ok 购买是否成功
 * @param value 关联值（失败时为原因，成功时为价格）
 * @param item_make_index 购买物品的制造索引
 * @return 游戏数据包
 */
LegacyPacket make_buy_item_result_packet(std::uint64_t session_id, bool ok, std::int32_t value,
                                         std::int32_t item_make_index) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmBuyItemSuccess : kSmBuyItemFail, value, low_word(item_make_index),
                           high_word(item_make_index), 0));
}

/// @}

/// @name 修理/仓库系统包
/// @{

/**
 * @brief 创建打开修理界面包
 * @param session_id 目标会话 ID
 * @param merchant_actor_id 商人角色 ID
 * @return 游戏数据包
 */
LegacyPacket make_send_user_repair_packet(std::uint64_t session_id, std::uint64_t merchant_actor_id) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendUserRepair, static_cast<std::int32_t>(merchant_actor_id), 0, 0, 0));
}

/**
 * @brief 创建打开仓库界面包
 * @param session_id 目标会话 ID
 * @param merchant_actor_id 仓库管理员角色 ID
 * @param count 仓库中物品数量
 * @return 游戏数据包
 */
LegacyPacket make_send_user_storage_packet(std::uint64_t session_id, std::uint64_t merchant_actor_id,
                                           std::uint16_t count) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSendUserStorageItem, static_cast<std::int32_t>(merchant_actor_id),
                           count, 0, 0));
}

/**
 * @brief 创建发送修理价格包
 * @param session_id 目标会话 ID
 * @param cost 修理费用
 * @return 游戏数据包
 */
LegacyPacket make_send_repair_cost_packet(std::uint64_t session_id, std::int32_t cost) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(kSmSendRepairCost, cost, 0, 0, 0));
}

/**
 * @brief 创建修理结果包
 * @param session_id 目标会话 ID
 * @param ok 修理是否成功
 * @param gold 修理消耗的金币
 * @param dura 修理后的当前耐久
 * @param dura_max 修理后的最大耐久
 * @return 游戏数据包
 */
LegacyPacket make_user_repair_result_packet(std::uint64_t session_id, bool ok, std::int32_t gold,
                                            std::uint16_t dura, std::uint16_t dura_max) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(ok ? kSmUserRepairItemOk : kSmUserRepairItemFail, ok ? gold : 0,
                           ok ? dura : 0, ok ? dura_max : 0, 0));
}

/**
 * @brief 创建仓库物品列表包
 * @param session_id 目标会话 ID
 * @param merchant_actor_id 仓库管理员角色 ID
 * @param player 目标玩家
 * @param item_configs 物品配置表
 * @return 游戏数据包
 * @details 发送玩家仓库中的所有非空物品列表。
 */
LegacyPacket make_save_item_list_packet(
    std::uint64_t session_id, std::uint64_t merchant_actor_id, const Player& player,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  std::string body;
  std::uint16_t count = 0;
  for (const auto& item : player.character().storage_items) {
    if (is_empty(item)) {
      continue;
    }
    const auto client_item = make_client_item(item, item_configs);
    body += legacy_encode_buffer(&client_item, sizeof(client_item));
    body.push_back('/');
    ++count;
  }
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSaveItemList, static_cast<std::int32_t>(merchant_actor_id), 0, 0,
                           count),
      body);
}

/**
 * @brief 创建仓库操作结果包
 * @param session_id 目标会话 ID
 * @param ident 结果标识符（成功/失败/已满等）
 * @return 游戏数据包
 */
LegacyPacket make_storage_result_packet(std::uint64_t session_id, std::uint16_t ident) {
  return make_legacy_game_packet(session_id, 0, 0, make_default_message(ident, 0, 0, 0, 0));
}

/**
 * @brief 创建取回仓库物品结果包
 * @param session_id 目标会话 ID
 * @param ident 结果标识符
 * @param make_index 被取回物品的制造索引
 * @return 游戏数据包
 */
LegacyPacket make_take_back_storage_result_packet(std::uint64_t session_id, std::uint16_t ident,
                                                  std::int32_t make_index) {
  return make_legacy_game_packet(session_id, 0, 0,
                                 make_default_message(ident, make_index, 0, 0, 0));
}

/// @}

/// @name 耐久度/生命值包
/// @{

/**
 * @brief 创建耐久度变更包
 * @param session_id 目标会话 ID
 * @param slot 装备槽位
 * @param item 耐久度变化的物品
 * @param item_configs 物品配置表
 * @return 游戏数据包
 */
LegacyPacket make_dura_change_packet(std::uint64_t session_id, std::size_t slot,
                                     const LegacyUserItem& item,
                                     const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto dura_max = item_dura_max(item, item_configs);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmDuraChange, item.dura, static_cast<std::uint16_t>(slot),
                           low_word(dura_max), high_word(dura_max)));
}

/**
 * @brief 创建生命值/魔法值变更包
 * @param session_id 目标会话 ID
 * @param player 生命值变化的玩家
 * @return 游戏数据包
 * @details 包含当前 HP、当前 MP 和最大 HP。
 */
LegacyPacket make_health_spell_changed_packet(std::uint64_t session_id, const Player& player) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmHealthSpellChanged, static_cast<std::int32_t>(player.id()),
                           player.character().ability.hp, player.character().ability.mp,
                           player.character().ability.max_hp));
}

/// @}

/// @name 战斗/伤害/死亡包
/// @{

/**
 * @brief 创建受击包
 * @param session_id 目标会话 ID
 * @param target 受击目标对象
 * @param hitter_id 攻击者角色 ID
 * @param damage 伤害值
 * @param magic_struck 是否为魔法攻击
 * @return 游戏数据包
 * @details 包含目标特征、状态、攻击者 ID 和是否魔法攻击的标志。
 *          hp/max_hp/damage 都裁剪到 uint16_t 范围发送。
 */
LegacyPacket make_struck_packet(std::uint64_t session_id, const GameObject& target,
                                std::uint64_t hitter_id, std::int32_t damage, bool magic_struck) {
  LegacyMessageBodyWL body;
  body.lparam1 = actor_feature(target);
  body.lparam2 = actor_status(target);
  body.ltag1 = static_cast<std::int32_t>(hitter_id);
  body.ltag2 = magic_struck ? 1 : 0;
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmStruck, static_cast<std::int32_t>(target.id()),
                           static_cast<std::uint16_t>(std::clamp(actor_hp(target), 0, 65535)),
                           static_cast<std::uint16_t>(std::clamp(actor_max_hp(target), 0, 65535)),
                           static_cast<std::uint16_t>(std::clamp(damage, 0, 65535))),
      legacy_encode_buffer(&body, sizeof(body)));
}

/**
 * @brief 创建死亡包
 * @param session_id 目标会话 ID
 * @param target 死亡的目标对象
 * @param now_death 是否为立即死亡包（kSmNowDeath），否则为普通死亡包（kSmDeath）
 * @return 游戏数据包
 */
LegacyPacket make_death_packet(std::uint64_t session_id, const GameObject& target,
                               bool now_death = false) {
  const auto desc = make_char_desc(target);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(now_death ? kSmNowDeath : kSmDeath,
                           static_cast<std::int32_t>(target.id()),
                           static_cast<std::uint16_t>(target.x()),
                           static_cast<std::uint16_t>(target.y()), actor_dir(target)),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

/**
 * @brief 创建骷髅（尸体）包
 * @param session_id 目标会话 ID
 * @param target 留下尸体的目标对象
 * @return 游戏数据包
 * @details 通知客户端在地面显示角色尸体的动画效果。
 */
LegacyPacket make_skeleton_packet(std::uint64_t session_id, const GameObject& target) {
  const auto desc = make_char_desc(target);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(legacy::kSmSkeleton, static_cast<std::int32_t>(target.id()),
                           static_cast<std::uint16_t>(target.x()),
                           static_cast<std::uint16_t>(target.y()), actor_dir(target)),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

/// @}

/// @name 空间移动包
/// @{

/**
 * @brief 创建空间移动隐藏 2 包
 * @param session_id 目标会话 ID
 * @param object 执行空间移动的对象
 * @return 游戏数据包
 * @details 第二种空间移动隐藏效果（show2=false 时使用）。
 */
LegacyPacket make_space_move_hide2_packet(std::uint64_t session_id, const GameObject& object) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSpaceMoveHide2, static_cast<std::int32_t>(object.id()), 0, 0, 0));
}

/**
 * @brief 创建空间移动隐藏包
 * @param session_id 目标会话 ID
 * @param object 执行空间移动的对象
 * @return 游戏数据包
 * @details 通知客户端隐藏指定对象的显示（空间移动消失效果）。
 */
LegacyPacket make_space_move_hide_packet(std::uint64_t session_id,
                                         const GameObject& object) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSpaceMoveHide, static_cast<std::int32_t>(object.id()), 0, 0, 0));
}

/**
 * @brief 创建空间移动显示包
 * @param session_id 目标会话 ID
 * @param object 执行空间移动的对象
 * @return 游戏数据包
 * @details 通知客户端在目标位置显示指定对象（空间移动出现效果）。
 */
LegacyPacket make_space_move_show_packet(std::uint64_t session_id, const GameObject& object) {
  const auto desc = make_char_desc(object);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSpaceMoveShow, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()),
                           make_word(actor_dir(object), actor_light(object))),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

/**
 * @brief 创建空间移动显示 2 包
 * @param session_id 目标会话 ID
 * @param object 执行空间移动的对象
 * @return 游戏数据包
 * @details show2 版本的空间移动显示效果。
 */
LegacyPacket make_space_move_show2_packet(std::uint64_t session_id, const GameObject& object) {
  const auto desc = make_char_desc(object);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSpaceMoveShow2, static_cast<std::int32_t>(object.id()),
                           static_cast<std::uint16_t>(object.x()),
                           static_cast<std::uint16_t>(object.y()),
                           make_word(actor_dir(object), actor_light(object))),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

/// @}

/// @name 复活/经验/等级提升包
/// @{

/**
 * @brief 创建复活包
 * @param session_id 目标会话 ID
 * @param player 复活的玩家
 * @return 游戏数据包
 * @details 通知客户端显示玩家复活效果。
 */
LegacyPacket make_alive_packet(std::uint64_t session_id, const Player& player) {
  const auto desc = make_char_desc(player);
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmAlive, static_cast<std::int32_t>(player.id()),
                           static_cast<std::uint16_t>(player.x()),
                           static_cast<std::uint16_t>(player.y()), actor_dir(player)),
      legacy_encode_buffer(&desc, sizeof(desc)));
}

/**
 * @brief 创建获得经验包
 * @param session_id 目标会话 ID
 * @param current_exp 当前总经验值
 * @param gained_exp 本次获得的经验值
 * @return 游戏数据包
 * @details 通知客户端显示获得经验的飘字效果。
 */
LegacyPacket make_win_exp_packet(std::uint64_t session_id, std::int32_t current_exp,
                                 std::int32_t gained_exp) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmWinExp, current_exp,
                           static_cast<std::uint16_t>(std::clamp(gained_exp, 0, 65535)), 0, 0));
}

/**
 * @brief 创建等级提升包
 * @param session_id 目标会话 ID
 * @param player 升级的玩家
 * @return 游戏数据包
 * @details 包含升级后的总经验值和新等级。
 */
LegacyPacket make_level_up_packet(std::uint64_t session_id, const Player& player) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmLevelUp, static_cast<std::int32_t>(player.character().ability.exp),
                           player.character().ability.level, 0, 0));
}

/// @}

/// @name 武器/子能力包
/// @{

/**
 * @brief 创建武器破碎包
 * @param session_id 目标会话 ID
 * @param player 武器破碎的玩家
 * @return 游戏数据包
 * @details 通知客户端播放武器破碎动画效果。
 */
LegacyPacket make_break_weapon_packet(std::uint64_t session_id, const Player& player) {
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmBreakWeapon, static_cast<std::int32_t>(player.id()), 0, 0, 0));
}

/**
 * @brief 创建子能力值包
 * @param session_id 目标会话 ID
 * @param player 目标玩家
 * @return 游戏数据包
 * @details 包含命中、攻速和毒抗三个子能力值。
 *          hit_point 和 speed_point 编码在一个 word 中，
 *          anti_poison 编码在另一个 word 中。
 */
LegacyPacket make_sub_ability_packet(std::uint64_t session_id, const Player& player) {
  const auto hit_point =
      static_cast<std::uint8_t>(std::clamp(player.accuracy_point(), 0, 255));
  const auto speed_point = static_cast<std::uint8_t>(std::clamp(player.speed_point(), 0, 255));
  const auto anti_poison =
      static_cast<std::uint8_t>(std::clamp(player.legacy_anti_poison(), 0, 255));
  return make_legacy_game_packet(
      session_id, 0, 0,
      make_default_message(kSmSubAbility, 0, make_word(hit_point, speed_point),
                           make_word(anti_poison, 0), 0));
}

/// @}

/// @name 登录序列/区域同步
/// @{

/**
 * @brief 发送完整登录序列包组到客户端
 * @param dispatch 运行时调度输出
 * @param player 登录的玩家
 * @param map_config 地图配置
 * @param item_configs 物品配置表
 * @param magic_configs 魔法配置表
 * @param area_state 区域状态值
 * @details 按顺序发送以下包完成客户端初始化：
 *          1. NewMap（地图切换）
 *          2. Logon（角色登录）
 *          3. Username（角色名称和颜色）
 *          4. AreaState（区域类型）
 *          5. MapDescription（地图名称）
 *          6. Ability（能力值）
 *          7. UseItems（已装备物品）
 *          8. MyMagic（已学魔法）
 */
void dispatch_login_sequence(RuntimeDispatch& dispatch, const Player& player,
                             const MapConfig& map_config,
                             const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                             const std::unordered_map<std::int32_t, MagicConfig>& magic_configs,
                             std::int32_t area_state) {
  const auto darkness = legacy_map_darkness(map_config);
  queue_packet(dispatch, player.session_id(),
               make_new_map_packet(player.session_id(), player, map_config, player.x(), player.y(),
                                   darkness));
  queue_packet(dispatch, player.session_id(), make_logon_packet(player.session_id(), player));
  queue_packet(dispatch, player.session_id(),
               make_username_packet(player.session_id(), player.id(), player.character().character_name,
                                    actor_name_color(player)));
  queue_packet(dispatch, player.session_id(), make_area_state_packet(player.session_id(), area_state));
  queue_packet(dispatch, player.session_id(),
               make_map_description_packet(player.session_id(), map_config));
  queue_packet(dispatch, player.session_id(), make_ability_packet(player.session_id(), player.character()));
  queue_packet(dispatch, player.session_id(),
               make_use_items_packet(player.session_id(), player, item_configs));
  queue_packet(dispatch, player.session_id(),
               make_my_magic_packet(player.session_id(), player, magic_configs));
}

/**
 * @brief 同步区域状态到客户端
 * @param dispatch 运行时调度输出
 * @param map_config 地图配置
 * @param player 目标玩家
 * @param force 是否强制发送（即使状态未变化）
 * @details 检查玩家当前位置的安全区状态，如果有变化则发送区域状态更新包。
 *          用于玩家进出安全区时的自动切换。
 */
void sync_area_state(RuntimeDispatch& dispatch, const MapConfig& map_config, Player& player,
                     bool force = false) {
  const auto in_safe = is_safe_zone(map_config, player.x(), player.y());
  if (force || in_safe != player.in_safe_zone()) {
    player.set_in_safe_zone(in_safe);
    queue_packet(dispatch, player.session_id(),
                 make_area_state_packet(player.session_id(),
                                        area_state_mask(map_config, player.x(), player.y())));
  }
}

/// @}

}  // namespace
