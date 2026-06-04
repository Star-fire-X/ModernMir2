/**
 * @file repository.cpp
 * @brief SQLite 数据库仓库类的实现
 * @details 实现 Repository 类及其所有数据库操作方法。
 *          包含 SQLite 的 CRUD 操作、事务管理、模式迁移、JSON 解析辅助函数
 *          以及二进制数据编解码等底层工具函数。
 *          所有对外的公共方法在数据库操作失败时都会抛出 std::runtime_error 异常。
 * @author mir2 Team
 * @date 2026-06-04
 */

#include "storage/repository.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "spdlog/spdlog.h"
#include "sqlite3.h"

namespace mir2 {

namespace {

/**
 * @brief 已删除角色的保存版本号步进值
 * @details 用于计算墓碑屏障版本号。当角色被删除时，
 *          其屏障版本号为当前 save_version 对齐到下一个 2^48 边界的值。
 *          这样可以确保同一角色在 2^48 次保存内不会被重复删除所覆盖。
 */
constexpr std::uint64_t kDeletedCharacterSaveVersionStride = 1ULL << 48;

/**
 * @brief SQLite 可存储的最大版本号
 * @details SQLite 的 int64 是有符号类型，取最大值作为 uint64 的上限，
 *          用于防止版本号计算溢出。
 */
constexpr std::uint64_t kMaxSqliteSaveVersion =
    static_cast<std::uint64_t>((std::numeric_limits<sqlite3_int64>::max)());

/**
 * @brief 计算已删除角色的保存版本号屏障值
 * @param save_version 角色的当前保存版本号
 * @return 屏障版本号（下一个对齐到 2^48 边界的值，最大不超过 kMaxSqliteSaveVersion - 1）
 * @details 屏障版本号用于墓碑记录，确保：
 *          - 已删除角色的旧版本数据不会通过后续同步操作重新写入
 *          - 屏障值通过对齐到 2^48 边界来保证版本号排序正确
 *          - 在处理接近最大值的情况下进行溢出保护
 */
std::uint64_t deleted_character_save_version_barrier(std::uint64_t save_version) {
  if (save_version >= kMaxSqliteSaveVersion - kDeletedCharacterSaveVersionStride) {
    return kMaxSqliteSaveVersion - 1;
  }
  return ((save_version / kDeletedCharacterSaveVersionStride) + 1) *
         kDeletedCharacterSaveVersionStride;
}

/**
 * @brief 读取文本文件的全部内容
 * @param path 文件路径
 * @return 文件内容的字符串
 * @throws std::runtime_error 如果无法打开文件
 */
std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path.string());
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

/**
 * @brief 执行 SQL 语句，失败时抛出异常
 * @param database SQLite 数据库连接指针
 * @param sql 要执行的 SQL 语句
 * @throws std::runtime_error 如果 SQL 执行失败
 * @note 使用 sqlite3_exec 执行，适用于不需要参数绑定的 DDL/DML 语句
 */
void exec_or_throw(sqlite3* database, const std::string& sql) {
  char* error = nullptr;
  if (sqlite3_exec(database, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    const std::string message = error != nullptr ? error : "sqlite_exec failed";
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

/**
 * @brief 检查数据库表是否包含指定列
 * @param database SQLite 数据库连接指针
 * @param table_name 表名
 * @param column_name 列名
 * @return true 如果表包含该列，false 如果表不存在或列不存在
 * @details 通过执行 PRAGMA table_info 查询表的结构信息，
 *          遍历结果集检查目标列是否存在。
 */
bool table_has_column(sqlite3* database, const std::string& table_name, const std::string& column_name) {
  sqlite3_stmt* statement = nullptr;
  const auto sql = "PRAGMA table_info(" + table_name + ");";
  if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    return false;
  }

  bool found = false;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    if (text != nullptr && column_name == text) {
      found = true;
      break;
    }
  }
  sqlite3_finalize(statement);
  return found;
}

/**
 * @brief 确保数据库表包含指定列，如果缺少则执行 ALTER TABLE 添加
 * @param database SQLite 数据库连接指针
 * @param table_name 表名
 * @param column_name 列名
 * @param alter_sql 用于添加列的 ALTER TABLE SQL 语句
 * @details 先检查列是否存在，不存在则执行提供的 ALTER TABLE 语句。
 *          用于模式迁移（schema migration）场景。
 */
void ensure_column(sqlite3* database, const std::string& table_name, const std::string& column_name,
                   const std::string& alter_sql) {
  if (!table_has_column(database, table_name, column_name)) {
    exec_or_throw(database, alter_sql);
  }
}

/**
 * @brief 安全释放 SQLite 预处理语句对象
 * @param statement 要释放的 sqlite3_stmt 指针
 * @details 封装 sqlite3_finalize，用于 RAII 风格的资源清理。
 *          传入 nullptr 时安全无副作用。
 */
void finalize_statement(sqlite3_stmt* statement) {
  if (statement != nullptr) {
    sqlite3_finalize(statement);
  }
}

/**
 * @brief 绑定字符串参数到 SQL 预处理语句
 * @param statement 预处理语句对象
 * @param index 参数索引（从 1 开始）
 * @param value 要绑定的字符串值
 * @details 使用 SQLITE_TRANSIENT 标志，让 SQLite 在需要时自行拷贝数据。
 */
void bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
  sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

/**
 * @brief 从指定表加载角色的保存版本号
 * @param database SQLite 数据库连接指针
 * @param account_id 账户ID
 * @param character_name 角色名
 * @param table_name 表名（可以是 characters 或 character_save_tombstones）
 * @return 如果找到则返回版本号，否则返回 std::nullopt
 * @throws std::runtime_error 如果 SQL 预处理失败
 * @note 返回前会自动释放预处理语句对象
 */
std::optional<std::uint64_t> load_save_version(sqlite3* database, const std::string& account_id,
                                               const std::string& character_name,
                                               const char* table_name) {
  sqlite3_stmt* statement = nullptr;
  const auto sql = std::string("SELECT save_version FROM ") + table_name +
                   " WHERE account_id = ?1 AND character_name = ?2 LIMIT 1;";
  if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare save_version statement.");
  }

  bind_text(statement, 1, account_id);
  bind_text(statement, 2, character_name);
  std::optional<std::uint64_t> version;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    version =
        static_cast<std::uint64_t>(std::max<sqlite3_int64>(0, sqlite3_column_int64(statement, 0)));
  }
  finalize_statement(statement);
  return version;
}

/**
 * @brief 绑定固定大小数组作为 BLOB 参数到 SQL 预处理语句
 * @tparam T 数组元素类型
 * @tparam N 数组大小
 * @param statement 预处理语句对象
 * @param index 参数索引（从 1 开始）
 * @param values 要绑定的数组引用
 * @details 将整个 std::array 的原始内存作为 BLOB 绑定，适用于 LegacyUserItem 等固定大小结构体数组。
 */
template <typename T, std::size_t N>
void bind_blob_array(sqlite3_stmt* statement, int index, const std::array<T, N>& values) {
  sqlite3_bind_blob(statement, index, values.data(), static_cast<int>(sizeof(values)),
                    SQLITE_TRANSIENT);
}

/**
 * @brief 从 SQL 查询结果列中读取 BLOB 数据到固定大小数组
 * @tparam T 数组元素类型
 * @tparam N 数组大小
 * @param statement 预处理语句对象
 * @param column 列索引（从 0 开始）
 * @param values 输出参数，接收读取的数据
 * @details 先将数组清零，然后从结果集的 BLOB 列拷贝数据。
 *          如果 BLOB 字节数与数组大小不匹配，发出警告并拷贝较小值。
 */
template <typename T, std::size_t N>
void read_blob_array(sqlite3_stmt* statement, int column, std::array<T, N>& values) {
  values.fill(T{});
  const auto* blob = sqlite3_column_blob(statement, column);
  const auto bytes = sqlite3_column_bytes(statement, column);
  if (blob == nullptr || bytes <= 0) {
    return;
  }
  if (static_cast<std::size_t>(bytes) != sizeof(values)) {
    spdlog::warn("Legacy blob column {} has {} bytes; expected {} bytes", column, bytes,
                 sizeof(values));
  }
  std::memcpy(values.data(), blob,
              std::min<std::size_t>(static_cast<std::size_t>(bytes), sizeof(values)));
}

/**
 * @brief 绑定动态字节数组作为 BLOB 参数到 SQL 预处理语句
 * @param statement 预处理语句对象
 * @param index 参数索引（从 1 开始）
 * @param values 要绑定的字节向量引用
 * @details 如果向量为空，则绑定 nullptr 和 0 长度。
 */
void bind_blob_vector(sqlite3_stmt* statement, int index, const std::vector<std::uint8_t>& values) {
  sqlite3_bind_blob(statement, index, values.empty() ? nullptr : values.data(),
                    static_cast<int>(values.size()), SQLITE_TRANSIENT);
}

/**
 * @brief 将商人货物列表编码为二进制 BLOB
 * @param goods 商人物品列表（每个 LegacyUserItem 为固定大小结构体）
 * @return 编码后的字节向量
 * @details 将结构体数组的原始内存直接拷贝到字节向量中，
 *          用于存储到 merchant_state 表的 goods_blob 列。
 */
std::vector<std::uint8_t> encode_merchant_goods_blob(const std::vector<LegacyUserItem>& goods) {
  std::vector<std::uint8_t> blob;
  blob.resize(goods.size() * sizeof(LegacyUserItem));
  if (!goods.empty()) {
    std::memcpy(blob.data(), goods.data(), blob.size());
  }
  return blob;
}

/**
 * @brief 从 SQL 查询结果列中解码商人物品 BLOB
 * @param statement 预处理语句对象
 * @param column 列索引（从 0 开始）
 * @return 解码后的 LegacyUserItem 向量
 * @details 从 BLOB 中按固定大小切割出每个 LegacyUserItem 结构体。
 *          如果 BLOB 大小不是结构体大小的整数倍，记录警告信息。
 */
std::vector<LegacyUserItem> decode_merchant_goods_blob(sqlite3_stmt* statement, int column) {
  std::vector<LegacyUserItem> goods;
  const auto* raw = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement, column));
  const auto bytes = sqlite3_column_bytes(statement, column);
  if (raw == nullptr || bytes <= 0) {
    return goods;
  }
  if (static_cast<std::size_t>(bytes) % sizeof(LegacyUserItem) != 0) {
    spdlog::warn("Merchant goods blob column {} has {} bytes; item size is {}", column, bytes,
                 sizeof(LegacyUserItem));
  }
  const auto count = static_cast<std::size_t>(bytes) / sizeof(LegacyUserItem);
  goods.resize(count);
  if (!goods.empty()) {
    std::memcpy(goods.data(), raw, goods.size() * sizeof(LegacyUserItem));
  }
  return goods;
}

/**
 * @brief 以小端序写入 32 位有符号整数到字节向量
 * @param out 输出字节向量引用
 * @param value 要写入的整数
 * @details 将整数按小端序逐字节追加到向量末尾（每 8 位写入一个字节）。
 */
void write_i32_le(std::vector<std::uint8_t>& out, std::int32_t value) {
  auto bits = static_cast<std::uint32_t>(value);
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((bits >> shift) & 0xffu));
  }
}

/**
 * @brief 以小端序从字节数据中读取 32 位有符号整数
 * @param data 输入字节数组指针
 * @return 解码后的 32 位整数
 * @details 从 4 个字节中按小端序拼装出完整的 32 位整数。
 */
std::int32_t read_i32_le(const std::uint8_t* data) {
  const auto bits = static_cast<std::uint32_t>(data[0]) |
                    (static_cast<std::uint32_t>(data[1]) << 8) |
                    (static_cast<std::uint32_t>(data[2]) << 16) |
                    (static_cast<std::uint32_t>(data[3]) << 24);
  return static_cast<std::int32_t>(bits);
}

/**
 * @brief 以小端序写入 64 位无符号整数到字节向量
 * @param out 输出字节向量引用
 * @param value 要写入的整数
 * @details 将整数按小端序逐字节追加到向量末尾（每 8 位写入一个字节）。
 */
void write_u64_le(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
  }
}

/**
 * @brief 以小端序从字节数据中读取 64 位无符号整数
 * @param data 输入字节数组指针
 * @return 解码后的 64 位整数
 * @details 从 8 个字节中按小端序拼装出完整的 64 位整数。
 */
std::uint64_t read_u64_le(const std::uint8_t* data) {
  std::uint64_t value = 0;
  for (int shift = 0; shift < 64; shift += 8) {
    value |= static_cast<std::uint64_t>(data[shift / 8]) << shift;
  }
  return value;
}

