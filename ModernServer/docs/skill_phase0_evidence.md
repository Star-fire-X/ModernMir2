# 迁移阶段0：技能系统证据冻结

生成日期：2026-04-30  
阶段目标：冻结 Delphi 原始行为证据与当前 C++ 差异，不实现技能逻辑，不修改生产行为。

## 结论

阶段0证据已建立，可以作为阶段1技能迁移的输入。`Release\Mir200\Data.mdb` 的 `Magic` 表已通过 32 位 Jet OLEDB 成功抽取，不能再使用 `ModernServer\config\magic\imported_magic.toml` 或 `default_magic.toml` 作为技能定义权威。

阶段1默认建议使用：

- 代码证据：`Source\M2Server`
- 数据证据：`Release\Mir200\Data.mdb`
- 客户端表现证据：`Source\Client\ClMain.pas`、`Source\Client\magiceff.pas`

但 `Source\M2Server` 与 `Source\Mir200` 的 `Magic.pas`、`ObjBase.pas`、`LocalDB.pas` 哈希不同，阶段1实现前必须保留双源差异记录。当前 diff 观察显示 `Magic.pas` 主要是注释和编码文字差异，`LocalDB.pas` 的差异不在 `LoadMagic`，`ObjBase.pas` 存在 `AM_FIREBALL = 1` 与 `AM_FIREBALL = 8` 的常量差异，可能影响怪物或特殊攻击分支，列为待确认。

## 阶段0产物

| 产物 | 用途 |
| --- | --- |
| `ModernServer\docs\skill_phase0_evidence.md` | 本证据文档，记录输入源、双源差异、Delphi 行为、C++ 风险和阶段1门禁 |
| `ModernServer\tests\golden\skill_phase0\magic_db.json` | 从 `Release\Mir200\Data.mdb` 的 `Magic` 表抽取的完整技能定义金样本 |
| `ModernServer\tests\golden\skill_phase0\formula_cases.json` | Delphi 公式输入输出样本，含 `Round`、`GetSpellPoint`、`MPow`、`GetPower`、`GetPower13`、`GetAttackPower`、`GetMagStruckDamage`、`AntiMagic`、`DELAY*10` |
| `ModernServer\tests\golden\skill_phase0\protocol_constants.json` | 技能相关协议常量和当前 C++ 冲突冻结 |
| `ModernServer\tests\golden\skill_phase0\spell_sequence_cases.json` | 火球、MP 不足、冷却过快、目标丢失、治愈、训练经验同步的消息顺序样本 |

## 输入源清单与 SHA256

