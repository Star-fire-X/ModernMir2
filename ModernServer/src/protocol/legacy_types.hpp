#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace mir2 {

constexpr std::uint16_t kLegacyGmData = 5;

constexpr std::uint16_t kSmPasswdSuccess = 502;
constexpr std::uint16_t kSmPasswdFail = 503;
constexpr std::uint16_t kSmNewIdSuccess = 504;
constexpr std::uint16_t kSmNewIdFail = 505;
constexpr std::uint16_t kSmChgPasswdSuccess = 506;
constexpr std::uint16_t kSmChgPasswdFail = 507;
constexpr std::uint16_t kSmQueryChr = 520;
constexpr std::uint16_t kSmNewChrSuccess = 521;
constexpr std::uint16_t kSmNewChrFail = 522;
constexpr std::uint16_t kSmDelChrSuccess = 523;
constexpr std::uint16_t kSmDelChrFail = 524;
constexpr std::uint16_t kSmStartPlay = 525;
constexpr std::uint16_t kSmStartFail = 526;
constexpr std::uint16_t kSmQueryChrFail = 527;
constexpr std::uint16_t kSmOutOfConnection = 528;
constexpr std::uint16_t kSmPassOkSelectServer = 529;
constexpr std::uint16_t kSmSelectServerOk = 530;
constexpr std::uint16_t kSmNeedUpdateAccount = 531;
constexpr std::uint16_t kSmUpdateIdSuccess = 532;
constexpr std::uint16_t kSmUpdateIdFail = 533;

constexpr std::uint16_t kSmTurn = 10;
constexpr std::uint16_t kSmWalk = 11;
constexpr std::uint16_t kSmSitDown = 12;
constexpr std::uint16_t kSmRun = 13;
constexpr std::uint16_t kSmHit = 14;
constexpr std::uint16_t kSmSpell = 17;
constexpr std::uint16_t kSmAlive = 27;
constexpr std::uint16_t kSmMoveFail = 28;
constexpr std::uint16_t kSmDisappear = 30;
constexpr std::uint16_t kSmStruck = 31;
constexpr std::uint16_t kSmDeath = 32;
constexpr std::uint16_t kSmNowDeath = 34;
constexpr std::uint16_t kSmHear = 40;
constexpr std::uint16_t kSmFeatureChanged = 41;
constexpr std::uint16_t kSmUsername = 42;
constexpr std::uint16_t kSmWinExp = 44;
constexpr std::uint16_t kSmLevelUp = 45;
constexpr std::uint16_t kSmBreakWeapon = 1102;
constexpr std::uint16_t kSmDayChanging = 46;
constexpr std::uint16_t kSmLogon = 50;
constexpr std::uint16_t kSmNewMap = 51;
constexpr std::uint16_t kSmAbility = 52;
constexpr std::uint16_t kSmHealthSpellChanged = 53;
constexpr std::uint16_t kSmMapDescription = 54;
constexpr std::uint16_t kSmAddItem = 200;
constexpr std::uint16_t kSmBagItems = 201;
constexpr std::uint16_t kSmDelItem = 202;
constexpr std::uint16_t kSmUpdateItem = 203;
constexpr std::uint16_t kSmAddMagic = 210;
constexpr std::uint16_t kSmSendMyMagic = 211;
constexpr std::uint16_t kSmDelMagic = 212;
constexpr std::uint16_t kSmSubAbility = 752;
constexpr std::uint16_t kSmDropItemSuccess = 600;
constexpr std::uint16_t kSmDropItemFail = 601;
constexpr std::uint16_t kSmItemShow = 610;
constexpr std::uint16_t kSmItemHide = 611;
constexpr std::uint16_t kSmOpenDoorOk = 612;
constexpr std::uint16_t kSmOpenDoorLock = 613;
constexpr std::uint16_t kSmCloseDoor = 614;
constexpr std::uint16_t kSmMagicFire = 638;
constexpr std::uint16_t kSmMagicFireFail = 639;
constexpr std::uint16_t kSmMagicLvExp = 640;
constexpr std::uint16_t kSmCharStatusChanged = 657;
constexpr std::uint16_t kSmSpaceMoveHide = 800;
constexpr std::uint16_t kSmSpaceMoveShow = 801;
constexpr std::uint16_t kSmSpaceMoveHide2 = 806;
constexpr std::uint16_t kSmSpaceMoveShow2 = 807;
constexpr std::uint16_t kSmTakeOnOk = 615;
constexpr std::uint16_t kSmTakeOnFail = 616;
constexpr std::uint16_t kSmTakeOffOk = 619;
constexpr std::uint16_t kSmTakeOffFail = 620;
constexpr std::uint16_t kSmSendUseItems = 621;
constexpr std::uint16_t kSmWeightChanged = 622;
constexpr std::uint16_t kSmEatOk = 635;
constexpr std::uint16_t kSmEatFail = 636;
constexpr std::uint16_t kSmDuraChange = 642;
constexpr std::uint16_t kSmClearObjects = 633;
constexpr std::uint16_t kSmChangeMap = 634;
constexpr std::uint16_t kSmMerchantSay = 643;
constexpr std::uint16_t kSmMerchantDlgClose = 644;
constexpr std::uint16_t kSmSendGoodsList = 645;
constexpr std::uint16_t kSmSendUserSell = 646;
constexpr std::uint16_t kSmSendBuyPrice = 647;
constexpr std::uint16_t kSmUserSellItemOk = 648;
constexpr std::uint16_t kSmUserSellItemFail = 649;
constexpr std::uint16_t kSmBuyItemSuccess = 650;
constexpr std::uint16_t kSmBuyItemFail = 651;
constexpr std::uint16_t kSmSendDetailGoodsList = 652;
constexpr std::uint16_t kSmGoldChanged = 653;
constexpr std::uint16_t kSmSendUserRepair = 668;
constexpr std::uint16_t kSmUserRepairItemOk = 669;
constexpr std::uint16_t kSmUserRepairItemFail = 670;
constexpr std::uint16_t kSmSendRepairCost = 671;
constexpr std::uint16_t kSmSendUserStorageItem = 700;
constexpr std::uint16_t kSmStorageOk = 701;
constexpr std::uint16_t kSmStorageFull = 702;
constexpr std::uint16_t kSmStorageFail = 703;
constexpr std::uint16_t kSmSaveItemList = 704;
constexpr std::uint16_t kSmTakeBackStorageItemOk = 705;
constexpr std::uint16_t kSmTakeBackStorageItemFail = 706;
constexpr std::uint16_t kSmTakeBackStorageItemFullBag = 707;
constexpr std::uint16_t kSmAreaState = 708;

