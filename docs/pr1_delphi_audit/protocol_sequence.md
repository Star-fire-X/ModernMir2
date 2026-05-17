# Delphi 战斗协议消息顺序映射

> 基于 `ObjBase.pas` + `Magic.pas` + `Grobal2.pas` + `ClMain.pas` + `Actor.pas` 静态审查
> 所有 SM_*/CM_* 消息 ID 来自 `Grobal2.pas:811-1099`

---

## 1. 客户端→服务端 (CM_*)

| Delphi 常量 | 值 | 含义 | 字段 | C++ Legacy | C++ client_v1 |
|------------|----|------|------|-----------|---------------|
| CM_TURN | 3010 | 转向 | dir | - | ActionIntent(kind=turn) |
| CM_WALK | 3011 | 走路 | x, y, dir | - | ActionIntent(kind=walk) |
| CM_SITDOWN | 3012 | 坐下 | - | - | - |
| CM_RUN | 3013 | 跑步 | x, y, dir | - | ActionIntent(kind=run) |
| CM_HIT | 3014 | 普通攻击 | target_id, x, y, dir | kCmHit | ActionIntent(kind=attack, legacy_ident=kCmHit) |
| CM_HEAVYHIT | 3015 | 攻杀 | 同上 | kCmHeavyHit | ActionIntent(legacy_ident=kCmHeavyHit) |
| CM_BIGHIT | 3016 | 烈火攻击 | 同上 | kCmBigHit | ActionIntent(legacy_ident=kCmBigHit) |
| CM_SPELL | 3017 | 施法 | magic_id, target_id, x, y, dir | - | SpellIntent(magic_id) |
| CM_POWERHIT | 3018 | 气功 | (同) | kCmPowerHit | ActionIntent(legacy_ident=kCmPowerHit) |
| CM_LONGHIT | 3019 | 刺杀 | (同) | kCmLongHit | ActionIntent(legacy_ident=kCmLongHit) |
| CM_WIDEHIT | 3024 | 半月 | (同) | kCmWideHit | ActionIntent(legacy_ident=kCmWideHit) |
| CM_FIREHIT | 3025 | 烈火命中 | (同) | kCmFireHit | ActionIntent(legacy_ident=kCmFireHit) |
| CM_CROSSHIT | 3035 | 十字斩 | (同) | kCmCrossHit | ActionIntent(legacy_ident=kCmCrossHit) |
| CM_THROW | 3005 | 投掷 | (stub) | - | - |

**注意**: CM_* 与 SM_* 的对应关系: `SM = CM - 3000` (Actor.pas:1118)

---

## 2. 服务端→客户端 (SM_*) 动作消息

| Delphi 常量 | 值 | 含义 | 触发时机 | C++ client_v1 |
|------------|----|------|---------|---------------|
| SM_TURN | 10 | 转向 | TurnXY | ActorAction(kind=turn) |
| SM_WALK | 11 | 走路 | WalkXY | ActorAction(kind=walk) |
| SM_SITDOWN | 12 | 坐下 | SitdownXY | - |
| SM_RUN | 13 | 跑步 | RunXY | ActorAction(kind=run) |
| SM_HIT | 14 | 普攻动作 | HitHit → HitMotion | ActorAction(kind=hit, legacy_ident=kSmHit) |
| SM_HEAVYHIT | 15 | 攻杀动作 | HitHit | ActorAction(kind=hit, legacy_ident=kSmHeavyHit) |
| SM_BIGHIT | 16 | 烈火动作 | HitHit | ActorAction(kind=hit, legacy_ident=kSmBigHit) |
| SM_SPELL | 17 | 施法动作 | SpellNow | ActorAction(kind=spell) |
| SM_POWERHIT | 18 | 气功动作 | HitHit | ActorAction(legacy_ident=kSmPowerHit) |
| SM_LONGHIT | 19 | 刺杀动作 | HitHit | ActorAction(legacy_ident=kSmLongHit) |
| SM_DIGUP | 20 | 挖出 | DigUpMine | - |
| SM_DIGDOWN | 21 | 挖下 | DigDownMine | - |
| SM_FLYAXE | 22 | 飞斧 | 怪物投射 | - |
| SM_LIGHTING | 23 | 雷电 | 怪物技能 | - |
| SM_WIDEHIT | 24 | 半月动作 | HitHit | ActorAction(legacy_ident=kSmWideHit) |
| SM_FIREHIT | 8 | 烈火命中动作 | HitHit | ActorAction(legacy_ident=kSmFireHit) |
| SM_CROSSHIT | 35 | 十字动作 | HitHit | ActorAction(legacy_ident=kSmCrossHit) |
| SM_RUSH | 6 | 野蛮冲撞 | CharRushRush | ActorAction(kind=rush) |
| SM_RUSHKUNG | 7 | 野蛮失败 | CharRushRush(无推人) | ActorAction(kind=rush_kung) |
| SM_BACKSTEP | 9 | 后退(抗拒) | CharPushed | ActorAction(kind=backstep) |