| 路径 | 大小 | SHA256 |
| --- | ---: | --- |
| `Source\M2Server\Magic.pas` | 42687 | `85F74716D2451F1A742F76DF53C6FADC2DBD36FC16FDD302ED9710376613ED2F` |
| `Source\M2Server\ObjBase.pas` | 536680 | `CE70E1E93712E13036A065E5DE4E76AE53FF7D8304FD7FA0C048509DAAECD46C` |
| `Source\M2Server\LocalDB.pas` | 77888 | `9ACB073E4D63263A6D485204444E2D4AF40B6E56A5F3C0C8AE3BC67D5EB4622F` |
| `Source\M2Server\RunDB.pas` | 18516 | `CB4C1A5ED825FF16E2343F401B1AAB0C61E75C2BD9DA7E5E3E53C46441D8388D` |
| `Source\Mir200\Magic.pas` | 42645 | `0AB12537B394528D72868620E9167F602D1981FF295CC6D4B3DCB8A9872D60B2` |
| `Source\Mir200\ObjBase.pas` | 535474 | `FC99BBFD45C08BD6F49A353ED98EB7B6CD2068C926DF02355E7EAF20FD8276E6` |
| `Source\Mir200\LocalDB.pas` | 77863 | `B788AA5105CE3750A98D6B20136689AAEFA5F16B29C9A04880FDCCAA58AE2238` |
| `Source\Mir200\RunDB.pas` | 18516 | `CB4C1A5ED825FF16E2343F401B1AAB0C61E75C2BD9DA7E5E3E53C46441D8388D` |
| `Source\Common\Grobal2.pas` | 62473 | `23A3DE0660C4E901D02D7615E5180E86287222DC676981989290AA62169F2BBF` |
| `Source\Common\MfdbDef.pas` | 38024 | `243E86E58CDF947D24213AF5E2F5BAC36BCDCCFEF16B33E4782D84CA276DFAE9` |
| `Source\Client\ClMain.pas` | 204526 | `450E54CA1926D55842A24B23DF3E0B701E941916DD9CA58525102848FEC88868` |
| `Source\Client\magiceff.pas` | 40363 | `739E16107537800B67D80E3162FF3FBC96BE4479D389227C827598EDD98887C9` |
| `Release\Mir200\Data.mdb` | 1765376 | `2BB9385B12760A64211D016C2E427A4A87E946B3CAC494B150CA549F58337C39` |
| `Legend of Mir\Data\Magic.wil` | 34117268 | `BE46A0258349B26DB9BA7DBA595ABAC1F0767D52FEF11D9F508E704F2F6DEAAC` |
| `Legend of Mir\Data\Magic.WIX` | 16088 | `7F95FF7BA4ADD42157E10EB0D43EE7C77FD3C5AC3EA1183E39971066A40DFC91` |
| `Legend of Mir\Data\Magic2.wil` | 1201252 | `398E5376F19638DF063CD6299199BF5C2365FA8525FE0C9E639EB3BB6C955D07` |
| `Legend of Mir\Data\Magic2.WIX` | 180 | `BC57FFA767053C8727CA6DA8D2BE88A0FC3224C36CC1A1285173A0856CF7DF82` |
| `Legend of Mir\Data\MagIcon.wil` | 61424 | `22109B4327ADBEF871EB0008E4FFF928D60020C400B43441A7522ABF33656A02` |
| `Legend of Mir\Data\MagIcon.WIX` | 336 | `B5E646CB44EBD117EB083053A11F7500166293621281CBA423349459E4FD0472` |
| `ModernServer\config\magic\default_magic.toml` | 1071 | `2D84F869035ADE6BF29C4C9126612948BBB0A1D3675F5ECCE133D19D2AF03439` |
| `ModernServer\config\magic\imported_magic.toml` | 74 | `B1E6AA408C0178977ED3AA42D7306FBD85631BD5F6147F2CAC6EF7D087836ED8` |
| `ModernServer\src\config\models.hpp` | 11040 | `00037D7CDC4C374223C01B28C59906E8E25F57E4353429D4BE3157400BBF6E5A` |
| `ModernServer\src\protocol\legacy_types.hpp` | 12111 | `3A789BCED4ED9864313D5487D287911E367AE054740F1D269476AFE37FA77896` |
| `ModernServer\src\world\map_actor.cpp` | 398393 | `275418A6926B4D93462D25FA6B21B63CF24A6D4F48903D0903F45203AEBC8EDF` |
| `ModernServer\src\world\game_object.cpp` | 28963 | `0CEE11172502E60ECDBA516D529E3C1BE5A6431CABC42DC17F72AF1980FEC846` |
| `ModernServer\src\world\legacy_frame_driver.hpp` | 1894 | `AAECC31DB784FDEE1E5F5864592B6CE18F5B7A8AB4B06A7D153EB33E85A98DB5` |
| `ModernClient\src\animation\legacy_animation.cpp` | 86610 | `CBD0616C557E306A3CAE940B88F8C54EACE6BEFFF021C24B6267FE06E2DDB125` |
| `ModernClient\src\animation\legacy_animation.hpp` | 25576 | `568DDE0AEC11E61E021EAFC48B234AB388D9BDB3B35557B8415F728FB5A04B87` |

## 双源差异结论

| 文件 | diff 行数 | 影响分类 | 结论 |
| --- | ---: | --- | --- |
| `Magic.pas` | 83 | 不影响行为，暂定 | diff 主要集中在注释和技能名注释文字，`IsSwordSkill`、`MPow`、`SpellNow` 的 case 结构未观察到行为改动 |
| `ObjBase.pas` | 77 | 待确认 | 存在 `AM_FIREBALL = 1` 与 `AM_FIREBALL = 8` 的常量差异；其余多为注释、管理命令、断线处理和 `ExistAttackSlaves`，但 `AM_FIREBALL` 可能影响非玩家技能或怪物攻击 |
| `LocalDB.pas` | 4 | 不影响技能行为 | diff 位于物品 `ATKSPD/EFFVALUE2` 注释，不在 `LoadMagic` |
| `RunDB.pas` | 0 | 不影响行为 | 文件一致 |

