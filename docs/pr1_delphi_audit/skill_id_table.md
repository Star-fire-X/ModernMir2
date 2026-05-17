# Delphi 技能 ID 完整参数表

> 基于 `Magic.pas` SpellNow (441-999) + `Grobal2.pas` TDefMagic (483-500) + `magiceff.pas` EffectBase (31-68) + `Actor.pas` HitEffectBase (70-77)

---

## 剑术技能 (IsSwordSkill = TRUE)

| ID | 名称 | 职业 | MP公式 | 冷却 | 目标类型 | 距离 | 范围 | 触发协议 | 动作标识 |
|----|------|------|--------|------|---------|------|------|---------|---------|
| 3 | 基本剑术 | 战士 | - | 攻速间隔 | 被动 | - | - | CM_HIT | HM_HIT |
| 4 | 攻杀剑术 | 战士 | - | 攻速间隔 | 单体 | 1 | 1 | CM_HEAVYHIT | HM_HEAVYHIT |
| 7 | 剑术 | 战士 | - | 攻速间隔 | 单体 | 1 | 1 | CM_HIT (通过普攻触发) | HM_HIT |
| 12 | 刺杀剑术 | 战士 | - | 攻速间隔 | 隔位单体 | 2 | 1 | CM_SPELL toggle→CM_LONGHIT | HM_LONGHIT |
| 25 | 半月弯刀 | 战士 | - | 攻速间隔 | 前方扇形 | 1 | 3格 | CM_SPELL toggle→CM_WIDEHIT | HM_WIDEHIT |
| 26 | 烈火剑法 | 战士 | Round(Spell/(MaxTrain+1)*(Lv+1))+DefSpell | 10s | 单体 | 1 | 1 | CM_SPELL→CM_FIREHIT | HM_FIREHIT |
| 27 | 野蛮冲撞 | 战士 | 同上 | 3s | 方向 | N | 路径 | CM_SPELL | SM_RUSH |
| 34 | 十字斩 | 战士 | 同上 | 攻速间隔 | 周围 | 1 | 7方向 | CM_SPELL→CM_CROSSHIT | HM_CROSSHIT |

**剑术技能特殊规则**:
- 触发: 客户端选择技能→发送CM_SPELL→服务端设置标志(BoAllowFireHit等)→下一次普通攻击自动使用
- 烈火: 蓄力后10秒内有效, 超时自动取消
- 野蛮: 立即执行, 不等待普通攻击
- 十字: toggle开关, 开启后所有普攻变为十字斩

---

## 法师技能

| ID | 名称 | 职业 | MP公式 | 冷却 (DelayTime) | 目标类型 | 距离 | 范围 | 道具需求 | 抗性检查 | EffectBase | EffectType (推测) |
|----|------|------|--------|----------|---------|------|------|---------|---------|-----------|------------------|
| 1 | 火球术 | 法师 | Spell/(MaxTrain+1)*(Lv+1)+DefSpell | 在DelayTime字段 | 单体 | 8 | 1 | 无 | AntiMagic | 0 | mtFly→mtExplosion |
| 5 | 雷电术 | 法师 | 同上 | 同上 | 单体 | 8 | 1 | 无 | AntiMagic | ? | mtLightingThunder |
| 8 | 抗拒火环 | 法师 | 同上 | 同上 | 自身周围 | 0 | 周围1格 | 无 | 等级差 | 1 (200) | - |
| 9 | 地狱火 | 法师 | 同上 | 同上 | 直线 | 5 | 直线 | 无 | 无 | 5 (900) | mtFireWind |
| 10 | 疾光电影 | 法师 | 同上 | 同上 | 直线 | 8 | 直线 | 无 | 无 | ? | mtFireWind |
| 20 | 诱惑之光 | 法师 | 同上 | 同上 | 单体 | 8 | 1 | 无 | 等级 | 25 (3960) | ? |
| 21 | 瞬息移动 | 法师 | 同上 | 同上 | 自身 | 0 | 1 | 无 | 无 | 14 (1500) | - |
| 22 | 火墙 | 法师 | 同上 | 同上 | 地面 | 8 | 十字5格 | 无 | 无 | 16 (940) | mtGroundEffect |
| 23 | 爆裂火焰 | 法师 | 同上 | 同上 | 地面 | 8 | 3×3 | 无 | 无 | 14 (1500) | mtExplosion |
| 24 | 冰咆哮 | 法师 | 同上 | 同上 | 地面(自身) | 0 | 5×5 | 无 | 无 | ? | mtExplosion |
| 31 | 魔法盾 | 法师 | 同上 | 同上 | 自身 | 0 | 1 | 无 | 无 | 31 (0) | char_effect |
| 33 | 冰咆哮变体 | 法师 | 同上 | 同上 | 地面 | 8 | 3×3 | 无 | 无 | 33 (130) | mtExplosion (MagBigExplosion, radius=1) |

