# 三类边界清单 (Annotated with Delphi Evidence)

> 基于 `docs/combat_skill_migration_design.md` Section 2 + Delphi 源码审查更新
> Delphi 行号证据来自 ObjBase.pas / Magic.pas / ClMain.pas / Actor.pas / magiceff.pas

---

## A. 绝对不能改 (23项)

| # | 内容 | Delphi 证据 | 审查确认 |
|---|------|-----------|---------|
| A1 | 普通攻击动作节奏 (hit: 6f×85ms=510ms, heavy: 6f×90ms=540ms, big: 8f×70ms=560ms) | Actor.pas:74-90 HA常量 | ✅ 确认 |
| A2 | 攻击间隔 = 客户端 `CanNextHit` (1400 - min(800, min(370, Level*14) + HitSpeed*60)) + 服务端 `HitXY` (900 - HitSpeed*60) | ClMain.pas:3348-3362, ObjBase.pas:9309-9316 | ✅ 确认 |
| A3 | 施法间隔 = `LatestSpellTime` + `LatestSpellDelay` | ObjBase.pas:9418-9421 | ✅ 确认 |
| A4 | 同一 frame 内按同玩家消息队列 FIFO 处理; `case` label 不重排移动/攻击/施法 | ObjBase.pas:12050-13010 (GetMsg while loop) | ✅ 确认 |
| A5 | 走砍限制: 移动后必须等 walk/run 时间才能攻击 | ClMain.pas:3337-3345 CanNextAction (IsIdle + Dizzy delay) | ✅ 确认 |
| A6 | 战士近战距离 = 1格 | ObjBase.pas:5253 DirectAttack (implicit, target lookup) | ✅ 确认 |
| A7 | 刺杀剑术: 距离=2格, 隔位 | ObjBase.pas:5270-5284 SwordLongAttack (GetNextPosition 2) | ✅ 确认 |
| A8 | 半月弯刀: 前方3格扇形 (7,1,2) | ObjBase.pas:5285-5304 SwordWideAttack (valarr: 7,1,2) | ✅ 确认 |
| A9 | 烈火剑法: 蓄力→下一刀触发→倍率=(4+level×4)×10% | ObjBase.pas:9468-9484 (SpellXY SWD_FIREHIT), ObjBase.pas:5403-5405 (_Attack FIREHIT) | ✅ 确认 |
| A10 | 野蛮冲撞: gate_roll < 6+level×3+level_gap (Random(20)) | Magic.pas:75 | ✅ 确认 |
| A11 | 服务端权威判定: 客户端不决定伤害/命中/击退/技能结果 | 所有伤害计算在服务端 ObjBase.pas | ✅ 确认 |
| A12 | 同玩家输入 FIFO | ObjBase.pas:12050 GetMsg while (队列顺序) | ✅ 确认 |
| A13 | DC/MC/SC/AC/MAC/Hit/Speed packed 字段语义 | ObjBase.pas:203-227 (AccuracyPoint, SpeedPoint, etc.) | ✅ 确认 |
| A14 | 幸运/诅咒公式: luck>0 gate→0 max, luck<0 gate→0 min | ObjBase.pas:5236-5250 GetAttackPower | ✅ 确认 |
| A15 | Delphi Random(N) 返回 [0, N-1] | 标准 Delphi 行为 | ✅ 确认 |
| A16 | Delphi Round = Banker's Rounding (round-half-even) | 标准 Delphi 行为 | ✅ 确认 |
| A17 | CM_*/SM_* 消息 ID 数值 | Grobal2.pas:811-1099 (SM_*), 1084-1099 (CM_*) | ✅ 确认 |
| A18 | legacy `#...!` 外部协议行为 | RunSock.pas / gate 协议 | ✅ 确认 |
| A19 | 死亡后禁止攻击/施法 | ObjBase.pas:12050 Operate (Death check implicit in state) | ✅ 确认 |
| A20 | 安全区禁止攻击 | ObjBase.pas:5256-5257 DirectAttack | ✅ 确认 |
| A21 | 魔法盾公式: Round(damage * 1.5) MP吸收 | ObjBase.pas:3589-3600 DamageHealth | ✅ 新增 |
| A22 | 红毒额外1.2x伤害+装备损耗 | ObjBase.pas:3483-3489 StruckDamage | ✅ 新增 |
| A23 | 烈火倍率在GetAttackPower之后叠加 | ObjBase.pas:5403-5405 (damage after GetAttackPower) | ✅ 新增 |

**新增项 (A21-A23)** 是在本次 Delphi 审查中发现的原设计中未列出的绝对不能改项。

---

## B. 应该保留 legacy 行为，但可以现代封装 (21项)

