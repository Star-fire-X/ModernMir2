# Delphi 技能施放完整调用链

> 基于 `F:\mir2\Source\M2Server\Magic.pas` (1,001行) + `ObjBase.pas` (14,718行) + `Source\Client\ClMain.pas` + `magiceff.pas` 静态审查

---

## 1. 总览: 客户端→服务端→客户端

```
[客户端] 按键 F1-F8 → 选择目标/地面
  ClMain.pas:1797 UseMagic(magic_key)
    → MP检查 (spell + defSpell <= MP)
    → 冷却检查:
        FireHit: 10秒 (1811)
        RushKung: 3秒 (1817)
        其他: 500ms (1824)
    → 构造 TUseMagicInfo:
        EffectNumber, MagicSerial, ServerMagicCode:=0
        MagicDelayTime := 200 + DelayTime
    → 发送 CM_SPELL (3017) → ActionLock:=TRUE

[服务端] ObjBase.pas:12112 CM_SPELL case
  → TUserHuman.SpellXY(magid, tx, ty, targ) [ObjBase.pas:9405+]
    → speedhack 检查 (9418-9421)
    → pum = GetMagic(magid) (9428)
    → case pum.MagicId:
        SWD_* (sword skills): 直接处理
        其他: DoSpell(pum, tx, ty, targ) (9538)

[Magic.pas] DoSpell → SpellNow [line 441-999]
  → 校验: 目标/距离/MP/道具/AntiMagic
  → 计算 power (MPow/GetPower/GetPower13 + MC/SC)
  → 执行效果 (伤害/治疗/BUFF/召唤/传送)
  → 广播: SendRefMsg(RM_SPELL) + SendRefMsg(RM_MAGICFIRE)
  → 训练: TrainSkill + SendDelayMsg(RM_MAGIC_LVEXP)

[客户端]
  ClMain.pas:4273 SM_SPELL → UseMagicSpell → Actor SendMsg(SM_SPELL)
  ClMain.pas:4277 SM_MAGICFIRE → UseMagicFire → Actor SendMsg(SM_MAGICFIRE)
  PlayScn.pas:892 NewMagic → 创建 TMagicEff 子类
```

---

## 2. 客户端 UseMagic — ClMain.pas:1797-1873

### 2.1 冷却时间

| 技能类型 | 冷却 | ClMain.pas 行号 |
|---------|------|----------------|
| FireHit (烈火) | 10秒 | 1811-1814 |
| RushKung (野蛮) | 3秒 | 1817-1820 |
| 其他技能 | 500ms | 1824 |
| vs 人类 (PK) | 300-1400ms | 1860-1863 |

### 2.2 CM_SPELL 消息

```pascal
// 非投射类 (EffectType = 0):
SendSpellMsg(CM_SPELL, x, y, dir, target_id, magid);
// → MakeDefaultMsg(CM_SPELL, MakeLong(x,y), Loword(target), dir, Hiword(target))
// ActionLock := TRUE

// 投射类 (EffectType <> 0):
TUseMagicInfo:
  ServerMagicCode := 0    // 等待服务端 FIRE
  MagicSerial := magid
  EffectNumber := pum.Def.Effect
  EffectType := pum.Def.EffectType
  Recusion := True
  Target := target_id
  MagicDelayTime := 200 + DelayTime
Myself.SendMsg(CM_SPELL, x, y, dir, target_id, magid, MakeLong(HIWORD(EffectType), LOWORD(EffectNumber)))
```

---

## 3. 服务端 SpellXY — ObjBase.pas:9405+

```pascal
procedure TUserHuman.SpellXY(magid, tx, ty: Integer; targ: TCreature);
```

### 3.1 Speedhack 检查 (9418-9421)
```pascal
if GetTickCount - LatestSpellTime < LatestSpellDelay then begin
  Inc(SpellTimeOverCount);
  if SpellTimeOverCount >= 4 then Exit;
end;
LatestSpellTime := GetTickCount;
LatestSpellDelay := pum.Def.DelayTime;
```