阶段1不要混用两个 Delphi 源树中的同名文件。若实现触及 `ObjBase.pas` 中怪物攻击、特殊攻击或 `AM_FIREBALL`，需要先确认目标版本应采用哪个常量。

## Magic.DB 抽取结果

抽取状态：成功。  
读取方式：32 位 `Microsoft.Jet.OLEDB.4.0`。  
表名：`Magic`。  
记录数：33。  
完整字段已冻结在 `magic_db.json`：`ID/Name/EffectType/Effect/Spell/Power/MaxPower/Job/NeedL1..3/L1Train..3Train/DELAY/DefSpell/DefPower/DefMaxPower/Descr`。

Delphi `LoadMagic` 的结构映射证据：

- `Source\M2Server\LocalDB.pas:291`：`MagicId := ID`
- `Source\M2Server\LocalDB.pas:293` 到 `Source\M2Server\LocalDB.pas:297`：`EffectType/Effect/Spell/Power/MaxPower`
- `Source\M2Server\LocalDB.pas:299` 到 `Source\M2Server\LocalDB.pas:306`：`NeedLevel[3] = NeedL3`，`MaxTrain[3] = L3Train`
- `Source\M2Server\LocalDB.pas:307`：`MaxTrainLevel := 3`
- `Source\M2Server\LocalDB.pas:308`：`DelayTime := DELAY * 10`

`Magic` 表中缺少但源码引用的 ID：

| ID | 源码证据 | 风险 |
| ---: | --- | --- |
| 34 | `IsSwordSkill` 与 `SWD_CROSSHIT` 引用 | DB 无定义，阶段1不能默认开放 |
| 35 | `SpellNow` 与 ID 11 分组 | DB 无定义，可能是扩展技能或残留分支 |
| 36 | 符咒类技能分组引用 | DB 无定义，可能是扩展技能或残留分支 |
| 37 | 与 ID 8 抗拒/推人分组 | DB 无定义，可能是扩展技能或残留分支 |

## MagicID 行为矩阵