**法师技能通用规则**:
- 伤害基准: `GetPower(MPow(pum)) + LoByte(WAbil.MC)` + Random范围
- 技能等级缩放: GetPower (线性, 25%-100%)

---

## 道士技能

| ID | 名称 | 职业 | MP公式 | 冷却 | 目标类型 | 距离 | 范围 | 道具需求 | 抗性检查 | EffectBase | EffectType |
|----|------|------|--------|------|---------|------|------|---------|---------|-----------|------------|
| 2 | 治愈术 | 道士 | Spell/(MaxTrain+1)*(Lv+1)+DefSpell | DelayTime | 单体(友) | 8 | 1 | 无 | 无 | 2 (400) | char_effect |
| 6 | 施毒术 | 道士 | 同上 | 同上 | 单体(敌) | 8 | 1 | 毒粉(StdMode=25,Shape≤2) | AntiPoison | 23 (0) | mtFly→mtExplosion |
| 11 | 灵魂火符 | 道士 | 同上+符消耗 | 同上 | 单体(敌) | 8 | 1 | 符? | AntiMagic | ? | mtFly→mtExplosion |
| 13 | 灵魂火球 | 道士 | 同上 | 同上 | 单体(敌) | 8 | 1 | 护身符×1 | AntiMagic | ? | mtFly→mtExplosion |
| 14 | 幽灵盾 | 道士 | 同上 | 同上 | 地面(友范围) | 8 | 半径3(7×7) | 护身符×1 | 无 | 8 (20) | mtGroundEffect |
| 15 | 神圣战甲术 | 道士 | 同上 | 同上 | 地面(友范围) | 8 | 半径3(7×7) | 护身符×1 | 无 | ? | mtGroundEffect |
| 16 | 困魔咒 | 道士 | 同上 | 同上 | 地面 | 8 | 十字8格 | 护身符×1 | 无 | 17 (1560) | mtKyulKai |
| 17 | 召唤骷髅 | 道士 | 同上 | 同上 | 地面 | 5 | 1 | 护身符×1 | 无 | 14 (1500) | mtGroundEffect |
| 18 | 隐身术 | 道士 | 同上 | 同上 | 自身 | 0 | 1 | 护身符×1 | 无 | 19 (1620) | char_effect |
| 19 | 集体隐身术 | 道士 | 同上 | 同上 | 地面(友范围) | 8 | 3×3 | 护身符×1 | 无 | 19 (1620) | char_effect |
| 28 | 心灵启示 | 道士 | 同上 | 同上 | 单体(敌) | 8 | 1 | 无 | 成功率Random(6)≤3+Lv | ? | - |
| 29 | 群体治愈术 | 道士 | 同上 | 同上 | 地面(友范围) | 8 | 3×3 | 无 | 无 | ? | char_effect |
| 30 | 召唤神兽 | 道士 | 同上 | 同上 | 地面 | 5 | 1 | 护身符×5 | 无 | 14 (1500) | mtGroundEffect |
| 32 | 圣言术 | 道士 | 同上 | 同上 | 单体(不死) | 8 | 1 | 无 | 成功率 | ? | - |
| 35 | 神圣魔法 | 道士 | 同上 | 同上 | 单体(敌) | 8 | 1 | 护身符×1 | AntiMagic | ? | mtFly→mtExplosion (shared branch with ID 11, 对非不死非人类1.2x) |
| 36 | DC BUFF | 道士 | 同上 | 同上 | 自身+召唤物 | 0 | 1 | 护身符×1 | 无 | ? | MagDcUp |
| 37 | 抗拒火环变体 | 法师/道士 | 同上 | 同上 | 自身周围 | 0 | 周围1格 | 无 | 等级差 | ? | MagPushArround (shared branch with ID 8) |

