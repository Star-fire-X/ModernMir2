/**
 * @file castle_manager.cpp
 * @brief 城堡/沙巴克攻城战管理系统实现
 * @details 实现了城堡管理器的完整攻城战生命周期管理，
 *          包括报名、开战、占领、超时、结束等核心逻辑。
 *          所有公会名称和城堡名称均经过标准化处理（去除首尾空格、忽略大小写）。
 */

#include "world/castle_manager.hpp"

#include <algorithm>
#include <utility>

#include "util/string_utils.hpp"

namespace mir2 {

/**
 * @brief 匿名命名空间，定义内部辅助函数
 */
namespace {

/**
 * @brief 不区分大小写地比较两个名称是否相等
 * @details 将两个字符串均转换为小写后比较，实现大小写不敏感的匹配。
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
 * @return 去除首尾空格后的名称
 */
std::string normalize_name(std::string name) {
  return util::trim(std::move(name));
}

}  // namespace

/**
 * @brief 构造函数
 * @param castle_name 城堡名称，构造时自动标准化处理
 */
CastleManager::CastleManager(std::string castle_name)
    : castle_name_(normalize_name(std::move(castle_name))) {}

/**
 * @brief 设置城堡所有者信息
 * @param guild_name 公会名称
 * @param lord_name 城主角色名称
 * @note 此方法仅用于初始化或GM命令直接设置，不经过攻城战流程。
 */
void CastleManager::set_owner(std::string guild_name, std::string lord_name) {
  owner_guild_ = normalize_name(std::move(guild_name));
  owner_lord_ = normalize_name(std::move(lord_name));
}

CastleRuntimeState CastleManager::runtime_state() const {
  CastleRuntimeState state;
  state.under_attack = under_attack_;
  state.timeout_warning_sent = timeout_warning_sent_;
  state.latest_war_start_ms = latest_war_start_ms_;
  state.castle_attack_started_ms = castle_attack_started_ms_;
  state.rush_guilds = rush_guilds_;
  state.registrations = registrations_;
  return state;
}

void CastleManager::load_runtime_state(const CastleRuntimeState& state) {
  under_attack_ = state.under_attack;
  timeout_warning_sent_ = state.timeout_warning_sent;
  latest_war_start_ms_ = state.latest_war_start_ms;
  castle_attack_started_ms_ = state.castle_attack_started_ms;
  rush_guilds_ = state.rush_guilds;
  registrations_ = state.registrations;
}

/**
 * @brief 发起攻城战报名
 * @details 验证顺序（任一条件不满足即返回对应错误码）：
 *          1. 公会名不能为空
 *          2. 请求者必须是公会会长
 *          3. 不能是自己的城堡
 *          4. 必须持有祖玛头像
 *          5. 不能重复报名
 * @param guild_name 公会名称
 * @param requester_is_lord 请求者是否为会长
 * @param has_zuma_piece 是否持有祖玛头像
 * @param current_day 当前游戏天数
 * @param delay_days 报名后到开战的延迟天数，默认4天
 * @return CastleWarOpResult::ok 报名成功，其他值为具体失败原因
 */
CastleWarOpResult CastleManager::propose_castle_war(std::string guild_name,
                                                    bool requester_is_lord,
                                                    bool has_zuma_piece,
                                                    std::int32_t current_day,
                                                    std::int32_t delay_days) {
  guild_name = normalize_name(std::move(guild_name));
  if (guild_name.empty()) {
    return CastleWarOpResult::empty_guild;
  }
  if (!requester_is_lord) {
    return CastleWarOpResult::requester_not_lord;
  }
  if (equals_name(guild_name, owner_guild_)) {
    return CastleWarOpResult::already_owner;
  }
  if (!has_zuma_piece) {
    return CastleWarOpResult::missing_zuma_piece;
  }
  if (is_registered(guild_name)) {
    return CastleWarOpResult::already_registered;
  }
  registrations_.push_back(CastleWarRegistration{std::move(guild_name),
                                                 current_day + delay_days});
  return CastleWarOpResult::ok;
}

/**
 * @brief 启动到期的攻城战
 * @details 遍历报名列表，筛选出攻击日期等于当前日期的公会，
 *          将这些公会从报名列表移至rush参战列表。
 *          如果有城堡所有者且其不在rush列表中，也将所有者加入（作为防守方）。
 *          成功开战后记录开始时间戳并发送开始事件。
 * @param current_day 当前游戏天数
 * @param now_ms 当前时间戳
 * @param events 输出参数，收集生成的攻城战事件
 * @return CastleWarOpResult 操作结果
 * @note 如果当前没有到期报名，返回 no_due_war。
 *       如果已经在攻城战中，返回 already_under_attack。
 */
CastleWarOpResult CastleManager::start_due_war(std::int32_t current_day,
                                               std::uint64_t now_ms,
                                               std::vector<CastleWarEvent>& events) {
  if (under_attack_) {
    return CastleWarOpResult::already_under_attack;
  }

  // 筛选出所有到期（attack_day == current_day）的报名公会
  std::vector<std::string> attackers;
  for (auto it = registrations_.begin(); it != registrations_.end();) {
    if (it->attack_day == current_day) {
      attackers.push_back(it->guild_name);
      it = registrations_.erase(it);
    } else {
      ++it;
    }
  }
  if (attackers.empty()) {
    return CastleWarOpResult::no_due_war;
  }

  // 将攻击方设为rush公会，同时将防守方（所有者）加入
  rush_guilds_ = std::move(attackers);
  if (!owner_guild_.empty() && !is_rush_guild(owner_guild_)) {
    rush_guilds_.push_back(owner_guild_);
  }
  under_attack_ = true;
  timeout_warning_sent_ = false;
  latest_war_start_ms_ = now_ms;
  castle_attack_started_ms_ = now_ms;
  events.push_back(CastleWarEvent{CastleWarEventType::start, castle_name_});
  return CastleWarOpResult::ok;
}

/**
 * @brief 尝试占领城堡
 * @details 核心占领逻辑：
 *          1. 必须在攻城战进行中
 *          2. 公会必须在rush参战列表中
 *          3. 不能是当前所有者（攻方才能占领）
 *          4. 必须已过占领等待期（kOccupationDelayMs）
 *          5. 核心区域内必须只有己方存活成员（有且仅有己方存活者）
 *
 *          成功占领后，如果rush列表中只剩一方（即只剩新所有者），
 *          则自动结束攻城战。
 * @param guild_name 尝试占领的公会
 * @param core_occupants 核心区域内的角色列表
 * @param now_ms 当前时间戳
 * @param events 输出参数，收集事件
 * @return CastleWarOpResult 操作结果
 */
CastleWarOpResult CastleManager::try_occupy(
    std::string guild_name, const std::vector<CastleCoreOccupant>& core_occupants,
    std::uint64_t now_ms, std::vector<CastleWarEvent>& events) {
  guild_name = normalize_name(std::move(guild_name));
  if (!under_attack_) {
    return CastleWarOpResult::not_under_attack;
  }
  if (guild_name.empty()) {
    return CastleWarOpResult::empty_guild;
  }
  if (equals_name(guild_name, owner_guild_)) {
    return CastleWarOpResult::already_owner;
  }
  if (!is_rush_guild(guild_name)) {
    return CastleWarOpResult::guild_not_registered;
  }
  if (now_ms < castle_attack_started_ms_ + kOccupationDelayMs) {
    return CastleWarOpResult::occupation_too_early;
  }

  /**
   * 检查核心占领条件：
   * - 所有存活成员必须属于尝试占领的公会
   * - 必须至少有一个存活的核心成员
   * - 如果存在其他公会的存活成员，则不能占领
   */
  bool candidate_present = false;
  for (const auto& occupant : core_occupants) {
    if (!occupant.alive) {
      continue;
    }
    if (!equals_name(occupant.guild_name, guild_name)) {
      return CastleWarOpResult::core_not_controlled;
    }
    candidate_present = true;
  }
  if (!candidate_present) {
    return CastleWarOpResult::core_not_controlled;
  }

  // 执行占领：更新所有者，清空城主（需要重新指定）
  owner_guild_ = guild_name;
  owner_lord_.clear();
  events.push_back(CastleWarEvent{CastleWarEventType::owner_changed, owner_guild_});
  // 如果只剩一方，自动结束攻城战
  if (rush_guilds_.size() <= 1) {
    finish_war(events);
  }
  return CastleWarOpResult::ok;
}

/**
 * @brief 攻城战心跳更新
 * @details 每帧由外部系统调用，执行以下检查：
 *          1. 如果未到超时警告时间且即将超时，发送超时警告
 *          2. 如果已超时，强制结束攻城战
 * @param now_ms 当前时间戳
 * @param events 输出参数，收集事件
 */
void CastleManager::run(std::uint64_t now_ms, std::vector<CastleWarEvent>& events) {
  if (!under_attack_) {
    return;
  }
  // 检查是否需要发送超时警告（在超时前 kTimeoutWarningLeadMs 时触发）
  if (!timeout_warning_sent_ &&
      now_ms >= latest_war_start_ms_ + kWarDurationMs - kTimeoutWarningLeadMs) {
    timeout_warning_sent_ = true;
    events.push_back(CastleWarEvent{CastleWarEventType::timeout_warning, castle_name_});
  }
  // 检查攻城战是否已超时
  if (now_ms >= latest_war_start_ms_ + kWarDurationMs) {
    finish_war(events);
  }
}

/**
 * @brief 结束攻城战
 * @details 重置攻城状态：取消攻击标记、清空rush列表、发送结束事件。
 * @param events 输出参数，收集结束事件
 */
void CastleManager::finish_war(std::vector<CastleWarEvent>& events) {
  if (!under_attack_) {
    return;
  }
  under_attack_ = false;
  rush_guilds_.clear();
  events.push_back(CastleWarEvent{CastleWarEventType::finish, castle_name_});
}

/**
 * @brief 检查公会是否已在报名列表中
 * @details 使用不区分大小写的名称比较
 * @param guild_name 公会名称
 * @return true 已报名，false 未报名
 */
bool CastleManager::is_registered(std::string_view guild_name) const {
  return std::any_of(registrations_.begin(), registrations_.end(),
                     [&](const CastleWarRegistration& registration) {
                       return equals_name(registration.guild_name, guild_name);
                     });
}

/**
 * @brief 检查公会是否为当前rush参战方
 * @details 使用不区分大小写的名称比较
 * @param guild_name 公会名称
 * @return true 是参战方，false 不是
 */
bool CastleManager::is_rush_guild(std::string_view guild_name) const {
  return std::any_of(rush_guilds_.begin(), rush_guilds_.end(),
                     [&](const std::string& rush_guild) {
                       return equals_name(rush_guild, guild_name);
                     });
}

}  // namespace mir2
