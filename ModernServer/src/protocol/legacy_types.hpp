/**
 * @file legacy_types.hpp
 * @brief 遗留协议类型定义
 *
 * @details 本文件定义了与经典 Legacy（Delphi）服务器兼容的核心数据类型和常量。
 * 这是整个协议模块的基础层，所有其他协议组件都依赖于本文件中的定义。
 *
 * 主要内容：
 * 1. 协议消息标识符常量（Sm/Cm 系列），覆盖登录、游戏、物品、交易等各子系统
 * 2. 装备槽位索引常量（kEquip*）
 * 3. 打包对齐的数据结构（LegacyDefaultMessage、LegacyUserItem、LegacyStdItem 等）
 * 4. 辅助函数（大小端字/双字操作、特征码构造等）
 *
 * @note 结构体使用 #pragma pack(push, 1) 确保与 Delphi 服务器的内存布局完全兼容。
 *       编译期 static_assert 验证了关键结构体的尺寸和字段偏移量。
 * @warning 修改任何结构体成员顺序或类型前，必须确认与 Delphi 端内存布局的一致性。
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace mir2 {

// ─── 网关消息标识符 ─────────────────────────────────────────────
constexpr std::uint16_t kLegacyGmData = 5;  ///< 网关数据消息标识

// ─── 登录/账号协议常量 ──────────────────────────────────────────
constexpr std::uint16_t kSmPasswdSuccess = 502;      ///< 密码验证成功
constexpr std::uint16_t kSmPasswdFail = 503;          ///< 密码验证失败
constexpr std::uint16_t kSmNewIdSuccess = 504;        ///< 创建新账号成功
constexpr std::uint16_t kSmNewIdFail = 505;           ///< 创建新账号失败
constexpr std::uint16_t kSmChgPasswdSuccess = 506;    ///< 修改密码成功
constexpr std::uint16_t kSmChgPasswdFail = 507;       ///< 修改密码失败
constexpr std::uint16_t kSmQueryChr = 520;            ///< 查询角色列表
constexpr std::uint16_t kSmNewChrSuccess = 521;       ///< 创建角色成功
constexpr std::uint16_t kSmNewChrFail = 522;          ///< 创建角色失败
constexpr std::uint16_t kSmDelChrSuccess = 523;       ///< 删除角色成功
constexpr std::uint16_t kSmDelChrFail = 524;          ///< 删除角色失败
constexpr std::uint16_t kSmStartPlay = 525;           ///< 开始游戏
constexpr std::uint16_t kSmStartFail = 526;           ///< 开始游戏失败
constexpr std::uint16_t kSmQueryChrFail = 527;        ///< 查询角色失败
constexpr std::uint16_t kSmOutOfConnection = 528;     ///< 连接数超限
constexpr std::uint16_t kSmPassOkSelectServer = 529;  ///< 密码通过，选择服务器
constexpr std::uint16_t kSmSelectServerOk = 530;      ///< 选择服务器成功
constexpr std::uint16_t kSmNeedUpdateAccount = 531;   ///< 需要更新账号信息
constexpr std::uint16_t kSmUpdateIdSuccess = 532;     ///< 更新账号成功
constexpr std::uint16_t kSmUpdateIdFail = 533;        ///< 更新账号失败

// ─── 游戏内服务器消息标识符 ─────────────────────────────────────
constexpr std::uint16_t kSmTurn = 10;           ///< 转身通知
constexpr std::uint16_t kSmRush = 6;            ///< 冲锋
constexpr std::uint16_t kSmRushKung = 7;        ///< 冲拳
constexpr std::uint16_t kSmBackStep = 9;        ///< 后退
constexpr std::uint16_t kSmWalk = 11;           ///< 行走通知
constexpr std::uint16_t kSmSitDown = 12;        ///< 坐下
constexpr std::uint16_t kSmRun = 13;            ///< 跑步通知
constexpr std::uint16_t kSmHit = 14;            ///< 受击
constexpr std::uint16_t kSmSpell = 17;          ///< 施法通知
constexpr std::uint16_t kSmDigUp = 20;          ///< 破土/钻出
constexpr std::uint16_t kSmDigDown = 21;        ///< 钻回地下
constexpr std::uint16_t kSmAlive = 27;          ///< 存活状态
constexpr std::uint16_t kSmMoveFail = 28;       ///< 移动失败
constexpr std::uint16_t kSmDisappear = 30;      ///< 消失
constexpr std::uint16_t kSmStruck = 31;         ///< 被击中
constexpr std::uint16_t kSmDeath = 32;          ///< 死亡
constexpr std::uint16_t kSmNowDeath = 34;       ///< 立即死亡
constexpr std::uint16_t kSmHear = 40;           ///< 听到消息
constexpr std::uint16_t kSmFeatureChanged = 41; ///< 外观特征改变
constexpr std::uint16_t kSmUsername = 42;       ///< 用户名信息
constexpr std::uint16_t kSmWinExp = 44;         ///< 获得经验
constexpr std::uint16_t kSmLevelUp = 45;        ///< 升级
constexpr std::uint16_t kSmBreakWeapon = 1102;  ///< 武器损坏
constexpr std::uint16_t kSmDayChanging = 46;    ///< 昼夜变化
constexpr std::uint16_t kSmLogon = 50;          ///< 登录游戏
constexpr std::uint16_t kSmNewMap = 51;         ///< 进入新地图
constexpr std::uint16_t kSmAbility = 52;        ///< 属性信息
constexpr std::uint16_t kSmHealthSpellChanged = 53; ///< 血量/法术值变化
constexpr std::uint16_t kSmMapDescription = 54;     ///< 地图描述
constexpr std::uint16_t kSmSysMessage = 100;        ///< 系统消息
constexpr std::uint16_t kSmGroupMessage = 101;      ///< 组队消息
constexpr std::uint16_t kSmCry = 102;               ///< 喊话
constexpr std::uint16_t kSmWhisper = 103;           ///< 私聊
constexpr std::uint16_t kSmGuildMessage = 104;      ///< 行会消息
constexpr std::uint16_t kSmAddItem = 200;           ///< 添加物品
constexpr std::uint16_t kSmBagItems = 201;          ///< 背包物品列表
constexpr std::uint16_t kSmDelItem = 202;           ///< 删除物品
constexpr std::uint16_t kSmUpdateItem = 203;        ///< 更新物品
constexpr std::uint16_t kSmAddMagic = 210;          ///< 添加魔法
constexpr std::uint16_t kSmSendMyMagic = 211;       ///< 发送魔法列表
constexpr std::uint16_t kSmDelMagic = 212;          ///< 删除魔法
constexpr std::uint16_t kSmSubAbility = 752;        ///< 属性点分配
constexpr std::uint16_t kSmDropItemSuccess = 600;   ///< 丢弃物品成功
constexpr std::uint16_t kSmDropItemFail = 601;      ///< 丢弃物品失败
constexpr std::uint16_t kSmItemShow = 610;          ///< 物品显现
constexpr std::uint16_t kSmItemHide = 611;          ///< 物品隐藏
constexpr std::uint16_t kSmOpenDoorOk = 612;        ///< 开门成功
constexpr std::uint16_t kSmOpenDoorLock = 613;      ///< 门已锁定
constexpr std::uint16_t kSmCloseDoor = 614;         ///< 关门
constexpr std::uint16_t kSmMagicFire = 638;         ///< 魔法释放
constexpr std::uint16_t kSmMagicFireFail = 639;     ///< 魔法释放失败
constexpr std::uint16_t kSmMagicLvExp = 640;        ///< 魔法等级经验
constexpr std::uint16_t kSmChangeLight = 654;       ///< 光照变化
constexpr std::uint16_t kSmChangeNameColor = 656;   ///< 名字颜色变化
constexpr std::uint16_t kSmCharStatusChanged = 657; ///< 角色状态变化
constexpr std::uint16_t kSmSpaceMoveHide = 800;     ///< 空间移动（隐藏）
constexpr std::uint16_t kSmSpaceMoveShow = 801;     ///< 空间移动（显现）
constexpr std::uint16_t kSmShowEvent = 804;         ///< 显示事件
constexpr std::uint16_t kSmHideEvent = 805;         ///< 隐藏事件
constexpr std::uint16_t kSmSpaceMoveHide2 = 806;    ///< 空间移动隐藏（变体）
constexpr std::uint16_t kSmSpaceMoveShow2 = 807;    ///< 空间移动显现（变体）
constexpr std::uint16_t kSmTakeOnOk = 615;          ///< 穿戴成功
constexpr std::uint16_t kSmTakeOnFail = 616;        ///< 穿戴失败
constexpr std::uint16_t kSmTakeOffOk = 619;         ///< 脱下成功
constexpr std::uint16_t kSmTakeOffFail = 620;       ///< 脱下失败
constexpr std::uint16_t kSmSendUseItems = 621;      ///< 发送已使用的物品
constexpr std::uint16_t kSmWeightChanged = 622;     ///< 负重变化
constexpr std::uint16_t kSmEatOk = 635;             ///< 使用物品成功
constexpr std::uint16_t kSmEatFail = 636;           ///< 使用物品失败
constexpr std::uint16_t kSmDuraChange = 642;        ///< 耐久度变化
constexpr std::uint16_t kSmClearObjects = 633;      ///< 清除所有对象
constexpr std::uint16_t kSmChangeMap = 634;         ///< 切换地图
constexpr std::uint16_t kSmOpenHealth = 1100;       ///< 打开血量条
constexpr std::uint16_t kSmCloseHealth = 1101;      ///< 关闭血量条
constexpr std::uint16_t kSmMerchantSay = 643;       ///< NPC 说话
constexpr std::uint16_t kSmMerchantDlgClose = 644;  ///< NPC 对话框关闭
constexpr std::uint16_t kSmPlayDice = 1200;         ///< 掷骰子
constexpr std::uint16_t kSmSendGoodsList = 645;     ///< 发送商品列表
constexpr std::uint16_t kSmSendUserSell = 646;      ///< 发送玩家出售列表
constexpr std::uint16_t kSmSendBuyPrice = 647;      ///< 发送购买价格
constexpr std::uint16_t kSmUserSellItemOk = 648;    ///< 出售物品成功
constexpr std::uint16_t kSmUserSellItemFail = 649;  ///< 出售物品失败
constexpr std::uint16_t kSmBuyItemSuccess = 650;    ///< 购买成功
constexpr std::uint16_t kSmBuyItemFail = 651;       ///< 购买失败
constexpr std::uint16_t kSmSendDetailGoodsList = 652;   ///< 发送商品详情列表
constexpr std::uint16_t kSmGoldChanged = 653;           ///< 金币数量变化
constexpr std::uint16_t kSmDealMenu = 673;              ///< 交易菜单
constexpr std::uint16_t kSmDealTryFail = 674;           ///< 交易尝试失败
constexpr std::uint16_t kSmDealAddItemOk = 675;         ///< 交易添加物品成功
constexpr std::uint16_t kSmDealAddItemFail = 676;       ///< 交易添加物品失败
constexpr std::uint16_t kSmDealDelItemOk = 677;         ///< 交易删除物品成功
constexpr std::uint16_t kSmDealDelItemFail = 678;       ///< 交易删除物品失败
constexpr std::uint16_t kSmDealCancel = 681;            ///< 交易取消
constexpr std::uint16_t kSmDealRemoteAddItem = 682;     ///< 对方交易添加物品
constexpr std::uint16_t kSmDealRemoteDelItem = 683;     ///< 对方交易删除物品
constexpr std::uint16_t kSmDealChangeGoldOk = 684;      ///< 交易金币变更成功
constexpr std::uint16_t kSmDealChangeGoldFail = 685;    ///< 交易金币变更失败
constexpr std::uint16_t kSmDealRemoteChangeGold = 686;  ///< 对方交易金币变更
constexpr std::uint16_t kSmDealSuccess = 687;           ///< 交易成功
constexpr std::uint16_t kSmSendUserRepair = 668;        ///< 发送玩家修理列表
constexpr std::uint16_t kSmUserRepairItemOk = 669;      ///< 修理物品成功
constexpr std::uint16_t kSmUserRepairItemFail = 670;    ///< 修理物品失败
constexpr std::uint16_t kSmSendRepairCost = 671;        ///< 发送修理费用
constexpr std::uint16_t kSmSendUserStorageItem = 700;   ///< 发送仓库物品列表
constexpr std::uint16_t kSmStorageOk = 701;             ///< 存入仓库成功
constexpr std::uint16_t kSmStorageFull = 702;           ///< 仓库已满
constexpr std::uint16_t kSmStorageFail = 703;           ///< 存入仓库失败
constexpr std::uint16_t kSmSaveItemList = 704;          ///< 保存物品列表
constexpr std::uint16_t kSmTakeBackStorageItemOk = 705;     ///< 取回仓库物品成功
constexpr std::uint16_t kSmTakeBackStorageItemFail = 706;   ///< 取回仓库物品失败
constexpr std::uint16_t kSmTakeBackStorageItemFullBag = 707; ///< 取回时背包已满
constexpr std::uint16_t kSmAreaState = 708;                  ///< 区域状态

// ─── 客户端请求消息标识符（登录选择服务器阶段） ─────────────────
constexpr std::uint16_t kCmQueryChr = 100;    ///< 查询角色列表
constexpr std::uint16_t kCmNewChr = 101;      ///< 创建角色
constexpr std::uint16_t kCmDelChr = 102;      ///< 删除角色
constexpr std::uint16_t kCmSelChr = 103;      ///< 选择角色
constexpr std::uint16_t kCmSelectServer = 104; ///< 选择服务器
constexpr std::uint16_t kCmIdPassword = 2001;  ///< 账号密码登录
constexpr std::uint16_t kCmAddNewUser = 2002;  ///< 注册新用户
constexpr std::uint16_t kCmChangePassword = 2003; ///< 修改密码
constexpr std::uint16_t kCmUpdateUser = 2004;     ///< 更新用户信息

// ─── 客户端请求消息标识符（游戏内操作） ─────────────────────────
constexpr std::uint16_t kCmQueryUsername = 80;     ///< 查询用户名
constexpr std::uint16_t kCmQueryBagItems = 81;     ///< 查询背包物品
constexpr std::uint16_t kCmDropItem = 1000;        ///< 丢弃物品
constexpr std::uint16_t kCmPickup = 1001;          ///< 拾取物品
constexpr std::uint16_t kCmOpenDoor = 1002;        ///< 开门
constexpr std::uint16_t kCmTakeOnItem = 1003;      ///< 穿戴装备
constexpr std::uint16_t kCmTakeOffItem = 1004;     ///< 脱下装备
constexpr std::uint16_t kCmExchgTakeOnItem = 1005; ///< 交换穿戴装备
constexpr std::uint16_t kCmEat = 1006;             ///< 使用物品
constexpr std::uint16_t kCmClickNpc = 1010;        ///< 点击 NPC
constexpr std::uint16_t kCmMerchantDlgSelect = 1011;    ///< NPC 对话框选项选择
constexpr std::uint16_t kCmMerchantQuerySellPrice = 1012;  ///< 查询出售价格
constexpr std::uint16_t kCmUserSellItem = 1013;      ///< 出售物品
constexpr std::uint16_t kCmUserBuyItem = 1014;       ///< 购买物品
constexpr std::uint16_t kCmUserGetDetailItem = 1015; ///< 获取物品详细信息
constexpr std::uint16_t kCmDropGold = 1016;          ///< 丢弃金币
constexpr std::uint16_t kCmUserRepairItem = 1023;    ///< 修理物品
constexpr std::uint16_t kCmMerchantQueryRepairCost = 1024; ///< 查询修理费用
constexpr std::uint16_t kCmDealTry = 1025;            ///< 发起交易
constexpr std::uint16_t kCmDealAddItem = 1026;        ///< 交易添加物品
constexpr std::uint16_t kCmDealDelItem = 1027;        ///< 交易删除物品
constexpr std::uint16_t kCmDealCancel = 1028;         ///< 取消交易
constexpr std::uint16_t kCmDealChangeGold = 1029;     ///< 交易变更金币
constexpr std::uint16_t kCmDealEnd = 1030;            ///< 交易确认结束
constexpr std::uint16_t kCmUserStorageItem = 1031;         ///< 存入仓库
constexpr std::uint16_t kCmUserTakeBackStorageItem = 1032; ///< 从仓库取回
constexpr std::uint16_t kCmTurn = 3010;   ///< 转身
constexpr std::uint16_t kCmWalk = 3011;   ///< 行走
constexpr std::uint16_t kCmSitDown = 3012; ///< 坐下
constexpr std::uint16_t kCmRun = 3013;    ///< 跑步
constexpr std::uint16_t kCmHit = 3014;    ///< 普通攻击
constexpr std::uint16_t kCmHeavyHit = 3015;  ///< 重击
constexpr std::uint16_t kCmBigHit = 3016;    ///< 猛击
constexpr std::uint16_t kCmSpell = 3017;     ///< 施法
constexpr std::uint16_t kCmPowerHit = 3018;  ///< 强力攻击
constexpr std::uint16_t kCmLongHit = 3019;   ///< 长距攻击
constexpr std::uint16_t kCmWideHit = 3024;   ///< 横扫攻击
constexpr std::uint16_t kCmFireHit = 3025;   ///< 火焰攻击
constexpr std::uint16_t kCmSay = 3030;       ///< 说话/聊天
constexpr std::uint16_t kCmCrossHit = 3035;  ///< 十字攻击

// ─── 装备槽位索引 ──────────────────────────────────────────────
constexpr std::size_t kEquipDress = 0;       ///< 衣服
constexpr std::size_t kEquipWeapon = 1;      ///< 武器
constexpr std::size_t kEquipRightHand = 2;   ///< 右手
constexpr std::size_t kEquipNecklace = 3;    ///< 项链
constexpr std::size_t kEquipHelmet = 4;      ///< 头盔
constexpr std::size_t kEquipArmRingLeft = 5; ///< 左手镯
constexpr std::size_t kEquipArmRingRight = 6; ///< 右手镯
constexpr std::size_t kEquipRingLeft = 7;    ///< 左戒指
constexpr std::size_t kEquipRingRight = 8;   ///< 右戒指
constexpr std::size_t kEquipBujuk = 9;       ///< 护身符
constexpr std::size_t kEquipBelt = 10;       ///< 腰带
constexpr std::size_t kEquipBoots = 11;      ///< 靴子
constexpr std::size_t kEquipCharm = 12;      ///< 宝石

// ─── 容量常量 ──────────────────────────────────────────────────
constexpr std::size_t kMaxEquipSlots = 13;       ///< 最大装备槽位数量
constexpr std::size_t kMaxBagItems = 46;         ///< 最大背包物品格数
constexpr std::size_t kMaxUserMagic = 20;        ///< 最大玩家魔法数量
constexpr std::size_t kMaxSaveItems = 50;        ///< 最大保存物品数量
constexpr std::size_t kRuntimeMaxStorageItems = 39; ///< 运行时最大仓库物品数

/**
 * @brief 紧凑打包（1 字节对齐）的数据类型区
 *
 * @note #pragma pack(push, 1) 确保以下结构体的内存布局与 Delphi 服务器完全一致。
 *       任何结构体的尺寸或字段偏移变化都可能导致协议兼容性问题。
 */