constexpr std::uint16_t kCmQueryChr = 100;
constexpr std::uint16_t kCmNewChr = 101;
constexpr std::uint16_t kCmDelChr = 102;
constexpr std::uint16_t kCmSelChr = 103;
constexpr std::uint16_t kCmSelectServer = 104;
constexpr std::uint16_t kCmIdPassword = 2001;
constexpr std::uint16_t kCmAddNewUser = 2002;
constexpr std::uint16_t kCmChangePassword = 2003;
constexpr std::uint16_t kCmUpdateUser = 2004;

constexpr std::uint16_t kCmQueryUsername = 80;
constexpr std::uint16_t kCmQueryBagItems = 81;
constexpr std::uint16_t kCmDropItem = 1000;
constexpr std::uint16_t kCmPickup = 1001;
constexpr std::uint16_t kCmTakeOnItem = 1003;
constexpr std::uint16_t kCmTakeOffItem = 1004;
constexpr std::uint16_t kCmExchgTakeOnItem = 1005;
constexpr std::uint16_t kCmEat = 1006;
constexpr std::uint16_t kCmClickNpc = 1010;
constexpr std::uint16_t kCmMerchantDlgSelect = 1011;
constexpr std::uint16_t kCmMerchantQuerySellPrice = 1012;
constexpr std::uint16_t kCmUserSellItem = 1013;
constexpr std::uint16_t kCmUserBuyItem = 1014;
constexpr std::uint16_t kCmUserGetDetailItem = 1015;
constexpr std::uint16_t kCmDropGold = 1016;
constexpr std::uint16_t kCmUserRepairItem = 1023;
constexpr std::uint16_t kCmMerchantQueryRepairCost = 1024;
constexpr std::uint16_t kCmUserStorageItem = 1031;
constexpr std::uint16_t kCmUserTakeBackStorageItem = 1032;
constexpr std::uint16_t kCmTurn = 3010;
constexpr std::uint16_t kCmWalk = 3011;
constexpr std::uint16_t kCmSitDown = 3012;
constexpr std::uint16_t kCmRun = 3013;
constexpr std::uint16_t kCmHit = 3014;
constexpr std::uint16_t kCmHeavyHit = 3015;
constexpr std::uint16_t kCmBigHit = 3016;
constexpr std::uint16_t kCmSpell = 3017;
constexpr std::uint16_t kCmPowerHit = 3018;
constexpr std::uint16_t kCmLongHit = 3019;
constexpr std::uint16_t kCmWideHit = 3024;
constexpr std::uint16_t kCmFireHit = 3025;
constexpr std::uint16_t kCmSay = 3030;
constexpr std::uint16_t kCmCrossHit = 3035;

