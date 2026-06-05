/**
 * @file guild_manager.hpp
 * @brief 公会管理系统头文件
 * @details 定义了公会系统的核心数据结构和管理器。
 *          包含公会成员管理、职位体系、外交关系（结盟/敌对）、
 *          公会聊天、公会战等完整功能。
 *          公会内部采用等级（Rank）体系组织成员，等级1为会长，等级99为默认成员。
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mir2 {

/** @brief 会长等级常量 */
inline constexpr std::uint8_t kGuildLordRank = 1;
/** @brief 默认成员等级常量 */
inline constexpr std::uint8_t kGuildDefaultRank = 99;

/**
 * @struct GuildMember
 * @brief 公会成员数据结构
 * @details 记录公会成员的基本信息，包括名称、等级、职位名称、在线状态和聊天设置。
 */
struct GuildMember {
  std::string name{};               ///< 成员名称
  std::uint8_t rank{kGuildDefaultRank}; ///< 公会等级（1=会长，99=默认成员）
  std::string rank_name{};          ///< 等级名称（如"会长"、"成员"）
  std::uint64_t online_actor_id{0}; ///< 在线角色ID，0表示离线
  bool hears_guild_chat{true};      ///< 是否接收公会聊天
};

/**
 * @struct GuildChatDelivery
 * @brief 公会聊天投递数据结构
 * @details 表示一条需要发送给特定在线成员的公会聊天消息。
 */
struct GuildChatDelivery {
  std::string member_name{};   ///< 目标成员名称
  std::uint64_t online_actor_id{0}; ///< 目标成员在线角色ID
  std::string text{};          ///< 聊天消息内容（格式："发送者:消息内容"）
};

/**
 * @struct GuildRankGroup
 * @brief 公会等级分组数据结构
 * @details 按等级分组的公会成员集合，每个等级组包含该等级的所有成员。
 */
struct GuildRankGroup {
  std::uint8_t rank{kGuildDefaultRank}; ///< 等级编号
  std::string rank_name{};              ///< 等级名称
  std::vector<GuildMember> members{};   ///< 该等级下的成员列表
};

/**
 * @struct GuildWarState
 * @brief 公会战状态数据结构
 * @details 记录与某个敌对公会的战争状态，包括开始时间、持续时间等。
 */
struct GuildWarState {
  std::string enemy_guild{};       ///< 敌对公会名称
  std::uint64_t start_ms{0};       ///< 开战时间戳（毫秒）
  std::uint64_t remain_ms{3 * 60 * 60 * 1000}; ///< 战争持续时间（默认3小时）
};

/**
 * @enum GuildMemberOpResult
 * @brief 公会成员操作结果枚举
 * @details 定义公会成员相关操作的返回结果状态码。
 */
enum class GuildMemberOpResult {
  ok,                        ///< 操作成功
  guild_not_found,           ///< 公会不存在
  requester_not_lord,        ///< 请求者不是会长
  target_not_found,          ///< 目标不存在
  target_not_online,         ///< 目标不在线
  target_not_facing_requester, ///< 目标未面向请求者
  target_rejects_guild,      ///< 目标拒绝加入公会
  already_member,            ///< 已是公会成员
  target_in_other_guild,     ///< 目标已在其他公会
  member_limit_reached,      ///< 公会成员数量已达上限
  not_member,                ///< 不是公会成员
  lord_cannot_leave,         ///< 会长不能退出公会（需先转让）
  target_is_lord             ///< 目标是会长
};

/**
 * @enum GuildRelationOpResult
 * @brief 公会外交关系操作结果枚举
 * @details 定义公会间关系操作（结盟、宣战等）的返回结果状态码。
 */
enum class GuildRelationOpResult {
  ok,                  ///< 操作成功
  guild_not_found,     ///< 公会不存在
  target_guild_not_found, ///< 目标公会不存在
  requester_not_lord,  ///< 请求者不是会长
  target_not_lord,     ///< 目标不是会长
  same_guild,          ///< 不能操作自身公会
  already_allied,      ///< 已经结盟
  not_allied,          ///< 未结盟
  target_rejects_ally, ///< 目标拒绝结盟
  hostile_guild        ///< 双方处于敌对状态
};

/**
 * @struct GuildAddMemberContext
 * @brief 添加公会成员的上下文参数
 * @details 包含添加成员时的环境约束条件，用于验证是否满足入会条件。
 */
