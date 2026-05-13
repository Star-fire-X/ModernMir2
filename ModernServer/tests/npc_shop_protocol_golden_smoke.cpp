#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/legacy_types.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "npc_shop_protocol_golden_smoke failed at " << stage << '\n';
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

bool contains_in_order(const std::string& text,
                       const std::vector<std::string_view>& tokens) {
  std::size_t offset = 0;
  for (const auto token : tokens) {
    const auto pos = text.find(token, offset);
    if (pos == std::string::npos) {
      std::cerr << "missing ordered token: " << token << '\n';
      return false;
    }
    offset = pos + token.size();
  }
  return true;
}

std::string case_text(const std::string& text, std::string_view case_name) {
  const auto name_token = std::string{"\"name\": \""} + std::string(case_name) + "\"";
  const auto begin = text.find(name_token);
  if (begin == std::string::npos) {
    return {};
  }
  const auto next = text.find("\"name\": \"", begin + name_token.size());
  return text.substr(begin, next == std::string::npos ? std::string::npos : next - begin);
}

bool check_compiled_protocol_constants() {
  return mir2::kCmClickNpc == 1010 && mir2::kCmMerchantDlgSelect == 1011 &&
         mir2::kCmMerchantQuerySellPrice == 1012 && mir2::kCmUserSellItem == 1013 &&
         mir2::kCmUserBuyItem == 1014 && mir2::kCmUserGetDetailItem == 1015 &&
         mir2::kCmUserRepairItem == 1023 && mir2::kCmMerchantQueryRepairCost == 1024 &&
         mir2::kCmUserStorageItem == 1031 && mir2::kCmUserTakeBackStorageItem == 1032 &&
         mir2::kSmAddItem == 200 && mir2::kSmMerchantSay == 643 &&
         mir2::kSmMerchantDlgClose == 644 && mir2::kSmSendGoodsList == 645 &&
         mir2::kSmSendUserSell == 646 && mir2::kSmSendBuyPrice == 647 &&
         mir2::kSmUserSellItemOk == 648 && mir2::kSmUserSellItemFail == 649 &&
         mir2::kSmBuyItemSuccess == 650 && mir2::kSmBuyItemFail == 651 &&
         mir2::kSmSendDetailGoodsList == 652 && mir2::kSmGoldChanged == 653 &&
         mir2::kSmSendUserRepair == 668 && mir2::kSmUserRepairItemOk == 669 &&
         mir2::kSmUserRepairItemFail == 670 && mir2::kSmSendRepairCost == 671;
}

bool check_fixture_constants(const std::string& constants) {
  return contains_all(constants,
                      {
                          "\"CM_CLICKNPC\": 1010",
                          "\"CM_MERCHANTDLGSELECT\": 1011",
                          "\"CM_MERCHANTQUERYSELLPRICE\": 1012",
                          "\"CM_USERSELLITEM\": 1013",
                          "\"CM_USERBUYITEM\": 1014",
                          "\"CM_USERGETDETAILITEM\": 1015",
                          "\"CM_USERREPAIRITEM\": 1023",
                          "\"CM_MERCHANTQUERYREPAIRCOST\": 1024",
                          "\"CM_USERSTORAGEITEM\": 1031",
                          "\"CM_USERTAKEBACKSTORAGEITEM\": 1032",
                          "\"SM_MERCHANTSAY\": 643",
                          "\"SM_MERCHANTDLGCLOSE\": 644",
                          "\"SM_SENDGOODSLIST\": 645",
                          "\"SM_SENDUSERSELL\": 646",
                          "\"SM_SENDBUYPRICE\": 647",
                          "\"SM_USERSELLITEM_OK\": 648",
                          "\"SM_USERSELLITEM_FAIL\": 649",
                          "\"SM_BUYITEM_SUCCESS\": 650",
                          "\"SM_BUYITEM_FAIL\": 651",
                          "\"SM_SENDDETAILGOODSLIST\": 652",
                          "\"SM_GOLDCHANGED\": 653",
                          "\"SM_SENDUSERREPAIR\": 668",
                          "\"SM_USERREPAIRITEM_OK\": 669",
                          "\"SM_USERREPAIRITEM_FAIL\": 670",
                          "\"SM_SENDREPAIRCOST\": 671",
                          "\"npc_interaction_range_rect\": 15",
                          "\"no_product\": 1",
                          "\"cannot_carry_or_add_item\": 2",
                          "\"not_enough_gold\": 3",
                      });
}

