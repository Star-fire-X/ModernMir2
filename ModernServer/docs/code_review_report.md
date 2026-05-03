# C++ 传奇服务端（ModernServer）系统代码审查报告

> 审查日期：2026-04-27
> 审查范围：`F:/mir2/ModernServer/` 全部 C++ 源码、配置、共享库及测试

---

## 一、总体结论

当前代码库**已经具备可作为原型继续开发的基础**，但距离能真正复刻老版传奇服务端行为的完整服务端，还有相当距离。

**最大风险**：架构选型偏离了老版传奇服务端「单线程全局大循环 + Tick 驱动」的确定性模型，引入了 Actor Mailbox + Bus + 多 Module 多线程的复杂度，使得行为复刻和问题排查难度显著增加。同时，核心战斗系统（技能、Buff、仇恨、AI）实现过于简化，与老版传奇的实际行为存在大量差异。

**当前状态判断**：代码库更倾向于一个「功能示范原型」而非「可上线服务端」。已有约 40% 的核心游戏逻辑（登录认证、地图加载、基础移动、基础战斗、商店/NPC 系统、行会/沙巴克）是可用且结构合理的，但剩余 60% 的关键系统缺失或过于简化。

---

## 二、严重问题

### 2.1 架构模型与老版传奇不一致

**涉及文件**：`core/local_bus.hpp:13-28`、`core/module.hpp:25-34`、`core/host_runtime.hpp:17-41`

当前 ModernServer 使用 Actor/Bus 模型（`LocalBus` + `Module` + `LogicRuntime` → `MapActor` 独立 Actor），而 Delphi 原始服务端是完全的单线程全局轮询模型（一个主循环依次执行：网络收包→处理队列→角色Tick→怪物Tick→NPC Tick→事件Tick→网络发包）。

`core/local_bus.hpp` 定义了多端点的消息总线，`core/module.hpp` 定义了 Module 抽象，每个 Module 运行在独立线程中。这与 Delphi 的 `TModuleManager.Execute` 单线程大循环完全不一致。

**影响**：当需要对齐 Delphi 行为时，多线程的时序不确定性会使 Bug 排查极其困难。Delphi 代码中的 `ProcessHuman`、`ProcessMonster`、`ProcessNpc` 等函数的执行顺序是严格确定的。

### 2.2 战斗伤害公式过于简化

**涉及文件**：`world/map_actor.cpp:5170-5184`

当前伤害公式极为简化：

```cpp
// 物理伤害 = max(1, 攻击者面板攻击 × 技能倍率 - 目标物理防御)
compute_melee_damage: max(1, attacker.melee_power() * multiplier - target.physical_defense())

// 法术伤害 = max(1, 攻击者法术强度 - 目标魔法防御)
compute_spell_damage: max(1, attacker.spell_power(magic.power) - target.magic_defense())

// 怪物伤害 = max(1, 怪物攻击力 - 目标物理防御)
compute_monster_damage: max(1, monster.attack_power - target.physical_defense())
```

老版传奇的伤害公式远为复杂，包括：
- 随机浮动（通常 ±20%）
- 幸运值影响最大伤害概率
- 诅咒影响
- 命中率判定（`pkg_Hit` 中的命中计算）
- 敏捷回避
- 攻击速度
- 元素属性克制
- BUFF 叠加计算
- 防御减伤带来的非线性收益

当前线性减法公式会导致高等级玩家对低防怪物伤害过高，防御装备收益非线性，与原版体验完全不同。

### 2.3 技能系统严重不完整

**涉及文件**：`world/map_actor.cpp:6815-7019`、`config/models.hpp:160-176`、`protocol/legacy_types.hpp:290-307`

法术处理仅支持单体/范围伤害、DOT、减速、护盾、治疗，但缺失：

- 升级修炼系统：当前 `MagicConfig` 中无技能等级、无熟练度字段
- 技能释放前摇和后摇时间：Delphi 的 `DelayTime` + `DefSpell` + `DefMinPower` 未实现
- 技能书学习流程
- 技能快捷键绑定系统

`config/models.hpp` 的 `MagicConfig` 结构缺少 `LegacyDefMagic` (`legacy_types.hpp:290-307`) 中的关键字段：

