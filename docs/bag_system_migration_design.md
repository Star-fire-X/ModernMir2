# 传奇 Mir2 Delphi 背包系统迁移到 C++ — 完整设计方案

> PR-1 update: `docs/legacy_bag_protocol_findings.md` supersedes this document's
> ABI and bag protocol assumptions. In particular, the confirmed PR-1 target is
> `TStdItem=76` and `TClientItem=84` under Delphi `$A8`, and listed legacy item
> commands use `MakeIndex + item name` rather than a wire-level `bagindex`.

> 状态：基于 2026-05-15 代码库审查
> 核心原则：以 Delphi 行为兼容为第一优先级，不做现代 MMO 背包系统设计
> 审查范围：Delphi ObjBase.pas / Grobal2.pas / itmunit.pas / M2Share.pas + C++ ModernServer + ModernClient

---

## 1. 结论摘要

当前 C++ 项目已经实现了基础的物品数据结构（`LegacyUserItem` 40 字节，`LegacyStdItem` 69 字节，`ItemConfig`）、物品规则库（`legacy_item_rules.cpp`）、背包数组（`CharacterRecord::bag_items[46]` fixed array）、装备数组（`equipped_items[13]`）、仓库数组（`storage_items[50]`）、MakeIndex 分配器、地面物品系统、交易/商店/仓库/NPC 脚本接口，以及大量相关 smoke 测试。

**根本性差异已引入**：Delphi 背包使用 `TList`（动态链表，删除时压缩，指针引用），C++ 使用 `std::array` 固定数组（空槽标记 index==0，slot 稳定）。这意味着**格子索引语义与 Delphi 完全不同**——Delphi 的 `bagindex` 是 TList 中的位置（删除后变化），C++ 的 slot 是数组固定位置（永不变化）。

**关键核对项**：Delphi 客户端的 `CM_*` 协议发送的是 `MakeIndex`（物品唯一 ID）还是 `bagindex`（TList 位置），这对 C++ 实现至关重要。如果是 bagindex，则 C++ 必须模拟 Delphi 的 TList 压缩行为或使用映射层。

**差距总结**：物品数据结构 ≈80% 完成，物品规则函数 ≈85% 完成，背包核心操作（add/del/move）≈60% 完成但语义有偏差，物品使用/丢弃/拾取 ≈70% 完成，协议刷新序列 ≈50% 对齐，bag index semantics 是最大的未解决兼容风险，client_v1 物品延迟通知机制可能给新客户端操作优势。

**建议**：分 10 个 PR 逐步迁移，以不破坏现有兼容行为为前提，每个 PR 有明确的验收标准。PR1 先做 Delphi 审查澄清 bagindex 语义。

---

## 2. 三类边界清单

### A. 绝对不能改

| # | 内容 | Delphi 证据 | 如果改了会破坏什么 | C++ 推荐实现方式 | 验证方法 |
|---|------|-----------|-------------------|-----------------|---------|
| A1 | 背包最大格子数 MAXBAGITEM = 46 | `ObjBase.pas` 多处检查 `ItemList.Count < MAXBAGITEM`，Grobal2 常量 | 旧客户端 UI 背包窗口 46 格布局，更多/更少格子导致 UI 错位、物品不可见、服务端拒绝添加 | `kMaxBagItems = 46` 已定义，不得修改 | 常量断言 + UI smoke |
| A2 | 背包格子显示顺序＝服务端数组顺序 | Delphi `SendBagItems` 从 `ItemList[0]` 到 `ItemList[Count-1]` 顺序发送 | 物品在背包 UI 中的排列顺序改变，玩家操作习惯被破坏 | 固定数组按 slot 0→45 顺序发送 | Golden trace + UI image diff |
| A3 | 物品唯一 ID (MakeIndex) 语义 | `Grobal2.pas:364` `MakeIndex: integer`，`ObjBase.pas:6496` 按 MakeIndex 删除物品 | 装备比较、交易校验、仓库存取使用 MakeIndex 定位物品，语义改变导致操作错误物品、复制/丢失 | `MakeIndexAllocator` 已实现，从 `kLegacyRuntimeFloor=200000` 递增分配 | 单元测试 + 经济一致性测试 |
| A4 | 物品实例字段 Layout (40 bytes) | `Grobal2.pas:363-380` `TUserItem = packed record`，offset 确认 | 存档不兼容、旧客户端解析错误、网络包字段错位 | `LegacyUserItem` packed(1) 已实现，static_assert sizeof==40 | 序列化 golden test |
| A5 | StdItem 模板字段语义 | `Grobal2.pas:317-359` TStdItem 69字节，字段 DC/MC/SC 为 MakeWord 封装的 min/max | 物品属性读取错误、极品识别错误、穿戴判定错误 | `LegacyStdItem` packed 已实现，static_assert sizeof==69 | 字段级 golden test |
| A6 | Desc[0..13] 极品属性数组 | `Grobal2.pas:368-376` desc[0]~desc[7]=升级属性, desc[10]=鉴定标记, desc[9]=保留/诅咒等 | 物品属性全错，极品装备丢失价值 | `desc[14]` 已实现，`legacy_upgraded_item_config()` 已处理升级叠加 | 物品属性 golden test |
| A7 | Dura/DuraMax 持久字段语义 | `Grobal2.pas:366-367` Dura:word, DuraMax:word | 装备破损判定错误、修理价格错误、商店卖价错误 | 已实现，`item_dura_max()` 辅助 | Smoke test |
| A8 | 数量型物品叠加规则 | Delphi 无显式 `count` 字段，数量型物品（药品、卷轴等）通过多个独立 UserItem 实例表示 | 如果 C++ 引入 count 字段，叠加行为完全不同：占用格子数、使用扣除、商店买卖全部改变 | **待源码核对**：确认 Delphi 是否有叠加机制 | Delphi 源码审查 |
| A9 | 不可叠加物品的独立实例 | 装备类物品每个都是独立 `TUserItem`（独立 MakeIndex、独立持久、独立属性） | 装备可叠加会导致极品装备复制 | `LegacyUserItem` 每个实例独立存储 | 单元测试 |
| A10 | 负重规则 | `ObjBase.pas:4058 CalcBagWeight`（遍历 ItemList 累加 StdItem.Weight），`ObjBase.pas:6417 IsAddWeightAvailable`（总重≤MaxWeight） | 玩家可携带超过负重上限的物品，经济失衡 | `calc_bag_weight()` 逻辑需对齐 | 负重 smoke |
| A11 | 穿戴负重 / 腕力 | `ObjBase.pas:6565-6574` 武器/右手需 Weight≤MaxHandWeight，其他需 Weight+CalcWearWeightEx≤MaxWearWeight | 低等级战士穿高负重装备 | `legacy_can_take_on_item()` 已实现 hand/wear weight 检查 | 装备 smoke |
| A12 | 背包满时的失败行为 | `ObjBase.pas:6483` `if ItemList.Count < MAXBAGITEM` 才添加，否则返回 False | 物品凭空消失或服务端崩溃 | `add_bag_item()` 需在满时返回 false | 单元测试 |
| A13 | 使用物品后的扣除顺序 | **待源码核对**：Delphi `EatItem` 何时扣除物品、何时发送刷新 | 药品使用后数量不对、背包刷新与 HP 刷新顺序错 | C++ 需对齐 Delphi 消耗→刷新顺序 | Golden trace |
| A14 | 丢弃物品的地图生成规则 | `ObjBase.pas:6712 GetDropPosition` → `PEnvir.AddToMap(OS_ITEMOBJECT)` → `SendRefMsg(RM_ITEMSHOW)` | 丢弃物品位置不对、无法拾取、物品丢失 | `DropItemDown` 已在 map_actor 实现 | 丢弃/拾取 smoke |
| A15 | 拾取物品的校验链 | `ObjBase.pas` 拾取前检查距离/可见性/归属/背包空间/负重 | 远程拾取、穿透拾取、满包拾取导致物品丢失 | `handle_pickup()` 需完整校验 | Smoke + fuzz |
| A16 | 穿戴/脱下装备时的物品流转 | `ObjBase.pas:698-699 ServerGetTakeOnItem/ServerGetTakeOffItem`，从背包删除→写入装备栏 | 物品复制（背包和装备同时存在）或丢失 | `equip_item()/remove_equipped_item()` 已实现 | 装备 smoke |
| A17 | CM_*/SM_* 消息 ID 数值 | `Grobal2.pas` 常量 + `legacy_types.hpp` 已对齐 | 旧客户端无法识别协议 | 常量已对齐，不得修改 | 协议 smoke |
| A18 | legacy `#...!` 外部协议字段顺序 | `TDefaultMessage` 字段顺序：Recog/Ident/Param/Tag/Series | 网关解包失败、连接断开 | `LegacyPacketHeader` + `LegacyGameCodec` 已实现 | 协议 golden test |
| A19 | 服务端权威判定 | Delphi 所有物品生成/删除/属性/价格/数量/坐标由服务端决定 | 客户端作弊 | C++ 已保持服务端权威 | Fuzz test |
| A20 | 背包操作 FIFO 顺序 | `ObjBase.pas` GetMsg while loop 按消息到达顺序处理 | 同帧操作乱序导致物品状态不一致 | `legacy_inbox_` 按 sequence 顺序处理 | Trace diff |

### B. 应该保留 legacy 行为，但可以现代封装

| # | 内容 | Delphi 证据 | C++ 封装方式 | 验证方法 |
|---|------|-----------|------------|---------|
| B1 | `TUserItem` 等价物 `LegacyUserItem` | `Grobal2.pas:363` packed record 40 bytes | `struct LegacyUserItem` packed(1) 已实现，size/offset static_assert | 序列化 golden |
| B2 | `TStdItem` 等价物 `LegacyStdItem` | `Grobal2.pas:317` record 69 bytes | `struct LegacyStdItem` packed(1) 已实现 | 序列化 golden |
| B3 | 物品模板现代版 `ItemConfig` | `Grobal2.pas TStdItem` | 非 packed struct，用于业务逻辑，需转换为 LegacyStdItem 发送客户端 | 字段完整性 |
| B4 | 背包容器 `bag_items[46]` | `ObjBase.pas ItemList: TList` | `std::array<LegacyUserItem, 46>` 固定数组 + `is_empty()` (index==0) | 行为对比 |
| B5 | 物品查找 `FindItemName` / `bag_item()` | `ObjBase.pas:6423` 按 name 匹配遍历 | `bag_item(make_index, expected_name)` 已实现，双重匹配 | 查找 smoke |
| B6 | 物品添加 `AddItem` | `ObjBase.pas:6480` 不检查重量，只检查 count<MAXBAGITEM | `add_bag_item()` 需对齐：不在此处检查负重 | 单元测试 |
| B7 | 物品删除 `DelItem` | `ObjBase.pas:6490` 按 MakeIndex+name 双重匹配，Dispose+Delete(压缩) | `remove_bag_item(make_index, name)` 已实现，但设 index=0 而非压缩 | 兼容审查 |
| B8 | 物品叠加逻辑 | **待源码核对**：Delphi 是否有叠加或有独立 count | 若 Delphi 无叠加，C++ 也不应有；若有，需在 `add_bag_item()` 实现 | Delphi 审查 |
| B9 | 负重计算 `CalcBagWeight` | `ObjBase.pas:4058` | `calc_bag_weight()` 遍历 bag_items，使用 item_configs_ 查 weight | 负重 smoke |
| B10 | 背包满检查 `CanAddItem` | `ObjBase.pas:6472` | `has_free_bag_slot()` / `can_add_bag_item()` 已实现 | 单元测试 |
| B11 | 物品使用 `EatItem` | `ObjBase.pas:616` | `apply_consumable()` 已实现 HP/MP 效果，但使用前校验和使用后扣除顺序需对齐 | Golden trace |
| B12 | 物品锁定 / 交易 reservation | Delphi 交易物品放入 `DealList`，协议锁定 | `TradeSession` + `TradeOffer` 已实现，物品引用副本 | 交易 smoke |
| B13 | 背包事务 / rollback | Delphi 无显式事务，失败路径各自处理 | `InventoryTransaction` 现代封装，但失败时必须匹配 Delphi 行为 | 经济一致性测试 |
| B14 | 背包协议适配 | `SendBagItems` / `SendAddItem` / `SendDelItem` | `map_actor_packets.hpp` 中的 make_*_packet 函数 | Protocol smoke |
| B15 | 物品 Tooltip 数据生成 | Delphi 客户端本地从 S (TStdItem) 字段计算 | `LegacyClientItem` = `LegacyStdItem` + make_index/dura/dura_max 已定义 | UI smoke |
| B16 | 背包存档读写 | `TSaveRcd.hum: TUserHuman` 含完整角色数据 | `CharacterRecord` + `PersistRequest` 异步持久化 | 存档兼容测试 |