| ID | 名称 | Job | 消耗字段 | 伤害字段 | Delay(ms) | Delphi 分支证据 | 阶段1注意 |
| ---: | --- | ---: | ---: | --- | ---: | --- | --- |
| 1 | 火球术 | 0 | 4+1 | 8..8+2..2 | 600 | `SpellNow` case `1,5` | 命中后 `RM_DELAYMAGIC` 延迟 600 |
| 2 | 治愈术 | 0 | 7+0 | 14..20 | 400 | 治愈分支 | 目标 nil 时回退自己，`RM_MAGHEALING` 延迟 800 |
| 3 | 基本剑术 | 0 | 0 | 0..0 | 0 | `IsSwordSkill` | 不走普通 `SpellNow` |
| 4 | 精神力战法 | 0 | 0 | 0..0 | 0 | `IsSwordSkill` | 武器/被动类，不能按主动魔法结算 |
| 5 | 大火球 | 0 | 3+5 | 6..6+10..10 | 600 | `SpellNow` case `1,5` | 与火球同分支，数据不同 |
| 6 | 施毒术 | 0 | 4+0 | 0..0 | 400 | 符咒/毒分支 | 需要符与状态叠加规则 |
| 7 | 攻杀剑术 | 0 | 0 | 0..0 | 0 | `IsSwordSkill` | 战士攻击流程，不按魔法包序 |
| 8 | 抗拒火环 | 0 | 8+0 | 1..1 | 300 | case `8,37` | 推人概率、等级差、邻格判断 |
| 9 | 地狱火 | 0 | 10+10 | 14..14+6..6 | 600 | 直线穿透 | 方向、路径和 13 格循环需对齐 |
| 10 | 疾光电影 | 0 | 30+30 | 12..12+12..12 | 1000 | 直线穿透 | 高 MP 消耗，方向判定敏感 |
| 11 | 雷电术 | 0 | 12+6 | 14..30+10..12 | 1000 | case `11,35` | `AntiMagic` 与 undead modifier |
| 12 | 刺杀剑术 | 0 | 0 | 0..0 | 0 | `IsSwordSkill` | 攻击距离和开关状态另走战斗流程 |
| 13 | 灵魂火符 | 0 | 5+2 | 8..10+3..3 | 600 | 符咒分支 | `CanUseBujuk`、扣符、延迟命中 |
| 14 | 幽灵盾 | 0 | 15+0 | 0..0 | 400 | 符咒状态分支 | 状态持续和广播需从源码补全 |
| 15 | 神圣战甲术 | 0 | 15+0 | 0..0 | 400 | 符咒状态分支 | 状态叠加规则需先冻结 |
| 16 | 困魔咒 | 0 | 10+5 | 0..0 | 500 | 符咒状态分支 | 地图/目标限制需补运行时样本 |
| 17 | 召唤骷髅 | 0 | 16+8 | 0..0 | 500 | 召唤分支 | 召唤物生命周期和数量限制 |
| 18 | 隐身术 | 0 | 5+0 | 0..0 | 500 | 状态分支 | 隐身状态广播、怪物仇恨 |
| 19 | 集体隐身术 | 0 | 10+0 | 0..0 | 500 | 状态范围分支 | 可见对象遍历顺序 |
| 20 | 诱惑之光 | 0 | 3+3 | 3..3+0..0 | 600 | 驯服分支 | `MagLightingShock` 随机链复杂 |
| 21 | 瞬息移动 | 0 | 10+8 | 0..0 | 500 | 传送分支 | 地图限制和失败路径需补样本 |
| 22 | 火墙 | 0 | 20+25 | 3..3+3..3 | 1200 | 火墙分支 | 地面持续效果、格子对象、客户端 Mon21/Magic 资源 |
| 23 | 爆裂火焰 | 0 | 15+10 | 8..8+6..6 | 600 | 范围爆炸 | 范围枚举和广播顺序 |
| 24 | 地狱雷光 | 0 | 35+20 | 10..30+10..30 | 600 | 范围雷电 | 距离、数量、随机顺序 |
| 25 | 半月弯刀 | 0 | 0+3 | 0..0 | 0 | `IsSwordSkill` | 攻击流程中的扇形判定 |
| 26 | 烈火剑法 | 0 | 0+7 | 0..0 | 0 | `IsSwordSkill` | 开关、MP 扣除和下次攻击触发 |
| 27 | 野蛮冲撞 | 0 | 15+0 | 0..0 | 0 | `IsSwordSkill` | 独立 3000ms rush 冷却 |
| 28 | 心灵启示 | 0 | 16+0 | 0..0 | 400 | 探测/查看血量 | level>=2 会启用看血能力 |
| 29 | 群体治愈术 | 0 | 12+30 | 10..10+4..4 | 400 | 群体治疗 | 可见对象范围和治疗帧 |
| 30 | 召唤神兽 | 0 | 16+24 | 0..0 | 1200 | 召唤分支 | 符数量 5、召唤物替换 |
| 31 | 魔法盾 | 0 | 20+30 | 0..0 | 0 | 护盾分支 | 魔法盾减伤公式和持续扣减 |
| 32 | 圣言术 | 0 | 50+40 | 0..0 | 1200 | 圣言分支 | 怪物等级、随机和死亡路径 |
| 33 | 冰咆哮 | 0 | 12+30 | 12..14+14..14 | 600 | 范围冰系 | 与爆裂/范围法术的枚举顺序比较 |

说明：`消耗字段` 展示为 `Spell + DefSpell` 原始字段，实际消耗由 `GetSpellPoint` 按等级计算；`伤害字段` 展示为 `Power..MaxPower + DefPower..DefMaxPower` 原始字段，实际伤害还经过 `MPow/GetPower/GetAttackPower/GetMagStruckDamage`。

## Delphi 核心行为证据

### 数据结构

