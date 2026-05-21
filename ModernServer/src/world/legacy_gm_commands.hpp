#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mir2 {

enum class LegacyUserDegree : std::uint8_t {
  normal,
  observer,
  sysop,
  admin,
  superadmin
};

enum class LegacyGmCommandImplementation : std::uint8_t {
  pending,
  implemented
};

struct LegacyGmCommandDefinition {
  std::string canonical_name{};
  std::vector<std::string> aliases{};
  LegacyUserDegree minimum_degree{LegacyUserDegree::normal};
  LegacyGmCommandImplementation implementation{LegacyGmCommandImplementation::pending};
  std::string dependency{};
};

[[nodiscard]] const std::vector<LegacyGmCommandDefinition>&
legacy_gm_command_definitions();
[[nodiscard]] const LegacyGmCommandDefinition* find_legacy_gm_command(
    std::string_view command_name);
[[nodiscard]] std::unordered_map<std::string, LegacyUserDegree> load_legacy_admin_list(
    const std::filesystem::path& path);
[[nodiscard]] LegacyUserDegree legacy_heuristic_user_degree(std::string_view account_id);
[[nodiscard]] bool legacy_user_degree_at_least(LegacyUserDegree actual,
                                               LegacyUserDegree required);
[[nodiscard]] std::string_view legacy_user_degree_name(LegacyUserDegree degree);

}  // namespace mir2