### C. 可以现代化

| # | 内容 | 现代化方式 | 不变证明 |
|---|------|---------|---------|
| C1 | C++ 类结构和 ownership | `Player` 持有 `CharacterRecord`，`MapActor` 管理 ground items | public 接口行为与 Delphi TUserHuman 一致 |
| C2 | 类型安全的 ItemId/ItemInstanceId/BagSlot | `enum class` 或 strong typedef | wire 值不变 |
| C3 | RAII 管理 `InventoryTransaction` | 构造时快照，析构时自动 rollback（若未 commit） | 不影响 Delphi 失败路径 |
| C4 | `ItemReservation` 锁定机制 | 现代 RAII lock，超时自动释放 | lock 语义与 Delphi DealList 一致 |
| C5 | 物品模板缓存 `item_configs_` | `std::unordered_map<int32_t, ItemConfig>` | 查询速度不影响行为 |
| C6 | 序列化适配层 | `CharacterRecord` ↔ `LegacyUserItem[]` ↔ 数据库 BLOB/JSON | 序列化结果与 Delphi 存档逐字节一致 |
| C7 | 日志/trace/metrics | `LegacyRuntimeTrace` 已实现 | 仅附加，不影响任何路径 |
| C8 | 背包调试工具 | `snapshot()` 可导出当前背包状态 | 仅读取 |
| C9 | 物品配置校验 | 启动时校验 `item_configs_` 完整性 | 仅校验 |
| C10 | golden trace / fuzz / smoke 测试 | GTest 参数化 fixture | 对比 Delphi 期望输出 |
| C11 | 内部 typed event | `ActorMail` 替代裸消息 | mail 字段与 Delphi 消息字段一致 |

---

## 3. Delphi 原版背包系统审查结果

### 3.1 Delphi 源码文件清单

| 文件 | 核心职责 | 关键结构/函数 |
|------|---------|-------------|
| `Source/Common/Grobal2.pas` | 全局类型定义 | `TUserItem` (40 bytes), `TStdItem` (69 bytes), `TAbility`, `TClientItem`, `TUserStateInfo`, `TDropItem`, `TMsgHeader`, `TDefaultMessage`, `#!` 协议常量 |
| `Source/M2Server/ObjBase.pas` | 生物基类 (TCreature→TUserHuman) | `AddItem`, `DelItem`, `DelItemIndex`, `CanAddItem`, `IsEnoughBag`, `FindItemName`, `CalcBagWeight`, `CanTakeOn`, `DropItemDown`, `UserDropItem`, `EatItem`, `ScatterBagItems`, `DropUseItems`, `TakeCretBagItems`, `SendBagItems`, `SendAddItem`, `SendDelItem`, `ApplyItemParameters`, `ApplyItemParametersEx`, `WeightChanged`, `GoldChanged` |
| `Source/M2Server/itmunit.pas` | 物品升级/鉴定单元 | `GetUpgrade`, `UpgradeRandomWeapon`, `UpgradeRandomDress`, `UpgradeRandomNecklace`, `UpgradeRandomBarcelet`, `UpgradeRandomRings`, `UpgradeRandomHelmet`, `RandomSetUnknown*`, `GetUpgradeStdItem` |
| `Source/M2Server/Envir.pas` | 地图环境 | `AddToMap`, `DeleteFromMap`, `GetItem`, `FindItem`, 地面物品管理 |
| `Source/M2Server/UsrEngn.pas` | 用户引擎 | `GetStdItem`, `GetStdItemName`, 物品模板查询 |
| `Source/Common/HUtil32.pas` | 工具函数 | `MakeWord`, `MakeLong`, `LoByte`, `HiByte`, `CompareText`, `GetValidStr3` |
| `Source/Common/EDCode.pas` | 加密编解码 | legacy `#...!` 帧编解码 |
| `Source/Client/ClMain.pas` | 客户端主窗体 | 协议分发、SendMsg、ProcMsg |
| `Source/Client/DWinCtl.pas` | UI 控件 | 背包窗口、物品网格、拖拽 |
| `Source/Client/clEvent.pas` | 客户端事件 | 背包事件处理 |

### 3.2 Delphi 背包核心结构确认

**TUserItem (40 bytes packed)**：
```
Offset  Size  Field       Delphi 类型    含义
0       4     MakeIndex   integer        物品唯一实例 ID（运行时分配，>0）
4       2     Index       word           物品模板 ID（StdItem 索引），0=空格
6       2     Dura        word           当前持久
8       2     DuraMax     word           最大持久
10      14    Desc[0..13] byte array     极品属性/附加属性数组
24      1     ColorR      byte           名称颜色 R（极品变色）
25      1     ColorG      byte           名称颜色 G
26      1     ColorB      byte           名称颜色 B
27      13    Prefix[0..12] char array   名称前缀（极品描述）
```

**Desc[0..13] 字段语义**（从 `Grobal2.pas:368-376` + `itmunit.pas` + `ObjBase.pas` 综合）：
```
Desc[0]  武器: DC升级值        衣服/头盔/项链/手镯/戒指: AC升级值
Desc[1]  武器: MC升级值        衣服/头盔/项链/手镯/戒指: MAC升级值
Desc[2]  武器: SC升级值        衣服/头盔/项链/手镯/戒指: DC升级值
Desc[3]  武器: AC升级值(lo)    衣服/头盔/项链/手镯/戒指: MC升级值
Desc[4]  武器: MAC升级值(lo)   衣服/头盔/项链/手镯/戒指: SC升级值
Desc[5]  武器: 准确(HIT)升级值 头盔/项链/手镯/戒指: Need(需求类型:1=DC 2=MC 3=SC)
Desc[6]  武器: 攻击速度(+10=正) 头盔/项链/手镯/戒指: NeedLevel(需求值)
Desc[7]  武器: 特殊属性(1~10)  头盔/项链/手镯/戒指: 不可脱下标记
Desc[8]                      头盔/项链/手镯/戒指: 未鉴定标记(待确认)
Desc[9]  保留
Desc[10] 武器: 鉴定标记
Desc[11] MAC_TYPE (gadget)
Desc[12] MC_TYPE (gadget)
Desc[13] 保留
```

**重要：Delphi 没有数量(count)字段**。数量型物品（药品、卷轴等）在 Delphi 中每个都是独立的 `TUserItem` 实例，通过 `MakeIndex` 区分。这意味着 99 瓶金创药在 Delphi 中是 99 个独立的 TUserItem（占用 99 个 TList 槽位，但在 SendBagItems 时客户端看到的是同一个 Index 的多条记录）。

**待源码核对**：客户端如何显示同 Index 的多个物品？是单独显示还是合并显示数量？

**TStdItem (69 bytes，非 EI 版本)**：
```
Offset  Size  Field        含义
0       15    Name[14]     物品名称（ShortString 格式：length byte + 14 chars）
15      1     StdMode      物品大类（0=药 3=杂物 4=技能书 5/6=武器 10/11=衣服 15=头盔 19/20/21=项链 22/23=戒指 24/26=手镯 25=符咒 30=蜡烛/火把 31=卷轴 52=靴子 53=护身符 54=腰带）
16      1     Shape        物品小类/外观 Shape
17      1     Weight       重量
18      1     AniCount     动画帧数
19      1     SpecialPwr   特殊能力（1~10=武器特殊 / -50~-1=怪物能力加成 / -100~-51=怪物能力减弱）
20      1     ItemDesc     物品标记（bit0=未鉴定 bit1=不可脱下 bit2=永不脱下 bit3=死亡破碎 bit4=永不掉落）
21      2     Looks        外观图索引
23      2     DuraMax      最大持久
25      2     AC           防御（LoByte=min HiByte=max）
27      2     MAC          魔御（LoByte=min HiByte=max）
29      2     DC           攻击（LoByte=min HiByte=max）
31      2     MC           魔法（LoByte=min HiByte=max）
33      2     SC           道术（LoByte=min HiByte=max）
35      1     Need         需求类型（0=等级 1=DC 2=MC 3=SC）
36      1     NeedLevel    需求值
37      4     Price        价格
41      4     Stock        存量
45      1     AtkSpd       攻击速度
46      1     Agility      敏捷
47      1     Accurate     准确
48      1     MgAvoid      魔法回避
49      1     Strong       强度（神圣）
50      1     Undead       不死系
51      4     HpAdd        HP 附加值
55      4     MpAdd        MP 附加值
59      4     ExpAdd       经验附加值
63      1     EffType1     特殊效果类型1
64      1     EffRate1     特殊效果触发率1
65      1     EffValue1    特殊效果值1
66      1     EffType2     特殊效果类型2
67      1     EffRate2     特殊效果触发率2
68      1     EffValue2    特殊效果值2
```

### 3.3 Delphi 背包容器行为

**关键发现：Delphi TList 与 C++ fixed array 的根本差异**

| 维度 | Delphi | C++ 当前 |
|------|--------|---------|
| 容器类型 | `ItemList: TList`（动态指针列表） | `std::array<LegacyUserItem, 46>`（固定数组） |
| 空格表示 | 不存在，TList 中无空槽 | `index == 0` |
| 格子索引 | `bagindex` = TList 位置（0..Count-1） | `slot` = 数组索引（0..45） |
| 删除后 | Dispose + Delete → 后续元素前移，Count-- | 设 index=0，slot 位置不变 |
| 最大容量 | 46 | 46 |
| 添加位置 | 追加到 TList 末尾（`ItemList.Add(pu)`） | **待确认**：找到第一个空 slot 还是追加？ |
| 内存管理 | New/Dispose 手动管理，TUserItem 在堆上 | 值语义，LegacyUserItem 在数组中 |

**这是整个迁移中最大的兼容性风险点**。必须在 PR1 确认：
1. Delphi `CM_*` 协议中，客户端发送的是 `MakeIndex` 还是 `bagindex`？
2. Delphi `SendDelItem` 发送的是 `MakeIndex` 还是 `bagindex`？
3. 客户端如何根据服务端消息定位背包中的物品？（如果是 bagindex，则 C++ 必须模拟 TList 压缩行为）

### 3.4 Delphi 背包核心函数行为

**AddItem (ObjBase.pas:6480)**：
```pascal
function TCreature.AddItem(pu: PTUserItem): Boolean;
// 只在 ItemList.Count < MAXBAGITEM 时追加到末尾
// 不检查重量，不检查叠加
// 调用 WeightChanged
// 返回 TRUE/FALSE
```

**DelItem (ObjBase.pas:6490)**：
```pascal
function TCreature.DelItem(svindex: integer; iname: string): Boolean;
// 遍历 ItemList，匹配 MakeIndex == svindex 且 name == iname
// 找到后 Dispose + Delete(i) → TList 压缩
// 调用 WeightChanged
```

**DelItemIndex (ObjBase.pas:6509)**：
```pascal
function TCreature.DelItemIndex(bagindex: integer): Boolean;
// 按 TList 位置删除：ItemList.Delete(bagindex)
// 仅在 bagindex >= 0 and bagindex < ItemList.Count 时有效
```

**CanAddItem (ObjBase.pas:6472)**：
```pascal
function TCreature.CanAddItem: Boolean;
// 仅检查 ItemList.Count < MAXBAGITEM
// 不检查重量
```

**IsEnoughBag (ObjBase.pas:6391)**：
```pascal
// 同 CanAddItem，仅是不同命名
```

**CalcBagWeight (ObjBase.pas:4058)**：
```pascal
// 遍历 ItemList，查询每个 item.Index 对应的 StdItem.Weight，累加
// 不包含装备重量（装备重量在 CalcWearWeightEx 中计算）
```

**DropItemDown (ObjBase.pas:6670-6735)**：
```pascal
// 1. 创建 TMapItem 地面物品
// 2. GetDropPosition 计算散落位置
// 3. PEnvir.AddToMap(OS_ITEMOBJECT) 添加到地图
// 4. 成功后 SendRefMsg(RM_ITEMSHOW) 通知所有可见客户端
// 5. 失败则 Dispose pmi
// 注意：此函数只处理地面物品生成，不处理背包删除
// 调用者（UserDropItem）先调用 DropItemDown，成功后调用 DelItemIndex
```

**UserDropItem (ObjBase.pas:6787)**：
```pascal
// 1. 校验（交易 CD 等）
// 2. 遍历 ItemList 找到匹配 name 的物品（找第一个匹配的）
// 3. 调用 DropItemDown 生成地面物品
// 4. 成功后调用 DeletePItemAndSend 删除背包物品 + 发送 SM_DELITEM
// 5. 发送系统消息
```