### 3.2 剑术技能分派 (9438-9538)
```pascal
case pum.MagicId of
  SWD_LONGHIT:  BoAllowLongHit := not BoAllowLongHit; (9438-9450)
  SWD_WIDEHIT:  BoAllowWideHit := not BoAllowWideHit; (9451-9467)
  SWD_FIREHIT:  // MP消耗+HealthSpellChanged+BoAllowFireHit (9468-9484)
  SWD_RUSHRUSH: // MP消耗+CharRushRush+训练 (9485-9510)
  SWD_CROSSHIT: BoAllowCrossHit := not BoAllowCrossHit; (9512-9528)
  else:         DoSpell(pum, tx, ty, targ); (9538)
end;
```

---

## 4. Magic.pas SpellNow — 核心技能分发 (441-999)

### 4.1 头文件处理

```pascal
// 剑术技能直接退出 (534)
if IsSwordSkill(pum.MagicId) then begin
  Result := FALSE;
  Exit;
end;

// 广播施法动作 (537)
SendRefMsg(RM_SPELL, pum.pDef.Effect, xx, yy, pum.pDef.MagicId, '');
// → SM_SPELL (17): 所有视野玩家看到施法动画
```

### 4.2 嵌套辅助函数

#### GetRPow (442-448)
```pascal
function GetRPow(pw: word): byte;
begin
  if HiByte(pw) > LoByte(pw) then
    Result := LoByte(pw) + Random(HiByte(pw)-LoByte(pw)+1)
  else
    Result := LoByte(pw);
end;
```
从 packed word 中按 `[LoByte, HiByte]` 抽取随机 power 值; 当上限不大于下限时返回下限。

#### GetPower (449-453)
```pascal
function GetPower(pw: integer): integer;
begin
  Result := Round(pw / (pum.pDef.MaxTrainLevel + 1) * (pum.Level + 1))
          + (pum.pDef.DefMinPower + Random(pum.pDef.DefMaxPower - pum.pDef.DefMinPower));
end;
```
技能等级线性缩放后追加技能定义的随机基础 power。

#### GetPower13 (454-462)
```pascal
function GetPower13(pw: integer): integer;
begin
  Result := Round(pw / 3 + (pw - pw / 3) / (pum.pDef.MaxTrainLevel + 1) * (pum.Level + 1)
          + (pum.pDef.DefMinPower + Random(pum.pDef.DefMaxPower - pum.pDef.DefMinPower)));
end;
```
1/3固定 + 2/3按等级缩放, 并在同一个 `Round` 内加入技能定义随机基础 power。常用于道士技能。

#### CanUseBujuk (463-489)
检查 `U_BUJUK` 或 `U_ARMRINGL` 栏位是否有护身符 (StdMode=25, Shape=5, `Round(Dura/100) >= count-1`)。`Shape<=2` 只属于施毒粉分支。

#### UseBujuk (490-524)
消耗护身符耐久: `Dura := Dura - count*100`。如果耐久不足则删除物品。

### 4.3 SpellNow 本地变量 (526-531)
```pascal
var
  idx, sx, sy, ndir, pwr, train, nofire, needfire: integer;
  bhasitem: Boolean;
  pstd: PTStdItem;
  hum: TUserHuman;
```

**关键标志**:
- `nofire`: TRUE = 跳过后处理的 RM_MAGICFIRE/训练/Result TRUE; 部分分支先设 TRUE, 成功后再重置 FALSE
- `needfire`: FALSE = 跳过魔法特效广播
- `train`: TRUE = 技能使用成功, 训练熟练度

### 4.4 后处理 (983-998)
```pascal
if not nofire then begin
  // 发送魔法特效广播
  if needfire then
    SendRefMsg(RM_MAGICFIRE, MakeWord(EffectType, Effect), MakeLong(xx,yy), target, '');
    // → SM_MAGICFIRE (638)

  // 训练技能熟练度
  if (pum.Level < 3) and train then
    if Abil.Level >= pum.pDef.NeedLevel[pum.Level] then begin
      user.TrainSkill(pum, 1 + Random(3));
      if not CheckMagicLevelup(pum) then
        SendDelayMsg(user, RM_MAGIC_LVEXP, 0, pum.pDef.MagicId,
                     pum.Level, pum.CurTrain, '', 1000);
    end;
  Result := TRUE;
end;
```

