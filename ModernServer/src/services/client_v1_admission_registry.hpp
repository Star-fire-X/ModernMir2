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

struct ClientV1Admission {
  std::string token{};
  std::string account_id{};
  std::string character_name{};
  std::chrono::steady_clock::time_point expires_at{};
};

class ClientV1AdmissionRegistry {
 public:
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