---

## 3. 服务端→客户端 (SM_*) 状态/事件消息

| Delphi 常量 | 值 | 含义 | 触发时机 | C++ client_v1 |
|------------|----|------|---------|---------------|
| SM_STRUCK | 31 | 受击 | _Attack → SendDelayMsg(RM_STRUCK) | ActorVitals + ActorAction(kind=struck) |
| SM_DEATH | 32 | 死亡 | Die → SendRefMsg(RM_DEATH) | ActorDeath |
| SM_NOWDEATH | 34 | 立即死亡 | 即死效果 | ActorDeath |
| SM_SKELETON | 33 | 骨架 | 死亡后 | - |
| SM_ALIVE | 27 | 复活 | Revive | ActorAction(kind=stand) |
| SM_MOVEFAIL | 28 | 移动失败 | 阻挡 | - |
| SM_HIDE | 29 | 隐藏 | 隐身? | - |
| SM_DISAPPEAR | 30 | 消失 | 离线/切换 | - |
| SM_MAGICFIRE | 638 | 魔法特效 | SpellNow后处理 | ActorMagicFire |
| SM_MAGICFIRE_FAIL | 639 | 魔法失败 | 技能失败 | ActorMagicFireFail |
| SM_NORMALEFFECT | 716 | 普通特效 | ? | - |
| SM_SPACEMOVE_HIDE | 800 | 空间移动(隐) | SorcerySpaceMove | scroll_hide |
| SM_SPACEMOVE_SHOW | 801 | 空间移动(现) | SorcerySpaceMove | char_effect |
| SM_SPACEMOVE_HIDE2 | 806 | 空间移动2(隐) | MagLightingSpaceMove | scroll_hide |
| SM_SPACEMOVE_SHOW2 | 807 | 空间移动2(现) | MagLightingSpaceMove | char_effect |
| SM_HEALTHSPELLCHANGED | 53 | HP/MP变化 | HealthSpellChanged | ActorVitals |
| SM_OPENHEALTH | 1100 | 开启血量显示 | OpenHealth | ActorVitals |
| SM_BREAKWEAPON | 1102 | 武器破损 | CheckWeaponUpgradeResult | SysMessage |
| SM_ABILITY | 52 | 属性变化 | RecalcAbilitys | SelfAbility |
| SM_SUBABILITY | 752 | 详细属性 | RecalcAbilitys | SelfAbilityDetail |
| SM_DURACHANGE | 642 | 耐久变化 | StruckDamage | DurabilityChange |
| SM_FEATURECHANGED | 41 | 外观变化 | equip/death/revive | ActorUpsert |
| SM_CHARSTATUSCHANGED | 657 | 状态变化 | poison/transparent/等 | ActorUpsert |
| SM_CHANGELIGHT | 654 | 光照变化 | 隐身/蜡烛/等 | ActorUpsert |
| SM_WINEXP | 44 | 获得经验 | GainExp | SelfAbility |
| SM_LEVELUP | 45 | 升级 | GainExp→level up | SelfAbility |

---

## 4. 普通攻击消息顺序

### 4.1 Delphi 完整顺序

