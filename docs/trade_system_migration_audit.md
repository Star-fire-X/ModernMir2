# 传奇 Mir2 交易系统 Delphi → C++ 行为兼容性审查报告

> 审查日期: 2026-05-25
> 审查范围: Delphi `Source/M2Server/ObjBase.pas` vs C++ `ModernServer/src/world/` + `ModernServer/src/services/`
> 审查目标: 行为兼容性, 非重构

---

## 1. 总体结论

**部分兼容，存在需要修复的行为偏移**

**理由:**

C++ 交易系统在协议层、物品/金币交换原子性、核心状态机方面基本复刻了 Delphi 行为。但存在以下实质性偏移:

- **C++ 移动即取消交易（P1）**: Delphi 仅在玩家转身或对方离开前方格子时取消，C++ 每步移动都取消。
- **C++ 修改交易内容会清除对方确认标志（P1）**: Delphi 只是阻止修改（对方已确认时），不清除对方标志。
- **C++ 缺失 `BoExchangeAvailable` 玩家交易开关（P2）**。
- **C++ 缺失台湾活动物品不可交易检查（P3）**。
- **C++ 网关层双重维护交易状态**，存在状态不一致风险（P2）。
- **C++ 断线/取消交易边界仍需 PR-2 单独审查（P0 候选）**：正常断线会进入 world logout，但 `cancel_trade_for` 在无法完整退还物品时仍可能残留 TradeSession。

未发现直接物品复制或金币复制路径。`commit_trade` 的常规 rollback 较完整，但 `cancel_trade_for` 的“背包无法完整退还”路径尚未证明完备，不能宣称所有取消/断线场景都无物品或金币丢失风险。

---

## 2. Delphi 原始交易链路

```
玩家 A 发起交易请求
  → TUserHuman.Run 消息分发 (ObjBase.pas:12271)
  → CM_DEALTRY → ServerGetDealTry(withwho) (ObjBase.pas:13959)
  → 检查 BoDealing (已在自己交易中则 exit)
  → GetFrontCret 获取前方目标
  → 检查目标非空、非自身
  → 检查互相面对 (cret.GetFrontCret = self)
  → 检查目标未在交易 (not cret.BoDealing)
  → 检查目标 RaceServer = RC_USERHUMAN
  → 检查目标 BoExchangeAvailable (玩家交易开关)
  → 双方 SysMsg 提示 + 调用双方 StartDeal (ObjBase.pas:13998)
  → StartDeal: BoDealing=TRUE, DealCret=who, ResetDeal, 发送 SM_DEALMENU(673), 记录 DealItemChangeTime

双方打开交易窗口
  → 客户端接收 SM_DEALMENU 后打开交易 UI

放入物品
  → CM_DEALADDITEM → ServerGetDealAddItem(iidx, iname) (ObjBase.pas:14099)
  → 检查 DealCret <> nil
  → 检查 not DealCret.BoDealSelect (对方未确认)
  → 遍历 ItemList 按 MakeIndex+名称查找物品
  → 检查 pstd.StdMode <> TAIWANEVENTITEM (台湾活动物品不可交易)
  → 检查 DealList.Count < MAXDEALITEM(10)
  → ItemList → DealList 移动物品 (物理移动指针)
  → AddDealItem: 发送 SM_DEALADDITEM_OK(675) 给自己
  → 发送 SM_DEALREMOTEADDITEM(682) 给交易对方 (含完整 TClientItem)
  → 更新双方 DealItemChangeTime

放入金币
  → CM_DEALCHGGOLD → ServerGetDealChangeGold(dgold) (ObjBase.pas:14159)
  → 检查 dgold >= 0
  → 检查 GetFrontCret = DealCret (仍在面前)
  → 检查 not DealCret.BoDealSelect (对方未确认)
  → 检查 self.Gold + DealGold >= dgold (持有金币足够)
  → self.Gold := (self.Gold + DealGold) - dgold (先退还旧 DealGold，再扣除新金额)
  → DealGold := dgold
  → 发送 SM_DEALCHGGOLD_OK(684) 给自己
  → 发送 SM_DEALREMOTECHGGOLD(686) 给交易对方
  → 更新双方 DealItemChangeTime

确认交易
  → CM_DEALEND → ServerGetDealEnd (ObjBase.pas:14186)
  → BoDealSelect := TRUE
  → 检查 1 秒稳定期 (GetTickCount - DealItemChangeTime >= 1000)
  → 不稳定则 BrokeDeal (取消交易)
  → 检查 DealCret.BoDealSelect (双方都确认)
  → 检查背包空间: MAXBAGITEM - Itemlist.Count >= 对方 DealList.Count
  → 检查金币上限: AvailableGold - Gold >= 对方 DealGold
  → 转移物品: DealList → 对方 AddItem(触发 WeightChanged) + SendAddItem
  → 转移金币: DealGold → 对方 Gold + GoldChanged
  → 双方发送 SM_DEALSUCCESS(687)
  → 清理双方交易状态

交易取消
  → CM_DEALCANCEL → ServerGetDealCancel → BrokeDeal (ObjBase.pas:14007)
  → BoDealing := FALSE
  → 发送 SM_DEALCANCEL(681)
  → 通知对方也 BrokeDeal (通过 DealCret)
  → ResetDeal: DealList 物品 → ItemList, Gold := Gold + DealGold
  → SysMsg "交易取消"

Run 循环周期性检查 (ObjBase.pas:11827-11831)
  → 每帧检查: if (GetFrontCret <> DealCret) or (DealCret = self) or (DealCret = nil) then BrokeDeal
  → Ghost 检查 (ObjBase.pas:7843-7845): if DealCret.BoGhost then DealCret := nil
```

