# Delphi 客户端动画与特效参数表

> 基于 `Actor.pas` (3,150行) + `magiceff.pas` (1,276行) + `PlayScn.pas` (1,684行) + `ClMain.pas` 静态审查

---

## 1. 人类动作帧表 (THumanAction)

**来源**: Actor.pas:74-90, HA 常量初始化

| 动作 | 枚举 | 帧数 | 帧间隔(ms) | 总时长(ms) | skip | 运动tick | 触发消息 |
|------|------|------|-----------|-----------|------|---------|---------|
| 站立 | ActStand | 4 | 200 | 800 | 4 | 0 | idle |
| 走路 | ActWalk | 6 | 90 | 540 | 2 | 2 | SM_WALK(11) |
| 跑步 | ActRun | 6 | 120 | 720 | 2 | 3 | SM_RUN(13) |
| 冲撞左 | ActRushLeft | 3 | 120 | 360 | 5 | 3 | SM_RUSH(6) |
| 冲撞右 | ActRushRight | 3 | 120 | 360 | 5 | 3 | SM_RUSH(6) |
| 战斗姿态 | ActWarMode | 1 | 200 | 200 | 0 | 0 | (自动,上次攻击后4秒) |
| 普通攻击 | ActHit | 6 | 85 | 510 | 2 | 0 | SM_HIT(14) |
| 攻杀 | ActHeavyHit | 6 | 90 | 540 | 2 | 0 | SM_HEAVYHIT(15) |
| 烈火 | ActBigHit | 8 | 70 | 560 | 0 | 0 | SM_BIGHIT(16) |
| 烈火蓄力 | ActFireHitReady | 6 | 70 | 420 | 4 | 0 | (烈火 prepare) |
| 施法 | ActSpell | 6 | 60 | 360 | 2 | 0 | SM_SPELL(17) |
| 坐下 | ActSitdown | 2 | 300 | 600 | 0 | 0 | SM_SITDOWN(12) |
| 受击 | ActStruck | 3 | 70 | 210 | 5 | 0 | SM_STRUCK(31) |
| 死亡 | ActDie | 4 | 120 | 480 | 4 | 0 | SM_DEATH(32)/SM_NOWDEATH(34) |

**帧索引公式**: `frame_index = start + dir * (frame + skip) + local_frame`

**战斗姿态 (WarMode)**:
- 攻击/施法后自动进入, 持续 4 秒
- 显示武器就绪的姿态 (不使用 ActStand)

**C++ 对应**: `legacy_animation.cpp:59-74` `kHumanActions[]`

---

## 2. 攻击动作↔SM 消息↔HitEffect 映射

**来源**: Actor.pas:2646-2682 (THumActor.CalcActorFrame)

| SM 消息 | 使用动作 | HitEffectNumber | HitEffectBase |
|---------|---------|----------------|---------------|
| SM_HIT (14) | ActHit | 0 | 0 (800) |
| SM_POWERHIT (18) | ActHit | 1 | 1 (1410) |
| SM_LONGHIT (19) | ActHit | 2 | 2 (1700) |
| SM_WIDEHIT (24) | ActHit | 3 | 3 (3480) |
| SM_FIREHIT (8) | ActHit | 4 | 4 (3390) |
| SM_CROSSHIT (35) | ActHit | 6 | 6 (?) |
| SM_HEAVYHIT (15) | ActHeavyHit | 0 | 0 (800) |
| SM_BIGHIT (16) | ActBigHit | 0 | 0 (800) |
| SM_SPELL (17) | ActSpell | (specialeffect) | - |

**SpellFrame 特殊值** (Actor.pas:2703-2739):
- EffectNumber=22 → SpellFrame=10
- EffectNumber=26 → SpellFrame=20
- EffectNumber=35 → SpellFrame=15
- EffectNumber=43 → SpellFrame=20
- 默认: `DEFSPELLFRAME=10`

---

## 3. 受击帧时间

**来源**: Actor.pas:1149-1159 (ReadyAction SM_STRUCK handler)

```pascal
struckframetime := 200 - Abil.Level;
if struckframetime < 80 then struckframetime := 80;
```

