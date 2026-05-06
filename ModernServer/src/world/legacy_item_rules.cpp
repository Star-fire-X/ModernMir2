#include "world/legacy_item_rules.hpp"

#include <algorithm>

#include "util/string_utils.hpp"

namespace mir2 {
namespace {

std::int32_t packed_low(std::uint16_t value) {
  return static_cast<std::int32_t>(value & 0xffu);
}

std::int32_t packed_high(std::uint16_t value) {
  return static_cast<std::int32_t>((value >> 8) & 0xffu);
}

std::uint16_t pack_range(std::int32_t low, std::int32_t high) {
  return static_cast<std::uint16_t>((std::clamp(high, 0, 255) << 8) |
                                    std::clamp(low, 0, 255));
}

std::uint16_t add_high(std::uint16_t packed, std::int32_t delta) {
  return pack_range(packed_low(packed), packed_high(packed) + delta);
}

std::uint16_t add_low(std::uint16_t packed, std::int32_t delta) {
  return pack_range(packed_low(packed) + delta, packed_high(packed));
}

bool matches_legacy_job(std::int32_t required_job, std::uint8_t character_job) {
  return required_job < 0 || required_job == static_cast<std::int32_t>(character_job);
}

bool matches_legacy_sex(std::int32_t required_sex, std::uint8_t character_sex) {
  return required_sex < 0 || required_sex == static_cast<std::int32_t>(character_sex);
}

std::uint8_t clamp_legacy_desc(std::int32_t value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::uint16_t clamp_legacy_dura(std::int32_t value) {
  return static_cast<std::uint16_t>(std::clamp(value, 0, 65000));
}

std::int32_t legacy_get_upgrade(std::int32_t count, std::int32_t range,
                                LegacyRandom& random) {
  std::int32_t result = 0;
  for (std::int32_t index = 0; index < count; ++index) {
    if (random.random(range) == 0) {
      ++result;
    } else {
      break;
    }
  }
  return result;
}

void add_legacy_dura(LegacyUserItem& item, std::int32_t amount) {
  item.dura_max = clamp_legacy_dura(static_cast<std::int32_t>(item.dura_max) + amount);
  item.dura = clamp_legacy_dura(static_cast<std::int32_t>(item.dura) + amount);
}

}  // namespace

std::int32_t legacy_resolve_slot_from_std_mode(std::int32_t std_mode) {
  switch (std_mode) {
    case 5:
    case 6:
      return static_cast<std::int32_t>(kEquipWeapon);
    case 10:
    case 11:
      return static_cast<std::int32_t>(kEquipDress);
    case 15:
    case 16:
      return static_cast<std::int32_t>(kEquipHelmet);
    case 19:
    case 20:
    case 21:
      return static_cast<std::int32_t>(kEquipNecklace);
    case 22:
    case 23:
      return static_cast<std::int32_t>(kEquipRingLeft);
    case 24:
    case 26:
      return static_cast<std::int32_t>(kEquipArmRingRight);
    case 25:
      return static_cast<std::int32_t>(kEquipBujuk);
    case 30:
      return static_cast<std::int32_t>(kEquipRightHand);
    case 52:
      return static_cast<std::int32_t>(kEquipBoots);
    case 53:
      return static_cast<std::int32_t>(kEquipCharm);
    case 54:
      return static_cast<std::int32_t>(kEquipBelt);
    default:
      return -1;
  }
}

bool legacy_item_fits_slot(const ItemConfig& item_config, std::int32_t slot) {
  if (slot < 0 || slot >= static_cast<std::int32_t>(kMaxEquipSlots)) {
    return false;
  }
  if (item_config.equip_slot >= 0) {
    return item_config.equip_slot == slot;
  }

  switch (slot) {
    case static_cast<std::int32_t>(kEquipDress):
      return item_config.std_mode == 10 || item_config.std_mode == 11;
    case static_cast<std::int32_t>(kEquipWeapon):
      return item_config.std_mode == 5 || item_config.std_mode == 6;
    case static_cast<std::int32_t>(kEquipRightHand):
      return item_config.std_mode == 30;
    case static_cast<std::int32_t>(kEquipNecklace):
      return item_config.std_mode == 19 || item_config.std_mode == 20 ||
             item_config.std_mode == 21;
    case static_cast<std::int32_t>(kEquipHelmet):
      return item_config.std_mode == 15 || item_config.std_mode == 16;
    case static_cast<std::int32_t>(kEquipRingLeft):
    case static_cast<std::int32_t>(kEquipRingRight):
      return item_config.std_mode == 22 || item_config.std_mode == 23;
    case static_cast<std::int32_t>(kEquipArmRingRight):
      return item_config.std_mode == 24 || item_config.std_mode == 26;
    case static_cast<std::int32_t>(kEquipArmRingLeft):
      return item_config.std_mode == 24 || item_config.std_mode == 25 ||
             item_config.std_mode == 26;
    case static_cast<std::int32_t>(kEquipBujuk):
      return item_config.std_mode == 25;
    case static_cast<std::int32_t>(kEquipBelt):
      return item_config.std_mode == 54;
    case static_cast<std::int32_t>(kEquipBoots):
      return item_config.std_mode == 52;
    case static_cast<std::int32_t>(kEquipCharm):
      return item_config.std_mode == 53;
    default:
      return false;
  }
}

bool legacy_slot_uses_hand_weight(std::size_t slot) {
  return slot == kEquipWeapon || slot == kEquipRightHand;
}

bool legacy_item_can_take_off(const ItemConfig* item_config, const LegacyUserItem& item) {
  if (is_empty(item)) {
    return true;
  }
  if (item.desc[7] != 0) {
    return false;
  }
  if (item_config == nullptr) {
    return true;
  }
  return (item_config->item_desc & (kLegacyItemUnableTakeOff | kLegacyItemNeverTakeOff)) == 0;
}

bool legacy_item_is_consumable(const ItemConfig& item_config) {
  return item_config.hp_add > 0 || item_config.mp_add > 0;
}

bool legacy_item_is_magic_book(const ItemConfig& item_config) {
  return item_config.std_mode == 4;
}

bool legacy_item_is_scroll(const ItemConfig& item_config) {
  return item_config.std_mode == 31;
}

bool legacy_item_is_unbind_bundle(const ItemConfig& item_config) {
  return !item_config.unbind_item.empty() && item_config.unbind_count > 0;
}

std::string legacy_scroll_kind(const ItemConfig& item_config) {
  if (!item_config.scroll_kind.empty()) {
    return util::lower_copy(item_config.scroll_kind);
  }
  const auto name = util::lower_copy(item_config.name);
  if (name.find("random") != std::string::npos) {
    return "random";
  }
  if (name.find("escape") != std::string::npos) {
    return "escape";
  }
  if (name.find("town") != std::string::npos || name.find("return") != std::string::npos ||
      name.find("recall") != std::string::npos) {
    return "town";
  }
  if (item_config.shape == 1) {
    return "random";
  }
  if (item_config.shape == 2) {
    return "escape";
  }
  return "town";
}

ItemConfig legacy_upgraded_item_config(const ItemConfig& item_config,
                                        const LegacyUserItem& user_item) {
  auto upgraded = item_config;
  switch (item_config.std_mode) {
    case 5:
    case 6:
      upgraded.dc = add_high(upgraded.dc, user_item.desc[0]);
      upgraded.mc = add_high(upgraded.mc, user_item.desc[1]);
      upgraded.sc = add_high(upgraded.sc, user_item.desc[2]);
      upgraded.ac = add_low(upgraded.ac, user_item.desc[3]);
      upgraded.accurate += user_item.desc[5];
      if (user_item.desc[6] > 10) {
        upgraded.atk_spd += user_item.desc[6] - 10;
      } else {
        upgraded.atk_spd -= user_item.desc[6];
      }
      if (user_item.desc[7] >= 1 && user_item.desc[7] <= 10) {
        upgraded.special_pwr = user_item.desc[7];
      }
      if (user_item.desc[10] != 0) {
        upgraded.item_desc |= 0x01;
      }
      break;
    case 10:
    case 11:
      upgraded.ac = add_high(upgraded.ac, user_item.desc[0]);
      upgraded.mac = add_high(upgraded.mac, user_item.desc[1]);
      upgraded.dc = add_high(upgraded.dc, user_item.desc[2]);
      upgraded.mc = add_high(upgraded.mc, user_item.desc[3]);
      upgraded.sc = add_high(upgraded.sc, user_item.desc[4]);
      break;
    case 15:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 26:
      upgraded.ac = add_high(upgraded.ac, user_item.desc[0]);
      upgraded.mac = add_high(upgraded.mac, user_item.desc[1]);
      upgraded.dc = add_high(upgraded.dc, user_item.desc[2]);
      upgraded.mc = add_high(upgraded.mc, user_item.desc[3]);
      upgraded.sc = add_high(upgraded.sc, user_item.desc[4]);
      if (user_item.desc[5] > 0) {
        upgraded.need = user_item.desc[5];
      }
      if (user_item.desc[6] > 0) {
        upgraded.need_level = user_item.desc[6];
      }
      break;
    default:
      break;
  }
  return upgraded;
}

void legacy_random_upgrade_monster_drop_item(const ItemConfig& item_config,
                                             LegacyUserItem& user_item,
                                             LegacyRandom& random) {
  std::int32_t up = 0;
  switch (item_config.std_mode) {
    case 5:
    case 6:
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(15) == 0) {
        user_item.desc[0] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(20) == 0) {
        const auto incp = (1 + up) / 3;
        if (incp > 0) {
          user_item.desc[6] = clamp_legacy_desc(random.random(3) != 0 ? incp : 10 + incp);
        }
      }
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(15) == 0) {
        user_item.desc[1] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(15) == 0) {
        user_item.desc[2] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(24) == 0) {
        user_item.desc[5] = clamp_legacy_desc(1 + (up / 2));
      }
      up = legacy_get_upgrade(12, 12, random);
      if (random.random(3) < 2) {
        add_legacy_dura(user_item, (1 + up) * 2000);
      }
      up = legacy_get_upgrade(12, 15, random);
      if (random.random(10) == 0) {
        user_item.desc[7] = clamp_legacy_desc(1 + (up / 2));
      }
      break;
    case 10:
    case 11:
      up = legacy_get_upgrade(6, 15, random);
      if (random.random(30) == 0) {
        user_item.desc[0] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 15, random);
      if (random.random(30) == 0) {
        user_item.desc[1] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) {
        user_item.desc[2] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) {
        user_item.desc[3] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) {
        user_item.desc[4] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 10, random);
      if (random.random(8) < 6) {
        add_legacy_dura(user_item, (1 + up) * 2000);
      }
      break;
    case 19:
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) {
        user_item.desc[0] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) {
        user_item.desc[1] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[2] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[3] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[4] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 10, random);
      if (random.random(4) < 3) {
        add_legacy_dura(user_item, (1 + up) * 1000);
      }
      break;
    case 20:
    case 21:
    case 24:
      up = legacy_get_upgrade(6, 30, random);
      if (random.random(60) == 0) {
        user_item.desc[0] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 30, random);
      if (random.random(60) == 0) {
        user_item.desc[1] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[2] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[3] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[4] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(20) < 15) {
        add_legacy_dura(user_item, (1 + up) * 1000);
      }
      break;
    case 26:
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(20) == 0) {
        user_item.desc[0] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(20) == 0) {
        user_item.desc[1] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[2] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[3] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[4] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(20) < 15) {
        add_legacy_dura(user_item, (1 + up) * 1000);
      }
      break;
    case 22:
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[2] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[3] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[4] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(4) < 3) {
        add_legacy_dura(user_item, (1 + up) * 1000);
      }
      break;
    case 23:
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) {
        user_item.desc[0] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) {
        user_item.desc[1] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[2] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[3] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[4] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(4) < 3) {
        add_legacy_dura(user_item, (1 + up) * 1000);
      }
      break;
    case 15:
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(40) == 0) {
        user_item.desc[0] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[1] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[2] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[3] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 20, random);
      if (random.random(30) == 0) {
        user_item.desc[4] = clamp_legacy_desc(1 + up);
      }
      up = legacy_get_upgrade(6, 12, random);
      if (random.random(4) < 3) {
        add_legacy_dura(user_item, (1 + up) * 1000);
      }
      break;
    default:
      break;
  }
}