---

## 3. C++ 当前交易链路

```
Modern client_v1 gateway 路径（不是 legacy Delphi 客户端）:
  Session → handle_trade_try_request (gateway, client_v1_game_gateway_service.cpp:1580)
  → 检查 session in_game, target 存在, 双方无 pending/active trade
  → 设置 pending_trade 状态
  → post_canonical_command → decode_client_v1_trade_try_command
  → CanonicalLegacyCommand → LogicCommandKind::trade_try
  → world_service → ActorMailKind::trade_try
  → map_actor_mail.hpp:1310 handler:
    → 检查 requester 非空、非死亡
    → is_directly_in_front_of(*requester, *candidate) 查找目标
    → 检查 mutually_facing (双方互相面对)
    → 检查双方无已有 trade_session
    → 创建 TradeSession, 填充双方 actor_id
    → 发送 kSmDealMenu(673) 给双方
  → gateway 收到 kSmDealMenu:
    → 验证 pending_trade_remote_name 匹配
    → trade_visible = true, 构建 TradeState, 发送给双方

放入物品 (client_v1):
  → handle_trade_add_item_request (gateway:1645)
  → 验证 trade_visible, trade_item_from_bag_locked (查重+查找)
  → 添加到 trade_local_items, 清除双方 trade_local_accept
  → post_canonical_command → trade_add_item
  → map_actor_mail.hpp:1365 handler:
    → 检查 session/offer 有效, offer->accepted = false 则阻止
    → 检查无重复 make_index
    → player->remove_bag_item(make_index) 物理移除
    → 加入 offer->items, 设置双方 accepted = false
    → 发送 kSmDealAddItemOk(675) 给自己
    → 发送 kSmDealRemoteAddItem(682) 给交易对方

放入金币 (client_v1):
  → handle_trade_set_gold_request (gateway:1739)
  → 上限裁切: gold = min(request.gold, character.gold)
  → 清除双方 trade_local_accept
  → post_canonical_command → trade_set_gold
  → map_actor_mail.hpp:1466 handler:
    → 检查 mail.amount >= 0, offer->accepted = false
    → 检查 character.gold + offer->gold >= mail.amount
    → 先 add_gold(offer->gold) 退还旧值, 再 spend_gold(mail.amount)
    → offer->gold = mail.amount, 清除双方 accepted
    → 发送 kSmDealChangeGoldOk(684)/Fail(685) 给自己
    → 发送 kSmDealRemoteChangeGold(686) 给交易对方

确认交易 (client_v1):
  → handle_trade_accept_request (gateway:1773)
  → 设置 trade_local_accept = true
  → post_canonical_command → trade_accept
  → map_actor_mail.hpp:1505 handler:
    → 检查 1 秒稳定期 (kLegacyTradeStableMs = 1000)
    → 不稳定则 cancel_trade_for (取消交易)
    → offer->accepted = true
    → 双方都 accepted → commit_trade (map_actor.cpp:2440)

commit_trade (map_actor.cpp:2440):
  → 检查双方非空、非死亡、in_interaction_range (距离<=15)
  → 检查金币无负值、金币不超 kLegacyBagGold (50000000)
  → 检查物品 make_index 无重复、物品仍存在于原主
  → 检查接收方 can_receive_trade_items (背包空格+重量+make_index 冲突)
  → 转移物品: add_bag_item (带 rollback 机制)
  → 转移金币: add_gold
  → 发送 add_item/gold_changed/weight_changed 给双方
  → 发送 kSmDealSuccess(687) 给双方
  → 清理 session

交易取消:
  → handle_trade_cancel_request (gateway:1614)
  → 清除 pending 或 post canonical trade_cancel
  → cancel_trade_for (map_actor.cpp:2370):
    → 发送 kSmDealCancel(681) 给双方
    → 退还物品: add_bag_item (逐个)
    → 退还金币: add_gold
    → 清理 session
    → 若背包满则无法完全退还, 保留剩余物品在 offer 中

移动/切图:
  → map_actor_movement.hpp:166, 190, 282, 303:
  → cancel_trade_for 在任何移动/切图前调用

死亡:
  → settle_player_death (map_actor.cpp:3402):
  → cancel_trade_for 在最开始调用

断线:
  → handle_disconnected (gateway:676):
  → 清除 peer 的 trade 状态
  → 发送空 TradeState 给对方 (关闭交易窗口)
  → 注意: 仅清除 gateway 层状态, world 层的 TradeSession 不清除!
```

---

## 4. 行为兼容性对照表