| Level | struckframetime (ms) |
|-------|---------------------|
| 1 | 199 |
| 10 | 190 |
| 50 | 150 |
| 100 | 100 |
| 120 | 80 (最小) |

**C++ 对应**: `ActorAction(kind=struck)` → `action_duration_ms` should use this formula

---

## 4. 怪物动作表 (MA9-MA52)

**来源**: Actor.pas:92-345

每个 MA* 是 TMonsterAction record = (ActStand, ActWalk, ActAttack, ActCritical, ActStruck, ActDie, ActDeath)

| MA 常量 | 帧配置 (推测典型值) | 使用种族 (RaceByPM) |
|---------|-------------------|-------------------|
| MA9 | stand:4f, walk:6f, attack:6f, struck:2f, die:4f | Race 9 (鹿/鸡) |
| MA10 | - | Race 10 (羊) |
| MA11 | - | Race 11 (狼) |
| MA12 | - | Race 12, 24 |
| MA13 | - | Race 13 |
| MA14 | - | Race 14, 17, 18, 23 (多种怪物) |
| MA15 | - | Race 15, 22 |
| MA16 | - | Race 16 |
| MA17 | - | Race 30, 31 (骷髅) |
| MA19 | - | Race 19, 20, 21, 37, 40, 45, 52, 53 (最多) |
| MA20 | - | Race 41, 42 |
| MA21 | - | Race 43 |
| MA22 | - | Race 47 |
| MA23 | - | Race 48, 49 |
| MA24 | - | Race 32 (沃玛教主) |
| MA25 | - | Race 33 |
| MA26 | - | Race 99 |
| MA27 | - | Race 98 |
| MA28 | - | Race 54 |
| MA29 | - | Race 55 |
| MA30 | - | Race 34 |
| MA31 | - | Race 35 |
| MA32 | - | Race 36 |
| MA33 | - | Race 60, 61, 62, 70, 71, 72 |
| MA34 | - | Race 63 |
| MA50-52 | - | NPC (Appearance 23/24/25) |

**C++ 对应**: `legacy_animation.cpp:107-357` 怪物 action tables

---

## 5. 绘制层级

**来源**: PlayScn.pas + magiceff.pas 特效创建顺序

```
层级 (从下到上):
  1. 地图底层 (tiles)
  2. 地面特效 (火墙/神圣战甲术/幽灵盾/困魔咒)
     → TMapEffect, mtGroundEffect, TBujaukGroundEffect(settled)
  3. 物品 (地面物品)
  4. 怪物/玩家 (按 Y 坐标排序)
  5. 角色附加特效 (魔法盾/隐身/毒)
     → TCharEffect, char_attached
  6. 飞行弹道 (火球/雷电/火符/飞斧)
     → TMagicEff (mtFly), TFlyingAxe, TFlyingArrow, TFireGunEffect, TLightingThunder, TExploBujaukEffect
  7. 爆炸特效 (弹道到达后)
     → TMagicEff (explosion), TThuderEffect
  8. UI overlay (血条/名字/公会名)
```

**C++ 对应**: `LegacyEffectManager` — ground_effects_ → char_effects_ → fly_effects_ → overlay_effects_

---

## 6. 16方向弹道系统

**来源**: magiceff.pas:420-589 (TMagicEff.Shift)

| 参数 | Delphi | C++ |
|------|--------|-----|
| 方向数 | 16 (0-15) | 16 (LegacyAnimation) |
| 起始点计算 | `sub_StartPoint(ax, ay, dir)` | start point calc |
| 终点计算 | `sub_EndPoint(ax, ay, dir)` | end point calc |
| 飞行时间 (有目标) | 700ms 基准 | 900ms |
| 飞行时间 (无目标) | 900ms 基准 | 900ms |
| 帧间隔 | 50ms (NextFrameTime) | 30-50ms |
| 动画帧数 | 6 (mtFly) | 6 |
| 碰撞检测距离 | ≤15px (X和Y方向) | ≤15px |
| 超时 | 10秒 | 10秒 |
| 飞行图像索引 | `FLYBASE(10) + Dir16*10 + curframe` | 相同公式 |
| 爆炸图像索引 | `MagExplosionBase + curframe` | 相同公式 |
| 目标跟踪 | 每帧重算 `firedis`, `/10` 平滑 | - |