`Source\Common\Grobal2.pas:483` 定义 `TDefMagic`，字段包括 `MagicId/MagicName/EffectType/Effect/Spell/MinPower/NeedLevel/MaxTrain/MaxTrainLevel/Job/DelayTime/DefSpell/DefMinPower/MaxPower/DefMaxPower/Desc`。`Source\Common\Grobal2.pas:503` 定义 `TUserMagic`，持有 `pDef/MagicId/Level/Key/CurTrain`。阶段1应优先把当前 `LegacyDefMagic` 与 `TDefMagic` 对齐，而不是扩展现有简化 `MagicConfig` 当权威。

### 技能分类

`Source\M2Server\Magic.pas:46` 的 `IsSwordSkill` 返回 true 的 ID 为 `3,4,7,12,25,26,27,34`。这些技能不走普通 `SpellNow` 分支，阶段1不能把它们当作普通远程魔法处理。

### 公式

已冻结到 `formula_cases.json`：

- `MPow := MinPower + Random(MaxPower-MinPower)`，上界不包含 `MaxPower`
- `GetSpellPoint := Round(Spell/(MaxTrainLevel+1)*(Level+1)) + DefSpell`
- `GetPower := Round(pw/(MaxTrainLevel+1)*(Level+1)) + DefMinPower + Random(DefMaxPower-DefMinPower)`
- `GetPower13 := Round(pw/3 + (pw-pw/3)/(MaxTrainLevel+1)*(Level+1) + DefMinPower + Random(...))`
- `GetAttackPower` 受 `Luck` 影响，`ranval < 0` 会被夹到 0
- `GetMagStruckDamage` 先扣 `MAC` 随机护甲，再加 undead bonus，再按魔法盾比例缩放并调用 `DamageBubbleDefence`
- `AntiMagic` 命中判断是 `AntiMagic <= Random(10)`

兼容风险：Delphi `Round` 默认是银行家舍入；C++ `std::round/std::lround` 是远离零舍入，不兼容。

### 时序

普通技能请求由 `Source\M2Server\ObjBase.pas:9405` 的 `SpellXY` 接收。关键顺序：

1. `GetTickCount - LatestSpellTime > LatestSpellDelay` 决定是否清零 `SpellTimeOverCount`
2. 普通技能设置 `LatestSpellDelay := pum.pDef.DelayTime + 800`
3. `DoSpell` 先按 `GetSpellPoint` 扣 MP，并调用 `HealthSpellChanged`
4. `MagicMan.SpellNow` 先广播 `RM_SPELL`
5. 具体技能分支可能投递 `RM_DELAYMAGIC/RM_MAGSTRUCK/RM_MAGHEALING`
6. 结尾广播 `RM_MAGICFIRE`，失败路径广播 `RM_MAGICFIRE_FAIL`
7. 训练经验通过 `RM_MAGIC_LVEXP` 延迟 800 或 1000 同步

当前 C++ 的立即结算流程不能直接替代上述队列时序。

### 协议

`Source\Common\Grobal2.pas` 中技能相关常量：

- `SM_ADDMAGIC = 210`
- `SM_SENDMYMAGIC = 211`
- `SM_DELMAGIC = 212`
- `SM_MAGICFIRE = 638`
- `SM_MAGICFIRE_FAIL = 639`
- `SM_MAGIC_LVEXP = 640`
- `SM_AREASTATE = 708`

当前 C++ `ModernServer\src\protocol\legacy_types.hpp:61` 定义 `kSmAreaState = 212`，与 Delphi `SM_DELMAGIC=212` 冲突。阶段1任何协议工作前必须先修正这个冲突。

### 客户端表现

`Source\Client\ClMain.pas:4273` 处理 `SM_SPELL`，调用 `UseMagicSpell(who, effectnum, tx, y, magic_id)`。`Source\Client\ClMain.pas:4277` 处理 `SM_MAGICFIRE`，调用 `UseMagicFire(who, efftype, effnum, targetx, targety, target)`。

注意：Delphi 客户端 `magiceff.pas` 的 `EffectBase` 索引来自服务端 `Effect` 或客户端 `EffectNumber` 路径，不应简单等同于 `MagicID`。当前 `ModernClient\src\animation\legacy_animation.cpp` 注释写“索引 = magic_id”，而代码也按 `magic_id` 查 `kEffectBase`，阶段1需要逐 ID 校准 `MagicID/Effect/EffectType/EffectBase/Magic2.wil/Mon21.wil`。

