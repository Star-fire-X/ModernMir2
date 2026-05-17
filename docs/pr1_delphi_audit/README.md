# PR-1 Delphi Combat/Skill Semantic Audit — Closed Baseline

> 基于 `F:\mir2\Source\` Delphi 源码静态审查。
> PR1 已收口: P0/P1 行为作为 PR2-PR13 的 expected behavior 基线; P2 视觉/音效项统一归属 PR10。

---

## 权威入口

| 问题 | 查阅 |
|------|------|
| PR1 是否完成、还有哪些后续输入? | `pr1_final_report.md` |
| 普通攻击的完整调用链? | `attack_call_chain.md` — CM_HIT -> HitXY -> HitHit -> _Attack -> Die |
| 某个技能的 Delphi 执行流程? | `skill_call_chain.md` — 按 magic_id 查 SpellNow 分支 |
| 某个伤害公式的 Delphi 源码? | `formula_tables.md` — 28 个公式, 含 Delphi 行号 |
| 某个技能 ID 的参数? | `skill_id_table.md` — EffectBase/HitEffectBase/TMagicType 映射 |
| 攻击/施法/死亡的消息顺序? | `protocol_sequence.md` — CM_*/SM_* 消息映射和流程顺序 |
| 客户端动画帧/特效参数? | `client_animation_tables.md` — 动作帧表、弹道、PR10 输入 |
| 哪些绝对不能改/应保留/可现代化? | `boundary_classification.md` — A/B/C 边界清单 |
| P0/P1/P2 状态? | `verification_checklist.md` — P0/P1 已收口, P2 -> PR10 |
| 预期的消息序列 JSON? | `golden_traces/` — 14 份 expected trace |

---

## 文档目录

| # | 文件 | 内容 |
|---|------|------|
| 1 | `pr1_final_report.md` | PR1 收口报告、已知差异、PR2-PR13 输入 |
| 2 | `verification_checklist.md` | P0/P1/P2 状态矩阵 |
| 3 | `attack_call_chain.md` | 完整攻击调用链, 每个函数含 Delphi 行号 |
| 4 | `skill_call_chain.md` | 36 个 magic ID 分发逻辑 |
| 5 | `formula_tables.md` | 28 个战斗公式, 含随机规则/取整/clamp |
| 6 | `skill_id_table.md` | 技能参数矩阵和特效映射表 |
| 7 | `protocol_sequence.md` | CM_*/SM_* 消息映射和流程顺序 |
| 8 | `client_animation_tables.md` | 动作帧表、怪物动作、弹道参数、PR10 输入 |
| 9 | `boundary_classification.md` | A/B/C 边界清单和已收口差异输入 |
| 10 | `golden_traces/` | 14 份 JSON expected trace |

---

## 关键发现摘要

1. **`dueltime` 字段不存在** — 当前 Delphi 版本攻击节流使用 `LatestHitTime`。
2. **客户端/服务端攻击节奏是双层门** — 客户端 `1400 - min(800, min(370, Level*14)+HitSpeed*60)`, 服务端 `900 - HitSpeed*60`。
3. **普通技能失败可能已经扣 MP** — `DoSpell` 先扣 MP, `SpellNow` 入口先广播 `SM_SPELL`, 然后才进入分支校验。
4. **半月弯刀无伤害衰减** — 三个扇形目标使用同一份 damage。
5. **十字斩 PvP = 80%** — 对玩家目标使用 `Round(damage*0.8)`。
6. **施毒/隐身 `nofire`** — 默认保护失败路径; 成功分支会重置为 FALSE 并发送普通 `SM_MAGICFIRE`。
7. **魔法盾和魔法泡泡是两层** — MP 吸收与魔法泡泡倍数减伤不能混为一个状态。
8. **AC/MAC range 使用 ShortInt** — 极端 range 值可能出现 Delphi 溢出语义。

---

## Golden Trace 覆盖矩阵

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
| `spell_poison.json` | 施毒成功 magic-fire、缺道具失败、毒粉、抗毒、tick |
| `spell_rush.json` | 野蛮冲撞 gate 和推人 |
| `buff_defence.json` | 神圣战甲/幽灵盾范围状态 |
| `death_player.json` | 死亡、经验、PK、掉落、广播 |

---

## 后续实现边界

- PR1 不允许作为实现 PR: 不修改 C++ 生产代码、不修测试。
- PR2-PR13 应直接消费本目录的结论和 trace, 不重新定义 Delphi 语义。
- PR10 负责客户端特效、声音、逐帧动画和像素/trace 对齐。

## 相关文档

- `../combat_skill_migration_design.md` — 总体战斗迁移设计方案
- `../pr1_delphi_audit/` — 本目录