/**
 * @brief 将武器升级记录列表编码为二进制 BLOB
 * @param records 武器升级记录列表
 * @return 编码后的字节向量
 * @details BLOB 格式：
 *          - [4字节] 记录数量（int32 LE）
 *          - 每条记录：
 *            - [32字节] 角色名称（固定宽度，不足补零）
 *            - [sizeof(LegacyUserItem)] 物品数据
 *            - [1字节] updc（升级次数-物理攻击）
 *            - [1字节] upsc（升级次数-魔法攻击）
 *            - [1字节] upmc（升级次数-道术攻击）
 *            - [1字节] durapoint（持久力点数）
 *            - [8字节] ready_time_ms（就绪时间戳，uint64 LE）
 */
std::vector<std::uint8_t> encode_weapon_upgrade_blob(
    const std::vector<LegacyWeaponUpgradeRecord>& records) {
  constexpr std::size_t kNameBytes = 32;
  std::vector<std::uint8_t> blob;
  write_i32_le(blob, static_cast<std::int32_t>(records.size()));
  for (const auto& record : records) {
    std::array<std::uint8_t, kNameBytes> name{};
    const auto copy_size = std::min(kNameBytes, record.character_name.size());
    std::memcpy(name.data(), record.character_name.data(), copy_size);
    blob.insert(blob.end(), name.begin(), name.end());
    const auto* item_bytes = reinterpret_cast<const std::uint8_t*>(&record.item);
    blob.insert(blob.end(), item_bytes, item_bytes + sizeof(LegacyUserItem));
    blob.push_back(record.updc);
    blob.push_back(record.upsc);
    blob.push_back(record.upmc);
    blob.push_back(record.durapoint);
    write_u64_le(blob, record.ready_time_ms);
  }
  return blob;
}

/**
 * @brief 从 SQL 查询结果列中解码武器升级记录 BLOB
 * @param statement 预处理语句对象
 * @param column 列索引（从 0 开始）
 * @return 解码后的武器升级记录向量
 * @details 按照 encode_weapon_upgrade_blob 的格式反向解析 BLOB。
 *          如果数据不足（BLOB 提前结束），记录警告信息并返回已解析部分。
 */
std::vector<LegacyWeaponUpgradeRecord> decode_weapon_upgrade_blob(sqlite3_stmt* statement,
                                                                  int column) {
  constexpr std::size_t kNameBytes = 32;
  constexpr std::size_t kRecordBytes = kNameBytes + sizeof(LegacyUserItem) + 4 + 8;
  std::vector<LegacyWeaponUpgradeRecord> records;
  const auto* raw = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement, column));
  const auto bytes = sqlite3_column_bytes(statement, column);
  if (raw == nullptr || bytes <= 4) {
    return records;
  }
  const auto available = static_cast<std::size_t>(bytes);
  const auto count = std::max(read_i32_le(raw), 0);
  std::size_t offset = 4;
  records.reserve(static_cast<std::size_t>(count));
  for (std::int32_t index = 0; index < count; ++index) {
    if (available < offset + kRecordBytes) {
      spdlog::warn("Merchant upgrade blob column {} ended early at record {}", column, index);
      break;
    }
    LegacyWeaponUpgradeRecord record;
    const auto* name = raw + offset;
    const auto name_len = std::find(name, name + kNameBytes, std::uint8_t{0}) - name;
    record.character_name.assign(reinterpret_cast<const char*>(name), name_len);
    offset += kNameBytes;
    std::memcpy(&record.item, raw + offset, sizeof(LegacyUserItem));
    offset += sizeof(LegacyUserItem);
    record.updc = raw[offset++];
    record.upsc = raw[offset++];
    record.upmc = raw[offset++];
    record.durapoint = raw[offset++];
    record.ready_time_ms = read_u64_le(raw + offset);
    offset += 8;
    records.push_back(record);
  }
  return records;
}

/**
 * @brief 将随从（奴隶）数据数组编码为二进制 BLOB
 * @param slaves 随从记录数组（固定大小为 kMaxLegacySlaves）
 * @return 编码后的字节向量
 * @details BLOB 格式（每个槽位）：
 *          - [32字节] 随从名称（固定宽度，不足补零）
 *          - [4字节] slave_exp（经验值，int32 LE）
 *          - [1字节] slave_exp_level（经验等级）
 *          - [1字节] slave_make_level（制作等级）
 *          - [4字节] remain_royalty_sec（剩余忠诚秒数，int32 LE）
 *          - [4字节] hp（生命值，int32 LE）
 *          - [4字节] mp（魔法值，int32 LE）
 */
std::vector<std::uint8_t> encode_slave_blob(
    const std::array<CharacterSlaveRecord, kMaxLegacySlaves>& slaves) {
  constexpr std::size_t kNameBytes = 32;
  constexpr std::size_t kSlotBytes = kNameBytes + 4 + 1 + 1 + 4 + 4 + 4;
  std::vector<std::uint8_t> blob;
  blob.reserve(kMaxLegacySlaves * kSlotBytes);
  for (const auto& slave : slaves) {
    std::array<std::uint8_t, kNameBytes> name{};
    const auto copy_size = std::min(kNameBytes, slave.name.size());
    std::memcpy(name.data(), slave.name.data(), copy_size);
    blob.insert(blob.end(), name.begin(), name.end());
    write_i32_le(blob, slave.slave_exp);
    blob.push_back(slave.slave_exp_level);
    blob.push_back(slave.slave_make_level);
    write_i32_le(blob, slave.remain_royalty_sec);
    write_i32_le(blob, slave.hp);
    write_i32_le(blob, slave.mp);
  }
  return blob;
}

/**
 * @brief 从 SQL 查询结果列中解码随从（奴隶）数据 BLOB 到固定数组
 * @param statement 预处理语句对象
 * @param column 列索引（从 0 开始）
 * @param slaves 输出参数，接收解码后的随从记录数组
 * @details 先清零数组，然后按固定槽位大小解析 BLOB。
 *          如果 BLOB 数据不足以填满所有槽位，则提前停止。
 *          每个槽位的名称字段以空字符结尾，长度不超过 32 字节。
 */
void decode_slave_blob(sqlite3_stmt* statement, int column,
                       std::array<CharacterSlaveRecord, kMaxLegacySlaves>& slaves) {
  slaves.fill(CharacterSlaveRecord{});
  const auto* raw = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement, column));
  const auto bytes = sqlite3_column_bytes(statement, column);
  if (raw == nullptr || bytes <= 0) {
    return;
  }

  constexpr std::size_t kNameBytes = 32;
  constexpr std::size_t kSlotBytes = kNameBytes + 4 + 1 + 1 + 4 + 4 + 4;
  const auto available = static_cast<std::size_t>(bytes);
  for (std::size_t index = 0; index < kMaxLegacySlaves; ++index) {
    const auto offset = index * kSlotBytes;
    if (available < offset + kSlotBytes) {
      break;
    }
    const auto* slot = raw + offset;
    auto& slave = slaves[index];
    const auto name_len =
        std::find(slot, slot + kNameBytes, std::uint8_t{0}) - slot;
    slave.name.assign(reinterpret_cast<const char*>(slot), name_len);
    slave.slave_exp = read_i32_le(slot + kNameBytes);
    slave.slave_exp_level = *(slot + kNameBytes + 4);
    slave.slave_make_level = *(slot + kNameBytes + 5);
    slave.remain_royalty_sec = read_i32_le(slot + kNameBytes + 6);
    slave.hp = read_i32_le(slot + kNameBytes + 10);
    slave.mp = read_i32_le(slot + kNameBytes + 14);
  }
}

/**
 * @brief 从 SQL 查询结果列中获取文本值
 * @param statement 预处理语句对象
 * @param column 列索引（从 0 开始）
 * @return 列文本内容的 std::string，如果列为空则返回空字符串
 */
std::string column_text(sqlite3_stmt* statement, int column) {
  const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
  return text != nullptr ? std::string(text) : std::string{};
}

/**
 * @brief 计算两个时间戳之间的毫秒差
 * @param now_ms 当前时间戳（毫秒）
 * @param then_ms 过去的时间戳（毫秒）
 * @return 时间差（毫秒），如果 now_ms < then_ms 则返回 0
 * @note 确保不会返回负值，用于计算密码失败时间间隔等场景
 */
std::int64_t elapsed_ms(std::int64_t now_ms, std::int64_t then_ms) {
  return now_ms >= then_ms ? now_ms - then_ms : 0;
}

/**
 * @brief 将字符串转换为小写并去除首尾空白
 * @param value 输入字符串视图
 * @return 处理后的字符串
 * @details 先拷贝输入字符串，然后使用 STL 算法去除首尾空白字符，
 *          最后将所有字母转为小写。用于不区分大小写的字符串比较。
 */
std::string lower_trim_copy(std::string_view value) {
  std::string text(value);
  auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  text.erase(text.begin(),
             std::find_if(text.begin(), text.end(),
                          [&](unsigned char ch) { return !is_space(ch); }));
  text.erase(std::find_if(text.rbegin(), text.rend(),
                          [&](unsigned char ch) { return !is_space(ch); })
                 .base(),
             text.end());
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

/**
 * @brief 判断 JSON 字符串值是否表示"无主"状态
 * @param value 输入字符串视图
 * @return true 如果值为空、"none"、"unclaimed"或"-"（不区分大小写且忽略首尾空白）
 * @details 用于解析城堡和行会数据时判断领主字段是否为空。
 *          兼容旧版遗留数据的多种"无主"表示方式。
 */
bool is_legacy_unclaimed(std::string_view value) {
  const auto lowered = lower_trim_copy(value);
  return lowered.empty() || lowered == "none" || lowered == "unclaimed" || lowered == "-";
}

/**
 * @brief 跳过 JSON 字符串中的空白字符
 * @param text JSON 文本字符串视图
 * @param pos 当前位置的引用（会被更新以跳过空白）
 */
void skip_json_ws(std::string_view text, std::size_t& pos) {
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
    ++pos;
  }
}

/**
 * @brief 在 JSON 字符串中查找指定键对应的值起始位置
 * @param json JSON 文本字符串视图
 * @param key 要查找的键名
 * @return 如果找到则返回值开始位置的索引，否则返回 std::nullopt
 * @details 查找格式为 "key": 的标记，跳过键名后的空白字符和冒号，
 *          定位到值开始的精确位置。处理键名可能出现在字符串值中的情况。
 */
std::optional<std::size_t> find_json_value_start(std::string_view json, std::string_view key) {
  const auto marker = "\"" + std::string(key) + "\"";
  auto pos = json.find(marker);
  while (pos != std::string_view::npos) {
    pos += marker.size();
    skip_json_ws(json, pos);
    if (pos >= json.size() || json[pos] != ':') {
      pos = json.find(marker, pos);
      continue;
    }
    ++pos;
    skip_json_ws(json, pos);
    return pos;
  }
  return std::nullopt;
}

/**
 * @brief 从 JSON 文本中解析指定键的字符串值
 * @param json JSON 文本字符串视图
 * @param key 键名
 * @return 如果找到则返回字符串值，否则返回 std::nullopt
 * @details 支持转义字符（\\n, \\r, \\t 等）的解析。
 *          字符串值必须以双引号包围。
 */
std::optional<std::string> json_string_field(std::string_view json, std::string_view key) {
  const auto value_pos = find_json_value_start(json, key);
  if (!value_pos.has_value() || *value_pos >= json.size() || json[*value_pos] != '"') {
    return std::nullopt;
  }

  std::string value;
  bool escape = false;
  for (std::size_t pos = *value_pos + 1; pos < json.size(); ++pos) {
    const auto ch = json[pos];
    if (escape) {
      switch (ch) {
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          value.push_back(ch);
          break;
      }
      escape = false;
      continue;
    }
    if (ch == '\\') {
      escape = true;
      continue;
    }
    if (ch == '"') {
      return value;
    }
    value.push_back(ch);
  }
  return std::nullopt;
}

/**
 * @brief 从 JSON 文本中解析指定键的整数值
 * @param json JSON 文本字符串视图
 * @param key 键名
 * @return 如果找到则返回整数值，否则返回 std::nullopt
 * @details 支持可选的符号前缀（+/-）。
 *          值必须由连续的数字字符组成（允许前导符号）。
 *          如果解析失败（如非数字字符或空值），返回 std::nullopt。
 */
std::optional<std::int32_t> json_int_field(std::string_view json, std::string_view key) {
  const auto value_pos = find_json_value_start(json, key);
  if (!value_pos.has_value() || *value_pos >= json.size()) {
    return std::nullopt;
  }

  auto pos = *value_pos;
  const auto begin = pos;
  if (json[pos] == '-' || json[pos] == '+') {
    ++pos;
  }
  while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])) != 0) {
    ++pos;
  }
  if (pos == begin || (pos == begin + 1 && (json[begin] == '-' || json[begin] == '+'))) {
    return std::nullopt;
  }

  try {
    return std::stoi(std::string(json.substr(begin, pos - begin)));
  } catch (...) {
    return std::nullopt;
  }
}