struct GuildAddMemberContext {
  bool target_online{true};                  ///< 目标是否在线
  bool target_facing_requester{true};        ///< 目标是否面向邀请者
  bool target_allows_guild{true};            ///< 目标是否允许加入公会
  std::uint64_t target_actor_id{0};          ///< 目标角色ID
  std::size_t max_member_count{0};           ///< 公会最大成员数（0表示无限制）
};

/**
 * @class Guild
 * @brief 公会实体类
 * @details 表示一个完整的公会，包含：
 *          - 公会名称与公告
 *          - 成员管理（等级体系、职位名称）
 *          - 外交关系（盟友列表、敌对列表）
 *          - 公会聊天分发
 *          - 公会战状态管理
 *
 *          公会内部使用 RankGroup 按等级组织成员，等级值越小权限越高。
 *          等级1（kGuildLordRank）为会长，具有管理权限。
 */
class Guild {
 public:
  Guild() = default;
  /**
   * @brief 构造函数
   * @param name 公会名称
   */
  explicit Guild(std::string name);

  // @{ 访问器方法
  [[nodiscard]] const std::string& name() const { return name_; }                                   ///< 获取公会名称
  [[nodiscard]] const std::vector<std::string>& notice_lines() const { return notice_lines_; }       ///< 获取公告内容
  [[nodiscard]] const std::vector<std::string>& ally_guilds() const { return ally_guilds_; }         ///< 获取盟友公会列表
  [[nodiscard]] const std::vector<GuildWarState>& hostile_guilds() const { return hostile_guilds_; } ///< 获取敌对公会列表
  [[nodiscard]] const std::vector<GuildRankGroup>& ranks() const { return ranks_; }                  ///< 获取等级分组列表
  [[nodiscard]] bool allow_ally_guild() const { return allow_ally_guild_; }                          ///< 是否允许结盟
  [[nodiscard]] std::size_t member_count() const;                                                    ///< 获取成员总数
  [[nodiscard]] bool empty() const { return member_count() == 0; }                                   ///< 公会是否为空
  [[nodiscard]] std::optional<std::string_view> lord_name() const;                                   ///< 获取会长名称
  [[nodiscard]] const GuildMember* find_member(std::string_view name) const;                         ///< 查找成员（const版本）
  [[nodiscard]] GuildMember* find_member(std::string_view name);                                     ///< 查找成员（可变版本）
  [[nodiscard]] bool has_member(std::string_view name) const { return find_member(name) != nullptr; }///< 是否包含某成员
  [[nodiscard]] bool is_lord(std::string_view name) const;                                           ///< 是否为会长
  [[nodiscard]] std::vector<GuildChatDelivery> guild_chat_deliveries(                                ///< 获取公会聊天投递列表
      std::string_view speaker_name, std::string_view text) const;
  [[nodiscard]] std::vector<std::uint64_t> online_member_actor_ids() const;                          ///< 获取所有在线成员角色ID
  [[nodiscard]] bool is_ally_guild(std::string_view guild_name) const;                               ///< 是否盟友公会
  [[nodiscard]] bool is_hostile_guild(std::string_view guild_name) const;                            ///< 是否敌对公会
  // @}

  /**
   * @brief 添加会长（仅初始化时将第一个成员设为会长时使用）
   * @param name 会长名称
   * @param rank_name 等级名称
   * @param online_actor_id 在线角色ID
   * @return true 成功，false 失败
   */
  bool add_lord(std::string name, std::string rank_name = "Guild Lord",
                std::uint64_t online_actor_id = 0);

  /**
   * @brief 添加普通成员
   * @param name 成员名称
   * @param rank_name 等级名称
   * @param online_actor_id 在线角色ID
   * @return true 成功，false 失败
   */
  bool add_member(std::string name, std::string rank_name = "Guild Member",
                  std::uint64_t online_actor_id = 0);

  /**
   * @brief 移除成员
   * @param name 成员名称
   * @return true 成功，false 成员不存在
   */
  bool remove_member(std::string_view name);

  /**
   * @brief 转让会长职位
   * @details 将会长职位转让给目标成员，原会长降为普通成员。
   * @param current_lord 现任会长名称
   * @param target_name 目标成员名称
   * @param old_lord_rank_name 原会长降级后的等级名称
   * @param new_lord_rank_name 新会长的等级名称
   * @return true 成功，false 失败
   */
  bool transfer_lord(std::string_view current_lord, std::string_view target_name,
                     std::string old_lord_rank_name = "Guild Member",
                     std::string new_lord_rank_name = "Guild Lord");

  /**
   * @brief 设置成员在线状态
   * @param name 成员名称
   * @param online_actor_id 在线角色ID
   * @return true 成功，false 成员不存在
   */
  bool set_member_online_actor(std::string_view name, std::uint64_t online_actor_id);