| 缺失字段 | 含义 | 重要性 |
|---------|------|--------|
| `effect_type` | 效果类型 | 高 |
| `effect` | 效果值 | 高 |
| `spell` | 施法动画 | 中 |
| `need_level` | 各等级修炼要求 | 高 |
| `max_train` | 各等级修炼上限 | 高 |
| `max_train_level` | 最高技能等级 | 高 |
| `job` | 职业限制 | 高 |
| `delay_time` | 施法冷却 | 高 |
| `def_spell` | 默认施法动画 | 中 |
| `def_min_power` | 默认最小威力 | 中 |
| `max_power` | 最大威力 | 中 |
| `def_max_power` | 默认最大威力 | 中 |

### 2.4 缺少封包加密和完整性校验

**涉及文件**：`protocol/legacy_protocol.cpp:8-15`、`protocol/legacy_protocol.cpp:17-55`

封包编码仅使用 `#` + payload + `!` 作为帧边界，没有任何加密或完整性校验。老版传奇使用 6-bit 编码 (`legacy_edcode.cpp`) 对消息体进行混淆。虽然 `legacy_edcode.hpp` 中提供了这些函数，但仅在部分消息中使用（如 `legacy_game_codec.cpp:52`）。

更严重的是，`legacy_protocol.cpp:38-40` 的 `drain_packets` 函数在解析时强制移除第一个数字字符：

```cpp
if (!payload.empty() && std::isdigit(static_cast<unsigned char>(payload.front())) != 0) {
    payload.erase(payload.begin());
}
```

这是一个奇怪的 hack，会破坏以数字开头的合法消息体。这似乎是针对某类特殊包格式（如 `1...` 前缀）的 workaround，但没有文档说明原因。

### 2.5 保存系统可能在断线时丢失数据

**涉及文件**：`world/game_object.cpp:597-607`、`world/map_actor.cpp:5403-5416`

`Player::on_tick` 每 500 tick 保存一次角色（即每 10 秒，以 20ms tick 计算）。角色断线时在 `despawn` 处理中会最后一次保存。但：

- 如果进程崩溃（非正常断线），自上次保存以来的所有数据都会丢失
- 没有 WAL 保护
- 没有定时定量保存策略
- 没有异常崩溃恢复机制
- 保存请求是 fire-and-forget（`bus->post("persistence_service", ...)`），不等待确认

### 2.6 网络层存在线程竞争风险

**涉及文件**：`services/gateway_service_base.cpp:79-143`、`protocol/game_session.cpp:99-128`

`GatewayServiceBase` 在多个线程间共享状态：
- `sessions_` map 由 `mutex_` 保护
- `io_context_.run()` 在多个线程中执行
- `bus_loop` 在独立线程中运行

`game_session.cpp:99-128` 的 `do_read` 在 Asio 回调中直接调用 `owner_.forward_packet()`，而后者在 `gateway_service_base.cpp:136` 中访问 `context_->bus->post()` —— 这是一个跨线程调用且不持有 `mutex_` 的操作。虽然 `LocalBus::post` 内部有锁，但 `sessions_` map 的并发访问保护不够完整。

---

## 三、中等问题

### 3.1 NPC/商店交互逻辑占据代码量过大

**涉及文件**：`world/map_actor.cpp:5487-6101`

`map_actor.cpp` 约 8000 行代码中，约 2500+ 行用于 NPC 对话框系统。行会和沙巴克的管理逻辑非常详尽，但这可能是「过早完善」——基础战斗系统尚未完整就大量投入行会管理。

### 3.2 MapActor::tick 的 Budget 机制逻辑可疑

**涉及文件**：`world/map_actor.cpp:5257-5290`

使用 wall-clock 时间做预算控制：

```cpp
const auto start = std::chrono::steady_clock::now();
// ...处理对象...
const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(...).count();
consumed_budget[object.kind()] += static_cast<std::uint64_t>(elapsed);
```

这是 per-map 的累计预算，意味着当某个 Map 中玩家对象数量很多时，后面的对象 tick 会被延迟到下一帧。这会导致：
- 不同 Map 的玩家体验不一致
- 预算消耗包含数据库 I/O 时间（在 tick 中通过 `persist_requests` 触发保存）
- `budget_for()` 返回的是毫秒值但预算基于 wall-clock 而非逻辑帧

### 3.3 视野广播是遍历所有玩家

**涉及文件**：`world/map_actor.cpp:382-391`