bool check_fixture_sequences(const std::string& sequences) {
  const auto click = case_text(sequences, "click_npc_opens_main_dialog");
  const auto open_buy = case_text(sequences, "select_buy_opens_goods_list");
  const auto buy_ok = case_text(sequences, "buy_simple_item_success");
  const auto buy_gold_fail = case_text(sequences, "buy_simple_item_fail_not_enough_gold");
  const auto open_sell = case_text(sequences, "open_sell_dialog_and_query_price");
  const auto sell_ok = case_text(sequences, "sell_item_success");
  const auto repair_ok = case_text(sequences, "open_repair_dialog_query_and_repair_success");
  const auto close = case_text(sequences, "close_dialog_from_script");
  if (click.empty() || open_buy.empty() || buy_ok.empty() || buy_gold_fail.empty() ||
      open_sell.empty() || sell_ok.empty() || repair_ok.empty() || close.empty()) {
    return false;
  }

  return contains_in_order(click,
                           {
                               "CM_CLICKNPC",
                               "SM_MERCHANTSAY",
                               "Shopkeeper/main dialog text",
                           }) &&
         contains_in_order(open_buy,
                           {
                               "CM_MERCHANTDLGSELECT",
                               "@buy",
                               "SM_MERCHANTSAY",
                               "SM_SENDGOODSLIST",
                               "Potion/0/100/12/",
                           }) &&
         contains_in_order(buy_ok,
                           {
                               "CM_USERBUYITEM",
                               "SM_ADDITEM",
                               "SM_BUYITEM_SUCCESS",
                               "P.GoldAfterBuy",
                           }) &&
         contains_in_order(buy_gold_fail,
                           {
                               "CM_USERBUYITEM",
                               "SM_BUYITEM_FAIL",
                               "\"recog\": 3",
                           }) &&
         contains_in_order(open_sell,
                           {
                               "CM_MERCHANTDLGSELECT",
                               "@sell",
                               "SM_SENDUSERSELL",
                               "CM_MERCHANTQUERYSELLPRICE",
                               "SM_SENDBUYPRICE",
                           }) &&
         contains_in_order(sell_ok,
                           {
                               "CM_USERSELLITEM",
                               "SM_USERSELLITEM_OK",
                               "P.GoldAfterSell",
                               "P bag no longer contains the sold item",
                           }) &&
         contains_in_order(repair_ok,
                           {
                               "CM_MERCHANTDLGSELECT",
                               "@repair",
                               "SM_SENDUSERREPAIR",
                               "CM_MERCHANTQUERYREPAIRCOST",
                               "SM_SENDREPAIRCOST",
                               "CM_USERREPAIRITEM",
                               "SM_USERREPAIRITEM_OK",
                               "Item.DuraAfterRepair",
                           }) &&
         contains_in_order(close,
                           {
                               "CM_MERCHANTDLGSELECT",
                               "@exit",
                               "SM_MERCHANTDLGCLOSE",
                           });
}

}  // namespace

int main() {
  if (!check_compiled_protocol_constants()) {
    return fail("compiled constants");
  }

  const auto source_root = std::filesystem::path(__FILE__).parent_path().parent_path();
  const auto fixture_root = source_root / "tests" / "golden" / "npc_shop_phase1";
  const auto constants = read_text(fixture_root / "npc_shop_protocol_constants.json");
  const auto sequences = read_text(fixture_root / "npc_shop_sequence_cases.json");
  if (!check_fixture_constants(constants)) {
    return fail("fixture constants");
  }
  if (!check_fixture_sequences(sequences)) {
    return fail("fixture sequences");
  }
  return 0;
}
