# Delphi 普通攻击完整调用链

> 基于 `F:\mir2\Source\M2Server\ObjBase.pas` (14,718行) + `UsrEngn.pas` (2,687行) + `Source\Client\ClMain.pas` (6,219行) 静态审查
> 所有行号基于当前源码版本

---

## 1. 总览链路

```
[客户端] CM_HIT (3014) → SendMsg
[网络层] RunSocket.Run → UserEngine.ExecuteRun
[服务端] UsrEngn.pas:2168 → hum.Operate
  → ObjBase.pas:11811 TUserHuman.Operate
    → message loop:12050 GetMsg(msg)
      → CM_HIT → 12081: HitXY(Ident, x, y, dir)  [ObjBase.pas:9302]
        → speedhack check (9309-9316)
        → inherited HitHit(nil, hitmode, dir)  [ObjBase.pas:5550]
          → CheckWeaponUpgradeResult (5550-5608)
          → _Attack(hitmode, targ)  [ObjBase.pas:5252]
            ├─ DirectAttack (5253-5269)
            ├─ SwordLongAttack (5270-5284)
            ├─ SwordWideAttack (5285-5304)
            ├─ SwordCrossAttack (5306-5328)
            ├─ GetAttackPower (5368/5392) → [5236-5250]
            ├─ targ.GetHitStruckDamage(self, dam) (5443) → [3433-3449]
            ├─ targ.StruckDamage(dam, self) (5448) → [3470-3581]
            │   └─ DamageHealth(damage) (3579) → [3585-3608]
            └─ skill training (5470-5527)
          → HitMotion(msg, dir, CX, CY) (5687) → SendRefMsg
```

---

## 2. 客户端发起攻击

### 2.1 CanNextHit — ClMain.pas:3348-3362

**攻击速度公式**:

```pascal
function CanNextHit: Boolean;
var
  levelfast, nexthit: Integer;
begin
  levelfast := _MIN(370, Level * 14);           // 等级加速, 最多 370
  levelfast := _MIN(800, levelfast + HitSpeed * 60); // 攻速修正, 最多 800
  if BoAttackSlow then
    nexthit := 1400 - levelfast + 1500          // 减速
  else
    nexthit := 1400 - levelfast;
  if nexthit < 0 then nexthit := 0;              // 最小间隔 0ms
  Result := GetTickCount - LastHitTime > nexthit;
end;
```

**参数来源**:
| 参数 | Delphi 来源 | 说明 |
|------|------------|------|
| `Level` | `TUserHuman.Abil.Level` | 玩家等级 |
| `HitSpeed` | `TUserHuman.HitSpeed` | 攻速属性 (0=默认, +值=更快, -值=更慢) |
| `BoAttackSlow` | 武器/状态效果 | 攻击减速标志 |
| `LastHitTime` | `TUserHuman.LastHitTime` | 上次攻击时间戳 (GetTickCount) |

**公式行为**:
| Level | HitSpeed | BoAttackSlow | nexthit |
|-------|----------|-------------|---------|
| 1 | 0 | false | 1400 - 14 = 1386ms |
| 20 | 0 | false | 1400 - 280 = 1120ms |
| 27 | 0 | false | 1400 - 370(MAX) = 1030ms |
| 40 | 0 | false | 1400 - 370 = 1030ms (已达上限) |
| 40 | +5 | false | 1400 - min(800, 370+300) = 730ms |
| 40 | 0 | true | 1400 - 370 + 1500 = 2530ms |

### 2.2 ServerAcceptNextAction — ClMain.pas:3324-3335

```pascal
function ServerAcceptNextAction: Boolean;
begin
  Result := not ActionLock or (GetTickCount - ActionLockTime > 10000);
end;
```
- 客户端动作锁: 发送攻击/施法后设置 `ActionLock:=TRUE`
- 10秒超时保护: 如果服务端 10 秒未响应, 自动解锁

### 2.3 AttackTarget — ClMain.pas:2174-2221

```pascal
// 选择攻击标识 (hitmode): 先按武器初始化, 后续蓄力/开关技能可覆盖
hitmsg := CM_HIT;
if weapon.StdMode = 6 then hitmsg := CM_HEAVYHIT;
if BoNextTimeFireHit then         → CM_FIREHIT
else if BoNextTimePowerHit then   → CM_POWERHIT
else if BoCanWideHit then         → CM_WIDEHIT
else if BoCanCrossHit then        → CM_CROSSHIT
else if BoCanLongHit and range_ok then → CM_LONGHIT

// 守卫条件
if CanNextAction and ServerAcceptNextAction and CanNextHit then
  Myself.SendMsg(hitmsg, Myself.XX, Myself.YY, dir, 0, 0, '', 0);
  ActionLock := TRUE;
```

