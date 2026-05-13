#pragma once

#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "shared/legacy/map_document.hpp"

namespace mir2 {

constexpr std::int32_t kLegacyBagGold = 50000000;

enum class LegacyMapObjectShape : std::uint8_t {
  moving_object,
  item_object,
  gate_object,
  event_object
};

struct LegacyMovingObjectState {
  bool ghost{false};
  bool hold_place{true};
  bool death{false};
  bool hide_mode{false};
  bool supervisor_mode{false};
};

struct LegacyMapItemState {
  bool is_gold{false};
  std::int32_t gold_amount{0};
};

struct LegacyMapGateState {
  std::string target_map_id{};
  std::int32_t target_x{0};
  std::int32_t target_y{0};
  bool require_doors_open{true};
};

struct LegacyMapObject {
  LegacyMapObjectShape shape{LegacyMapObjectShape::moving_object};
  std::uint64_t object_id{0};
  std::uint64_t a_time_ms{0};
  bool blocks_walk{false};
  LegacyMovingObjectState moving{};
  LegacyMapItemState item{};
  LegacyMapGateState gate{};
};

struct LegacyMapAddResult {
  bool ok{false};
  bool merged{false};
  std::uint64_t object_id{0};
};

class LegacyMapEnvironment {
 public:
  struct Cell {
    std::vector<LegacyMapObject> obj_list{};
  };

  LegacyMapEnvironment() = default;
  LegacyMapEnvironment(std::int32_t width, std::int32_t height,
                       std::shared_ptr<const legacy::MapDocument> movement_map = {});

  void reset(std::int32_t width, std::int32_t height,
             std::shared_ptr<const legacy::MapDocument> movement_map = {});

  [[nodiscard]] bool in_bounds(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] bool static_can_move(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] bool static_can_fly(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] bool can_walk(std::int32_t x, std::int32_t y, bool allow_dup) const;
  [[nodiscard]] bool can_fly_line(std::int32_t from_x, std::int32_t from_y,
                                  std::int32_t to_x, std::int32_t to_y) const;
  [[nodiscard]] bool can_fire_fly_line(std::int32_t from_x, std::int32_t from_y,
                                       std::int32_t to_x, std::int32_t to_y) const;

  [[nodiscard]] bool add_moving_object(std::int32_t x, std::int32_t y, std::uint64_t object_id,
                                       std::uint64_t now_ms,
                                       LegacyMovingObjectState state = {});
  [[nodiscard]] int move_to_moving_object(std::int32_t old_x, std::int32_t old_y,
                                          std::uint64_t object_id, std::int32_t new_x,
                                          std::int32_t new_y, bool allow_dup,
                                          std::uint64_t now_ms,
                                          LegacyMovingObjectState state = {});
  [[nodiscard]] LegacyMapAddResult add_item_object(std::int32_t x, std::int32_t y,
                                                   std::uint64_t object_id,
                                                   LegacyMapItemState item,
                                                   std::uint64_t now_ms);
  [[nodiscard]] bool add_placeholder_object(std::int32_t x, std::int32_t y,
                                            LegacyMapObjectShape shape,
                                            std::uint64_t object_id,
                                            std::uint64_t now_ms,
                                            bool blocks_walk = false);
  [[nodiscard]] bool add_gate_object(std::int32_t x, std::int32_t y,
                                     std::uint64_t object_id,
                                     LegacyMapGateState gate,
                                     std::uint64_t now_ms);
  [[nodiscard]] int delete_from_map(std::int32_t x, std::int32_t y,
                                    LegacyMapObjectShape shape, std::uint64_t object_id);
  bool verify_map_time(std::int32_t x, std::int32_t y, std::uint64_t object_id,
                       std::uint64_t now_ms);
  [[nodiscard]] const LegacyMapObject* gate_at(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] bool around_door_opened(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] std::vector<std::pair<std::int32_t, std::int32_t>> open_doors_around(
      std::int32_t x, std::int32_t y, std::uint64_t now_ms);
  [[nodiscard]] std::vector<std::pair<std::int32_t, std::int32_t>> close_expired_doors(
      std::uint64_t now_ms, std::uint64_t ttl_ms);
  [[nodiscard]] std::size_t door_core_count() const { return door_cores_.size(); }
  [[nodiscard]] bool door_is_open(std::int32_t x, std::int32_t y) const;

  [[nodiscard]] std::optional<std::uint64_t> first_item_object_id(std::int32_t x,
                                                                  std::int32_t y) const;
  [[nodiscard]] std::optional<std::size_t> item_object_count(std::int32_t x,
                                                             std::int32_t y) const;
  [[nodiscard]] std::vector<std::uint64_t> item_object_ids_in_order() const;
  [[nodiscard]] const Cell* cell(std::int32_t x, std::int32_t y) const;

 private:
  using CellKey = std::pair<std::int32_t, std::int32_t>;

  struct DoorCore {
    std::int32_t number{0};
    bool open{false};
    bool locked{false};
    std::int32_t lock_key{0};
    std::uint64_t open_time_ms{0};
    std::vector<CellKey> tiles{};
  };

  struct DoorTile {
    std::int32_t number{0};
    std::size_t core_index{0};
  };

  [[nodiscard]] Cell* mutable_cell(std::int32_t x, std::int32_t y);
  void erase_cell_if_empty(CellKey key);
  void load_doors_from_map();
  [[nodiscard]] DoorCore* door_core_at(std::int32_t x, std::int32_t y);
  [[nodiscard]] const DoorCore* door_core_at(std::int32_t x, std::int32_t y) const;

  std::int32_t width_{0};
  std::int32_t height_{0};
  std::shared_ptr<const legacy::MapDocument> movement_map_{};
  std::map<CellKey, Cell> cells_{};
  std::vector<DoorCore> door_cores_{};
  std::map<CellKey, DoorTile> door_tiles_{};
};

}  // namespace mir2