## 当前 C++ 对齐风险

| 风险 | 当前证据 | 阶段1处理 |
| --- | --- | --- |
| `MagicConfig` 是简化模型 | `ModernServer\src\config\models.hpp:164` 只有 `mp_cost/power/radius` 等现代字段 | 不能作为 Delphi 技能定义权威；应从 `magic_db.json` 生成或加载 `LegacyDefMagic` 等价结构 |
| `LegacyDefMagic` 已接近 Delphi 结构 | `ModernServer\src\protocol\legacy_types.hpp:290` | 可保留并作为网络兼容结构基础 |
| 当前 spell 立即结算 | `ModernServer\src\world\map_actor.cpp:7510` 到后续 damage/apply | 阶段1需要接入 Delphi 式 delay queue，不要只改伤害公式 |
| 协议 212 冲突 | `kSmAreaState=212` vs `SM_DELMAGIC=212` | 阶段1前置阻塞 |
| 缺少 `SM_MAGICFIRE*` 常量 | C++ 未冻结 638/639/640 | 先补协议常量和 golden test |
| 客户端动画索引风险 | `legacy_animation.cpp:299`、`:932` | 以 Delphi `Effect` 而非现代假设逐项校准 |
| `std::lround` 风险 | 公式需要 Delphi Round | 统一实现 Delphi round helper 并覆盖公式测试 |

## 阶段1前门禁

阻塞项：

1. 协议 `212` 冲突必须修正，`SM_DELMAGIC=212` 与 `SM_AREASTATE=708` 不能混淆。
2. `MagicConfig` 不能作为技能权威；必须引入或生成 Delphi 等价 `TDefMagic` 数据路径。
3. 普通魔法不能继续仅做立即结算；必须先设计接入 `RM_DELAYMAGIC/RM_MAGSTRUCK/RM_MAGHEALING/RM_MAGICFIRE` 的最小队列。
4. `MagicID 34..37` 源码引用但 DB 缺失，不能默认启用，需标记为 source-only 或等待版本确认。
5. 客户端 `EffectBase` 映射需要按 `MagicID + Effect + EffectType` 校验，不能按当前 `magic_id` 注释直接迁移。

可进入阶段1的最小证据集合：

- `magic_db.json` 的 33 条 Magic 记录
- `formula_cases.json` 的公式样本
- `protocol_constants.json` 的协议常量与冲突
- `spell_sequence_cases.json` 的普通魔法消息顺序
- 本文档的双源差异结论与 C++ 风险清单

## 推荐阶段1最小实现顺序

1. 协议常量对齐：修正 `SM_DELMAGIC/SM_AREASTATE/SM_MAGICFIRE/SM_MAGICFIRE_FAIL/SM_MAGIC_LVEXP`，加常量测试。
2. 数据结构对齐：从 `magic_db.json` 或 MDB 生成 Delphi 等价 `LegacyDefMagic`，保留 `NeedLevel/MaxTrain/DelayTime/DefSpell/DefPower/DefMaxPower`。
3. 公式测试先行：用 `formula_cases.json` 写 C++ 单元测试，先覆盖 Round、MP、伤害、魔防、魔法盾和 `DelayTime*10`。
4. 施法入口最小兼容：实现 `SpellXY -> DoSpell -> SpellNow` 的最小普通魔法路径，只覆盖火球、治愈、MP 不足、冷却过快。
5. 队列与包序：接入 delay message，让 `SM_SPELL`、`SM_MAGICFIRE`、命中/治疗、经验同步按 golden 顺序发出。
6. 客户端表现校准：按 `Effect/EffectType` 校准 `EffectBase`，先覆盖 ID 1、2、5、11、22、29、31、33。
7. 扩展特殊技能：再处理符咒、状态、召唤、武器技能、传送和范围技能。

## 待补运行时捕获

以下项目源码证据已经定位，但仍建议用 Delphi 运行时抓包或日志确认：