**飞行速度公式** (有目标):
```
newfiredisX = (target.X - fireX) / max(tax, tay) * 700 / 10  // 跟踪
firedisX += (newfiredisX - firedisX) / 10                       // 平滑
// 每帧位移: firedis/700 * ms
```

**飞行速度公式** (无目标):
```
// 每帧位移: firedis/900 * ms
```

---

## 7. 特效帧配置详情

**来源**: magiceff.pas:321-443 (TMagicEff.Create)

| mtype | start | frame | curframe | FixedEffect | Repetition | ExplosionFrame | NextFrameTime | ImgLib |
|-------|-------|-------|----------|-------------|------------|----------------|---------------|--------|
| mtReady | - | - | - | - | - | - | - | WMagic |
| mtFly | 0 | 6 | start | FALSE | Recusion | 10 | 50 | WMagic |
| mtExplosion | 0 | -1 | start | TRUE | FALSE | 10 | 50 | WMagic |
| mtFireBall | 0 | 6 | start | FALSE | Recusion | 1 | 50 | WMagic |
| mtGroundEffect | 0 | 20 | start | TRUE | FALSE | 20 | 50 | WMon21Img |
| mtFlyAxe | 0 | 3 | start | FALSE | Recusion | 3 | 50 | WMagic |
| mtFlyArrow | 0 | 1 | start | FALSE | Recusion | 1 | 50 | WMagic |
| mtThunder | 0 | -1 | start | TRUE | FALSE | 6 | 50 | WMagic2 |
| mtFireThunder | 0 | -1 | start | TRUE | FALSE | 10 | 50 | WMagic2 |
| mtLightingThunder | 0 | 10 | start | TRUE | FALSE | - | 50 | WMagic |
| mtExploBujauk | 0 | 3 | start | FALSE | Recusion | - | 50 | WMagic |
| mtBujaukGroundEffect | 0 | 3 | start | FALSE | Recusion | - | 50 | WMagic |
| mtFireWind | - | 6* | - | FALSE | - | - | 50 | WMagic |

*mtFireWind 使用 FIREGUNFRAME=6 个粒子节点

**C++ 对应**: `legacy_animation.cpp` LegacyEffectManager 特效创建

---

## 8. 召唤特效参数 (PlayScn.pas:892-1049)

| Magic ID | 名称 | ExplosionBase | NextFrameTime | ExplosionFrame | Light | 特殊说明 |
|----------|------|---------------|---------------|----------------|-------|---------|
| 18 | Soul Sword | 1570 | 80ms | - | - | TMagicEff(1,1,...) |
| 21 | Iceberg | 1660 | 80ms | 20 | 3 | - |
| 26 | Tamer Whip | 3990 | 80ms | 10 | 2 | - |
| 27 | Phoenix | 1800 | 80ms | 10 | 3 | - |
| 30 | Holy Water | 3930 | 80ms | 16 | 3 | - |
| 31 | Storm | 3850 | 80ms | 20 | 3 | - |
| 10 (ExploBujauk) | - | MagExplosionBase=1360 | 50ms | - | - | - |
| 17 (ExploBujauk) | - | MagExplosionBase=1540 | 50ms | - | - | - |
| 11 (BujaukGround) | - | - | 50ms | 16 | - | Fire |
| 12 (BujaukGround) | - | - | 50ms | 16 | - | - |
| mtGround (char) | - | MagExplosionBase=3580 | 50ms | 20 | 3 | TBujaukGroundEffect |
| mtGround (map) | - | MagExplosionBase=3580 | 50ms | 20 | 3 | TMapEffect |

**电系技能特殊处理**:
- `mtThunder` → `TThuderEffect(10, ...)`, WMagic2, ExplosionFrame=6
- `mtFireThunder` → `TThuderEffect(140, ...)`, WMagic2, ExplosionFrame=10
- `mtLightingThunder` → `TLightingThunder(970, ...)`, lightning bolt image

---

## 9. 声音触发表

### 9.1 动作开始声音 (RunSound, Actor.pas:1937-1965)