`for_each_player` 模板函数遍历所有玩家，没有九宫格优化。战斗打击包发送给全图所有玩家（`map_actor.cpp:6803-6808`）。老版传奇服务端使用九宫格分区广播来减少网络包数量，全图广播在玩家数量较多时（50+ 同屏）会成为性能瓶颈。

### 3.4 怪物 AI 过于简单

**涉及文件**：`world/map_actor.cpp:7529-7644`

`handle_monster_ai` 只有：
1. 检查/获取仇恨目标（`aggro_target` 或 `last_hitter` 或最近玩家）
2. 如距离 ≤ 1 则攻击
3. 否则向目标移动一步

缺少：
- 巡逻/守卫 AI
- 技能释放 AI
- 逃跑逻辑（低血量）
- 召唤/分裂行为
- 区域守卫 / 主动攻击 / 被动 区分
- Boss AI 阶段切换
- 掉落物生成逻辑

### 3.5 角色进出地图逻辑缺失

**涉及文件**：`world/logic_runtime.cpp:353-396`、`world/map_actor.cpp:5418-5421`

`route_logic_command` 中 `enter_world` case 只处理进入地图，但：
- 没有跨图传送处理（`ActorMailKind::transfer` 只从旧 Map 移除并放入 cross_map_mails，但未见接收端处理）
- `resolve_map_id` 使用简单的 map_id 查找，不支持地图连接关系
- 没有地图传送门、地图切换点等概念

### 3.6 仓库/背包系统无完整的约束验证

**涉及文件**：`world/game_object.cpp:177-188`、`world/game_object.cpp:269-276`

`can_add_bag_item` 会检查重量和槽位，但 `add_bag_item` 仅找空槽位而不验证重量。从商店购买时 `buy_item` 调用了 `can_add_bag_item`，但从仓库取回物品（`map_actor.cpp` 中的 `storage_item` / `take_back_storage_item`）没有同样检查。

### 3.7 Tick 中间状态的持久化未考虑一致性

**涉及文件**：`world/game_object.cpp:597-607`、`services/persistence_service.cpp:81-91`

在 Player tick（每500帧）中直接保存角色，这发生在 tick 循环内部。`PersistenceService` 虽然是独立线程，但请求是 fire-and-forget 的，没有 back-pressure 控制。如果 SQLite 写入很慢，消息队列会堆积。

### 3.8 经验曲线不合理

**涉及文件**：`world/game_object.cpp:58-60`

```cpp
std::uint32_t next_level_exp(std::uint8_t level) {
    return static_cast<std::uint32_t>(std::max<std::int32_t>(100, static_cast<std::int32_t>(level) * 100));
}
```

这意味着从 Level 2 到 Level 3 只需要 300 经验，Level 10 到 Level 11 只需要 1100 经验。这与老版传奇的指数增长经验曲线完全不同，会导致升级速度过快。

---

## 四、轻微问题

### 4.1 代码重复

`apply_runtime_castle_defaults` 在两个文件中重复定义：
- `world/logic_runtime.cpp:22-255`
- `services/world_service.cpp:85-318`

约 230 行完全相同的代码。

### 4.2 魔法数字散落各处

| 位置 | 值 | 含义 |
|------|-----|------|
| `map_actor.cpp:23` | `255` | 默认名字颜色 |
| `map_actor.cpp:27` | `100` | 跨地图同步重试限制 |
| `map_actor.cpp:28` | `6` | NPC 对话框每页条数 |
| `map_actor.cpp:34-37` | - | 行会头衔硬编码数组 |
| `game_object.cpp:333-334` | `250, 250` | 走和跑的基础间隔（均为 250ms，走和跑无区别） |
| `game_object.cpp:60` | `level * 100` | 升级经验曲线 |
| `game_object.cpp:597` | `500` | 保存间隔 tick 数 |
| `auth_service.cpp:16` | `2` | 最大角色槽位 |
| `auth_service.cpp:17` | `5000` | 账号操作限流时间 |

### 4.3 命名不一致

- `Monster::magical_defense()` 使用美式拼写
- `Player::magic_defense()` 使用不同缩写
- `actor_physical_defense()` 在 `map_actor.cpp` 中使用 `actor_` 前缀，但在 `game_object.hpp` 中使用 `player->physical_defense()`
- `LegacyUserItem.prefix[13]` 中的 `prefix` 字段命名令人困惑（与装备前缀/后缀无关）