| 阶段 | Delphi 行为 | C++ 行为 | 兼容 | 风险 | 代码证据 |
|---|---|---|---|---|---|
| 交易请求-面向检查 | GetFrontCret + 互相面对 | is_directly_in_front_of + mutually_facing | 是 | - | Delphi:13965-13966, C++:map_actor_mail.hpp:1319-1333 |
| 交易请求-死亡检查 | 无显式死亡检查(通过 BoExchangeAvailable 间接) | 显式 is_dead() 检查 | 是(更强) | - | C++:map_actor_mail.hpp:1326 |
| 交易请求-重复交易 | 检查双方 BoDealing | 检查 trade_session_for 返回 nullptr | 是 | - | Delphi:13963,13966, C++:map_actor_mail.hpp:1334-1335 |
| 交易请求-玩家开关 | 检查 BoExchangeAvailable | 无此检查 | **否** | P2 | Delphi:13968 |
| 交易请求-台湾活动物品 | 检查 StdMode <> TAIWANEVENTITEM | 无此检查 | **否** | P3 | Delphi:14114 |
| 交易启动-双方窗口 | StartDeal 同时调用双方 | 发送 kSmDealMenu 给双方(gateway 需匹配 pending) | 是 | - | Delphi:13972-13973, C++:map_actor_mail.hpp:1353-1358 |
| 放入物品-对方已确认阻止 | 检查 not DealCret.BoDealSelect | 检查 offer->accepted | **否(行为不同)** | P1 | Delphi:14110, C++:map_actor_mail.hpp:1375 |
| 放入物品-清除对方确认 | 不清除(只阻止) | 清除双方 accepted=false | **否** | P1 | Delphi:无清除逻辑, C++:map_actor_mail.hpp:1396-1397 |
| 放入金币-距离重检 | 检查 GetFrontCret = DealCret | 不检查(仅检查 session 存在) | **否** | P2 | Delphi:14168, C++:map_actor_mail.hpp:1469 |
| 放入金币-清除对方确认 | 不清除(只阻止) | 清除双方 accepted=false | **否** | P1 | Delphi:无清除逻辑, C++:map_actor_mail.hpp:1490-1491 |
| 金币模型 | Delta 模型: Gold+DealGold-dgold | Delta 模型: add_gold(old)+spend_gold(new) | 是 | - | Delphi:14171, C++:map_actor_mail.hpp:1483-1487 |
| 确认-1秒稳定期 | 检查 < 1000ms, 不稳定则 BrokeDeal | 检查 < 1000ms, 不稳定则 cancel_trade_for | 是 | - | Delphi:14195-14198, C++:map_actor_mail.hpp:1516-1522 |
| 提交前-背包检查 | 检查 MAXBAGITEM - Count >= 对方物品数 | can_receive_trade_items (空格+重量+make_index) | 是(更强) | - | Delphi:14204,14207, C++:map_actor.cpp:2335-2368 |
| 提交前-金币检查 | 检查 AvailableGold 上限 | 检查 kLegacyBagGold 上限 | 是 | - | Delphi:14205,14208, C++:map_actor.cpp:2475-2476 |
| 提交-原子交换 | 先转移物品再转移金币 | 先转移物品(带rollback)再转移金币 | 是 | - | Delphi:14212-14267, C++:map_actor.cpp:2502-2524 |
| 提交-失败回滚 | 无显式 rollback(BrokeDeal 不完整) | 有 rollback_added lambda(partial) | 是(更强) | - | C++:map_actor.cpp:2489-2500 |
| 取消-物品回滚 | ResetDeal: DealList→ItemList, Gold+=DealGold | cancel_trade_for: add_bag_item 逐个, add_gold | 是 | - | Delphi:13987-13994, C++:map_actor.cpp:2391-2419 |
| 取消-背包满处理 | 直接移动(不检查背包容量) | 逐个检查 add_bag_item 返回值, 满则保留 | 是(更强) | - | C++:map_actor.cpp:2395-2401 |
| 移动取消交易 | 仅转身/对方离开触发(GetFrontCret 变) | **每步移动都取消** | **否** | P1 | Delphi:11827-11831, C++:map_actor_movement.hpp:166 |
| 死亡取消交易 | Ghost 检查→DealCret:=nil→Run 循环 BrokeDeal | settle_player_death 显式 cancel_trade_for | 是 | - | Delphi:7843-7845, C++:map_actor.cpp:3409 |
| 断线取消交易 | ReadySave → BrokeDeal | handle_disconnected 清除 gateway 状态 | **部分(见偏移)** | P2 | Delphi:11770-11773, C++:gateway:711-716 |
| 交易中禁止NPC交互 | BoDealing 时 exit | reject_trade_locked_item_change | 是 | - | Delphi:13514,13798, C++:map_actor_mail.hpp:14-20 |
| 交易中禁止拾取物品 | BoDealing 时 exit | reject_trade_locked_item_change | 是 | - | Delphi:6864, C++:map_actor_mail.hpp:1071 |
| 交易中禁止装备操作 | BoDealing 时阻止 | reject_trade_locked_item_change | 是 | - | Delphi:13314, C++:map_actor_mail.hpp:989 |
| SM_* 消息 ID | 673-687 | kSmDeal* 673-687 一致 | 是 | - | Grobal2.pas:968-982, legacy_types.hpp:112-124 |
| CM_* 消息 ID | 1025-1030 | kCmDeal* 1025-1030 一致 | 是 | - | Grobal2.pas:1130-1135, legacy_types.hpp:166-171 |
| SM_DEALSUCCESS 顺序 | 先对方后自己 | 先 second 后 first | 是(等价) | - | Delphi:14281-14289, C++:map_actor.cpp:2548-2551 |

---

## 5. 交易状态机兼容性审查

### 5.1 Delphi 状态机 (隐式, 基于 Boolean 标志组合)

