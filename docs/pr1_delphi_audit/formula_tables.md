# Delphi 战斗公式表

> 基于 `ObjBase.pas` + `Magic.pas` + `ClMain.pas` 静态审查
> 所有行号为当前 Delphi 源码版本

---

## 公式索引

| # | 公式名 | Delphi 来源 | 绝对不能改 |
|---|--------|------------|-----------|
| F1 | 攻击力随机 (Luck) | ObjBase.pas:5236-5250 | **是** |
| F2 | 客户端攻击速度 | ClMain.pas:3348-3362 | **是** |
| F3 | 服务端攻击间隔 | ObjBase.pas:9309-9316 | **是** |
| F4 | 魔法基础威力 | Magic.pas:55-58 | **是** |
| F5 | 技能等级缩放 (GetPower) | Magic.pas:449-453 | **是** |
| F6 | 技能等级缩放 (GetPower13) | Magic.pas:454-462 | **是** |
| F7 | 物理防御减伤 | ObjBase.pas:3433-3449 | **是** |
| F8 | 魔法防御减伤 | ObjBase.pas:3452-3468 | **是** |
| F9 | 魔法泡泡减伤 | ObjBase.pas:3445-3448 | **是** |
| F10 | 魔法盾MP吸收 | ObjBase.pas:3589-3600 | **是** |
| F11 | 命中判定 | ObjBase.pas:5259/5435 | **是** |
| F12 | 烈火倍率 | ObjBase.pas:5403-5405 | **是** |
| F13 | 抗魔判定 | ObjBase.pas:552 | **是** |
| F14 | 抗毒判定 | Magic.pas:773 | **是** |
| F15 | 毒持续时间 | Magic.pas:775-786 | **是** |
| F16 | 隐身/BUFF持续时间 | Magic.pas:694/883/902 | **是** |
| F17 | 火墙持续时间 | Magic.pas:655 | **是** |
| F18 | 治疗公式 | Magic.pas:708-712 | **是** |
| F19 | 野蛮冲撞 gate | Magic.pas:75 | **是** |
| F20 | 圣言术成功率 | Magic.pas:176 | **是** |
| F21 | 瞬息移动成功率 | Magic.pas:203 | **是** |
| F22 | 心灵启示成功率 | ObjBase.pas:972 | **是** |
| F23 | 石化触发 | ObjBase.pas:5454 | **是** |
| F24 | PK惩罚 (武器诅咒) | ObjBase.pas:2723 | **是** |
| F25 | 装备损耗 (基础) | ObjBase.pas:3483 | **是** |
| F26 | 装备损耗 (单件) | ObjBase.pas:3534 | **是** |
| F27 | 武器耐久损耗 | ObjBase.pas:5444 | **是** |
| F28 | 吸血公式 | ObjBase.pas:5459-5466 | **是** |

---

## F1: 攻击力随机 (Luck公式)

**Delphi**: `TCreature.GetAttackPower` — ObjBase.pas:5236-5250

```
输入:
  damage = LoByte(DC)        — DC下限
  ranval = HiByte(DC) - LoByte(DC)  — DC范围 (可正可负)
  luck   = Luck              — 幸运值 (正=幸运, 负=诅咒)

算法:
  ranval := max(0, ranval)

  if luck > 0:
    gate = Random(10 - min(9, luck))
    if gate == 0:
      result = damage + ranval           ← 幸运一击(最大值)
    else:
      result = damage + Random(ranval + 1)
  else:
    result = damage + Random(ranval + 1)  ← 普通随机
    if luck < 0:
      gate_range = 10 - max(0, -luck)
      gate = (gate_range <= 0) ? 0 : Random(gate_range)
      if gate == 0:
        result = damage                  ← 诅咒一击(最小值)

输出: 最终物理攻击力 (int)
随机规则: Random(N) 返回 [0, N-1], N>0
取整规则: 整数运算, 无取整
clamp: result >= damage (luck<0 gate=0时) 或 result >= damage (otherwise)
```

**幸运9套**: luck=9 → gate=`Random(1)`=0 永远触发 → **总是取最大值**
**诅咒10套**: luck=-10 → gate_range=0 → gate=0永远触发 → **总是取最小值**

**绝对不能改**: 是 — 整个传奇装备经济建立在幸运9套机制上

