# PR1 收口清单

> PR1 范围: 本清单只冻结 Delphi 战斗/技能 expected behavior, 不修改 C++ 运行时代码。
> `Source/M2Server` 是主要证据源; `Source/Mir200` 只用于双源差异记录。
>
> 状态含义:
> - `confirmed`: Delphi 语义已从源码确认, 可作为后续 PR 的行为基线。
> - `known_gap`: Delphi 语义已确认, C++ 对齐情况存在差异或风险, 交给后续 PR 处理。
> - `deferred_pr10`: 视觉、音效或渲染细节延期到 PR10。

---

## P0 — 攻击/施法时序

| ID | 结论 | Delphi 证据 | C++ 影响 | 后续 PR | 阻塞 |
|---|---|---|---|---|---|
| P0-1 服务端攻击间隔 | `HitXY` 使用 `LatestHitTime` 和 `900 - HitSpeed*60`; 成功攻击会刷新 `LatestHitTime`。 | `ObjBase.pas:9302-9316`; 详见 `attack_call_chain.md:§3.1`, `formula_tables.md:F3` | C++ `begin_attack_attempt()` 需保持同公式; 当前额外 200ms 下限属于已知安全增强。 | PR3 验证节流公式和无效攻击是否消耗冷却 | no |
| P0-2 客户端 `CanNextHit` | 客户端发送攻击前使用 `1400 - min(800, min(370, Level*14) + HitSpeed*60)`。 | `ClMain.pas:3348-3362`; 详见 `attack_call_chain.md:§2.1`, `formula_tables.md:F2` | C++ 客户端 `can_next_hit()` 已按该公式实现; 服务端公式不同是 Delphi 本身的双层门。 | PR3 保持客户端/服务端双层节奏测试 | no |
| P0-3 攻击动作和伤害顺序 | 攻击流程先进入 `HitHit`, 执行 `_Attack`, 再 `HitMotion` 广播动作; `_Attack` 内部可发送 `RM_STRUCK`/死亡消息。 | `ObjBase.pas:5252-5547`, `ObjBase.pas:5550-5692`; 详见 `attack_call_chain.md:§14`, `protocol_sequence.md:§4` | C++ 必须用 golden trace 锁住 SM_HIT/SM_STRUCK/SM_DEATH 顺序, 避免 frame-end 派发重排。 | PR2/PR9 消息顺序 trace | no |
| P0-4 施法动作广播时机 | 普通 `SpellNow` 入口在分支校验前广播 `RM_SPELL`; 失败分支可在动作后返回失败特效。 | `Magic.pas:534-537`, `Magic.pas:983-998`; 详见 `skill_call_chain.md:§4.1`, `protocol_sequence.md:§7` | C++ 如果先校验再广播, 会改变客户端施法动画时机; `spell_failure.json` 冻结期望。 | PR6/PR7 按技能分支对齐失败路径 | no |
| P0-5 同帧输入顺序 | Delphi `Operate` 从消息队列按到达顺序取包; 不存在固定“移动优先于攻击”的二次排序。 | `ObjBase.pas:12050-13010`; 详见 `attack_call_chain.md:§3` | C++ `legacy_inbox_`/client_v1 桥接必须保持同玩家 FIFO, 每帧预算不能重排同玩家命令。 | PR11 client_v1 频率和 FIFO 测试 | no |
| P0-6 玩家/怪物 `RM_STRUCK` 延迟 | 玩家目标使用 `SendDelayMsg(..., 200ms)`; 怪物/非玩家路径使用即时发送。 | `ObjBase.pas:5449`, `ObjBase.pas:5542`; 详见 `attack_call_chain.md:§6.6`, `struck_delay_player_vs_monster.json` | C++ 当前统一 frame-end 派发存在时序风险; 需要明确保留或记录兼容差异。 | PR2/PR9 trace 对齐 | no |
| P0-7 抗魔判定 | 公式为 `target.AntiMagic <= Random(10)`, `Random(10)` 范围 `[0,9]`。 | `Magic.pas:552`; 详见 `formula_tables.md:F13` | C++ `legacy_anti_magic_pass()` 已对应; 需要 golden formula 保持边界。 | PR4/PR6 formula tests | no |
| P0-8 MP 扣除和失败消耗 | `DoSpell` 先算 `GetSpellPoint`, MP 足够即 `DamageSpell` + `HealthSpellChanged`, 然后进入 `SpellNow`; 因此通过入口但分支目标失败的普通技能已经消耗 MP。 | `ObjBase.pas:7106-7128`, `Magic.pas:548-570`; 详见 `skill_call_chain.md:§3.2`, `spell_failure.json` | C++ 失败路径需区分“入口前失败”和“SpellNow 分支失败”; 后者应按 Delphi 已消耗 MP。 | PR6/PR7 失败路径测试 | no |
| P0-9 客户端动作锁超时 | `ServerAcceptNextAction` 等服务器 ack, 10 秒无响应后客户端自动解锁。 | `ClMain.pas:3324-3335`; 详见 `attack_call_chain.md:§2.2` | C++ 客户端 action lock 超时必须保持 10s, 避免卡死或过早重发。 | PR3 客户端动作锁测试 | no |