#pragma pack(push, 1)

/**
 * @brief 遗留协议的短字符串类型
 *
 * @details Delphi 风格的短字符串，第一个字节为长度，后面紧跟字符数据。
 * 不保证以 null 结尾。
 *
 * @tparam N 字符串的最大长度
 */
template <std::size_t N>
struct LegacyShortString {
  std::uint8_t length{0};            ///< 字符串实际长度
  std::array<char, N> value{};       ///< 字符数据缓冲区
};

/**
 * @brief 遗留协议的默认消息头结构
 *
 * @details 对应 Delphi 中的 TDefaultMessage 结构，
 * 包含消息识别符和四个关联参数。
 */
struct LegacyDefaultMessage {
  std::int32_t recog{0};     ///< 识别码/承载整数值
  std::uint16_t ident{0};    ///< 消息标识符
  std::uint16_t param{0};    ///< 参数
  std::uint16_t tag{0};      ///< 标签
  std::uint16_t series{0};   ///< 序列号/附加参数
};

/**
 * @brief 遗留协议的用户注册信息
 *
 * @details 用于账号注册流程的完整用户信息结构体。
 */
struct LegacyUserEntryInfo {
  LegacyShortString<10> login_id{};     ///< 登录账号
  LegacyShortString<10> password{};     ///< 密码
  LegacyShortString<20> user_name{};    ///< 用户昵称
  LegacyShortString<14> ss_no{};        ///< 身份证号/安全码
  LegacyShortString<14> phone{};        ///< 电话号码
  LegacyShortString<20> quiz{};         ///< 密码提示问题
  LegacyShortString<12> answer{};       ///< 密码提示答案
  LegacyShortString<40> email{};        ///< 电子邮箱
};