/**
 * @brief 从 JSON 文本中解析指定键的字符串数组值
 * @param json JSON 文本字符串视图
 * @param key 键名
 * @return 如果找到则返回字符串向量，否则返回 std::nullopt
 * @details 解析 JSON 数组格式：["item1", "item2", ...]。
 *          支持转义字符处理，数组元素必须为双引号包围的字符串。
 *          空数组返回空向量。
 */
std::optional<std::vector<std::string>> json_string_array_field(std::string_view json,
                                                                std::string_view key) {
  const auto value_pos = find_json_value_start(json, key);
  if (!value_pos.has_value() || *value_pos >= json.size() || json[*value_pos] != '[') {
    return std::nullopt;
  }

  std::vector<std::string> values;
  auto pos = *value_pos + 1;
  while (pos < json.size()) {
    skip_json_ws(json, pos);
    if (pos >= json.size()) {
      break;
    }
    if (json[pos] == ']') {
      return values;
    }
    if (json[pos] != '"') {
      return std::nullopt;
    }

    bool escape = false;
    std::string value;
    ++pos;
    for (; pos < json.size(); ++pos) {
      const auto ch = json[pos];
      if (escape) {
        value.push_back(ch);
        escape = false;
        continue;
      }
      if (ch == '\\') {
        escape = true;
        continue;
      }
      if (ch == '"') {
        ++pos;
        break;
      }
      value.push_back(ch);
    }
    values.push_back(std::move(value));
    skip_json_ws(json, pos);
    if (pos < json.size() && json[pos] == ',') {
      ++pos;
      continue;
    }
    if (pos < json.size() && json[pos] == ']') {
      return values;
    }
  }

  return std::nullopt;
}

/**
 * @brief 将字符串数组以指定分隔符连接为一个字符串
 * @param values 字符串向量
 * @param separator 分隔符字符串视图
 * @return 连接后的字符串
 */
std::string join_strings(const std::vector<std::string>& values, std::string_view separator) {
  std::string joined;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      joined += separator;
    }
    joined += values[index];
  }
  return joined;
}

/**
 * @brief 转义字符串供 JSON 使用
 * @param text 要转义的原始文本
 * @return 转义后的字符串
 * @details 对特殊字符进行 JSON 转义：
 *          - 反斜杠 -> \\\\
 *          - 双引号 -> \\"
 *          - 换行符 -> \\n
 *          - 回车符 -> \\r
 *          - 制表符 -> \\t
 */
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

/**
 * @brief 去除字符串首尾的空白字符
 * @param value 要处理的字符串
 * @return 处理后的字符串副本
 */
std::string trim_copy(std::string value) {
  auto first = value.begin();
  while (first != value.end() && std::isspace(static_cast<unsigned char>(*first)) != 0) {
    ++first;
  }
  auto last = value.end();
  while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
    --last;
  }
  return std::string(first, last);
}

/**
 * @brief 向成员列表中添加唯一成员（去重）
 * @param members 成员字符串向量引用（输出参数）
 * @param member_name 要添加的成员名
 * @details 在添加前先去除空白，并检查是否已存在相同成员。
 *          空字符串或仅空白字符串不添加。
 *          比较时去除空白后不区分大小写。
 */
void push_unique_member(std::vector<std::string>& members, const std::string& member_name) {
  if (member_name.empty()) {
    return;
  }
  const auto lowered = trim_copy(member_name);
  if (lowered.empty()) {
    return;
  }
  const auto already_present =
      std::any_of(members.begin(), members.end(), [&](const std::string& existing) {
        return trim_copy(existing) == lowered;
      });
  if (!already_present) {
    members.push_back(lowered);
  }
}

/**
 * @brief 将 CSV 格式的成员文本分割为字符串向量
 * @param text CSV 格式的成员文本（多个成员以逗号分隔）
 * @return 成员字符串向量（已去重）
 * @details 逐个字符解析，以逗号为分隔符。
 *          每个子串通过 push_unique_member 添加以保证唯一性。
 */