---

## 5. 各技能详细分支

### 5.1 Fireball (1) / Lightning Bolt (5) — 行548-570

```
条件:
  - MagCanHitTarget(user.CX, user.CY, target)  → 视线/路径检查
  - IsProperTarget(target)                       → 可攻击目标
  - target.AntiMagic <= Random(10)                → 抗魔检查
  - abs(target.CX-xx)<=1 and abs(target.CY-yy)<=1 → 距离检查(目标在落点1格内)

Power计算:
  base = GetPower(MPow(pum)) + LoByte(WAbil.MC)
  range = ShortInt(HiByte(WAbil.MC) - LoByte(WAbil.MC)) + 1
  pwr = GetAttackPower(base, range)

消息:
  SendDelayMsg(user, RM_DELAYMAGIC, pwr, MakeLong(xx,yy), 2, integer(target), '', 600)
  → 600ms延迟, 服务端在延迟后对目标造成伤害

训练: train := TRUE if target.RaceServer >= RC_ANIMAL
失败: target := nil; 不排 RM_DELAYMAGIC/不造成伤害, 但入口 SM_SPELL 已发且后处理仍发送普通 RM_MAGICFIRE

Random调用:
  - Random(10) → 抗魔判定 [0,9]
  - MPow内部: Random(MaxPower - MinPower)
  - GetAttackPower内部: 2次 Random
```

**C++映射**: `map_actor_mail.hpp:3116` (anti-magic + delayed hit)

### 5.2 Healing (2) — 行699-721

```
条件:
  - target=nil → target:=user (默认治疗自己)
  - IsProperFriend(target) → 友方检查

Power计算:
  base = GetPower(MPow(pum)) + LoByte(WAbil.SC) * 2
  range = ShortInt(HiByte(WAbil.SC) - LoByte(WAbil.SC)) * 2 + 1
  pwr = GetAttackPower(base, range)

消息:
  if target.WAbil.HP < target.WAbil.MaxHP:
    SendDelayMsg(user, RM_MAGHEALING, 0, pwr, 0, 0, '', 800)
    → 800ms延迟, 治疗目标

训练: train := TRUE if HP < MaxHP

额外: if BoAbilSeeHealGauge: SendMsg(target, RM_INSTANCEHEALGUAGE, ...)
```

**C++映射**: `map_actor_mail.hpp:3133` (self-heal, delayed)

### 5.3 Poison (6) — 行734-829 (最大分支)

```
条件:
  - IsProperTarget(target)
  - 需要毒粉 (U_BUJUK 或 U_ARMRINGL): StdMode=25, Shape<=2, Dura>=100
  - Anti-poison: 6 >= Random(7 + target.AntiPoison)

Shape=1 (灰色毒粉):
  RM_MAKEPOISON, POISON_DECHEALTH (绿毒)
  power = GetPower13(30) + 2 * GetRPow(WAbil.SC)
  → 掉血毒

Shape=2 (黄色毒粉):
  RM_MAKEPOISON, POISON_DAMAGEARMOR (红毒)
  power = GetPower13(40) + 2 * GetRPow(WAbil.SC)
  → 增伤毒

训练: Target is human or >= RC_ANIMAL

nofire 初始为 TRUE; 有毒粉并完成分支后在 `Magic.pas:794` 重置为 FALSE, 因此成功施毒会进入后处理发送 RM_MAGICFIRE。缺道具或失败路径保持 nofire=TRUE 并由 SpellXY 发送 RM_MAGICFIRE_FAIL。

物品消耗: 毒粉耐久 -100, 如果耐久<100则删除

Random调用:
  - Random(7 + target.AntiPoison) → 抗毒判定 [0, 6+AntiPoison]
```

**C++映射**: `map_actor_mail.hpp:3151` (green/red poison, needs poison powder)

### 5.4 Repulsion (8) / Push (37) — 行572-577

```
条件:
  - MagPushArround(user, pum.Level) > 0

MagPushArround (Magic.pas:61-85):
  for each surrounding target:
    等级差 = user.Level - target.Level
    gate = Random(20)
    成功条件: gate < 6 + push_level*3 + level_gap
    成功: CharPushed(target, direction) → 击退目标

训练: train := TRUE if any pushed
```