---

## P1 — 公式边界和技能特殊规则

| ID | 结论 | Delphi 证据 | C++ 影响 | 后续 PR | 阻塞 |
|---|---|---|---|---|---|
| P1-1 烈火倍率取整 | 烈火在基础伤害和幸运公式之后追加 `Round(dam/100*(HitDouble*10))`; Delphi `Round` 是 banker's rounding。 | `ObjBase.pas:5403-5405`; 详见 `formula_tables.md:F12`, `attack_firehit.json` | C++ 必须用 `delphi_round`, 不能用 `std::lround`。 | PR4/PR5 | no |
| P1-2 半月无衰减 | `SwordWideAttack` 前方三格目标使用同一个 `damage`, 没有主/副目标衰减。 | `ObjBase.pas:5285-5304`; 详见 `attack_widehit.json` | C++ `collect_wide_hit_targets` 只能选目标, 伤害不应按位置衰减。 | PR5 | no |
| P1-3 十字斩 PvP 80% | `SwordCrossAttack` 对 `RC_USERHUMAN` 目标使用 `Round(damage*0.8)`。 | `ObjBase.pas:5320-5323`; 详见 `attack_crosshit.json` | C++ 十字斩多目标结算需区分玩家/怪物目标。 | PR5 | no |
| P1-4 AC/MAC range `ShortInt` | Delphi 使用 `ShortInt(HiByte(AC)-LoByte(AC))`; range 大于 127 时可能按 ShortInt 溢出。 | `ObjBase.pas:3433`; 详见 `formula_tables.md:F7` | C++ 是否保留溢出语义会影响极端 AC/MAC; PR4 需明确实现或记录差异。 | PR4 | no |
| P1-5 吸血累加器 | 吸血按浮点累加, 累计值达到 2 以上才回血并扣减累计值。 | `ObjBase.pas:5459-5466`; 详见 `attack_call_chain.md:§6.7` | C++ 不能每次小额伤害立即回血。 | PR4/PR12 | no |
| P1-6 石化触发 | 装备特技石化触发公式为 `Random(5 + target.AntiPoison) = 0`。 | `ObjBase.pas:5454`; 详见 `attack_call_chain.md:§6.7` | C++ 装备特技需按 AntiPoison 调整概率。 | PR4/PR12 | no |
| P1-7 红毒伤害/耐久 | `POISON_DAMAGEARMOR` 使装备损耗和伤害都乘以 `1.2`。 | `ObjBase.pas:3483-3489`; 详见 `attack_basic.json` | C++ 需要同时影响最终伤害和装备耐久损耗。 | PR8/PR12 | no |
| P1-8 冰咆哮/地狱雷光非不死伤害 | `MagElecBlizzard` 对不死目标全额, 非不死目标使用 `pwr div 10`。 | `Magic.pas:361-381`; 详见 `formula_tables.md:F8` | C++ 已有阶段测试覆盖, 后续保持 golden formula。 | PR6 | no |
| P1-9 野蛮冲撞 gate | 成功门槛为 `Random(20) < 6 + level*3 + level_gap`。 | `Magic.pas:61-85`; 详见 `formula_tables.md:F19`, `spell_rush.json` | C++ `handle_legacy_rush_rush` 必须保持同概率和等级差。 | PR5 | no |
| P1-10 圣言术成功率 | 圣言术目标必须是不死、非 NeverDie, 成功率使用 `Random(100) < 15 + level*7 + level_gap`。 | `Magic.pas:176`; 详见 `formula_tables.md:F20` | C++ 即死路径需保持目标过滤和概率。 | PR7 | no |
| P1-11 心灵启示成功率 | 心灵启示使用 `Random(6) <= 3 + level` 开启目标血量显示。 | `Magic.pas:972`; 详见 `formula_tables.md:F21` | C++ open-health marker 只在成功后训练。 | PR7 | no |
| P1-12 施毒 `nofire` | 施毒分支先设 `nofire := TRUE`; 有毒粉且完成分支后重置 `nofire := FALSE`, 因而成功施毒和抗毒抵抗都会发送普通 `SM_MAGICFIRE`; 缺道具/目标失败路径才跳过后处理并返回失败。 | `Magic.pas:734-829`, `Magic.pas:983-998`; 详见 `spell_poison.json` | C++ 需区分成功施毒/抗毒抵抗 magic-fire 与缺道具失败 magic-fire-fail。 | PR7/PR8 | no |
| P1-13 隐身 `nofire` | 单体/群体隐身在 shared bujuk 分支中成功后也设置 `nofire := FALSE`, 状态变化与普通 `SM_MAGICFIRE` 后处理并存。 | `Magic.pas:919-933`, `Magic.pas:983-998`; 详见 `skill_call_chain.md:§6.5` | C++ case 18/19 需保留成功 magic-fire 与状态包顺序。 | PR8 | no |
| P1-14 毒 tick 与恢复 tick 顺序 | Delphi 在角色运行阶段处理持续状态和回血; PR1 将 C++ 对齐要求冻结为“恢复/状态 tick 必须有固定顺序并被测试锁住”。 | `ObjBase.pas:7586-7682`; 详见 `skill_call_chain.md:§7` | 后续 PR 不得让同帧毒死/回血结果依赖容器遍历偶然性。 | PR8/PR12 | no |
| P1-15 魔法盾 MP 吸收 | `DamageHealth` 中 `spdam := Round(damage*1.5)`; MP 足够则完全吸收, MP 不足则按 `(spdam-MP)/1.5` 反推穿透伤害。 | `ObjBase.pas:3589-3600`; 详见 `formula_tables.md:F10` | C++ `apply_damage` 和魔法泡泡不是同一层, 必须分开测试。 | PR4/PR8 | no |
| P1-16 技能训练量 | 法师/道士普通技能成功训练 `1 + Random(3)`; 高级剑术训练多为固定 `1`。 | `ObjBase.pas:5470-5527`, `Magic.pas:992`; 详见 `skill_call_chain.md:§8` | C++ 训练经验、升级包和 stale lvexp 需要保持分支差异。 | PR6/PR7/PR8 | no |