std::vector<std::string> split_csv_members(std::string_view text) {
  std::vector<std::string> members;
  std::string current;
  for (const auto ch : text) {
    if (ch == ',') {
      push_unique_member(members, current);
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  push_unique_member(members, current);
  return members;
}

/**
 * @brief 从 SQLite 查询结果行中读取行会状态
 * @param statement 预处理语句对象（包含 guild_name 和 payload_json 两列）
 * @return 解析后的 GuildState 结构体
 * @details 从 payload_json 中尝试读取以下字段（支持多种兼容命名）：
 *          - lord/chief/master：领主名称
 *          - members：成员列表（支持字符串数组或 CSV 字符串格式）
 *          - applicants：申请者列表（支持字符串数组或 CSV 字符串格式）
 *        读取后对成员数据进行归一化处理：
 *          - 确保领主在成员列表中
 *          - 将领主移动到成员列表首位
 *          - 移除申请者中已经是成员的数据
 */
GuildState read_guild_state_row(sqlite3_stmt* statement) {
  GuildState guild_state;
  guild_state.guild_name = column_text(statement, 0);
  const auto payload = column_text(statement, 1);
  if (const auto value = json_string_field(payload, "lord"); value.has_value() && !value->empty()) {
    guild_state.lord = *value;
  } else if (const auto value = json_string_field(payload, "chief");
             value.has_value() && !value->empty()) {
    guild_state.lord = *value;
  } else if (const auto value = json_string_field(payload, "master");
             value.has_value() && !value->empty()) {
    guild_state.lord = *value;
  }

  if (const auto members = json_string_array_field(payload, "members");
      members.has_value() && !members->empty()) {
    for (const auto& member : *members) {
      push_unique_member(guild_state.members, member);
    }
  } else if (const auto members = json_string_field(payload, "members");
             members.has_value() && !members->empty()) {
    for (const auto& member : split_csv_members(*members)) {
      push_unique_member(guild_state.members, member);
    }
  }

  if (const auto applicants = json_string_array_field(payload, "applicants");
      applicants.has_value() && !applicants->empty()) {
    for (const auto& applicant : *applicants) {
      push_unique_member(guild_state.applicants, applicant);
    }
  } else if (const auto applicants = json_string_field(payload, "applicants");
             applicants.has_value() && !applicants->empty()) {
    for (const auto& applicant : split_csv_members(*applicants)) {
      push_unique_member(guild_state.applicants, applicant);
    }
  }

  // 确保领主在成员列表中，并将领主旋转到列表首位
  if (!guild_state.lord.empty()) {
    push_unique_member(guild_state.members, guild_state.lord);
    std::rotate(guild_state.members.begin(),
                std::find(guild_state.members.begin(), guild_state.members.end(), guild_state.lord),
                guild_state.members.end());
  }
  // 过滤掉已经是成员的申请者
  guild_state.applicants.erase(
      std::remove_if(guild_state.applicants.begin(), guild_state.applicants.end(),
                     [&](const std::string& applicant) {
                       return std::any_of(guild_state.members.begin(), guild_state.members.end(),
                                          [&](const std::string& member) {
                                            return trim_copy(member) == trim_copy(applicant);
                                          });
                     }),
      guild_state.applicants.end());
  return guild_state;
}

/**
 * @brief 根据 GuildState 构建 JSON payload 字符串
 * @param guild_state 行会状态结构体
 * @return 格式化的 JSON 字符串
 * @details 生成格式：{"lord":"...","members":["..."],"applicants":["..."]}
 *          所有字符串值经过 json_escape 转义处理。
 */
std::string build_guild_payload_json(const GuildState& guild_state) {
  std::ostringstream payload;
  payload << "{\"lord\":\"" << json_escape(guild_state.lord) << "\",\"members\":[";
  for (std::size_t index = 0; index < guild_state.members.size(); ++index) {
    if (index > 0) {
      payload << ',';
    }
    payload << '"' << json_escape(guild_state.members[index]) << '"';
  }
  payload << "],\"applicants\":[";
  for (std::size_t index = 0; index < guild_state.applicants.size(); ++index) {
    if (index > 0) {
      payload << ',';
    }
    payload << '"' << json_escape(guild_state.applicants[index]) << '"';
  }
  payload << "]}";
  return payload.str();
}

/**
 * @brief 从 SQLite 查询结果行中读取账户记录
 * @param statement 预处理语句对象（包含 accounts 表完整的 19 列）
 * @return 填充后的 AccountRecord 结构体
 * @details 列顺序必须与 SQL 查询语句中的 SELECT 顺序一致：
 *          account_id, password, display_name, user_name, ss_no, phone,
 *          quiz, answer, email, quiz2, answer2, birthday, mobile_phone,
 *          memo1, memo2, server_index, passwd_fail, passwd_fail_time_ms, is_banned
 */
AccountRecord read_account_row(sqlite3_stmt* statement) {
  AccountRecord record;
  record.account_id = column_text(statement, 0);
  record.password = column_text(statement, 1);
  record.display_name = column_text(statement, 2);
  record.user_name = column_text(statement, 3);
  record.ss_no = column_text(statement, 4);
  record.phone = column_text(statement, 5);
  record.quiz = column_text(statement, 6);
  record.answer = column_text(statement, 7);
  record.email = column_text(statement, 8);
  record.quiz2 = column_text(statement, 9);
  record.answer2 = column_text(statement, 10);
  record.birthday = column_text(statement, 11);
  record.mobile_phone = column_text(statement, 12);
  record.memo1 = column_text(statement, 13);
  record.memo2 = column_text(statement, 14);
  record.server_index = sqlite3_column_int(statement, 15);
  record.passwd_fail = sqlite3_column_int(statement, 16);
  record.passwd_fail_time_ms = sqlite3_column_int64(statement, 17);
  record.banned = sqlite3_column_int(statement, 18) != 0;
  return record;
}

/**
 * @brief 从 SQLite 查询结果行中读取角色记录
 * @param statement 预处理语句对象（包含 characters 表完整的 49 列）
 * @return 填充后的 CharacterRecord 结构体
 * @details 列顺序必须与 SQL 查询语句中的 SELECT 顺序一致。
 *          涉及多种数据类型的读取：
 *          - 文本字段使用 column_text
 *          - 整数字段使用 sqlite3_column_int
 *          - BLOB 数组字段使用 read_blob_array
 *          - 浮点字段使用 sqlite3_column_double
 *          角色属性（ability）的子字段从多个独立的列中逐个读取并填充。
 */
CharacterRecord read_character_row(sqlite3_stmt* statement) {
  CharacterRecord record;
  record.account_id = column_text(statement, 0);
  record.character_name = column_text(statement, 1);
  record.map_id = column_text(statement, 2);
  record.x = sqlite3_column_int(statement, 3);
  record.y = sqlite3_column_int(statement, 4);
  record.dir = static_cast<std::uint8_t>(sqlite3_column_int(statement, 5));
  record.light = static_cast<std::uint8_t>(sqlite3_column_int(statement, 6));
  record.job = static_cast<std::uint8_t>(sqlite3_column_int(statement, 7));
  record.sex = static_cast<std::uint8_t>(sqlite3_column_int(statement, 8));
  record.hair = static_cast<std::uint8_t>(sqlite3_column_int(statement, 9));
  record.gold = sqlite3_column_int(statement, 10);
  record.feature = sqlite3_column_int(statement, 11);
  record.status = sqlite3_column_int(statement, 12);
  record.ability.level = static_cast<std::uint8_t>(sqlite3_column_int(statement, 13));
  record.ability.hp = static_cast<std::uint16_t>(sqlite3_column_int(statement, 14));
  record.ability.mp = static_cast<std::uint16_t>(sqlite3_column_int(statement, 15));
  record.ability.max_hp = static_cast<std::uint16_t>(sqlite3_column_int(statement, 16));
  record.ability.max_mp = static_cast<std::uint16_t>(sqlite3_column_int(statement, 17));
  record.ability.ac = static_cast<std::uint16_t>(sqlite3_column_int(statement, 18));
  record.ability.mac = static_cast<std::uint16_t>(sqlite3_column_int(statement, 19));
  record.ability.dc = static_cast<std::uint16_t>(sqlite3_column_int(statement, 20));
  record.ability.mc = static_cast<std::uint16_t>(sqlite3_column_int(statement, 21));
  record.ability.sc = static_cast<std::uint16_t>(sqlite3_column_int(statement, 22));
  record.ability.exp = static_cast<std::uint32_t>(sqlite3_column_int(statement, 23));
  record.ability.max_exp = static_cast<std::uint32_t>(sqlite3_column_int(statement, 24));
  record.ability.weight = static_cast<std::uint16_t>(sqlite3_column_int(statement, 25));
  record.ability.max_weight = static_cast<std::uint16_t>(sqlite3_column_int(statement, 26));
  record.ability.wear_weight = static_cast<std::uint8_t>(sqlite3_column_int(statement, 27));
  record.ability.max_wear_weight = static_cast<std::uint8_t>(sqlite3_column_int(statement, 28));
  record.ability.hand_weight = static_cast<std::uint8_t>(sqlite3_column_int(statement, 29));
  record.ability.max_hand_weight = static_cast<std::uint8_t>(sqlite3_column_int(statement, 30));
  read_blob_array(statement, 31, record.equipped_items);
  read_blob_array(statement, 32, record.bag_items);
  read_blob_array(statement, 33, record.storage_items);
  read_blob_array(statement, 34, record.magics);
  record.guild_name = column_text(statement, 35);
  record.guild_title = column_text(statement, 36);
  record.attack_mode = static_cast<std::uint8_t>(std::clamp(sqlite3_column_int(statement, 37), 0, 255));
  record.pk_point = sqlite3_column_int(statement, 38);
  record.death_time_ms = static_cast<std::uint64_t>(std::max<std::int64_t>(0, sqlite3_column_int64(statement, 39)));
  read_blob_array(statement, 40, record.quest_marks);
  read_blob_array(statement, 41, record.quest_open_units);
  read_blob_array(statement, 42, record.quest_units);
  read_blob_array(statement, 43, record.script_params);
  record.daily_quest = static_cast<std::uint32_t>(std::max<std::int64_t>(0, sqlite3_column_int64(statement, 44)));
  decode_slave_blob(statement, 45, record.slaves);
  record.body_luck = sqlite3_column_double(statement, 46);
  record.birth_items_granted = sqlite3_column_int(statement, 47) != 0;
  record.save_version =
      static_cast<std::uint64_t>(std::max<sqlite3_int64>(0, sqlite3_column_int64(statement, 48)));
  return record;
}

/**
 * @brief 创建默认角色记录
 * @param account_id 账户ID
 * @param character_name 角色名
 * @return 填充了默认值的 CharacterRecord 结构体
 * @details 用于开发测试环境或访客账户的初始角色创建。
 *          默认角色出生在地图 "0" 的坐标 (330, 270)，
 *          等级为 1，拥有基本出生装备（木剑）和初始魔法（基础魔法）。
 */
CharacterRecord make_default_character(const std::string& account_id,
                                       const std::string& character_name) {
  CharacterRecord record;
  record.account_id = account_id;
  record.character_name = character_name;
  record.map_id = "0";
  record.x = 330;
  record.y = 270;
  record.dir = 0;
  record.light = 0;
  record.job = 0;
  record.sex = 0;
  record.hair = 0;
  record.gold = 0;
  record.feature = 0;
  record.status = 0;

  record.ability.level = 1;
  record.ability.hp = 15;
  record.ability.mp = 15;
  record.ability.max_hp = 15;
  record.ability.max_mp = 15;
  record.ability.ac = 0;
  record.ability.mac = 0;
  record.ability.dc = make_word(1, 2);
  record.ability.mc = make_word(1, 2);
  record.ability.sc = make_word(1, 2);
  record.ability.max_exp = 100;
  record.ability.max_weight = 30;

  // 出生装备：装备栏第2格为木剑（make_index=100001, index=1）
  record.equipped_items[1].make_index = 100001;
  record.equipped_items[1].index = 1;
  record.equipped_items[1].dura = 1000;
  record.equipped_items[1].dura_max = 1000;

  // 背包初始物品：第1格为蜡烛（make_index=100002, index=2）
  record.bag_items[0].make_index = 100002;
  record.bag_items[0].index = 2;
  record.bag_items[0].dura = 1000;
  record.bag_items[0].dura_max = 1000;

  // 初始魔法：基础魔法（magic_id=1, 等级1, 快捷键F）
  record.magics[0].magic_id = 1;
  record.magics[0].level = 1;
  record.magics[0].key = 'F';
  record.magics[0].cur_train = 0;
  record.birth_items_granted = true;
  return record;
}

/**
 * @brief 将角色记录的所有字段绑定到 SQL 预处理语句
 * @param statement 预处理语句对象（含 49 个参数）
 * @param character 角色记录
 * @details 按参数索引顺序绑定：
 *          - 第 1-31 个参数：基本字段（文本、整数、uint8/uint16）
 *          - 第 32-35 个参数：固定大小数组 BLOB（装备、背包、仓库、魔法）
 *          - 第 36-49 个参数：扩展字段（行会、PK、任务、随从、幸运、版本号）
 *          索引从 1 开始，与 SQL 语句中的 ?N 占位符对应。
 */
void bind_character_fields(sqlite3_stmt* statement, const CharacterRecord& character) {
  bind_text(statement, 1, character.account_id);
  bind_text(statement, 2, character.character_name);
  bind_text(statement, 3, character.map_id);
  sqlite3_bind_int(statement, 4, character.x);
  sqlite3_bind_int(statement, 5, character.y);
  sqlite3_bind_int(statement, 6, character.dir);
  sqlite3_bind_int(statement, 7, character.light);
  sqlite3_bind_int(statement, 8, character.job);
  sqlite3_bind_int(statement, 9, character.sex);
  sqlite3_bind_int(statement, 10, character.hair);
  sqlite3_bind_int(statement, 11, character.gold);
  sqlite3_bind_int(statement, 12, character.feature);
  sqlite3_bind_int(statement, 13, character.status);
  sqlite3_bind_int(statement, 14, character.ability.level);
  sqlite3_bind_int(statement, 15, character.ability.hp);
  sqlite3_bind_int(statement, 16, character.ability.mp);
  sqlite3_bind_int(statement, 17, character.ability.max_hp);
  sqlite3_bind_int(statement, 18, character.ability.max_mp);
  sqlite3_bind_int(statement, 19, character.ability.ac);
  sqlite3_bind_int(statement, 20, character.ability.mac);
  sqlite3_bind_int(statement, 21, character.ability.dc);
  sqlite3_bind_int(statement, 22, character.ability.mc);
  sqlite3_bind_int(statement, 23, character.ability.sc);
  sqlite3_bind_int(statement, 24, static_cast<int>(character.ability.exp));
  sqlite3_bind_int(statement, 25, static_cast<int>(character.ability.max_exp));
  sqlite3_bind_int(statement, 26, character.ability.weight);
  sqlite3_bind_int(statement, 27, character.ability.max_weight);
  sqlite3_bind_int(statement, 28, character.ability.wear_weight);
  sqlite3_bind_int(statement, 29, character.ability.max_wear_weight);
  sqlite3_bind_int(statement, 30, character.ability.hand_weight);
  sqlite3_bind_int(statement, 31, character.ability.max_hand_weight);
  bind_blob_array(statement, 32, character.equipped_items);
  bind_blob_array(statement, 33, character.bag_items);
  bind_blob_array(statement, 34, character.storage_items);
  bind_blob_array(statement, 35, character.magics);
  bind_text(statement, 36, character.guild_name);
  bind_text(statement, 37, character.guild_title);
  sqlite3_bind_int(statement, 38, character.attack_mode);
  sqlite3_bind_int(statement, 39, character.pk_point);
  sqlite3_bind_int64(statement, 40, static_cast<sqlite3_int64>(character.death_time_ms));
  bind_blob_array(statement, 41, character.quest_marks);
  bind_blob_array(statement, 42, character.quest_open_units);
  bind_blob_array(statement, 43, character.quest_units);
  bind_blob_array(statement, 44, character.script_params);
  sqlite3_bind_int64(statement, 45, static_cast<sqlite3_int64>(character.daily_quest));
  const auto slave_blob = encode_slave_blob(character.slaves);
  bind_blob_vector(statement, 46, slave_blob);
  sqlite3_bind_double(statement, 47, character.body_luck);
  sqlite3_bind_int(statement, 48, character.birth_items_granted ? 1 : 0);
  sqlite3_bind_int64(statement, 49, static_cast<sqlite3_int64>(character.save_version));
}

/**
 * @brief 创建默认账户记录
 * @param account_id 账户ID
 * @param password 密码
 * @return 填充了默认值的 AccountRecord 结构体
 * @details 用于开发测试环境或访客账户的创建。
 *          display_name 和 user_name 设为 "Guest"，其他字段填充占位值。
 *          密码失败计数、封禁状态等安全字段均为默认值。
 */
AccountRecord make_default_account(const std::string& account_id, const std::string& password) {
  AccountRecord record;
  record.account_id = account_id;
  record.password = password;
  record.display_name = "Guest";
  record.user_name = "Guest";
  record.ss_no = "000000-0000000";
  record.phone = "000-0000";
  record.quiz = "seed";
  record.answer = "seed";
  record.email = "guest@example.invalid";
  record.quiz2 = "seed2";
  record.answer2 = "seed2";
  record.birthday = "2000/01/01";
  record.mobile_phone = "000-0000-0000";
  record.memo1 = "seed";
  record.memo2 = "seed";
  return record;
}

/**
 * @brief 确保指定账户存在于数据库中（不存在则创建）
 * @param database SQLite 数据库连接指针
 * @param account_id 账户ID
 * @throws std::runtime_error 如果 SQL 预处理或执行失败
 * @details 使用 INSERT OR IGNORE 语义，如果账户已存在则不执行任何操作。
 *          仅在创建时设置 account_id 和 display_name（与 account_id 相同），
 *          其他字段使用表的默认值。
 */
void ensure_account(sqlite3* database, const std::string& account_id) {
  static constexpr const char* kSql =
      "INSERT OR IGNORE INTO accounts(account_id, display_name) VALUES(?1, ?2);";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare ensure_account statement.");
  }

  bind_text(statement, 1, account_id);
  bind_text(statement, 2, account_id);

  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute ensure_account statement.");
  }
  finalize_statement(statement);
}

/**
 * @brief 保存账户记录（含 UPSERT 逻辑）
 * @param database SQLite 数据库连接指针
 * @param account 要保存的账户记录
 * @throws std::runtime_error 如果 SQL 预处理或执行失败
 * @details 使用 INSERT ... ON CONFLICT DO UPDATE 语法：
 *          - 如果 account_id 不存在则插入新记录
 *          - 如果 account_id 已存在则更新除主键外的所有字段
 *          - 更新时自动设置 updated_at = CURRENT_TIMESTAMP
 */
void save_account(sqlite3* database, const AccountRecord& account) {
  static constexpr const char* kSql =
      "INSERT INTO accounts(account_id, password, display_name, user_name, ss_no, phone, quiz,"
      " answer, email, quiz2, answer2, birthday, mobile_phone, memo1, memo2, server_index,"
      " passwd_fail, passwd_fail_time_ms, is_banned)"
      " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18,"
      " ?19)"
      " ON CONFLICT(account_id) DO UPDATE SET password = excluded.password,"
      " display_name = excluded.display_name, user_name = excluded.user_name, ss_no = excluded.ss_no,"
      " phone = excluded.phone, quiz = excluded.quiz, answer = excluded.answer, email = excluded.email,"
      " quiz2 = excluded.quiz2, answer2 = excluded.answer2, birthday = excluded.birthday,"
      " mobile_phone = excluded.mobile_phone, memo1 = excluded.memo1, memo2 = excluded.memo2,"
      " server_index = excluded.server_index, passwd_fail = excluded.passwd_fail,"
      " passwd_fail_time_ms = excluded.passwd_fail_time_ms, is_banned = excluded.is_banned,"
      " updated_at = CURRENT_TIMESTAMP;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare save_account statement.");
  }

  bind_text(statement, 1, account.account_id);
  bind_text(statement, 2, account.password);
  bind_text(statement, 3, account.display_name);
  bind_text(statement, 4, account.user_name);
  bind_text(statement, 5, account.ss_no);
  bind_text(statement, 6, account.phone);
  bind_text(statement, 7, account.quiz);
  bind_text(statement, 8, account.answer);
  bind_text(statement, 9, account.email);
  bind_text(statement, 10, account.quiz2);
  bind_text(statement, 11, account.answer2);
  bind_text(statement, 12, account.birthday);
  bind_text(statement, 13, account.mobile_phone);
  bind_text(statement, 14, account.memo1);
  bind_text(statement, 15, account.memo2);
  sqlite3_bind_int(statement, 16, account.server_index);
  sqlite3_bind_int(statement, 17, account.passwd_fail);
  sqlite3_bind_int64(statement, 18, account.passwd_fail_time_ms);
  sqlite3_bind_int(statement, 19, account.banned ? 1 : 0);

  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute save_account statement.");
  }
  finalize_statement(statement);
}