---

## F2: 客户端攻击速度公式

**Delphi**: `CanNextHit` — ClMain.pas:3348-3362

```
输入:
  Level     — 玩家等级
  HitSpeed  — 攻速属性 (0=默认, 正=快, 负=慢)
  BoAttackSlow — 减速标志 (Boolean)

算法:
  levelfast = min(370, Level * 14)
  levelfast = min(800, levelfast + HitSpeed * 60)
  if BoAttackSlow:
    nexthit = 1400 - levelfast + 1500
  else:
    nexthit = 1400 - levelfast
  nexthit = max(0, nexthit)

输出: 攻击间隔 (ms)

取值范围:
  - 最慢: 1400ms (Level=1, HitSpeed=0)
  - 中等: 1030ms (Level>=27, HitSpeed=0, 已达等级上限)
  - 最快: 600ms (Level>=27, HitSpeed=5)
  - 减速最慢: 2900ms (Level=1, BoAttackSlow=true)
  - 理论最快: 0ms (Level>=14, HitSpeed>=14)
```

**绝对不能改**: 是 — 攻击速度改变直接改变 PvP 平衡

---

## F3: 服务端攻击间隔 (防加速)

**Delphi**: `TUserHuman.HitXY` — ObjBase.pas:9309-9316

```
输入:
  HitSpeed — 攻速属性
  LatestHitTime — 上次攻击时间戳

算法:
  hit_interval = 900 - (HitSpeed * 60)
  if GetTickCount - LatestHitTime < hit_interval:
    HitTimeOverCount++
    if HitTimeOverCount >= 4: 拒绝攻击 (超频4次)
    HitTimeOverSum++
    if HitTimeOverSum >= 6: 拒绝攻击 (累计6次)

输出: 攻击许可 (allow/block)
```

**注意**: 服务端间隔 900ms 比客户端 1400ms 更宽 — 客户端是硬限制, 服务端是防作弊兜底

---

## F4: 魔法基础威力 (MPow)

**Delphi**: `TMagicManager.MPow` — Magic.pas:55-58

```
输入:
  pum.Def.MinPower — 技能定义最小威力
  pum.Def.MaxPower — 技能定义最大威力

算法:
  result = MinPower + Random(MaxPower - MinPower)

输出: 技能基础威力 (int)
随机规则: Random(range) → [0, range-1]
```

**绝对不能改**: 是 — 所有魔法技能伤害的基础

---

## F5: 技能等级缩放 (GetPower)

**Delphi**: Magic.pas:449-453

```
输入:
  pw    — 基础威力值
  Level — 技能等级 (0-3)
  MaxTrainLevel — 最大训练等级 (通常=3)
  DefMinPower/DefMaxPower — 技能定义附加随机威力

算法:
  scaled = Round(pw / (MaxTrainLevel + 1) * (Level + 1))
  bonus = DefMinPower + Random(DefMaxPower - DefMinPower)
  result = scaled + bonus

输出: 按等级缩放后的威力

示例 (MaxTrainLevel=3, DefMinPower=DefMaxPower=0):
  Level 0: Round(pw * 1/4) = 25%
  Level 1: Round(pw * 2/4) = 50%
  Level 2: Round(pw * 3/4) = 75%
  Level 3: Round(pw * 4/4) = 100%

取整规则: Delphi Round() = Banker's Rounding (round-half-even)
```

**绝对不能改**: 是

---

## F6: 技能等级缩放 (GetPower13)

**Delphi**: Magic.pas:454-462

```
输入:
  pw    — 基础威力值
  Level — 技能等级 (0-3)
  MaxTrainLevel — 最大训练等级
  DefMinPower/DefMaxPower — 技能定义附加随机威力

算法:
  p1 = pw / 3                          // 固定部分 (1/3)
  p2 = pw - p1                         // 可变部分 (2/3)
  bonus = DefMinPower + Random(DefMaxPower - DefMinPower)
  result = Round(p1 + p2 / (MaxTrainLevel + 1) * (Level + 1) + bonus)

输出: 按等级缩放后的威力

示例 (MaxTrainLevel=3, pw=60, DefMinPower=DefMaxPower=0):
  Level 0: Round(20 + 40*1/4) = 30 (50%)
  Level 1: Round(20 + 40*2/4) = 40 (67%)
  Level 2: Round(20 + 40*3/4) = 50 (83%)
  Level 3: Round(20 + 40*4/4) = 60 (100%)

与 GetPower 的区别: GetPower13 的 level=0 是 50% 而非 25% — 更适合道士技能
```