### 4.4 LegacyPacket 头字段大量未使用

**涉及文件**：`core/messages.hpp:18-26`、`protocol/legacy_protocol.cpp:17-55`

`LegacyPacketHeader` 定义了多个字段（`code`, `socket_number`, `user_gate_index`, `ident`, `user_list_index`, `temp`, `length`），但在实际封包解析中这些字段除了 `length` 外几乎都被忽略或赋零值。如果后续需要兼容网关层（LoginGate/RunGate），这些字段的值必须正确填充。

### 4.5 TOML 配置加载缺少验证

**涉及文件**：`config/config_loader.cpp`

配置加载没有验证：
- `tick_ms` 是否为正数
- `port` 是否在有效范围 (1-65535)
- `budget_ms` 是否合理
- `map_id` 是否唯一
- `address` 是否为有效 IP
- 地图 `source_map` 文件是否存在

### 4.6 测试覆盖范围有限

**涉及文件**：`tests/` 目录下的 36 个 smoke 测试

现有测试以 Smoke Test 为主（功能冒烟验证），缺乏：
- 单元测试（如 damage 公式测试）
- 边界条件测试（如满背包、零金币等场景）
- 压力测试（如多个玩家同时操作）
- 回归测试套件

---

## 五、过度设计分析

### 5.1 Module/Bus 架构过度工程化（影响：高）

**涉及文件**：`core/module.hpp`、`core/local_bus.hpp`、`core/host_runtime.hpp`

一个通用的「微服务总线」架构，每个 Module 运行在独立线程，通过 `LocalBus::post()` 做异步消息传递。

对于老版传奇服务端而言，这是过度设计。Delphi 服务端就是一个简单的大循环：

```pascal
while not Terminated do begin
  ProcessNetwork;
  ProcessMessages;
  ProcessHumans;
  ProcessMonsters;
  ProcessNpcs;
  ProcessEvents;
  FlushPackets;
end;
```

**建议**：保持现有的 Module 分离（auth/world/persistence/log 的职责划分是合理的），但 WorldService 内部应该回归单线程确定性模型。不需要 `LogicRuntime` → `MapActor` 的二级 Mailbox 系统。

### 5.2 LogicCommand → ActorMail 的双层消息转换（影响：中）

**涉及文件**：`services/world_service.cpp:338-504`、`world/logic_runtime.cpp:349-538`、`world/map_actor.cpp:5313-7415`

先解析网关包为 `LogicCommand`（带详细枚举），再转换为 `ActorMail`（又一个详细枚举），再做 Switch 分发。双层转换增加了间接性，且 `ActorMailKind` 和 `LogicCommandKind` 有大量重叠（约 20 个枚举值完全一致）。

老版传奇服务端在收到客户端包后直接调用相应函数（`SendMsg` → `GetMsg` → 直接操作对象）。不需要经过两层消息转换。

### 5.3 CastleDialogContext / RuntimeConfig 过度参数化（影响：低）

**涉及文件**：`config/models.hpp:183-246`、`config/models.hpp:10-89`、`config/config_loader.cpp:460-613`、`world/logic_runtime.cpp:22-255`

`CastleDialogContext` 有 50+ 个字符串字段用于存储各种模板文本。虽然可配置性是好的，但实现方式（每个模板一个字段）导致配置加载代码和运行时默认值填充代码非常冗长。在老版传奇中，大部分提示文本是硬编码的。

### 5.4 对象系统的虚函数层次（影响：低）

**涉及文件**：`world/game_object.hpp`

定义了 `GameObject` 基类，派生 `Player`、`Monster`、`Npc`、`EventObject`。但 `on_tick` 和 `on_mail` 虚函数的分发逻辑实际上与直接的 switch/if-else 没有本质区别，因为所有对象的存储容器是 `std::unordered_map<uint64_t, std::unique_ptr<GameObject>>`。

Delphi 的做法是使用不同的 TList 分别管理：`HumansList`, `MonsterList`, `NpcList` 等，在处理时直接用 `TList.Objects[i]` 强转。分离的 List 更有利于批量处理和空间分区。

---

## 六、缺失模块与缺失能力

### 按优先级排列