/**
 * @brief 遗留协议的用户附加注册信息
 *
 * @details 在 LegacyUserEntryInfo 基础上的扩展信息字段。
 */
struct LegacyUserEntryAddInfo {
  LegacyShortString<20> quiz2{};        ///< 第二密码提示问题
  LegacyShortString<12> answer2{};      ///< 第二密码提示答案
  LegacyShortString<10> birthday{};     ///< 生日
  LegacyShortString<13> mobile_phone{}; ///< 手机号码
  LegacyShortString<20> memo1{};        ///< 备注 1
  LegacyShortString<20> memo2{};        ///< 备注 2
};

/**
 * @brief 遗留协议的双长整数消息体
 *
 * @details 包含四个 32 位整数的消息体结构。
 */
struct LegacyMessageBodyWL {
  std::int32_t lparam1{0};  ///< 长参数 1
  std::int32_t lparam2{0};  ///< 长参数 2
  std::int32_t ltag1{0};    ///< 长标签 1
  std::int32_t ltag2{0};    ///< 长标签 2
};

/**
 * @brief 遗留协议的角色外观描述
 *
 * @details 描述角色的外观特征和状态。
 */
struct LegacyCharDesc {
  std::int32_t feature{0};  ///< 外观特征码
  std::int32_t status{0};   ///< 状态标识
};