**绝对不能改**: 是

---

## F7: 物理防御减伤

**Delphi**: `GetHitStruckDamage` — ObjBase.pas:3433-3449

```
输入:
  damage    — 原始物理伤害
  WAbil.AC  — 目标物理防御 (packed: LoByte=min, HiByte=max)
  LifeAttrib — 目标生命属性 (LA_UNDEAD等)
  hiter.UndeadPower — 攻击者对不死加成
  BoAbilMagBubbleDefence — 魔法泡泡防御标志
  MagBubbleDefenceLevel — 魔法泡泡等级

算法:
  AC_min = LoByte(WAbil.AC)
  AC_max = HiByte(WAbil.AC)
  armor = AC_min + Random(ShortInt(AC_max - AC_min) + 1)
  damage = max(0, damage - armor)

  if LifeAttrib == LA_UNDEAD and hiter != nil:
    damage += hiter.AddAbil.UndeadPower

  if damage > 0 and BoAbilMagBubbleDefence:
    damage = Round(damage / 100 * (MagBubbleDefenceLevel + 2) * 8)
    // 见 F9

输出: 减伤后伤害 (int, >=0)
随机规则: Random(AC_range + 1) → [0, AC_range]
取整规则: max(0, ...) 截断负值
clamp: damage >= 0

注意: ShortInt 类型转换意味着 AC_range 是有符号8位 (-128~127)
     如果 AC_max - AC_min > 127, 会出现溢出!
```

**绝对不能改**: 是

---

## F8: 魔法防御减伤

**Delphi**: `GetMagStruckDamage` — ObjBase.pas:3452-3468

```
与 F7 结构完全一致, 但使用 WAbil.MAC 替代 WAbil.AC

算法:
  MAC_min = LoByte(WAbil.MAC)
  MAC_max = HiByte(WAbil.MAC)
  armor = MAC_min + Random(ShortInt(MAC_max - MAC_min) + 1)
  damage = max(0, damage - armor)
  // undead bonus + bubble defence 同 F7
```

**绝对不能改**: 是

---

## F9: 魔法泡泡减伤

**Delphi**: `GetHitStruckDamage` 内部 — ObjBase.pas:3445-3448

```
输入:
  damage                 — 减伤后伤害
  MagBubbleDefenceLevel  — 魔法泡泡等级 (对应技能等级)

算法:
  damage = Round(damage / 100.0 * (MagBubbleDefenceLevel + 2) * 8)

输出: 泡泡减伤后伤害

示例:
  Level 0: Round(damage * 0.16)  → 减伤 84%
  Level 1: Round(damage * 0.24)  → 减伤 76%
  Level 2: Round(damage * 0.32)  → 减伤 68%
  Level 3: Round(damage * 0.40)  → 减伤 60%

取整规则: Delphi Round() = Banker's Rounding
```

**绝对不能改**: 是

**注意**: 泡泡减伤使用 `(level + 2) * 8 / 100 = (level+2)*0.08` 倍率, 与魔法盾 (F10) 不同

---

## F10: 魔法盾 MP 吸收

**Delphi**: `DamageHealth` — ObjBase.pas:3589-3600

```
输入:
  damage       — 传入伤害
  BoMagicShield — 魔法盾是否激活
  WAbil.MP     — 当前 MP

算法:
  if BoMagicShield and damage > 0 and MP > 0:
    spdam = Round(damage * 1.5)         // MP消耗 = 伤害 × 1.5
    if MP >= spdam:
      MP -= spdam                       // 完全吸收
    else:
      remaining = spdam - MP             // MP不足
      MP = 0
      damage = Round(remaining / 1.5)   // 穿透伤害
    HealthSpellChanged                  // 广播HP/MP变化
```

**魔法泡泡 vs 魔法盾**:
| 特性 | 魔法泡泡 (F9) | 魔法盾 (F10) |
|------|-------------|------------|
| 触发位置 | GetHitStruckDamage (护甲减伤后) | DamageHealth (HP修改前) |
| 效果 | 倍数减伤 | MP吸收 |
| 消耗 | 泡泡耐久 (DamageBubbleDefence) | MP |
| 可叠加 | 是 (先泡泡减伤, 再盾吸收) | 是 |