| 状态 | 条件 | 说明 |
|---|---|---|
| Idle | BoDealing=FALSE, DealCret=nil | 未交易 |
| Trading | BoDealing=TRUE, DealCret<>nil, BoDealSelect=FALSE | 交易窗口打开, 可修改 |
| Confirmed(己方) | BoDealSelect=TRUE | 已按确认, 等待对方 |
| Committed | 双方 BoDealSelect + 时间稳定 + 空间金币检查通过 → 交换 → 清理 | 瞬间完成 |
| Cancelled | BrokeDeal 调用 | 清理+回滚 |

**Delphi 无显式 Locked 状态**。`BoDealSelect` 既是"锁定"也是"确认"——Delphi 客户端只有一个"确认"按钮, 没有独立的"锁定"按钮。

### 5.2 C++ 状态机

| 状态 | 条件 |
|---|---|
| Idle | trade_session_for = nullptr |
| Pending (gateway only) | pending_trade_remote_name 非空 |
| Trading | trade_visible=true (gateway), TradeSession 存在 (world) |
| Accepted(己方) | offer->accepted = true |
| Committed | 双方 accepted + 时间稳定 + 检查通过 → commit_trade |
| Cancelled | cancel_trade_for 调用 |

### 5.3 状态机差异

| 差异点 | 影响 | 风险 |
|---|---|---|
| Delphi 无 Pending 状态 | C++ 的两阶段(发起→世界确认→打开窗口)增加了一个中间状态, 依赖 gateway 匹配 | P2 |
| C++ 无 "对方已确认时阻止修改" 而是清除对方确认 | 用户体验不同: Delphi 保留对方确认状态, C++ 重置 | P1 |
| 移动即取消 vs 转身才取消 | C++ 更严格, 玩家无法在交易中微调位置 | P1 |

---

## 6. 交易发起/接受兼容性审查

| 检查项 | Delphi | C++ | 兼容 |
|---|---|---|---|
| 请求方在线 | 消息处理前提 | session 存在检查 | 是 |
| 被请求方在线 | GetFrontCret 非 nil | find_player 非 nullptr | 是 |
| 同地图 | 隐含(GetFrontCret 同地图) | is_directly_in_front_of 隐含同地图 | 是 |
| 距离 | 相邻格子(GetFrontCret 语义) | 相邻格子(direction_delta) | 是 |
| 互相面对 | GetFrontCret 双向检查 | mutually_facing | 是 |
| 死亡 | BoExchangeAvailable 间接 | 显式 is_dead() | 是 |
| 已在交易 | BoDealing 检查 | trade_session_for 检查 | 是 |
| 玩家允许交易 | **BoExchangeAvailable** | **缺失** | **否** |
| NPC/商店互斥 | BoDealing 阻止 NPC 交互 | reject_trade_locked_item_change | 是 |
| 请求超时 | DealItemChangeTime > 3000 后物品可被丢弃 | 无显式交易请求超时 | P3 |

---

## 7. 交易物品兼容性审查

| 检查项 | Delphi | C++ | 兼容 |
|---|---|---|---|
| 物品位置校验 | ItemList 中按索引查找 | bag_items 中按 make_index 查找 | 是 |
| MakeIndex 校验 | PTUserItem(ItemList[i]).MakeIndex = iidx | item.make_index == mail.item_make_index | 是 |
| 名称校验 | CompareText(GetStdItemName, iname) = 0 | item_name(item, configs_) == mail.payload | 是 |
| 数量校验 | 无(一次一件) | 无(一次一件) | 是 |
| 持久保持 | uitem.Dura/DuraMax 完整传递 | LegacyUserItem 完整保留 | 是 |
| 附加属性保持 | GetUpgradeStdItem 复制升级属性 | LegacyUserItem 完整保留 | 是 |
| 绑定/不可交易 | 台湾活动物品检查(StdMode <> TAIWANEVENTITEM) | **缺失台湾活动物品检查** | **否(P3)** |
| 交易栏物品锁定 | 移入 DealList(物理移除) | remove_bag_item(物理移除) | 是 |
| 修改物品后锁定状态 | **不清除**(仅阻止修改) | **清除双方 accepted** | **否(P1)** |
| 取消回滚 | DealList→ItemList 整体移回 | add_bag_item 逐个移回 | 是 |
| 成功转移 | AddItem+SendAddItem | add_bag_item+make_add_item_packet | 是 |
| 背包空间不足 | ServerGetDealEnd 中检查 | can_receive_trade_items 提前检查 | 是 |
| 已放入物品被丢弃 | 物理已移出 ItemList, 不可丢弃 | remove_bag_item 后不可丢弃 | 是 |
| 已放入物品被使用 | 不在 ItemList, 不可使用 | remove_bag_item 后不在 bag | 是 |

---

## 8. 交易金币兼容性审查