void legacy_random_set_unknown_monster_drop_item(const ItemConfig& item_config,
                                                 LegacyUserItem& user_item,
                                                 LegacyRandom& random) {
  constexpr std::int32_t kRingOfUnknown = 130;
  constexpr std::int32_t kBraceletOfUnknown = 131;
  constexpr std::int32_t kHelmetOfUnknown = 132;
  if (item_config.shape != kRingOfUnknown && item_config.shape != kBraceletOfUnknown &&
      item_config.shape != kHelmetOfUnknown) {
    return;
  }

  std::int32_t up = 0;
  std::int32_t sum = 0;
  if (item_config.std_mode == 15) {
    up = legacy_get_upgrade(4, 3, random) + legacy_get_upgrade(4, 8, random) +
         legacy_get_upgrade(4, 20, random);
    if (up > 0) {
      user_item.desc[0] = clamp_legacy_desc(up);
    }
    sum += up;
    up = legacy_get_upgrade(4, 3, random) + legacy_get_upgrade(4, 8, random) +
         legacy_get_upgrade(4, 20, random);
    if (up > 0) {
      user_item.desc[1] = clamp_legacy_desc(up);
    }
    sum += up;
    for (std::size_t index = 2; index <= 4; ++index) {
      up = legacy_get_upgrade(3, 15, random) + legacy_get_upgrade(3, 30, random);
      if (up > 0) {
        user_item.desc[index] = clamp_legacy_desc(up);
      }
      sum += up;
    }
    up = legacy_get_upgrade(6, 30, random);
    if (up > 0) {
      add_legacy_dura(user_item, (1 + up) * 1000);
    }
    if (random.random(30) == 0) {
      user_item.desc[7] = 1;
    }
    user_item.desc[8] = 1;
    if (sum >= 3) {
      if (user_item.desc[0] >= 5) {
        user_item.desc[5] = 1;
        user_item.desc[6] = clamp_legacy_desc(25 + user_item.desc[0] * 3);
      } else if (user_item.desc[2] >= 2) {
        user_item.desc[5] = 1;
        user_item.desc[6] = clamp_legacy_desc(35 + user_item.desc[2] * 4);
      } else if (user_item.desc[3] >= 2) {
        user_item.desc[5] = 2;
        user_item.desc[6] = clamp_legacy_desc(18 + user_item.desc[3] * 2);
      } else if (user_item.desc[4] >= 2) {
        user_item.desc[5] = 3;
        user_item.desc[6] = clamp_legacy_desc(18 + user_item.desc[4] * 2);
      } else {
        user_item.desc[6] = clamp_legacy_desc(18 + sum * 2);
      }
    }
    return;
  }

  if (item_config.std_mode == 22 || item_config.std_mode == 23) {
    for (std::size_t index = 2; index <= 4; ++index) {
      up = legacy_get_upgrade(3, 4, random) + legacy_get_upgrade(3, 8, random) +
           legacy_get_upgrade(6, 20, random);
      if (up > 0) {
        user_item.desc[index] = clamp_legacy_desc(up);
      }
      sum += up;
    }
    up = legacy_get_upgrade(6, 30, random);
    if (up > 0) {
      add_legacy_dura(user_item, (1 + up) * 1000);
    }
    if (random.random(30) == 0) {
      user_item.desc[7] = 1;
    }
    user_item.desc[8] = 1;
    if (sum >= 3) {
      if (user_item.desc[2] >= 3) {
        user_item.desc[5] = 1;
        user_item.desc[6] = clamp_legacy_desc(25 + user_item.desc[2] * 3);
      } else if (user_item.desc[3] >= 3) {
        user_item.desc[5] = 2;
        user_item.desc[6] = clamp_legacy_desc(18 + user_item.desc[3] * 2);
      } else if (user_item.desc[4] >= 3) {
        user_item.desc[5] = 3;
        user_item.desc[6] = clamp_legacy_desc(18 + user_item.desc[4] * 2);
      } else {
        user_item.desc[6] = clamp_legacy_desc(18 + sum * 2);
      }
    }
    return;
  }

  if (item_config.std_mode == 24 || item_config.std_mode == 26) {
    up = legacy_get_upgrade(3, 5, random) + legacy_get_upgrade(5, 20, random);
    if (up > 0) {
      user_item.desc[0] = clamp_legacy_desc(up);
    }
    sum += up;
    up = legacy_get_upgrade(3, 5, random) + legacy_get_upgrade(5, 20, random);
    if (up > 0) {
      user_item.desc[1] = clamp_legacy_desc(up);
    }
    sum += up;
    for (std::size_t index = 2; index <= 4; ++index) {
      up = legacy_get_upgrade(3, 15, random) + legacy_get_upgrade(5, 30, random);
      if (up > 0) {
        user_item.desc[index] = clamp_legacy_desc(up);
      }
      sum += up;
    }
    up = legacy_get_upgrade(6, 30, random);
    if (up > 0) {
      add_legacy_dura(user_item, (1 + up) * 1000);
    }
    if (random.random(30) == 0) {
      user_item.desc[7] = 1;
    }
    user_item.desc[8] = 1;
    if (sum >= 2) {
      if (user_item.desc[0] >= 3) {
        user_item.desc[5] = 1;
        user_item.desc[6] = clamp_legacy_desc(25 + user_item.desc[0] * 3);
      } else if (user_item.desc[2] >= 2) {
        user_item.desc[5] = 1;
        user_item.desc[6] = clamp_legacy_desc(30 + user_item.desc[2] * 3);
      } else if (user_item.desc[3] >= 2) {
        user_item.desc[5] = 2;
        user_item.desc[6] = clamp_legacy_desc(20 + user_item.desc[3] * 2);
      } else if (user_item.desc[4] >= 2) {
        user_item.desc[5] = 3;
        user_item.desc[6] = clamp_legacy_desc(20 + user_item.desc[4] * 2);
      } else {
        user_item.desc[6] = clamp_legacy_desc(18 + sum * 2);
      }
    }
  }
}

