#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/messages.hpp"

struct sqlite3;

namespace mir2 {

struct AccountOperationResult {
  std::int32_t status_code{0};
  std::optional<AccountRecord> account{};
};

struct LegacyImportRecord {
  std::string source_file{};
  std::int32_t record_index{0};
  std::string account_id{};
  std::string character_name{};
  std::string status{};
  std::string message{};
  std::vector<std::uint8_t> raw_record{};
};

class Repository {
 public:
  explicit Repository(const std::filesystem::path& database_path);
  ~Repository();

  Repository(const Repository&) = delete;
  Repository& operator=(const Repository&) = delete;

  void ensure_schema(const std::filesystem::path& schema_path);
  void seed_runtime();
  [[nodiscard]] std::optional<AccountRecord> load_account(const std::string& account_id);
  [[nodiscard]] AccountOperationResult authenticate_account(const std::string& account_id,
                                                            const std::string& password,
                                                            std::int64_t now_ms);
  [[nodiscard]] CastleDialogContext load_castle_dialog_context();
  [[nodiscard]] GuildCastleSnapshot load_guild_castle_snapshot();
  void save_guild_payload(const std::string& guild_name, const std::string& payload_json);
  void save_guild_state(const GuildState& guild_state);
  void delete_guild(const std::string& guild_name);
  void save_castle_state(const std::string& castle_name, const std::string& payload_json);
  [[nodiscard]] std::vector<MerchantStateRecord> load_merchant_states();
  void save_merchant_state(const MerchantStateRecord& state);
  [[nodiscard]] bool create_account(const AccountRecord& account);
  [[nodiscard]] bool update_account(const AccountRecord& account);
  [[nodiscard]] std::int32_t change_password(const std::string& account_id,
                                             const std::string& password,
                                             const std::string& new_password,
                                             std::int64_t now_ms);
  [[nodiscard]] std::optional<CharacterRecord> load_character(const std::string& account_id,
                                                              const std::string& character_name);
  [[nodiscard]] std::optional<CharacterRecord> load_character_by_name(
      const std::string& character_name);
  [[nodiscard]] std::vector<CharacterRecord> list_characters(const std::string& account_id);
  [[nodiscard]] bool create_character(const CharacterRecord& character);
  [[nodiscard]] bool delete_character(const std::string& account_id,
                                      const std::string& character_name);
  bool save_character(const CharacterRecord& character);
  void record_legacy_import(const LegacyImportRecord& record);
  [[nodiscard]] std::size_t count_legacy_import_records();
  void record_audit(const AuditEvent& audit);

 private:
  sqlite3* database_{nullptr};
};

}  // namespace mir2