constexpr std::size_t kEquipDress = 0;
constexpr std::size_t kEquipWeapon = 1;
constexpr std::size_t kEquipRightHand = 2;
constexpr std::size_t kEquipNecklace = 3;
constexpr std::size_t kEquipHelmet = 4;
constexpr std::size_t kEquipArmRingLeft = 5;
constexpr std::size_t kEquipArmRingRight = 6;
constexpr std::size_t kEquipRingLeft = 7;
constexpr std::size_t kEquipRingRight = 8;
constexpr std::size_t kEquipBujuk = 9;
constexpr std::size_t kEquipBelt = 10;
constexpr std::size_t kEquipBoots = 11;
constexpr std::size_t kEquipCharm = 12;

constexpr std::size_t kMaxEquipSlots = 13;
constexpr std::size_t kMaxBagItems = 46;
constexpr std::size_t kMaxUserMagic = 20;
constexpr std::size_t kMaxSaveItems = 50;

#pragma pack(push, 1)

template <std::size_t N>
struct LegacyShortString {
  std::uint8_t length{0};
  std::array<char, N> value{};
};

struct LegacyDefaultMessage {
  std::int32_t recog{0};
  std::uint16_t ident{0};
  std::uint16_t param{0};
  std::uint16_t tag{0};
  std::uint16_t series{0};
};

struct LegacyUserEntryInfo {
  LegacyShortString<10> login_id{};
  LegacyShortString<10> password{};
  LegacyShortString<20> user_name{};
  LegacyShortString<14> ss_no{};
  LegacyShortString<14> phone{};
  LegacyShortString<20> quiz{};
  LegacyShortString<12> answer{};
  LegacyShortString<40> email{};
};

struct LegacyUserEntryAddInfo {
  LegacyShortString<20> quiz2{};
  LegacyShortString<12> answer2{};
  LegacyShortString<10> birthday{};
  LegacyShortString<13> mobile_phone{};
  LegacyShortString<20> memo1{};
  LegacyShortString<20> memo2{};
};

struct LegacyMessageBodyWL {
  std::int32_t lparam1{0};
  std::int32_t lparam2{0};
  std::int32_t ltag1{0};
  std::int32_t ltag2{0};
};

struct LegacyCharDesc {
  std::int32_t feature{0};
  std::int32_t status{0};
};

struct LegacyUserItem {
  std::int32_t make_index{0};
  std::uint16_t index{0};
  std::uint16_t dura{0};
  std::uint16_t dura_max{0};
  std::array<std::uint8_t, 14> desc{};
  std::uint8_t color_r{0};
  std::uint8_t color_g{0};
  std::uint8_t color_b{0};
  std::array<char, 13> prefix{};
};

struct LegacyAbility {
  std::uint8_t level{1};
  std::uint8_t reserved1{0};
  std::uint16_t ac{0};
  std::uint16_t mac{0};
  std::uint16_t dc{0};
  std::uint16_t mc{0};
  std::uint16_t sc{0};
  std::uint16_t hp{15};
  std::uint16_t mp{15};
  std::uint16_t max_hp{15};
  std::uint16_t max_mp{15};
  std::uint8_t exp_count{0};
  std::uint8_t exp_max_count{0};
  std::uint32_t exp{0};
  std::uint32_t max_exp{100};
  std::uint16_t weight{0};
  std::uint16_t max_weight{30};
  std::uint8_t wear_weight{0};
  std::uint8_t max_wear_weight{100};
  std::uint8_t hand_weight{0};
  std::uint8_t max_hand_weight{100};
};

struct LegacyStdItem {
  LegacyShortString<14> name{};
  std::uint8_t std_mode{0};
  std::uint8_t shape{0};
  std::uint8_t weight{0};
  std::uint8_t ani_count{0};
  std::int8_t special_pwr{0};
  std::uint8_t item_desc{0};
  std::uint16_t looks{0};
  std::uint16_t dura_max{0};
  std::uint16_t ac{0};
  std::uint16_t mac{0};
  std::uint16_t dc{0};
  std::uint16_t mc{0};
  std::uint16_t sc{0};
  std::uint8_t need{0};
  std::uint8_t need_level{0};
  std::int32_t price{0};
  std::int32_t stock{0};
  std::uint8_t atk_spd{0};
  std::uint8_t agility{0};
  std::uint8_t accurate{0};
  std::uint8_t mg_avoid{0};
  std::uint8_t strong{0};
  std::uint8_t undead{0};
  std::int32_t hp_add{0};
  std::int32_t mp_add{0};
  std::int32_t exp_add{0};
  std::uint8_t eff_type1{0};
  std::uint8_t eff_rate1{0};
  std::uint8_t eff_value1{0};
  std::uint8_t eff_type2{0};
  std::uint8_t eff_rate2{0};
  std::uint8_t eff_value2{0};
};