---

## 3. 服务端网络层 → TUserHuman.Operate

### 3.1 ExecuteRun 主循环 — UsrEngn.pas:2525-2583

```
每帧执行顺序:
1. ProcessUserHumans   (hum.Operate — 处理玩家消息)
2. ProcessMonsters     (cret.Run — 怪物AI)
3. ProcessMerchants    (cret.Run — 商人)
4. ProcessNpcs         (cret.Run — NPC)
5. ProcessMissions     (每1000ms)
6. CheckHolySeizeValid (每1000ms)
7. CheckOpenDoors      (每500ms)
```

### 3.2 TUserHuman.Operate — ObjBase.pas:11811-13066

**阶段1: Periodic Housekeeping** (11811-12046)
```
- BoDealing 处理 (11827-11832)
- BoAllowFireHit 20秒超时 (11841-11851)
- operatetime_30sec: 20秒间隔, 活动广播 (11860-11870)
- operatetime: 3秒间隔, 重复检查/加速检测 (11872-11898)
- operatetime_sec: 1秒间隔, 日志/行会战/组队/召唤物广播 (11906-11993)
- operatetime_500m: 500ms间隔, 活动道具检查 (11995-12022)
```

**阶段2: Message Dispatch** (12049-13010)
```pascal
while GetMsg(msg) do begin
  case msg.Ident of
    CM_TURN (3010):    TurnXY
    CM_WALK (3011):    WalkXY
    CM_RUN (3013):      RunXY
    CM_SITDOWN (3012):  SitdownXY
    CM_HIT (3014):              ┐
    CM_HEAVYHIT (3015):         │
    CM_BIGHIT (3016):           │
    CM_POWERHIT (3018):         ├→ HitXY(Ident, x, y, dir)
    CM_LONGHIT (3019):          │
    CM_WIDEHIT (3024):          │
    CM_FIREHIT (3025):          │
    CM_CROSSHIT (3035):         ┘
    CM_SPELL (3017):    SpellXY(magid, tx, ty, targ)
    CM_THROW (3005):    (stub)
    CM_SAY (....):      Say
    // ... item/guild/deal/npc handlers ...
  end;
end;
```

**阶段3: Fallback** (13024-13053)
```pascal
else inherited RunMsg(msg);   // 处理未知消息
// 关闭/ghost 处理
inherited Run;                // 调用 TCreature.Run (再生/毒/死亡)
```

---

## 4. HitXY — 攻击入口

**位置**: ObjBase.pas:~9302

```pascal
procedure TUserHuman.HitXY(Ident: Integer; x, y, dir: Integer);
```

**Speedhack 检查** (9309-9316):
```pascal
// 检查攻击间隔
hitinterval := 900 - (HitSpeed * 60);
if GetTickCount - LatestHitTime < hitinterval then begin
  Inc(HitTimeOverCount);
  Inc(HitTimeOverSum);
  if HitTimeOverCount >= 4 then Exit;    // 4次超频=阻止
  if HitTimeOverSum >= 6 then Exit;       // 累计6次=阻止
end;
LatestHitTime := GetTickCount;
```

**攻击分派** (9333-9352):
```pascal
// 特殊: CM_HEAVYHIT + weapon Shape=19 → DigUpMine (挖矿)
if (Ident = CM_HEAVYHIT) and (weapon.Shape = 19) then begin
  DigUpMine(x, y);
  Exit;
end;

// 普通攻击: 调用祖先 HitHit
inherited HitHit(nil, hitmode, dir);
```

---

## 5. HitHit — 攻击广播与执行

**位置**: ObjBase.pas:5550-5688

**签名**: `procedure TCreature.HitHit(target: TCreature; hitmode, dir: word)`

### 5.1 内部函数: IdentifyWeapon (5551-5563)
识别升级后的武器 look 值。

### 5.2 内部函数: CheckWeaponUpgradeResult (5564-5608)
```pascal
// 检查武器升级结果 (从 Desc[10] 读取)
if weapon broken (Index=0):
  SendRefMsg(RM_BREAKWEAPON, 0,0,0,0, '')  // SM_BREAKWEAPON (1102)
if upgrade successful:
  RecalcAbilitys
  SendMsg(RM_ABILITY)                        // SM_ABILITY (52)
  SendMsg(RM_SUBABILITY)                     // SM_SUBABILITY (752)
```

### 5.3 内部函数: GetSWSpell (5609-5612)
```pascal
// 计算剑术技能 MP 消耗
Result := Round(pum.Def.Spell / (pum.Def.MaxTrainLevel+1) * (pum.Level+1)) + pum.Def.DefSpell
```

### 5.4 主流程