**C++映射**: `map_actor_mail.hpp:3204`

### 5.5 Fire Line (9) / Lightning Line (10) — 行578-607

```
条件:
  - GetNextDirection(user.CX, user.CY, xx, yy) → 计算方向
  - Distance: 5 (magic 9) / 8 (magic 10)

Power计算 (同Fireball MC公式)

执行:
  MagPassThroughMagic(sx, sy, xx, yy, ndir, pwr, pierce_flag)
  - magic 9: pierce_flag = FALSE (单目标)
  - magic 10: pierce_flag = TRUE (贯通所有目标)

MagPassThroughMagic (ObjBase.pas:7138+):
  - 沿直线逐格查找目标
  - 每格: DirectAttack(target, pwr)
  - pierce=TRUE: 命中后继续前进; pierce=FALSE: 命中后停止

训练: train := TRUE if any hit
```

**C++映射**: `map_actor_mail.hpp:3268` (line attack)

### 5.6 Soul Fireball (11) / Holy Magic (35) — 行608-632

```
条件:
  - IsProperTarget(target)
  - target.AntiMagic <= Random(10)

Power计算 (同Fireball MC公式)

特殊:
  - magic 11: if undead → pwr := Round(pwr * 1.5)  [不死1.5倍]
  - magic 35: if not undead and not human → pwr := Round(pwr * 1.2)

消息:
  SendDelayMsg(user, RM_DELAYMAGIC, pwr, MakeLong(xx,yy), 2, integer(target), '', 600)

训练: train := TRUE if target >= RC_ANIMAL
```

**C++映射**: `map_actor_mail.hpp:3312` (undead bonus)

### 5.7 Soul Fire (13) — 行858-876

```
条件:
  - CanUseBujuk(user, 1) → 消耗1护身符
  - MagCanHitTarget
  - IsProperTarget

Power计算:
  base = GetPower13(MPow(pum)) + 5 * LoByte(WAbil.SC)
  range = 5 * (HiByte(WAbil.SC) - LoByte(WAbil.SC)) + 1
  pwr = GetAttackPower(base, range)

消息:
  SendDelayMsg(user, RM_DELAYMAGIC, pwr, MakeLong(xx,yy), 2, integer(target), '', 600)
```

**注意**: ID 13 与 ID 11 的区别: 13使用SC计算伤害+需要护身符, 11使用MC+不需要道具。

**C++映射**: `map_actor_mail.hpp:3473` (anti-magic, delayed hit)

### 5.8 Defence Area (14: 幽灵盾, 15: 神圣战甲术) — 行878-904

```
条件:
  - CanUseBujuk(user, 1)

Power计算:
  base = GetPower13(60) + 5 * LoByte(WAbil.SC)
  range = 5 * (HiByte(WAbil.SC) - LoByte(WAbil.SC)) + 1
  pwr = GetAttackPower(base, range)

执行:
  magic 14: MagMakeDefenceArea(xx, yy, 3, pwr, TRUE)   → 魔法防御
  magic 15: MagMakeDefenceArea(xx, yy, 3, pwr, FALSE)  → 物理防御

MagMakeDefenceArea (ObjBase.pas:7266+):
  - range=3 表示 x/y 从中心 -3 到 +3, 即 7x7 区域
  - 影响区域内所有友方

训练: train := TRUE
```

**C++映射**: `map_actor_mail.hpp:3535` (area buff)

### 5.9 DC Up (36) — 行887-894

```
条件:
  - CanUseBujuk(user, 1)

Power计算 (同14/15)

执行:
  MagDcUp(pwr, TRUE)

MagDcUp (ObjBase.pas:7304+):
  - 临时增加自身+召唤物 DC

训练: train := TRUE
```

**C++映射**: `map_actor_mail.hpp:3500` (self + slaves DC buff)

### 5.10 Holy Curtain (16: 困魔咒) — 行905-912

