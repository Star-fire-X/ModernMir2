#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "legacy_bag_flow_target_smoke pending target mismatch: " << message << '\n';
  }
  return condition;
}

bool check_pending_fixtures(const std::filesystem::path& golden_root) {
  const auto sequences = read_text(golden_root / "bag_sequence_cases.json");
  bool ok = true;
  ok &= expect(sequences.find("eat_consumable_deletes_instance_then_eat_ok") != std::string::npos,
               "fixture records eat target sequence");
  ok &= expect(sequences.find("drop_item_creates_ground_before_bag_delete") != std::string::npos,
               "fixture records drop target sequence");
  ok &= expect(sequences.find("pickup_readds_ground_item_on_weight_failure") != std::string::npos,
               "fixture records pickup target sequence");
  ok &= expect(sequences.find("take_on_uses_resolved_tlist_bagindex") != std::string::npos,
               "fixture records take-on target sequence");
  ok &= expect(sequences.find("take_off_adds_bag_item_before_clearing_equipment") !=
                   std::string::npos,
               "fixture records take-off target sequence");
  return ok;
}

}  // namespace

int main() {
  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto golden_root = source_root / "tests" / "golden" / "bag_phase0";

  bool ok = check_pending_fixtures(golden_root);
  ok &= expect(false, "TList target order for A,B -> remove A -> add C is B,C");
  ok &= expect(false, "generic consumable target deletes the instance instead of dura--");
  ok &= expect(false, "drop target creates ground item before deleting bag item");
  ok &= expect(false, "pickup target restores ground item on add/weight failure");
  ok &= expect(false, "take-on/take-off target follows Delphi item/equipment/message order");
  return ok ? 0 : 1;
}
