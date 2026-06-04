/**
 * @file repository.hpp
 * @brief 数据仓库类声明 - 提供SQLite数据库的CRUD操作接口
 * @details 定义了 Repository 类，封装了所有与 SQLite 数据库的交互操作，
 *          包括账户管理、角色管理、行会管理、城堡管理、商人状态、审计日志等。
 *          同时定义了辅助数据结构 AccountOperationResult 和 LegacyImportRecord。
 * @author mir2 Team
 * @date 2026-06-04
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/messages.hpp"

struct sqlite3;

namespace mir2 {

/**
 * @struct AccountOperationResult
 * @brief 账户操作结果结构体
 * @details 封装账户认证等操作的返回结果，包含状态码和可选的账户记录。
 *          状态码约定：
 *          -  1: 认证成功
 *          - -1: 密码错误
 *          - -2: 登录锁定（失败次数过多）
 *          - -4: 账户不存在
 *          - -5: 账户已被封禁
 */
struct AccountOperationResult {
  std::int32_t status_code{0};               ///< 操作状态码
  std::optional<AccountRecord> account{};    ///< 可选的账户记录
};

/**
 * @struct LegacyImportRecord
 * @brief 遗留数据导入记录结构体
 * @details 记录从旧版数据库导入角色数据时的每条记录的处理结果，
 *          包括原始文件信息、账户/角色标识、导入状态和原始二进制数据。
 *          用于追踪导入过程，便于排查问题和回滚。
 */
struct LegacyImportRecord {
  std::string source_file{};                 ///< 源文件名（如 Hum.DB 或 Mir.DB）
  std::int32_t record_index{0};             ///< 在源文件中的记录索引
  std::string account_id{};                 ///< 导入后的账户ID
  std::string character_name{};             ///< 导入后的角色名
  std::string status{};                     ///< 导入状态（imported/skipped/failed）
  std::string message{};                    ///< 状态描述信息
  std::vector<std::uint8_t> raw_record{};  ///< 原始二进制记录数据
};

/**
 * @class Repository
 * @brief SQLite 数据库仓库类
 * @details 提供对游戏服务器 SQLite 数据库的完整访问层，封装所有数据库操作。
 *          采用 RAII 模式管理数据库连接生命周期，禁止拷贝构造和拷贝赋值。
 *          支持事务处理、模式迁移（schema migration）、列兼容性检查等特性。
 *
 *          主要功能领域：
 *          - 账户管理：创建、更新、认证、密码修改
 *          - 角色管理：创建、删除、保存、加载、列出
 *          - 行会管理：保存状态、删除行会、管理成员
 *          - 城堡管理：加载/保存城堡状态
 *          - 商人管理：加载/保存商人货物和价格
 *          - 导入管理：记录遗留数据导入和审计事件
 *
 * @note 所有公共方法在数据库操作失败时均会抛出 std::runtime_error 异常
 */
class Repository {
 public:
  /**
   * @brief 构造函数 - 打开或创建 SQLite 数据库
   * @param database_path 数据库文件路径（如果目录不存在会自动创建）
   * @throws std::runtime_error 如果无法打开数据库或设置 PRAGMA 失败
   * @note 构造函数会自动启用 WAL 日志模式和 NORMAL 同步模式以平衡性能与安全性
   */
  explicit Repository(const std::filesystem::path& database_path);

  /**
   * @brief 析构函数 - 关闭数据库连接
   * @details 安全关闭 SQLite 数据库连接，并将指针置空。
   *          如果数据库已经关闭或为 nullptr，则操作安全无副作用。
   */
  ~Repository();

  /** @brief 禁止拷贝构造（RAII 资源管理不允许复制） */
  Repository(const Repository&) = delete;
  /** @brief 禁止拷贝赋值（RAII 资源管理不允许复制） */
  Repository& operator=(const Repository&) = delete;

  /**
   * @brief 确保数据库模式是最新的
   * @param schema_path SQL 模式文件路径（包含 CREATE TABLE 等语句）
   * @details 执行模式文件中的 SQL 语句创建表结构，然后执行兼容性迁移：
   *          - 迁移旧版 characters 表结构（如果存在）
   *          - 确保 accounts 表包含所有需要的列
   *          - 确保 characters 表包含所有需要的列
   *          - 确保 merchant_state 表包含所有需要的列
   * @throws std::runtime_error 如果 SQL 执行失败
   */
  void ensure_schema(const std::filesystem::path& schema_path);

  /**
   * @brief 填充运行时种子数据
   * @details 创建默认的访客账户（guest/pass）及其初始角色（Hero），
   *          用于开发和测试环境的基础数据初始化。
   */
  void seed_runtime();

  /**
   * @brief 根据账户ID加载账户记录
   * @param account_id 账户唯一标识
   * @return 如果找到则返回 AccountRecord，否则返回 std::nullopt
   * @throws std::runtime_error 如果 SQL 预处理或执行失败
   */
  [[nodiscard]] std::optional<AccountRecord> load_account(const std::string& account_id);