struct LegacyClientItem {
  LegacyStdItem item{};
  std::int32_t make_index{0};
  std::uint16_t dura{0};
  std::uint16_t dura_max{0};
};

struct LegacyDefMagic {
  std::uint16_t magic_id{0};
  LegacyShortString<12> magic_name{};
  std::uint8_t effect_type{0};
  std::uint8_t effect{0};
  std::uint16_t spell{0};
  std::uint16_t min_power{0};
  std::array<std::uint8_t, 4> need_level{};
  std::array<std::int32_t, 4> max_train{};
  std::uint8_t max_train_level{0};
  std::uint8_t job{0};
  std::int32_t delay_time{1000};
  std::uint8_t def_spell{0};
  std::uint8_t def_min_power{0};
  std::uint16_t max_power{0};
  std::uint8_t def_max_power{0};
  LegacyShortString<15> desc{};
};

struct LegacyUseMagicInfo {
  std::uint16_t magic_id{0};
  std::uint8_t level{0};
  char key{0};
  std::int32_t cur_train{0};
};

struct LegacyClientMagic {
  char key{0};
  std::uint8_t level{0};
  std::int32_t cur_train{0};
  LegacyDefMagic def{};
};

#pragma pack(pop)

static_assert(sizeof(LegacyUserItem) == 40);
static_assert(offsetof(LegacyUserItem, make_index) == 0);
static_assert(offsetof(LegacyUserItem, index) == 4);
static_assert(offsetof(LegacyUserItem, dura) == 6);
static_assert(offsetof(LegacyUserItem, dura_max) == 8);
static_assert(offsetof(LegacyUserItem, desc) == 10);
static_assert(offsetof(LegacyUserItem, color_r) == 24);
static_assert(offsetof(LegacyUserItem, color_g) == 25);
static_assert(offsetof(LegacyUserItem, color_b) == 26);
static_assert(offsetof(LegacyUserItem, prefix) == 27);
static_assert(sizeof(LegacyStdItem) == 69);
static_assert(offsetof(LegacyStdItem, name) == 0);
static_assert(offsetof(LegacyStdItem, std_mode) == 15);
static_assert(offsetof(LegacyStdItem, shape) == 16);
static_assert(offsetof(LegacyStdItem, weight) == 17);
static_assert(offsetof(LegacyStdItem, ani_count) == 18);
static_assert(offsetof(LegacyStdItem, price) == 37);
static_assert(offsetof(LegacyStdItem, eff_value2) == 68);
static_assert(sizeof(LegacyClientItem) == 77);
static_assert(offsetof(LegacyClientItem, item) == 0);
static_assert(offsetof(LegacyClientItem, make_index) == 69);
static_assert(offsetof(LegacyClientItem, dura) == 73);
static_assert(offsetof(LegacyClientItem, dura_max) == 75);

template <std::size_t N>
inline void set_short_string(LegacyShortString<N>& target, const std::string& value) {
  target.length = static_cast<std::uint8_t>(std::min<std::size_t>(N, value.size()));
  std::fill(target.value.begin(), target.value.end(), '\0');
  std::memcpy(target.value.data(), value.data(), target.length);
}

template <std::size_t N>
[[nodiscard]] inline std::string to_string(const LegacyShortString<N>& value) {
  return std::string(value.value.data(), value.value.data() + value.length);
}

inline std::uint16_t make_word(std::uint8_t low, std::uint8_t high) {
  return static_cast<std::uint16_t>(low) | (static_cast<std::uint16_t>(high) << 8);
}

inline std::int32_t make_long(std::uint16_t low, std::uint16_t high) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(low) |
                                   (static_cast<std::uint32_t>(high) << 16));
}

inline std::int32_t make_feature(std::uint8_t race, std::uint8_t dress, std::uint8_t weapon,
                                 std::uint8_t face) {
  return make_long(make_word(race, weapon), make_word(face, dress));
}

inline std::uint16_t low_word(std::int32_t value) {
  return static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) & 0xffffu);
}

inline std::uint16_t high_word(std::int32_t value) {
  return static_cast<std::uint16_t>((static_cast<std::uint32_t>(value) >> 16) & 0xffffu);
}

inline bool is_empty(const LegacyUserItem& item) { return item.index == 0; }
inline bool is_empty(const LegacyUseMagicInfo& magic) { return magic.magic_id == 0; }

}  // namespace mir2