```pascal
// Step 1: 方向设置
Self.Dir := dir;

// Step 2: 目标回退
if target = nil then target := GetFrontCret;

// Step 3: 宽/十字斩 MP 消耗 (5618-5636)
if hitmode = HM_WIDEHIT then begin
  mp_cost := GetSWSpell(PWideHitSkill) + DefSpell;
  if mp_cost <= MP then begin
    DamageSpell(mp_cost);
    HealthSpellChanged;
  end else
    hitmode := HM_HIT;  // MP不足, 降级为普攻
end;

// Step 4: 武器升级检查 (5639-5648)
CheckWeaponUpgradeResult;

// Step 5: 保存/重置标志 (5650-5654)
bo_allow_power := BoAllowPowerHit; BoAllowPowerHit := FALSE;
bo_allow_fire := BoAllowFireHit; BoAllowFireHit := FALSE;

// Step 6: 执行攻击 (5653)
_Attack(hitmode, targ);

// Step 7: 目标选择 (5654)
if _Attack result then SelectTarget(targ);

// Step 8: 广播攻击动作 (5656-5688)
case hitmode of
  HM_HIT:      msg := RM_HIT;       // SM_HIT (14)
  HM_HEAVYHIT: msg := RM_HEAVYHIT;  // SM_HEAVYHIT (15)
  HM_BIGHIT:   msg := RM_BIGHIT;    // SM_BIGHIT (16)
  HM_POWERHIT: msg := RM_POWERHIT;  // SM_POWERHIT (18)
  HM_LONGHIT:  msg := RM_LONGHIT;   // SM_LONGHIT (19)
  HM_WIDEHIT:  msg := RM_WIDEHIT;   // SM_WIDEHIT (24)
  HM_FIREHIT:  msg := RM_FIREHIT;   // SM_FIREHIT (8)
  HM_CROSSHIT: msg := RM_CROSSHIT;  // SM_CROSSHIT (35)
end;
HitMotion(msg, Self.Dir, CX, CY);       // 内部调用 SendRefMsg
```

---

## 6. _Attack — 伤害计算核心

**位置**: ObjBase.pas:5252-5547

**签名**: `function TCreature._Attack(hitmode: word; targ: TCreature): Boolean`

**返回值**: TRUE 如果造成了伤害

### 6.1 内部函数: DirectAttack (5253-5269)

```pascal
function DirectAttack(target: TCreature; damage: integer): Boolean;
begin
  Result := FALSE;
  // 安全检查 (5256-5257)
  if (Self.RaceServer = RC_USERHUMAN) and (target.RaceServer = RC_USERHUMAN) then
    if (Self in SafeZone) or (target in SafeZone) then Exit;
  // 目标验证 (5258)
  if not IsProperTarget(target) then Exit;
  // 命中判定 (5259)
  if Random(target.SpeedPoint) < AccuracyPoint then begin
    // 命中! (5260-5262)
    target.StruckDamage(damage, Self);
    SendDelayMsg(RM_STRUCK, RM_REFMESSAGE, damage, target.WAbil.HP,
                 target.WAbil.MaxHP, Longint(Self), '', 500);
    if target.RaceServer <> RC_USERHUMAN then
      target.SendMsg(target, RM_STRUCK, damage, target.WAbil.HP,
                     target.WAbil.MaxHP, Longint(Self), '');
    Result := TRUE;
  end;
  // MISS = Result FALSE, 不发送消息给目标
end;
```

**关键**: `Random(target.SpeedPoint) < AccuracyPoint`
- 命中条件: 随机值 [0, SpeedPoint-1] < AccuracyPoint
- 例如 AccuracyPoint=10, SpeedPoint=15: 命中率 = 10/15 = 66.7%
- **MISS 时不广播任何消息给目标** — 仅攻击者客户端本地可知 (因为 `_Attack` 返回 FALSE, 但 HitHit 仍广播了攻击动作)

### 6.2 内部函数: SwordLongAttack (5270-5284)

```pascal
function SwordLongAttack(damage: integer): Boolean;
begin
  Result := FALSE;
  // 计算2格前方位置 (5276)
  GetNextPosition(Self.CX, Self.CY, Self.Dir, 2, nx, ny);
  target := Envir.GetCret(nx, ny);
  if (target <> nil) and IsProperTarget(target) then begin
    Result := DirectAttack(target, damage);
    if Result then SelectTarget(target);
  end;
end;
```
- 刺杀剑术: 攻击 2 格远的正前方目标
- **隔位无视防御**: `DirectAttack` 直接调用 `target.StruckDamage(damage, Self)`, 不经过 `_Attack` 主目标路径的 `GetHitStruckDamage`; 因此刺杀/半月/十字二级命中不再做 AC 减伤。

### 6.3 内部函数: SwordWideAttack (5285-5304)