/**
 * @brief 迁移旧版 characters 表到新版结构
 * @param database SQLite 数据库连接指针
 * @param schema_sql 新表结构的 SQL 定义
 * @details 如果旧表包含 inventory_json 列（旧的 JSON 存储方式）且不包含新版
 *          equipped_blob 列，则执行迁移：
 *          1. 将旧表重命名为 characters_legacy
 *          2. 使用新 schema 创建 characters 表
 *          3. 从旧表迁移兼容字段到新表
 *          4. 删除旧表
 *          整个迁移过程在事务中执行，失败时自动回滚。
 * @note 迁移逻辑通过检查列名自动判断是否需要执行，可重复安全调用。
 */
void migrate_legacy_characters_table(sqlite3* database, const std::string& schema_sql) {
  if (!table_has_column(database, "characters", "inventory_json") ||
      table_has_column(database, "characters", "equipped_blob")) {
    return;
  }

  const auto equip_blob_size =
      std::to_string(sizeof(std::array<LegacyUserItem, kMaxEquipSlots>));
  const auto bag_blob_size = std::to_string(sizeof(std::array<LegacyUserItem, kMaxBagItems>));
  const auto storage_blob_size =
      std::to_string(sizeof(std::array<LegacyUserItem, kMaxSaveItems>));
  const auto magic_blob_size =
      std::to_string(sizeof(std::array<LegacyUseMagicInfo, kMaxUserMagic>));

  exec_or_throw(database, "BEGIN IMMEDIATE;");
  try {
    exec_or_throw(database, "ALTER TABLE characters RENAME TO characters_legacy;");
    exec_or_throw(database, schema_sql);
    exec_or_throw(
        database,
        "INSERT INTO characters(account_id, character_name, map_id, x, y, dir, light, job, sex, hair,"
        " gold, feature, status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp,"
        " weight, max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight,"
        " equipped_blob, bag_blob, storage_blob, magic_blob)"
        " SELECT account_id, character_name, map_id, x, y, 0, 0, 0, 0, 0, 0, 0, 0,"
        " COALESCE(level, 1), COALESCE(hp, 15), COALESCE(mp, 15),"
        " COALESCE(hp, 15), COALESCE(mp, 15),"
        " 0, 0, 0, 0, 0, 0, 100, 0, 30, 0, 100, 0, 100,"
        " zeroblob(" +
            equip_blob_size + "), zeroblob(" + bag_blob_size + "), zeroblob(" + storage_blob_size +
            "), zeroblob(" + magic_blob_size + ") FROM characters_legacy;");
    exec_or_throw(database, "DROP TABLE characters_legacy;");
    exec_or_throw(database, "COMMIT;");
  } catch (...) {
    try {
      exec_or_throw(database, "ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

/**
 * @brief 确保 characters 表包含所有必需的扩展列
 * @param database SQLite 数据库连接指针
 * @details 逐个检查和添加新版本所需的列：
 *          - 外观相关：hair（发型）
 *          - 行会相关：guild_name, guild_title
 *          - 战斗相关：attack_mode, pk_point, death_time_ms
 *          - 任务相关：quest_blob, quest_open_blob, quest_unit_blob, script_param_blob, daily_quest
 *          - 随从系统：slave_blob
 *          - 幸运值：body_luck
 *          - 初始物品：birth_items_granted
 *          - 版本控制：save_version
 *          使用 ALTER TABLE ADD COLUMN 逐个添加缺失的列。
 */
void ensure_characters_columns(sqlite3* database) {
  if (!table_has_column(database, "characters", "hair")) {
    exec_or_throw(database,
                  "ALTER TABLE characters ADD COLUMN hair INTEGER NOT NULL DEFAULT 0;");
  }
  ensure_column(database, "characters", "guild_name",
                "ALTER TABLE characters ADD COLUMN guild_name TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "characters", "guild_title",
                "ALTER TABLE characters ADD COLUMN guild_title TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "characters", "attack_mode",
                "ALTER TABLE characters ADD COLUMN attack_mode INTEGER NOT NULL DEFAULT 1;");
  ensure_column(database, "characters", "pk_point",
                "ALTER TABLE characters ADD COLUMN pk_point INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "characters", "death_time_ms",
                "ALTER TABLE characters ADD COLUMN death_time_ms INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "characters", "quest_blob",
                "ALTER TABLE characters ADD COLUMN quest_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "quest_open_blob",
                "ALTER TABLE characters ADD COLUMN quest_open_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "quest_unit_blob",
                "ALTER TABLE characters ADD COLUMN quest_unit_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "script_param_blob",
                "ALTER TABLE characters ADD COLUMN script_param_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "daily_quest",
                "ALTER TABLE characters ADD COLUMN daily_quest INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "characters", "slave_blob",
                "ALTER TABLE characters ADD COLUMN slave_blob BLOB NOT NULL DEFAULT X'';");
  ensure_column(database, "characters", "body_luck",
                "ALTER TABLE characters ADD COLUMN body_luck REAL NOT NULL DEFAULT 0;");
  ensure_column(database, "characters", "birth_items_granted",
                "ALTER TABLE characters ADD COLUMN birth_items_granted INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "characters", "save_version",
                "ALTER TABLE characters ADD COLUMN save_version INTEGER NOT NULL DEFAULT 0;");
}

/**
 * @brief 确保 merchant_state 表包含武器升级 BLOB 列
 * @param database SQLite 数据库连接指针
 * @details 检查并添加 upgrade_blob 列，用于存储商人武器升级记录。
 *          该列存储通过 encode_weapon_upgrade_blob 编码的二进制数据。
 */
void ensure_merchant_state_columns(sqlite3* database) {
  ensure_column(database, "merchant_state", "upgrade_blob",
                "ALTER TABLE merchant_state ADD COLUMN upgrade_blob BLOB NOT NULL DEFAULT X'';");
}

/**
 * @brief 确保 accounts 表包含所有必需的列，并修复数据
 * @param database SQLite 数据库连接指针
 * @details 逐个检查和添加 accounts 表所需的列：
 *          - 安全字段：password, passwd_fail, passwd_fail_time_ms, is_banned
 *          - 个人资料：user_name, ss_no, phone, birthday, mobile_phone
 *          - 密保问题：quiz, answer, quiz2, answer2
 *          - 备注字段：memo1, memo2
 *          - 服务器索引：server_index
 *          - 时间戳：updated_at
 *        列添加完成后，执行数据修复：
 *          - 填充空的 display_name 为 account_id
 *          - 修复空的 updated_at 为 created_at 或当前时间
 */
void ensure_accounts_columns(sqlite3* database) {
  ensure_column(database, "accounts", "password",
                "ALTER TABLE accounts ADD COLUMN password TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "user_name",
                "ALTER TABLE accounts ADD COLUMN user_name TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "ss_no",
                "ALTER TABLE accounts ADD COLUMN ss_no TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "phone",
                "ALTER TABLE accounts ADD COLUMN phone TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "quiz",
                "ALTER TABLE accounts ADD COLUMN quiz TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "answer",
                "ALTER TABLE accounts ADD COLUMN answer TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "email",
                "ALTER TABLE accounts ADD COLUMN email TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "quiz2",
                "ALTER TABLE accounts ADD COLUMN quiz2 TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "answer2",
                "ALTER TABLE accounts ADD COLUMN answer2 TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "birthday",
                "ALTER TABLE accounts ADD COLUMN birthday TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "mobile_phone",
                "ALTER TABLE accounts ADD COLUMN mobile_phone TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "memo1",
                "ALTER TABLE accounts ADD COLUMN memo1 TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "memo2",
                "ALTER TABLE accounts ADD COLUMN memo2 TEXT NOT NULL DEFAULT '';");
  ensure_column(database, "accounts", "server_index",
                "ALTER TABLE accounts ADD COLUMN server_index INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "accounts", "passwd_fail",
                "ALTER TABLE accounts ADD COLUMN passwd_fail INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "accounts", "passwd_fail_time_ms",
                "ALTER TABLE accounts ADD COLUMN passwd_fail_time_ms INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "accounts", "is_banned",
                "ALTER TABLE accounts ADD COLUMN is_banned INTEGER NOT NULL DEFAULT 0;");
  ensure_column(database, "accounts", "updated_at",
                "ALTER TABLE accounts ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';");
  // 修复缺失的 display_name：设置为 account_id
  exec_or_throw(
      database,
      "UPDATE accounts SET display_name = account_id WHERE COALESCE(display_name, '') = '';");
  // 修复空的 updated_at：使用 created_at 或当前时间作为备选
  exec_or_throw(database,
                "UPDATE accounts SET updated_at = COALESCE(NULLIF(updated_at, ''), created_at,"
                " CURRENT_TIMESTAMP);");
}

}  // namespace

/**
 * @brief 构造函数 - 打开或创建 SQLite 数据库连接
 * @param database_path 数据库文件路径
 * @throws std::runtime_error 如果无法打开数据库或设置 PRAGMA 失败
 * @details 自动创建数据库文件所在目录（如果不存在），
 *          然后打开 SQLite 数据库连接，并设置运行参数：
 *          - WAL 日志模式：提高并发读取性能
 *          - NORMAL 同步模式：在安全性与写入性能之间取得平衡
 */
Repository::Repository(const std::filesystem::path& database_path) {
  std::filesystem::create_directories(database_path.parent_path());
  if (sqlite3_open(database_path.string().c_str(), &database_) != SQLITE_OK) {
    throw std::runtime_error("Failed to open sqlite database: " + database_path.string());
  }
  exec_or_throw(database_, "PRAGMA journal_mode = WAL;");
  exec_or_throw(database_, "PRAGMA synchronous = NORMAL;");
}

/**
 * @brief 析构函数 - 关闭数据库连接
 * @details 如果数据库连接指针不为空，则关闭连接并将指针置空。
 *          安全可重复调用。
 */
Repository::~Repository() {
  if (database_ != nullptr) {
    sqlite3_close(database_);
    database_ = nullptr;
  }
}

/**
 * @brief 确保数据库模式是最新的
 * @param schema_path SQL 模式文件路径
 * @throws std::runtime_error 如果 SQL 执行失败
 * @details 执行顺序：
 *          1. 读取并执行 schema SQL 文件创建基础表结构
 *          2. 检测并迁移旧版 characters 表（如果存在）
 *          3. 确保 accounts 表包含所有新列
 *          4. 确保 characters 表包含所有新列
 *          5. 确保 merchant_state 表包含所有新列
 */
void Repository::ensure_schema(const std::filesystem::path& schema_path) {
  const auto schema_sql = read_text_file(schema_path);
  exec_or_throw(database_, schema_sql);
  migrate_legacy_characters_table(database_, schema_sql);
  ensure_accounts_columns(database_);
  ensure_characters_columns(database_);
  ensure_merchant_state_columns(database_);
}

/**
 * @brief 填充运行时种子数据
 * @details 创建默认的访客账户（guest/pass）及其初始角色（Hero）。
 *          用于开发和测试环境的基础数据初始化。
 */
void Repository::seed_runtime() {
  save_account(database_, make_default_account("guest", "pass"));
  save_character(make_default_character("guest", "Hero"));
}

/**
 * @brief 根据账户ID加载账户记录
 * @param account_id 账户唯一标识
 * @return 如果找到则返回 AccountRecord，否则返回 std::nullopt
 * @throws std::runtime_error 如果 SQL 预处理失败
 */
std::optional<AccountRecord> Repository::load_account(const std::string& account_id) {
  static constexpr const char* kSql =
      "SELECT account_id, password, display_name, user_name, ss_no, phone, quiz, answer, email,"
      " quiz2, answer2, birthday, mobile_phone, memo1, memo2, server_index, passwd_fail,"
      " passwd_fail_time_ms, is_banned FROM accounts WHERE account_id = ?1 LIMIT 1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_account statement.");
  }

  bind_text(statement, 1, account_id);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    auto account = read_account_row(statement);
    finalize_statement(statement);
    return account;
  }

  finalize_statement(statement);
  return std::nullopt;
}

/**
 * @brief 加载城堡对话框上下文数据
 * @return CastleDialogContext 包含城堡名称、领主、战争日期、费用等信息
 * @throws std::runtime_error 如果 SQL 预处理失败
 * @details 先从 castle_state 表加载第一个城堡记录的 JSON payload，
 *          解析其中的 owner_guild、lord、战争日期、费用等信息。
 *          然后根据 owner_guild 查询 guilds 表补充领主名称。
 *          支持多种 JSON 字段命名风格的兼容性解析。
 */