  /**
   * @brief 认证账户登录
   * @param account_id 账户ID
   * @param password 明文密码
   * @param now_ms 当前时间戳（毫秒）
   * @return AccountOperationResult 包含状态码和账户信息
   * @details 认证流程：
   *          1. 检查账户是否存在
   *          2. 检查账户是否被封禁
   *          3. 检查是否因密码失败次数过多而锁定（5次/60秒）
   *          4. 验证密码是否匹配
   *          5. 更新密码失败计数和时间戳
   * @note 认证成功后会重置密码失败计数
   */
  [[nodiscard]] AccountOperationResult authenticate_account(const std::string& account_id,
                                                            const std::string& password,
                                                            std::int64_t now_ms);

  /**
   * @brief 加载城堡对话框上下文数据
   * @return CastleDialogContext 包含城堡信息、领主、战争日期等
   * @details 从 castle_state 表加载第一个城堡记录，解析其 JSON payload，
   *          然后查询拥有该城堡的行会信息，补充领主名称等数据。
   *          支持多种 JSON 字段命名风格的兼容性解析（如 lord/chief/master）。
   */
  [[nodiscard]] CastleDialogContext load_castle_dialog_context();

  /**
   * @brief 加载行会和城堡快照
   * @return GuildCastleSnapshot 包含城堡对话框数据和所有行会列表
   * @details 联合查询 castle_state 和 guilds 表，构建完整的行会-城堡状态快照。
   *          guilds 表按行会名称排序返回。
   */
  [[nodiscard]] GuildCastleSnapshot load_guild_castle_snapshot();

  /**
   * @brief 保存行会 JSON payload
   * @param guild_name 行会名称
   * @param payload_json 行会数据的 JSON 字符串
   * @details 使用 UPSERT 语义（INSERT OR REPLACE），
   *          如果行会已存在则更新 payload_json，否则插入新记录。
   * @throws std::runtime_error 如果 SQL 执行失败
   */
  void save_guild_payload(const std::string& guild_name, const std::string& payload_json);

  /**
   * @brief 保存规范化后的行会状态
   * @param guild_state 行会状态结构体
   * @details 在保存前对数据进行规范化处理：
   *          - 去除行会名称和领主的首尾空白
   *          - 去重成员列表
   *          - 确保领主在成员列表中
   *          - 过滤申请者中已经是成员的数据
   *          最后调用 save_guild_payload 写入数据库。
   */
  void save_guild_state(const GuildState& guild_state);

  /**
   * @brief 删除行会
   * @param guild_name 要删除的行会名称
   * @details 分两步操作：
   *          1. 清除所有角色中对该行会的引用（guild_name 和 guild_title 置空）
   *          2. 从 guilds 表中删除行会记录
   *          两步操作在一个事务中执行。
   * @throws std::runtime_error 如果任何一步 SQL 执行失败
   */
  void delete_guild(const std::string& guild_name);

  /**
   * @brief 保存城堡状态
   * @param castle_name 城堡名称
   * @param payload_json 城堡状态的 JSON 字符串
   * @details 使用 UPSERT 语义，如果城堡已存在则更新 payload_json。
   * @throws std::runtime_error 如果 SQL 执行失败
   */
  void save_castle_state(const std::string& castle_name, const std::string& payload_json);

  /**
   * @brief 加载所有商人状态
   * @return MerchantStateRecord 向量，包含商人货物、武器升级记录和价格信息
   * @details 分两步查询：
   *          1. 从 merchant_state 表加载基本状态（货物blob、升级blob）
   *          2. 从 merchant_prices 表加载价格信息并关联到对应商人
   *          使用哈希表建立 merchant_key 到数组索引的映射以高效关联数据。
   * @throws std::runtime_error 如果 SQL 预处理失败
   */
  [[nodiscard]] std::vector<MerchantStateRecord> load_merchant_states();

  /**
   * @brief 保存商人状态（含事务保护）
   * @param state 商人状态记录
   * @details 在一个事务中执行三步操作：
   *          1. 更新/插入 merchant_state 表（状态、货物blob、升级blob）
   *          2. 删除该商人的所有旧价格记录
   *          3. 插入新的价格记录
   *          如果 merchant_key 为空则直接返回不执行任何操作。
   * @throws std::runtime_error 如果任何一步失败（事务会自动回滚）
   */
  void save_merchant_state(const MerchantStateRecord& state);

  /**
   * @brief 创建新账户
   * @param account 账户记录
   * @return true 如果创建成功，false 如果账户ID已存在（违反唯一约束）
   * @details 使用 INSERT 语句，如果 account_id 已存在则会违反 UNIQUE 约束导致返回 false。
   * @throws std::runtime_error 如果 SQL 预处理失败
   */
  [[nodiscard]] bool create_account(const AccountRecord& account);

  /**
   * @brief 更新账户信息
   * @param account 包含更新后数据的账户记录
   * @return true 如果至少有一行被更新
   * @details 使用 account_id 作为 WHERE 条件，只更新指定字段，
   *          同时自动更新 updated_at 时间戳。
   * @throws std::runtime_error 如果 SQL 预处理失败
   */
  [[nodiscard]] bool update_account(const AccountRecord& account);