struct LegacyShortMessage {
  std::uint16_t ident{0};
  std::uint16_t msg{0};
};

/**
 * @brief 遗留协议的用户物品结构
 *
 * @details 对应到玩家背包/仓库中的具体物品实例。
 * 固定大小 40 字节。
 *
 * @warning static_assert 验证了 sizeof(LegacyUserItem) == 40。
 */
struct LegacyUserItem {
  std::int32_t make_index{0};              ///< 物品制作索引（唯一实例标识）
  std::uint16_t index{0};                  ///< 物品模板索引
  std::uint16_t dura{0};                   ///< 当前耐久度
  std::uint16_t dura_max{0};               ///< 最大耐久度
  std::array<std::uint8_t, 14> desc{};     ///< 描述信息/附加属性
  std::uint8_t color_r{0};                 ///< 颜色 R 分量
  std::uint8_t color_g{0};                 ///< 颜色 G 分量
  std::uint8_t color_b{0};                 ///< 颜色 B 分量
  std::array<char, 13> prefix{};           ///< 物品前缀名
};

/**
 * @brief 遗留协议的角色能力属性
 *
 * @details 包含角色的等级、攻防属性、血量/魔法值、经验值、负重等
 * 完整属性信息。
 */
struct LegacyAbility {
  std::uint8_t level{1};        ///< 等级
  std::uint8_t reserved1{0};    ///< 保留字段
  std::uint16_t ac{0};          ///< 防御力
  std::uint16_t mac{0};         ///< 魔法防御力
  std::uint16_t dc{0};          ///< 攻击力
  std::uint16_t mc{0};          ///< 魔法力
  std::uint16_t sc{0};          ///< 道术力
  std::uint16_t hp{15};         ///< 当前生命值
  std::uint16_t mp{15};         ///< 当前魔法值
  std::uint16_t max_hp{15};     ///< 最大生命值
  std::uint16_t max_mp{15};     ///< 最大魔法值
  std::uint8_t exp_count{0};     ///< 经验计数
  std::uint8_t exp_max_count{0}; ///< 最大经验计数
  std::uint32_t exp{0};          ///< 当前经验值
  std::uint32_t max_exp{100};    ///< 升级所需经验值
  std::uint16_t weight{0};       ///< 当前负重
  std::uint16_t max_weight{30};  ///< 最大负重
  std::uint8_t wear_weight{0};   ///< 当前穿戴重量
  std::uint8_t max_wear_weight{100}; ///< 最大穿戴重量
  std::uint8_t hand_weight{0};   ///< 当前手持重量
  std::uint8_t max_hand_weight{100}; ///< 最大手持重量
};