```
帧 N: [客户端] CM_HIT (3014) → SendMsg
帧 N: [服务端] Operate → HitXY → HitHit
         ├─ (1) CheckWeaponUpgradeResult
         │      → RM_BREAKWEAPON (1102) if weapon broken
         │      → RM_ABILITY (52) + RM_SUBABILITY (752)
         ├─ (2) _Attack(hitmode, target)
         │      ├─ DirectAttack→Random(SpeedPoint) < AccuracyPoint
         │      │   ├─ HIT:  StruckDamage → RM_DURACHANGE (642), DamageHealth→HealthSpellChanged
         │      │   │   → SendDelayMsg(RM_STRUCK, 200ms delay, to target)
         │      │   │   → SM_HEALTHSPELLCHANGED (53) to self+open_health
         │      │   └─ MISS: 无消息给目标
         │      ├─ SwordLongAttack (刺杀): DirectAttack at range 2
         │      ├─ SwordWideAttack (半月): DirectAttack×3 (正前+左前+右前)
         │      └─ SwordCrossAttack (十字): DirectAttack×7 (PvP80%)
         │      → 技能训练 RM_MAGIC_LVEXP (1000ms delay)
         ├─ (3) HitMotion(msg, dir, cx, cy)
         │      → SendRefMsg(RM_HIT, dir, cx, cy, 0, '')  // SM_HIT (14)
         └─ (4) 怪物目标: SendMsg(RM_STRUCK, immediate)

帧 N+ω: [客户端] 收到 SM_HIT → ProcMsg → ReadyAction(SM_HIT)
         → CalcActorFrame(HIT) → 播放 hit:6f×85ms=510ms 动画
         → SetSound → weapon sound at frame 2
帧 N+ω+(200ms): [客户端] 收到 SM_STRUCK → ReadyAction(SM_STRUCK)
         → CalcActorFrame(STRUCK) → 播放 struck:3f×70ms=210ms 动画
         → RunSound → strucksound
帧 N+ω: [客户端] 收到 SM_HEALTHSPELLCHANGED → HP/MP 刷新
帧 N+ω+(死亡时): [客户端] 收到 SM_DEATH → ReadyAction(SM_DEATH)
         → CalcActorFrame(DIE) → 播放 die:4f×120ms=480ms 动画
```

### 4.2 C++ client_v1 期望顺序

```
1. ActorAction(kind=hit, legacy_ident=kSmHit)     → 播放攻击动画
2. ActorVitals(hp, mp, damage, source) + ActorAction(kind=struck)  → 受击+HP刷新
3. ActorDeath (if died)                             → 死亡动画
4. (怪物) ActorVitals(exp) or SelfAbility           → 经验/升级
```

---

## 5. 技能施放消息顺序

### 5.1 火球术 (1) / 雷电术 (5)

```
[客户端] CM_SPELL (3017)
[服务端] DoSpell → SpellNow
  (0) GetSpellPoint → DamageSpell → HealthSpellChanged       // SM_HEALTHSPELLCHANGED (53)
  (1) SendRefMsg(RM_SPELL, effect, xx, yy, magic_id, '')      // SM_SPELL (17)
  (2) 校验: MagCanHitTarget, IsProperTarget, AntiMagic<=Random(10)
  (3) SendDelayMsg(RM_DELAYMAGIC, pwr, pos, 2, target, '', 600)
  (4) needfire:=TRUE → SendRefMsg(RM_MAGICFIRE, ...)          // SM_MAGICFIRE (638)
  (5) 训练: SendDelayMsg(RM_MAGIC_LVEXP, ..., 1000ms)
  (6) 600ms后: 服务端处理 RM_DELAYMAGIC → GetHitStruckDamage → 伤害
       → SendDelayMsg(RM_STRUCK, ...200ms)  // SM_STRUCK (31)

[客户端]
  收到 SM_HEALTHSPELLCHANGED (53) → MP刷新
  收到 SM_SPELL (17) → UseMagicSpell → ReadyAction(SM_SPELL) → 施法动画
  收到 SM_MAGICFIRE (638) → UseMagicFire → NewMagic → 飞射物特效
  收到 SM_STRUCK (31) + SM_HEALTHSPELLCHANGED (53) → 受击动画+HP刷新
```

### 5.2 治愈术 (2)

```
[服务端]
  (1) SendRefMsg(RM_SPELL, ...)                              // SM_SPELL
  (2) SendDelayMsg(user, RM_MAGHEALING, 0, pwr, 0, 0, '', 800)
  (3) 800ms后: HealthSpellChanged                             // HP刷新
```

### 5.3 施毒术 (6)

```
[服务端]
  (1) SpellNow入口已 SendRefMsg(RM_SPELL, ...)              // SM_SPELL
  (2) nofire:=TRUE → 有毒粉并完成分支后 nofire:=FALSE
  (3) 校验: IsProperTarget, CanUseBujuk, 抗毒判定
  (4) SendRefMsg(RM_MAKEPOISON, poison_kind, power, ...)    // 毒状态
  (5) 有有效毒粉后处理发送 RM_MAGICFIRE; 抗毒失败只是不发送 RM_MAKEPOISON
  (6) 缺道具/目标失败返回 RM_MAGICFIRE_FAIL
  (7) 每tick: HealthSpellChanged (HP衰减)
```