- 目标丢失、目标死亡、坐标偏移时客户端是否看到 `SM_SPELL` 后接 `SM_MAGICFIRE_FAIL`
- Delphi `Randomize/Random` 的种子和跨消息调用顺序
- 部署环境下 Delphi `Round` 的 FPU control word
- 火墙、冰咆哮、群体隐身、召唤类技能的可见对象枚举顺序
- 魔法盾持续时间扣减和叠加刷新规则

## 阶段3主动法术证据补充

阶段3启用的 legacy 兼容施法集合为 `1,2,5,8,9,10,11,23,24,28,29,31,32,33`。`35/37` 只作为 Delphi 源码同分支证据保留：`35` 与雷电术 `11` 同分支，`37` 与抗拒火环 `8` 同分支，但它们不在 `Release\Mir200\Data.mdb` 的 active Magic 表内，因此阶段3不允许施放、读书学习或进入 active config。

已新增机器可读样本：`ModernServer/tests/golden/skill_phase0/active_spell_cases.json`。该文件冻结每个阶段3新增 MagicID 的源码来源、随机调用、delay、训练条件和仍需运行时抓包确认的缺口。

### 源码分支摘要

| MagicID | 行为 | Delphi 证据 | 阶段3落地约束 |
| --- | --- | --- | --- |
| `8` | 抗拒火环 | `Magic.pas` case `8,37`，`ObjBase.pas` `MagPushArround` | 邻格目标、等级差、`Random(20)` gate、`Random(2)` 推开距离；只有实际移动才训练 |
| `9` | 地狱火 | `Magic.pas` case `9`，`ObjBase.pas` `MagPassThroughMagic(FALSE)` | 按施法坐标求方向，最多 5 格，逐目标 `AntiMagic <= Random(10)`，600ms `RM_MAGSTRUCK` |
| `10` | 疾光电影 | `Magic.pas` case `10`，`ObjBase.pas` `MagPassThroughMagic(TRUE)` | 按施法坐标求方向，最多 8 格，命中 power 使用 `Round(pwr * 1.5)`，600ms delayed hit |
| `11` | 雷电术 | `Magic.pas` case `11,35` | 单体目标，`AntiMagic` 后 600ms delayed hit，undead 目标 `Round(pwr * 1.5)` |
| `23` | 爆裂火焰 | `Magic.pas` case `23`，`ObjBase.pas` `MagBigExplosion/GetMapCreatures` | 目标坐标半径 1，按 Delphi 地图枚举顺序即时 `RM_MAGSTRUCK` |
| `24` | 地狱雷光 | `Magic.pas` case `24`，`ObjBase.pas` `MagElecBlizzard` | 施法者半径 2，undead 全额，非 undead `pwr div 10` |
| `28` | 心灵启示 | `Magic.pas` case `28` | `Random(6) <= 3 + level`，1500ms 后只设置服务端 open-health marker；阶段3不加客户端血条协议 |
| `29` | 群体治愈术 | `Magic.pas` case `29`，`ObjBase.pas` `MagBigHealing` | 目标坐标半径 1，友方且受伤玩家才排 800ms `RM_MAGHEALING` |
| `31` | 魔法盾 | `Magic.pas` case `31`，`ObjBase.pas` `MagBubbleDefenceUp/GetMagStruckDamage` | 当前无盾才激活；魔法伤害按 `Round(damage / 100 * (level+2)*8)` 缩放，并扣减护盾剩余时间 |
| `32` | 圣言术 | `Magic.pas` case `32` | undead、非 NeverDie、等级 gate、`Random(100)` kill gate；成功死亡才训练 |
| `33` | 冰咆哮 | `Magic.pas` case `33`，`ObjBase.pas` `GetMapCreatures` | 目标坐标半径 1，按范围枚举即时 `RM_MAGSTRUCK`，客户端冰系表现后续校准 |

### 当前 C++ 对齐状态