/**
 * @brief 遗留协议的物品标准定义
 *
 * @details 对应物品模板的完整定义，包含物品的基础属性、
 * 装备需求、特殊效果等。固定大小 76 字节。
 *
 * @warning static_assert 验证了 sizeof(LegacyStdItem) == 76，
 *          以及各关键字段的正确偏移量。
 */
struct LegacyStdItem {
  LegacyShortString<14> name{};           ///< 物品名称
  std::uint8_t std_mode{0};               ///< 物品模式/类型
  std::uint8_t shape{0};                  ///< 物品形状
  std::uint8_t weight{0};                 ///< 物品重量
  std::uint8_t ani_count{0};              ///< 动画帧数
  std::int8_t special_pwr{0};             ///< 特殊威力
  std::uint8_t item_desc{0};              ///< 物品描述标识
  std::uint8_t padding_after_item_desc{0}; ///< 填充字节（对齐用）
  std::uint16_t looks{0};                 ///< 外观索引
  std::uint16_t dura_max{0};              ///< 最大耐久度
  std::uint16_t ac{0};                    ///< 防御力
  std::uint16_t mac{0};                   ///< 魔法防御力
  std::uint16_t dc{0};                    ///< 攻击力
  std::uint16_t mc{0};                    ///< 魔法力
  std::uint16_t sc{0};                    ///< 道术力
  std::uint8_t need{0};                   ///< 需求类型（等级/职业等）
  std::uint8_t need_level{0};             ///< 需求等级
  std::array<std::uint8_t, 2> padding_before_price{}; ///< 填充字节
  std::int32_t price{0};                  ///< 价格
  std::int32_t stock{0};                  ///< 库存数量
  std::uint8_t atk_spd{0};                ///< 攻击速度
  std::uint8_t agility{0};                ///< 敏捷
  std::uint8_t accurate{0};               ///< 准确
  std::uint8_t mg_avoid{0};               ///< 魔法躲避
  std::uint8_t strong{0};                 ///< 强度
  std::uint8_t undead{0};                 ///< 不死系特效
  std::array<std::uint8_t, 2> padding_before_hp_add{}; ///< 填充字节
  std::int32_t hp_add{0};                 ///< 生命值附加
  std::int32_t mp_add{0};                 ///< 魔法值附加
  std::int32_t exp_add{0};                ///< 经验值附加
  std::uint8_t eff_type1{0};              ///< 特殊效果类型 1
  std::uint8_t eff_rate1{0};              ///< 特殊效果概率 1
  std::uint8_t eff_value1{0};             ///< 特殊效果值 1
  std::uint8_t eff_type2{0};              ///< 特殊效果类型 2
  std::uint8_t eff_rate2{0};              ///< 特殊效果概率 2
  std::uint8_t eff_value2{0};             ///< 特殊效果值 2
  std::array<std::uint8_t, 2> padding_tail{}; ///< 尾部填充字节
};