  /**
   * @brief 清除成员在线状态（下线时调用）
   * @param name 成员名称
   * @param online_actor_id 在线角色ID（用于验证）
   * @return true 成功，false 成员不存在或ID不匹配
   */
  bool clear_member_online_actor(std::string_view name, std::uint64_t online_actor_id);

  /**
   * @brief 设置成员是否接收公会聊天
   * @param name 成员名称
   * @param hears_guild_chat 是否接收公会聊天
   * @return true 成功，false 成员不存在
   */
  bool set_member_hears_guild_chat(std::string_view name, bool hears_guild_chat);

  /**
   * @brief 设置是否允许结盟
   * @param allow_ally_guild 是否允许其他公会发起结盟
   */
  void set_allow_ally_guild(bool allow_ally_guild) { allow_ally_guild_ = allow_ally_guild; }

  /**
   * @brief 添加盟友公会
   * @param guild_name 盟友公会名称
   * @return true 成功，false 名称无效或已是盟友
   */
  bool make_ally_guild(std::string guild_name);

  /**
   * @brief 解除盟友关系
   * @param guild_name 原盟友公会名称
   * @return true 成功，false 未结盟
   */
  bool break_ally_guild(std::string_view guild_name);

  /**
   * @brief 宣战
   * @param guild_name 目标公会名称
   * @param now_ms 当前时间戳
   * @param remain_ms 战争持续时间，默认3小时
   * @return true 成功，false 名称无效或已是盟友
   */
  bool declare_guild_war(std::string guild_name, std::uint64_t now_ms,
                         std::uint64_t remain_ms = 3 * 60 * 60 * 1000);

  /**
   * @brief 移除敌对公会
   * @param guild_name 敌对公会名称
   * @return true 成功，false 未处于敌对状态
   */
  bool remove_hostile_guild(std::string_view guild_name);

  /**
   * @brief 到期结束公会战
   * @details 检查所有敌对公会，将已超时的战争从列表中移除。
   * @param now_ms 当前时间戳
   * @return 包含到期敌对公会名称的列表
   */
  std::vector<std::string> expire_guild_wars(std::uint64_t now_ms);

  /**
   * @brief 设置公会公告
   * @param notice_lines 公告文本行列表
   */
  void set_notice_lines(std::vector<std::string> notice_lines);

 private:
  /**
   * @brief 确保指定等级的分组存在
   * @details 如果等级分组不存在则创建。会长等级（kGuildLordRank=1）插入到最前面，
   *          其他等级追加到末尾。
   * @param rank 等级编号
   * @param rank_name 等级名称
   * @return 对应等级分组的引用
   */
  GuildRankGroup& ensure_rank(std::uint8_t rank, std::string rank_name);

  std::string name_{};                        ///< 公会名称
  std::vector<std::string> notice_lines_{};   ///< 公告内容（多行）
  std::vector<std::string> ally_guilds_{};    ///< 盟友公会列表
  std::vector<GuildWarState> hostile_guilds_{}; ///< 敌对公会列表
  std::vector<GuildRankGroup> ranks_{};        ///< 等级分组列表
  bool allow_ally_guild_{false};              ///< 是否允许其他公会结盟
};

/**
 * @class GuildManager
 * @brief 公会总管理器
 * @details 管理服务器上所有公会的创建、查找、删除以及公会间交互操作。
 *          提供公会成员管理、外交关系处理、公会聊天分发等功能。
 *          作为公会系统的统一入口，协调各公会实体之间的操作。
 */
class GuildManager {
 public:
  // @{ 访问器与查询方法
  [[nodiscard]] const std::vector<Guild>& guilds() const { return guilds_; }  ///< 获取所有公会列表
  [[nodiscard]] Guild* find_guild(std::string_view name);                     ///< 按名称查找公会（可变）
  [[nodiscard]] const Guild* find_guild(std::string_view name) const;         ///< 按名称查找公会（const）
  [[nodiscard]] Guild* find_guild_by_member(std::string_view member_name);    ///< 按成员名称查找所属公会（可变）
  [[nodiscard]] const Guild* find_guild_by_member(std::string_view member_name) const;  ///< 按成员名称查找所属公会（const）
  // @}

  /**
   * @brief 创建公会
   * @param name 公会名称
   * @param lord_name 会长名称
   * @param lord_rank_name 会长等级名称
   * @param lord_actor_id 会长角色ID
   * @return 创建成功的公会指针，失败返回 nullptr
   * @note 创建条件：公会名不能重复，会长不能已在其他公会
   */
  Guild* create_guild(std::string name, std::string lord_name,
                      std::string lord_rank_name = "Guild Lord",
                      std::uint64_t lord_actor_id = 0);