CastleDialogContext Repository::load_castle_dialog_context() {
  CastleDialogContext context;

  static constexpr const char* kCastleSql =
      "SELECT castle_name, payload_json FROM castle_state ORDER BY castle_id ASC LIMIT 1;";
  sqlite3_stmt* castle_statement = nullptr;
  if (sqlite3_prepare_v2(database_, kCastleSql, -1, &castle_statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_castle_dialog_context castle query.");
  }

  std::string owner_guild;
  if (sqlite3_step(castle_statement) == SQLITE_ROW) {
    context.castle_name = column_text(castle_statement, 0);
    const auto payload = column_text(castle_statement, 1);
    // 解析 owner_guild，排除遗留数据的"无主"标记
    if (const auto value = json_string_field(payload, "owner_guild"); value.has_value() &&
                                                                !value->empty()) {
      if (!is_legacy_unclaimed(*value)) {
        context.owner_guild = *value;
        owner_guild = *value;
      }
    }
    // 解析领主名称（支持 lord 字段）
    if (const auto value = json_string_field(payload, "lord"); value.has_value() && !value->empty()) {
      if (!is_legacy_unclaimed(*value)) {
        context.lord = *value;
      }
    }
    // 解析城堡战争日期（兼容 castle_war_date 和 war_date 两种命名）
    if (const auto value = json_string_field(payload, "castle_war_date");
        value.has_value() && !value->empty()) {
      context.castle_war_date = *value;
    } else if (const auto value = json_string_field(payload, "war_date");
               value.has_value() && !value->empty()) {
      context.castle_war_date = *value;
    }
    // 解析战争列表（兼容字符串数组和逗号分隔字符串两种格式）
    if (const auto value = json_string_field(payload, "list_of_war");
        value.has_value() && !value->empty()) {
      context.list_of_war = *value;
    } else if (const auto wars = json_string_array_field(payload, "list_of_war");
               wars.has_value() && !wars->empty()) {
      context.list_of_war = join_strings(*wars, ", ");
    } else if (const auto wars = json_string_array_field(payload, "wars");
               wars.has_value() && !wars->empty()) {
      context.list_of_war = join_strings(*wars, ", ");
    }
    // 解析费用信息
    if (const auto value = json_int_field(payload, "guild_war_fee"); value.has_value()) {
      context.guild_war_fee = *value;
    }
    if (const auto value = json_int_field(payload, "upgrade_weapon_fee"); value.has_value()) {
      context.upgrade_weapon_fee = *value;
    }
    if (const auto value = json_int_field(payload, "guild_create_fee"); value.has_value()) {
      context.guild_create_fee = *value;
    }
  }
  finalize_statement(castle_statement);

  // 如果有归属行会，查询行会补充领主名称（如果城堡记录中未提供）
  if (!owner_guild.empty()) {
    static constexpr const char* kGuildSql =
        "SELECT payload_json FROM guilds WHERE guild_name = ?1 LIMIT 1;";
    sqlite3_stmt* guild_statement = nullptr;
    if (sqlite3_prepare_v2(database_, kGuildSql, -1, &guild_statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare load_castle_dialog_context guild query.");
    }

    bind_text(guild_statement, 1, owner_guild);
    if (sqlite3_step(guild_statement) == SQLITE_ROW) {
      const auto payload = column_text(guild_statement, 0);
      // 如果城堡记录中没有领主，从行会数据中获取
      // 支持 lord/chief/master 多种命名
      if (context.lord.empty()) {
        if (const auto value = json_string_field(payload, "lord"); value.has_value() &&
                                                                  !value->empty()) {
          context.lord = *value;
        } else if (const auto value = json_string_field(payload, "chief");
                   value.has_value() && !value->empty()) {
          context.lord = *value;
        } else if (const auto value = json_string_field(payload, "master");
                   value.has_value() && !value->empty()) {
          context.lord = *value;
        }
      }
    }
    finalize_statement(guild_statement);
  }

  return context;
}

/**
 * @brief 加载行会和城堡快照数据
 * @return GuildCastleSnapshot 包含城堡对话框上下文和所有行会状态列表
 * @throws std::runtime_error 如果 SQL 预处理失败
 * @details 先加载城堡上下文数据，然后从 guilds 表加载所有行会状态，
 *          行会按名称升序排序返回。
 */
GuildCastleSnapshot Repository::load_guild_castle_snapshot() {
  GuildCastleSnapshot snapshot;
  snapshot.castle_dialog = load_castle_dialog_context();

  static constexpr const char* kGuildSql =
      "SELECT guild_name, payload_json FROM guilds ORDER BY guild_name ASC;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kGuildSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_guild_castle_snapshot statement.");
  }

  while (sqlite3_step(statement) == SQLITE_ROW) {
    snapshot.guilds.push_back(read_guild_state_row(statement));
  }
  finalize_statement(statement);
  return snapshot;
}

/**
 * @brief 保存行会 JSON payload
 * @param guild_name 行会名称
 * @param payload_json 行会数据的 JSON 字符串
 * @throws std::runtime_error 如果 SQL 预处理或执行失败
 * @details 使用 UPSERT 语义（INSERT OR REPLACE）：
 *          - 如果行会不存在则插入新记录
 *          - 如果行会已存在则更新 payload_json
 */
void Repository::save_guild_payload(const std::string& guild_name, const std::string& payload_json) {
  static constexpr const char* kSql =
      "INSERT INTO guilds(guild_name, payload_json) VALUES(?1, ?2)"
      " ON CONFLICT(guild_name) DO UPDATE SET payload_json = excluded.payload_json;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare save_guild_payload statement.");
  }

  bind_text(statement, 1, guild_name);
  bind_text(statement, 2, payload_json);
  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute save_guild_payload statement.");
  }
  finalize_statement(statement);
}

/**
 * @brief 保存规范化后的行会状态
 * @param guild_state 行会状态结构体
 * @details 在保存前对数据进行规范化处理：
 *          1. 去除行会名称和领主的首尾空白
 *          2. 成员列表去重
 *          3. 确保领主在成员列表中
 *          4. 过滤申请者中已经是成员的数据
 *          最后将规范化后的数据转为 JSON 格式并调用 save_guild_payload 保存。
 */
void Repository::save_guild_state(const GuildState& guild_state) {
  auto normalized = guild_state;
  normalized.guild_name = trim_copy(normalized.guild_name);
  normalized.lord = trim_copy(normalized.lord);
  // 成员去重
  std::vector<std::string> members;
  for (const auto& member : normalized.members) {
    push_unique_member(members, member);
  }
  normalized.members = std::move(members);
  // 确保领主也在成员列表中
  if (!normalized.lord.empty()) {
    push_unique_member(normalized.members, normalized.lord);
  }
  // 过滤申请者：去除空字符串和已是成员的数据
  std::vector<std::string> applicants;
  for (const auto& applicant : normalized.applicants) {
    const auto normalized_applicant = trim_copy(applicant);
    if (normalized_applicant.empty()) {
      continue;
    }
    const auto already_member =
        std::any_of(normalized.members.begin(), normalized.members.end(),
                    [&](const std::string& member) {
                      return trim_copy(member) == normalized_applicant;
                    });
    if (!already_member) {
      push_unique_member(applicants, normalized_applicant);
    }
  }
  normalized.applicants = std::move(applicants);

  save_guild_payload(normalized.guild_name, build_guild_payload_json(normalized));
}

/**
 * @brief 删除行会
 * @param guild_name 要删除的行会名称
 * @throws std::runtime_error 如果 SQL 预处理或执行失败
 * @details 分两步操作：
 *          1. 清除所有角色中对该行会的引用（将 guild_name 和 guild_title 置空）
 *          2. 从 guilds 表中删除行会记录
 *          @warning 该方法不在显式事务中执行，如果两步之间发生错误，
 *                   角色表中的引用可能不会被清除。
 */
void Repository::delete_guild(const std::string& guild_name) {
  // 第一步：清除所有角色中对该行会的引用
  static constexpr const char* kClearCharactersSql =
      "UPDATE characters SET guild_name = '', guild_title = '' WHERE guild_name = ?1;";

  sqlite3_stmt* clear_statement = nullptr;
  if (sqlite3_prepare_v2(database_, kClearCharactersSql, -1, &clear_statement, nullptr) !=
      SQLITE_OK) {
    throw std::runtime_error("Failed to prepare clear_guild_characters statement.");
  }

  bind_text(clear_statement, 1, guild_name);
  if (sqlite3_step(clear_statement) != SQLITE_DONE) {
    finalize_statement(clear_statement);
    throw std::runtime_error("Failed to execute clear_guild_characters statement.");
  }
  finalize_statement(clear_statement);

  // 第二步：从 guilds 表中删除行会记录
  static constexpr const char* kSql = "DELETE FROM guilds WHERE guild_name = ?1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare delete_guild statement.");
  }

  bind_text(statement, 1, guild_name);
  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute delete_guild statement.");
  }
  finalize_statement(statement);
}

/**
 * @brief 保存城堡状态
 * @param castle_name 城堡名称
 * @param payload_json 城堡状态的 JSON 字符串
 * @throws std::runtime_error 如果 SQL 预处理或执行失败
 * @details 使用 UPSERT 语义，如果城堡已存在则更新 payload_json，否则插入新记录。
 */
void Repository::save_castle_state(const std::string& castle_name, const std::string& payload_json) {
  static constexpr const char* kSql =
      "INSERT INTO castle_state(castle_name, payload_json) VALUES(?1, ?2)"
      " ON CONFLICT(castle_name) DO UPDATE SET payload_json = excluded.payload_json;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare save_castle_state statement.");
  }

  bind_text(statement, 1, castle_name);
  bind_text(statement, 2, payload_json);
  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute save_castle_state statement.");
  }
  finalize_statement(statement);
}

/**
 * @brief 加载所有商人状态
 * @return MerchantStateRecord 向量，包含商人货物、武器升级记录和价格信息
 * @throws std::runtime_error 如果 SQL 预处理失败
 * @details 分两步查询：
 *          第一步：从 merchant_state 表加载所有商人的基本状态和 BLOB 数据
 *          第二步：从 merchant_prices 表加载价格信息
 *          使用哈希表（merchant_key -> 数组索引）高效关联价格数据到对应商人。
 */
std::vector<MerchantStateRecord> Repository::load_merchant_states() {
  // 第一步：加载商人基本状态
  static constexpr const char* kStateSql =
      "SELECT merchant_key, npc_id, map_id, goods_blob, upgrade_blob FROM merchant_state"
      " ORDER BY merchant_key;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kStateSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_merchant_states statement.");
  }

  std::vector<MerchantStateRecord> states;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    MerchantStateRecord state;
    state.merchant_key = column_text(statement, 0);
    state.npc_id = column_text(statement, 1);
    state.map_id = column_text(statement, 2);
    state.goods = decode_merchant_goods_blob(statement, 3);
    state.weapon_upgrades = decode_weapon_upgrade_blob(statement, 4);
    states.push_back(std::move(state));
  }
  finalize_statement(statement);

  // 第二步：加载价格信息并关联到商人
  static constexpr const char* kPricesSql =
      "SELECT merchant_key, item_id, sell_price FROM merchant_prices ORDER BY merchant_key, item_id;";
  statement = nullptr;
  if (sqlite3_prepare_v2(database_, kPricesSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_merchant_prices statement.");
  }

  // 建立 merchant_key 到数组索引的哈希映射
  std::unordered_map<std::string, std::size_t> index_by_key;
  for (std::size_t i = 0; i < states.size(); ++i) {
    index_by_key[states[i].merchant_key] = i;
  }
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto key = column_text(statement, 0);
    const auto state_it = index_by_key.find(key);
    if (state_it == index_by_key.end()) {
      continue;
    }
    states[state_it->second].prices[sqlite3_column_int(statement, 1)] =
        sqlite3_column_int(statement, 2);
  }
  finalize_statement(statement);
  return states;
}

/**
 * @brief 保存商人状态（含事务保护）
 * @param state 商人状态记录
 * @throws std::runtime_error 如果任何一步失败（事务自动回滚）
 * @details 在一个事务中执行三步操作：
 *          1. 更新/插入 merchant_state 表（NPC信息、货物BLOB、升级BLOB）
 *          2. 删除该商人的所有旧价格记录
 *          3. 逐条插入新的价格记录
 *          如果 merchant_key 为空则直接返回不执行任何操作。
 *          任何步骤失败都会导致事务回滚。
 */