```
条件:
  - CanUseBujuk(user, 1)

Time计算:
  time = GetPower13(30 + GetRPow(WAbil.SC) * 2)  // 持续秒数(?)

执行:
  MagMakeHolyCurtain(user, time, xx, yy)

MagMakeHolyCurtain (Magic.pas:219-284):
  - 检查 CanWalk 在 (xx,yy) 及其周围8格
  - 创建 8 个 THolyCurtainEvent 在十字形位置
  - 添加到 HolySeizeList
  - 每个事件: 持续 time 秒, seize 范围内的怪物

训练: train := TRUE
```

**C++映射**: `map_actor_mail.hpp:3367` (area capture, event system)

### 5.11 Summon Skeleton (17) / Summon Shinsu (30) — 行913-966

```
条件:
  - magic 17: CanUseBujuk(user, 1) → 1个护身符
  - magic 30: CanUseBujuk(user, 5) → 5个护身符

执行:
  magic 17: MakeSlave(__WhiteSkeleton, pum.Level, 1, 10*24*60*60)
  magic 30: MakeSlave(__ShinSu, pum.Level, 1, 10*24*60*60)

MakeSlave (ObjBase.pas:5985+):
  - 创建Monster对象
  - 设置 Master = user
  - 生命期 = 10天 (10*24*60*60 秒)
  - 等级 = pum.Level (技能等级)

训练: train := TRUE
```

**C++映射**: `map_actor_mail.hpp:3459` (Skeleton), `:3466` (Shinsu)

### 5.12 Invisibility (18) / Group Invisibility (19) — 行919-930

```
条件:
  - CanUseBujuk(user, 1)

Time计算:
  time = GetPower13(30 + GetRPow(WAbil.SC) * 2)

执行:
  magic 18: MagMakePrivateTransparent(user, time)
  magic 19: MagMakeGroupTransparent(user, xx, yy, time)

MagMakePrivateTransparent (Magic.pas:387-416):
  - 检查 STATE_TRANSPARENT
  - 清除半径9格内动物的目标 (它们忘记你)
  - 设置 STATE_TRANSPARENT, BoHumHideMode, BoFixedHideMode
  - 持续时间: time 秒

MagMakeGroupTransparent (Magic.pas:418-439):
  - 对 (xx,yy) 周围1格内的友方施加透明
  - SendDelayMsg: RM_TRANSPARENT

训练: train := TRUE

nofire 初始为 TRUE; shared bujuk 分支成功后在 `Magic.pas:933` 重置为 FALSE, 因此成功隐身会进入后处理发送 RM_MAGICFIRE。
```

**C++映射**: `map_actor_mail.hpp:3582` (transparent)

### 5.13 Taming/Lightning Shock (20: 诱惑之光) — 行633-637

```
条件:
  - IsProperTarget(target)

执行:
  MagLightingShock(user, target, xx, yy, pum.Level)

MagLightingShock (Magic.pas:87-162):
  - 非人类目标:
    - 等级检查: level >= 5 → 退出
    - 随机判定: Random(100) < 成功率
    - 成功 → 转化为奴隶; 失败 → 伤害
  - 不死目标:
    - 伤害+可能即死

训练: train := TRUE if succeeds
```

**C++映射**: `map_actor_mail.hpp:3644` (taming, holy seize, damage)

### 5.14 Teleport (21: 瞬息移动) — 行643-649

```
条件: 无目标要求

执行:
  MagLightingSpaceMove(user, pum.Level)

MagLightingSpaceMove (Magic.pas:193-217):
  - Random(11) < 4 + level*2 → 成功
  - 成功: SendRefMsg(RM_SPACEMOVE_HIDE2, ...), RandomSpaceMove(HomeMap, 1)
  - 失败: 不掉落

训练: train := TRUE if moved

needfire := FALSE
```

**C++映射**: `map_actor_mail.hpp:3764` (random space move)

### 5.15 Fire Wall (22) — 行650-660

