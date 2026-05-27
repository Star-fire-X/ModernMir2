# PR1 Final Report: Delphi 战斗/技能语义审计收口

## 结论摘要

PR1 已在 `docs/pr1_delphi_audit/` 内完成收口: P0 攻击/施法时序和 P1 公式/技能规则不再保留开放问题。后续 PR 需要实现或测试这些已冻结行为, 不能重新解释 Delphi 语义。

本目录是原始 Delphi 语义证据。重排后的 combat stack PR-1 额外提供
`docs/delphi_cpp_combat_compatibility_audit.md` 和
`ModernServer/tests/golden/legacy_combat/` 作为 CTest 消费入口, 但仍不修改 C++
生产代码或协议常量。

## 2026 Stack Refresh

重排后的基线固定为 `origin/main@d7dcc0d7af37a91b2f1abaa9e1f741abc28d6f61`。
`safe_zone / area_state / home_leash / visibility order / monster ATTACK_SPD >= 200`
均按主线已吸收或部分吸收处理。尤其 monster `ATTACK_SPD >= 200` 是当前导入与
运行时速度下限基线, 不再列为本轮 combat stack 的兼容 bug。

## 已解决项

| 类别 | 结论 |
|---|---|
| 攻击节奏 | 客户端 `CanNextHit` 和服务端 `HitXY` 是两层不同公式, 均已冻结。 |
| 攻击消息顺序 | `HitXY -> HitHit/_Attack -> HitMotion` 和 `SM_STRUCK` 延迟差异已冻结。 |
| 施法失败 | 普通技能 `DoSpell` 先扣 MP, `SpellNow` 入口先广播 `SM_SPELL`, 分支失败后不产生效果。 |
| 物理公式 | Luck gate、AC `ShortInt` range、烈火 banker's rounding、MISS 不广播 `SM_STRUCK` 已冻结。 |
| 战士技能 | 刺杀距离 2、半月无衰减、十字 PvP 80%、野蛮 gate 已冻结。 |
| 法师/道士技能 | 抗魔、施毒/隐身 `nofire`, 圣言、心灵启示、地狱雷光非不死 10% 等规则已冻结。 |
| 状态/死亡 | 魔法盾 MP 吸收、红毒 1.2x、毒/恢复 tick 顺序要求、死亡经验/PK/掉落顺序已冻结。 |

## 已知差异输入

| 差异 | PR1 结论 | 后续 PR |
|---|---|---|
| 服务端攻击节流额外 200ms 下限 | C++ 安全增强, 不改变合法节奏, 需要测试锁住。 | PR3 |
| 无效攻击是否刷新攻击冷却 | Delphi 只在合法 `HitXY` 流程刷新; C++ 需验证目标失败路径。 | PR3/PR5 |
| 客户端可见 `SM_STRUCK` 延迟 | Delphi 对玩家和怪物主目标延迟 200ms; 刺杀/半月/十字二级 `DirectAttack` 延迟 500ms。怪物即时 `RM_STRUCK` 是内部 AI/RunMsg 反应。C++ frame-end 派发存在对齐风险。 | PR2/PR5/PR9 |
| 施毒/隐身 `nofire` | Delphi 成功/抵抗分支会发送普通 `SM_MAGICFIRE`; 缺道具或目标失败才走 `SM_MAGICFIRE_FAIL`。 | PR7/PR8 |
| Magic ID 34 | Delphi 源码引用但 MDB 无定义; 启用需显式配置。 | PR5/PR11 |
| AC/MAC `ShortInt` range | 极端装备值是否模拟溢出是 PR4 的实现决定。 | PR4 |

## Golden Trace 覆盖

| Trace | 覆盖 |
|---|---|
| `attack_basic.json` | 普攻成功、伤害、耐久、死亡入口 |
| `attack_miss.json` | MISS 不发 `SM_STRUCK` |
| `attack_longhit.json` | 刺杀距离 2 和直线目标 |
| `attack_widehit.json` | 半月三格扇形且无衰减 |
| `attack_crosshit.json` | 十字斩和 PvP 80% |
| `attack_firehit.json` | 烈火蓄力、倍率、训练 |
| `struck_delay_player_vs_monster.json` | 主目标 200ms、DirectAttack 二级 500ms、怪物内部即时反应差异 |
| `spell_fireball.json` | 火球动作、弹道、延迟伤害 |
| `spell_failure.json` | MP 已扣、动作已广播、效果失败 |
| `spell_heal.json` | 治愈延迟回血 |
| `spell_poison.json` | 施毒 nofire、毒粉、抗毒、tick |
| `spell_rush.json` | 野蛮冲撞 gate 和推人 |
| `buff_defence.json` | 神圣战甲/幽灵盾范围状态 |
| `death_player.json` | 死亡、经验、PK、掉落、广播 |

## PR2-PR13 输入清单

| PR | 输入 |
|---|---|
| PR2 | 普攻消息顺序、MISS、主目标可见 `SM_STRUCK` 200ms 延迟、怪物内部即时反应、`attack_basic.json`。 |
| PR3 | 双层攻击节奏、ActionLock 10s、同玩家 FIFO、无效攻击冷却风险。 |
| PR4 | Delphi Round、Luck gate、AC/MAC range、魔法盾、红毒、吸血、石化公式。 |
| PR5 | 刺杀、半月、烈火、野蛮、十字斩全部战士技能规则, 包含 DirectAttack 二级命中 500ms 受击延迟。 |
| PR6 | 火球/大火球/雷电/地狱火/疾光/爆裂/地狱雷光/冰咆哮/火墙 trace。 |
| PR7 | 治愈、施毒、灵魂火符、召唤、心灵启示、圣言、诱惑等道士/控制技能。 |
| PR8 | 毒、隐身、防御状态、魔法盾、tick 顺序和下线/死亡清理。 |
| PR9 | 死亡、经验、PK、掉落、复活和死亡后操作阻断。 |
| PR10 | EffectBase、HitEffectBase、SpellFrame、声音、死亡动画、弹道像素 trace。 |
| PR11 | CM/SM 协议映射、source-only magic ID、client_v1 频率公平。 |
| PR12 | 非法输入、重复伤害/掉落/经验、越权攻击和异常状态交错。 |
| PR13 | 将 formula、trace、animation、fuzz 覆盖纳入 CI suite。 |

## 剩余 PR10 项

PR1 不再阻塞服务端战斗语义。客户端视觉/音效项统一延期到 PR10: `EffectBase[0..35]`, `HitEffectBase[0..5]`, `SpellFrame`, 受击帧时间, 技能声音, 魔法声音编号, `SM_DEATH`/`SM_NOWDEATH`, 16 方向弹道和召唤物显示。