**注意**: 施毒术的 `nofire:=TRUE` 是失败路径的默认保护; 成功分支会重置为 FALSE 并发送 `SM_MAGICFIRE`。它不取消 `SpellNow` 入口的 `SM_SPELL` 施法动作。隐身术同理。

### 5.4 隐身术 (18)

```
[服务端]
  (1) SpellNow入口已 SendRefMsg(RM_SPELL, ...)              // SM_SPELL
  (2) nofire:=TRUE → shared bujuk 分支成功后 nofire:=FALSE
  (3) MagMakePrivateTransparent(user, time)
      → 清除9格内动物目标
      → 设置 STATE_TRANSPARENT
      → SendRefMsg(RM_TRANSPARENT, ...)                     // SM_CHARSTATUSCHANGED?
  (4) 成功后处理发送 RM_MAGICFIRE
  (5) 训练
```

### 5.5 召唤 (17/30)

```
[服务端]
  (1) SendRefMsg(RM_SPELL, ...) → SM_SPELL
  (2) MakeSlave(race, level, count, lifetime)
  (3) needfire:=TRUE → SendRefMsg(RM_MAGICFIRE, ...) → SM_MAGICFIRE
  (4) 新Monster出现在地图上 → SendRefMsg(RM_APPEAR, ...)
```

### 5.6 火墙 (22)

```
[服务端]
  (1) SendRefMsg(RM_SPELL, ...) → SM_SPELL
  (2) MagMakeFireCross → 创建5个TFireBurnEvent
  (3) needfire:=TRUE → SendRefMsg(RM_MAGICFIRE, ground_effect)
  (4) 每tick: fire events → ApplyDamage → HealthSpellChanged per target
```

### 5.7 野蛮冲撞 (27)

```
[服务端]
  (1) SpellXY → SWD_RUSHRUSH分支 → CharRushRush
  (2) MagPushArround: 逐格推人
      → CharPushed: SendRefMsg(SM_BACKSTEP) + 广播
  (3) 自身: SendRefMsg(SM_RUSH) 或 SM_RUSHKUNG
  (4) 目标: SendRefMsg(SM_STRUCK) → 受击
```

---

## 6. 死亡消息顺序

### 6.1 玩家死亡

```
[服务端] Die (ObjBase.pas:2583-2810)

阶段1 — 经验分配 (2611-2676):
  (1) GainExp → RM_WINEXP (44) + RM_LEVELUP (45)

阶段2 — PK惩罚 (2678-2732):
  (2) IncPkPoint → 系统提示
  (3) AddBodyLuck(-500)
  (4) Random(5)=0 → MakeWeaponUnlock → RM_BREAKWEAPON (1102)

阶段3 — 掉落 (2734-2766):
  (5) DropUseItems → RM_ITEMSHOW (物品出现在地上)
  (6) ScatterBagItems → 同上
  (7) ScatterGolds → 同上

阶段4 — 广播 (2806):
  (8) SendRefMsg(RM_DEATH, Dir, CX, CY, 1, '') → SM_DEATH (32)
```

### 6.2 怪物死亡

```
[服务端]
  (1) GainExp → RM_WINEXP (44) + RM_LEVELUP (45) (给击杀者)
  (2) DropUseItems + ScatterBagItems + ScatterGolds → RM_ITEMSHOW
  (3) SendRefMsg(RM_DEATH, Dir, CX, CY, 1, '') → SM_DEATH (32)
  (4) zen (重生) timer 开始
```

---

## 7. C++ 消息类型映射