```
条件: 无 (可对地面施放)

Power:
  base = GetPower(MPow(pum)) + LoByte(WAbil.MC)
  range = ShortInt(HiByte(WAbil.MC) - LoByte(WAbil.MC)) + 1
  dam = GetAttackPower(base, range)

Duration:
  duration = GetPower(10) + GetRPow(WAbil.MC) / 2  // 持续秒数

执行:
  MagMakeFireCross(user, dam, duration, xx, yy)

MagMakeFireCross (Magic.pas:286-315):
  - 在 (xx,yy-1), (xx-1,yy), (xx,yy), (xx+1,yy), (xx,yy+1) 创建 5 个 TFireBurnEvent
  - 如果格子上已有事件, 不重复创建
  - 事件: OpenStartMs + duration, 每 run_tick_ms 造成 dam 伤害

训练: train := TRUE if any created
```

**C++映射**: `map_actor_mail.hpp:3787` (fire_burn events)

### 5.16 Big Explosion (23) / Poison Storm (33) — 行661-682

```
条件: 无 (范围伤害)

Power (同Fireball MC公式)

执行:
  MagBigExplosion(user, power, xx, yy, 1)

MagBigExplosion (Magic.pas:341-359):
  - 半径 1 (3x3区域)
  - 对每个非友方目标: SendDelayMsg(RM_MAGSTRUCK, ...)
  - 直接造成魔法伤害

训练: train := TRUE if any hit
```

**C++映射**: `map_actor_mail.hpp:3822` (area magic damage)

### 5.17 Elec Blizzard (24: 冰咆哮) — 行683-691

```
条件: 无

Power (同Fireball MC公式)

执行:
  MagElecBlizzard(user, power)

MagElecBlizzard (Magic.pas:361-381):
  - 半径 2 (5x5区域)
  - 非不死: power / 10  (只有10%伤害)
  - 不死: full power
  - 动物: full power

训练: train := TRUE if any hit
```

**C++映射**: `map_actor_mail.hpp:3837` (area, undead full dmg)

### 5.18 Magic Bubble (31: 魔法盾) — 行692-696

```
条件: 无 (自身施放)

Duration:
  duration = GetPower(15 + GetRPow(WAbil.MC))

执行:
  user.MagBubbleDefenceUp(pum.Level, duration)

MagBubbleDefenceUp (ObjBase.pas:7235+):
  - 设置 BoAbilMagBubbleDefence := TRUE
  - 设置 MagBubbleDefenceLevel := level
  - 持续时间: duration 秒
  - 减伤公式: Round(damage / 100 * (level + 2) * 8)

训练: train := TRUE
```

**C++映射**: `map_actor_mail.hpp:3909` (magic shield)

### 5.19 Turn Undead (32: 圣言术) — 行638-642

```
条件:
  - IsProperTarget(target)

执行:
  MagTurnUndead(user, target, xx, yy, pum.Level)

MagTurnUndead (Magic.pas:164-191):
  - 仅对不死怪物
  - 判定: Random(100) < 15 + level*7 + level_gap
  - 成功: target.Die (即死)
  - 失败: 无效果

训练: train := TRUE if killed
```

**C++映射**: `map_actor_mail.hpp:3925` (instant kill undead)

### 5.20 Mass Healing (29: 群体治愈术) — 行722-732

```
条件: 无 (范围治疗)

Power (同Healing SC*2公式)

执行:
  MagBigHealing(user, pwr, xx, yy)

MagBigHealing (Magic.pas:317-339):
  - 半径 1 (3x3区域)
  - 对每个友方: SendDelayMsg(RM_MAGHEALING, ...)
  - 如果目标 HP < MaxHP: 治疗

训练: train := TRUE if any healed
```

**C++映射**: `map_actor_mail.hpp:3883` (area heal)

### 5.21 Open Health (28: 心灵启示) — 行968-980

```
条件:
  - target <> nil
  - not target.BoOpenHealth

成功率: Random(6) <= 3 + pum.Level
  level=0: 4/6=67%, level=1: 5/6=83%, level=2: 6/6=100%, level=3: 6/6=100%

Duration:
  OpenHealthTime := GetPower13(30 + GetRPow(WAbil.SC) * 2) * 1000  // 毫秒

消息:
  SendDelayMsg(target, RM_DOOPENHEALTH, ...)

训练: train := TRUE if opened
```

**C++映射**: `map_actor_mail.hpp:3852` (reveals target HP)

---

## 6. Mag* 辅助函数调用链