void Repository::save_merchant_state(const MerchantStateRecord& state) {
  if (state.merchant_key.empty()) {
    return;
  }
  exec_or_throw(database_, "BEGIN IMMEDIATE;");
  try {
    // 第一步：保存商人基本状态（含 UPSERT）
    static constexpr const char* kStateSql =
        "INSERT INTO merchant_state(merchant_key, npc_id, map_id, goods_blob, upgrade_blob)"
        " VALUES(?1, ?2, ?3, ?4, ?5)"
        " ON CONFLICT(merchant_key) DO UPDATE SET npc_id = excluded.npc_id,"
        " map_id = excluded.map_id, goods_blob = excluded.goods_blob,"
        " upgrade_blob = excluded.upgrade_blob,"
        " updated_at = CURRENT_TIMESTAMP;";
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kStateSql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare save_merchant_state statement.");
    }
    const auto goods_blob = encode_merchant_goods_blob(state.goods);
    const auto upgrade_blob = encode_weapon_upgrade_blob(state.weapon_upgrades);
    bind_text(statement, 1, state.merchant_key);
    bind_text(statement, 2, state.npc_id);
    bind_text(statement, 3, state.map_id);
    bind_blob_vector(statement, 4, goods_blob);
    bind_blob_vector(statement, 5, upgrade_blob);
    if (sqlite3_step(statement) != SQLITE_DONE) {
      finalize_statement(statement);
      throw std::runtime_error("Failed to execute save_merchant_state statement.");
    }
    finalize_statement(statement);

    // 第二步：清除该商人的旧价格数据
    static constexpr const char* kDeleteSql = "DELETE FROM merchant_prices WHERE merchant_key = ?1;";
    if (sqlite3_prepare_v2(database_, kDeleteSql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare clear_merchant_prices statement.");
    }
    bind_text(statement, 1, state.merchant_key);
    if (sqlite3_step(statement) != SQLITE_DONE) {
      finalize_statement(statement);
      throw std::runtime_error("Failed to execute clear_merchant_prices statement.");
    }
    finalize_statement(statement);

    // 第三步：逐条插入新的价格数据
    static constexpr const char* kPriceSql =
        "INSERT INTO merchant_prices(merchant_key, item_id, sell_price) VALUES(?1, ?2, ?3);";
    for (const auto& [item_id, sell_price] : state.prices) {
      if (sqlite3_prepare_v2(database_, kPriceSql, -1, &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare save_merchant_price statement.");
      }
      bind_text(statement, 1, state.merchant_key);
      sqlite3_bind_int(statement, 2, item_id);
      sqlite3_bind_int(statement, 3, sell_price);
      if (sqlite3_step(statement) != SQLITE_DONE) {
        finalize_statement(statement);
        throw std::runtime_error("Failed to execute save_merchant_price statement.");
      }
      finalize_statement(statement);
    }
    exec_or_throw(database_, "COMMIT;");
  } catch (...) {
    try {
      exec_or_throw(database_, "ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

/**
 * @brief 认证账户登录
 * @param account_id 账户ID
 * @param password 明文密码
 * @param now_ms 当前时间戳（毫秒）
 * @return AccountOperationResult 包含状态码和账户信息
 * @details 认证流程：
 *          1. 检查账户是否存在（不存在返回状态码 -4）
 *          2. 检查账户是否被封禁（已封禁返回状态码 -5）
 *          3. 检查密码失败次数 >= 5 次且距上次失败在 60 秒内（锁定返回状态码 -2）
 *          4. 验证密码是否匹配（不匹配返回状态码 -1，并递增失败计数）
 *          5. 认证成功时重置失败计数和时间戳（返回状态码 1）
 *          @note 每次认证失败或成功都会持久化更新数据库中的失败计数和时间戳。
 */
AccountOperationResult Repository::authenticate_account(const std::string& account_id,
                                                        const std::string& password,
                                                        std::int64_t now_ms) {
  auto account = load_account(account_id);
  if (!account.has_value()) {
    return AccountOperationResult{-4, std::nullopt};
  }
  if (account->banned) {
    return AccountOperationResult{-5, account};
  }

  // 检查登录锁定：密码失败 5 次以上且距上次失败不超过 60 秒
  const auto login_locked =
      account->passwd_fail >= 5 && elapsed_ms(now_ms, account->passwd_fail_time_ms) <= 60 * 1000;
  if (login_locked) {
    account->passwd_fail_time_ms = now_ms;
    save_account(database_, *account);
    return AccountOperationResult{-2, account};
  }

  if (account->password == password) {
    // 认证成功：重置失败计数
    account->passwd_fail = 0;
    account->passwd_fail_time_ms = 0;
    save_account(database_, *account);
    return AccountOperationResult{1, account};
  }

  // 密码错误：递增失败计数并更新时间戳
  account->passwd_fail += 1;
  account->passwd_fail_time_ms = now_ms;
  save_account(database_, *account);
  return AccountOperationResult{-1, account};
}

/**
 * @brief 创建新账户
 * @param account 账户记录
 * @return true 如果创建成功，false 如果 account_id 已存在（违反 UNIQUE 约束）
 * @throws std::runtime_error 如果 SQL 预处理失败
 * @details 使用 INSERT 语句，如果 account_id 已存在，
 *          由于 UNIQUE 约束冲突，sqlite3_step 会返回非 SQLITE_DONE。
 */
bool Repository::create_account(const AccountRecord& account) {
  static constexpr const char* kSql =
      "INSERT INTO accounts(account_id, password, display_name, user_name, ss_no, phone, quiz,"
      " answer, email, quiz2, answer2, birthday, mobile_phone, memo1, memo2, server_index,"
      " passwd_fail, passwd_fail_time_ms, is_banned)"
      " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18,"
      " ?19);";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare create_account statement.");
  }

  bind_text(statement, 1, account.account_id);
  bind_text(statement, 2, account.password);
  bind_text(statement, 3, account.display_name);
  bind_text(statement, 4, account.user_name);
  bind_text(statement, 5, account.ss_no);
  bind_text(statement, 6, account.phone);
  bind_text(statement, 7, account.quiz);
  bind_text(statement, 8, account.answer);
  bind_text(statement, 9, account.email);
  bind_text(statement, 10, account.quiz2);
  bind_text(statement, 11, account.answer2);
  bind_text(statement, 12, account.birthday);
  bind_text(statement, 13, account.mobile_phone);
  bind_text(statement, 14, account.memo1);
  bind_text(statement, 15, account.memo2);
  sqlite3_bind_int(statement, 16, account.server_index);
  sqlite3_bind_int(statement, 17, account.passwd_fail);
  sqlite3_bind_int64(statement, 18, account.passwd_fail_time_ms);
  sqlite3_bind_int(statement, 19, account.banned ? 1 : 0);

  const auto result = sqlite3_step(statement);
  finalize_statement(statement);
  return result == SQLITE_DONE;
}

/**
 * @brief 更新账户信息
 * @param account 包含更新后数据的账户记录
 * @return true 如果至少有一行被更新
 * @throws std::runtime_error 如果 SQL 预处理失败
 * @details 使用 account_id 作为 WHERE 条件，更新除主键外的指定字段。
 *          同时自动更新 updated_at 时间戳为 CURRENT_TIMESTAMP。
 *          @note 不更新 passwd_fail、passwd_fail_time_ms 和 is_banned 等安全相关字段，
 *                这些字段由专门的认证方法管理。
 */
bool Repository::update_account(const AccountRecord& account) {
  static constexpr const char* kSql =
      "UPDATE accounts SET password = ?2, display_name = ?3, user_name = ?4, ss_no = ?5,"
      " phone = ?6, quiz = ?7, answer = ?8, email = ?9, quiz2 = ?10, answer2 = ?11,"
      " birthday = ?12, mobile_phone = ?13, memo1 = ?14, memo2 = ?15,"
      " updated_at = CURRENT_TIMESTAMP WHERE account_id = ?1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare update_account statement.");
  }

  bind_text(statement, 1, account.account_id);
  bind_text(statement, 2, account.password);
  bind_text(statement, 3, account.display_name);
  bind_text(statement, 4, account.user_name);
  bind_text(statement, 5, account.ss_no);
  bind_text(statement, 6, account.phone);
  bind_text(statement, 7, account.quiz);
  bind_text(statement, 8, account.answer);
  bind_text(statement, 9, account.email);
  bind_text(statement, 10, account.quiz2);
  bind_text(statement, 11, account.answer2);
  bind_text(statement, 12, account.birthday);
  bind_text(statement, 13, account.mobile_phone);
  bind_text(statement, 14, account.memo1);
  bind_text(statement, 15, account.memo2);

  const auto result = sqlite3_step(statement);
  const auto changes = sqlite3_changes(database_);
  finalize_statement(statement);
  return result == SQLITE_DONE && changes > 0;
}

/**
 * @brief 修改账户密码
 * @param account_id 账户ID
 * @param password 当前密码（用于验证）
 * @param new_password 新密码（长度至少3个字符）
 * @param now_ms 当前时间戳（毫秒）
 * @return 1=成功, 0=账户不存在或新密码太短, -1=当前密码错误, -2=修改锁定
 * @details 修改锁定条件：密码失败 >= 5 次且距上次失败在 3 分钟内。
 *          成功修改后重置失败计数和时间戳。
 *          密码验证失败时会递增失败计数。
 */
std::int32_t Repository::change_password(const std::string& account_id, const std::string& password,
                                         const std::string& new_password, std::int64_t now_ms) {
  if (new_password.size() < 3) {
    return 0;
  }

  auto account = load_account(account_id);
  if (!account.has_value()) {
    return 0;
  }

  // 检查修改锁定：密码失败 5 次以上且距上次失败不超过 3 分钟
  const auto change_locked =
      account->passwd_fail >= 5 && elapsed_ms(now_ms, account->passwd_fail_time_ms) <= 3 * 60 * 1000;
  if (change_locked) {
    return -2;
  }

  if (account->password == password) {
    // 密码验证成功：更新密码并重置失败计数
    account->password = new_password;
    account->passwd_fail = 0;
    account->passwd_fail_time_ms = 0;
    save_account(database_, *account);
    return 1;
  }

  // 密码验证失败：递增失败计数
  account->passwd_fail += 1;
  account->passwd_fail_time_ms = now_ms;
  save_account(database_, *account);
  return -1;
}

/**
 * @brief 加载指定账户下的特定角色
 * @param account_id 账户ID
 * @param character_name 角色名
 * @return 如果找到则返回 CharacterRecord，否则返回 std::nullopt
 * @throws std::runtime_error 如果 SQL 预处理失败
 */
std::optional<CharacterRecord> Repository::load_character(const std::string& account_id,
                                                          const std::string& character_name) {
  static constexpr const char* kSql =
      "SELECT account_id, character_name, map_id, x, y, dir, light, job, sex, hair, gold, feature,"
      " status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp, weight,"
      " max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight, equipped_blob,"
      " bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode, pk_point,"
      " death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
      " daily_quest, slave_blob, body_luck, birth_items_granted, save_version FROM characters"
      " WHERE account_id = ?1 AND character_name = ?2"
      " LIMIT 1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_character statement.");
  }

  bind_text(statement, 1, account_id);
  bind_text(statement, 2, character_name);

  CharacterRecord record;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    record = read_character_row(statement);
    finalize_statement(statement);
    return record;
  }

  finalize_statement(statement);
  return std::nullopt;
}

/**
 * @brief 根据角色名加载角色（全局唯一）
 * @param character_name 角色名
 * @return 如果找到则返回 CharacterRecord，否则返回 std::nullopt
 * @throws std::runtime_error 如果 SQL 预处理失败
 * @details 按 updated_at 降序、account_id 升序排序后取第一条。
 *          这允许同一角色名在不同账户下存在时返回最新更新的那个。
 */
std::optional<CharacterRecord> Repository::load_character_by_name(
    const std::string& character_name) {
  static constexpr const char* kSql =
      "SELECT account_id, character_name, map_id, x, y, dir, light, job, sex, hair, gold, feature,"
      " status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp, weight,"
      " max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight, equipped_blob,"
      " bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode, pk_point,"
      " death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
      " daily_quest, slave_blob, body_luck, birth_items_granted, save_version FROM characters"
      " WHERE character_name = ?1"
      " ORDER BY updated_at DESC, account_id ASC"
      " LIMIT 1;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare load_character_by_name statement.");
  }

  bind_text(statement, 1, character_name);
  if (sqlite3_step(statement) == SQLITE_ROW) {
    auto record = read_character_row(statement);
    finalize_statement(statement);
    return record;
  }

  finalize_statement(statement);
  return std::nullopt;
}

/**
 * @brief 列出指定账户下的所有角色
 * @param account_id 账户ID
 * @return CharacterRecord 向量，按 updated_at 降序、角色名升序排列
 * @throws std::runtime_error 如果 SQL 预处理失败
 */
std::vector<CharacterRecord> Repository::list_characters(const std::string& account_id) {
  static constexpr const char* kSql =
      "SELECT account_id, character_name, map_id, x, y, dir, light, job, sex, hair, gold, feature,"
      " status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp, weight,"
      " max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight, equipped_blob,"
      " bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode, pk_point,"
      " death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
      " daily_quest, slave_blob, body_luck, birth_items_granted, save_version FROM characters WHERE account_id = ?1"
      " ORDER BY updated_at DESC, character_name ASC;";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare list_characters statement.");
  }

  bind_text(statement, 1, account_id);
  std::vector<CharacterRecord> characters;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    characters.push_back(read_character_row(statement));
  }
  finalize_statement(statement);
  return characters;
}