| 检查项 | Delphi | C++ | 兼容 |
|---|---|---|---|
| 金币数量校验 | dgold >= 0 | mail.amount >= 0 | 是 |
| 金币扣除时机 | 修改时立即: Gold+DealGold-dgold | 修改时立即: add_gold(old)+spend_gold(new) | 是 |
| 金币模型 | Delta 模型(退还旧值+扣除新值) | Delta 模型 | 是 |
| 修改金币后锁定状态 | **不清除**(仅阻止修改) | **清除双方 accepted** | **否(P1)** |
| 锁定后金币修改 | 无独立锁定步骤 | 无独立锁定步骤(accepted 前可改) | 是 |
| 交易期间金币消耗 | 已扣除,不可被其他系统消耗 | 已 spend_gold,不可被其他系统消耗 | 是 |
| 取消回滚 | Gold := Gold + DealGold | add_gold(offer.gold) | 是 |
| 成功转移 | DealGold→Gold+GoldChanged | add_gold | 是 |
| 金币上限 | AvailableGold(通常=50000000) | kLegacyBagGold(50000000) | 是 |
| 非法金币请求 | 检查 self.Gold+DealGold >= dgold | 检查 gold+offer->gold >= amount | 是 |
| 修改金币时距离检查 | **GetFrontCret = DealCret** | **缺失** | **否(P2)** |

---

## 9. 提交/回滚/原子性审查

| 检查项 | Delphi | C++ | 兼容 |
|---|---|---|---|
| 双方确认条件 | DealCret.BoDealSelect = TRUE | session->first.accepted && second.accepted | 是 |
| 1秒稳定期 | GetTickCount - DealItemChangeTime >= 1000 | kLegacyTradeStableMs = 1000 | 是 |
| 不稳定处理 | BrokeDeal(整体取消) | cancel_trade_for(整体取消) | 是 |
| 提交前背包空间 | MAXBAGITEM - Itemlist.Count >= 对方物品数 | can_receive_trade_items | 是 |
| 提交前金币上限 | AvailableGold - Gold >= 对方金币 | gold + received_gold <= kLegacyBagGold | 是 |
| 提交前物品存在 | 隐含(物品在 DealList 中) | offered_items_still_exist 检查 | 是(C++更强) |
| 提交前距离检查 | **无**(只在修改金币时检查) | in_interaction_range | 是(C++更强) |
| 提交前死亡检查 | **无**(依赖 Run 循环异步检测) | 显式 is_dead() | 是(C++更强) |
| 提交失败整体回滚 | BrokeDeal(但此前已操作的数据不回滚) | cancel_trade_for + rollback_added | 是(C++更强) |
| 防物品复制 | 物理移动指针, 无复制路径 | 物理 remove→add, 带 rollback | 是 |
| 防金币复制 | Gold 直接加减 | add_gold/spend_gold | 是 |
| 防物品丢失 | ResetDeal 整体移回 | cancel_trade_for 逐个 add_bag_item | 是 |
| 断线回滚 | ReadySave→BrokeDeal | 正常 logout 路径会调用 cancel_trade_for；背包无法完整退还时仍需 PR-2 证明 | **待复核(P0候选)** |
| 死亡回滚 | Ghost→DealCret=nil→Run→BrokeDeal | settle_player_death→cancel_trade_for | 是 |
| 切图回滚 | Run 循环 GetFrontCret 变化→BrokeDeal | cancel_trade_for 在 movement.hpp | 是 |

---

## 10. 协议与消息顺序兼容性审查

### 10.1 SM_* 消息 ID 对照

| 值 | Delphi 名 | C++ 名 | 一致 |
|---|---|---|---|
| 673 | SM_DEALMENU | kSmDealMenu | 是 |
| 674 | SM_DEALTRY_FAIL | kSmDealTryFail | 是 |
| 675 | SM_DEALADDITEM_OK | kSmDealAddItemOk | 是 |
| 676 | SM_DEALADDITEM_FAIL | kSmDealAddItemFail | 是 |
| 677 | SM_DEALDELITEM_OK | kSmDealDelItemOk | 是 |
| 678 | SM_DEALDELITEM_FAIL | kSmDealDelItemFail | 是 |
| 681 | SM_DEALCANCEL | kSmDealCancel | 是 |
| 682 | SM_DEALREMOTEADDITEM | kSmDealRemoteAddItem | 是 |
| 683 | SM_DEALREMOTEDELITEM | kSmDealRemoteDelItem | 是 |
| 684 | SM_DEALCHGGOLD_OK | kSmDealChangeGoldOk | 是 |
| 685 | SM_DEALCHGGOLD_FAIL | kSmDealChangeGoldFail | 是 |
| 686 | SM_DEALREMOTECHGGOLD | kSmDealRemoteChangeGold | 是 |
| 687 | SM_DEALSUCCESS | kSmDealSuccess | 是 |

### 10.2 CM_* 消息 ID 对照

| 值 | Delphi 名 | C++ 名 | 一致 |
|---|---|---|---|
| 1025 | CM_DEALTRY | kCmDealTry | 是 |
| 1026 | CM_DEALADDITEM | kCmDealAddItem | 是 |
| 1027 | CM_DEALDELITEM | kCmDealDelItem | 是 |
| 1028 | CM_DEALCANCEL | kCmDealCancel | 是 |
| 1029 | CM_DEALCHGGOLD | kCmDealChangeGold | 是 |
| 1030 | CM_DEALEND | kCmDealEnd | 是 |

**所有 19 个协议消息 ID 完全一致。**

### 10.3 消息下发顺序分析

**交易成功时 (Delphi)**:
```
1. 将己方 DealList 物品加入对方背包: AddItem 触发 WeightChanged
2. 对方: SendAddItem 发送 SM_ADDITEM
3. 若己方 DealGold > 0: 对方 GoldChanged
4. 将对方 DealList 物品加入自己背包: AddItem 触发 WeightChanged
5. 自己: SendAddItem 发送 SM_ADDITEM
6. 若对方 DealGold > 0: 自己 GoldChanged
7. 对方: SM_DEALSUCCESS
8. 自己: SM_DEALSUCCESS
```