bool legacy_can_take_on_item(const CharacterRecord& character,
                             const ItemConfig& item_config,
                             const LegacyUserItem& user_item,
                             std::int32_t slot,
                             std::int32_t current_wear_weight,
                             std::int32_t current_hand_weight,
                             std::int32_t old_slot_weight,
                             std::string* reason) {
  if (!legacy_item_fits_slot(item_config, slot)) {
    if (reason != nullptr) {
      *reason = "slot";
    }
    return false;
  }

  const auto upgraded = legacy_upgraded_item_config(item_config, user_item);
  if (!matches_legacy_job(upgraded.job, character.job)) {
    if (reason != nullptr) {
      *reason = "job";
    }
    return false;
  }
  if (!matches_legacy_sex(upgraded.sex, character.sex)) {
    if (reason != nullptr) {
      *reason = "sex";
    }
    return false;
  }
  if (upgraded.need == 0 && upgraded.need_level > 0 &&
      character.ability.level < upgraded.need_level) {
    if (reason != nullptr) {
      *reason = "level";
    }
    return false;
  }
  if (upgraded.need >= 1 && upgraded.need <= 3 && upgraded.need_level > 0) {
    const auto current = [&]() {
      switch (upgraded.need) {
        case 1:
          return packed_high(character.ability.dc);
        case 2:
          return packed_high(character.ability.mc);
        case 3:
          return packed_high(character.ability.sc);
        default:
          return 0;
      }
    }();
    if (current < upgraded.need_level) {
      if (reason != nullptr) {
        *reason = "need";
      }
      return false;
    }
  }

  const auto new_weight = std::max(upgraded.weight, 0);
  if (legacy_slot_uses_hand_weight(static_cast<std::size_t>(slot))) {
    if (current_hand_weight - old_slot_weight + new_weight >
        static_cast<std::int32_t>(character.ability.max_hand_weight)) {
      if (reason != nullptr) {
        *reason = "hand_weight";
      }
      return false;
    }
  } else if (current_wear_weight - old_slot_weight + new_weight >
             static_cast<std::int32_t>(character.ability.max_wear_weight)) {
    if (reason != nullptr) {
      *reason = "wear_weight";
    }
    return false;
  }
  return true;
}

}  // namespace mir2
