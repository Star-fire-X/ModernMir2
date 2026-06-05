/**
 * @file castle_manager.hpp
 * @brief 城堡/沙巴克攻城战管理系统头文件
 * @details 定义了攻城战相关的数据结构（报名、占领、事件类型）以及城堡管理器 CastleManager，
 *          用于管理城堡的归属、攻城战的发起、进行和结束等完整生命周期。
 *          攻城战流程：公会报名 -> 到期自动开战 -> 城堡核心争夺 -> 占领或超时结束。
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "config/models.hpp"

namespace mir2 {

/**
 * @struct CastleCoreOccupant
 * @brief 城堡核心区域的占领者信息
 * @details 表示在城堡核心区域内的某个角色所属公会及存活状态。
 *          只有存活且同公会的成员同时占据核心时，才能成功占领城堡。
 */
struct CastleCoreOccupant {
  std::string guild_name{}; ///< 占领者所属公会名称
  bool alive{true};         ///< 占领者是否存活
};

/**
 * @enum CastleWarEventType
 * @brief 攻城战事件类型枚举
 * @details 定义攻城战生命周期中可能触发的各类事件，用于通知外部系统。
 */
enum class CastleWarEventType {
  start,           ///< 攻城战开始
  timeout_warning, ///< 攻城战即将超时警告
  owner_changed,   ///< 城堡所有者变更
  finish           ///< 攻城战结束
};

/**
 * @struct CastleWarEvent
 * @brief 攻城战事件数据结构
 * @details 包含事件类型和关联的公会名称，由城堡管理器在攻城战过程中生成。
 */
struct CastleWarEvent {
  CastleWarEventType type{}; ///< 事件类型
  std::string guild_name{};  ///< 关联公会名称
};

/**
 * @enum CastleWarOpResult
 * @brief 攻城战操作结果枚举
 * @details 定义攻城战相关操作的返回结果状态码，用于向调用者反馈操作成败及具体原因。
 */
enum class CastleWarOpResult {
  ok,                     ///< 操作成功
  requester_not_lord,     ///< 请求者不是公会会长
  missing_zuma_piece,     ///< 缺少祖玛头像（攻城道具）
  already_owner,          ///< 已经是城堡所有者
  already_registered,     ///< 已经报名
  no_due_war,             ///< 没有到期的攻城战
  already_under_attack,   ///< 已经在攻城战中
  not_under_attack,       ///< 当前不在攻城战中
  empty_guild,            ///< 公会名称为空
  guild_not_registered,   ///< 公会未报名攻城战
  occupation_too_early,   ///< 占领时间过早（尚在初始保护期内）
  core_not_controlled     ///< 核心区域未被己方完全控制
};

/**
 * @class CastleManager
 * @brief 城堡/沙巴克攻城战管理器
 * @details 核心职责：
 *          1. 管理城堡的归属（所有者公会和城主名称）
 *          2. 处理攻城战报名（验证公会会长身份、祖玛头像等条件）
 *          3. 按计划日期自动触发攻城战
 *          4. 处理城堡核心区域的占领逻辑
 *          5. 管理攻城战的进行、超时警告和结束
 *
 *          时间常量说明：
 *          - kOccupationDelayMs: 开战后的最短占领等待时间（10分钟）
 *          - kWarDurationMs: 攻城战最大持续时间（3小时）
 *          - kTimeoutWarningLeadMs: 超时前提前告警时间（10分钟）
 */
class CastleManager {
 public:
  /** @brief 开战后可占领城堡的最短等待时间（10分钟） */
  static constexpr std::uint64_t kOccupationDelayMs = 10ULL * 60ULL * 1000ULL;
  /** @brief 攻城战最大持续时间（3小时） */
  static constexpr std::uint64_t kWarDurationMs = 3ULL * 60ULL * 60ULL * 1000ULL;
  /** @brief 超时前提前发出警告的时间（10分钟） */
  static constexpr std::uint64_t kTimeoutWarningLeadMs = 10ULL * 60ULL * 1000ULL;

  /**
   * @brief 构造函数
   * @param castle_name 城堡名称，默认值为 "Sabuk"（沙巴克）
   */
  explicit CastleManager(std::string castle_name = "Sabuk");