/**
 * @brief 遗留协议的客户端物品展示结构
 *
 * @details 在 LegacyStdItem 基础上附加实例信息的物品结构，
 * 用于向客户端展示物品详细数据（包含实例索引和耐久度）。
 */
struct LegacyClientItem {
  LegacyStdItem item{};        ///< 物品标准定义
  std::int32_t make_index{0};  ///< 物品制作索引
  std::uint16_t dura{0};       ///< 当前耐久度
  std::uint16_t dura_max{0};   ///< 最大耐久度
};

/**
 * @brief 遗留协议的魔法定义结构
 *
 * @details 定义魔法的完整属性，包括标识符、名称、效果、
 * 消耗、等级需求、训练要求等。
 */
struct LegacyDefMagic {
  std::uint16_t magic_id{0};            ///< 魔法 ID
  LegacyShortString<12> magic_name{};   ///< 魔法名称
  std::uint8_t effect_type{0};          ///< 效果类型
  std::uint8_t effect{0};               ///< 效果值
  std::uint16_t spell{0};               ///< 消耗法术值
  std::uint16_t min_power{0};           ///< 最小威力
  std::array<std::uint8_t, 4> need_level{};    ///< 各级所需等级
  std::array<std::int32_t, 4> max_train{};     ///< 各级最大熟练度
  std::uint8_t max_train_level{0};             ///< 最大可修炼等级
  std::uint8_t job{0};                  ///< 职业要求
  std::int32_t delay_time{1000};        ///< 延迟时间（毫秒）
  std::uint8_t def_spell{0};            ///< 默认消耗法术值
  std::uint8_t def_min_power{0};        ///< 默认最小威力
  std::uint16_t max_power{0};           ///< 最大威力
  std::uint8_t def_max_power{0};        ///< 默认最大威力
  LegacyShortString<15> desc{};         ///< 魔法描述
};