### 6.1 MagPushArround (Magic.pas:61-85)
```
for i := 0 to 7:
  GetNextPosition at direction i
  if creature exists and level > target.level:
    gate = Random(20)
    if gate < 6 + push_level*3 + level_gap:
      CharPushed(target, opposite_direction)
```

### 6.2 MagPassThroughMagic (ObjBase.pas:7138+)
```
从 (sx,sy) 沿 ndir 方向逐格前进:
  for each cell:
    if creature exists and IsProperTarget:
      DirectAttack(target, pwr)
      if not pierce: break
```

### 6.3 MagCanHitTarget (ObjBase.pas:7170+)
```
检查:
  1. target 不为 nil
  2. target 不是自身
  3. IsProperTarget(target)
  4. 两点之间无墙阻挡
```

### 6.4 MagMakeHolyCurtain (Magic.pas:219-284)
```
在 (x,y) 周围十字方向创建 8 个 THolyCurtainEvent:
  for i := 0..7:
    nx, ny = x + dx[i], y + dy[i]
    if CanWalk(nx, ny, FALSE):
      create TFireBurnEvent at (nx, ny)
      event.type = holy_curtain
      event.continue_ms = time * 1000
      group same-event creatures into HolySeizeList
```

---

## 7. 技能失败行为汇总

| 失败原因 | MP消耗? | 发送动作? | 发送特效? | 动作锁? |
|---------|---------|---------|---------|--------|
| MP不足 | 否 (SpellXY阶段拒绝) | 否 | 否 | 否 |
| 道具不足 | 是 (DoSpell已扣MP; SpellNow内检查失败) | 是 (RM_SPELL已发) | 是 (SpellXY发送RM_MAGICFIRE_FAIL) | 是 |
| 目标无效 | 是 (Magic.pas中先扣MP再校验) | 是 (RM_SPELL已发) | 否 | 是 |
| 距离过远 | 是 | 是 | 否 | 是 |
| AntiMagic抵抗 | 是 | 是 | 可能 (needfire=true) | 是 |
| 抗毒抵抗 | 是 (MP已在DoSpell扣除; 毒粉已扣) | 是 (RM_SPELL已发) | 是 (分支完成后 nofire=false, 但无RM_MAKEPOISON) | 是 |
| Random(20) gate失败 | 是 (技能已施放) | 是 | 是 | 是 |

**关键**: 大多数失败路径仍消耗MP且触发动作锁。MP不足是入口前不消耗MP的路径; 普通技能缺道具发生在 SpellNow 内, 已经扣除 MP 并广播动作。

---

## 8. 技能熟练度训练

### 8.1 训练条件
```pascal
if (pum.Level < 3) and train then  // 未满级 + 成功使用
  if user.Abil.Level >= pum.pDef.NeedLevel[pum.Level] then begin
    user.TrainSkill(pum, 1 + Random(3));  // 增加1~3熟练度
    if not CheckMagicLevelup(pum) then    // 检查是否升级
      SendDelayMsg(user, RM_MAGIC_LVEXP, 0, pum.pDef.MagicId,
                   pum.Level, pum.CurTrain, '', 1000);
  end;
```

### 8.2 熟练度增加量
- 法师/道士技能: `1 + Random(3)` = 每次1-3点
- 战士基本剑术: `1 + Random(3)`
- 战士攻杀: `1 + Random(3)`
- 战士刺杀/半月/烈火/十字: `1` (每次固定1点)

### 8.3 升级条件
```pascal
pum.CurTrain >= pum.Def.MaxTrain[pum.Level]
```
每个等级有不同的 `MaxTrain` 阈值 (从 TDefMagic.MaxTrain 数组读取)。

---

## 9. 与 C++ 实现的对照

