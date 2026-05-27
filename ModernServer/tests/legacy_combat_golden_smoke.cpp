#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int fail(std::string_view stage) {
  std::cerr << "legacy_combat_golden_smoke failed at " << stage << '\n';
  return 1;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

bool contains_all(const std::string& text, const std::vector<std::string_view>& tokens) {
  for (const auto token : tokens) {
    if (text.find(token) == std::string::npos) {
      std::cerr << "missing token: " << token << '\n';
      return false;
    }
  }
  return true;
}

bool contains_none(const std::string& text, const std::vector<std::string_view>& tokens) {
  for (const auto token : tokens) {
    if (text.find(token) != std::string::npos) {
      std::cerr << "unexpected token: " << token << '\n';
      return false;
    }
  }
  return true;
}

bool check_sequence_fixture(const std::string& sequences) {
  return contains_all(sequences,
                      {
                          "\"schema_version\": 1",
                          "\"baseline_commit\": \"d7dcc0d7af37a91b2f1abaa9e1f741abc28d6f61\"",
                          "\"source_trace_root\": \"docs/pr1_delphi_audit/golden_traces\"",
                          "\"name\": \"hitxy_cadence_window\"",
                          "\"name\": \"spellxy_cooldown\"",
                          "\"name\": \"attack_longhit_dual_target_chain\"",
                          "\"name\": \"attack_widehit_order\"",
                          "\"name\": \"attack_crosshit_order\"",
                          "\"name\": \"pk_target_rules\"",
                          "\"name\": \"struck_delay_200_500\"",
                          "\"name\": \"death_revival_packet_order\"",
                          "\"name\": \"hit_rng_sword4_heavy_big\"",
                          "\"owner_pr\": \"PR-2\"",
                          "\"owner_pr\": \"PR-3\"",
                          "\"owner_pr\": \"PR-4\"",
                          "\"owner_pr\": \"PR-5\"",
                          "\"owner_pr\": \"PR-6\"",
                          "HitSpeed 16",
                          "SpeedHackTimerOverCount > 8",
                          "main target delay 200ms",
                          "secondary DirectAttack delay 500ms",
                          "SM_NOWDEATH",
                      });
}

bool check_smoke_classification(const std::string& classification) {
  return contains_all(classification,
                      {
                          "\"schema_version\": 1",
                          "\"role\": \"current_stability_smoke\"",
                          "\"role\": \"delphi_parity_smoke\"",
                          "\"test\": \"safe_zone_legacy_smoke.cpp\"",
                          "\"test\": \"monster_home_leash_smoke.cpp\"",
                          "\"test\": \"monster_special_race_smoke.cpp\"",
                          "\"test\": \"monster_attack_legacy_smoke.cpp\"",
                          "\"test\": \"legacy_action_cadence_smoke.cpp\"",
                          "\"contains_known_parity_gap\": false",
                          "Player HitXY cadence now uses Delphi window semantics",
                          "monster ATTACK_SPD >= 200 is not a combat bug",
                          "monster active search using imported search_rate_ms",
                      });
}

bool check_canonical_snapshots(const std::string& snapshots) {
  return contains_all(snapshots,
                      {
                          "\"artifact\": \"legacy_combat.canonical_combat_snapshots\"",
                          "\"status\": \"pr6_canonicalized\"",
                          "\"name\": \"basic_hit\"",
                          "\"name\": \"basic_miss\"",
                          "\"name\": \"basic_death\"",
                          "\"name\": \"longhit_second_tile\"",
                          "\"name\": \"widehit_multi_target\"",
                          "\"name\": \"firehit\"",
                          "\"name\": \"magic_hit\"",
                          "\"name\": \"poison\"",
                          "\"name\": \"firewall_tick\"",
                          "\"name\": \"monster_attack_player\"",
                          "\"name\": \"player_death_revival_ring\"",
                          "\"name\": \"monster_death_drop\"",
                          "skill_status_poison_buff_hide_shield_smoke.cpp",
                          "monster_legacy_tick_ai_smoke.cpp",
                      });
}

bool check_audit_doc(const std::string& audit) {
  return contains_all(audit,
                      {
                          "Baseline: `origin/main@d7dcc0d7af37a91b2f1abaa9e1f741abc28d6f61`",
                          "`safe_zone` combat blocking",
                          "`area_state` sync",
                          "monster home leash",
                          "visibility order",
                          "monster `ATTACK_SPD >= 200` import clamp",
                          "not a combat compatibility bug",
                          "ModernServer/tests/golden/legacy_combat/combat_sequence_cases.json",
                          "ModernServer/tests/golden/legacy_combat/canonical_combat_snapshots.json",
                          "PR-2",
                          "PR-3",
                          "PR-4",
                          "PR-5",
                          "PR-6",
                      }) &&
         contains_none(audit,
                       {
                           "monster `ATTACK_SPD >= 200` bug",
                           "ATTACK_SPD >= 200 compatibility bug",
                       });
}

bool check_source_evidence(const std::filesystem::path& trace_root) {
  const auto attack_basic = read_text(trace_root / "attack_basic.json");
  const auto spell_failure = read_text(trace_root / "spell_failure.json");
  const auto longhit = read_text(trace_root / "attack_longhit.json");
  const auto struck_delay = read_text(trace_root / "struck_delay_player_vs_monster.json");
  const auto death = read_text(trace_root / "death_player.json");

  return contains_all(attack_basic, {"\"flow\": \"attack_basic\"", "HitTimeOverCount"}) &&
         contains_all(spell_failure, {"\"flow\": \"spell_failure\"", "SM_SPELL"}) &&
         contains_all(longhit, {"\"flow\": \"attack_longhit\"", "DirectAttack"}) &&
         contains_all(struck_delay, {"\"flow\": \"struck_delay_player_vs_monster\"",
                                     "\"delay_ms\": 200", "\"delay_ms\": 500"}) &&
         contains_all(death, {"\"flow\": \"death_player\"", "SM_DEATH"});
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto repo_root = source_root.parent_path();
  const auto fixture_root = source_root / "tests" / "golden" / "legacy_combat";
  const auto trace_root = repo_root / "docs" / "pr1_delphi_audit" / "golden_traces";

  if (!check_sequence_fixture(read_text(fixture_root / "combat_sequence_cases.json"))) {
    return fail("combat sequence fixture");
  }
  if (!check_smoke_classification(read_text(fixture_root / "combat_smoke_classification.json"))) {
    return fail("smoke classification");
  }
  if (!check_canonical_snapshots(read_text(fixture_root /
                                           "canonical_combat_snapshots.json"))) {
    return fail("canonical snapshots");
  }
  if (!check_audit_doc(read_text(repo_root / "docs" /
                                 "delphi_cpp_combat_compatibility_audit.md"))) {
    return fail("audit doc");
  }
  if (!check_source_evidence(trace_root)) {
    return fail("source evidence");
  }
  return 0;
}