**ScatterBagItems (ObjBase.pas:2391)** / **DropUseItems (ObjBase.pas:2518)**：
```pascal
// 死亡爆物逻辑
// ScatterBagItems: 从 ItemList 随机选择部分物品掉落（爆率与 PK 值/幸运等有关）
// DropUseItems: 从 UseItems（装备栏）按死亡破碎规则掉落装备
// 两个函数都有独立的随机物品选择逻辑
```

### 3.5 Delphi 背包协议消息

**SendBagItems (ObjBase.pas:883)**：
```pascal
// 发送完整背包数据给客户端
// 通过 SM_BAGITEMS (201) 消息
// 包含 ItemList 中每个物品的完整 TClientItem 数据
```

**SendAddItem (ObjBase.pas:879)**：
```pascal
// 发送单个物品添加
// 通过 SM_ADDITEM (200) 消息
// 包含 TClientItem 完整数据
```

**SendDelItem (ObjBase.pas:881)**：
```pascal
// 发送单个物品删除
// 通过 SM_DELITEM (202) 消息
// 包含物品标识信息
```

**SendUpdateItem (ObjBase.pas:880)**：
```pascal
// 发送单个物品更新
// 通过 SM_UPDATEITEM (203) 消息
// 用于持久变化等
```

---

## 4. 当前 C++ 项目差距分析

### 4.1 已实现的背包能力