  /**
   * @brief 修改账户密码
   * @param account_id 账户ID
   * @param password 当前密码（用于验证）
   * @param new_password 新密码（长度至少3个字符）
   * @param now_ms 当前时间戳（毫秒）
   * @return 1=成功, 0=账户不存在或新密码太短, -1=当前密码错误, -2=修改锁定
   * @details 修改锁定条件：密码失败 >= 5 次且距上次失败在 3 分钟内。
   *          成功修改后重置失败计数。
   */
  [[nodiscard]] std::int32_t change_password(const std::string& account_id,
                                             const std::string& password,
                                             const std::string& new_password,
                                             std::int64_t now_ms);

  /**
   * @brief 加载指定账户下的特定角色
   * @param account_id 账户ID
   * @param character_name 角色名
   * @return 如果找到则返回 CharacterRecord，否则返回 std::nullopt
   * @throws std::runtime_error 如果 SQL 预处理失败
   */
  [[nodiscard]] std::optional<CharacterRecord> load_character(const std::string& account_id,
                                                              const std::string& character_name);

  /**
   * @brief 根据角色名加载角色（全局唯一）
   * @param character_name 角色名
   * @return 如果找到则返回 CharacterRecord，否则返回 std::nullopt
   * @details 按 updated_at 降序、account_id 升序排序后取第一条。
   *          这允许同一角色名在不同账户下存在时返回最新更新的那个。
   * @throws std::runtime_error 如果 SQL 预处理失败
   */
  [[nodiscard]] std::optional<CharacterRecord> load_character_by_name(
      const std::string& character_name);

  /**
   * @brief 列出指定账户下的所有角色
   * @param account_id 账户ID
   * @return CharacterRecord 向量，按 updated_at 降序、角色名升序排列
   * @throws std::runtime_error 如果 SQL 预处理失败
   */
  [[nodiscard]] std::vector<CharacterRecord> list_characters(const std::string& account_id);

  /**
   * @brief 创建新角色
   * @param character 角色记录
   * @return true 如果创建成功
   * @details 创建前会自动确保账户存在（如果账户不存在则创建）。
   *          同时检查墓碑表（character_save_tombstones）中的保存版本号，
   *          确保新角色的版本号大于墓碑版本，防止已删除角色被重新创建。
   * @throws std::runtime_error 如果 SQL 预处理失败
   */
  [[nodiscard]] bool create_character(const CharacterRecord& character);

  /**
   * @brief 删除角色（软删除，含版本号屏障）
   * @param account_id 账户ID
   * @param character_name 角色名
   * @return true 如果角色被成功删除
   * @details 采用 tombstone 模式的删除策略：
   *          1. 读取角色的当前 save_version
   *          2. 计算屏障版本号（下一个 2^48 对齐值）
   *          3. 在墓碑表中插入/更新记录（含屏障版本号）
   *          4. 从 characters 表中删除角色记录
   *          这种机制确保已删除角色的旧版本数据不会通过后续同步操作复活。
   *          事务保护：如果删除失败则回滚墓碑插入。
   * @throws std::runtime_error 如果 SQL 执行失败
   */
  [[nodiscard]] bool delete_character(const std::string& account_id,
                                      const std::string& character_name);

  /**
   * @brief 保存角色数据（含乐观锁和墓碑检查）
   * @param character 要保存的角色记录
   * @return true 如果保存成功（受影响的记录数 > 0）
   * @details 保存流程：
   *          1. 确保账户存在
   *          2. 开启事务
   *          3. 检查墓碑表，如果角色的 save_version <= 墓碑版本则拒绝保存
   *          4. 使用 UPSERT 语义，通过 `excluded.save_version >= characters.save_version`
   *             条件实现乐观锁，防止旧版本覆盖新版本
   *          5. 检查 sqlite3_changes() 确定是否有行被实际更新
   * @note 如果角色已被删除（墓碑版本 >= 保存版本），保存会被拒绝并返回 false
   * @throws std::runtime_error 如果 SQL 执行失败（事务自动回滚）
   */
  bool save_character(const CharacterRecord& character);

  /**
   * @brief 记录遗留数据导入操作
   * @param record 导入记录
   * @details 将遗留系统数据导入的每条记录写入 legacy_import_records 表，
   *          包含源文件信息、处理状态和原始二进制数据，用于审计和故障排查。
   * @throws std::runtime_error 如果 SQL 执行失败
   */
  void record_legacy_import(const LegacyImportRecord& record);

  /**
   * @brief 统计遗留数据导入记录总数
   * @return 导入记录的数量
   * @throws std::runtime_error 如果 SQL 预处理失败
   */
  [[nodiscard]] std::size_t count_legacy_import_records();

  /**
   * @brief 记录审计事件
   * @param audit 审计事件记录
   * @details 将登录审计等安全事件写入 login_audit 表，
   *          包含分类、消息、会话密钥和创建时间戳。
   * @throws std::runtime_error 如果 SQL 执行失败
   */
  void record_audit(const AuditEvent& audit);

 private:
  sqlite3* database_{nullptr};  ///< SQLite 数据库连接指针（RAII 管理）
};

}  // namespace mir2