**道士技能通用规则**:
- 伤害/治疗基准: `GetPower13(MPow(pum)) + 5*LoByte(WAbil.SC)` 或 `2*LoByte(WAbil.SC)`
- 技能等级缩放: GetPower13 (有50%基底, 50%-100%)
- 护身符: U_BUJUK (首选) 或 U_ARMRINGL (备选), StdMode=25
- 毒粉: Shape≤2 (1=灰/绿毒, 2=黄/红毒), Dura≥100
- nofire=true 是失败路径默认保护; 成功施毒/隐身分支会重置为 FALSE 并发送普通 SM_MAGICFIRE, 缺道具或目标失败才走 SM_MAGICFIRE_FAIL

---

## TDefMagic 记录字段 (Grobal2.pas:483-500)

```pascal
TDefMagic = record
  MagicId: Integer;          // 技能ID (1-37)
  MagicName: string;         // 技能名称 (韩文+中文)
  EffectType: Byte;          // TMagicType 枚举值 (0-14)
  Effect: Byte;              // 特效编号 (用于 EffectBase 索引)
  Spell: Integer;            // MP消耗基准
  MinPower: Integer;         // 最小威力
  NeedLevel: array[0..3] of Integer;  // 每级需要的玩家等级
  MaxTrain: array[0..3] of Integer;   // 每级最大熟练度
  MaxTrainLevel: Integer;    // 最大训练等级 (=3)
  Job: Integer;              // 职业 (0=战士, 1=法师, 2=道士)
  DelayTime: Integer;        // 施法延迟/冷却 (ms?)
  DefSpell: Integer;         // 固定MP消耗 (加到公式)
  DefMinPower: Integer;      // 固定最小威力
  MaxPower: Integer;         // 最大威力
  DefMaxPower: Integer;      // 固定最大威力
  Desc: string;              // 描述文本
end;
```

---

## EffectBase 完整表 (magiceff.pas:31-68)

| Magic ID | EffectBase Value | WIL库 | 说明 |
|----------|-----------------|-------|------|
| 0 | 0 | WMagic | Fireball |
| 1 | 200 | WMagic | Repulsive |
| 2 | 400 | WMagic | Ice Storm |
| 3 | 600 | WMagic | Heaven's Punishment |
| 4 | 0 | WMagic | Arrow |
| 5 | 900 | WMagic | Fire Wind |
| 6 | 920 | WMagic | Fire Gun |
| 7 | 940 | WMagic | Thunder Bolt |
| 8 | 20 | WMagic2 | Magic Sphere |
| 9 | 940 | WMagic | Hellfire |
| 10 | 940 | WMagic | Soul Fire Shield |
| 11 | 940 | WMagic | Soul Shield |
| 12 | 0 | WMagic | Mass Spel |
| 13 | 1380 | WMagic | Plague |
| 14 | 1500 | WMagic | Summon/Teleport |
| 15 | 1520 | WMagic | Vampirism |
| 16 | 940 | WMagic | Fire Wall |
| 17 | 1560 | WMagic | Ice Thrust |
| 18 | 1590 | WMagic | Flash |
| 19 | 1620 | WMagic | Scorpion |
| 20 | 1650 | WMagic | Fire Blast |
| 21 | 1680 | WMagic | Iceberg |
| 22 | 0 | WMagic | Mirror Image |
| 23 | 0 | WMagic | Poison |
| 24 | 0 | WMagic | Soul Shield |
| 25 | 3960 | WMagic | Tamer's Whip |
| 26 | 1790 | WMagic | Phoenix |
| 27 | 0 | WMagic2 | Reflection |
| 28 | 3880 | WMagic | Curse |
| 29 | 3920 | WMagic | Holy Water |
| 30 | 3840 | WMagic | Storm |
| 31 | 0 | WMon21Img | Tornado |
| 32 | 40 | WMagic2 | CrossHit |
| 33 | 130 | WMagic2 | Heaven Fist |
| 34 | 160 | WMagic2 | Fire Dragon |
| 35 | 190 | WMagic2 | Fire Storm |