| 能力 | 实现位置 | 完成度 | 备注 |
|------|---------|--------|------|
| LegacyUserItem (40 bytes) | `protocol/legacy_types.hpp` | 100% | packed, static_assert sizeof==40, offset 验证 |
| LegacyStdItem (69 bytes) | `protocol/legacy_types.hpp` | 100% | packed, static_assert sizeof==69 |
| LegacyAbility | `protocol/legacy_types.hpp` | 100% | 含所有负重/腕力/穿戴负重字段 |
| ItemConfig (现代模板) | `config/models.hpp` | 90% | 字段齐全，缺少部分 Delphi 字段（如 gift 等） |
| bag_items[46] 固定数组 | `core/messages.hpp` CharacterRecord | 100% | 固定数组 + is_empty() |
| equipped_items[13] | `core/messages.hpp` CharacterRecord | 100% | 含 kEquipDress..kEquipCharm 13 槽 |
| storage_items[50] | `core/messages.hpp` CharacterRecord | 100% | 仓库 |
| MakeIndexAllocator | `world/make_index_allocator.hpp/cpp` | 100% | 从 200000 递增 |
| GroundItem 结构 | `world/map_actor.hpp` | 100% | 含所有权/过期/死亡掉落标记 |
| 物品规则 (legacy_item_rules) | `world/legacy_item_rules.cpp` | 85% | 槽位匹配/穿戴判定/消耗品判定/随机升级/升级后属性 |
| 装备穿戴/脱下 | `game_object.hpp` Player | 100% | equip_item/remove_equipped_item |
| 消耗品效果 | `game_object.hpp` Player | 100% | apply_consumable |
| 金币操作 | `game_object.hpp` Player | 100% | add_gold/spend_gold/can_spend_gold |
| has_free_bag_slot | `game_object.hpp` Player | 100% | 扫描 array 找 index==0 |
| can_add_bag_item | `game_object.hpp` Player | 100% | 空格+重量检查 |
| add_bag_item | `game_object.hpp` Player | 100% | **但语义与 Delphi 不同：找空槽 vs 追加末尾** |
| remove_bag_item | `game_object.hpp` Player | 100% | 按 make_index+name 匹配后设 index=0 |
| remove_bag_item_at | `game_object.hpp` Player | 100% | 按 slot 删除 |
| remove_storage_item | `game_object.hpp` Player | 100% | 仓库删除 |
| 装备属性重算 | `game_object.hpp` Player | 100% | refresh_derived_state |
| 交易系统 | `map_actor.hpp` TradeSession/TradeOffer | 100% | 含物品锁定、金币、确认 |
| 商店买卖 | map_actor.cpp | 100% | buy/sell/repair |
| 仓库存取 | map_actor.cpp | 100% | storage store/retrieve |
| NPC 脚本物品操作 | map_actor.cpp | ~80% | TakeItem/GiveItem 基本实现 |
| 背包协议包生成 | `map_actor_packets.hpp` | ~80% | make_std_item, make_client_item 等 |
| client_v1 物品协议 | `shared/protocol/client_v1/protocol.hpp` | 100% | BagSnapshot/InventoryAdd/Update/Remove 等 |
| 测试覆盖 | ModernServer/tests/*item* smoke | ~70% | 有大量测试但未覆盖所有边界 |

### 4.2 TODO / 占位 / 简化实现

| 项 | 当前状态 | 需要补齐 |
|----|---------|---------|
| Delphi 叠加行为 | 未确认 | PR1 审查：确认 Delphi 是否有 count 叠加机制 |
| 背包 index 语义 | 固定数组 slot 但 Delphi 是 TList | PR1 审查：确认客户端协议使用 MakeIndex 还是 bagindex |
| SendBagItems 序列 | 未完整实现 legacy 协议包 | 补齐 legacy 协议中的 SM_BAGITEMS 发送 |
| SendDelItem via MakeIndex | 部分实现 | 确认后补齐 |
| 丢弃物品的 confirm 窗口 | 未实现 | 补齐客户端丢弃确认 UI |
| 拖拽移动物品 | client_v1 支持但 legacy 不支持 | 确认 legacy 是否需要服务端支持 |
| 使用物品后消息顺序 | 未 golden trace 验证 | 补齐 golden trace |
| 死亡爆物完整逻辑 | 框架存在但未 golden 验证 | 补齐 golden trace |
| 装备脱落（背包满时） | 待确认 Delphi 行为 | 审查 Delphi |
| 异步存档竞态 | 当前同步 persist | 如果用异步，需防旧状态覆盖 |
| 背包操作 trace | LegacyRuntimeTrace 框架存在 | 补齐所有背包操作的 trace 点 |
| Fuzz 测试 | 基本框架存在 | 补齐物品 fuzz |

### 4.3 可以复用的代码

- `LegacyUserItem` / `LegacyStdItem` 结构体：完全可用
- `ItemConfig` 现代模板：完全可用
- `legacy_item_rules` 函数：基本可用（除待核对点）
- `Player::add_bag_item()` / `remove_bag_item()`：需要调整语义但接口可用
- `Player::has_free_bag_slot()` / `can_add_bag_item()`：可用
- `MakeIndexAllocator`：完全可用
- `MapActor::GroundItem` + 地面物品管理：基本可用
- `TradeSession` / `TradeOffer`：基本可用
- `map_actor_packets.hpp` 中的包生成函数：基本可用
- 现有 smoke test fixture：可复用框架

### 4.4 可能破坏 Delphi 行为的现代化抽象

| 风险 | 描述 | 影响 | 缓解 |
|------|------|------|------|
| 固定数组 vs TList | bag_slot 语义完全不同 | 客户端背包 UI 物品位置混乱 | PR1 确认协议语义 |
| client_v1 批量消息 | client_v1 一次 flush 多条物品消息，Delphi 逐条接收 | client_v1 客户端可能在操作速度上获得优势 | 确保 server-authoritative 且 FIFO |
| 异步 persist | 如果存档在物品操作后异步完成，可能丢失或复制 | 物品经济安全隐患 | 保持同步 persist 或版本号 |
| 无 TList 压缩 | 删除物品后空格保留在中间，Delphi 删除后空白被压缩 | SendBagItems 发送给旧客户端的格子顺序可能不对 | 如果客户端按格子索引定位，需在发送时压缩空格 |
| ItemConfig vs LegacyStdItem | ItemConfig 只是业务逻辑类型，发送给客户端时必须转换为 LegacyStdItem | 字段不全或转换错误 | 逐字段 golden test |

### 4.5 必须补齐的测试

| 测试类型 | 现有覆盖 | 需要补齐 |
|---------|---------|---------|
| 背包空格扫描 | 间接覆盖 | 独立单元测试 |
| 添加物品满包 | 有 | 补充 fuzz 随机数量 |
| 删除物品 bagindex 语义 | 部分 | 补充 key-based vs slot-based 区别 |
| 负重计算 | 部分 | 补充 Delphi 对比 |
| 使用物品消息顺序 | 无 golden | 补充 golden trace |
| 丢弃/拾取消息顺序 | 部分 smoke | 补充 golden trace |
| 多人同时拾取 | 无 | 补充并发 fuzz |
| 交易物品锁定 | 部分 smoke | 补充边界 |
| 存档兼容 | 有 importer | 补充 round-trip |
| 协议 golden | 有 protocol smoke | 补充 item-specific |
| 经济一致性 | 无 | 全场景覆盖 |

---

## 5. 背包总体架构设计

### 5.1 模块定义

| 模块 | 负责什么 | 不负责什么 | Delphi 对应 | 生命周期 | 线程 | C++ 文件建议 |
|------|---------|-----------|-----------|---------|------|------------|
| `ItemTemplate` (ItemConfig) | 物品模板数据：属性/价格/重量/效果/装备限制 | 物品实例状态（持久/属性/数量） | `TStdItem` | 进程级常驻，服务启动加载 | 只读 | `config/models.hpp` |
| `ItemInstance` (LegacyUserItem) | 物品实例状态：唯一ID/模板ID/持久/极品属性/颜色/前缀 | 物品模板 | `TUserItem` | Player 成员，随角色生命周期 | 玩家逻辑线程 | `protocol/legacy_types.hpp` |
| `InventorySlots` (bag_items array) | 背包容器：格子扫描/添加/删除/查找 | 物品效果/装备判定/负重 | `ItemList: TList` | Player 成员 | 玩家逻辑线程 | `core/messages.hpp` (CharacterRecord) |
| `InventoryService` (MapActor 内嵌) | 背包操作编排：校验/原子操作/协议发送/持久化 | 物品模板/UI/网络 | TUserHuman 方法 | MapActor tick 内调用 | 玩家逻辑线程 | `world/map_actor.cpp` (现有) |
| `InventoryTransaction` | 多物品原子操作：快照/回滚/提交 | 单个物品操作 | 隐式（Delphi 无显式事务） | 栈上临时对象 | 玩家逻辑线程 | 新文件 `world/inventory_transaction.hpp` |
| `ItemReservation` | 物品临时锁定：交易/商店/NPC 脚本期间防并发修改 | 持久锁定 | `DealList` / 隐式协议锁 | 栈上临时对象 | 玩家逻辑线程 | 新文件 `world/item_reservation.hpp` |
| `ItemUseHandler` | 物品使用：药品/卷轴/技能书/宝箱 | 装备穿戴（那是 EquipmentHandler） | `EatItem` / `ReadBook` / `ServerGetEatItem` | MapActor tick 内 | 玩家逻辑线程 | `world/map_actor.cpp` (现有 handle 分支) |
| `ItemDropHandler` | 丢弃：校验/生成地面物品/发送消息 | 死亡爆物（那是 DeathHandler） | `UserDropItem` / `DropItemDown` | MapActor tick 内 | 玩家逻辑线程 | `world/map_actor.cpp` (现有) |
| `ItemPickupHandler` | 拾取：校验/删除地面物品/添加到背包/发送消息 | 自动拾取（如果 Delphi 没有就不实现） | `ServerGetPickUp` | MapActor tick 内 | 玩家逻辑线程 | `world/map_actor.cpp` (现有) |
| `InventoryProtocolAdapter` | 背包协议编码：BagItems/AddItem/DelItem/UpdateItem | 业务逻辑 | `SendBagItems` / `SendAddItem` / `SendDelItem` | 无状态工具函数 | 玩家逻辑线程 | `world/map_actor_packets.hpp` (现有) |
| `InventoryPersistenceAdapter` | 背包存档序列化/反序列化 | 存档时机/事务 | `TSaveRcd.hum` | 持久化时调用 | 持久化线程（只读快照） | `services/persistence_service.cpp` (现有) |
| `InventoryTrace` | 背包操作 trace：操作/结果/物品ID/格子/时间 | 业务逻辑 | 无（新增） | 每次操作生成一条 | 玩家逻辑线程 | 复用 `LegacyRuntimeTrace` |

### 5.2 架构关键决策

**Q1: 背包状态应该挂在 Player 上，还是由独立 InventoryService 管理？**

**答案**：挂在 Player 上（`CharacterRecord::bag_items`），与 Delphi 一致。Delphi 中 `ItemList` 是 `TCreature` 的成员字段，每个玩家有自己的背包。`MapActor` 作为协调器调用 `Player` 方法。不需要独立的 `InventoryService`——这会引入不必要的间接层且改变所有权语义。

**Q2: 物品模板与物品实例是否分离？**

**答案**：已分离。`ItemConfig`（进程级常量）与 `LegacyUserItem`（玩家实例变量）分离，与 Delphi `TStdItem`（服务端单一副本查询）和 `TUserItem`（实例在 ItemList 中）一致。

**Q3: 背包操作是否必须在玩家逻辑阶段完成？**

**答案**：必须。与 Delphi 一致——背包操作在 `legacy_operate_player_running` → handle_mail → handle_xxx 中完成，该阶段在玩家逻辑 tick 内串行执行，确保 FIFO 且无并发。

**Q4: 背包事务是否允许跨 frame？**

**答案**：不允许。Delphi 所有背包操作在单个 GetMsg 处理的 while 循环内完成。C++ 必须保持同 frame 完成，不支持跨 frame 的异步背包事务。

**Q5: 背包是否允许异步修改？**

**答案**：不允许。背包修改必须在玩家逻辑线程内同步完成。网络线程、持久化线程、其他玩家逻辑不得直接修改玩家背包。

**Q6: 背包存档是否允许异步？**

**答案**：当前是同步持久化（`queue_save_player_character` 在 frame-end 发出 `PersistRequest`，persistence_service 异步写磁盘）。注意：`PersistRequest` 携带的是 `CharacterRecord` **快照**（`player.persistent_snapshot()`），所以异步写盘不会影响运行中状态。但必须防版本回退（旧快照覆盖新快照）。

**Q7: client_v1 是否必须降级到 legacy 背包操作 pipeline？**

**答案**：不需要降级。client_v1 使用的 `BagSnapshot`/`InventoryAdd`/`InventoryRemove` 等现代消息是 `client_v1_session` 内从 legacy 包或内部状态生成的。关键是：消息产生的时机和顺序必须与 legacy 一致，不能给 client_v1 客户端操作优势。

**Q8: 如何保证同一玩家背包操作 FIFO？**

**答案**：`Player::legacy_inbox_` 按 `legacy_command_sequence_` (单调递增 sequence) 排序，`legacy_operate_player_running` 逐条取出处理，每个 command 处理完才处理下一个。与 Delphi `GetMsg` while 循环一致。

---

## 6. 物品数据结构设计

### 6.1 物品实例字段表 (LegacyUserItem)

| 字段名 | Delphi 类型 | 含义 | 持久化 | 同步客户端 | C++ 类型 | 绝对不能改 | 待核对点 |
|--------|-----------|------|--------|----------|---------|----------|---------|
| MakeIndex | integer (4 bytes) | 物品唯一实例 ID | 是 | 是 | `std::int32_t` | 是 | 运行时分配起始值确认 |
| Index | word (2 bytes) | 物品模板 ID (StdItem 索引) | 是 | 是 | `std::uint16_t` | 是 | 0=空格 |
| Dura | word (2 bytes) | 当前持久 | 是 | 是 | `std::uint16_t` | 是 | 非装备类物品的 Dura 语义 |
| DuraMax | word (2 bytes) | 最大持久 | 是 | 是 | `std::uint16_t` | 是 | 从 ItemConfig 读取 |
| Desc[0..13] | byte array (14 bytes) | 极品属性数组 | 是 | 是 | `std::array<std::uint8_t, 14>` | 是 | 每个槽位的精确语义（见上文） |
| ColorR/G/B | byte×3 (3 bytes) | 名称颜色 | 是 | 是 | `std::uint8_t` | 是 | 品级颜色规则 |
| Prefix[0..12] | char array (13 bytes) | 名称前缀 | 是 | 是 | `std::array<char, 13>` | 是 | 极品描述文本 |

### 6.2 StdItem 字段映射

完整字段映射见第 3.2 节 `TStdItem` 表。

`ItemConfig`（现代 C++ 模板）是 `LegacyStdItem` 的展开版本，字段语义相同但类型更安全（`std::int32_t` 代替 `std::uint16_t` 等）。发送给客户端时必须转换为 `LegacyStdItem`（通过 `make_std_item()` 完成）。

### 6.3 字段归属

- **来自 StdItem**：`Index`（模板 ID），`DuraMax`（从模板复制），`Name`（通过 `Index` 查询）
- **属于 UserItem 实例**：`MakeIndex`（运行时分配），`Dura`（运行时变化），`Desc[]`（升级/鉴定产生），`ColorR/G/B`（品级决定），`Prefix`（极品描述）
- **运行时生成**：`MakeIndex`（服务端分配，客户端不生成），`Prefix` 字符串（服务端根据 Desc 计算，**待核对**）
- **服务端权威维护**：所有字段均由服务端维护。客户端收到的是副本，不能提交决定。
- **客户端只显示**：客户端显示所有字段但不得修改提交。唯一的"客户端本地操作"是 UI 拖拽排序（如果 Delphi 允许本地排序的话——**待核对**）。

---

## 7. 背包格子与叠加规则设计

### 7.1 背包格子

| 维度 | Delphi 原版 | C++ 推荐实现 | 兼容风险 |
|------|-----------|------------|---------|
| 最大格子数 | 46 (MAXBAGITEM) | `kMaxBagItems = 46` 已对齐 | 无 |
| 起始索引 | TList[0] 是第一个物品 | 数组 slot 0 是第一个格子 | **高**：如果 Delphi 客户端按 bagindex 定位，C++ 的 slot 必须映射到 TList 位置 |
| 空格表示 | TList 中没有空槽（所有元素都是有效物品） | `index == 0` 表示空格 | **高**：如果客户端期望连续排列，C++ 需要发送时压缩 |
| 添加位置 | TList.Add 追加到末尾 | **待 PR1 确认后决定**：追加到第一个空格还是数组末尾 | **高** |
| 删除后压缩 | TList.Delete 删除元素 → 后续元素前移，Count-- | 当前设 index=0（不压缩） | **高** |
| 客户端显示顺序 | TList[0] → TList[Count-1] 顺序 | 按 slot 0→45 顺序发送（跳过空格） | 如果客户端依赖 bagindex 定位，需映射 |

**关键决策**：`bagindex` 语义必须在 PR1 中通过 Delphi 源码审查 + 协议抓包确认。如果 Delphi 客户端使用 `bagindex`（TList 位置）来定位物品（例如 `CM_EAT 1006` 的 `svindex` 参数），则 C++ 必须在以下方案中选择：

**方案 A（推荐）**：在发送给 legacy 客户端的协议包中，将固定数组压缩为连续排列（跳过空格），使客户端的 bagindex 对应压缩后的位置。内部存储保持固定数组不变。

**方案 B**：改为动态列表存储，与 Delphi TList 行为完全一致。

**方案 C**：维护 bagindex → slot 的映射表，对外暴露 bagindex，内部用 slot。

推荐方案 A，因为对内部代码改动最小，只在协议层做适配。

### 7.2 叠加规则

**待源码核对**：Delphi 原版是否有物品数量叠加机制？

从 `TUserItem` 结构来看，**没有单独的 `Count` 字段**。每个数量型物品（药品、卷轴等）在 Delphi 中都是独立的 UserItem 实例。这意味着：
- 99 瓶药水 = 99 个 TUserItem（99 个 MakeIndex）
- 每个占用一个 TList 槽位
- 使用一瓶 = 删除一个 TUserItem 实例

如果 Delphi 确实没有叠加，则 C++ 也不应有。`LegacyUserItem` 当前也没有 count 字段。

**如果 Delphi 有某种叠加机制**（例如通过 `Desc[9]` 或客户端本地合并显示），则需在 PR1 确认后实现。

### 7.3 背包满规则

| 场景 | Delphi 行为 | C++ 推荐 | 兼容要求 |
|------|-----------|---------|---------|
| 无空格 | `ItemList.Count >= 46` → `CanAddItem = FALSE` | `has_free_bag_slot() = false` | 一致 |
| 有空格+负重不足 | `AddItem` 不检查重量，只检查数量 → 添加成功（但可能超重） | **关键差异**：Delphi 的 `AddItem` 不检查重量！重量检查在 `CanTakeOn`（穿戴时）、商店购买前等外部调用。C++ 的 `can_add_bag_item()` 当前检查重量，这可能与 Delphi 行为不同 | **待核对**：哪个 Delphi 入口点调用者负责重量检查 |
| 任务奖励多个物品 | 逐个添加，某个失败后继续尝试后续物品（非原子） | **待核对**：Delphi 任务脚本 GiveItem 是否原子 | Delphi 审查 |
| 商店购买多个物品 | 逐个添加，某个失败时提示但已成功的保留 | **待核对** | Delphi 审查 |

---

## 8. 物品添加 / 删除 / 移动设计

### 8.1 添加物品

**Delphi 原版链路**：
```
调用者生成 TUserItem (New) + 填充字段
→ AddItem(pu)
   → if ItemList.Count < MAXBAGITEM then ItemList.Add(pu) → WeightChanged → TRUE
   → else FALSE
→ (调用者负责) 发送 SendAddItem / SendBagItems
→ (调用者负责) 发送系统消息
```

**C++ 推荐链路**（对齐 Delphi）：
```
生成 LegacyUserItem + 唯一 MakeIndex
→ Player::add_bag_item(item)
   → 扫描第一个空闲 slot (index==0)
   → 写入 item 到该 slot
   → return true/false
→ 调用者负责：queue 背包刷新协议 (AddItem 或 BagItems)
→ 调用者负责：queue 系统消息
→ 调用者负责：标记 dirty → frame-end 持久化
```

**关键对齐点**：
- `add_bag_item` 不检查重量（与 Delphi `AddItem` 一致）
- `add_bag_item` 不发送协议（与 Delphi `AddItem` 一致，由调用者负责）
- `add_bag_item` 不持久化（由 frame-end/autosave 负责）

**添加失败处理**：
- Delphi `AddItem` 返回 FALSE 时，调用者通常 `Dispose(pu)` 释放物品
- C++ 添加失败时，由调用者决定是否销毁物品实例（`LegacyUserItem` 是值类型，不添加即可丢弃）

### 8.2 删除物品

**Delphi 原版链路**：
```
DelItem(svindex, iname)   // 按 MakeIndex+Name 删除
→ 遍历 ItemList 找到 MakeIndex==svindex && name==iname
→ Dispose + Delete(i) → TList 压缩
→ WeightChanged
→ return TRUE

DelItemIndex(bagindex)    // 按 TList 位置删除
→ ItemList.Delete(bagindex) → TList 压缩
→ 无 WeightChanged
→ return TRUE
```

**C++ 推荐链路**：
```
remove_bag_item(make_index, expected_name)
→ 遍历 bag_items 找到 make_index 匹配 + name 匹配
→ 设该 slot 的 index=0 (清空)
→ return std::optional<LegacyUserItem> (被删除的物品)
→ 调用者负责：queue SendDelItem
→ 调用者负责：WeightChanged
```

**关键差异**：
- Delphi 的 `Delete(i)` 会使后续元素前移。C++ 的 slot 清空不会移动。
- 如果 legacy 客户端使用 bagindex 定位物品，需在发送 Delete 消息时传递正确的压缩后 index（方案 A）。

### 8.3 移动物品

**Delphi 行为分析**：
- TList 不支持"移动物品到指定位置"的内置操作（指针列表没有 swap/exchange）
- 客户端背包 UI 的拖拽移动可能是**纯客户端本地行为**（不发送协议到服务端）
- 服务端在下一次 `SendBagItems` 时才同步真实顺序

**待源码核对**：
1. Delphi 客户端拖拽背包物品时是否发送 CM_ 移动请求？
2. 如果发送，对应的 CM_ 消息 ID 是什么？
3. 服务端如何处理？

**C++ 推荐**：
- 如果 Delphi 不发送服务端移动协议，则 C++ 也应保持客户端本地拖拽（不通知服务端）
- 下一次完整刷新 `SendBagItems` 时服务端覆盖客户端排列
- 对 client_v1 客户端，`BagSnapshot` 携带完整顺序，客户端本地拖拽后不在服务端反映

---

## 9. 物品使用设计

### 9.1 Delphi 使用链路分析

**EatItem (ObjBase.pas:616)**：
```pascal
function TCreature.EatItem(std: TStdItem; pu: PTUserItem): Boolean;
// 1. 检查 HP/MP 是否已满 (std.HpAdd > 0 && HP < MaxHP) || (std.MpAdd > 0 && MP < MaxMP)
// 2. 如果满了 → SysMsg + return FALSE (不消耗物品)
// 3. 如果没满 → HP += HpAdd, MP += MpAdd
// 4. HealthSpellChanged (发送 HP/MP 变化)
// 5. return TRUE (调用者负责扣除物品)
```

**ServerGetEatItem (ObjBase.pas:700)**：
```pascal
// 1. 校验状态（死亡/麻痹/等）
// 2. 找到物品
// 3. EatItem → 如果成功
//    a. 扣除/删除物品
//    b. SendDelItem / SendUpdateItem
//    c. HealthSpellChanged
// 4. 如果失败 → 发送使用失败提示
```

### 9.2 物品使用分类

| 物品类别 | StdMode | Delphi 行为 | C++ 当前状态 | 待核对 |
|---------|---------|-----------|------------|--------|
| 药品 (HP/MP) | 0 | EatItem 处理 HP/MP 恢复，使用后扣除 | `apply_consumable()` + `remove_bag_item()` | 使用失败（满血）是否扣除物品 |
| 卷轴 (回城/随机) | 31 | Scroll 处理 → 地图切换/随机传送 → 扣除卷轴 | `legacy_item_is_scroll()` 已识别但使用链路待补齐 | 卷轴使用后消息顺序 |
| 技能书 | 4 | `ReadBook` → 添加技能 → 扣除技能书 | `legacy_item_is_magic_book()` 已有 | 技能已满时是否消耗书 |
| 装备类 | 5/6/10/11/15/19-26/30/52-54 | 使用=穿戴 → `ServerGetTakeOnItem` | `handle_take_on()` 已实现 | 使用装备时是否调 EatItem |
| 油/祝福油 | 3.Shape=4 | 武器祝福/诅咒 | `legacy_is_blessed_oil()` 已识别 | 祝福/诅咒判定逻辑 |
| 礼包/宝箱 | 待核对 | 使用后获得物品 | 未实现 | Delphi 是否有此功能 |

### 9.3 使用物品的推荐顺序

**药品使用（对齐 Delphi 顺序）**：
```
1. CM_EAT → 校验状态 (死亡/麻痹/交易/商店/安全区/地图限制)
2. 找到物品 (按 MakeIndex + Name)
3. EatItem 效果判定
   → 满血：SysMsg("您的体力已满") + return (不扣除)
   → 不满：HP/MP 增加
4. HealthSpellChanged → SM_HEALTHSPELLCHANGED (53) 发送 HP/MP 刷新
5. 扣除物品 (remove_bag_item or reduce count)
6. SendDelItem / SendUpdateItem 发送背包刷新
7. 系统消息 (如果有)
```

**待 golden trace 验证**：第 4 步和第 5 步的顺序——Delphi 是先发 HealthSpellChanged 还是先扣除物品？

---

## 10. 丢弃与拾取设计

### 10.1 丢弃

**Delphi 原版顺序 (UserDropItem + DropItemDown)**：
```
1. 客户端 CM_DROPITEM (1000) 发送物品标识
2. 服务端校验：
   a. 交易 CD (GetTickCount - DealItemChangeTime > 3000)
   b. 死亡/麻痹/等状态
   c. 找到背包中的物品
   d. 是否可丢弃 (绑定物品/任务物品标记)
3. DropItemDown → 生成地面物品 TMapItem
   a. GetDropPosition(散落范围) 计算坐标
   b. PEnvir.AddToMap(OS_ITEMOBJECT, pmi)
   c. SendRefMsg(RM_ITEMSHOW) 地面物品出现
4. 成功后：
   a. DeletePItemAndSend → 删除背包物品 + SendDelItem
   b. 发送丢弃成功系统消息
5. 失败则 Dispose(pmi) → 物品保持背包
```

**C++ 推荐顺序（严格对齐）**：
```
1. 接收 CM_DROPITEM / client_v1 drop request
2. 校验状态 + 校验物品存在 + 校验可丢弃
3. 创建 GroundItem → environment_.add_to_map()
4. 成功后：
   a. 从背包删除物品
   b. queue SendDelItem (legacy) 或 InventoryRemove (client_v1)
   c. queue RM_ITEMSHOW (地面物品出现)
   d. queue 系统消息
5. 地面生成失败 → 不回滚背包（物品还在包里）
```

### 10.2 拾取

**Delphi 原版顺序**：
```
1. 客户端 CM_PICKUP (1001) 发送地面物品 ID
2. 服务端校验：
   a. 距离 (通常 2 格以内？待核对)
   b. 可见性
   c. 物品归属 (如果为死亡掉落，有时间归属限制)
   d. 背包空间 (CanAddItem)
   e. 负重 (IsAddWeightAvailable)
3. 从地图删除地面物品 (DeleteFromMap)
4. 添加到背包 (AddItem)
5. SendBagItems / SendAddItem（但通常使用 SendAddItem）
6. SendRefMsg(RM_ITEMHIDE) 地面物品消失
7. 发送系统消息（金币为数量，物品为名称）
```

**C++ 推荐顺序（严格对齐）**：
```
1. 校验距离/可见性/归属/背包空间/负重
2. 如果校验失败 → 发送失败提示（不操作）
3. 如果校验成功：
   a. 从地图删除地面物品
   b. 添加到背包
   c. queue RM_ITEMHIDE (地面物品消失)
   d. queue SendAddItem (背包添加)
   e. queue 系统消息
```

**关键对齐点**：
- 先删地图or先加背包：Delphi 是先 `DeleteFromMap` 再 `AddItem`？需 golden trace 验证
- 背包满时：地面物品保持存在，发送"背包已满"系统消息
- 拾取金币：与拾取物品类似但无背包格子占用

### 10.3 多人同时拾取

- Delphi：同一 tick 内先到先得（GetMsg 顺序）
- C++：同一 tick 内 FIFO → 第一人拿走后第二人校验失败
- client_v1 批量消息：不能因消息批量到达而获得拾取优势 — 服务端权威校验是最关键的防线

---

## 11. 背包与装备系统交互

### 11.1 穿戴装备

**Delphi 原版顺序 (ServerGetTakeOnItem)**：
```
1. 校验状态
2. 找到背包物品 (按 MakeIndex+Name)
3. 判定装备槽位 (CanTakeOn: 职业/性别/等级/负重检查)
4. 如果目标装备槽非空 → 需要脱下旧装备
   a. 脱下旧装备到背包 (AddItem)
   b. 如果背包满 → 脱装备失败 → 整个穿戴失败
5. 从背包删除新装备 (DelItem)
6. 写入装备槽 (UseItems[slot] := uitem)
7. SendDelItem (旧装备的背包刷新，如果需要)
8. SendUseItems (装备栏刷新)
9. RecalcAbilitys → SM_ABILITY + SM_SUBABILITY
10. FeatureChanged → SM_FEATURECHANGED (外观刷新)
11. 系统消息
```

**C++ 推荐顺序**：
```
1. ~10. 与 Delphi 一致
```

**关键边界**：
- 背包满时脱装备：Delphi 拒绝整个操作。C++ 必须相同。
- 替换（Swap）：如果同类型装备替换，是否支持直接交换？Delphi 中是先脱再穿（两步操作，非原子）。如果脱装备成功但穿装备失败，玩家失去旧装备保护。C++ 应使用事务包装确保原子性。

### 11.2 脱下装备

**Delphi 原版顺序 (ServerGetTakeOffItem)**：
```
1. 校验状态
2. 检查背包空间 (CanAddItem)
3. 如果背包满 → 失败+提示
4. 从装备栏删除 (UseItems[slot].Index := 0)
5. 添加到背包 (AddItem)
6. SendUseItems (装备栏刷新)
7. SendAddItem (背包添加)
8. RecalcAbilitys → SM_ABILITY + SM_SUBABILITY
9. FeatureChanged → SM_FEATURECHANGED
10. 系统消息
```

### 11.3 完整交互表

| 操作 | Delphi 原版顺序 | C++ 推荐顺序 | 消息顺序 | 兼容风险 | 测试方法 |
|------|--------------|------------|---------|---------|---------|
| 穿装备（空槽） | 删背包→写装备→SendDelItem→SendUseItems→Recalc→FeatureChanged | 同 | 背包删除→装备刷新→属性刷新→外观刷新 | 低 | 装备 smoke |
| 穿装备（替换） | 脱旧→穿新→(两步) | 事务包装 | 同 | 中：如果脱成功穿失败 | 事务回滚测试 |
| 脱装备 | 删装备→加背包→SendUseItems→SendAddItem→Recalc→FeatureChanged | 同 | 装备删除→背包添加→属性刷新→外观刷新 | 低（背包满已处理） | 脱装备 smoke |
| 背包满时脱装备 | 拒绝 | 拒绝 | 仅失败消息 | 无 | 单元测试 |
| 装备持久变化 | StruckDamage→Dura--→SM_DURACHANGE | 同 | 持久变化消息 | 低 | 持久 smoke |

---

## 12. 背包与商店 / 仓库 / 交易 / 任务交互

### 12.1 商店买卖

| 操作 | Delphi 顺序 | 权威 | 是否需要事务 | 失败回滚 | 消息顺序 |
|------|-----------|------|-----------|---------|---------|
| 买入 | 金币校验→扣金币→AddItem→SendAddItem→GoldChanged→系统消息 | 服务端 | 否（背包满已在 AddItem 前检查） | 扣金币成功但 AddItem 失败=金币丢失？**待核对** | 金币变化→背包添加→系统消息 |
| 卖出 | 查物品→DelItem→SendDelItem→加金币→GoldChanged→系统消息 | 服务端 | 否 | 删物品成功但加金币失败=物品丢失？**待核对** | 背包删除→金币变化→系统消息 |
| 修理 | 扣金币→修改 Dura→SendUpdateItem→SendUseItems→GoldChanged→系统消息 | 服务端 | 否 | 低风险 | 金币变化→持久刷新→系统消息 |

**待核对**：Delphi 商店买卖是否有事务保护？还是依赖顺序保证（金币操作不会失败）？

### 12.2 仓库存取

| 操作 | Delphi 顺序 | 背包满处理 | 消息顺序 |
|------|-----------|-----------|---------|
| 存入 | 查物品→DelItem→AddStorage→SendDelItem→SendStorageItemList | N/A（总是在加仓库物品） | 背包删除→仓库刷新 |
| 取出 | 查仓库物品→DelStorageItem→AddItem→SendAddItem→SendStorageItemList | 拒绝+提示"背包已满" | 仓库删除→背包添加 |

### 12.3 交易

| 阶段 | Delphi 行为 | C++ 当前 |
|------|-----------|---------|
| 放入交易栏 | 物品从背包临时移除到 DealList | `TradeOffer.items` 保存物品副本 |
| 取消交易 | DealList 物品恢复到背包 | 物品副本恢复到背包 |
| 确认交易 | 双方 DealList 物品交叉添加到对方背包 | 原子交换 |
| 背包满 | 交易完成时背包满 → 取消交易+回滚？**待核对** | 当前会提示失败 |

**关键**：交易中物品必须被"锁定"——不能同时使用/丢弃/出售/存入仓库。C++ 已通过 `TradeSession` 中的物品副本来隔离。

### 12.4 任务/NPC 脚本

| 操作 | 含义 | 背包满处理 |
|------|------|-----------|
| `CheckItem` | 检查背包中是否有指定物品（名称+数量） | N/A |
| `TakeItem` | 从背包扣除指定数量的物品 | 物品不足→失败 |
| `GiveItem` | 给予物品到背包 | **待核对**：背包满时是否部分成功还是全部拒绝 |

---

## 13. 协议与消息顺序映射

### 13.1 CM_* 客户端→服务端

| Delphi 常量 | 值 | 含义 | C++ Legacy | C++ client_v1 |
|------------|----|------|-----------|---------------|
| CM_QUERYBAGITEMS | 81 | 请求背包数据 | kCmQueryBagItems | 登录时自动发送 BagSnapshot |
| CM_DROPITEM | 1000 | 丢弃物品 | kCmDropItem | ActionIntent + item info |
| CM_PICKUP | 1001 | 拾取物品 | kCmPickup | ActionIntent(kind=pickup) |
| CM_TAKEONITEM | 1003 | 穿戴装备 | kCmTakeOnItem | ActionIntent(kind=equip) |
| CM_TAKEOFFITEM | 1004 | 脱下装备 | kCmTakeOffItem | ActionIntent(kind=unequip) |
| CM_EXCHGTAKEONITEM | 1005 | 交换装备 | kCmExchgTakeOnItem | 待核对 |
| CM_EAT | 1006 | 使用物品 | kCmEat | ActionIntent(kind=use_item) |
| CM_DROPGOLD | 1016 | 丢弃金币 | kCmDropGold | ActionIntent |
| CM_USERSELLITEM | 1013 | 出售物品 | kCmUserSellItem | MerchantAction |
| CM_USERBUYITEM | 1014 | 购买物品 | kCmUserBuyItem | MerchantAction |
| CM_USERREPAIRITEM | 1023 | 修理物品 | kCmUserRepairItem | MerchantAction |
| CM_DEALADDITEM | 1026 | 交易添加物品 | kCmDealAddItem | TradeAction |
| CM_DEALDELITEM | 1027 | 交易删除物品 | kCmDealDelItem | TradeAction |
| CM_USERSTORAGEITEM | 1031 | 仓库存储 | kCmUserStorageItem | NpcAction |
| CM_USERTAKEBACKSTORAGEITEM | 1032 | 仓库取回 | kCmUserTakeBackStorageItem | NpcAction |

### 13.2 SM_* 服务端→客户端（背包相关）

| Delphi 常量 | 值 | 含义 | 触发时机 | C++ Legacy | C++ client_v1 |
|------------|----|------|---------|-----------|---------------|
| SM_ADDBAGITEM | 200 | 添加物品 | AddItem后 | kSmAddItem | InventoryAdd |
| SM_BAGITEMS | 201 | 背包全量刷新 | 登录/打开背包 | kSmBagItems | BagSnapshot |
| SM_DELITEM | 202 | 删除物品 | DelItem后 | kSmDelItem | InventoryRemove |
| SM_UPDATEITEM | 203 | 更新物品 | Dura变化后 | kSmUpdateItem | InventoryUpdate |
| SM_DROPITEM_SUCCESS | 600 | 丢弃成功 | UserDropItem后 | kSmDropItemSuccess | item drop ack |
| SM_DROPITEM_FAIL | 601 | 丢弃失败 | 丢弃校验失败 | kSmDropItemFail | item drop fail |
| SM_ITEMSHOW | 610 | 地面物品出现 | DropItemDown后 | kSmItemShow | GroundItemAdd |
| SM_ITEMHIDE | 611 | 地面物品消失 | 拾取/过期 | kSmItemHide | GroundItemRemove |
| SM_TAKEON_OK | 615 | 穿戴成功 | ServerGetTakeOnItem | kSmTakeOnOk | EquipmentSnapshot |
| SM_TAKEON_FAIL | 616 | 穿戴失败 | 校验失败 | kSmTakeOnFail | error message |
| SM_TAKEOFF_OK | 619 | 脱下成功 | ServerGetTakeOffItem | kSmTakeOffOk | EquipmentSnapshot |
| SM_TAKEOFF_FAIL | 620 | 脱下失败 | 背包满等 | kSmTakeOffFail | error message |
| SM_EAT_OK | 635 | 使用成功 | EatItem后 | kSmEatOk | UseItemResult |
| SM_EAT_FAIL | 636 | 使用失败 | EatItem校验失败 | kSmEatFail | UseItemResult |
| SM_WEIGHTCHANGED | 622 | 负重变化 | WeightChanged后 | kSmWeightChanged | SelfAbility |
| SM_GOLDCHANGED | 653 | 金币变化 | GoldChanged后 | kSmGoldChanged | SelfAbility |
| SM_DURACHANGE | 642 | 持久变化 | StruckDamage后 | kSmDuraChange | DurabilityChange |
| SM_ABILITY | 52 | 属性变化 | RecalcAbilitys后 | kSmAbility | SelfAbility |
| SM_SUBABILITY | 752 | 详细属性 | RecalcAbilitys后 | kSmSubAbility | SelfAbilityDetail |

### 13.3 关键消息顺序（必须对齐）

**使用药品**：
```
Delphi: EatItem效果 → HealthSpellChanged(53) → 扣除物品 → SendDelItem(202) → 系统消息
C++目标: 同顺序
待验证: HealthSpellChanged 在扣除物品前还是后？
```

**拾取物品**：
```
Delphi: DeleteFromMap → AddItem → RM_ITEMHIDE(611) → SendAddItem(200) → 系统消息
C++目标: 同顺序
```

**穿装备**：
```
Delphi: DelItem(旧) → AddItem(旧) → UseItems[slot]=新 → SendDelItem(202旧) → SendUseItems → RecalcAbilitys → SM_ABILITY(52)+SM_SUBABILITY(752) → SM_FEATURECHANGED(41) → 系统消息
C++目标: 同顺序
```

---

## 14. 主循环与时序兼容分析

### 14.1 主循环对比

| 阶段 | Delphi | C++ | 背包操作位置 |
|------|--------|-----|-----------|
| 网络接收 | RunSocket.Run | process_ingress_batch | 解码 → enqueue_legacy_command |
| 玩家处理 | ExecuteRun → ProcessUserHumans → GetMsg while | legacy_operate_player_running → handle_mail | **背包操作在此执行** |
| 怪物处理 | ProcessMonsters | legacy_process_monster | 死亡爆物 → 生成 GroundItem |
| NPC 处理 | ProcessMerchants/ProcessNpcs | legacy_process_merchant/npc | 商家刷新/任务脚本 |
| 帧结束 | EventMan.Run | frame-end dispatch | **保存/发包在此阶段发出** |

### 14.2 时序分析

**Q1: 背包操作命令在哪个阶段处理？**
**A**: `legacy_operate_player_running` → `handle_mail`。每个玩家 tick 从 `legacy_inbox_` 取出最多 `player_input_budget_per_tick` 条命令逐条处理。背包操作（使用/丢弃/拾取/装备/商店/交易）都在此完成。

**Q2: 物品使用效果在哪个阶段生效？**
**A**: `handle_mail` 内同步生效。HP/MP 直接修改玩家属性，背包物品扣除直接修改 bag_items。

**Q3: 拾取和移动同 frame 如何排序？**
**A**: 按 `legacy_inbox_` 的 sequence 顺序 → FIFO。先到达的命令先处理。

**Q4: 使用药品和受到伤害同 frame 如何排序？**
**A**: 按 `legacy_inbox_` sequence 顺序。如果 CM_EAT 先于 monster damage tick，则先回血再扣血；反之则先扣血再回血（可能导致死亡但被回血救回来）。

**Q5: frame-end dispatch 是否改变消息顺序？**
**A**: 当前 `RuntimeDispatch` 收集 session_events，frame-end 统一发送。关键：收集顺序 = 发送顺序。如果多个消息在同一个 RuntimeDispatch 中，它们按被 queue 的顺序发送。必须验证这个顺序与 Delphi `SendMsg` / `SendRefMsg` 即时发送的顺序一致。

**Q6: client_v1 批量消息是否给 client_v1 客户端操作优势？**
**A**: 关键差异 — Delphi 客户端每次 recv 一个消息，而 client_v1 客户端一次 recv 可能带走一个 batch 中的多条消息。但这不改变服务端权威判定：服务端按 sequence 处理，客户端只是更快看到结果。如果同一个 tick 内有多条物品消息，client_v1 客户端可能在 UI 上更快看到刷新，但操作的 FCFS 判定仍然是服务端决定的。

### 14.3 时序建议

```
背包系统应接入 C++ 主循环的哪个阶段：
  → legacy_operate_player_running → handle_mail（已在此处理）

哪些行为必须立即执行：
  → 物品数据修改（add/remove/move）必须在 handle_mail 内同步完成

哪些行为可以延迟到 frame-end：
  → 协议发送（collect 到 RuntimeDispatch）——但保持收集顺序
  → 持久化（PersistRequest）——frame-end 发出

哪些消息必须严格保持 Delphi 顺序：
  → 所有 SM_* 消息的发送顺序必须与 Delphi 一致（已在 Section 13 列出）
```

---

## 15. 异常路径与安全设计

### 15.1 安全审查清单

| # | 风险 | Delphi 可能行为 | C++ 防护策略 | 兼容影响 | 测试 |
|---|------|--------------|-----------|---------|------|
| 1 | 客户端伪造物品 ID | 服务端按 MakeIndex 校验，不信任客户端 | 服务端只信任自己的 MakeIndex | 无 | Fuzz |
| 2 | 客户端伪造背包格子 | 服务端校验 bagindex 在有效范围 | 校验 slot 在 [0,45] 且非空 | 中（bagindex语义） | Fuzz |
| 3 | 客户端伪造数量 | 无 count 字段，无此风险 | N/A | 无 | N/A |
| 4 | 客户端伪造持久 | 服务端维护 Dura，客户端提交参与价格计算但不改变服务端值 | 商店卖价由服务端计算 | 无 | Smoke |
| 5 | 客户端伪造属性 | 服务端权威维护 Desc[] | 服务端权威 | 无 | Fuzz |
| 6 | 使用不存在的物品 | EatItem 找不到→失败+不扣物品 | 校验物品存在 | 无 | Smoke |
| 7 | 删除不存在的物品 | DelItem 找不到→返回 FALSE | 返回空 optional | 无 | Smoke |
| 8 | 源格为空移动 | Delphi 可能不支持服务端移动 | 校验源格非空 | 中 | Fuzz |
| 9 | 目标格非法 | bagindex 超出范围 | 校验 slot 在 [0,45] | 无 | Fuzz |
| 10 | 数量溢出 | 无 count 字段，但 Dura 可能溢出 | Dura 上限 clamp | 无 | Fuzz |
| 11 | 背包满但仍添加成功 | AddItem 检查 Count | 检查 has_free_bag_slot | 无 | 单元测试 |
| 12 | 负重不足但仍添加成功 | Delphi AddItem 不检查重量，由调用者负责 | 调用者检查 can_add_bag_item | 无 | 单元测试 |
| 13 | 交易锁定物品被使用/丢弃 | DealItemChangeTime 3s CD | ItemReservation 检查 | 无 | Smoke |
| 14 | 商店出售时物品已被移动 | 按 MakeIndex 定位，已移动则失败 | 按 make_index 查找 | 无 | Smoke |
| 15 | 使用药品和死亡同 frame | GetMsg 顺序决定 | legacy_inbox_ sequence 顺序 | 无 | Smoke |
| 16 | 拾取地面物品多人竞争 | 先到先得 (FCFS) | FIFO by sequence，第二人校验失败 | 无 | Concurrency smoke |
| 17 | 丢弃生成地图物品失败 | Dispose pmi（物品不丢失——还在背包） | 与 Delphi 一致 | 无 | Smoke |
| 18 | 存档失败导致回档复制物品 | Delphi 同步写盘 | 异步持久化 + 版本号防旧覆盖 | 高 | 持久化测试 |
| 19 | 异步保存旧状态覆盖新状态 | Delphi 同步保存 | persist queue 携带 CharacterRecord 快照 + 版本号 | 高 | 持久化测试 |
| 20 | client_v1 重放旧操作 | 无（老协议无重放） | 命令 sequence 单调递增 + session dedup | 低 | Smoke |
| 21 | 网络乱序导致 UI 状态覆盖 | SM_* 顺序由服务端保证 | RuntimeDispatch 保持收集顺序 | 低 | Protocol smoke |
| 22 | 多线程直接访问背包 | Delphi 单线程 | 仅玩家逻辑线程修改背包 | 高 | Thread safety assert |
| 23 | Tooltip 指向已删除物品 | client_v1 缓存可能持有旧引用 | BagSnapshot 全量刷新 + item validity check | 低 | UI smoke |
| 24 | 背包刷新时客户端仍在拖拽 | 客户端本地处理（待核对） | 客户端刷新时应取消拖拽 | 低 | UI fuzz |
| 25 | 背包 index 语义不匹配 | 如果客户端按 bagindex 定位且 C++ 不压缩 | 方案 A：协议层压缩空格 | 高 | Protocol golden |

---

## 16. 持久化与存档设计

### 16.1 保存字段

背包数据通过 `CharacterRecord::bag_items` 整体序列化。每个 `LegacyUserItem` 的所有字段（MakeIndex, Index, Dura, DuraMax, Desc[0..13], ColorR/G/B, Prefix）都持久化。

### 16.2 保存时机

| 时机 | 触发 | 当前 C++ |
|------|------|---------|
| 登录加载 | LoadCharacter | `persistence_service` → `CharacterRecord` |
| 下线保存 | Player closed | `queue_save_player_character` in `dispatch_legacy_close` |
| 定时保存 | 15 分钟间隔 | `kLegacyAutoSaveMs = 15 * 60 * 1000` |
| 交易完成后 | 双方角色变更 | frame-end persist |
| 商店买卖后 | 角色变更（金币+物品） | frame-end persist |
| 仓库存取后 | 角色变更 | frame-end persist |
| 死亡爆物后 | 物品变更 | frame-end persist |
| 服务器关闭 | 全局保存 | ? （待确认） |

### 16.3 一致性要求

| 要求 | 建议 |
|------|------|
| 背包/装备/金币是否同事务 | 是：它们在同一个 `CharacterRecord` 中，一次 `PersistRequest` 整体持久化 |
| 交易完成是否双方同事务 | 建议是：但 Delphi 可能不是（双方独立保存）。至少在同一个 RuntimeDispatch 的 persist_requests 中 |
| 保存失败处理 | 当前无显式处理 — **需要实现**：重试队列 + 错误日志 |
| 异步保存防旧覆盖 | `PersistRequest` 携带 `CharacterRecord` 快照。如果多个 save 并发，使用版本号（`save_version`）或 UUID 防止旧覆盖新 |
| 操作日志 | 建议新增：物品操作审计日志（MakeIndex, 操作类型, 时间, 角色） |
| 崩溃回档范围 | 最大 15 分钟（autosave 间隔）。建议在关键操作（交易完成/商店大宗交易）后立即触发保存 |

### 16.4 防复制物品

| 策略 | 说明 |
|------|------|
| MakeIndex 单调递增 | 每个物品有全局唯一 ID |
| 保存版本号 | 每次保存递增，加载时检查 |
| 服务端权威 | 客户端不生成 MakeIndex |
| 操作日志 | 关键操作审计 |
| 定期校验 | 可选：定期扫描重复 MakeIndex |

---

## 17. 测试、Golden Trace、经济一致性与 Fuzz 方案

### 17.1 单元测试

```
Items:
  ✓ bag_item_lookup_by_make_index_and_name
  ✓ bag_item_lookup_by_empty_slot
  ✓ add_item_first_empty_slot
  ✓ add_item_full_bag_rejected
  ✓ remove_item_by_make_index
  ✓ remove_item_by_slot
  ✓ has_free_bag_slot_empty
  ✓ has_free_bag_slot_full
  ✓ can_add_bag_item_weight_check
  ✓ calc_bag_weight_empty
  ✓ calc_bag_weight_multiple_items
  ✓ bag_slot_scan_order_0_to_45
  ✓ make_index_unique_monotonic
  ✓ equip_item_swaps_correctly
  ✓ unequip_full_bag_rejected
  ✓ item_serialization_roundtrip
  ✓ consume_drug_restores_hp_mp
  ✓ consume_drug_fails_when_full
  ✓ book_learn_magic_and_consumed
  ✓ scroll_teleport_and_consumed
  ✓ pickup_from_ground_to_bag
  ✓ pickup_full_bag_keeps_ground_item
  ✓ drop_item_spawns_ground
  ✓ drop_item_removes_from_bag
```

### 17.2 协议 Smoke 测试

```
Protocol (legacy #...!):
  ✓ CM_QUERYBAGITEMS → SM_BAGITEMS
  ✓ CM_EAT → SM_EAT_OK / SM_EAT_FAIL + SM_HEALTHSPELLCHANGED
  ✓ CM_DROPITEM → SM_DROPITEM_SUCCESS + SM_DELITEM + SM_ITEMSHOW
  ✓ CM_PICKUP → SM_ITEMHIDE + SM_ADDITEM
  ✓ CM_TAKEONITEM → SM_TAKEON_OK + SM_DELITEM + SM_ABILITY
  ✓ CM_TAKEOFFITEM → SM_TAKEOFF_OK + SM_ADDITEM + SM_ABILITY
  ✓ CM_USERBUYITEM → SM_BUYITEM_SUCCESS + SM_ADDITEM + SM_GOLDCHANGED
  ✓ CM_USERSELLITEM → SM_USERSELLITEMOK + SM_DELITEM + SM_GOLDCHANGED
  ✓ CM_USERSTORAGEITEM → SM_STORAGEOK
  ✓ CM_USERTAKEBACKSTORAGEITEM → SM_TAKEBACKSTORAGEITEMOK
  ✓ CM_DEALADDITEM → SM_DEALADDITEMOK
  ✓ CM_DEALEND → SM_DEALSUCCESS + 双方背包刷新

Protocol (client_v1):
  ✓ BagSnapshot 完整发送
  ✓ InventoryAdd/Remove/Update 单物品变化
  ✓ UseItemResult 带成功/失败
  ✓ GroundItemAdd/Remove 地面物品
  ✓ EquipmentSnapshot 装备栏
  ✓ TradeState 交易全过程
```

### 17.3 Golden Trace 测试

```
基于 Delphi 协议抓包或静态审查生成 expected trace，按帧/消息 ID/参数顺序。

物品使用 golden:
  Input: CM_EAT make_index=200001 name="金创药(小)量"
  Expected trace:
    [EAT CHECK] item found, hp=50/100, std.HpAdd=30
    [EAT APPLY] hp: 50→80
    [SEND] SM_HEALTHSPELLCHANGED: hp=80 mp=100
    [SEND] SM_DELITEM: make_index=200001
    [SEND] SM_EAT_OK

丢弃 golden:
  Input: CM_DROPITEM make_index=200001 name="金创药(小)量"
  Expected trace:
    [DROP CHECK] item found, count=1
    [DROP SPAWN] ground_item at (x+dx, y+dy)
    [SEND] SM_DELITEM: make_index=200001
    [SEND] SM_ITEMSHOW: ground_item_id=xxx looks=xxx
    [SEND] SM_DROPITEM_SUCCESS

拾取 golden:
  Input: CM_PICKUP ground_item_id=xxx
  Expected trace:
    [PICKUP CHECK] distance=1, bag_space=ok, weight=ok
    [PICKUP EXEC] delete from map, add to bag
    [SEND] SM_ITEMHIDE: ground_item_id=xxx
    [SEND] SM_ADDITEM: TClientItem(...)
```

### 17.4 经济一致性测试

```
Economic invariants:
  ✓ 物品总数守恒：del_bag + add_ground = 0 (丢弃)
  ✓ 物品总数守恒：add_bag + del_ground = 0 (拾取)
  ✓ 金币守恒：玩家A扣金币 + 玩家B加金币 = 0 (交易)
  ✓ 金币守恒：玩家扣金币 + 商店加金币 = 0 (商店)
  ✓ 交易双方物品守恒
  ✓ 不产生负金币
  ✓ 不产生负数量
  ✓ 不产生幽灵物品（MakeIndex 存在但无对应背包/装备/仓库/地面物品）
  ✓ 同一 MakeIndex 不能同时在背包和装备栏
  ✓ 同一 MakeIndex 不能同时在背包和仓库
  ✓ 同一 MakeIndex 不能同时在两个玩家背包
```

### 17.5 Fuzz 测试

```
Randomized inputs:
  - 随机物品 ID (合法/非法/越界)
  - 随机格子索引 (-1, 0~45, 46+)
  - 随机数量 (0, 负数, 超大值)
  - 随机持久 (0, 负数, >DuraMax)
  - 高频使用 (同 tick 多次 CM_EAT)
  - 高频拾取 (同 tick 多次 CM_PICKUP)
  - 高频丢弃 (同 tick 多次 CM_DROPITEM)
  - 操作交错 (使用→丢弃→拾取→穿戴→出售 随机排列)
  - 背包刷新和拖拽交错
  - 交易/商店/仓库/任务 操作交错
  - 下线/死亡/切图 与背包操作交错
  - 随机网络延迟+重放

Expected invariants (after each fuzz iteration):
  - 不崩溃
  - 不越权（客户端不能绕过服务端校验）
  - 不复制物品
  - 不丢失物品
  - 不破坏存档
  - 不改变消息顺序
```

---

## 18. PR 拆分实施计划

### PR-1: Delphi 背包语义最终审查

**目标**: 确认所有"待源码核对"项，特别是 bagindex 语义、数量叠加、客户端协议定位方式。

**涉及文件**: Delphi `ObjBase.pas`, `Grobal2.pas`, `ClMain.pas`, `DWinCtl.pas`

**主要改动**: 无代码改动，仅文档

**不改什么**: 代码

**风险**: 审查结果可能推翻当前设计假设

**测试**: 不适用

**验收标准**:
- [ ] 确认 CM_EAT/CM_DROPITEM/CM_TAKEONITEM 的 `svindex` 参数是 MakeIndex 还是 bagindex
- [ ] 确认 SendDelItem 发送的是 MakeIndex 还是 bagindex
- [ ] 确认 Delphi 是否有物品数量叠加机制
- [ ] 确认客户端拖拽背包物品是否发送服务端请求
- [ ] 确认丢弃前是否需要客户端确认窗口
- [ ] 输出 updated boundary classification
- [ ] 输出 golden trace expected sequences（如果 Delphi 能运行则录制，否则静态推导）
- [ ] 输出待核对清单清零

### PR-2: ItemTemplate / ItemInstance 字段完整性补齐

**目标**: 补齐 `ItemConfig` 中缺失的字段，确保与 Delphi `TStdItem` 字段完全对应。

**涉及文件**: `ModernServer/src/config/models.hpp`, `ModernServer/src/world/legacy_item_rules.cpp`

**主要改动**:
- 补全 ItemConfig 缺失字段
- 确保 make_std_item() 转换不丢失字段
- 补齐 legacy_upgraded_item_config 未覆盖的 StdMode

**不改什么**: 背包操作逻辑

**风险**: 低——纯数据层补齐

**测试**: 现有 item smoke + 新增字段完整性单元测试

**验收标准**:
- [ ] ItemConfig 字段与 TStdItem 字段逐项对应
- [ ] make_std_item() 往返测试通过
- [ ] LegacyStdItem 与 Delphi TStdItem 逐字节对比通过

### PR-3: 背包格子语义对齐 + InventoryComponent

**目标**: 根据 PR1 审查结果，对齐背包格子语义。

**涉及文件**: `ModernServer/src/world/map_actor.cpp`, `ModernServer/src/core/messages.hpp`

**主要改动**（取决于 PR1 结果）:
- 如果客户端用 bagindex：实现方案 A（协议层压缩空格）
- 如果客户端用 MakeIndex：当前实现基本正确
- 调整 `add_bag_item` 的位置策略（第一空格 vs 末尾追加）
- 补齐 `SendBagItems` 的 legacy 协议实现

**不改什么**: 物品使用、丢弃、装备逻辑

**风险**: 中——bagindex 语义影响所有协议

**测试**: 协议 golden trace + 单元测试

**验收标准**:
- [ ] bag 格子语义与 Delphi 行为一致
- [ ] SendBagItems 输出与 Delphi 格式一致
- [ ] SendDelItem 输出与 Delphi 格式一致
- [ ] SendAddItem 输出与 Delphi 格式一致

### PR-4: 背包协议刷新序列对齐

**目标**: 确保所有背包相关消息的发送顺序与 Delphi 一致。

**涉及文件**: `ModernServer/src/world/map_actor.cpp`, `ModernServer/src/world/map_actor_packets.hpp`

**主要改动**:
- 对齐 SM_BAGITEMS (201) 发送时机和内容
- 对齐 SM_ADDITEM (200) 参数格式
- 对齐 SM_DELITEM (202) 参数格式
- 对齐 SM_UPDATEITEM (203) 参数格式
- 补齐 legacy `#...!` 协议的背包消息编码

**不改什么**: 业务逻辑

**风险**: 低——纯协议层调整

**测试**: Protocol smoke + golden trace diff

**验收标准**:
- [ ] 所有背包 SM_* 消息编码与 Delphi 一致
- [ ] 消息发送顺序与 Delphi 一致
- [ ] legacy 和 client_v1 双通道 smoke 通过

### PR-5: 物品使用逻辑完整对齐

**目标**: 对齐 Delphi EatItem / ReadBook / Scroll / BlessedOil 等使用逻辑。

**涉及文件**: `ModernServer/src/world/map_actor.cpp`

**主要改动**:
- 对齐 HP/MP 药品使用流程（校验→效果→扣除→刷新顺序）
- 对齐卷轴使用（回城/随机传送/行会回城）
- 对齐技能书使用
- 对齐祝福油/武器升级材料使用
- 补齐使用失败路径（满血/满蓝/地图禁止/状态禁止）

**不改什么**: 背包格子逻辑、装备逻辑

**风险**: 中——使用顺序影响玩家体验

**测试**: EatItem smoke + golden trace + fuzz

**验收标准**:
- [ ] 药品使用后 HP/MP 刷新在物品扣除之前（与 Delphi 顺序一致）
- [ ] 使用失败不扣除物品
- [ ] 战斗中/死亡中/交易中/麻痹中禁止使用

### PR-6: 丢弃与拾取完整对齐

**目标**: 对齐 Delphi DropItemDown / UserDropItem / PickUp 完整流程。

**涉及文件**: `ModernServer/src/world/map_actor.cpp`, `ModernServer/src/world/map_actor.hpp`

**主要改动**:
- 对齐丢弃商品确认流程（如果需要）
- 对齐地面物品所有权/归属/过期规则
- 对齐拾取距离/可见性/归属校验
- 对齐多人同时拾取竞争
- 补齐部分数量丢弃（如果 Delphi 支持）

**不改什么**: 背包格子逻辑

**风险**: 中——拾取竞争和物品归属不当导致纠纷

**测试**: Drop/Pickup smoke + concurrency fuzz + golden trace

**验收标准**:
- [ ] 丢弃→生成地面物品→删除背包→消息发送 顺序与 Delphi 一致
- [ ] 拾取→删除地面物品→添加背包→消息发送 顺序与 Delphi 一致
- [ ] 背包满保留地面物品
- [ ] 多人同时拾取 FCFS

### PR-7: 背包与装备交互完整对齐

**目标**: 对齐装备穿戴/脱下/替换的完整流程。

**涉及文件**: `ModernServer/src/world/map_actor.cpp`, `ModernServer/src/world/map_actor_player.hpp`

**主要改动**:
- 实现 InventoryTransaction 用于装备替换原子操作
- 对齐背包满时脱装备失败行为
- 对齐装备属性重算/外观刷新顺序
- 对齐装备持久变化消息

**不改什么**: 装备属性公式（那是 combat system）

**风险**: 中——装备替换失败路径复杂

**测试**: Equipment smoke + transaction test + golden trace

**验收标准**:
- [ ] 装备替换原子操作（脱+穿要么全成功要么全失败）
- [ ] 背包满时脱下装备失败+提示
- [ ] 属性重算和外观刷新顺序与 Delphi 一致

### PR-8: 背包与其他系统接口对齐

**目标**: 补齐商店/仓库/交易/任务系统的背包操作接口。

**涉及文件**: `ModernServer/src/world/map_actor.cpp`

**主要改动**:
- 实现 `ItemReservation` 用于交易/商店操作期间锁定
- 补齐商店买卖的事务安全
- 补齐仓库存取的满包处理
- 补齐任务 NPC 脚本的 GiveItem/TakeItem 失败路径
- 补齐交易完成时双方背包满的回滚逻辑

**不改什么**: 各系统核心逻辑

**风险**: 高——跨系统交互路径复杂，回滚逻辑可能引入新 bug

**测试**: 全系统 interaction smoke + 经济一致性测试

**验收标准**:
- [ ] 商店买卖失败不丢物品不丢金币
- [ ] 仓库存取背包满时正确处理
- [ ] 交易取消物品完整恢复
- [ ] 任务奖励背包满时正确处理
- [ ] 所有跨系统操作有 InventoryTransaction 保护

### PR-9: 异常路径与安全防护

**目标**: 补齐所有安全防护和异常路径处理。

**涉及文件**: `ModernServer/src/world/map_actor.cpp`, `ModernServer/src/services/persistence_service.cpp`

**主要改动**:
- 实现持久化版本号防旧覆盖
- 实现 MakeIndex 唯一性校验
- 补齐客户端伪造防护（所有输入校验）
- 补齐并发安全（断言/线程检查）
- 补齐操作审计日志
- 补齐存档失败重试

**不改什么**: 正常路径行为

**风险**: 高——防护不当导致经济漏洞

**测试**: Security fuzz + 绕过尝试测试 + 并发测试

**验收标准**:
- [ ] 无法通过伪造 MakeIndex 操作其他玩家的物品
- [ ] 无法通过伪造 bagindex 操作不存在的物品
- [ ] 存档版本号防旧覆盖
- [ ] 同 MakeIndex 不重复出现
- [ ] 所有外部输入有校验

### PR-10: CI / Golden Trace / Fuzz 全覆盖

**目标**: 补齐全部测试。

**涉及文件**: `ModernServer/tests/`, `ModernClient/tests/`, `docs/pr1_delphi_audit/golden_traces/`

**主要改动**:
- 补齐所有单元测试（Section 17.1）
- 补齐协议 smoke（Section 17.2）
- 补齐 golden trace（Section 17.3）
- 补齐经济一致性测试（Section 17.4）
- 补齐 fuzz 框架（Section 17.5）
- 补齐 CI 守门脚本

**不改什么**: 业务代码

**风险**: 低——纯测试增加

**测试**: 本身就是测试

**验收标准**:
- [ ] 所有单元测试通过
- [ ] 所有 protocol smoke 通过
- [ ] golden trace diff 为零（或已文档化的允许差异）
- [ ] 经济一致性检查通过
- [ ] fuzz 运行 10 万次无 invariant 违反
- [ ] CI pipeline 包含所有测试

---

## 19. 风险清单与最终验收标准

### 19.1 顶级风险

| # | 风险 | 严重度 | 可能影响 | 缓解措施 | PR |
|---|------|--------|---------|---------|-----|
| R1 | bagindex 语义不匹配（TList vs fixed array） | 严重 | 旧客户端背包 UI 完全混乱 | PR1 优先确认协议语义 | PR1→PR3 |
| R2 | Delphi 数量叠加机制未确认 | 严重 | 药品/卷轴等行为完全错误 | PR1 审查 | PR1→PR5 |
| R3 | 异步存档版本竞态 | 高 | 回档/物品复制/物品丢失 | 版本号 + 快照机制 | PR9 |
| R4 | client_v1 批量消息操作优势 | 中 | client_v1 客户端在 PvP/抢拾中占优 | 服务端权威 + FIFO | PR4 |
| R5 | 装备替换非原子导致装备丢失 | 高 | 玩家极品装备丢失 | InventoryTransaction | PR7 |
| R6 | 交易取消回滚不完备 | 高 | 交易中物品丢失 | ItemReservation + 回滚测试 | PR8 |
| R7 | 死亡爆物/使用药品同 frame 顺序 | 中 | 玩家以为喝了药但已死 | 按 sequence FIFO 与 Delphi 对比 | PR6+PR5 |
| R8 | 地面物品 expired 逻辑与 Delphi 不同 | 中 | 地面物品过早消失或永久不消失 | PR1 审查过期时间 | PR6 |

### 19.2 最终验收标准

**背包操作验收**：
- [ ] 添加物品到第一个可用空格，与 Delphi 行为一致
- [ ] 删除物品正确清除，与 Delphi 行为一致
- [ ] 背包满拒绝添加，与 Delphi 行为一致
- [ ] 负重计算与 Delphi 逐条一致
- [ ] 背包格子顺序与 Delphi UI 显示一致

**物品使用验收**：
- [ ] 药品使用 → HP/MP 刷新在物品扣除前/后与 Delphi 顺序一致
- [ ] 使用失败不扣除物品
- [ ] 禁止状态（死亡/麻痹/交易/商店/地图）下使用被拒绝
- [ ] 卷轴/技能书/祝福油使用与 Delphi 行为一致

**丢弃/拾取验收**：
- [ ] 丢弃→地面物品生成→背包删除→消息发送 顺序与 Delphi 一致
- [ ] 拾取→地面物品删除→背包添加→消息发送 顺序与 Delphi 一致
- [ ] 多人同时拾取 FCFS
- [ ] 背包满/负重不足保留地面物品

**装备交互验收**：
- [ ] 穿戴/脱下/替换与 Delphi 行为一致
- [ ] 满包脱装备失败
- [ ] 属性/外观刷新顺序与 Delphi 一致

**跨系统交互验收**：
- [ ] 商店买卖不产生也不能丢失物品/金币
- [ ] 仓库存取完整
- [ ] 交易取消/完成完整恢复/交换
- [ ] 任务奖励/扣除正确处理

**协议验收**：
- [ ] 所有 SM_* 消息编码与 Delphi 一致
- [ ] 所有 SM_* 消息发送顺序与 Delphi 一致
- [ ] legacy `#...!` 和 client_v1 双通道正确

**安全性验收**：
- [ ] 无法通过伪造输入绕过服务端校验
- [ ] 所有背包操作在玩家逻辑线程内
- [ ] 持久化版本号防旧覆盖

**测试验收**：
- [ ] 所有单元测试通过
- [ ] 所有 protocol smoke 通过
- [ ] golden trace diff = 0
- [ ] 经济一致性检查通过
- [ ] fuzz 10 万次无 invariant 违反

---

## 附录 A: 术语对照

| Delphi | C++ | 说明 |
|--------|-----|------|
| TUserItem | LegacyUserItem | 物品实例结构体 |
| TStdItem | LegacyStdItem / ItemConfig | 物品模板 |
| TAbility | LegacyAbility | 角色属性 |
| TAddAbility | (内联计算) | 装备附加属性 |
| TClientItem | LegacyClientItem | 发送给客户端的数据 |
| TList ItemList | std::array<LegacyUserItem, 46> bag_items | 背包容器 |
| UseItems[0..12] | equipped_items[13] | 装备栏 |
| MAXBAGITEM = 46 | kMaxBagItems = 46 | 背包最大格子数 |
| MakeIndex | make_index | 物品唯一 ID |
| Index | index | 物品模板 ID |
| Dura/DuraMax | dura/dura_max | 持久/最大持久 |
| Desc[0..13] | desc[0..13] | 极品属性数组 |
| StdMode | std_mode | 物品大类 |
| CM_EAT (1006) | kCmEat (1006) | 使用物品请求 |
| SM_ADDBAGITEM (200) | kSmAddItem (200) | 添加物品通知 |
| SM_BAGITEMS (201) | kSmBagItems (201) | 全量背包刷新 |

## 附录 B: Desc[] 升级属性完整对照表

从 Delphi `itmunit.pas` + `ObjBase.pas` 综合：

```
武器 (StdMode 5,6):
  Desc[0] = DC 升级值 (加到 DC.HiByte)
  Desc[1] = MC 升级值 (加到 MC.HiByte)
  Desc[2] = SC 升级值 (加到 SC.HiByte)
  Desc[3] = AC 升级值 (加到 AC.LoByte)
  Desc[4] = MAC 升级值 (加到 MAC.LoByte)
  Desc[5] = HIT(准确) 升级值 (加到 AC.HiByte)
  Desc[6] = 攻击速度: <=10 负速 (加到 MAC.HiByte 直接), >10 正速 (MAC.HiByte + value - 10)
  Desc[7] = 特殊属性: 1~10 武器特殊能力 (SpecialPwr 覆盖)
  Desc[10] = 鉴定标记 (ItemDesc |= $01)

衣服 (StdMode 10,11):
  Desc[0] = AC 升级值 (加到 AC.HiByte)
  Desc[1] = MAC 升级值 (加到 MAC.HiByte)
  Desc[2] = DC 升级值 (加到 DC.HiByte)
  Desc[3] = MC 升级值 (加到 MC.HiByte)
  Desc[4] = SC 升级值 (加到 SC.HiByte)

头盔/项链/手镯/戒指 (StdMode 15,19,20,21,22,23,24,26):
  Desc[0] = AC 升级值 (加到 AC.HiByte)
  Desc[1] = MAC 升级值 (加到 MAC.HiByte)
  Desc[2] = DC 升级值 (加到 DC.HiByte)
  Desc[3] = MC 升级值 (加到 MC.HiByte)
  Desc[4] = SC 升级值 (加到 SC.HiByte)
  Desc[5] = 需求类型: 1=DC 2=MC 3=SC (Need 覆盖)
  Desc[6] = 需求等级/值 (NeedLevel 覆盖)
  Desc[7] = 不可脱下标记 (防脱下)
  Desc[8] = 未鉴定标记 (待确认)
```

## 附录 C: 当前 C++ 测试文件索引

**ModernServer tests (物品相关)**:
- `items_smoke.cpp`
- `item_phase1_compat_smoke.cpp`
- `item_phase2_ground_smoke.cpp`
- `item_phase2_use_smoke.cpp`
- `item_phase3_trade_smoke.cpp`
- `legacy_item_resolution_smoke.cpp`
- `special_consumables_legacy_smoke.cpp`
- `equipment_cantakeon_legacy_smoke.cpp`
- `equipment_derived_upgrade_stats_smoke.cpp`
- `equipment_special_combat_smoke.cpp`
- `player_death_drop_smoke.cpp`
- `weapon_upgrade_smoke.cpp`
- `weapon_luck_lifecycle_smoke.cpp`
- `client_v1_inventory_npc_smoke.cpp`
- `protocol_command_golden_smoke.cpp`

**ModernClient tests (物品相关)**:
- `inventory_ui_smoke.cpp`
- `item_pending_smoke.cpp`

**Golden traces**:
- `tests/golden/npc_shop_phase1/`
- `tests/golden/trade_phase1/`
- `tests/golden/protocol_phase1/`