**绝对不能改**: 是

---

## F11: 命中判定

**Delphi**: `DirectAttack` — ObjBase.pas:5259; `_Attack` — ObjBase.pas:5435

```
输入:
  AccuracyPoint     — 攻击者命中 (byte)
  target.SpeedPoint — 目标闪避 (byte)

算法:
  hit_roll = Random(target.SpeedPoint)
  hit = hit_roll < AccuracyPoint

输出: TRUE (命中) / FALSE (MISS)

命中率 = AccuracyPoint / SpeedPoint (当 AccuracyPoint <= SpeedPoint)
        100% (当 AccuracyPoint > SpeedPoint, 因为 Random(SpeedPoint) 最大值为 SpeedPoint-1)

注意: Delphi Random(N) 返回 [0, N-1], N>0
      如果 SpeedPoint=0, Random(0) 行为未定义 — 需要保护
```

**绝对不能改**: 是

---

## F12: 烈火倍率

**Delphi**: `_Attack` — ObjBase.pas:5403-5405

```
输入:
  dam    — 基础攻击力 (来自 GetAttackPower)
  HitDouble — 烈火伤害倍率参数
  FireLevel — 烈火技能等级

算法:
  HitDouble = 4 + FireLevel * 4    // level 0→4, level 1→8, level 2→12, level 3→16
  bonus = Round(dam / 100.0 * (HitDouble * 10))
  dam += bonus

输出: 烈火加成的攻击力

倍率:
  Level 0: 1 + Round(40/100) = 1.40x
  Level 1: 1 + Round(80/100) = 1.80x
  Level 2: 1 + Round(120/100) = 2.20x
  Level 3: 1 + Round(160/100) = 2.60x
```

**绝对不能改**: 是 — 战士爆发核心

---

## F13: 抗魔判定

**Delphi**: `SpellNow` — Magic.pas:552

```
输入:
  target.AntiMagic — 目标抗魔值
  Random(10)       — [0, 9] 随机值

算法:
  hit = target.AntiMagic <= Random(10)

输出: TRUE (魔法命中) / FALSE (魔法被抵抗)
```

**绝对不能改**: 是

---

## F14: 抗毒判定

**Delphi**: `SpellNow` — Magic.pas:773

```
输入:
  target.AntiPoison — 目标抗毒值

算法:
  hit = 6 >= Random(7 + target.AntiPoison)

输出: TRUE (毒生效) / FALSE (毒被抵抗)

成功率: 7 / (7 + AntiPoison)
   AntiPoison=0: 7/7 = 100%
   AntiPoison=5: 7/12 = 58.3%
   AntiPoison=10: 7/17 = 41.2%
```

---

## F15: 毒持续时间

**Delphi**: `SpellNow` — Magic.pas:775-786

```
Shape=1 (灰毒/绿毒 POISON_DECHEALTH):
  power = GetPower13(30) + 2 * GetRPow(WAbil.SC)

Shape=2 (黄毒/红毒 POISON_DAMAGEARMOR):
  power = GetPower13(40) + 2 * GetRPow(WAbil.SC)

power 含义: 毒持续时间 (秒), 作为 `MakePoison(poison, sec, poisonlv)` 的 `sec` 存入 `StatusArr[poison]`
tick 伤害: 绿毒每 2500ms 造成 `1 + PoisonLevel`, 与 `sec` 分离
```

**确认**: `StatusArr` 每 1000ms 递减一次 (`ObjBase.pas:7860-7864`); 绿毒伤害 tick 使用 `ObjBase.pas:7921-7931` 的 2500ms 周期。

---

## F16: BUFF 持续时间

**Delphi**: Magic.pas:694, 883, 902

```
魔法盾 (31): GetPower(15 + GetRPow(WAbil.MC))
防御BUFF (14/15): GetPower13(60) + 5*LoByte(SC) + GetAttackPower(...)
DC BUFF (36): 同 14/15
隐身 (18/19): GetPower13(30 + GetRPow(WAbil.SC)*2)
困魔咒 (16): GetPower13(30 + GetRPow(WAbil.SC)*2)
心灵启示 (28): GetPower13(30 + GetRPow(WAbil.SC)*2) * 1000 (毫秒)

通用公式:
  duration = GetPower13(base + GetRPow(SC_or_MC) * multiplier)
```