/**
 * @brief 遗留协议的已习得魔法信息
 *
 * @details 玩家已学习的魔法在运行时使用的精简信息结构。
 */
struct LegacyUseMagicInfo {
  std::uint16_t magic_id{0};   ///< 魔法 ID
  std::uint8_t level{0};       ///< 当前等级
  char key{0};                 ///< 快捷键
  std::int32_t cur_train{0};   ///< 当前熟练度
};

/**
 * @brief 遗留协议的客户端魔法信息
 *
 * @details 发送给客户端的完整魔法信息，包含快捷键设置、
 * 等级、熟练度和魔法定义数据。
 */
struct LegacyClientMagic {
  char key{0};                 ///< 快捷键
  std::uint8_t level{0};       ///< 等级
  std::int32_t cur_train{0};   ///< 当前熟练度
  LegacyDefMagic def{};        ///< 魔法定义
};

#pragma pack(pop)

// ─── 编译期内存布局校验 ─────────────────────────────────────────
// 以下 static_assert 确保打包后的结构体尺寸和字段偏移量
// 与 Delphi 服务器完全一致，防止因编译器差异导致的协议不兼容。

static_assert(sizeof(LegacyUserItem) == 40);
static_assert(offsetof(LegacyUserItem, make_index) == 0);
static_assert(offsetof(LegacyUserItem, index) == 4);
static_assert(offsetof(LegacyUserItem, dura) == 6);
static_assert(offsetof(LegacyUserItem, dura_max) == 8);
static_assert(offsetof(LegacyUserItem, desc) == 10);
static_assert(offsetof(LegacyUserItem, color_r) == 24);
static_assert(offsetof(LegacyUserItem, color_g) == 25);
static_assert(offsetof(LegacyUserItem, color_b) == 26);
static_assert(offsetof(LegacyUserItem, prefix) == 27);
static_assert(sizeof(LegacyStdItem) == 76);
static_assert(offsetof(LegacyStdItem, name) == 0);
static_assert(offsetof(LegacyStdItem, std_mode) == 15);
static_assert(offsetof(LegacyStdItem, shape) == 16);
static_assert(offsetof(LegacyStdItem, weight) == 17);
static_assert(offsetof(LegacyStdItem, ani_count) == 18);
static_assert(offsetof(LegacyStdItem, special_pwr) == 19);
static_assert(offsetof(LegacyStdItem, item_desc) == 20);
static_assert(offsetof(LegacyStdItem, looks) == 22);
static_assert(offsetof(LegacyStdItem, dura_max) == 24);
static_assert(offsetof(LegacyStdItem, ac) == 26);
static_assert(offsetof(LegacyStdItem, mac) == 28);
static_assert(offsetof(LegacyStdItem, dc) == 30);
static_assert(offsetof(LegacyStdItem, mc) == 32);
static_assert(offsetof(LegacyStdItem, sc) == 34);
static_assert(offsetof(LegacyStdItem, need) == 36);
static_assert(offsetof(LegacyStdItem, need_level) == 37);
static_assert(offsetof(LegacyStdItem, price) == 40);
static_assert(offsetof(LegacyStdItem, stock) == 44);
static_assert(offsetof(LegacyStdItem, atk_spd) == 48);
static_assert(offsetof(LegacyStdItem, undead) == 53);
static_assert(offsetof(LegacyStdItem, hp_add) == 56);
static_assert(offsetof(LegacyStdItem, mp_add) == 60);
static_assert(offsetof(LegacyStdItem, exp_add) == 64);
static_assert(offsetof(LegacyStdItem, eff_type1) == 68);
static_assert(offsetof(LegacyStdItem, eff_value2) == 73);
static_assert(sizeof(LegacyClientItem) == 84);
static_assert(offsetof(LegacyClientItem, item) == 0);
static_assert(offsetof(LegacyClientItem, make_index) == 76);
static_assert(offsetof(LegacyClientItem, dura) == 80);
static_assert(offsetof(LegacyClientItem, dura_max) == 82);