| 优先级 | 缺失能力 | 说明 | 参考 Delphi 源码 |
|--------|---------|------|-----------------|
| **P0** | 技能书/学习/修炼系统 | `MagicConfig` 缺少技能树、等级、熟练度 | `TUserMagic` |
| **P0** | 怪物掉落系统 | 无掉落表、无随机掉落、无物品爆出逻辑 | `TMonster.Die` |
| **P0** | PK/善恶系统 | 仅有 `allow_pk` 标志位 | `TBaseObject.PKLevel` |
| **P0** | 组队系统 | 完全缺失 | `TGroup` |
| **P0** | 交易系统 | 玩家间直接交易缺失 | `TUserMsg.Deal` |
| **P1** | 脚本引擎 | 所有 NPC 行为硬编码在 C++ switch 中 | `TMarketScript` |
| **P1** | 行会战/沙巴克攻城完整逻辑 | 行会管理框架已有但战争机制不完整 | `TGuild` / `TCastle` |
| **P1** | 任务/事件系统 | `EventObject` 类为空壳 | `TEventManager` |
| **P1** | 九宫格视野广播优化 | 当前全图广播 | `SendRefMsg` range 参数 |
| **P1** | 怪物 AI 增强 | 巡逻/守卫/技能释放/逃跑/掉落物 | `TMonsterAI` |
| **P2** | 邮件系统 | 缺失 | - |
| **P2** | 排行榜 | 缺失 | - |
| **P2** | 师徒/婚姻系统 | 缺失 | - |
| **P2** | 封包加密/网关兼容 | 需与 RunGate/SelGate 协议对齐 | `TEncode` / `TDecode` |
| **P3** | 控制台管理命令 | 缺失 GM 命令系统 | `TManage` |
| **P3** | 游戏内置公告/活动系统 | 缺失 | `TNotice` |

---

## 七、修改建议

### P0 — 必须立刻修复

| # | 问题 | 涉及文件 | 说明 |
|---|------|---------|------|
| 1 | 修复 damage 公式 | `world/map_actor.cpp:5170-5184` | 引入随机浮动（±20-30%）、幸运值影响、命中率判定。参考 Delphi `TMagic.AttackPower` 和 `TBaseObject.GetHitDamage` |
| 2 | 修复封包解析 Bug | `protocol/legacy_protocol.cpp:38-40` | 删除无注释的首字符移除逻辑 |
| 3 | 添加异步持久化确认 | `world/game_object.cpp:597-607`, `world/map_actor.cpp:5403-5416` | 改为「脏标记 + 最小间隔」模式，despawn 时阻塞确认保存完成 |
| 4 | 补全 MagicConfig 字段 | `config/models.hpp:160-176` | 对齐 `LegacyDefMagic` 结构，至少包括 `need_level`, `max_train`, `job`, `delay_time` |

### P1 — 开发前应该修复

| # | 问题 | 涉及文件 | 说明 |
|---|------|---------|------|
| 5 | 回归单线程世界模型 | `world/logic_runtime.*`, `services/world_service.*` | 移除 LogicRuntime 的内部 Mailbox 队列，MapActor tick 直接在 WorldService run 循环中顺序调用 |
| 6 | 实现九宫格视野广播 | `world/map_actor.cpp:382-391` | 替换 `for_each_player`，基于 9x9 网格分区索引 objects_ |
| 7 | 添加怪物掉落系统 | `world/map_actor.cpp:7504-7527` | 至少支持配置化的固定掉落表和随机掉落表 |
| 8 | 添加技能学习/修炼系统 | `world/game_object.*`, `config/models.hpp` | UseMagic 等级的持久化和升级逻辑 |
| 9 | 实现 PK/善恶点数系统 | `world/map_actor.cpp` 中的 `resolve_pk_block_reason` | 红名惩罚、善恶值变化规则 |
| 10 | 修复经验曲线 | `world/game_object.cpp:58-60` | 替换为 Delphi 同款指数曲线 |

### P2 — 后续优化

| # | 问题 | 涉及文件 |
|---|------|---------|
| 11 | 移除 `LogicCommandKind` → `ActorMailKind` 双重枚举转换 | `world/logic_runtime.cpp`, `world/map_actor.cpp` |
| 12 | 统一 `apply_runtime_castle_defaults` 到单一位置 | `world/logic_runtime.cpp`, `services/world_service.cpp` |
| 13 | 对话框模板移到 Lua 或 JSON 配置 | `config/models.hpp`, `world/map_actor.cpp` |
| 14 | 实现基于 NPC 脚本文件的脚本引擎 | `config/npc_scripts/market_def/*.txt` |
| 15 | 添加组队系统 | 新增 `world/party_system.*` |
| 16 | 修复 MonPlayer 走和跑的间隔 | `world/game_object.cpp:333-334`（当前走和跑均为 250ms） |