---

## P2 — PR10 视觉/音效延期项

这些项不阻塞 PR2-PR9 的服务端战斗语义实现, 但 PR10 必须逐项核对客户端表现。

| ID | PR10 输入 | Delphi 证据 | 当前要求 |
|---|---|---|---|
| P2-1 | `EffectBase[0..35]` 与 magic effect/effect_type 映射 | `magiceff.pas`, `ClMain.pas:4277` | PR10 输出逐 ID 特效表和 animation trace。 |
| P2-2 | `HitEffectBase[0..5]` 命中特效 | `Actor.pas`, `magiceff.pas` | PR10 锁定烈火/半月/刺杀等命中特效。 |
| P2-3 | 施法动画 `SpellFrame` 特殊停帧 | `Actor.pas:2703-2739` | PR10 与 `legacy_animation.cpp` 逐帧对齐。 |
| P2-4 | 受击帧时间和等级相关最小值 | `Actor.pas:1149-1159` | PR10 确认客户端动作持续时间。 |
| P2-5 | 技能声音触发帧 | `ClMain.pas`, `Actor.pas` | PR10 与 `legacy_sound_rules.cpp` 对齐。 |
| P2-6 | 魔法声音编号 `10000 + Serial*10` | `ClMain.pas`, `magiceff.pas` | PR10 输出声音 golden。 |
| P2-7 | 死亡动画反向/立即死亡差异 | `Actor.pas:1035-1056` | PR10 区分 `SM_DEATH` 与 `SM_NOWDEATH`。 |
| P2-8 | 弹道飞行/碰撞像素阈值 | `magiceff.pas:518-545` | PR10 用 animation replay 覆盖 16 方向弹道。 |
| P2-9 | 召唤物 name color / HP bar | 客户端渲染代码 | PR10/客户端 UI PR 处理, 不阻塞服务端 PR。 |

---

## PR1 收口统计

| 类别 | 数量 | 状态 |
|---|---:|---|
| P0 攻击/施法时序 | 9 | 全部收口, 作为 PR2/PR3/PR6/PR7/PR11 输入 |
| P1 公式/技能规则 | 16 | 全部收口, 差异只记录为后续 PR 工作 |
| P2 客户端表现 | 9 | 全部归属 PR10 |

PR1 不再保留 P0/P1 的开放问题。剩余工作是实现和测试这些已冻结的行为。