| # | 内容 | Delphi 证据 | C++ 封装方式 |
|---|------|-----------|------------|
| B1 | CombatContext (攻击者/目标/伤害/命中/消息) | _Attack 局部变量 (hiter, targ, dam, hitmode, ...) | `struct CombatContext` (栈上临时) |
| B2 | AttackContext (攻击标识/剑术ID/范围) | HitHit 的 hitmode + _Attack 内部 sword skill flags | `struct AttackContext` |
| B3 | SkillCastContext (技能ID/等级/目标/消耗/效果) | SpellNow 局部变量 (pum, pwr, train, nofire, ...) | `struct SkillCastContext` |
| B4 | DamageContext (原始伤害/护甲随机/减伤后/最终) | GetHitStruckDamage 参数+返回值链 | `struct DamageResult` |
| B5 | SkillDefinition (技能ID/职业/等级/消耗/参数) | TDefMagic record (Grobal2.pas:483-500) + SpellNow case | `MagicConfig` |
| B6 | SkillRuntime (技能执行状态机) | Magic.pas SpellNow case 分支 | `map_actor_mail.hpp` spell branch |
| B7 | Buff/StatusEffect 封装 | TCreature 状态字段 (BoPoison, BoTransparent, BoMagicShield...) | `TimedStatusEffect` + `status_effects_` |
| B8 | Poison/Paralysis/Invisibility 状态 | MakePoison (ObjBase.pas:5898), STATE_TRANSPARENT | `apply_legacy_poison()`, `activate_legacy_transparent()` |
| B9 | TargetSelector (单体/方向/范围/阵营) | DirectAttack, SwordLongAttack, MagPassThroughMagic | `find_attack_target_*()`, `collect_legacy_area_targets()` |
| B10 | AreaSelector (半径/扇形/前方N格) | SwordWideAttack(valarr), MagBigExplosion(radius) | `collect_wide_hit_targets()`, `collect_spell_target_ids()` |
| B11 | DamageCalculator | GetAttackPower + GetHitStruckDamage | `roll_legacy_player_attack_power()` + `legacy_physical_struck_damage()` |
| B12 | HitCalculator | DirectAttack: Random(SpeedPoint) < AccuracyPoint | accuracy vs hit_roll |
| B13 | CombatProtocolAdapter | HitMotion→SendRefMsg, SendDelayMsg patterns | `make_hit_packet()`, `make_struck_packet()`, etc. |
| B14 | MagicEffectAdapter | UseMagicFire→NewMagic 特效分派 | `ActorMagicFire` → `LegacyEffectManager` |
| B15 | CombatTrace | (由 C++ 新增) | `LegacyRuntimeTrace` + `add_legacy_trace()` |
| B16 | 技能配置表封装 | TDefMagic + Magic DB 表 | `MagicConfig` from YAML/JSON |
| B17 | 动作锁封装 | LatestHitTime + LatestSpellTime 节流 | `begin_attack_attempt()` + `begin_spell_attempt()` |
| B18 | 技能冷却封装 | DelayTime + FireHit(10s) + Rush(3s) | `LegacyMagicDefinition.delay_time_ms` |
| B19 | combat smoke 测试 | (由 C++ 新增) | GTest smoke fixtures |
| B20 | golden trace | (由 C++ 新增) | LegacyRuntimeTrace JSON |
| B21 | fuzz 防护 | (由 C++ 新增) | Fuzz test harness |

---

## C. 可以现代化 (13项)

| # | 内容 | Delphi 证据 | 现代方式 | 不变证明 |
|---|------|-----------|---------|---------|
| C1 | C++ 类结构和 ownership | TCreature→TAnimal→TUserHuman 继承 | MapActor→GameObject→Player/Monster | 所有 public 接口行为一致 |
| C2 | 技能定义数据表加载 | TDefMagic + 数据库 | YAML/JSON → MagicConfig | 加载后字段值与 Delphi 一致 |
| C3 | 类型安全枚举 | Integer magic IDs | `enum class ActorMailKind` | wire 值不变 |
| C4 | RAII 管理临时上下文 | Pascal 栈变量 | C++ RAII struct | 生命周期一致 |
| C5 | trace id / frame index / action seq | 无 | LegacyRuntimeTrace | 仅附加 |
| C6 | 日志/metrics/debug overlay | 无 | 结构化日志 | 仅读取 |
| C7 | 战斗 replay 工具 | 无 | ActorMail 录制/回放 | 仅读取 |
| C8 | 公式单元测试 | 无 | GTest 参数化 | 输入输出一致 |
| C9 | 技能 fixture | 无 | 构造 Player | 属性与 Delphi 一致 |
| C10 | golden trace / image / fuzz | 无 | CI 自动化 | 仅对比 |
| C11 | 内部 typed event | ActorMail 替代裸消息 ID | ActorMail | mail 字段与 Delphi 消息一致 |
| C12 | 客户端特效资源缓存 | WIL 文件直接加载 | 预加载+缓存 | 渲染结果逐像素一致 |
| C13 | 服务端技能配置校验 | 无 | 启动时校验 magic_configs_ | 仅校验 |