---

## F17: 火墙持续时间

**Delphi**: `SpellNow` — Magic.pas:655

```
duration = GetPower(10) + GetRPow(WAbil.MC) / 2
```

---

## F18: 治疗公式

**Delphi**: `SpellNow` — Magic.pas:708-712

```
base = GetPower(MPow(pum)) + LoByte(WAbil.SC) * 2
range = ShortInt(HiByte(WAbil.SC) - LoByte(WAbil.SC)) * 2 + 1
heal = GetAttackPower(base, range)

群体治疗 (29): 公式相同, 应用于范围

注意事项:
  - SC 贡献 2x 倍率 (LoByte(SC)*2)
  - 范围也是 2x
```

---

## F19: 野蛮冲撞 gate

**Delphi**: `MagPushArround` — Magic.pas:75

```
level_gap = user.Level - target.Level
gate = Random(20)
success = gate < 6 + push_level * 3 + level_gap

push_level = 技能等级 (0-3)
```

---

## F20: 圣言术成功率

**Delphi**: `MagTurnUndead` — Magic.pas:176

```
level_gap = user.Level - target.Level
success = Random(100) < 15 + level * 7 + level_gap

level=0: 15% + level_gap
level=3: 36% + level_gap
```

---

## F21: 瞬息移动成功率

**Delphi**: `MagLightingSpaceMove` — Magic.pas:203

```
success = Random(11) < 4 + level * 2

level=0: 4/11 = 36.4%
level=1: 6/11 = 54.5%
level=2: 8/11 = 72.7%
level=3: 10/11 = 90.9%
```

---

## F22: 心灵启示成功率

**Delphi**: `SpellNow` — Magic.pas:972 / ObjBase.pas:972

```
success = Random(6) <= 3 + pum.Level

level=0: 4/6 = 66.7%
level=1: 5/6 = 83.3%
level=2: 6/6 = 100%
level=3: 6/6 = 100%
```

---

## F23: 石化触发

**Delphi**: `_Attack` — ObjBase.pas:5454

```
条件: BoAbilMakeStone (装备特技)
触发: Random(5 + target.AntiPoison) = 0

成功: MakePoison(POISON_STONE, 5, 0)  → 石化5秒
```

---

## F24: PK惩罚武器诅咒

**Delphi**: `Die` — ObjBase.pas:2723

```
条件: PKLevel < 1
触发: Random(5) = 0  → 20% 概率

效果: LastHiter.MakeWeaponUnlock  → 武器被诅咒
```

---

## F25-F27: 装备耐久损耗

### F25: 受击装备基础损耗 — ObjBase.pas:3483
```
wdam = Random(10) + 5   → [5, 14]
if POISON_DAMAGEARMOR: wdam *= 1.2
```

### F26: 受击单件装备损耗 — ObjBase.pas:3534
```
for each equipped item:
  if Random(8) = 0: 该装备耐久减少 wdam
```
每件装备每次受击有 1/8 概率受损。

### F27: 攻击武器耐久损耗 — ObjBase.pas:5444
```
weapondamage = Random(5) + 2 - AddAbil.WeaponStrong
```
每次攻击武器损耗 [2-WeaponStrong, 6-WeaponStrong]。

---

## F28: 吸血公式

**Delphi**: `_Attack` — ObjBase.pas:5459-5466

```
条件: SuckupEnemyHealthRate > 0

SuckupEnemyHealth += damage * SuckupEnemyHealthRate / 100
n = Trunc(SuckupEnemyHealth)
if n >= 2:
  DamageHealth(-n)  // 负值 = 回血
  SuckupEnemyHealth -= n
```

**特点**: 吸血按浮点累加, 只有累计 >= 2 时才实际回血。这防止了小额伤害反复吸血的问题。

---

## C++ 公式实现对照