### P3 — 可暂时保留

| # | 说明 |
|---|------|
| 17 | 保持 Module/Bus 架构的高层分离（auth/persistence 的线程隔离是合理的） |
| 18 | 保持 TOML 配置系统的可配置性 |
| 19 | 保持现有的虚函数对象模型 |
| 20 | 添加 TOML 配置验证逻辑 |

---

## 八、建议的重构方向

### 8.1 短期 — 对齐 Delphi 行为

核心思路：**在 WorldService 内部回归 Delphi 的确定性单线程模型**。

```cpp
WorldService::run() {
    while (running) {
        // 1. 排空所有入站消息（从 Bus 队列）
        drain_inbound_messages();

        // 2. 全局 Tick（严格顺序）
        ++global_tick;
        for (auto& [id, map] : maps) {
            map->process_players(global_tick);      // 依次 ProcessHuman
            map->process_monsters(global_tick);     // 依次 ProcessMonster
            map->process_npcs(global_tick);         // 依次 ProcessNpc
            map->process_events(global_tick);       // 依次 ProcessEvent
        }

        // 3. 集中排空出站包（确保顺序）
        flush_all_pending_packets();

        // 4. 定时持久化（异步 + 确认）
        flush_dirty_characters();
    }
}
```

这样改的好处：
- 行为完全确定，与 Delphi 模型一致，方便对照验证
- 无需各对象内部的状态机/Tick调度，逻辑在 MapActor 层面直接驱动
- 网络包处理在帧的明确时刻执行，不会出现在 tick 中间收到不一致状态

### 8.2 中期 — 优化可维护性

1. **引入 Lua 脚本引擎**用于 NPC 对话框逻辑。将已有 `config/npc_scripts/market_def/` 目录下的 `.txt` 文件格式（`[@main]` 段落格式）解析为 Lua 表，在 C++ 层面只保留核心战斗/移动/物品逻辑。

2. **将重复的辅助函数提取到独立文件**：
   - `world/item_utils.hpp` — `item_name`, `find_item_config`, `packed_min/max` 等
   - `world/actor_utils.hpp` — `actor_hp`, `actor_max_hp`, `actor_level` 等
   - `world/castle_utils.hpp` — `apply_runtime_castle_defaults` 等

3. **统一伤害系统**：创建 `world/combat_engine.hpp` 作为战斗计算的唯一入口，包含完整的 Delphi 公式实现（随机浮动、命中率、暴击、防御减伤曲线）。

### 8.3 长期 — 架构演进

1. **MapActor 应管理 Map 级别的空间索引**（网格或四叉树），替换当前的 `objects_` 线性查找和 `find_attack_target_*` 的 O(n) 遍历。

2. **PersistenceService 应实现 Write-Ahead Log**（利用 SQLite 的 WAL 模式），保证崩溃恢复。保存操作应改为批量提交（如每 30 秒 flush 一次）。

3. **实现网关层兼容**：当需要对接原始 RunGate/LoginGate 时：
   - `LegacyPacketHeader` 的字段必须正确填充
   - 封包编码需要与 Delphi 的 `TSendMsg` → `EncodeBuf` 流程完全一致
   - 6-bit 编码必须应用于所有消息体（当前仅在部分消息中使用）

4. **将配置系统迁移为数据库驱动**：将 `ItemConfig`、`MagicConfig`、`SpawnConfig` 等从 TOML 文件迁移到 SQLite 表，支持热更新（无需重启服务端）。

---

## 附录 A：当前文件清单与模块映射

### 源码文件（按模块）