| Delphi SM_* | C++ client_v1 消息类型 | message_id | 字段 |
|------------|----------------------|-----------|------|
| SM_HIT (14) | ActorAction | 307 | kind=hit, legacy_ident |
| SM_HEAVYHIT (15) | ActorAction | 307 | kind=hit, legacy_ident=kSmHeavyHit |
| SM_BIGHIT (16) | ActorAction | 307 | kind=hit, legacy_ident=kSmBigHit |
| SM_SPELL (17) | ActorAction(spell) + ActorMagicFire | 307+315 | magic_id, magic_effect |
| SM_STRUCK (31) | ActorVitals + ActorAction(struck) | 308+307 | damage, hp, mp, source_actor_id |
| SM_DEATH (32) | ActorDeath | 309 | actor_id, coords, dir |
| SM_NOWDEATH (34) | ActorDeath | 309 | (same) |
| SM_RUSH (6) | ActorAction(kind=rush) | 307 | legacy_ident=kSmRush |
| SM_RUSHKUNG (7) | ActorAction(kind=rush_kung) | 307 | legacy_ident=kSmRushKung |
| SM_BACKSTEP (9) | ActorAction(kind=backstep) | 307 | legacy_ident=kSmBackStep |
| SM_MAGICFIRE (638) | ActorMagicFire | 315 | effect_type, effect, legacy_ident |
| SM_MAGICFIRE_FAIL (639) | ActorMagicFireFail | 317 | actor_id, legacy_ident |
| SM_HEALTHSPELLCHANGED (53) | ActorVitals | 308 | hp, mp, max_hp, max_mp |
| SM_FEATURECHANGED (41) | ActorUpsert | ? | feature, state |
| SM_CHARSTATUSCHANGED (657) | ActorUpsert | ? | status flags |
| SM_ABILITY (52) | SelfAbility | ? | ability |
| SM_ALIVE (27) | ActorAction(kind=stand) | 307 | - |
| SM_OPENHEALTH (1100) | ActorVitals | 308 | (hp visible) |
| SM_BREAKWEAPON (1102) | SysMessage | ? | text |

---

## 8. 消息顺序关键规则

| 规则 | Delphi 行为 | C++ 期望 |
|------|-----------|---------|
| 攻击结算先于动作广播 | `_Attack` → `HitMotion`; `_Attack` 内可先排 `RM_STRUCK`/死亡消息 | PR2/PR9 用 trace 锁定 SM_HIT/SM_STRUCK/SM_DEATH 相对顺序 |
| 普攻MISS无受击包 | `DirectAttack` miss 后不发 `RM_STRUCK` | 不发送 ActorAction(struck) 或伤害 ActorVitals |
| 施法动作先于特效/失败 | `RM_SPELL` → 分支校验 → `RM_MAGICFIRE`/`RM_MAGICFIRE_FAIL` | ActorAction(spell) → ActorMagicFire/ActorMagicFireFail |
| `nofire`默认保护失败路径 | `RM_SPELL` 已在入口发送; 成功分支可重置 `nofire:=FALSE` 后发送 `RM_MAGICFIRE` | PR7/PR8 区分成功 magic-fire 与缺道具/失败 magic-fire-fail |
| 特效先于效果 | RM_MAGICFIRE → RM_DELAYMAGIC | ActorMagicFire → (delay) → ActorVitals |
| HP刷新在受击后 | StruckDamage → HealthSpellChanged | make_struck_packet → ActorVitals |
| 死亡最后 | 所有掉落/经验 → RM_DEATH | settle_death → ActorDeath |
| 经验在死亡前 | GainExp → Die | award_kill → ActorDeath |
| 半月无衰减 | `SwordWideAttack` 三格使用同一 damage | PR5 不按副目标衰减 |
| 十字PvP 80% | `SwordCrossAttack` 对玩家 `Round(damage*0.8)` | PR5 区分玩家/怪物目标 |
| 烈火倍率 | 基础伤害后 `Round(dam/100*(HitDouble*10))` | PR4/PR5 使用 Delphi rounding |
| 怪物RM_STRUCK immediate | SendMsg (无延迟) | PR2/PR9 trace 校验或记录兼容差异 |
| 玩家RM_STRUCK 200ms delay | SendDelayMsg(200ms) | PR2/PR9 trace 校验或记录兼容差异 |

**关键风险**: C++ frame-end dispatch 一次性发送所有 queued 消息, 而 Delphi 使用 `SendDelayMsg` 分时发送。这可能导致客户端在同一帧收到更多消息。

---

## 9. PR1 收口归属

| 项目 | PR1 结论 | 后续 PR |
|------|----------|---------|
| Delphi `SendDelayMsg(200ms)` vs C++ frame-end dispatch | 玩家 `RM_STRUCK` 延迟、怪物即时已经冻结为行为基线。 | PR2/PR9 |
| 施毒/隐身 `nofire:=TRUE` | 默认抑制失败路径后处理; 成功分支重置为 FALSE 并发送普通 `SM_MAGICFIRE`。 | PR7/PR8 |
| 死亡消息顺序 | 经验、PK惩罚、掉落先结算, 最后广播 `SM_DEATH`。 | PR9/PR12 |
| SM_RUSH vs SM_RUSHKUNG | 是否成功推人决定动作消息。 | PR5 |
| SM_DIGUP/SM_DIGDOWN 挖矿消息 | 属客户端动作/表现兼容项。 | PR10 |