| Delphi | C++ 函数 | 文件 | 对齐状态 |
|--------|---------|------|---------|
| F1 GetAttackPower | `legacy_attack_power()` | legacy_skill_formula.cpp:84 | 已实现 |
| F1 GetAttackPower (rolls) | `legacy_attack_power_from_rolls()` | legacy_skill_formula.cpp:66 | 已实现 |
| F2 CanNextHit | `can_next_hit()` | game_state.hpp:503 | 已实现 |
| F3 HitXY throttle | `begin_attack_attempt()` | game_object.hpp:397 | 已实现 |
| F4 MPow | `legacy_mpow()` | legacy_skill_formula.cpp:35 | 已实现 |
| F5 GetPower | `legacy_power()` | legacy_skill_formula.cpp:43 | 已实现 |
| F6 GetPower13 | `legacy_power13()` | legacy_skill_formula.cpp:57 | 已实现 |
| F7 GetHitStruckDamage | `legacy_physical_struck_damage()` | map_actor.cpp:639 | 已实现 |
| F8 GetMagStruckDamage | `legacy_mag_struck_damage()` | legacy_skill_formula.cpp:103 | 已实现 |
| F9 Magic Bubble | in `legacy_mag_struck_damage()` | legacy_skill_formula.cpp:114-117 | 已实现 |
| F10 Magic Shield | `apply_damage()` → `DamageResult` | game_object.cpp / map_actor.cpp | PR1 已冻结, PR4/PR8 验证 |
| F11 Hit Check | accuracy vs speed roll | map_actor_mail.hpp:2357-2365 | 已实现 |
| F12 FireHit bonus | fire_hit_bonus calc | map_actor_mail.hpp:2345-2355 | 已实现 |
| F13 AntiMagic | `legacy_anti_magic_pass()` | legacy_skill_formula.cpp:121 | 已实现 |
| F14 AntiPoison | in poison spell handler | map_actor_mail.hpp:3151+ | PR1 已冻结, PR8 验证 |
| F15 Poison duration | `legacy_poison_seconds()` | map_actor.cpp:1153 | 已实现 |
| F16 Buff duration | `legacy_defence_status_seconds()` etc | map_actor.cpp:1142+ | 已实现 |
| F17 FireWall duration | `legacy_fire_wall_seconds()` | map_actor.cpp:1169 | 已实现 |
| F18 Heal | `legacy_heal_power()` | map_actor.cpp:1036 | 已实现 |
| F19 Rush gate | rush_gate calc | map_actor.cpp:2709-2713 | 已实现 |
| F20 TurnUndead | spell case 32 | map_actor_mail.hpp:3925 | PR1 已冻结, PR7 验证 |

---

## 随机数规则总结

| Delphi 调用 | 范围 | 用途 |
|------------|------|------|
| `Random(N)` | [0, N-1] (N>0) | 所有随机判定 |
| `Random(ranval+1)` | [0, ranval] | 伤害随机 |
| `Random(10-luck)` | [0, 9-luck] (luck>0) | 幸运gate |
| `Random(10+luck)` | [0, 9+luck] (luck<0) | 诅咒gate |
| `Random(SpeedPoint)` | [0, SpeedPoint-1] | 命中判定 |
| `Random(AC_range+1)` | [0, AC_range] | AC减伤 |
| `Random(target.SpeedPoint)` | [0, SpeedPoint-1] | 命中判定 |
| `Random(5+AntiPoison)` | [0, 4+AntiPoison] | 石化 |
| `Random(10)+5` | [5, 14] | 装备损耗 |
| `Random(8)` | [0, 7] (1/8概率) | 装备损坏 |
| `Random(5)+2-WeaponStrong` | [2-WS, 6-WS] | 武器损耗 |
| `Random(5)` | [0, 4] (20%概率) | PK武器诅咒 |
| `Random(3)+1` | [1, 3] | 技能训练 |
| `Random(10)` | [0, 9] (10%概率) | 抗魔判定 |
| `Random(7+AntiPoison)` | [0, 6+AntiPoison] | 抗毒判定 |
| `Random(20)` | [0, 19] | 野蛮冲撞gate |
| `Random(100)` | [0, 99] | 圣言术 |
| `Random(11)` | [0, 10] | 瞬息移动 |
| `Random(6)` | [0, 5] | 心灵启示 |

**关键**: Delphi `Random(N)` 返回 `[0, N-1]`, C++ `LegacyRandom::random(N)` 必须返回 `[0, N-1]`。