- `ModernServer/src/world/map_actor.cpp` 的 legacy spell gate 已扩展到阶段3支持集；非支持 ID 继续走旧 fallback，`34..37` 不进入支持集。
- `ActorMail` 使用明确的 `LegacyDelayedEffectKind` 和字段承载 delayed hit/heal/open-health，不使用字符串 payload 携带技能数据。
- `SpawnConfig.life_attrib` 已接入 C++ 配置和怪物运行时，用于区分 Delphi `LA_UNDEAD`。
- `Player`/`Monster` 增加最小运行时状态：magic bubble 和 open-health marker。
- `mir2_skill_active_spell_phase3_smoke` 覆盖阶段3新增主动技能的最小成功路径、delay、训练和关键失败路径；它不是完整 Delphi 抓包替代品，仍需后续补多目标同格顺序和客户端表现校准。

## 阶段4符咒、毒与基础状态证据补充

阶段4启用的新增 legacy 兼容施法集合为 `6,13,14,15,18,19`。阶段4结束后的普通 legacy 支持集为 `1,2,5,6,8,9,10,11,13,14,15,18,19,23,24,28,29,31,32,33`。`16/17/20/21/22/30` 继续延期到地图事件、召唤、诱惑、瞬移和火墙阶段；`34..37` 继续 source-only。

新增机器可读样本：`ModernServer/tests/golden/skill_phase0/bujuk_status_spell_cases.json`。该文件冻结阶段4技能的符咒/毒粉形态、delay、状态持续、随机调用和训练条件。

### 源码分支摘要

| MagicID | 行为 | Delphi 证据 | 阶段4落地约束 |
| --- | --- | --- | --- |
| `6` | 施毒术 | `Magic.pas` case `6`，`ObjBase.pas` `MakePoison`，`Grobal2.pas` `POISON_DECHEALTH=0/POISON_DAMAGEARMOR=1` | 装备位 `U_BUJUK` 优先，`U_ARMRINGL` fallback；物品 `StdMode=25` 且 `Shape<=2`，`Shape=1` 掉血毒、`Shape=2` 破防毒；扣 100 持久后 1000ms `RM_MAKEPOISON`，掉血毒每 2500ms 造成 `1+PoisonLevel` |
| `13` | 灵魂火符 | `Magic.pas` 符咒组 case `13` | `CanUseBujuk(user,1)` 后扣 100 持久；SC 公式，`AntiMagic <= Random(10)`，1200ms `RM_DELAYMAGIC`；目标为怪物/动物时训练 |
| `14` | 幽灵盾 | `Magic.pas` case `14`，`ObjBase.pas` `MagMakeDefenceArea(..., TRUE)`/`MagMagDefenceUp` | 半径 3 友方目标，提升 magic defence；只在新状态或更长 duration 生效时计入训练 |
| `15` | 神圣战甲术 | `Magic.pas` case `15`，`ObjBase.pas` `MagMakeDefenceArea(..., FALSE)`/`MagDefenceUp` | 半径 3 友方目标，提升 physical defence；过期后恢复防御 |
| `18` | 隐身术 | `Magic.pas` case `18`，`MagMakePrivateTransparent` | 已隐身时不训练；成功设置 `STATE_TRANSPARENT`，并按现有怪物仇恨字段尝试清理目标；移动会解除 fixed hide |
| `19` | 集体隐身术 | `Magic.pas` case `19`，`MagMakeGroupTransparent` | 半径 1 友方、未隐身目标排 800ms `RM_TRANSPARENT`；至少一个目标入队才训练 |

### 当前 C++ 对齐状态

- `ActorMail::LegacyDelayedEffectKind` 增加 `make_poison` 和 `transparent`，并用显式字段承载 poison kind、poison level、duration ticks，不使用字符串 payload。
- `Player`/`Monster` 增加 legacy poison 状态；`Player` 增加 defence up、magic defence up、transparent 状态位。状态位使用 Delphi `GetCharStatus` bit 位置：`STATE_TRANSPARENT=8`、`STATE_DEFENCEUP=9`、`STATE_MAGDEFENCEUP=10`。
- `MapActor` 新增符咒/毒粉装备检查：普通符咒要求 `Shape=5`，毒粉要求 `Shape<=2`；均优先 `kEquipBujuk`，再 fallback `kEquipArmRingLeft`。
- `mir2_skill_bujuk_status_phase4_smoke` 覆盖符咒消耗、缺符失败、灵魂火符 delayed hit、施毒 delayed status、基础防御状态和单体/群体隐身。