**交易成功时 (C++)**:
```
1. 转移物品: 逐个 add_bag_item
2. 转移金币: add_gold
3. 双方: make_add_item_packet (背包物品添加)
4. 双方: make_gold_changed_packet (金币变化)
5. 双方: make_weight_changed_packet (负重变化)
6. queue_save_character (双方)
7. second: kSmDealSuccess
8. first: kSmDealSuccess
9. 双方: "Trade completed." system notice
```

**关键差异**:
- Delphi 静态源码确认: `SendAddItem`、`GoldChanged`、`WeightChanged` 均可发生在 `SM_DEALSUCCESS` 之前。
- C++ 当前也在 `SM_DEALSUCCESS` 之前发送 `SM_ADDITEM`、`SM_GOLDCHANGED`、`SM_WEIGHTCHANGED`，PR-1 golden 固化这一 before-success 边界。
- C++ 当前提交时会对双方发送 `SM_GOLDCHANGED`；Delphi 仅对本次提交中实际收到对方 `DealGold` 的一侧调用 `GoldChanged`，出金币方在修改交易金币时已通过 `SM_DEALCHGGOLD_OK` 获得扣减后的金币值。该差异不在 PR-1 修复。

### 10.4 legacy Delphi 客户端与 Modern client_v1 路径

C++ 使用两套消息路径:

1. **Legacy SM_* 消息** → gateway 解释后转为 `client_v1::TradeState` → 发送给 ModernClient
2. **legacy Delphi 客户端** 直接接收原始 legacy SM_* 封包

Modern client_v1 不是 legacy Delphi 客户端。gateway 的 `TradeState` 路径只能证明 ModernClient 行为，不能替代 legacy Delphi 客户端动态兼容验证。PR-1 只做 `ObjBase.pas` 与 `ClMain.pas` 的静态源码审查，不运行 Delphi 客户端。

---

## 11. 已确认兼容点

1. **协议消息 ID**: 所有 13 个 SM_DEAL* 和 6 个 CM_DEAL* 消息 ID 完全一致 (Delphi: Grobal2.pas:968-982,1130-1135; C++: legacy_types.hpp:112-124,166-171)

2. **金币 Delta 模型**: 修改金币时先退还旧 deal_gold 再扣除新金额 (Delphi: ObjBase.pas:14171; C++: map_actor_mail.hpp:1483-1487)

3. **物品物理移除**: 放入交易栏时从背包物理移除 (Delphi: ItemList.Delete+DealList.Add; C++: remove_bag_item)

4. **1 秒稳定期**: 确认交易前双方最后修改时间必须超过 1000ms (Delphi: ObjBase.pas:14195; C++: map_actor_mail.hpp:1516-1522)

5. **交易中禁止 NPC 交互**: 双方都阻止 (Delphi: ObjBase.pas:13514,13798; C++: map_actor_mail.hpp:14-20)

6. **交易中禁止拾取物品**: (Delphi: ObjBase.pas:6864; C++: map_actor_mail.hpp:1071)

7. **提交前背包空间检查**: (Delphi: ObjBase.pas:14204,14207; C++: map_actor.cpp:2335-2368)

8. **金币上限 50000000**: (Delphi: ObjBase.pas:17 BAGGOLD=50000000; C++: legacy_map_environment.hpp:16 kLegacyBagGold=50000000)

9. **死亡取消交易**: (Delphi: ObjBase.pas:7843-7845+11829; C++: map_actor.cpp:3409)

10. **最大交易物品数 10**: (Delphi: ObjBase.pas:45 MAXDEALITEM=10; C++: 无硬限制, 依靠背包空间)

---

## 12. 已确认偏移点

### 偏移 #1 [P1] 修改交易内容清除对方确认标志

- **Delphi**: 如果交易对方已按确认 (BoDealSelect=TRUE), 己方无法修改物品/金币。对方确认状态**不被清除**, 己方无法操作。(ObjBase.pas:14110,14143,14169)
- **C++**: 修改物品/金币会将**双方** accepted 设为 false, 对方需要重新确认。(map_actor_mail.hpp:1396-1397,1448-1451,1490-1493)
- **影响**: 用户体验不同。Delphi 行为是"对方已确认则你不能再改"; C++ 行为是"你改了,双方都得重新确认"。
- **风险**: 可能被利用进行"确认欺诈"——A 确认后 B 快速修改物品, A 不知情下仍然点了确认。
- **是否必须修复**: 是

### 偏移 #2 [P1] 移动即取消交易

- **Delphi**: 每 Run 帧检查 `GetFrontCret <> DealCret`——仅当玩家转身或对方离开前方格子时才取消。(ObjBase.pas:11827-11831)
- **C++**: 每次 step/walk/run 移动都立即调用 `cancel_trade_for`。(map_actor_movement.hpp:166,190,282,303)
- **影响**: C++ 中玩家完全无法在交易中移动。Delphi 中可以微调位置(只要不转身)。
- **是否必须修复**: 是
- **推荐最小修复**: 仅当移动导致 `!in_interaction_range` 或 `!mutually_facing` 时才取消。