| Delphi Magic.pas | C++ map_actor_mail.hpp 行号 | 对齐状态 |
|------------------|---------------------------|---------|
| SpellNow (1: fireball) | ~3116 | 已实现 |
| SpellNow (2: healing) | ~3133 | 已实现 |
| SpellNow (5: lightning) | ~3116 | 已实现 |
| SpellNow (6: poison) | ~3151 | 已实现 |
| SpellNow (8: repulsion) | ~3204 | 已实现 |
| SpellNow (9: fire line) | ~3268 | 已实现 |
| SpellNow (10: lightning line) | ~3268 | 已实现 |
| SpellNow (11: soul fireball) | ~3312 | 已实现 |
| SpellNow (13: soul fire) | ~3473 | 已实现 |
| SpellNow (14/15: defence area) | ~3535 | 已实现 |
| SpellNow (16: holy curtain) | ~3367 | 已实现 |
| SpellNow (17/30: summon) | ~3459/~3466 | 已实现 |
| SpellNow (18/19: invisibility) | ~3582 | 已实现 |
| SpellNow (20: taming) | ~3644 | 已实现 |
| SpellNow (21: teleport) | ~3764 | 已实现 |
| SpellNow (22: fire wall) | ~3787 | 已实现 |
| SpellNow (23/33: big explosion) | ~3822 | 已实现 |
| SpellNow (24: blizzard) | ~3837 | 已实现 |
| SpellNow (28: open health) | ~3852 | 已实现 |
| SpellNow (29: mass healing) | ~3883 | 已实现 |
| SpellNow (31: magic bubble) | ~3909 | 已实现 |
| SpellNow (32: turn undead) | ~3925 | 已实现 |
| SpellNow (36: DC up) | ~3500 | 已实现 |
| MagPushArround | spell case 8 | 已实现 |
| MagPassThroughMagic | spell case 9/10 | 已实现 |
| MagBigExplosion | spell case 23/33 | 已实现 |
| MagElecBlizzard | spell case 24 | 已实现 |
| MagMakeFireCross | spell case 22 | 已实现 |
| MagMakeHolyCurtain | spell case 16 | 已实现 |
| MagMakePrivateTransparent | spell case 18 | 已实现 |
| MagMakeGroupTransparent | spell case 19 | 已实现 |

---

## 10. PR1 收口结论

- **MP 扣除**: 普通技能经 `DoSpell` 先执行 `GetSpellPoint`; MP 足够时立即 `DamageSpell(spell)` 和 `HealthSpellChanged`, 然后才进入 `MagicMan.SpellNow` (`ObjBase.pas:7106-7128`)。因此 SpellNow 分支内目标失败仍属于已消耗 MP 的失败。
- **施法动作失败路径**: `SpellNow` 入口先 `SendRefMsg(RM_SPELL, ...)` (`Magic.pas:537`), 然后执行分支目标/道具/抗性逻辑。PR6/PR7 需要保留“动作已广播, 效果失败”的 trace。
- **抗魔判定**: 魔法命中使用 `target.AntiMagic <= Random(10)` (`Magic.pas:552`), `Random(10)` 范围为 `[0,9]`。
- **火墙布局**: `MagMakeFireCross` 创建中心和上下左右 5 个格子, 由 `spell_rush.json` 之外的事件技能 trace 在 PR6/PR13 继续覆盖。
- **地狱雷光/冰系范围**: `MagElecBlizzard` 对不死目标全额, 对非不死目标 `pwr div 10` (`Magic.pas:361-381`)。
- **抗拒/野蛮 gate**: 推人门槛为 `Random(20) < 6 + level*3 + level_gap` (`Magic.pas:61-85`)。
- **符咒 fallback**: `CanUseBujuk` 同时检查 `U_BUJUK` 和 `U_ARMRINGL` (`Magic.pas:463-489`), `UseBujuk` 按耐久扣除 (`Magic.pas:490-524`)。
- **治愈附加消息**: 治愈成功可发送 `RM_INSTANCEHEALGUAGE`; PR7 可记录为可视辅助消息, 不改变治疗数值。
- **圣言术**: 成功率为 `Random(100) < 15 + level*7 + level_gap` (`Magic.pas:176`), 已归入 P1 规则而非视觉项。
- **诱惑之光**: 驯服/伤害/麻痹分支由 PR7 作为道士/法师控制技能处理, PR1 只冻结源码入口和随机规则。
- **技能训练**: 普通法师/道士成功训练 `1 + Random(3)` (`Magic.pas:992`), 高级剑术多为固定 `1` (`ObjBase.pas:5470-5527`)。