| 目录 | 文件 | 行数（估计） | 对应 Delphi 模块 |
|------|------|------------|-----------------|
| `src/app/` | `main.cpp` | 123 | `M2Server.dpr` |
| `src/core/` | `host_runtime.*` | ~140 | `TModuleManager` |
| `src/core/` | `local_bus.hpp` | 80 | (新增，Delphi 无) |
| `src/core/` | `messages.hpp` | 416 | `M2Share.pas` 消息定义 |
| `src/core/` | `bounded_mpsc_queue.hpp` | 76 | (新增) |
| `src/core/` | `wheel_timer.hpp` | 45 | (新增) |
| `src/config/` | `config_loader.*` | ~660 | `TConfig` |
| `src/config/` | `models.hpp` | 284 | 配置数据结构 |
| `src/protocol/` | `legacy_protocol.*` | ~70 | `TSendMsg`, `TGetMsg` |
| `src/protocol/` | `legacy_edcode.*` | ~160 | `EncodeBuf`, `DecodeBuf` |
| `src/protocol/` | `legacy_game_codec.*` | ~70 | `EncodeMessage` |
| `src/protocol/` | `legacy_types.hpp` | 363 | `M2Share.pas` 类型定义 |
| `src/protocol/` | `game_session.*` | ~170 | `TUserEngine` Socket |
| `src/services/` | `auth_service.*` | ~930 | `TLoginSvr` 部分 |
| `src/services/` | `world_service.*` | ~900 | `TM2Server` 主逻辑 |
| `src/services/` | `persistence_service.*` | ~330 | `TDBServer` |
| `src/services/` | `gateway_service_base.*` | ~280 | `TUserEngine` |
| `src/services/` | `game_gateway_service.*` | ~20 | RunGate 替代 |
| `src/services/` | `login_gateway_service.*` | ~20 | LoginGate 替代 |
| `src/services/` | `log_service.*` | ~45 | 日志系统 |
| `src/world/` | `game_object.*` | ~740 | `TBaseObject`, `TUserPlay`, `TMonster`, `TNpc` |
| `src/world/` | `logic_runtime.*` | ~610 | `TModuleManager.Execute` |
| `src/world/` | `map_actor.*` | ~8000 | `TMap` + 所有对象交互 |
| `src/storage/` | `repository.*` | ~800 | `TDBServer.SaveChar` |
| `src/importer/` | `legacy_importer.*` | 未详查 | 导入工具 |
| `src/util/` | `logger.hpp` | 32 | 日志工具 |
| `src/util/` | `string_utils.hpp` | 47 | 字符串工具 |
| `shared/legacy/` | `map_document.hpp` | 123 | `TMap.ReadMapFile` |
| `shared/legacy/` | `movement_rules.hpp` | 196 | `TBaseObject.MoveTo` |

### 测试文件

`tests/` 目录下有 36 个 Smoke 测试文件，覆盖了账户、认证、战斗、Buff、物品、商店、行会、PK、移动、护盾等基本场景。

---

## 附录 B：Delphi 源码对比关键差异

| 功能 | Delphi 实现 | ModernServer 实现 | 差异评估 |
|------|------------|-------------------|---------|
| 主循环 | 单线程 `Execute` 大循环 | 多 Module + Bus + Actor Mailbox | 行为不确定 |
| 对象管理 | 分离的 TList (Humans, Monsters, Npcs, Events) | 统一 `unordered_map<uint64_t, unique_ptr<GameObject>>` | 可用但查找效率低 |
| 技能系统 | `TUserMagic.Level/TranPoint`, `TMagic.DelayTime`, `CurtrainPoint` | 简化 `MagicConfig` 无等级/修炼 | 严重不完整 |
| 伤害公式 | `GetHitDamage` 含随机浮动、幸运、命中 | 简单线性减法 | 体验完全不同 |
| 封包编码 | `EncodeBuf` 6-bit 编码所有消息体 | 部分使用 6-bit，部分明文 | 安全性不足 |
| NPC 脚本 | 外部 `.txt` 文件 + `TMarketScript` 解析 | 硬编码在 C++ switch 中 | 可维护性差 |
| 物品 DB | Access MDB / ODBC | SQLite | 更好 |
| 地图格子 | `TMapCell` 12 字节 | `legacy::MapCell` 12 字节 | 一致 |
| 方向系统 | 8 方向 (0-7) | 8 方向，`next_direction` 一致 | 一致 |
| 装备槽位 | 13 槽位 (Dress-Helmet...) | 13 槽位 | 一致 |

---

> 审查结论：代码库已有良好基础，具备继续开发条件。建议优先将战斗核心和世界逻辑回归确定性单线程模型，以利后续对齐 Delphi 原始服务端行为。