```pascal
const valarr: array[0..2] of Integer = (7, 1, 2);  // 方向偏移: 前方+左前+右前
function SwordWideAttack(damage: integer): Boolean;
begin
  Result := FALSE;
  for i := 0 to 2 do begin
    ndir := (Dir + valarr[i]) mod 8;
    GetNextPosition(CX, CY, ndir, 1, nx, ny);
    target := Envir.GetCret(nx, ny);
    if (target <> nil) and IsProperTarget(target) then begin
      if DirectAttack(target, damage) then begin
        Result := TRUE;
        SelectTarget(target);
      end;
    end;
  end;
end;
```
- 半月弯刀: 攻击前方、左前方、右前方 3 个目标
- **所有目标伤害相同** (都使用同一个 `damage` 值) — 无伤害衰减

### 6.4 内部函数: SwordCrossAttack (5306-5328)

```pascal
const valarr: array[0..6] of Integer = (7, 1, 2, 3, 4, 5, 6);  // 除身后外所有方向
function SwordCrossAttack(damage: integer): Boolean;
begin
  Result := FALSE;
  for i := 0 to 6 do begin
    ndir := (Dir + valarr[i]) mod 8;
    GetNextPosition(CX, CY, ndir, 1, nx, ny);
    target := Envir.GetCret(nx, ny);
    if (target <> nil) and IsProperTarget(target) then begin
      // 非人类目标 = 全额伤害; 人类目标 = 80% 伤害
      if target.RaceServer <> RC_USERHUMAN then
        Result := DirectAttack(target, damage) or Result
      else
        Result := DirectAttack(target, Round(damage * 0.8)) or Result;
      if Result then SelectTarget(target);
    end;
  end;
end;
```
- 十字斩: 攻击周围 7 个方向
- **PvP 伤害 = 80%**

### 6.5 主流程: 伤害计算 (5366-5398)

```pascal
// Step 1: 计算基础攻击力 (5368-5392)
if targ <> nil then begin
  dam := GetAttackPower(LoByte(DC), HiByte(DC) - LoByte(DC));
  // HM_POWERHIT: 额外伤害 (5401)
  if hitmode = HM_POWERHIT then
    dam := dam + HitPowerPlus;
  // HM_FIREHIT: 烈火倍率 (5403-5405)
  if hitmode = HM_FIREHIT then begin
    dam := dam + Round(dam / 100 * (HitDouble * 10));
  end;
end else begin
  // 空挥: 也计算 power (5392)
  dam := GetAttackPower(LoByte(DC), HiByte(DC) - LoByte(DC));
end;
```

### 6.6 主流程: 多目标攻击 (5401-5427)

```pascal
// 根据 hitmode 计算二级伤害并执行多目标攻击
case hitmode of
  HM_LONGHIT:  seconddam := Round(dam / (PLongHitSkill.pDef.MaxTrainLevel+2) * (PLongHitSkill.Level+2));
               SwordLongAttack(seconddam);
  HM_WIDEHIT:  seconddam := Round(dam / (PWideHitSkill.pDef.MaxTrainLevel+10) * (PWideHitSkill.Level+2));
               SwordWideAttack(seconddam);
  HM_CROSSHIT: seconddam := Round(dam / (PCrossHitSkill.pDef.MaxTrainLevel+11) * (PCrossHitSkill.Level+3));
               SwordCrossAttack(seconddam);
end;
```

**关键发现**:
- 刺杀伤害 = `Round(dam / (MaxTrainLevel+2) * (Level+2))` (`ObjBase.pas:5406`)
- 半月伤害 = `Round(dam / (MaxTrainLevel+10) * (Level+2))` (`ObjBase.pas:5415`)
- 十字伤害 = `Round(dam / (MaxTrainLevel+11) * (Level+3))` (`ObjBase.pas:5424`)
- 其中 `dam` 是调用 `GetAttackPower` 获取的基础攻击力(不含烈火倍率)
- 非玩家攻击者走 `seconddam := dam` fallback。
- 烈火倍率 (`HitDouble`) 仅用于主目标

### 6.7 主流程: 主目标攻击 (5429-5532)