### 偏移 #3 [P0候选] 断线/取消交易边界回滚不完备

- **Delphi**: ReadySave→BrokeDeal 完整清理+回滚。(ObjBase.pas:11770-11773)
- **C++**: 正常 disconnect 会进入 world logout 并调用 `cancel_trade_for`，不是仅清 gateway。但 `cancel_trade_for` 在背包无法完整退还时会保留 offer/session，若随后 actor 被移除或保存，仍存在物品卡死或丢失风险。
- **影响**: 风险点应从“disconnect 完全不清 world session”修正为“world cancel 未证明在所有 actor 移除/背包满路径上可完成回滚”。
- **风险**: 涉及物品/金币安全，应作为 PR-2 的 P0 阻塞项。
- **是否必须修复**: 是，但不在 PR-1 修复。

### 偏移 #4 [P2] 缺失 BoExchangeAvailable 检查

- **Delphi**: 玩家可切换 BoExchangeAvailable。关闭时他人无法发起交易请求。(ObjBase.pas:13968)
- **C++**: 无此检查, 任何在线玩家都可以被发起交易。
- **是否必须修复**: 建议修复

### 偏移 #5 [P2] 修改金币时缺失距离检查

- **Delphi**: ServerGetDealChangeGold 检查 `GetFrontCret = DealCret`。(ObjBase.pas:14168)
- **C++**: trade_set_gold handler 仅检查 session 存在, 不检查距离。(map_actor_mail.hpp:1469)
- **是否必须修复**: 建议修复

### 偏移 #6 [P3] 缺失台湾活动物品检查

- **Delphi**: ServerGetDealAddItem 中检查 `pstd.StdMode <> TAIWANEVENTITEM`。(ObjBase.pas:14114)
- **C++**: 无等效检查。
- **是否必须修复**: 按需修复

### 偏移 #7 [P3] 消息顺序差异

- **Delphi**: 静态源码显示 `AddItem/SendAddItem/GoldChanged/WeightChanged` 发生在 `SM_DEALSUCCESS` 前。
- **C++**: 当前同样在 `kSmDealSuccess` 前发送 `SM_ADDITEM/SM_GOLDCHANGED/SM_WEIGHTCHANGED`，但具体子顺序和双方 `SM_GOLDCHANGED` 覆盖范围与 Delphi 不完全相同。
- **是否必须修复**: PR-1 不修复，只用 golden 固化当前 C++ 顺序并标注 Delphi 静态依据；真实 legacy UI 影响需后续动态验证。

---

## 13. 无法确认点

| 问题 | Delphi 需补充 | C++ 需补充 | 建议 |
|---|---|---|---|
| 旧客户端(非 v1)交易路径是否工作 | N/A | legacy protocol codec 交易消息处理 | 测试 legacy 客户端直接连接 C++ 服务器 |
| 高负载下交易消息顺序 | N/A | 需要并发压力测试 | 压测多个交易对同时操作 |
| 客户端交易窗口对消息顺序的依赖 | 已做 ClMain.pas 静态分析；仍缺动态 UI 运行验证 | N/A | 后续 legacy Delphi 客户端动态验证 |
| 交易中物品丢弃拦截(deal item drop) | DealItemChangeTime > 3000 丢弃逻辑 (ObjBase.pas:6796) | C++ 中无等效的"3秒后可丢弃交易中物品"逻辑 | 需要确认该 Delphi 行为是否为 bug |
| C++ TradeSession 内存泄漏 | N/A | 断线/world actor 销毁时是否清理 | 添加 session 超时+lease 机制 |

---

## 14. 修复优先级

### P0 (必须立即修复——物品/金币安全)

**偏移 #3**: 断线/取消交易边界回滚不完备 → 背包满、actor 移除等场景可能导致物品卡死或丢失

> 正常 disconnect 会进入 world logout；P0 风险不应表述为“world 完全不清理”，而应聚焦 `cancel_trade_for` 无法完整退还时的剩余 offer/session 处理。

### P1 (高风险——影响交易状态机和用户体验)

1. **偏移 #1**: 修改交易内容清除对方确认标志 → 行为语义不同
2. **偏移 #2**: 移动即取消交易 → 行为更严格, 与 Delphi/旧客户端不兼容

### P2 (中风险——影响边界行为和兼容性)

3. **偏移 #4**: 缺失 BoExchangeAvailable 检查
4. **偏移 #5**: 修改金币时缺失距离检查

### P3 (低风险——可记录, 按需修复)

5. **偏移 #6**: 台湾活动物品检查
6. **偏移 #7**: 消息顺序细节差异（PR-1 仅固化 golden）

---

## 15. 建议增加的兼容性测试

现有测试文件 `client_v1_trade_group_guild_smoke.cpp:330-423` 已覆盖:

- 非面对时交易失败
- 交易打开/取消/重试
- 添加物品(含 InventoryRemove 验证)
- 设置金币(含 SelfAbility 验证)
- 确认+提交(含 InventoryAdd + SelfAbility + TradeState visibility)

**需要补充的测试**:

```
1. 交易请求-距离不足失败: 两个玩家距离>1 格子, 期望 SM_DEALTRY_FAIL
2. 对方死亡交易失败: B 死亡后 A 发起交易, 期望失败
3. 双方同时发起交易: A→B 和 B→A 同时, 期望只有一个成功
4. 重复请求: A 已向 B 发起后再次发起, 期望 fail
5. 修改物品清除锁定: A 确认后 B 修改物品, 期望 A 的确认被正确处理
6. 背包满交易失败: A 背包满, B 放入物品, 双方确认, 期望整体失败回滚
7. 金币不足失败: A 设置金币 100000000(>持有), 期望裁切到持有值
8. 交易中移动: A 在交易窗口中移动一步, 验证 Delphi 兼容行为
9. 交易中断线: A 断线, 验证 B 收到取消+物品/金币正确回滚到 A
10. 交易中死亡: A 在交易窗口打开时被怪物杀死, 验证交易取消+回滚
11. 绑定物品: 放入不可交易物品, 期望 fail
12. 金币溢出: 双方金币和 > 50000000, 期望交易失败
13. 交易完成 golden snapshot: 基于 Delphi 静态源码审查固化 ModernServer 消息序列, 不要求 Delphi 客户端动态 trace
14. 防复制 fuzz test: 随机化操作序列(添加/删除/修改金币/确认/取消/断线), 检查总物品数+总金币数守恒
15. 防丢失 fuzz test: 同上, 检查无物品/金币丢失
16. legacy Delphi 客户端动态 smoke test: 后续独立阶段执行, 不属于 PR-1
17. 并发交易: 10 对玩家同时交易, 检查数据一致性
```

---

## 16. 最小修复建议

### 修复 1: 断线/actor 移除时保证 world TradeSession 完整回滚 (偏移 #3)

**文件**: `ModernServer/src/world/map_actor.cpp`

正常 disconnect 已会进入 world logout 并调用 `cancel_trade_for`。后续 PR-2 应修复的是 world 层回滚不变量: actor 删除或保存前, trade offer 中的物品/金币必须完整归还或进入可恢复状态; `cancel_trade_for` 在背包满无法完整退还时不能留下不可找回的 session。

```cpp
// PR-2 方向:
// player_disconnect / actor_remove / leave_world 统一走 world-level cancel_trade_for(actor_id)
// cancel_trade_for 必须幂等, 并处理无法完整退还 offer items 的持久化/恢复边界
```

### 修复 2: 修改交易内容不清除对方确认 (偏移 #1)

**文件**: `ModernServer/src/world/map_actor_mail.hpp`

在 `trade_add_item` (line 1375)、`trade_remove_item` (line 1424)、`trade_set_gold` (line 1473-1476) 中:

```cpp
// 将当前检查:
if (offer == nullptr || peer_offer == nullptr || offer->accepted) {
// 改为:
if (offer == nullptr || peer_offer == nullptr || peer_offer->accepted) {
    // 如果对方已确认, 阻止修改
}

// 删除以下两行(在 3 个 handler 中均存在):
// offer->accepted = false;
// peer_offer->accepted = false;
```

### 修复 3: 移动取消交易改为仅距离/方向变化时 (偏移 #2)

**文件**: `ModernServer/src/world/map_actor_movement.hpp`

在 movement handler 中, 将 `cancel_trade_for` 移到移动之后, 改为条件检查:

```cpp
// 移动后添加:
if (auto* session = trade_session_for(player.id()); session != nullptr) {
    auto* peer = find_player(session->first_actor_id == player.id()
                             ? session->second_actor_id : session->first_actor_id);
    if (peer == nullptr || !in_interaction_range(player, *peer) ||
        !mutually_facing(player, *peer)) {
        cancel_trade_for(player.id(), dispatch, true);
    }
}
```

### 修复 4: 添加 BoExchangeAvailable 检查 (偏移 #4)

**文件**: `ModernServer/src/world/map_actor_mail.hpp` (trade_try handler)

在 trade_try handler 中添加:

```cpp
if (!target->character().allow_trade) {
    queue_packet(dispatch, requester->session_id(),
                 make_deal_simple_packet(requester->session_id(), kSmDealTryFail));
    break;
}
```

### 修复 5: 添加修改金币时距离检查 (偏移 #5)

**文件**: `ModernServer/src/world/map_actor_mail.hpp` (trade_set_gold handler)

在 trade_set_gold handler 中添加 `in_interaction_range` 检查。

---

## 17. 最终结论

**当前 C++ 交易系统尚未达到 Delphi 完整行为兼容。**

核心交易流程(协议 ID、物品/金币交换、常规提交 rollback、1 秒稳定期)基本兼容。未发现直接物品复制或金币复制路径，但取消/断线边界的物品回收仍需 PR-2 证明和修复。

但存在必须后续修复或验证的高风险偏移:

1. **断线/取消交易边界回滚不完备** → 背包满、actor 移除等场景可能导致物品/金币卡死或丢失
2. **修改交易内容清除对方确认标志** → 与 Delphi 的"阻止修改"行为不同, 可能被利用
3. **移动即取消交易** → 比 Delphi 严格得多, 玩家无法在交易中做任何位置微调

**PR-1 结论**: 本阶段只固化 Delphi/C++ 静态行为基线和 ModernServer golden，不运行 Delphi 客户端，不修 accepted、断线、移动、BoExchangeAvailable、TAIWANEVENTITEM、MAXDEALITEM 等业务偏移。

**可以直接继续开发的前提**: 下一步优先进入 PR-2，修复 world 层断线/取消交易回滚不变量，因为它涉及物品安全性。其他 P1/P2 偏移可以在后续迭代中修复。