  // @{ 访问器方法
  [[nodiscard]] const std::string& castle_name() const { return castle_name_; }             ///< 获取城堡名称
  [[nodiscard]] const std::string& owner_guild() const { return owner_guild_; }             ///< 获取所有者公会名称
  [[nodiscard]] const std::string& owner_lord() const { return owner_lord_; }               ///< 获取城主名称
  [[nodiscard]] const std::vector<CastleWarRegistration>& registrations() const {
    return registrations_;                                                                   ///< 获取攻城战报名列表
  }
  [[nodiscard]] const std::vector<std::string>& rush_guilds() const { return rush_guilds_; } ///< 获取当前参战公会列表
  [[nodiscard]] bool under_attack() const { return under_attack_; }                          ///< 是否正在被攻打
  [[nodiscard]] std::uint64_t castle_attack_started_ms() const {
    return castle_attack_started_ms_;                                                        ///< 获取攻城战开始时间戳
  }
  [[nodiscard]] CastleRuntimeState runtime_state() const;
  // @}

  /**
   * @brief 设置城堡所有者
   * @param guild_name 公会名称
   * @param lord_name 城主角色名称
   */
  void set_owner(std::string guild_name, std::string lord_name);
  void load_runtime_state(const CastleRuntimeState& state);

  /**
   * @brief 发起攻城战报名
   * @param guild_name 报名公会名称
   * @param requester_is_lord 请求者是否为公会会长
   * @param has_zuma_piece 是否持有祖玛头像
   * @param current_day 当前游戏天数
   * @param delay_days 报名后延迟天数，默认4天
   * @return CastleWarOpResult 操作结果
   */
  CastleWarOpResult propose_castle_war(std::string guild_name, bool requester_is_lord,
                                       bool has_zuma_piece, std::int32_t current_day,
                                       std::int32_t delay_days = 4);

  /**
   * @brief 启动到期的攻城战
   * @details 检查所有报名的公会，将在当前日期到期的报名转为实际的攻城战。
   *          同时将城堡所有者公会加入防守方。
   * @param current_day 当前游戏天数
   * @param now_ms 当前时间戳（毫秒）
   * @param events 输出参数，用于收集生成的攻城战事件
   * @return CastleWarOpResult 操作结果
   */
  CastleWarOpResult start_due_war(std::int32_t current_day, std::uint64_t now_ms,
                                  std::vector<CastleWarEvent>& events);

  /**
   * @brief 尝试占领城堡
   * @details 检查核心区域占领条件：公会必须在rush名单中、已过等待期、核心内只有己方存活成员。
   *          成功占领后，若只剩一个rush公会则直接结束攻城战。
   * @param guild_name 尝试占领的公会名称
   * @param core_occupants 核心区域内所有角色列表
   * @param now_ms 当前时间戳（毫秒）
   * @param events 输出参数，用于收集生成的事件
   * @return CastleWarOpResult 操作结果
   */
  CastleWarOpResult try_occupy(std::string guild_name,
                               const std::vector<CastleCoreOccupant>& core_occupants,
                               std::uint64_t now_ms,
                               std::vector<CastleWarEvent>& events);

  /**
   * @brief 攻城战心跳更新
   * @details 每帧调用，检查超时警告和攻城战到期结束逻辑。
   * @param now_ms 当前时间戳（毫秒）
   * @param events 输出参数，用于收集生成的事件
   */
  void run(std::uint64_t now_ms, std::vector<CastleWarEvent>& events);

  /**
   * @brief 结束攻城战
   * @details 重置攻城状态，清空rush公会列表，发送结束事件。
   * @param events 输出参数，用于收集生成的事件
   */
  void finish_war(std::vector<CastleWarEvent>& events);

 private:
  /**
   * @brief 检查公会是否已报名
   * @param guild_name 公会名称
   * @return true 已报名，false 未报名
   */
  [[nodiscard]] bool is_registered(std::string_view guild_name) const;

  /**
   * @brief 检查公会是否为当前rush参战方
   * @param guild_name 公会名称
   * @return true 是参战方，false 不是
   */
  [[nodiscard]] bool is_rush_guild(std::string_view guild_name) const;

  std::string castle_name_{};                             ///< 城堡名称
  std::string owner_guild_{};                             ///< 所有者公会名称
  std::string owner_lord_{};                              ///< 城主角色名称
  std::vector<CastleWarRegistration> registrations_{};    ///< 攻城战报名列表
  std::vector<std::string> rush_guilds_{};                ///< 当前攻城战参战公会列表
  bool under_attack_{false};                              ///< 是否正在被攻打
  bool timeout_warning_sent_{false};                      ///< 是否已发送超时警告
  std::uint64_t latest_war_start_ms_{0};                  ///< 最近一次攻城战开始时间戳
  std::uint64_t castle_attack_started_ms_{0};             ///< 当前攻城战开始时间戳（用于占领等待期计算）
};

}  // namespace mir2
