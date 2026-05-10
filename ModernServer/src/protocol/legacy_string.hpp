#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mir2 {

class LegacyStringView {
 public:
  constexpr LegacyStringView() = default;
  constexpr explicit LegacyStringView(std::string_view bytes) : bytes_(bytes) {}

  [[nodiscard]] constexpr std::string_view bytes() const noexcept { return bytes_; }
  [[nodiscard]] constexpr std::size_t byte_size() const noexcept { return bytes_.size(); }
  [[nodiscard]] constexpr bool empty() const noexcept { return bytes_.empty(); }
  [[nodiscard]] constexpr int compare(LegacyStringView other) const noexcept {
    return bytes_.compare(other.bytes_);
  }

 private:
  std::string_view bytes_{};
};

class LegacyString {
 public:
  LegacyString() = default;
  explicit LegacyString(std::string bytes);
  explicit LegacyString(std::string_view bytes);

  [[nodiscard]] LegacyStringView view() const noexcept;
  [[nodiscard]] std::string_view bytes() const noexcept;
  [[nodiscard]] std::size_t byte_size() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] const std::string& str() const noexcept;
  [[nodiscard]] std::string take_bytes() && noexcept;

 private:
  std::string bytes_{};
};

[[nodiscard]] bool operator==(LegacyStringView lhs, LegacyStringView rhs) noexcept;
[[nodiscard]] bool operator!=(LegacyStringView lhs, LegacyStringView rhs) noexcept;

[[nodiscard]] std::string copy_legacy_bytes(std::string_view bytes);
[[nodiscard]] std::string legacy_debug_hex(LegacyStringView value);
[[nodiscard]] std::vector<LegacyString> split_legacy_fields(LegacyStringView value,
                                                            char delimiter);
[[nodiscard]] bool is_valid_legacy_account_id(LegacyStringView account_id);
[[nodiscard]] bool is_valid_legacy_character_name(LegacyStringView name);

[[nodiscard]] inline bool is_valid_legacy_account_id(std::string_view account_id) {
  return is_valid_legacy_account_id(LegacyStringView{account_id});
}

[[nodiscard]] inline bool is_valid_legacy_character_name(std::string_view name) {
  return is_valid_legacy_character_name(LegacyStringView{name});
}

}  // namespace mir2
