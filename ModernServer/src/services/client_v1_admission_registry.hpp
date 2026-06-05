/**
 * @file client_v1_admission_registry.hpp
 * @brief Client v1 准入注册表头文件
 *
 * @details 定义 ClientV1Admission 结构体和 ClientV1AdmissionRegistry 类。
 *          用于管理新协议客户端进入游戏世界的准入令牌，支持令牌的生成、
 *          消费和自动过期回收。令牌使用 TTL(生存时间)机制确保安全性。
 *
 * @note 该注册表是线程安全的，使用互斥锁保护内部数据结构。
 *       过期的令牌会在每次操作前自动清理。
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>

namespace mir2 {

/**
 * @struct ClientV1Admission
 * @brief Client v1 准入记录
 *
 * @details 保存新协议客户端的准入凭证信息，包括令牌、账号ID、角色名和过期时间。
 *          玩家从登录网关认证后获得此令牌，用于进入游戏网关。
 */
struct ClientV1Admission {
  std::string token{};           ///< 准入令牌字符串
  std::string account_id{};      ///< 账号ID
  std::string character_name{};  ///< 选择的角色名
  std::chrono::steady_clock::time_point expires_at{}; ///< 令牌过期时间点
};

/**
 * @class ClientV1AdmissionRegistry
 * @brief Client v1 准入注册表
 *
 * @details 线程安全的准入令牌管理器，提供以下功能：
 *          - issue(): 生成新的准入令牌，自动设置 TTL
 *          - consume(): 消费(验证并移除)一个准入令牌
 *          - 内部定期清理过期令牌
 *
 *          令牌由随机数和计数器组合生成，保证唯一性。
 */
class ClientV1AdmissionRegistry {
 public:
  /**
   * @brief 签发一个新的准入令牌
   * @param account_id 账号ID
   * @param character_name 角色名
   * @param ttl 令牌生存时间，默认30秒
   * @return 生成的令牌字符串
   */
  [[nodiscard]] std::string issue(const std::string& account_id, const std::string& character_name,
                                  std::chrono::seconds ttl = std::chrono::seconds(30)) {
    std::scoped_lock lock(mutex_);
    reap_expired_locked();

    std::ostringstream token;
    token << std::hex << random_() << random_() << counter_++;
    ClientV1Admission admission;
    admission.token = token.str();
    admission.account_id = account_id;
    admission.character_name = character_name;
    admission.expires_at = std::chrono::steady_clock::now() + ttl;
    admissions_[admission.token] = admission;
    return admission.token;
  }

  /**
   * @brief 消费(验证并移除)一个准入令牌
   * @param token 令牌字符串
   * @return 如果令牌有效则返回对应的准入记录，否则返回 std::nullopt
   * @note 消费操作是幂等的，令牌被消费后无法再次使用
   */
  [[nodiscard]] std::optional<ClientV1Admission> consume(const std::string& token) {
    std::scoped_lock lock(mutex_);
    reap_expired_locked();
    const auto it = admissions_.find(token);
    if (it == admissions_.end()) {
      return std::nullopt;
    }
    auto admission = it->second;
    admissions_.erase(it);
    return admission;
  }

 private:
  /**
   * @brief 清理所有过期的令牌(必须在持有锁的情况下调用)
   */
  void reap_expired_locked() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = admissions_.begin(); it != admissions_.end();) {
      if (it->second.expires_at <= now) {
        it = admissions_.erase(it);
      } else {
        ++it;
      }
    }
  }

  std::mutex mutex_{};
  std::unordered_map<std::string, ClientV1Admission> admissions_{};
  std::mt19937_64 random_{std::random_device{}()};
  std::uint64_t counter_{1};
};

}  // namespace mir2