```pascal
// Step 1: 目标验证 (5429-5431)
if targ = nil then Exit;

// Step 2: 命中判定 (5432-5438)
if IsProperTarget(targ) then begin
  if AccuracyPoint > Random(targ.SpeedPoint) then begin
    // 命中!
  end else
    dam := 0;  // MISS
end else
  dam := 0;

// Step 3: 护甲减伤 (5443)
dam := targ.GetHitStruckDamage(Self, dam);

// Step 4: 武器耐久损耗计算 (5444)
weapondamage := Random(5) + 2 - AddAbil.WeaponStrong;

// Step 5: 应用伤害 (5448)
targ.StruckDamage(dam, Self);

// Step 6: RM_STRUCK 延迟消息 (5449)
SendDelayMsg(RM_STRUCK, RM_REFMESSAGE, dam, targ.WAbil.HP,
             targ.WAbil.MaxHP, Longint(Self), '', 200);

// Step 7: 石化触发 (5452-5454)
if BoAbilMakeStone then
  if Random(5 + targ.AntiPoison) = 0 then
    targ.MakePoison(POISON_STONE, 5, 0);

// Step 8: 吸血 (5459-5466)
if SuckupEnemyHealthRate > 0 then begin
  SuckupEnemyHealth := SuckupEnemyHealth + dam * SuckupEnemyHealthRate / 100;
  n := Trunc(SuckupEnemyHealth);
  if n >= 2 then begin
    DamageHealth(-n);  // 负值=回血
    SuckupEnemyHealth := SuckupEnemyHealth - n;
  end;
end;

// Step 9: 技能训练 (5470-5527)
// 基本剑术 (3): 1+Random(3) 熟练度
// 攻杀 (4): 1+Random(3) 熟练度
// 刺杀: 1 熟练度 (每次)
// 半月: 1 熟练度 (每次)
// 烈火: 1 熟练度 (每次)
// 十字: 1 熟练度 (每次)

// Step 10: 非人类目标 internal immediate RM_STRUCK (5542-5543)
if targ.RaceServer <> RC_USERHUMAN then
  targ.SendMsg(RM_STRUCK, dam, targ.WAbil.HP, targ.WAbil.MaxHP, Longint(Self), '');
```

**武器耐久损耗** (5535-5539):
```pascal
if weapondamage > 0 then
  if weapon_equipped then
    DoDamageWeapon(weapondamage);
```

### 6.8 Random() 调用汇总 (_Attack)

| # | 行号 | 调用 | 范围 | 用途 |
|---|------|------|------|------|
| 1 | 5259 | `Random(target.SpeedPoint)` | [0, SpeedPoint-1] | 主目标命中 |
| 2 | 5368 | `GetAttackPower` 内部 | [0, ranval] + gate | 攻击力随机 |
| 3 | 5444 | `Random(5) + 2 - WeaponStrong` | [2, 6] - WeaponStrong | 武器耐久损耗 |
| 4 | 5454 | `Random(5 + targ.AntiPoison)` | [0, 4+AntiPoison] | 石化触发 |
| 5 | 5472 | `1 + Random(3)` | [1, 3] | 基本剑术训练 |
| 6 | 5483 | `1 + Random(3)` | [1, 3] | 攻杀剑术训练 |

---

## 7. GetAttackPower — 攻击力计算

**位置**: ObjBase.pas:5236-5250

```pascal
function TCreature.GetAttackPower(damage, ranval: integer): integer;
begin
  if ranval < 0 then ranval := 0;

  if Luck > 0 then begin
    // 幸运一击: gate=0 → 取最大值
    if Random(10 - MIN(9, Luck)) = 0 then
      Result := damage + ranval
    else
      Result := damage + Random(ranval + 1);
  end else begin
    // 普通 + 诅咒
    Result := damage + Random(ranval + 1);
    if Luck < 0 then
      // 诅咒一击: gate=0 → 取最小值
      if Random(10 - MAX(0, -Luck)) = 0 then
        Result := damage;
  end;
end;
```

**参数含义**:
- `damage`: LoByte(DC) = DC 下限
- `ranval`: HiByte(DC) - LoByte(DC) = DC 范围

**幸运公式**:
| Luck | gate_range | 幸运一击概率 | 期望伤害 |
|------|-----------|------------|---------|
| 0 | N/A | 0% | DC_min + (DC_range)/2 |
| 1 | Random(9) | 1/9 = 11.1% | 接近上限 |
| ... | ... | ... | ... |
| 9 | Random(1) | 1/1 = 100% | **总是最大值** |

**诅咒公式**:
| Luck | gate_range | 诅咒一击概率 | 期望伤害 |
|------|-----------|------------|---------|
| -1 | Random(9) | 1/9 = 11.1% | 接近下限 |
| ... | ... | ... | ... |
| -10 | Random(0) = 0 → gate 永远=0 | 100% | **总是最小值** |

**Random() 调用**:
- `Random(ranval + 1)` — 伤害随机 [0, ranval]
- `Random(10 - MIN(9, Luck))` — 幸运 gate [0, 10-luck-1]
- `Random(10 - MAX(0, -Luck))` — 诅咒 gate [0, 10+luck-1]

---

## 8. GetHitStruckDamage — 物理减伤

**位置**: ObjBase.pas:3433-3449