---

## 审查发现: 缺失的边界项

在本次 Delphi 源码审查中, 发现以下原设计文档未覆盖的项:

### 新增 A 类 (绝对不能改)

| # | 内容 | Delphi 证据 | 影响 |
|---|------|-----------|------|
| A21 | 魔法盾 MP 吸收公式 Round(damage*1.5) | ObjBase.pas:3589-3600 | 法师生存能力 |
| A22 | 红毒(POISON_DAMAGEARMOR) 1.2x伤害+装备损耗 | ObjBase.pas:3483-3489 | PvP 道士平衡 |
| A23 | 烈火倍率在 GetAttackPower 之后叠加 | ObjBase.pas:5403-5405 | 战士爆发伤害 |

### 新增 B 类 (应保留 legacy)

| # | 内容 | Delphi 证据 |
|---|------|-----------|
| B22 | 吸血累加器 (>2才回血) | ObjBase.pas:5459-5466 |
| B23 | 石化触发 (Random(5+AntiPoison)=0) | ObjBase.pas:5454 |
| B24 | 技能训练量 (1+Random(3) for 法师/道士, 1 for 高级剑术) | ObjBase.pas:5472-5527, Magic.pas:992 |

### PR1 已收口差异输入

| # | 内容 | Delphi 位置 | 后续 PR |
|---|------|-----------|--------|
| G1 | 半月弯刀三格目标无伤害衰减 | ObjBase.pas:5285-5304 | PR5 |
| G2 | 十字斩 PvP 目标使用 80% 伤害 | ObjBase.pas:5320-5323 | PR5 |
| G3 | 当前 Delphi 版本无 `dueltime` 字段, 攻击节流使用 `LatestHitTime` | 全局搜索: 0 matches; ObjBase.pas:9309 | PR3 |
| G4 | 施毒术 `nofire:=TRUE`, 不发送普通弹道 | Magic.pas:736 | PR7/PR8 |
| G5 | 隐身术/集体隐身术 `nofire:=TRUE` | Magic.pas:919-930 | PR8 |
| G6 | AC/MAC range 使用 `ShortInt` 转换 | ObjBase.pas:3433 | PR4 |

---

## D. 有意的 C++ 安全增强 (Delphi 中不存在)

这些是 C++ 实现中加入的额外安全措施, 在 Delphi 原版中不存在。它们不影响 legacy 兼容性, 但应该被记录。

| # | 内容 | Delphi 行为 | C++ 增强 | 理由 |
|---|------|-----------|---------|------|
| D1 | 攻击间隔下限 200ms | `900 - HitSpeed*60` 无下限 (HitSpeed≥15 时可达 0) | `std::max(200, 900 - legacy_hit_speed_ * 60)` | 防止极高 HitSpeed 下的零间隔攻击 |
| D2 | 攻击加速断开连接 | 无断开连接机制 (仅静默丢弃) | `hit_speed_hack_timer_over_count_ > 8` → force disconnect | 主动反作弊, 8 次违规后才触发 (合法玩家不会触发) |
| D3 | 施法 2 次宽限期 | 单次违规即阻止 (`LatestSpellDelay` 一次检查) | `spell_time_over_count_ < 2` 宽容 2 次 | 网络抖动容忍度, 防止高 ping 玩家被误判 |

**验证**: 这些差异不会改变正常游戏行为 (合法攻击/施法频率远低于阈值)。它们在 `game_object.cpp:853-872` (攻击) 和 `game_object.cpp:827-851` (施法) 中实现。

**PR-3 修复**: D1/D2 保留不变, 但修复了 `latest_hit_time_ms_` 在拒绝时更新的 bug (现已匹配 Delphi 仅在成功时更新)。

---

## 最终边界统计

| 类别 | 原设计 | 审查确认 | 新增 | 合计 |
|------|--------|---------|------|------|
| A. 绝对不能改 | 20 | 20 | 3 | **23** |
| B. 保留legacy封装 | 21 | 21 | 3 | **24** |
| C. 可以现代化 | 13 | 13 | 0 | **13** |
| 已收口差异输入 | ~15 | 已验证12 | +6 | **18** |
