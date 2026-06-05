/**
 * @file guild_manager.cpp
 * @brief 公会管理系统实现
 * @details 实现了Guild（公会实体）和GuildManager（公会管理器）的全部功能。
 *          包括成员管理、等级体系、外交关系（结盟/宣战）、
 *          公会聊天分发、会长转让、公会战到期管理等核心逻辑。
 *          所有名称比较均不区分大小写，名称均去除首尾空格后存储。
 */

#include "world/guild_manager.hpp"

#include <algorithm>
#include <utility>

#include "util/string_utils.hpp"

namespace mir2 {

/**
 * @brief 匿名命名空间，定义内部辅助函数
 */
namespace {

/**
 * @brief 不区分大小写比较两个名称是否相等
 * @param lhs 左侧名称
 * @param rhs 右侧名称
 * @return true 相等，false 不相等
 */
bool equals_name(std::string_view lhs, std::string_view rhs) {
  return util::lower_copy(lhs) == util::lower_copy(rhs);
}

/**
 * @brief 标准化名称：去除首尾空格
 * @param name 原始名称
 * @return 处理后的名称
 */
std::string normalize_name(std::string name) {
  return util::trim(std::move(name));
}

}  // namespace

// ============================================================================
// Guild 成员方法实现
// ============================================================================

/**
 * @brief 构造函数
 * @param name 公会名称，自动标准化
 */
Guild::Guild(std::string name) : name_(normalize_name(std::move(name))) {}

/**
 * @brief 计算公会成员总数
 * @details 遍历所有等级分组，累加各组的成员数量。
 * @return 成员总数
 */
std::size_t Guild::member_count() const {
  std::size_t count = 0;
  for (const auto& rank : ranks_) {
    count += rank.members.size();
  }
  return count;
}

/**
 * @brief 获取会长名称
 * @details 会长信息存储在 ranks_ 的第一个元素中（等级为 kGuildLordRank）。
 *          如果公会没有会长（数据结构异常），返回 std::nullopt。
 * @return 会长名称的 optional 值
 */
std::optional<std::string_view> Guild::lord_name() const {
  if (ranks_.empty() || ranks_.front().rank != kGuildLordRank ||
      ranks_.front().members.empty()) {
    return std::nullopt;
  }
  return std::string_view{ranks_.front().members.front().name};
}

/**
 * @brief 查找成员（const版本）
 * @details 遍历所有等级分组，逐个比较成员名称。
 * @param name 成员名称
 * @return 成员指针，未找到返回 nullptr
 */
const GuildMember* Guild::find_member(std::string_view name) const {
  for (const auto& rank : ranks_) {
    for (const auto& member : rank.members) {
      if (equals_name(member.name, name)) {
        return &member;
      }
    }
  }
  return nullptr;
}

/**
 * @brief 查找成员（可变版本）
 * @details 遍历所有等级分组，逐个比较成员名称。
 * @param name 成员名称
 * @return 成员指针，未找到返回 nullptr
 */
GuildMember* Guild::find_member(std::string_view name) {
  for (auto& rank : ranks_) {
    for (auto& member : rank.members) {
      if (equals_name(member.name, name)) {
        return &member;
      }
    }
  }
  return nullptr;
}

/**
 * @brief 判断指定成员是否为会长
 * @param name 成员名称
 * @return true 是会长，false 不是
 */
bool Guild::is_lord(std::string_view name) const {
  const auto lord = lord_name();
  return lord.has_value() && equals_name(*lord, name);
}

/**
 * @brief 生成公会聊天投递列表
 * @details 构造形如 "发送者:消息内容" 的消息文本，
 *          筛选出所有在线且开启公会聊天的成员进行投递。
 * @param speaker_name 发言者名称
 * @param text 消息文本
 * @return 需要投递的消息列表
 */
std::vector<GuildChatDelivery> Guild::guild_chat_deliveries(std::string_view speaker_name,
                                                            std::string_view text) const {
  std::vector<GuildChatDelivery> deliveries;
  if (text.empty()) {
    return deliveries;
  }
  const auto message = std::string(speaker_name) + ":" + std::string(text);
  for (const auto& rank : ranks_) {
    for (const auto& member : rank.members) {
      if (member.online_actor_id != 0 && member.hears_guild_chat) {
        deliveries.push_back(GuildChatDelivery{member.name, member.online_actor_id, message});
      }
    }
  }
  return deliveries;
}

/**
 * @brief 获取所有在线成员的角色ID列表
 * @return 在线角色ID列表
 */
std::vector<std::uint64_t> Guild::online_member_actor_ids() const {
  std::vector<std::uint64_t> actor_ids;
  for (const auto& rank : ranks_) {
    for (const auto& member : rank.members) {
      if (member.online_actor_id != 0) {
        actor_ids.push_back(member.online_actor_id);
      }
    }
  }
  return actor_ids;
}

/**
 * @brief 检查是否为盟友公会
 * @param guild_name 公会名称
 * @return true 是盟友，false 不是
 */
bool Guild::is_ally_guild(std::string_view guild_name) const {
  return std::any_of(ally_guilds_.begin(), ally_guilds_.end(),
                     [&](const std::string& ally) {
                       return equals_name(ally, guild_name);
                     });
}

/**
 * @brief 检查是否为敌对公会
 * @param guild_name 公会名称
 * @return true 是敌对，false 不是
 */
bool Guild::is_hostile_guild(std::string_view guild_name) const {
  return std::any_of(hostile_guilds_.begin(), hostile_guilds_.end(),
                     [&](const GuildWarState& war) {
                       return equals_name(war.enemy_guild, guild_name);
                     });
}

/**
 * @brief 添加会长
 * @details 将指定成员以会长等级（kGuildLordRank=1）加入公会。
 *          如果公会已有会长则返回 false。
 * @param name 会长名称
 * @param rank_name 等级名称
 * @param online_actor_id 在线角色ID
 * @return true 成功，false 名称无效或已是成员
 */
bool Guild::add_lord(std::string name, std::string rank_name, std::uint64_t online_actor_id) {
  name = normalize_name(std::move(name));
  rank_name = util::trim(std::move(rank_name));
  if (name.empty() || has_member(name)) {
    return false;
  }
  auto& rank = ensure_rank(kGuildLordRank, std::move(rank_name));
  rank.members.push_back(GuildMember{name, rank.rank, rank.rank_name, online_actor_id, true});
  return true;
}

/**
 * @brief 添加普通成员
 * @details 将指定成员以默认等级（kGuildDefaultRank=99）加入公会。
 * @param name 成员名称
 * @param rank_name 等级名称
 * @param online_actor_id 在线角色ID
 * @return true 成功，false 名称无效或已是成员
 */
bool Guild::add_member(std::string name, std::string rank_name, std::uint64_t online_actor_id) {
  name = normalize_name(std::move(name));
  rank_name = util::trim(std::move(rank_name));
  if (name.empty() || has_member(name)) {
    return false;
  }
  auto& rank = ensure_rank(kGuildDefaultRank, std::move(rank_name));
  rank.members.push_back(GuildMember{name, rank.rank, rank.rank_name, online_actor_id, true});
  return true;
}

/**
 * @brief 移除成员
 * @details 遍历所有等级分组，找到指定成员并移除。
 *          如果移除后该分组为空，自动清理该分组。
 * @param name 成员名称
 * @return true 成功删除，false 成员不存在
 */
bool Guild::remove_member(std::string_view name) {
  for (auto rank_it = ranks_.begin(); rank_it != ranks_.end(); ++rank_it) {
    auto& members = rank_it->members;
    const auto old_size = members.size();
    // 使用 erase-remove 惯用法移除匹配的成员
    members.erase(std::remove_if(members.begin(), members.end(),
                                 [&](const GuildMember& member) {
                                   return equals_name(member.name, name);
                                 }),
                  members.end());
    if (members.size() != old_size) {
      if (members.empty()) {
        ranks_.erase(rank_it);
      }
      return true;
    }
  }
  return false;
}

/**
 * @brief 设置成员在线状态
 * @param name 成员名称
 * @param online_actor_id 在线角色ID
 * @return true 成功，false 成员不存在
 */
bool Guild::set_member_online_actor(std::string_view name, std::uint64_t online_actor_id) {
  auto* member = find_member(name);
  if (member == nullptr) {
    return false;
  }
  member->online_actor_id = online_actor_id;
  return true;
}

/**
 * @brief 清除成员在线状态
 * @details 用于成员下线时清理。需要验证提供的 actor_id 与存储的一致，防止误清理。
 * @param name 成员名称
 * @param online_actor_id 当前在线角色ID（用于验证）
 * @return true 成功，false 成员不存在或ID不匹配
 */
bool Guild::clear_member_online_actor(std::string_view name, std::uint64_t online_actor_id) {
  auto* member = find_member(name);
  if (member == nullptr || member->online_actor_id != online_actor_id) {
    return false;
  }
  member->online_actor_id = 0;
  return true;
}

/**
 * @brief 设置成员是否接收公会聊天
 * @param name 成员名称
 * @param hears_guild_chat 是否接收
 * @return true 成功，false 成员不存在
 */
bool Guild::set_member_hears_guild_chat(std::string_view name, bool hears_guild_chat) {
  auto* member = find_member(name);
  if (member == nullptr) {
    return false;
  }
  member->hears_guild_chat = hears_guild_chat;
  return true;
}

/**
 * @brief 添加盟友
 * @param guild_name 盟友公会名称
 * @return true 成功，false 名称无效或已是盟友
 */
bool Guild::make_ally_guild(std::string guild_name) {
  guild_name = normalize_name(std::move(guild_name));
  if (guild_name.empty() || is_ally_guild(guild_name)) {
    return false;
  }
  ally_guilds_.push_back(std::move(guild_name));
  return true;
}

/**
 * @brief 解除盟友关系
 * @param guild_name 原盟友公会名称
 * @return true 成功，false 未结盟
 */
bool Guild::break_ally_guild(std::string_view guild_name) {
  const auto old_size = ally_guilds_.size();
  ally_guilds_.erase(std::remove_if(ally_guilds_.begin(), ally_guilds_.end(),
                                    [&](const std::string& ally) {
                                      return equals_name(ally, guild_name);
                                    }),
                     ally_guilds_.end());
  return ally_guilds_.size() != old_size;
}

/**
 * @brief 宣战
 * @details 向目标公会宣战。如果已经处于敌对状态，则重置战争计时器。
 *          不能对盟友宣战。
 * @param guild_name 目标公会名称
 * @param now_ms 当前时间戳
 * @param remain_ms 战争持续时间，默认3小时
 * @return true 成功，false 名称无效或已是盟友
 */
bool Guild::declare_guild_war(std::string guild_name, std::uint64_t now_ms,
                              std::uint64_t remain_ms) {
  guild_name = normalize_name(std::move(guild_name));
  if (guild_name.empty() || is_ally_guild(guild_name)) {
    return false;
  }
  // 如果已经处于敌对状态，重置战争计时
  for (auto& war : hostile_guilds_) {
    if (equals_name(war.enemy_guild, guild_name)) {
      war.start_ms = now_ms;
      war.remain_ms = remain_ms;
      return true;
    }
  }
  // 新建敌对关系
  hostile_guilds_.push_back(GuildWarState{std::move(guild_name), now_ms, remain_ms});
  return true;
}

/**
 * @brief 移除敌对公会
 * @param guild_name 敌对公会名称
 * @return true 成功移除，false 未处于敌对状态
 */
bool Guild::remove_hostile_guild(std::string_view guild_name) {
  const auto old_size = hostile_guilds_.size();
  hostile_guilds_.erase(std::remove_if(hostile_guilds_.begin(), hostile_guilds_.end(),
                                       [&](const GuildWarState& war) {
                                         return equals_name(war.enemy_guild, guild_name);
                                       }),
                        hostile_guilds_.end());
  return hostile_guilds_.size() != old_size;
}

/**
 * @brief 到期结束公会战
 * @details 遍历敌对列表，找出所有已超时的战争：
 *          now_ms - start_ms > remain_ms 即为超时。
 *          将超时的敌对公会从列表中移除并返回。
 * @param now_ms 当前时间戳
 * @return 到期结束战争的目标公会名称列表
 */
std::vector<std::string> Guild::expire_guild_wars(std::uint64_t now_ms) {
  std::vector<std::string> expired;
  for (auto it = hostile_guilds_.begin(); it != hostile_guilds_.end();) {
    if (now_ms - it->start_ms > it->remain_ms) {
      expired.push_back(it->enemy_guild);
      it = hostile_guilds_.erase(it);
    } else {
      ++it;
    }
  }
  return expired;
}

/**
 * @brief 设置公会公告
 * @param notice_lines 公告文本行列表
 */
void Guild::set_notice_lines(std::vector<std::string> notice_lines) {
  notice_lines_ = std::move(notice_lines);
}

/**
 * @brief 确保指定等级的分组存在
 * @details 查找指定等级的分组，如果不存在则创建。
 *          会长等级（kGuildLordRank=1）插在 ranks_ 最前面，其他等级追加到末尾。
 *          如果提供了等级名称且分组已存在，则更新分组名及所有成员的等级名称。
 * @param rank 等级编号
 * @param rank_name 等级名称
 * @return 对应等级分组的引用
 */
GuildRankGroup& Guild::ensure_rank(std::uint8_t rank, std::string rank_name) {
  rank_name = util::trim(std::move(rank_name));
  // 查找是否已有该等级分组
  const auto it = std::find_if(ranks_.begin(), ranks_.end(),
                               [&](const GuildRankGroup& group) {
                                 return group.rank == rank;
                               });
  if (it != ranks_.end()) {
    // 如果提供了等级名称，更新分组名及该组所有成员的等级名
    if (!rank_name.empty()) {
      it->rank_name = std::move(rank_name);
      for (auto& member : it->members) {
        member.rank_name = it->rank_name;
      }
    }
    return *it;
  }

  // 创建新的等级分组
  GuildRankGroup group;
  group.rank = rank;
  group.rank_name = std::move(rank_name);
  // 会长等级插入到最前面，其他等级追加到末尾
  if (rank == kGuildLordRank) {
    return *ranks_.insert(ranks_.begin(), std::move(group));
  }
  ranks_.push_back(std::move(group));
  return ranks_.back();
}

/**
 * @brief 转让会长职位
 * @details 实现逻辑：
 *          1. 验证当前会长和目标成员的合法性
 *          2. 从原分组中移除目标成员，记录其信息
 *          3. 移除原会长
 *          4. 将目标成员插入会长等级组
 *          5. 将原会长作为普通成员重新加入
 * @param current_lord 当前会长名称
 * @param target_name 目标成员名称
 * @param old_lord_rank_name 原会长降级后的等级名称
 * @param new_lord_rank_name 新会长的等级名称
 * @return true 成功，false 验证失败
 */
bool Guild::transfer_lord(std::string_view current_lord, std::string_view target_name,
                          std::string old_lord_rank_name, std::string new_lord_rank_name) {
  // 验证：当前会长必须真是会长，目标不能是会长，目标必须是成员
  if (!is_lord(current_lord) || is_lord(target_name) || find_member(target_name) == nullptr) {
    return false;
  }

  // 从原等级分组中移除目标成员
  std::string target_copy;
  std::uint64_t target_actor_id = 0;
  bool target_hears_guild_chat = true;
  for (auto rank_it = ranks_.begin(); rank_it != ranks_.end(); ++rank_it) {
    auto& members = rank_it->members;
    const auto member_it = std::find_if(members.begin(), members.end(),
                                        [&](const GuildMember& member) {
                                          return equals_name(member.name, target_name);
                                        });
    if (member_it == members.end()) {
      continue;
    }
    target_copy = member_it->name;
    target_actor_id = member_it->online_actor_id;
    target_hears_guild_chat = member_it->hears_guild_chat;
    members.erase(member_it);
    if (members.empty()) {
      ranks_.erase(rank_it);
    }
    break;
  }

  // 记录原会长信息
  auto* old_lord = find_member(current_lord);
  if (old_lord == nullptr || target_copy.empty()) {
    return false;
  }
  const auto old_lord_name = old_lord->name;
  const auto old_lord_actor_id = old_lord->online_actor_id;
  const auto old_lord_hears_guild_chat = old_lord->hears_guild_chat;
  remove_member(current_lord);

  // 将目标成员插入会长等级分组
  auto& lord_rank = ensure_rank(kGuildLordRank, std::move(new_lord_rank_name));
  lord_rank.members.insert(lord_rank.members.begin(),
                           GuildMember{target_copy, kGuildLordRank, lord_rank.rank_name,
                                       target_actor_id, target_hears_guild_chat});
  // 将原会长作为普通成员重新加入
  if (add_member(old_lord_name, std::move(old_lord_rank_name), old_lord_actor_id)) {
    set_member_hears_guild_chat(old_lord_name, old_lord_hears_guild_chat);
  }
  return true;
}

// ============================================================================
// GuildManager 成员方法实现
// ============================================================================

/**
 * @brief 按名称查找公会（可变版本）
 * @param name 公会名称
 * @return 公会指针，未找到返回 nullptr
 */
Guild* GuildManager::find_guild(std::string_view name) {
  for (auto& guild : guilds_) {
    if (equals_name(guild.name(), name)) {
      return &guild;
    }
  }
  return nullptr;
}

/**
 * @brief 按名称查找公会（const版本）
 * @param name 公会名称
 * @return 公会指针，未找到返回 nullptr
 */
const Guild* GuildManager::find_guild(std::string_view name) const {
  for (const auto& guild : guilds_) {
    if (equals_name(guild.name(), name)) {
      return &guild;
    }
  }
  return nullptr;
}

/**
 * @brief 按成员名称查找所属公会（可变版本）
 * @param member_name 成员名称
 * @return 公会指针，成员不在任何公会中返回 nullptr
 */
Guild* GuildManager::find_guild_by_member(std::string_view member_name) {
  for (auto& guild : guilds_) {
    if (guild.has_member(member_name)) {
      return &guild;
    }
  }
  return nullptr;
}

/**
 * @brief 按成员名称查找所属公会（const版本）
 * @param member_name 成员名称
 * @return 公会指针，成员不在任何公会中返回 nullptr
 */
const Guild* GuildManager::find_guild_by_member(std::string_view member_name) const {
  for (const auto& guild : guilds_) {
    if (guild.has_member(member_name)) {
      return &guild;
    }
  }
  return nullptr;
}

/**
 * @brief 创建公会
 * @details 创建条件：
 *          1. 公会名称不能为空
 *          2. 会长名称不能为空
 *          3. 公会名不能与已有公会重复
 *          4. 会长不能已在其他公会中
 * @param name 公会名称
 * @param lord_name 会长名称
 * @param lord_rank_name 会长等级名称
 * @param lord_actor_id 会长角色ID
 * @return 创建成功的公会指针，失败返回 nullptr
 */
Guild* GuildManager::create_guild(std::string name, std::string lord_name,
                                  std::string lord_rank_name,
                                  std::uint64_t lord_actor_id) {
  name = normalize_name(std::move(name));
  lord_name = normalize_name(std::move(lord_name));
  if (name.empty() || lord_name.empty() || find_guild(name) != nullptr ||
      find_guild_by_member(lord_name) != nullptr) {
    return nullptr;
  }
  Guild guild{name};
  if (!guild.add_lord(std::move(lord_name), std::move(lord_rank_name), lord_actor_id)) {
    return nullptr;
  }
  guilds_.push_back(std::move(guild));
  return &guilds_.back();
}

/**
 * @brief 删除公会
 * @param name 公会名称
 * @return true 成功删除，false 公会不存在
 */
bool GuildManager::erase_guild(std::string_view name) {
  const auto old_size = guilds_.size();
  guilds_.erase(std::remove_if(guilds_.begin(), guilds_.end(),
                               [&](const Guild& guild) {
                                 return equals_name(guild.name(), name);
                               }),
                guilds_.end());
  return guilds_.size() != old_size;
}

/**
 * @brief 由会长添加成员
 * @details 验证流程：
 *          1. 公会在且请求者是会长
 *          2. 目标名称有效
 *          3. 目标在线且面对邀请者
 *          4. 目标允许加入公会
 *          5. 目标尚未加入任何公会
 *          6. 公会成员数未达上限
 * @param guild_name 公会名称
 * @param requester_name 请求者名称
 * @param target_name 目标成员名称
 * @param context 添加成员的上下文约束
 * @return GuildMemberOpResult 操作结果
 */
GuildMemberOpResult GuildManager::add_member_by_lord(
    std::string_view guild_name, std::string_view requester_name, std::string target_name,
    const GuildAddMemberContext& context) {
  auto* guild = find_guild(guild_name);
  if (guild == nullptr) {
    return GuildMemberOpResult::guild_not_found;
  }
  if (!guild->is_lord(requester_name)) {
    return GuildMemberOpResult::requester_not_lord;
  }
  target_name = normalize_name(std::move(target_name));
  if (target_name.empty()) {
    return GuildMemberOpResult::target_not_found;
  }
  if (!context.target_online) {
    return GuildMemberOpResult::target_not_online;
  }
  if (!context.target_facing_requester) {
    return GuildMemberOpResult::target_not_facing_requester;
  }
  if (!context.target_allows_guild) {
    return GuildMemberOpResult::target_rejects_guild;
  }
  if (guild->has_member(target_name)) {
    return GuildMemberOpResult::already_member;
  }
  if (find_guild_by_member(target_name) != nullptr) {
    return GuildMemberOpResult::target_in_other_guild;
  }
  if (context.max_member_count > 0 && guild->member_count() >= context.max_member_count) {
    return GuildMemberOpResult::member_limit_reached;
  }
  return guild->add_member(std::move(target_name), "Guild Member", context.target_actor_id)
             ? GuildMemberOpResult::ok
             : GuildMemberOpResult::target_not_found;
}

/**
 * @brief 由会长移除成员
 * @details 如果被移除的是会长自己，或者移除后公会为空，则自动删除公会。
 * @param guild_name 公会名称
 * @param requester_name 请求者（会长）名称
 * @param target_name 目标成员名称
 * @return GuildMemberOpResult 操作结果
 */
GuildMemberOpResult GuildManager::remove_member_by_lord(std::string_view guild_name,
                                                        std::string_view requester_name,
                                                        std::string_view target_name) {
  auto* guild = find_guild(guild_name);
  if (guild == nullptr) {
    return GuildMemberOpResult::guild_not_found;
  }
  if (!guild->is_lord(requester_name)) {
    return GuildMemberOpResult::requester_not_lord;
  }
  if (!guild->has_member(target_name)) {
    return GuildMemberOpResult::not_member;
  }
  const auto remove_self = equals_name(requester_name, target_name);
  if (!guild->remove_member(target_name)) {
    return GuildMemberOpResult::not_member;
  }
  // 如果公会变空或会长退出（解散公会），删除公会
  if (guild->empty() || remove_self) {
    erase_guild(guild_name);
  }
  return GuildMemberOpResult::ok;
}

/**
 * @brief 成员主动退出公会
 * @param member_name 成员名称
 * @return GuildMemberOpResult 操作结果
 * @note 会长不能通过此方法退出，会返回 lord_cannot_leave。
 */
GuildMemberOpResult GuildManager::leave_member(std::string_view member_name) {
  auto* guild = find_guild_by_member(member_name);
  if (guild == nullptr) {
    return GuildMemberOpResult::not_member;
  }
  if (guild->is_lord(member_name)) {
    return GuildMemberOpResult::lord_cannot_leave;
  }
  return guild->remove_member(member_name) ? GuildMemberOpResult::ok
                                           : GuildMemberOpResult::not_member;
}

/**
 * @brief 转让会长职位
 * @param guild_name 公会名称
 * @param requester_name 请求者（原会长）名称
 * @param target_name 目标成员名称
 * @return GuildMemberOpResult 操作结果
 */
GuildMemberOpResult GuildManager::transfer_lord(std::string_view guild_name,
                                                std::string_view requester_name,
                                                std::string_view target_name) {
  auto* guild = find_guild(guild_name);
  if (guild == nullptr) {
    return GuildMemberOpResult::guild_not_found;
  }
  if (!guild->is_lord(requester_name)) {
    return GuildMemberOpResult::requester_not_lord;
  }
  if (!guild->has_member(target_name)) {
    return GuildMemberOpResult::not_member;
  }
  if (guild->is_lord(target_name)) {
    return GuildMemberOpResult::target_is_lord;
  }
  return guild->transfer_lord(requester_name, target_name) ? GuildMemberOpResult::ok
                                                           : GuildMemberOpResult::not_member;
}

/**
 * @brief 获取公会聊天投递列表
 * @param guild_name 公会名称
 * @param speaker_name 发言者名称
 * @param text 消息内容
 * @return 投递消息列表
 */
std::vector<GuildChatDelivery> GuildManager::guild_chat_deliveries(
    std::string_view guild_name, std::string_view speaker_name, std::string_view text) const {
  const auto* guild = find_guild(guild_name);
  if (guild == nullptr) {
    return {};
  }
  return guild->guild_chat_deliveries(speaker_name, text);
}

/**
 * @brief 公会结盟
 * @details 双向结盟流程：
 *          1. 双方公会均存在
 *          2. 不能与自己结盟
 *          3. 双方请求者均须是会长
 *          4. 目标公会不拒绝结盟
 *          5. 双方不处于敌对状态
 *          6. 双方尚未结盟
 *          满足条件后，双方互相加入盟友列表。
 * @param requester_guild 请求方公会
 * @param requester_name 请求方会长
 * @param target_guild 目标方公会
 * @param target_name 目标方会长
 * @return GuildRelationOpResult 操作结果
 */
GuildRelationOpResult GuildManager::make_ally(std::string_view requester_guild,
                                              std::string_view requester_name,
                                              std::string_view target_guild,
                                              std::string_view target_name) {
  auto* requester = find_guild(requester_guild);
  if (requester == nullptr) {
    return GuildRelationOpResult::guild_not_found;
  }
  auto* target = find_guild(target_guild);
  if (target == nullptr) {
    return GuildRelationOpResult::target_guild_not_found;
  }
  if (equals_name(requester->name(), target->name())) {
    return GuildRelationOpResult::same_guild;
  }
  if (!requester->is_lord(requester_name)) {
    return GuildRelationOpResult::requester_not_lord;
  }
  if (!target->is_lord(target_name)) {
    return GuildRelationOpResult::target_not_lord;
  }
  if (!target->allow_ally_guild()) {
    return GuildRelationOpResult::target_rejects_ally;
  }
  if (requester->is_hostile_guild(target->name()) || target->is_hostile_guild(requester->name())) {
    return GuildRelationOpResult::hostile_guild;
  }
  if (requester->is_ally_guild(target->name()) || target->is_ally_guild(requester->name())) {
    return GuildRelationOpResult::already_allied;
  }
  requester->make_ally_guild(target->name());
  target->make_ally_guild(requester->name());
  return GuildRelationOpResult::ok;
}

/**
 * @brief 解除同盟
 * @details 单向解除即可，不需要对方同意。双方互相从盟友列表中移除。
 * @param requester_guild 请求方公会
 * @param requester_name 请求方会长
 * @param target_guild 目标公会
 * @return GuildRelationOpResult 操作结果
 */
GuildRelationOpResult GuildManager::break_ally(std::string_view requester_guild,
                                               std::string_view requester_name,
                                               std::string_view target_guild) {
  auto* requester = find_guild(requester_guild);
  if (requester == nullptr) {
    return GuildRelationOpResult::guild_not_found;
  }
  auto* target = find_guild(target_guild);
  if (target == nullptr) {
    return GuildRelationOpResult::target_guild_not_found;
  }
  if (!requester->is_lord(requester_name)) {
    return GuildRelationOpResult::requester_not_lord;
  }
  if (!requester->is_ally_guild(target->name())) {
    return GuildRelationOpResult::not_allied;
  }
  requester->break_ally_guild(target->name());
  target->break_ally_guild(requester->name());
  return GuildRelationOpResult::ok;
}

/**
 * @brief 宣战
 * @details 双向宣战流程：
 *          1. 双方公会必须存在且不是同一公会
 *          2. 请求者必须是会长
 *          3. 不能对盟友宣战
 *          4. 成功向目标宣战后，再让目标方也向请求方宣战
 *          5. 如果目标方向请求方宣战失败（如恰好是盟友），则回滚请求方的宣战
 * @param requester_guild 宣战方公会
 * @param requester_name 宣战方会长
 * @param target_guild 目标公会
 * @param now_ms 当前时间戳
 * @param remain_ms 战争持续时间
 * @return GuildRelationOpResult 操作结果
 */
GuildRelationOpResult GuildManager::declare_guild_war(std::string_view requester_guild,
                                                      std::string_view requester_name,
                                                      std::string_view target_guild,
                                                      std::uint64_t now_ms,
                                                      std::uint64_t remain_ms) {
  auto* requester = find_guild(requester_guild);
  if (requester == nullptr) {
    return GuildRelationOpResult::guild_not_found;
  }
  auto* target = find_guild(target_guild);
  if (target == nullptr) {
    return GuildRelationOpResult::target_guild_not_found;
  }
  if (equals_name(requester->name(), target->name())) {
    return GuildRelationOpResult::same_guild;
  }
  if (!requester->is_lord(requester_name)) {
    return GuildRelationOpResult::requester_not_lord;
  }
  if (requester->is_ally_guild(target->name()) || target->is_ally_guild(requester->name())) {
    return GuildRelationOpResult::already_allied;
  }
  if (!requester->declare_guild_war(target->name(), now_ms, remain_ms)) {
    return GuildRelationOpResult::already_allied;
  }
  // 双向宣战：如果目标方宣战失败，回滚请求方的宣战
  if (!target->declare_guild_war(requester->name(), now_ms, remain_ms)) {
    requester->remove_hostile_guild(target->name());
    return GuildRelationOpResult::already_allied;
  }
  return GuildRelationOpResult::ok;
}

/**
 * @brief 到期结束所有公会的战争
 * @details 遍历所有公会，检查并移除已超时的敌对关系。
 *          同时自动从对方的敌对列表中移除本方。
 * @param now_ms 当前时间戳
 * @return 到期结束的战争列表，每项格式为"公会A/公会B"
 */
std::vector<std::string> GuildManager::expire_guild_wars(std::uint64_t now_ms) {
  std::vector<std::string> expired_pairs;
  for (auto& guild : guilds_) {
    const auto expired = guild.expire_guild_wars(now_ms);
    for (const auto& enemy_name : expired) {
      // 同时从对方的敌对列表中移除本方
      if (auto* enemy = find_guild(enemy_name); enemy != nullptr) {
        enemy->remove_hostile_guild(guild.name());
      }
      expired_pairs.push_back(guild.name() + "/" + enemy_name);
    }
  }
  return expired_pairs;
}

}  // namespace mir2