```pascal
function TCreature.GetHitStruckDamage(hiter: TCreature; damage: integer): integer;
var
  armor: integer;
begin
  armor := LoByte(WAbil.AC) + Random(HiByte(WAbil.AC) - LoByte(WAbil.AC) + 1);
  damage := MAX(0, damage - armor);

  // 不死加成
  if (LifeAttrib = LA_UNDEAD) and (hiter <> nil) then
    damage := damage + hiter.AddAbil.UndeadPower;

  // 魔法泡泡防御
  if (damage > 0) and BoAbilMagBubbleDefence then begin
    damage := Round(damage / 100 * (MagBubbleDefenceLevel + 2) * 8);
    DamageBubbleDefence;
  end;

  Result := damage;
end;
```

**公式**:
```
AC = AC_min + Random(AC_max - AC_min + 1)
damage = max(0, attack_power - AC)
if undead: damage += attacker.UndeadPower
if magic_bubble: damage = Round(damage / 100 * (level + 2) * 8)
```

**Random()**: `Random(HiByte(AC) - LoByte(AC) + 1)` — [0, AC_range]

**注意**: `armor` 使用 `ShortInt(HiByte(WAbil.AC)-LoByte(WAbil.AC)) + 1` 做类型转换 — Delphi 中 `Byte` 范围 0-255, `ShortInt` 范围 -128~127

---

## 9. GetMagStruckDamage — 魔法减伤

**位置**: ObjBase.pas:3452-3468

**与 GetHitStruckDamage 结构完全相同**, 但使用 `WAbil.MAC` 替代 `WAbil.AC`:

```pascal
armor := LoByte(WAbil.MAC) + Random(HiByte(WAbil.MAC) - LoByte(WAbil.MAC) + 1);
```

---

## 10. StruckDamage — 装备耐久+伤害应用

**位置**: ObjBase.pas:3470-3581

```pascal
procedure TCreature.StruckDamage(damage: integer; hiter: TCreature);
```

### 10.1 流程

```
Step 1 (3476-3478): 记录攻击者
  if (damage > 0) and (hiter <> nil): SetLastHiter(hiter)

Step 2 (3483-3489): 武器耐久损耗
  wdam := Random(10) + 5
  if POISON_DAMAGEARMOR: wdam *= 1.2; damage *= 1.2

Step 3 (3492-3530): 衣服耐久
  if dress_equipped and has_dura:
    reduce dress.Dura by wdam
    if broken: bocalc := TRUE
    send RM_DURACHANGE

Step 4 (3533-3570): 其他装备耐久
  for each equipped item (U_HELMET..U_RINGR, excl U_BUJUK):
    if Random(8) = 0: reduce dura by wdam
    if broken: bocalc := TRUE

Step 5 (3572-3576): 属性重算
  if bocalc: RecalcAbilitys; SendMsg(RM_ABILITY); SendMsg(RM_SUBABILITY)

Step 6 (3579): 应用伤害到 HP
  DamageHealth(damage)
```

**关键**: 装备耐久损耗在 `DamageHealth` **之前**处理。这意味着红毒 (POISON_DAMAGEARMOR) 会同时增加装备损耗和实际伤害。

**Random() 调用**:
- `Random(10) + 5` — 基础装备损耗 [5, 14]
- `Random(8)` — 每件装备 1/8 概率损坏 (每个受击独立判定)

---

## 11. DamageHealth — HP 修改+魔法盾吸收

**位置**: ObjBase.pas:3585-3608

```pascal
procedure TCreature.DamageHealth(damage: integer);
var
  spdam: integer;
begin
  // 魔法盾吸收 (3589-3600)
  if BoMagicShield and (damage > 0) and (WAbil.MP > 0) then begin
    spdam := Round(damage * 1.5);      // MP 消耗 = 伤害 × 1.5
    if WAbil.MP >= spdam then begin
      WAbil.MP := WAbil.MP - spdam;     // 完全吸收
      spdam := 0;
    end else begin
      spdam := spdam - WAbil.MP;        // MP 不足, 剩余穿透
      WAbil.MP := 0;
    end;
    damage := Round(spdam / 1.5);       // 穿透伤害转回 HP 伤害
    HealthSpellChanged;                // 广播 HP/MP 变化
  end;

  // HP 修改 (3601-3607)
  if damage > 0 then
    WAbil.HP := MAX(0, WAbil.HP - damage)   // 受伤
  else
    WAbil.HP := MIN(WAbil.MaxHP, WAbil.HP - damage);  // 治疗 (负值)
end;
```

**魔法盾公式**:
```
MP消耗 = Round(伤害 × 1.5)
如果 MP >= MP消耗: MP -= MP消耗, HP不减少
如果 MP < MP消耗: MP = 0, HP减少 = Round((MP消耗 - 原MP) / 1.5)
```