| 消息 | 声音 |
|------|------|
| SM_STRUCK | struckweaponsound + strucksound + screamsound |
| SM_NOWDEATH | diesound |
| SM_HIT / SM_THROW / SM_FLYAXE / SM_LIGHTING / SM_DIGDOWN | attacksound |
| SM_ALIVE / SM_DIGUP | appearsound |
| SM_SPELL | magicstartsound |

### 9.2 帧触发声音 (RunActSound, Actor.pas:1967-2041)

| 消息 | 帧 | 声音 |
|------|----|------|
| SM_HIT + SM_THROW | frame 2 | weaponsound |
| SM_POWERHIT | frame 2 | weaponsound + s_yedo |
| SM_LONGHIT | frame 2 | weaponsound + s_longhit |
| SM_WIDEHIT | frame 2 | weaponsound + s_widehit |
| SM_FIREHIT | frame 2 | weaponsound + s_firehit |
| SM_CROSSHIT | frame 2 | weaponsound |

### 9.3 魔法声音 (SetSound, Actor.pas:1879-1883)

| 阶段 | 声音编号公式 |
|------|------------|
| 开始 | `10000 + CurMagic.MagicSerial * 10 + 0` |
| 飞行 | `10000 + CurMagic.MagicSerial * 10 + 1` |
| 爆炸 | `10000 + CurMagic.MagicSerial * 10 + 2` |

### 9.4 怪物声音 (Actor.pas:1906-1912)

```
序号 = 200 + Appearance * 10 + [0..6]
  0: attacksound
  1: strucksound
  2: diesound
  3: appearsound
  4: stepsound
  5: magicsound
  6: specialsound
```

---

## 10. 动画时钟

| 时钟 | Delphi | C++ LegacyAnimationClock | 对齐 |
|------|--------|--------------------------|------|
| 移动 tick | 帧驱动 (~100ms) | 100ms (move_tick) | 是 |
| 动画 tick | 帧驱动 (~50ms) | 50ms (ani_tick) | 是 |
| 主循环 | 帧驱动 (不定间隔) | 20ms (可配置) | 不同但独立 |
| 动作时长 | 帧数×帧间隔 | action_duration_ms 查表 | 是 |

---

## 11. 死亡动画特殊处理

**来源**: Actor.pas:1035-1056

```pascal
// SM_DEATH: Die animation (last frame → first frame 反向?)
SM_DEATH:
  startframe, endframe, frametime from ActDie
  startframe := endframe    // 从最后一帧开始
  // 播放后停留在死亡帧

// SM_NOWDEATH: 立即死亡 (无动画过渡)
SM_NOWDEATH:
  startframe, endframe from ActDie
  // 立即跳到死亡帧

// SM_SKELETON: 尸体→骨架
SM_SKELETON:
  Death := FALSE
  Skeleton := TRUE
  // 骨架站立动画
```

---

## 12. C++ 对齐要点

| Delphi 特性 | C++ 实现位置 | 状态 |
|------------|------------|------|
| kHumanActions[] 14种动作 | legacy_animation.cpp:59-74 | **已实现** |
| 怪物 MA* action tables | legacy_animation.cpp:107-357 | **已实现** |
| 16方向弹道系统 | LegacyEffectManager fly_effects_ | **已实现** |
| EffectBase[0..35] | PR10 animation trace | **PR10 延期** |
| HitEffectBase[0..5] | PR10 hit-effect trace | **PR10 延期** |
| SpellFrame 特殊值 | legacy_animation.cpp | **PR10 延期** |
| 受击帧时间 Level 公式 | game_state.hpp action_duration_ms | **PR10 延期** |
| 声音触发 (帧2) | PR10 audio golden | **PR10 延期** |
| 魔法声音公式 10000+Serial*10 | PR10 audio golden | **PR10 延期** |
| 绘制层级 | LegacyEffectManager 4层 | **已实现** |
| 战斗姿态 4秒 | war_mode timer | **已实现** |
| 死亡动画反向播放 | PR10 death animation trace | **PR10 延期** |
| SM_NOWDEATH 立即跳帧 | PR10 death animation trace | **PR10 延期** |