**WIL库选择规则** (magiceff.pas:274-319):
- `WMagic2`: magic_indices 33, 34, 35, 8, 27
- `WMon21Img`: magic_index 31
- `WMagic`: 其余所有

---

## HitEffectBase 表 (magiceff.pas:70-77)

| Index | Value | 对应攻击类型 | 说明 |
|-------|-------|------------|------|
| 0 | 800 | - | Mass Spel |
| 1 | 1410 | SM_POWERHIT | Power hit effect |
| 2 | 1700 | SM_LONGHIT | Long hit effect |
| 3 | 3480 | SM_WIDEHIT | Wide hit effect |
| 4 | 3390 | SM_FIREHIT | Fire hit effect |
| 5 | 40 | - | CrossHit |
| 6 | - | SM_CROSSHIT | Cross hit effect |

---

## TMagicType → 特效类映射 (magiceff.pas:84-88, PlayScn.pas:892-1049)

| TMagicType | 枚举值 | Delphi特效类 | C++对应 | 帧配置 |
|-----------|--------|------------|---------|--------|
| mtReady | 0 | TMagicEff | - | - |
| mtFly | 1 | TMagicEff + TMagicEff | fly_effects_ | start=0, frame=6, NextFrameTime=50ms |
| mtExplosion | 2 | TMagicEff | fly_effects_ | frame=-1, FixedEffect=TRUE, ExplosionFrame=10 |
| mtFlyAxe | 3 | TFlyingAxe | fly_effects_ | start=0, frame=3, ReadyFrame=65 |
| mtFireWind | 4 | TFireGunEffect | fly_effects_ | FIREGUNFRAME=6, NextFrameTime=50ms |
| mtFireGun | 5 | TFireGunEffect | fly_effects_ | 同 FireWind |
| mtLightingThunder | 6 | TLightingThunder | fly_effects_ | Lightning bolt image |
| mtThunder | 7 | TThuderEffect | fly_effects_ | WMagic2, ExplosionFrame=6 |
| mtExploBujauk | 8 | TExploBujaukEffect | fly_effects_ | 飞行3帧+爆炸 |
| mtBujaukGroundEffect | 9 | TBujaukGroundEffect | ground_effects_ | 飞行+地面持续 |
| mtKyulKai | 10 | (see NewMagic) | ground_effects_ | 结界效果 |
| mtFlyArrow | 11 | TFlyingArrow | fly_effects_ | ReadyFrame=40, Y-offset=-46 |
| mtFireBall | 12 | TFlyingFireBall | fly_effects_ | 8方向, ReadyFrame=65 |
| mtGroundEffect | 13 | TMagicEff(ground) | ground_effects_ | WMon21Img, ExplosionFrame=20 |
| mtFireThunder | 14 | TThuderEffect(fire) | fly_effects_ | WMagic2, ExplosionFrame=10 |

---

## PlayScn.pas NewMagic 特殊处理 (行892-1049)

| Magic ID | 特效类型 | ExplosionBase | NextFrameTime | ExplosionFrame | Light |
|----------|---------|---------------|---------------|----------------|-------|
| 18 | Soul Sword | 1570 | 80 | - | - |
| 21 | Iceberg | 1660 | 80 | 20 | 3 |
| 26 | Tamer Whip | 3990 | 80 | 10 | 2 |
| 27 | Phoenix | 1800 | 80 | 10 | 3 |
| 30 | Holy Water | 3930 | 80 | 16 | 3 |
| 31 | Storm | 3850 | 80 | 20 | 3 |
| 10 | ExploBujauk | MagExplosionBase=1360 | 50 | - | - |
| 17 | ExploBujauk | MagExplosionBase=1540 | 50 | - | - |
| 11/12 | BujaukGround | ExplosionFrame=16 | 50 | 16 | - |
| (mtGround) | Ground | MagExplosionBase=3580 | 50 | 20 | 3 |