**魔法泡泡防御 vs 魔法盾**:
- **魔法泡泡** (`BoAbilMagBubbleDefence`): 在 `GetHitStruckDamage`/`GetMagStruckDamage` 中减伤
- **魔法盾** (`BoMagicShield`): 在 `DamageHealth` 中用 MP 吸收伤害
- 两者是独立的效果, 可叠加 (先泡泡减伤, 再盾吸收)

---

## 12. HealthSpellChanged — HP/MP变化广播

**位置**: ObjBase.pas:6409-6415

```pascal
procedure TCreature.HealthSpellChanged;
begin
  if RaceServer = RC_USERHUMAN then
    UpdateMsg(self, RM_HEALTHSPELLCHANGED, 0, 0, 0, 0, '');
  if BoOpenHealth then
    SendRefMsg(RM_HEALTHSPELLCHANGED, 0, 0, 0, 0, '');
end;
```

**行为**:
- `UpdateMsg` → 仅发送给该玩家自己的客户端 (RM_HEALTHSPELLCHANGED → SM_HEALTHSPELLCHANGED (53))
- `SendRefMsg` → 如果开启了血量显示 (心灵启示), 广播给周围所有玩家

**Operate 中的处理** (12413-12421):
```pascal
// TUserHuman.Operate 处理 RM_HEALTHSPELLCHANGED 消息
// 发送 SM_HEALTHSPELLCHANGED (53) 给客户端 socket
// 字段: msg.Sender.WAbil.HP, msg.Sender.WAbil.MP, msg.Sender.WAbil.MaxHP
```

---

## 13. Die — 死亡处理

**位置**: ObjBase.pas:2583-2810

### 13.1 阶段1: 状态设置 (2597-2607)

```pascal
if NeverDie then Exit;
Death := TRUE;
DeathTime := GetTickCount;
// 清除 PK 列表
// 清零 IncHealth/IncSpell/IncHealing
```

### 13.2 阶段2: 经验分配 (2611-2676)

```pascal
// 如果目标是怪物 (RaceServer <> RC_USERHUMAN) 且有 LastHiter:
if ExpHiter is TUserHuman then begin
  exp := ExpHiter.CalcGetExp(Self.Level, Self.FightExp);
  ExpHiter.GainExp(exp);                  // 直接击杀者得经验
  // MapQuest 检查
end;
if ExpHiter.Master <> nil then begin     // 奴隶击杀
  ExpHiter.GainSlaveExp(Self.Level);
  exp := ExpHiter.Master.CalcGetExp(Self.Level, Self.FightExp);
  ExpHiter.Master.GainExp(exp);           // 主人得经验
end;
```

### 13.3 阶段3: PK 惩罚 (2678-2732)

```pascal
// 坏杀判定: 非战斗区域 + 击杀人类 + PKLevel < 2
if BoBadKill then begin
  // 行会战检查 → guildwarkill
  // 攻城战检查 → guildwarkill
  if not guildwarkill and not IsGoodKilling then begin
    LastHiter.IncPkPoint(100);            // PK值 +100
    LastHiter.AddBodyLuck(-500);          // 诅咒 -500
    if PKLevel < 1 then
      if Random(5) = 0 then               // 20% 概率
        LastHiter.MakeWeaponUnlock;       // 武器被诅咒
  end;
end;
```

### 13.4 阶段4: 物品掉落+广播 (2734-2809)

```pascal
// 非战斗区域 / 非Fight3Zone / 非动物:
if not FightZone and not Fight3Zone and not BoAnimal then begin
  if RaceServer <> RC_USERHUMAN then begin
    DropUseItems(ehiter);      // 掉落使用中物品
    ScatterBagItems(ehiter);  // 散落背包物品
    ScatterGolds(ehiter);     // 散落金币
  end else begin
    DropUseItems(nil);         // 玩家死亡掉落
    ScatterBagItems(nil);
    AddBodyLuck(...);          // 死亡惩罚
  end;
end;

// 广播死亡
SendRefMsg(RM_DEATH, Dir, CX, CY, 1, '');   // SM_DEATH (32)
```

**Random()**: `Random(5)` — 武器被诅咒概率 20% (PKLevel < 1 时)

---

## 14. 消息发送顺序总结

### 14.1 攻击流程消息顺序

```
1. RM_HIT / RM_HEAVYHIT / ... (HitMotion → SendRefMsg)
   → 广播攻击动作给所有视野玩家
   → 客户端播放攻击动画

2. RM_STRUCK (SendDelayMsg, 200ms client-visible delay for main target)
   → 发送受击消息给目标, 最终转为客户端 SM_STRUCK
   → 客户端播放受击动画

2a. RM_STRUCK internal (SendMsg immediate for non-human targets)
   → 怪物 RunMsg/AI 立即反应, 不是 socket 可见 SM_STRUCK

3. RM_HEALTHSPELLCHANGED (HealthSpellChanged → UpdateMsg / SendRefMsg)
   → HP/MP 变化通知

4. RM_DEATH (Die → SendRefMsg)
   → 如果 HP=0, 广播死亡

5. RM_MAGIC_LVEXP (attack-triggered sword skills: 3000ms)
   → 技能训练通知 (客户端显示"技能熟练度提升")
```