  /**
   * @brief 删除公会
   * @param name 公会名称
   * @return true 成功删除，false 公会不存在
   */
  bool erase_guild(std::string_view name);

  /**
   * @brief 由会长添加公会成员
   * @param guild_name 公会名称
   * @param requester_name 请求者（会长）名称
   * @param target_name 目标成员名称
   * @param context 添加成员的上下文（在线状态、面向状态等约束）
   * @return GuildMemberOpResult 操作结果
   */
  GuildMemberOpResult add_member_by_lord(std::string_view guild_name,
                                         std::string_view requester_name,
                                         std::string target_name,
                                         const GuildAddMemberContext& context = {});

  /**
   * @brief 由会长移除公会成员
   * @param guild_name 公会名称
   * @param requester_name 请求者（会长）名称
   * @param target_name 目标成员名称
   * @return GuildMemberOpResult 操作结果
   * @note 如果移除后公会为空，或会长将自己移除（解散公会），则自动删除公会。
   */
  GuildMemberOpResult remove_member_by_lord(std::string_view guild_name,
                                            std::string_view requester_name,
                                            std::string_view target_name);

  /**
   * @brief 成员主动退出公会
   * @param member_name 成员名称
   * @return GuildMemberOpResult 操作结果
   * @note 会长不能通过此方法退出，必须先转让会长职位。
   */
  GuildMemberOpResult leave_member(std::string_view member_name);

  /**
   * @brief 转让会长职位
   * @param guild_name 公会名称
   * @param requester_name 请求者（原会长）名称
   * @param target_name 目标成员名称
   * @return GuildMemberOpResult 操作结果
   */
  GuildMemberOpResult transfer_lord(std::string_view guild_name,
                                    std::string_view requester_name,
                                    std::string_view target_name);

  /**
   * @brief 获取公会聊天投递列表
   * @param guild_name 公会名称
   * @param speaker_name 发送者名称
   * @param text 消息内容
   * @return 需要投递的聊天消息列表（仅发送给在线且开启公会聊天的成员）
   */
  [[nodiscard]] std::vector<GuildChatDelivery> guild_chat_deliveries(
      std::string_view guild_name, std::string_view speaker_name, std::string_view text) const;

  // @{ 外交关系相关方法

  /**
   * @brief 结盟
   * @details 两个公会的会长同意后建立同盟关系。要求双方不是敌对状态、不拒绝结盟。
   * @param requester_guild 请求方公会
   * @param requester_name 请求方会长名称
   * @param target_guild 目标方公会
   * @param target_name 目标方会长名称
   * @return GuildRelationOpResult 操作结果
   */
  GuildRelationOpResult make_ally(std::string_view requester_guild,
                                  std::string_view requester_name,
                                  std::string_view target_guild,
                                  std::string_view target_name);

  /**
   * @brief 解除同盟
   * @param requester_guild 请求方公会
   * @param requester_name 请求方会长名称
   * @param target_guild 目标公会
   * @return GuildRelationOpResult 操作结果
   */
  GuildRelationOpResult break_ally(std::string_view requester_guild,
                                   std::string_view requester_name,
                                   std::string_view target_guild);

  /**
   * @brief 宣战
   * @details 双向宣战，双方公会互相加入敌对列表。
   *          如果目标已是盟友则拒绝宣战。
   * @param requester_guild 宣战方公会
   * @param requester_name 宣战方会长名称
   * @param target_guild 目标公会
   * @param now_ms 当前时间戳
   * @param remain_ms 战争持续时间，默认3小时
   * @return GuildRelationOpResult 操作结果
   */
  GuildRelationOpResult declare_guild_war(std::string_view requester_guild,
                                          std::string_view requester_name,
                                          std::string_view target_guild,
                                          std::uint64_t now_ms,
                                          std::uint64_t remain_ms = 3 * 60 * 60 * 1000);

  /**
   * @brief 到期结束所有公会的战争
   * @details 遍历所有公会，检查并移除已超时的敌对关系。
   *          同时从对方的敌对列表中移除本方。
   * @param now_ms 当前时间戳
   * @return 已到期结束的公会战列表，格式为"公会A/公会B"
   */
  std::vector<std::string> expire_guild_wars(std::uint64_t now_ms);
  // @}

 private:
  std::vector<Guild> guilds_{};  ///< 所有公会列表
};

}  // namespace mir2