/**
 * @brief 创建新角色
 * @param character 角色记录
 * @return true 如果创建成功
 * @throws std::runtime_error 如果 SQL 预处理失败
 * @details 创建流程：
 *          1. 自动确保账户存在（如果账户不存在则创建）
 *          2. 检查墓碑表中的版本号，如果墓碑版本 >= 保存版本，
 *             说明该角色之前被删除过，自动递增保存版本号以绕过墓碑屏障
 *          3. 执行 INSERT 语句创建角色记录
 */
bool Repository::create_character(const CharacterRecord& character) {
  ensure_account(database_, character.account_id);
  auto character_to_create = character;
  // 检查墓碑版本号，防止已删除角色被重新创建
  const auto tombstone_version =
      load_save_version(database_, character.account_id, character.character_name,
                        "character_save_tombstones");
  if (tombstone_version.has_value() && *tombstone_version >= character_to_create.save_version) {
    character_to_create.save_version = *tombstone_version + 1;
  }

  static constexpr const char* kSql =
      "INSERT INTO characters(account_id, character_name, map_id, x, y, dir, light, job, sex, hair,"
      " gold, feature, status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp,"
      " weight, max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight,"
      " equipped_blob, bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode,"
      " pk_point, death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
      " daily_quest, slave_blob, body_luck, birth_items_granted, save_version)"
      " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18,"
      " ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, ?31, ?32, ?33, ?34, ?35, ?36,"
      " ?37, ?38, ?39, ?40, ?41, ?42, ?43, ?44, ?45, ?46, ?47, ?48, ?49);";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare create_character statement.");
  }

  bind_character_fields(statement, character_to_create);

  const auto result = sqlite3_step(statement);
  finalize_statement(statement);
  return result == SQLITE_DONE;
}

/**
 * @brief 删除角色（软删除，含版本号屏障和墓碑记录）
 * @param account_id 账户ID
 * @param character_name 角色名
 * @return true 如果角色被成功删除
 * @throws std::runtime_error 如果 SQL 执行失败
 * @details 采用 tombstone 模式的删除策略，有效防止已删除角色数据复活：
 *          1. 从 characters 表读取角色的当前 save_version
 *          2. 如果角色不存在返回 false
 *          3. 计算屏障版本号（下一个 2^48 对齐值，见 deleted_character_save_version_barrier）
 *          4. 在事务中执行：
 *             a. 在 character_save_tombstones 表中插入/更新墓碑记录
 *             b. 从 characters 表中删除角色记录
 *             c. 如果删除操作影响了 0 行则回滚（可能已被其他操作删除）
 *          @note 屏障版本号确保后续任何 save_version 小于该值的保存请求都会被拒绝。
 */
bool Repository::delete_character(const std::string& account_id, const std::string& character_name) {
  // 读取当前 save_version
  const auto save_version =
      load_save_version(database_, account_id, character_name, "characters");
  if (!save_version.has_value()) {
    return false;
  }
  // 计算屏障版本号
  const auto tombstone_save_version = deleted_character_save_version_barrier(*save_version);
  exec_or_throw(database_, "BEGIN IMMEDIATE;");
  bool deleted = false;
  try {
    // 插入/更新墓碑记录（使用 MAX 确保版本号单调递增）
    static constexpr const char* kTombstoneSql =
        "INSERT INTO character_save_tombstones(account_id, character_name, save_version, deleted_at)"
        " VALUES(?1, ?2, ?3, CURRENT_TIMESTAMP)"
        " ON CONFLICT(account_id, character_name) DO UPDATE SET"
        " save_version = MAX(character_save_tombstones.save_version, excluded.save_version),"
        " deleted_at = CURRENT_TIMESTAMP;";
    static constexpr const char* kDeleteSql =
        "DELETE FROM characters WHERE account_id = ?1 AND character_name = ?2;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kTombstoneSql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare delete_character tombstone statement.");
    }
    bind_text(statement, 1, account_id);
    bind_text(statement, 2, character_name);
    sqlite3_bind_int64(statement, 3, static_cast<sqlite3_int64>(tombstone_save_version));
    if (sqlite3_step(statement) != SQLITE_DONE) {
      finalize_statement(statement);
      throw std::runtime_error("Failed to execute delete_character tombstone statement.");
    }
    finalize_statement(statement);

    // 从 characters 表中删除角色
    if (sqlite3_prepare_v2(database_, kDeleteSql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare delete_character statement.");
    }

    bind_text(statement, 1, account_id);
    bind_text(statement, 2, character_name);
    const auto result = sqlite3_step(statement);
    const auto changes = sqlite3_changes(database_);
    finalize_statement(statement);
    if (result != SQLITE_DONE) {
      throw std::runtime_error("Failed to execute delete_character statement.");
    }
    deleted = changes > 0;
    // 如果实际影响了行则提交，否则回滚（角色可能已被并发删除）
    exec_or_throw(database_, deleted ? "COMMIT;" : "ROLLBACK;");
  } catch (...) {
    try {
      exec_or_throw(database_, "ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
  return deleted;
}

/**
 * @brief 保存角色数据（含乐观锁和墓碑检查）
 * @param character 要保存的角色记录
 * @return true 如果保存成功（受影响的记录数 > 0）
 * @throws std::runtime_error 如果 SQL 执行失败（事务自动回滚）
 * @details 保存流程：
 *          1. 确保账户存在（如果不存在则自动创建）
 *          2. 开启事务
 *          3. 检查墓碑表，如果角色的 save_version <= 墓碑版本则拒绝保存并回滚
 *          4. 使用 UPSERT 语义，通过 WHERE excluded.save_version >= characters.save_version
 *             条件实现乐观锁，防止旧版本覆盖新版本
 *          5. 检查 sqlite3_changes() 确定是否有行被实际更新
 *          6. 如果更新成功则提交事务，否则回滚
 */
bool Repository::save_character(const CharacterRecord& character) {
  ensure_account(database_, character.account_id);
  exec_or_throw(database_, "BEGIN IMMEDIATE;");
  try {
    // 检查墓碑版本：如果角色已被删除且版本号大于等于保存版本，则拒绝保存
    const auto tombstone_version =
        load_save_version(database_, character.account_id, character.character_name,
                          "character_save_tombstones");
    if (tombstone_version.has_value() && *tombstone_version >= character.save_version) {
      exec_or_throw(database_, "ROLLBACK;");
      return false;
    }

    // UPSERT：使用 excluded.save_version >= characters.save_version 作为乐观锁
    static constexpr const char* kSql =
        "INSERT INTO characters(account_id, character_name, map_id, x, y, dir, light, job, sex, hair,"
        " gold, feature, status, level, hp, mp, max_hp, max_mp, ac, mac, dc, mc, sc, exp, max_exp,"
        " weight, max_weight, wear_weight, max_wear_weight, hand_weight, max_hand_weight,"
        " equipped_blob, bag_blob, storage_blob, magic_blob, guild_name, guild_title, attack_mode,"
        " pk_point, death_time_ms, quest_blob, quest_open_blob, quest_unit_blob, script_param_blob,"
        " daily_quest, slave_blob, body_luck, birth_items_granted, save_version)"
        " VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18,"
        " ?19, ?20, ?21, ?22, ?23, ?24, ?25, ?26, ?27, ?28, ?29, ?30, ?31, ?32, ?33, ?34, ?35, ?36,"
        " ?37, ?38, ?39, ?40, ?41, ?42, ?43, ?44, ?45, ?46, ?47, ?48, ?49)"
        " ON CONFLICT(account_id, character_name) DO UPDATE SET map_id = excluded.map_id,"
        " x = excluded.x, y = excluded.y, dir = excluded.dir, light = excluded.light, job = excluded.job,"
        " sex = excluded.sex, hair = excluded.hair, gold = excluded.gold,"
        " feature = excluded.feature, status = excluded.status, level = excluded.level,"
        " hp = excluded.hp, mp = excluded.mp, max_hp = excluded.max_hp,"
        " max_mp = excluded.max_mp, ac = excluded.ac, mac = excluded.mac, dc = excluded.dc,"
        " mc = excluded.mc, sc = excluded.sc, exp = excluded.exp, max_exp = excluded.max_exp,"
        " weight = excluded.weight, max_weight = excluded.max_weight,"
        " wear_weight = excluded.wear_weight, max_wear_weight = excluded.max_wear_weight,"
        " hand_weight = excluded.hand_weight, max_hand_weight = excluded.max_hand_weight,"
        " equipped_blob = excluded.equipped_blob, bag_blob = excluded.bag_blob,"
        " storage_blob = excluded.storage_blob, magic_blob = excluded.magic_blob,"
        " guild_name = excluded.guild_name, guild_title = excluded.guild_title,"
        " attack_mode = excluded.attack_mode, pk_point = excluded.pk_point,"
        " death_time_ms = excluded.death_time_ms, quest_blob = excluded.quest_blob,"
        " quest_open_blob = excluded.quest_open_blob, quest_unit_blob = excluded.quest_unit_blob,"
        " script_param_blob = excluded.script_param_blob, daily_quest = excluded.daily_quest,"
        " slave_blob = excluded.slave_blob, body_luck = excluded.body_luck,"
        " birth_items_granted = excluded.birth_items_granted,"
        " save_version = excluded.save_version, updated_at = CURRENT_TIMESTAMP"
        " WHERE excluded.save_version >= characters.save_version;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
      throw std::runtime_error("Failed to prepare save_character statement.");
    }

    bind_character_fields(statement, character);

    if (sqlite3_step(statement) != SQLITE_DONE) {
      finalize_statement(statement);
      throw std::runtime_error("Failed to execute save_character statement.");
    }
    const auto saved = sqlite3_changes(database_) > 0;
    finalize_statement(statement);
    exec_or_throw(database_, "COMMIT;");
    return saved;
  } catch (...) {
    try {
      exec_or_throw(database_, "ROLLBACK;");
    } catch (...) {
    }
    throw;
  }
}

/**
 * @brief 记录遗留数据导入操作
 * @param record 导入记录
 * @throws std::runtime_error 如果 SQL 预处理或执行失败
 * @details 将遗留系统数据导入的每条记录写入 legacy_import_records 表，
 *          包含源文件信息、记录索引、账户/角色标识、处理状态、
 *          状态描述和原始二进制数据，用于审计和故障排查。
 */
void Repository::record_legacy_import(const LegacyImportRecord& record) {
  static constexpr const char* kSql =
      "INSERT INTO legacy_import_records(source_file, record_index, account_id, character_name,"
      " status, message, raw_record) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7);";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare record_legacy_import statement.");
  }

  bind_text(statement, 1, record.source_file);
  sqlite3_bind_int(statement, 2, record.record_index);
  bind_text(statement, 3, record.account_id);
  bind_text(statement, 4, record.character_name);
  bind_text(statement, 5, record.status);
  bind_text(statement, 6, record.message);
  sqlite3_bind_blob(statement, 7, record.raw_record.data(),
                    static_cast<int>(record.raw_record.size()), SQLITE_TRANSIENT);

  if (sqlite3_step(statement) != SQLITE_DONE) {
    finalize_statement(statement);
    throw std::runtime_error("Failed to execute record_legacy_import statement.");
  }
  finalize_statement(statement);
}

/**
 * @brief 统计遗留数据导入记录总数
 * @return 导入记录的数量（非负整数）
 * @throws std::runtime_error 如果 SQL 预处理失败
 */
std::size_t Repository::count_legacy_import_records() {
  static constexpr const char* kSql = "SELECT COUNT(*) FROM legacy_import_records;";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare count_legacy_import_records statement.");
  }

  std::size_t count = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    count = static_cast<std::size_t>(std::max(sqlite3_column_int64(statement, 0),
                                             static_cast<sqlite3_int64>(0)));
  }
  finalize_statement(statement);
  return count;
}

/**
 * @brief 记录审计事件
 * @param audit 审计事件记录（包含分类、消息、会话密钥）
 * @throws std::runtime_error 如果 SQL 预处理或执行失败
 * @details 将登录审计等安全事件写入 login_audit 表。
 *          自动使用 datetime('now') 生成创建时间戳。
 *          用于安全监控和问题排查。
 */
void Repository::record_audit(const AuditEvent& audit) {
  static constexpr const char* kSql =
      "INSERT INTO login_audit(category, message, session_key, created_at)"
      " VALUES(?1, ?2, ?3, datetime('now'));";

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database_, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    throw std::runtime_error("Failed to prepare record_audit statement.");
  }

  sqlite3_bind_text(statement, 1, audit.category.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, audit.message.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, audit.session_key.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(statement) != SQLITE_DONE) {
    sqlite3_finalize(statement);
    throw std::runtime_error("Failed to execute record_audit statement.");
  }
  sqlite3_finalize(statement);
}

}  // namespace mir2