/**
 * @brief 设置遗留短字符串的值
 *
 * @details 将 std::string 的内容复制到 LegacyShortString 中。
 * 如果源字符串超过 N 字节，则截断至 N 字节。
 * 未使用的字符位置被填充为 '\0'。
 *
 * @tparam N 短字符串的最大长度
 * @param[out] target 目标短字符串结构
 * @param value 源字符串
 */
template <std::size_t N>
inline void set_short_string(LegacyShortString<N>& target, const std::string& value) {
  target.length = static_cast<std::uint8_t>(std::min<std::size_t>(N, value.size()));
  std::fill(target.value.begin(), target.value.end(), '\0');
  std::memcpy(target.value.data(), value.data(), target.length);
}

/**
 * @brief 将遗留短字符串转换为 std::string
 *
 * @details 从 LegacyShortString 中提取有效字符数据构造 std::string。
 *
 * @tparam N 短字符串的最大长度
 * @param value 源短字符串结构
 * @return std::string 转换后的标准字符串
 */
template <std::size_t N>
[[nodiscard]] inline std::string to_string(const LegacyShortString<N>& value) {
  return std::string(value.value.data(), value.value.data() + value.length);
}

/**
 * @brief 将两个 8 位值合并为一个 16 位值
 *
 * @details 构造：result = low | (high << 8)
 *
 * @param low 低 8 位
 * @param high 高 8 位
 * @return std::uint16_t 合并后的 16 位值
 */
inline std::uint16_t make_word(std::uint8_t low, std::uint8_t high) {
  return static_cast<std::uint16_t>(low) | (static_cast<std::uint16_t>(high) << 8);
}

/**
 * @brief 将两个 16 位值合并为一个 32 位值
 *
 * @details 构造：result = low | (high << 16)
 *
 * @param low 低 16 位
 * @param high 高 16 位
 * @return std::int32_t 合并后的 32 位值
 */
inline std::int32_t make_long(std::uint16_t low, std::uint16_t high) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(low) |
                                   (static_cast<std::uint32_t>(high) << 16));
}

/**
 * @brief 构造角色外观特征码
 *
 * @details 将种族、衣服、武器、头型四个属性编码为一个 32 位特征值。
 *
 * @param race 种族
 * @param dress 衣服样式
 * @param weapon 武器样式
 * @param face 头型/面部
 * @return std::int32_t 合并后的特征码
 */
inline std::int32_t make_feature(std::uint8_t race, std::uint8_t dress, std::uint8_t weapon,
                                 std::uint8_t face) {
  return make_long(make_word(race, weapon), make_word(face, dress));
}

/**
 * @brief 提取 32 位值的低 16 位
 *
 * @param value 32 位输入值
 * @return std::uint16_t 低 16 位
 */
inline std::uint16_t low_word(std::int32_t value) {
  return static_cast<std::uint16_t>(static_cast<std::uint32_t>(value) & 0xffffu);
}

/**
 * @brief 提取 32 位值的高 16 位
 *
 * @param value 32 位输入值
 * @return std::uint16_t 高 16 位
 */
inline std::uint16_t high_word(std::int32_t value) {
  return static_cast<std::uint16_t>((static_cast<std::uint32_t>(value) >> 16) & 0xffffu);
}

/**
 * @brief 检查 LegacyUserItem 是否为空
 *
 * @details 通过检查 index 字段是否为 0 来判断物品槽位是否为空。
 *
 * @param item 用户物品实例
 * @return true 如果该物品为空（index == 0）
 */
inline bool is_empty(const LegacyUserItem& item) { return item.index == 0; }

/**
 * @brief 检查 LegacyUseMagicInfo 是否为空
 *
 * @details 通过检查 magic_id 字段是否为 0 来判断魔法槽位是否为空。
 *
 * @param magic 已习得魔法信息
 * @return true 如果该魔法为空（magic_id == 0）
 */
inline bool is_empty(const LegacyUseMagicInfo& magic) { return magic.magic_id == 0; }

}  // namespace mir2