### 14.2 延迟消息一览

| 消息 | 延迟 | 接收者 | 用途 |
|------|------|--------|------|
| RM_STRUCK (objbase:5261-5262) | 500ms | target | 二级 DirectAttack 命中+HP刷新 (刺杀/半月/十字) |
| RM_STRUCK internal (objbase:5264-5265) | 0ms | non-human DirectAttack target | 二级命中的怪物 RunMsg/AI 反应, 非 socket 可见包 |
| RM_STRUCK (objbase:5449) | 200ms | target | 主目标客户端可见受击动画+伤害 |
| RM_STRUCK internal (objbase:5542) | 0ms | non-human target | 怪物 RunMsg/AI 反应, 非 socket 可见包 |
| RM_MAGIC_LVEXP | 3000ms for attack-triggered sword skills; 1000ms for normal SpellNow training | attacker | 技能训练显示 |
| RM_BREAKWEAPON | 0ms (immediate) | attacker | 武器破损通知 |

---

## 15. 与 C++ 实现的对照点

| Delphi | C++ | 对齐状态 |
|--------|-----|---------|
| `HitXY` speedhack 900-HitSpeed*60 | `begin_attack_attempt()` latest_hit_time_ms | **待 golden trace 验证** |
| `CanNextHit` 1400-level*14-HitSpeed*60 | `can_next_hit()` game_state.hpp:503 | PR1 已冻结, PR3 保持数值测试 |
| `GetAttackPower` Luck gate | `roll_legacy_player_attack_power` | PR1 已冻结, PR4 golden formula |
| `GetHitStruckDamage` AC random | `legacy_physical_struck_damage` | PR1 已冻结, PR4 golden formula |
| `StruckDamage` 装备损耗 Random(10)+5 | `apply_legacy_struck_equipment_durability` | PR1 已冻结, PR4/PR12 验证 |
| `DamageHealth` 魔法盾 Round(damage*1.5) | `apply_damage` → `DamageResult` | PR1 已冻结, PR4/PR8 验证 |
| `Die` 掉落顺序 | `settle_player_death` | PR1 已冻结, PR9 golden trace |
| HitHit → HitMotion → SendRefMsg | `make_hit_packet` → `queue_packet` | PR1 已冻结, PR2 protocol trace |
| `DirectAttack` MISS 不广播 | `break` after `miss` trace | 已实现 |
| `SwordWideAttack` 3格扇形 (7,1,2) | `collect_wide_hit_targets` 3-cell fan | PR1 已冻结, PR5 golden trace |
| `SwordCrossAttack` PvP 80% 伤害 | `collect_cross_hit_targets` | PR1 已冻结, PR5 验证 PvP 衰减 |

---

## 16. PR1 收口结论

- **攻击间隔**: Delphi 服务端使用 `LatestHitTime` + `900 - HitSpeed*60` (`ObjBase.pas:9309-9316`); 客户端另有 `CanNextHit` (`ClMain.pas:3348-3362`)。PR3 必须保留双层门。
- **MISS 行为**: `DirectAttack` 命中失败时不进入 `StruckDamage`, 因而不广播 `SM_STRUCK`。`attack_miss.json` 冻结该序列。
- **玩家/怪物受击延迟**: 客户端可见 `SM_STRUCK` 对玩家和怪物主目标均延迟 200ms (`ObjBase.pas:5449`); 刺杀/半月/十字的二级 `DirectAttack` 可见受击延迟 500ms (`ObjBase.pas:5261-5262`)。怪物目标额外收到即时内部 `RM_STRUCK` (`ObjBase.pas:5264-5265`, `ObjBase.pas:5542`) 供 AI/RunMsg 反应。`struck_delay_player_vs_monster.json` 冻结该差异。
- **半月/十字**: 半月 3 个方向无伤害衰减 (`ObjBase.pas:5285-5304`); 十字斩对玩家使用 80% 伤害 (`ObjBase.pas:5320-5323`)。
- **装备特技/红毒/吸血**: 石化、红毒和吸血公式已在 `verification_checklist.md` P1 项冻结, 后续 PR 只实现/测试, 不重新解释 Delphi 语义。
- **死亡路径**: `Die` 的经验、PK、掉落和 `SM_DEATH` 顺序由 `death_player.json` 冻结, PR9 负责实现验证。
