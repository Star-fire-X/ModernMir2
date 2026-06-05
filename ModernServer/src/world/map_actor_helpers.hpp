#pragma once

/**
 * @file map_actor_helpers.hpp
 * @brief 地图actor实现细节的头文件，被包含在 map_actor.cpp 的 mir2 命名空间内部
 * @details 该文件是 map_actor.cpp 的实现辅助头文件，定义了一个匿名命名空间，
 *          其中包含游戏机制常量、怪物种族ID、AI行为枚举、玩家状态辅助函数、
 *          NPC服务辅助函数、物品装备辅助函数以及行会/城堡相关的对话框构建和执行函数。
 *          所有内容都在匿名命名空间中，因此仅限于当前翻译单元可见。
 * @note 该文件不应被单独包含，仅供 map_actor.cpp 使用
 */
namespace {

// ============================================================================
// @name 游戏机制常量
// @brief 定义游戏核心机制中使用的各种常量值
// ============================================================================

/** @brief 默认名称颜色索引（255 = 白色） */
constexpr std::uint8_t kDefaultNameColor = 255;
/** @brief 默认聊天文字颜色索引 */
constexpr std::uint8_t kDefaultChatColor = 255;
/** @brief 默认聊天文字阴影索引 */
constexpr std::uint8_t kDefaultChatShadow = 0;
/** @brief 默认商人NPC脸部图像索引（0 = 无特殊脸部） */
constexpr std::int32_t kDefaultMerchantFace = 0;
/** @brief 跨地图同步最大重试次数 */
constexpr std::uint8_t kCrossMapSyncRetryLimit = 100;
/** @brief 传统视图范围（单位：格数），12格为传奇标准视野距离 */
constexpr std::int32_t kLegacyViewRange = 12;
/** @brief 区域类型：战斗区（PK区域） */
constexpr std::int32_t kAreaFight = 1;
/** @brief 区域类型：安全区（禁止PK） */
constexpr std::int32_t kAreaSafe = 2;
/** @brief 区域类型：自由PK区（无惩罚） */
constexpr std::int32_t kAreaFreePk = 4;
/** @brief 攻击模式：全体攻击（攻击所有可见目标） */
constexpr std::uint8_t kHamAll = 0;
/** @brief 攻击模式：和平模式（不攻击任何玩家） */
constexpr std::uint8_t kHamPeace = 1;
/** @brief 攻击模式：组队模式（只攻击非队友目标） */
constexpr std::uint8_t kHamGroup = 2;
/** @brief 攻击模式：行会模式（只攻击非行会成员） */
constexpr std::uint8_t kHamGuild = 3;
/** @brief 攻击模式：PK攻击模式（只攻击红名玩家） */
constexpr std::uint8_t kHamPkAttack = 4;
/** @brief 地图切换保护时间（毫秒），玩家换图后3秒内不可被攻击 */
constexpr std::uint64_t kMapChangeProtectMs = 3000;
/** @brief 玩家尸体保留时间（毫秒），3分钟后消失 */
constexpr std::uint64_t kPlayerCorpseMs = 180000;
/** @brief 怪物尸体保留时间（毫秒），3分钟后消失 */
constexpr std::uint64_t kMonsterCorpseMs = 180000;
/** @brief 传统掉落的归属权时间（毫秒），2分钟内只有击杀者可拾取 */
constexpr std::uint64_t kLegacyDropOwnerMs = 120000;
/** @brief 地面物品自动消失时间（毫秒），1小时后刷新消失 */
constexpr std::uint64_t kLegacyGroundItemExpireMs = 60ULL * 60ULL * 1000ULL;
/** @brief 武器升级有效期（毫秒），3天后未取回武器将被丢弃 */
constexpr std::uint64_t kLegacyWeaponUpgradeExpireMs = 3ULL * 24ULL * 60ULL * 60ULL * 1000ULL;
/** @brief 怪物金币掉落单堆最大数量，每堆最多2000金币 */
constexpr std::int32_t kLegacyMonsterGoldDropChunk = 2000;
/** @brief 怪物金币掉落最大堆数，最多分为17堆 */
constexpr std::int32_t kLegacyMonsterGoldDropMaxChunks = 17;
/** @brief 城门自动关闭时间（毫秒），5秒后自动关闭 */
constexpr std::uint64_t kDoorAutoCloseMs = 5000;
/** @brief 静态城门对象ID基地址，高64位标记区分对象类型 */
constexpr std::uint64_t kStaticGateObjectBase = 0x7000000000000000ULL;
/** @brief 地图任务NPC对象ID基地址 */
constexpr std::uint64_t kMapQuestNpcObjectBase = 0x7100000000000000ULL;
/** @brief 启动时任务NPC对象ID（预留ID） */
constexpr std::uint64_t kStartupQuestNpcObjectId = 0x71ffff0000000000ULL;
/** @brief NPC对话框每页显示条目数 */
constexpr std::size_t kNpcDialogPageSize = 6;

// ============================================================================
// @name 中毒系统常量
// ============================================================================

/** @brief 中毒类型：扣血，中毒后持续减少HP */
constexpr std::int32_t kPoisonDecHealth = 0;
/** @brief 中毒类型：石化（蜘蛛网效果），Race=5 */
constexpr std::int32_t kLegacyPoisonStone = 5;
// ============================================================================
// @name 怪物种族ID常量（Race Server ID）
// @brief 传统传奇服务端使用的怪物种族ID，决定怪物AI行为、攻击方式和外观
// @note 种族ID是传奇服务端中决定怪物行为逻辑的关键字段，不同的种族ID对应
//       不同的AI攻击模式、移动方式和特殊技能
// ============================================================================

/** @brief 种族ID：城门守卫（固定位置守卫） */
constexpr std::int32_t kRcDoorGuard = 11;
/** @brief 种族ID：弓箭警卫（远程弓箭攻击） */
constexpr std::int32_t kRcArcherPolice = 20;
/** @brief 种族ID：狼 */
constexpr std::int32_t kRcWolf = 53;
/** @brief 种族ID：普通怪物（基础AI） */
constexpr std::int32_t kRcMonster = 80;
/** @brief 种族ID：奥玛战士（Oma） */
constexpr std::int32_t kRcOma = 81;
/** @brief 种族ID：吐网蜘蛛（喷吐蛛网限制玩家移动） */
constexpr std::int32_t kRcSpitSpider = 82;
/** @brief 种族ID：减速怪物（攻击附带减速效果） */
constexpr std::int32_t kRcSlowMonster = 83;
/** @brief 种族ID：杀人草（伪装成地面物品的伏击型怪物） */
constexpr std::int32_t kRcKillingHerb = 85;
/** @brief 种族ID：普通骷髅 */
constexpr std::int32_t kRcSkeleton = 86;
/** @brief 种族ID：双斧骷髅（双手武器攻击） */
constexpr std::int32_t kRcDualAxeSkeleton = 87;
/** @brief 种族ID：重斧骷髅（重型武器攻击） */
constexpr std::int32_t kRcHeavyAxeSkeleton = 88;
/** @brief 种族ID：骷髅骑士（骑乘骷髅） */
constexpr std::int32_t kRcKnightSkeleton = 89;
/** @brief 种族ID：大角虫（Kudeki，喷毒气攻击） */
constexpr std::int32_t kRcBigKudeki = 90;
/** @brief 种族ID：牛魔法师（远程魔法攻击） */
constexpr std::int32_t kRcMagCowFaceMon = 91;
/** @brief 种族ID：黑暗荆棘（ThornDark，远程投掷攻击） */
constexpr std::int32_t kRcThornDark = 93;
/** @brief 种族ID：掘地僵尸（从地下钻出攻击） */
constexpr std::int32_t kRcDigOutZombi = 95;
/** @brief 种族ID：石像王（Sculture King，带随从的BOSS） */
constexpr std::int32_t kRcScultureKing = 102;
/** @brief 种族ID：蜂王（Bee Queen，可召唤小蜜蜂） */
constexpr std::int32_t kRcBeeQueen = 103;
/** @brief 种族ID：弓箭手怪物（远程射击） */
constexpr std::int32_t kRcArcherMon = 104;
/** @brief 种族ID：毒蛾（GasMoth，释放毒气） */
constexpr std::int32_t kRcGasMoth = 105;
/** @brief 种族ID：粪虫（GasDung，释放毒气） */
constexpr std::int32_t kRcGasDung = 106;
/** @brief 种族ID：蜈蚣王（Centipede King，BOSS级蜈蚣） */
constexpr std::int32_t kRcCentipedeKing = 107;
/** @brief 种族ID：城堡城门 */
constexpr std::int32_t kRcCastleDoor = 110;
/** @brief 种族ID：城墙（不可移动不可攻击的建筑） */
constexpr std::int32_t kRcWall = 111;
/** @brief 种族ID：弓箭守卫（城堡弓箭守卫） */
constexpr std::int32_t kRcArcherGuard = 112;
/** @brief 种族ID：蜘蛛巢穴（可召唤小蜘蛛） */
constexpr std::int32_t kRcSpiderHouse = 116;
/** @brief 种族ID：高危蜘蛛 */
constexpr std::int32_t kRcHighRiskSpider = 118;
/** @brief 种族ID：剧毒蜘蛛 */
constexpr std::int32_t kRcBigPoisonSpider = 119;
/** @brief 种族ID：石像王（无随从版本） */
constexpr std::int32_t kRcScultureKingNoFollower = 122;
/** @brief 种族ID：贵族猪王 */
constexpr std::int32_t kRcNoblePigKing = 124;
/** @brief 种族ID：剧毒幽灵（释放剧毒气体） */
constexpr std::int32_t kRcToxicGhost = 127;

// ============================================================================
// @name 怪物种族行为枚举
// @brief 定义怪物的AI行为类型，根据种族ID映射到具体的行为模式
// ============================================================================

/**
 * @enum LegacyMonsterRaceBehavior
 * @brief 怪物种族行为枚举，描述怪物的AI攻击模式
 * @details 该枚举根据怪物的种族服务器ID（race_server）将怪物分类为不同的
 *          行为模式，包括普通攻击、喷吐、毒气、魔法、投掷、伪装、钻地、召唤等
 */
enum class LegacyMonsterRaceBehavior {
  normal,         ///< 普通行为：基础近战AI，无特殊技能
  spit,           ///< 喷吐行为：蜘蛛类怪物，喷吐蛛网限制目标移动
  front_gas,      ///< 正面毒气：朝面前方向释放毒气攻击
  front_magic,    ///< 正面魔法：朝面前方向释放魔法攻击
  fly_axe,        ///< 投掷飞斧：远程投掷武器攻击目标
  stick_hide,     ///< 伪装隐藏：伪装成地面物品，靠近后突然攻击（如杀人草）
  digout_zombi,   ///< 掘地行为：从地下钻出偷袭目标
  centipede,      ///< 蜈蚣行为：BOSS级蜈蚣的特殊攻击模式
  summoner,       ///< 召唤行为：可召唤其他怪物助战（如蜂王、蜘蛛巢穴）
  sculture_king,  ///< 石像王行为：BOSS级石像王的特殊AI，可能召唤随从
  guard,          ///< 守卫行为：固定位置守卫，不主动追击但攻击入侵者
  structure       ///< 建筑行为：静态建筑（城门、城墙），不可移动
};

/**
 * @brief 根据种族服务器ID获取怪物的行为类型
 * @details 将种族ID映射到 LegacyMonsterRaceBehavior 枚举，决定怪物的AI攻击模式。
 *          这是传奇服务端怪物行为系统的核心映射函数，根据 race_server 值将怪物
 *          归类为普通、喷吐、毒气、魔法、投掷、伪装、钻地、召唤、石像王、守卫或建筑行为。
 * @param race_server 怪物种族服务器ID（对应 Mir2 服务端 Monster DB 的 Race 字段）
 * @return 对应的 LegacyMonsterRaceBehavior 枚举值，默认为 normal
 * @see LegacyMonsterRaceBehavior
 */
LegacyMonsterRaceBehavior legacy_monster_race_behavior(std::int32_t race_server) {
  switch (race_server) {
    case kRcSpitSpider:
    case kRcHighRiskSpider:
    case kRcBigPoisonSpider:
      return LegacyMonsterRaceBehavior::spit;
    case kRcBigKudeki:
    case kRcGasMoth:
    case kRcGasDung:
    case kRcToxicGhost:
      return LegacyMonsterRaceBehavior::front_gas;
    case kRcMagCowFaceMon:
      return LegacyMonsterRaceBehavior::front_magic;
    case kRcDualAxeSkeleton:
    case kRcThornDark:
    case kRcArcherMon:
      return LegacyMonsterRaceBehavior::fly_axe;
    case kRcKillingHerb:
      return LegacyMonsterRaceBehavior::stick_hide;
    case kRcDigOutZombi:
      return LegacyMonsterRaceBehavior::digout_zombi;
    case kRcCentipedeKing:
      return LegacyMonsterRaceBehavior::centipede;
    case kRcBeeQueen:
    case kRcSpiderHouse:
      return LegacyMonsterRaceBehavior::summoner;
    case kRcScultureKing:
    case kRcScultureKingNoFollower:
      return LegacyMonsterRaceBehavior::sculture_king;
    case kRcDoorGuard:
    case kRcArcherGuard:
    case kRcArcherPolice:
      return LegacyMonsterRaceBehavior::guard;
    case kRcCastleDoor:
    case kRcWall:
      return LegacyMonsterRaceBehavior::structure;
    default:
      return LegacyMonsterRaceBehavior::normal;
  }
}

/**
 * @brief 判断怪物是否具有特殊行为（非普通AI）
 * @details 快速检查怪物种族ID是否映射到特殊行为类型，用于决定是否需要
 *          执行特殊的AI逻辑处理。
 * @param race_server 怪物种族服务器ID
 * @return true 如果怪物具有特殊行为（喷吐、毒气、魔法等），false 如果只是普通怪物
 * @see legacy_monster_race_behavior
 */
bool legacy_monster_has_special_behavior(std::int32_t race_server) {
  return legacy_monster_race_behavior(race_server) != LegacyMonsterRaceBehavior::normal;
}

/**
 * @brief 传统喷吐攻击范围映射表
 * @details 8个方向的5x5范围喷吐攻击模板。第一维表示8个方向（0-7），
 *          第二维和第三维表示5x5的攻击范围网格，值为1表示该格被攻击覆盖。
 *          用于蜘蛛类怪物的蛛网喷吐和毒气攻击的命中判定。
 * @note 方向编码：0=上, 1=右上, 2=右, 3=右下, 4=下, 5=左下, 6=左, 7=左上
 *       网格中心(2,2)为怪物所在位置，1表示攻击覆盖的格子
 */
constexpr std::array<std::array<std::array<std::uint8_t, 5>, 5>, 8> kLegacySpitMap{{
    {{{0, 0, 1, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
    {{{0, 0, 0, 0, 1}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 1, 1}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 1}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 1, 0, 0}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {1, 0, 0, 0, 0}}},
    {{{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {1, 1, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
    {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}}},
}};

// ============================================================================
// @name 行会头衔页面定义
// ============================================================================

/**
 * @struct GuildTitlePage
 * @brief 行会头衔页面，包含一页头衔分类标签和对应的头衔列表
 */
struct GuildTitlePage {
  std::string_view label;            ///< 页面标签（如 "Core Roles", "Field Roles"）
  std::array<std::string_view, 3> titles;  ///< 该页包含的3个头衔名称
};

/**
 * @brief 行会头衔分页数据，每页包含一个分类标签和3个头衔选项
 * @details 共2页：第1页为核心角色（Member, Deputy, Elder），
 *          第2页为战地角色（Vanguard, Scout, Quartermaster）
 */
constexpr std::array<GuildTitlePage, 2> kGuildTitlePages{{
    {"Core Roles", {"Member", "Deputy", "Elder"}},
    {"Field Roles", {"Vanguard", "Scout", "Quartermaster"}},
}};

// ============================================================================
// @name 前向声明（在其他文件中实现）
// ============================================================================

/**
 * @brief 计算商人收购物品的价格
 * @param item 玩家出售的物品
 * @param item_configs 物品配置表
 * @param price_rate_percent 价格百分比系数，默认100
 * @return 收购价格
 */
std::int32_t compute_merchant_sell_price(
    const LegacyUserItem& item, const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
    std::int32_t price_rate_percent = 100);

/**
 * @brief 创建确认应答包
 * @param session_id 会话ID
 * @param ok 是否成功
 * @return 确认应答包
 */
LegacyPacket make_ack_packet(std::uint64_t session_id, bool ok);

/**
 * @brief 创建移动失败通知包
 * @param session_id 会话ID
 * @param object 失败的对象
 * @return 移动失败包
 */
LegacyPacket make_move_fail_packet(std::uint64_t session_id, const GameObject& object);

/**
 * @brief 创建系统通知消息包
 * @param session_id 会话ID
 * @param message 通知消息内容
 * @return 系统通知包
 */
LegacyPacket make_system_notice_packet(std::uint64_t session_id, const std::string& message);

/**
 * @brief 判断对象是否为不死系（亡灵）生物
 * @param object 游戏对象
 * @return true 如果是不死系生物
 */
bool actor_undead(const GameObject& object);

/**
 * @brief 获取对象的魔法防御范围（最小、最大）
 * @param object 游戏对象
 * @return 魔法防御的最小值和最大值
 */
std::pair<std::int32_t, std::int32_t> actor_magic_defense_range(const GameObject& object);

// ============================================================================
// @name 对话框数据结构
// @brief 用于NPC对话框和行会/城堡管理对话框的数据结构
// ============================================================================

/**
 * @struct MerchantDialogEntry
 * @brief 商人对话框条目，包含显示标签和触发动作
 */
struct MerchantDialogEntry {
  std::string label;   ///< 按钮显示文本（如 "Buy", "Sell"）
  std::string action;  ///< 点击后触发的动作命令（如 "@buy", "@sell"）
};

/**
 * @struct GuildMemberDialogTarget
 * @brief 行会成员管理目标，指定页面和成员名称
 */
struct GuildMemberDialogTarget {
  std::size_t page{1};          ///< 成员列表当前页码
  std::string member_name{};    ///< 目标成员名称
};

/**
 * @struct GuildMemberTitleDialogTarget
 * @brief 行会成员头衔设置目标，指定成员页面、头衔页面和成员名称
 */
struct GuildMemberTitleDialogTarget {
  std::size_t member_page{1};   ///< 成员列表页码
  std::size_t title_page{1};    ///< 头衔页面页码
  std::string member_name{};    ///< 目标成员名称
};

/**
 * @struct GuildApplicantDialogTarget
 * @brief 行会申请者管理目标，指定页面和申请者名称
 */
struct GuildApplicantDialogTarget {
  std::size_t page{1};             ///< 申请者列表当前页码
  std::string applicant_name{};    ///< 申请者角色名称
};

/**
 * @struct GuildBrowseTarget
 * @brief 行会浏览目标，来源类型、页码和行会名称
 */
struct GuildBrowseTarget {
  std::string source{"directory"};  ///< 浏览来源（"directory"/"applications"/"castle_show"等）
  std::size_t page{1};              ///< 来源列表页码
  std::string guild_name{};         ///< 目标行会名称
};

/**
 * @struct GuildBrowseListTarget
 * @brief 行会浏览列表目标，包含浏览页和列表页的双层分页信息
 */
struct GuildBrowseListTarget {
  std::string source{"directory"};    ///< 浏览来源
  std::size_t browse_page{1};         ///< 浏览页面页码
  std::size_t list_page{1};           ///< 列表页面页码
  std::string guild_name{};           ///< 目标行会名称
};

/**
 * @struct GuildTitleConfirmTarget
 * @brief 行会头衔确认目标，包含完整的成员和头衔信息
 */
struct GuildTitleConfirmTarget {
  std::size_t member_page{1};  ///< 成员列表页码
  std::size_t title_page{1};   ///< 头衔页面页码
  std::string member_name{};   ///< 目标成员名称
  std::string title_name{};    ///< 要设置的头衔名称
};

/**
 * @struct CastleWarConfirmTarget
 * @brief 城堡战争确认目标，包含页码和目标行会名称
 */
struct CastleWarConfirmTarget {
  std::size_t page{1};         ///< 目标列表页码
  std::string guild_name{};    ///< 宣战目标行会名称
};

/**
 * @struct CastleGuildBrowseTarget
 * @brief 城堡行会浏览目标，来源类型、页码和行会名称
 */
struct CastleGuildBrowseTarget {
  std::string source{"wars"};  ///< 查看来源（"wars"/"targets"）
  std::size_t page{1};         ///< 来源列表页码
  std::string guild_name{};    ///< 目标行会名称
};

/**
 * @struct CastleActionResult
 * @brief 城堡操作执行结果，包含处理状态、成功标志、摘要和详细信息列表
 */
struct CastleActionResult {
  bool handled{false};                  ///< 是否已处理
  bool success{false};                  ///< 操作是否成功
  std::string summary{};                ///< 结果摘要
  std::vector<std::string> details{};   ///< 详细信息列表
};

/**
 * @struct GuildActionResult
 * @brief 行会操作执行结果，包含处理状态、状态描述、摘要和详细信息列表
 */
struct GuildActionResult {
  bool handled{false};                  ///< 是否已处理
  std::string status{"Failed"};         ///< 操作状态（"Success"/"Failed"/"Pending"）
  std::string summary{};                ///< 结果摘要
  std::vector<std::string> details{};   ///< 详细信息列表
};

// ============================================================================
// @name 基础工具函数
// ============================================================================

/**
 * @brief 解析字符串为 int32 整数
 * @param text 待解析的字符串
 * @return 如果解析成功返回整数值，否则返回 std::nullopt
 */
std::optional<std::int32_t> parse_int32(std::string_view text);

/**
 * @brief 将字符串列表从指定起始位置连接为一个字符串
 * @param tokens 字符串列表
 * @param start_index 起始索引
 * @param separator 分隔符，默认为空格
 * @return 连接后的字符串
 */
std::string join_tokens(const std::vector<std::string>& tokens, std::size_t start_index,
                        std::string_view separator = " ");

// ============================================================================
// @name 行会浏览辅助函数
// ============================================================================

/**
 * @brief 规范化行会浏览来源字符串
 * @details 将来源字符串转换为标准形式，非法来源默认返回 "directory"
 * @param source 原始来源字符串
 * @return 规范化后的来源字符串
 */
std::string normalize_guild_browse_source(std::string_view source) {
  const auto lowered = util::lower_copy(std::string(source));
  if (lowered == "applications" || lowered == "castle_show" || lowered == "castle_wars" ||
      lowered == "castle_targets") {
    return lowered;
  }
  return "directory";
}

/**
 * @brief 构建行会浏览的返回动作命令
 * @details 根据来源类型和页码构建对应的 @guild_xxx 返回命令
 * @param source 浏览来源（"directory"/"applications"/"castle_show"/"castle_wars"/"castle_targets"）
 * @param page 当前页码
 * @param guild_name 行会名称（可选，用于城堡战争浏览）
 * @return 返回动作命令字符串
 */
std::string build_guild_browse_back_action(std::string_view source, std::size_t page,
                                           std::string_view guild_name = {}) {
  if (source == "applications") {
    return "@guild_my_applications " + std::to_string(static_cast<int>(page));
  }
  if (source == "castle_show") {
    return "@castle_show";
  }
  if (source == "castle_wars") {
    return "@castle_guild_browse wars " + std::to_string(static_cast<int>(page)) + " " +
           std::string(guild_name);
  }
  if (source == "castle_targets") {
    return "@castle_guild_browse targets " + std::to_string(static_cast<int>(page)) + " " +
           std::string(guild_name);
  }
  return "@guild_directory " + std::to_string(static_cast<int>(page));
}

/**
 * @brief 构建行会浏览列表的返回动作命令
 * @details 用于行会成员列表/申请者列表的返回按钮，根据来源类型分发到
 *          build_guild_browse_back_action 或者构建 @guild_browse 返回命令
 * @param source 浏览来源
 * @param browse_page 浏览页码
 * @param guild_name 行会名称
 * @return 返回动作命令字符串
 */
std::string build_guild_browse_list_back_action(std::string_view source, std::size_t browse_page,
                                                std::string_view guild_name) {
  if (source == "applications" || source == "directory") {
    return "@guild_browse " + std::string(source) + " " +
           std::to_string(static_cast<int>(browse_page)) + " " + std::string(guild_name);
  }
  return build_guild_browse_back_action(source, browse_page, guild_name);
}

/**
 * @brief 汇总名称列表，生成预览文本
 * @details 取前 preview_count 个名称用逗号连接，超出部分显示 "+N more"
 * @param names 名称列表
 * @param preview_count 预览数量，默认3个
 * @return 汇总文本字符串，空列表返回 "None"
 */
std::string summarize_name_list(const std::vector<std::string>& names, std::size_t preview_count = 3) {
  if (names.empty()) {
    return "None";
  }

  const auto count = std::min<std::size_t>(names.size(), preview_count);
  std::vector<std::string> preview;
  preview.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    preview.push_back(names[index]);
  }

  auto summary = join_tokens(preview, 0, ", ");
  if (names.size() > count) {
    summary += " +" + std::to_string(static_cast<int>(names.size() - count)) + " more";
  }
  return summary;
}

// ============================================================================
// @name 对象创建和类型转换
// ============================================================================

/**
 * @brief 根据 ActorMail 创建对应的 GameObject 子类对象
 * @details 根据邮件类型（spawn_player / spawn_monster / spawn_npc）创建
 *          对应的 Player、Monster、Npc 对象，默认返回 EventObject
 * @param mail ActorMail 邮件数据，包含所有创建参数
 * @return 创建的 GameObject 唯一指针
 */
std::unique_ptr<GameObject> make_object(const ActorMail& mail) {
  switch (mail.kind) {
    case ActorMailKind::spawn_player:
      return std::make_unique<Player>(mail.actor_id, mail.session_id, mail.character);
    case ActorMailKind::spawn_monster:
      return std::make_unique<Monster>(mail.actor_id, mail.name, mail.map_id, mail.x, mail.y,
                                       mail.level, mail.max_hp, mail.attack_power, mail.dc_min,
                                       mail.dc_max, mail.defense, mail.magic_defense, mail.mc,
                                       mail.sc, mail.exp_reward, mail.life_attrib, mail.max_mp,
                                       mail.race_server, mail.race_image,
                                       mail.appearance, mail.cool_eye, mail.speed, mail.accuracy,
                                       mail.walk_speed_ms, mail.walk_step, mail.walk_wait_ms,
                                       mail.attack_speed_ms, mail.monster_ai_profile,
                                       mail.monster_search_rate_ms,
                                       mail.home_x, mail.home_y, mail.home_area,
                                       mail.legacy_spawn_group, mail.master_actor_id,
                                       mail.monster_is_slave, mail.slave_exp,
                                       mail.slave_make_level, mail.slave_exp_level,
                                       mail.master_royalty_time_ms, mail.slave_life_time_ms,
                                       mail.monster_no_item, mail.monster_tameable,
                                       mail.monster_drop_items,
                                       mail.monster_drop_gold);
    case ActorMailKind::spawn_npc:
      return std::make_unique<Npc>(mail.actor_id, mail.name, mail.map_id, mail.x, mail.y,
                                   mail.npc_service, mail.merchant_items,
                                   mail.npc_dialog_sections, mail.npc_price_rate_percent,
                                   mail.merchant_key, mail.merchant_products,
                                   mail.merchant_prices, mail.legacy_deal_std_modes,
                                   mail.weapon_upgrades);
    default:
      return std::make_unique<EventObject>(mail.actor_id, mail.name, mail.map_id, mail.x, mail.y);
  }
}

/**
 * @brief 将 GameObject 向下转型为 Player 指针（非常量版本）
 * @param object 游戏对象指针
 * @return 转型后的 Player 指针，非 Player 对象返回 nullptr
 */
Player* as_player(GameObject* object) { return dynamic_cast<Player*>(object); }

/**
 * @brief 将 GameObject 向下转型为 Player 指针（常量版本）
 * @param object 游戏对象常量指针
 * @return 转型后的常量 Player 指针，非 Player 对象返回 nullptr
 */
const Player* as_player(const GameObject* object) { return dynamic_cast<const Player*>(object); }

/**
 * @brief 将 GameObject 向下转型为 Npc 指针（非常量版本）
 * @param object 游戏对象指针
 * @return 转型后的 Npc 指针，非 Npc 对象返回 nullptr
 */
Npc* as_npc(GameObject* object) { return dynamic_cast<Npc*>(object); }

/**
 * @brief 将 GameObject 向下转型为 Npc 指针（常量版本）
 * @param object 游戏对象常量指针
 * @return 转型后的常量 Npc 指针，非 Npc 对象返回 nullptr
 */
const Npc* as_npc(const GameObject* object) { return dynamic_cast<const Npc*>(object); }

/**
 * @brief 将 GameObject 向下转型为 Monster 指针（非常量版本）
 * @param object 游戏对象指针
 * @return 转型后的 Monster 指针，非 Monster 对象返回 nullptr
 */
Monster* as_monster(GameObject* object) { return dynamic_cast<Monster*>(object); }

/**
 * @brief 将 GameObject 向下转型为 Monster 指针（常量版本）
 * @param object 游戏对象常量指针
 * @return 转型后的常量 Monster 指针，非 Monster 对象返回 nullptr
 */
const Monster* as_monster(const GameObject* object) { return dynamic_cast<const Monster*>(object); }

// ============================================================================
// @name 玩家命令分类
// ============================================================================

/**
 * @brief 判断是否为传统的玩家操作命令
 * @details 识别玩家发出的各类操作指令，包括移动、攻击、施法、聊天、
 *          NPC交互、物品操作、交易等
 * @param kind ActorMail 的类型
 * @return true 如果是玩家操作命令
 */
bool is_legacy_player_command(ActorMailKind kind) {
  switch (kind) {
    case ActorMailKind::turn:
    case ActorMailKind::move:
    case ActorMailKind::run:
    case ActorMailKind::attack:
    case ActorMailKind::spell:
    case ActorMailKind::say:
    case ActorMailKind::click_npc:
    case ActorMailKind::merchant_select:
    case ActorMailKind::query_username:
    case ActorMailKind::query_bag_items:
    case ActorMailKind::query_storage_items:
    case ActorMailKind::query_detail_goods:
    case ActorMailKind::query_sell_price:
    case ActorMailKind::query_repair_cost:
    case ActorMailKind::drop_item:
    case ActorMailKind::pickup_item:
    case ActorMailKind::open_door:
    case ActorMailKind::take_on_item:
    case ActorMailKind::take_off_item:
    case ActorMailKind::eat_item:
    case ActorMailKind::drop_gold:
    case ActorMailKind::revive:
    case ActorMailKind::buy_item:
    case ActorMailKind::sell_item:
    case ActorMailKind::repair_item:
    case ActorMailKind::storage_item:
    case ActorMailKind::take_back_storage_item:
    case ActorMailKind::trade_try:
    case ActorMailKind::trade_cancel:
    case ActorMailKind::trade_add_item:
    case ActorMailKind::trade_remove_item:
    case ActorMailKind::trade_set_gold:
    case ActorMailKind::trade_accept:
      return true;
    default:
      return false;
  }
}

/**
 * @brief 判断是否为需要响应补偿的传统命令
 * @details 响应补偿命令指那些客户端需要收到服务器响应包后才能继续操作的命令。
 *          包括转身、移动、跑步、攻击、施法和复活。
 *          这些命令执行后必须及时向客户端发送应答包，否则客户端会卡住。
 * @param kind ActorMail 的类型
 * @return true 如果该命令需要响应补偿
 */
bool is_legacy_response_compensated_command(ActorMailKind kind) {
  switch (kind) {
    case ActorMailKind::turn:
    case ActorMailKind::move:
    case ActorMailKind::run:
    case ActorMailKind::attack:
    case ActorMailKind::spell:
    case ActorMailKind::say:
    case ActorMailKind::click_npc:
    case ActorMailKind::merchant_select:
    case ActorMailKind::eat_item:
    case ActorMailKind::revive:
      return true;
    default:
      return false;
  }
}

// ============================================================================
// @name 时间和数据转换工具
// ============================================================================

/**
 * @brief 获取自程序启动以来的毫秒数
 * @details 使用 steady_clock 计算，适用于性能测量和超时判断
 * @return 已运行的毫秒数（int32 范围）
 */
std::int32_t tick_count_ms() {
  static const auto started = std::chrono::steady_clock::now();
  return static_cast<std::int32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                            started)
          .count());
}

/**
 * @brief 将 int32 值截取为合法的 uint8 字节（0-255范围）
 * @param value 输入值
 * @return 截取后的字节值
 */
std::uint8_t legacy_byte(std::int32_t value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

// ============================================================================
// @name Actor属性读取辅助函数
// @brief 从GameObject中提取各类属性，自动处理Player/Monster类型差异
// ============================================================================

/**
 * @brief 获取Actor的朝向方向
 * @details 对于Player返回 character.dir，对于Monster返回 dir()，
 *          其他类型默认返回4（向下）
 * @param object 游戏对象
 * @return 方向值（0-7），0=上，顺时针递增，4=下
 */
std::uint8_t actor_dir(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().dir;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->dir();
  }
  return 4;
}

/**
 * @brief 获取Actor的照明值（仅玩家有照明属性）
 * @param object 游戏对象
 * @return 照明值，非Player返回0
 */
std::uint8_t actor_light(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().light;
  }
  return 0;
}

/**
 * @brief 获取Actor的外观特征值
 * @details 玩家返回 feature 字段，怪物通过 race_image 和 appearance
 *          组合生成特征值
 * @param object 游戏对象
 * @return 特征编码值
 */
std::int32_t actor_feature(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().feature;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return make_feature(legacy_byte(monster->race_image()), 0,
                        legacy_byte(monster->appearance()), 0);
  }
  return 0;
}

/**
 * @brief 获取Actor的状态值（仅玩家有状态属性）
 * @param object 游戏对象
 * @return 状态值，非Player返回0
 */
std::int32_t actor_status(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().status;
  }
  return 0;
}

/**
 * @brief 获取Actor的命中速度（仅玩家需要此属性，用于攻击动画速度）
 * @param object 游戏对象
 * @return 命中速度值，非Player返回0
 */
std::int32_t actor_hit_speed(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->legacy_hit_speed();
  }
  return 0;
}

/**
 * @brief 获取Actor的魔法抗性（仅怪物有此属性）
 * @details 返回怪物魔法防御值的非负部分
 * @param object 游戏对象
 * @return 魔法抗性值，非Monster返回0
 */
std::int32_t legacy_actor_anti_magic(const GameObject& object) {
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return std::max(monster->magical_defense(), 0);
  }
  return 0;
}

/**
 * @brief 获取Actor的毒物抗性（仅玩家有此属性）
 * @param object 游戏对象
 * @return 毒物抗性值，非Player返回0
 */
std::int32_t legacy_actor_anti_poison(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return std::max(player->legacy_anti_poison(), 0);
  }
  return 0;
}

/**
 * @brief 获取Actor的名称颜色
 * @details 根据PK等级决定颜色索引：PK>=2为红色(249)，PK=1为棕色(251)，
 *          其他情况使用玩家自身的名称颜色配置，非玩家返回默认白色(255)
 * @param object 游戏对象
 * @return 名称颜色索引
 */
std::uint8_t actor_name_color(const GameObject& object) {
  const auto* player = as_player(&object);
  if (player == nullptr) {
    return kDefaultNameColor;
  }
  if (player->pk_level() >= 2) {
    return 249;
  }
  if (player->pk_level() == 1) {
    return 251;
  }
  return player->legacy_name_color();
}

/**
 * @brief 获取Actor的名称字符串
 * @param object 游戏对象
 * @return 玩家返回角色名，其他对象返回 object.name()
 */
std::string actor_name(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().character_name;
  }
  return object.name();
}

/**
 * @brief 判断怪物是否使用特殊骨架数据包（骷髅/精灵类型）
 * @details 检查怪物名称是否为白骷髅(__whiteskeleton)、精灵(__elf)或精灵战士(__elfwarrior)。
 *          这类怪物使用特殊的动作帧数据包格式。
 * @param object 游戏对象
 * @return true 如果该怪物需要特殊骨架包处理
 */
bool actor_uses_skeleton_packet(const GameObject& object) {
  const auto* monster = as_monster(&object);
  if (monster == nullptr) {
    return false;
  }
  const auto lowered = util::lower_copy(monster->name());
  return lowered == "__whiteskeleton" || lowered == "__elf" || lowered == "__elfwarrior";
}

/**
 * @brief 创建传统角色描述结构体
 * @details 从GameObject中提取特征值和状态值填充到 LegacyCharDesc 结构中
 * @param object 游戏对象
 * @return 填充好的 LegacyCharDesc 结构
 */
LegacyCharDesc make_char_desc(const GameObject& object) {
  LegacyCharDesc desc;
  desc.feature = actor_feature(object);
  desc.status = actor_status(object);
  return desc;
}

/**
 * @brief 获取地图的暗黑度级别
 * @details 返回0=白天(无黑暗), 1=黑暗, 2=默认
 * @param map_config 地图配置
 * @return 暗黑度值
 */
std::uint16_t legacy_map_darkness(const MapConfig& map_config) {
  if (map_config.daylight) {
    return 0;
  }
  if (map_config.darkness) {
    return 1;
  }
  return 2;
}

// ============================================================================
// @name 坐标和方向计算辅助函数
// ============================================================================

/**
 * @brief 从uint16打包值中提取最小值（低8位）
 * @param value 打包的16位值
 * @return 低8位值（0-255）
 */
std::int32_t packed_min(std::uint16_t value) { return static_cast<std::int32_t>(value & 0xffu); }

/**
 * @brief 从uint16打包值中提取最大值（高8位）
 * @param value 打包的16位值
 * @return 高8位值（0-255）
 */
std::int32_t packed_max(std::uint16_t value) {
  return static_cast<std::int32_t>((value >> 8) & 0xffu);
}

/**
 * @brief 根据方向值获取坐标偏移量（dx, dy）
 * @details 8方向编码：0=上, 1=右上, 2=右, 3=右下, 4=下, 5=左下, 6=左, 7=左上
 * @param dir 方向值（0-7），会自动取模8
 * @return 坐标偏移对 (dx, dy)
 */
std::pair<std::int32_t, std::int32_t> direction_delta(std::uint8_t dir) {
  switch (dir % 8) {
    case 0:
      return {0, -1};
    case 1:
      return {1, -1};
    case 2:
      return {1, 0};
    case 3:
      return {1, 1};
    case 4:
      return {0, 1};
    case 5:
      return {-1, 1};
    case 6:
      return {-1, 0};
    case 7:
    default:
      return {-1, -1};
  }
}

// ============================================================================
// @name 攻击范围与剑术技能
// ============================================================================

/**
 * @brief 根据攻击标识解析攻击范围
 * @param ident 攻击标识（如 kCmLongHit 为远程攻击）
 * @return 攻击范围（格数），长击返回2，其他返回1
 */
std::int32_t resolve_attack_range(std::uint16_t ident) {
  switch (ident) {
    case kCmLongHit:
      return 2;
    default:
      return 1;
  }
}

/**
 * @brief 判断魔法ID是否为传统P14剑术技能
 * @details P14使用的剑术技能包括：基本剑术(3)、攻杀剑术(4)、
 *          刺杀剑术(7)、半月弯刀(12)、烈火剑法(25)、
 *          雷霆剑法(26)、开天斩(27)、逐日剑法(34)
 * @param magic_id 魔法ID
 * @return true 如果是P14剑术技能
 */
bool legacy_p14_sword_skill(std::int32_t magic_id) {
  switch (magic_id) {
    case 3:
    case 4:
    case 7:
    case 12:
    case 25:
    case 26:
    case 27:
    case 34:
      return true;
    default:
      return false;
  }
}

/**
 * @brief 将魔法ID映射为攻击标识
 * @details 剑术技能对应的攻击标识用于网络包中的攻击类型字段：
 *          攻杀(4)->重击, 半月(12)->长击, 烈火(25)->横斩,
 *          雷霆(26)->火击, 逐日(34)->十字斩, 基本剑术(3)->普通攻击
 * @param magic_id 魔法ID
 * @return 对应的攻击标识（kCmHit/kCmHeavyHit/kCmLongHit等）
 */
std::uint16_t legacy_attack_ident_for_sword_skill(std::int32_t magic_id) {
  switch (magic_id) {
    case 4:
      return kCmHeavyHit;
    case 12:
      return kCmLongHit;
    case 25:
      return kCmWideHit;
    case 26:
      return kCmFireHit;
    case 34:
      return kCmCrossHit;
    case 3:
    default:
      return kCmHit;
  }
}

/**
 * @brief 将攻击标识反向映射为魔法ID
 * @details legacy_attack_ident_for_sword_skill 的逆函数
 * @param ident 攻击标识
 * @return 对应的魔法ID，无法识别返回0
 */
std::int32_t legacy_sword_skill_for_attack_ident(std::uint16_t ident) {
  switch (ident) {
    case kCmHeavyHit:
      return 4;
    case kCmPowerHit:
      return 7;
    case kCmLongHit:
      return 12;
    case kCmWideHit:
      return 25;
    case kCmFireHit:
      return 26;
    case kCmCrossHit:
      return 34;
    default:
      return 0;
  }
}

/**
 * @brief 传统命中判定：命中掷骰小于准确度即命中
 * @param accuracy_point 攻击方准确度
 * @param hit_roll 命中掷骰值
 * @return true 如果命中成功
 */
bool legacy_hit_roll_succeeds(std::int32_t accuracy_point, std::int32_t hit_roll) {
  return hit_roll < accuracy_point;
}

// ============================================================================
// @name 战斗状态判断
// ============================================================================

/**
 * @brief 判断游戏对象是否存活
 * @param object 游戏对象
 * @return true 如果对象存活（未死亡）
 */
bool is_alive(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return !player->is_dead();
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return !monster->is_dead();
  }
  return false;
}

/**
 * @brief 判断游戏对象是否为可攻击目标
 * @details 只有存活的玩家或怪物才是可攻击目标
 * @param object 游戏对象
 * @return true 如果是可攻击目标
 */
bool is_attackable_target(const GameObject& object) {
  return (as_player(&object) != nullptr || as_monster(&object) != nullptr) && is_alive(object);
}

// 前向声明：定义在文件末尾
bool is_safe_zone(const MapConfig& map_config, std::int32_t x, std::int32_t y);

/**
 * @brief 判断攻击源是否会引起怪物的反击
 * @details 检查攻击源是否是怪物有效的反击目标。对于玩家攻击者，需要不在安全区、
 *          不是幽灵状态且未开启透明模式。对于怪物攻击者，需要不是召唤兽攻击主人、
 *          不是同源怪物等条件。
 * @param monster 被攻击的怪物
 * @param source 攻击源对象
 * @param map_config 地图配置（用于安全区判断）
 * @param current_tick 当前时间戳
 * @return true 如果怪物应该反击该攻击源
 */
bool is_legacy_monster_retaliation_source(const Monster& monster, const GameObject& source,
                                          const MapConfig& map_config,
                                          std::uint64_t current_tick) {
  if (const auto* source_player = as_player(&source); source_player != nullptr) {
    if (monster.is_slave() && source_player->id() == monster.master_actor_id()) {
      return false;
    }
    return !source_player->is_dead() && !source_player->legacy_ghost() &&
           !is_safe_zone(map_config, source_player->x(), source_player->y()) &&
           !source_player->legacy_transparent_active(current_tick);
  }
  const auto* source_monster = as_monster(&source);
  if (source_monster == nullptr || source_monster->id() == monster.id() ||
      source_monster->master_actor_id() == 0 || source_monster->is_dead() ||
      source_monster->legacy_ghosted() || source_monster->hide_mode()) {
    return false;
  }
  if (source_monster->id() == monster.master_actor_id() ||
      source_monster->master_actor_id() == monster.master_actor_id()) {
    return false;
  }
  return true;
}

/**
 * @brief 对怪物应用传统伤害并处理反击和死亡逻辑
 * @details 对怪物造成伤害后，如果怪物未死亡且AI类型支持反击，则会选择攻击源
 *          为目标。如果怪物在本次伤害中死亡，则标记死亡时间。
 * @param objects 当前地图的所有对象
 * @param monster 目标怪物
 * @param damage 伤害值
 * @param source_actor_id 攻击者ID
 * @param map_config 地图配置
 * @param current_tick 当前逻辑滴答
 * @param now_ms 当前时间（毫秒）
 * @return 实际造成的伤害值（0表示未造成伤害）
 */
std::int32_t apply_legacy_monster_damage(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    Monster& monster,
    std::int32_t damage,
    std::uint64_t source_actor_id,
    const MapConfig& map_config,
    std::uint64_t current_tick,
    std::uint64_t now_ms) {
  const auto was_dead = monster.is_dead();
  const auto applied = monster.apply_damage(damage, source_actor_id, now_ms);
  if (applied <= 0) {
    return 0;
  }

  if (!monster.is_dead() &&
      (monster.ai_profile() == MonsterAiProfile::basic ||
       monster.ai_profile() == MonsterAiProfile::aggressive)) {
    const auto source_it = objects.find(source_actor_id);
    if (source_it != objects.end()) {
      if (is_legacy_monster_retaliation_source(monster, *source_it->second,
                                               map_config, current_tick)) {
        monster.select_target(source_actor_id, now_ms);
      }
    }
  }

  if (!was_dead && monster.is_dead() && monster.death_time_ms() == 0) {
    static_cast<void>(monster.mark_legacy_death(now_ms));
  }
  return applied;
}

// ============================================================================
// @name Actor属性读取函数
// ============================================================================

/**
 * @brief 获取Actor的当前生命值（HP）
 * @param object 游戏对象
 * @return 当前HP值
 */
std::int32_t actor_hp(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().ability.hp;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->hp();
  }
  return 0;
}

/**
 * @brief 获取Actor的最大生命值（MaxHP）
 * @param object 游戏对象
 * @return 最大HP值
 */
std::int32_t actor_max_hp(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().ability.max_hp;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->max_hp();
  }
  return 0;
}

/**
 * @brief 获取Actor的等级
 * @param object 游戏对象
 * @return 等级值，默认为1
 */
std::int32_t actor_level(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->character().ability.level;
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->level();
  }
  return 1;
}

/**
 * @brief 获取Actor的物理防御值
 * @param object 游戏对象
 * @return 物理防御值
 */
std::int32_t actor_physical_defense(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->physical_defense();
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->physical_defense();
  }
  return 0;
}

/**
 * @brief 获取Actor的魔法防御值
 * @param object 游戏对象
 * @return 魔法防御值
 */
std::int32_t actor_magic_defense(const GameObject& object) {
  if (const auto* player = as_player(&object); player != nullptr) {
    return player->magic_defense();
  }
  if (const auto* monster = as_monster(&object); monster != nullptr) {
    return monster->magical_defense();
  }
  return 0;
}

// ============================================================================
// @name 数据包和事件队列辅助函数
// ============================================================================

/**
 * @brief 向玩家的会话投递数据包（带延迟）
 * @param dispatch 运行时调度器
 * @param session_id 玩家会话ID
 * @param packet 要发送的数据包
 * @param delay_ms 延迟毫秒数
 */
void queue_packet(RuntimeDispatch& dispatch, std::uint64_t session_id, LegacyPacket packet,
                  std::int32_t delay_ms) {
  dispatch.session_events.push_back(SessionEvent{
      SessionEventKind::send_packet, "game_gateway", session_id, {}, std::move(packet), {},
      delay_ms});
}

/**
 * @brief 向玩家的会话立即投递数据包（无延迟重载版本）
 * @param dispatch 运行时调度器
 * @param session_id 玩家会话ID
 * @param packet 要发送的数据包
 */
void queue_packet(RuntimeDispatch& dispatch, std::uint64_t session_id, LegacyPacket packet) {
  queue_packet(dispatch, session_id, std::move(packet), 0);
}

/**
 * @brief 强制断开玩家连接
 * @param dispatch 运行时调度器
 * @param session_id 玩家会话ID
 * @param reason 断开原因描述
 */
void queue_force_disconnect(RuntimeDispatch& dispatch, std::uint64_t session_id,
                            std::string reason) {
  dispatch.session_events.push_back(SessionEvent{
      SessionEventKind::force_disconnect, "game_gateway", session_id, {}, {}, std::move(reason)});
}

/**
 * @brief 遍历所有玩家对象并执行回调
 * @tparam Fn 回调函数类型（接收 actor_id 和 Player 引用）
 * @param objects 当前地图的所有对象
 * @param fn 回调函数
 */
template <typename Fn>
void for_each_player(const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
                     Fn&& fn) {
  for (const auto& [actor_id, object] : objects) {
    const auto* player = as_player(object.get());
    if (player == nullptr) {
      continue;
    }
    fn(actor_id, *player);
  }
}

// ============================================================================
// @name 物品查询辅助函数
// ============================================================================

/**
 * @brief 根据物品索引查找物品配置
 * @param item_configs 物品配置表
 * @param item_index 物品索引ID
 * @return 物品配置指针，未找到返回 nullptr
 */
const ItemConfig* find_item_config(const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                                   std::int32_t item_index) {
  const auto it = item_configs.find(item_index);
  return it != item_configs.end() ? &it->second : nullptr;
}

/**
 * @brief 获取物品的名称
 * @param item 玩家物品
 * @param item_configs 物品配置表
 * @return 物品名称，未找到配置返回 "Item <index>"
 */
std::string item_name(const LegacyUserItem& item,
                      const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (const auto* config = find_item_config(item_configs, item.index);
      config != nullptr && !config->name.empty()) {
    return config->name;
  }
  return "Item " + std::to_string(item.index);
}

/**
 * @brief 获取物品的外观显示ID
 * @param item 玩家物品
 * @param item_configs 物品配置表
 * @return 外观ID，优先使用配置中的 looks 字段，否则返回物品索引
 */
std::int32_t item_looks(const LegacyUserItem& item,
                        const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return config->looks > 0 ? config->looks : item.index;
  }
  return item.index;
}

/**
 * @brief 获取物品的重量
 * @param item 玩家物品
 * @param item_configs 物品配置表
 * @return 物品重量，空物品返回0
 */
std::int32_t item_weight(const LegacyUserItem& item,
                         const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (is_empty(item)) {
    return 0;
  }
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return std::max(config->weight, 0);
  }
  return 0;
}

/**
 * @brief 根据金币数量获取外观ID
 * @details 不同数量的金币堆使用不同的外观ID
 * @param amount 金币数量
 * @return 外观ID（112-116）
 */
std::int32_t gold_looks(std::int32_t amount) {
  if (amount >= 1000) {
    return 116;
  }
  if (amount >= 300) {
    return 115;
  }
  if (amount >= 70) {
    return 114;
  }
  if (amount >= 30) {
    return 113;
  }
  return 112;
}

/**
 * @brief 显示耐久度单位（数据库存储值转显示值）
 * @details 数据库中的dura以千分之一为单位，此处除以1000并四舍五入
 * @param dura 原始耐久度值（千分之一单位）
 * @return 显示的耐久度
 */
std::int32_t display_dura_units(std::uint16_t dura) {
  return static_cast<std::int32_t>((static_cast<std::uint32_t>(dura) + 500) / 1000);
}

/**
 * @brief 获取物品的最大耐久度
 * @param item 玩家物品
 * @param item_configs 物品配置表
 * @return 最大耐久度，优先使用物品自身字段，其次使用配置值
 */
std::uint16_t item_dura_max(const LegacyUserItem& item,
                            const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  if (item.dura_max > 0) {
    return item.dura_max;
  }
  if (const auto* config = find_item_config(item_configs, item.index); config != nullptr) {
    return static_cast<std::uint16_t>(std::clamp(config->dura_max, 0, 65535));
  }
  return 0;
}

/**
 * @brief 根据物品 StdMode 解析装备槽位（委托给 legacy_resolve_slot_from_std_mode）
 * @param std_mode 物品标准模式
 * @return 槽位索引
 */
std::int32_t resolve_slot_from_std_mode(std::int32_t std_mode) {
  return legacy_resolve_slot_from_std_mode(std_mode);
}

/**
 * @brief 判断物品是否适合指定槽位（委托给 legacy_item_fits_slot）
 * @param item_config 物品配置
 * @param slot 槽位索引
 * @return true 如果物品适合该槽位
 */
bool item_fits_slot(const ItemConfig& item_config, std::int32_t slot) {
  return legacy_item_fits_slot(item_config, slot);
}

/**
 * @brief 判断物品是否为消耗品（委托给 legacy_item_is_consumable）
 * @param item_config 物品配置
 * @return true 如果是消耗品
 */
bool is_consumable(const ItemConfig& item_config) {
  return legacy_item_is_consumable(item_config);
}

/**
 * @brief 判断是否需要展示详细商品列表
 * @details StdMode <= 4 或为31/42时为简单物品，不需要详细列表
 * @param item_config 物品配置
 * @return true 如果需要展示详细列表
 */
bool requires_detail_goods_list(const ItemConfig& item_config) {
  return !(item_config.std_mode <= 4 || item_config.std_mode == 31 || item_config.std_mode == 42);
}

// ============================================================================
// @name 范围与可见性判断
// ============================================================================

/**
 * @brief 判断两个对象是否在交互范围内（15格）
 * @param lhs 对象A
 * @param rhs 对象B
 * @return true 如果在交互范围内
 */
bool in_interaction_range(const GameObject& lhs, const GameObject& rhs) {
  return std::abs(lhs.x() - rhs.x()) <= 15 && std::abs(lhs.y() - rhs.y()) <= 15;
}

/**
 * @brief 判断 target 是否在 viewer 的正前方
 * @param viewer 观察者
 * @param target 目标对象
 * @return true 如果目标在观察者正前方
 */
bool is_directly_in_front_of(const GameObject& viewer, const GameObject& target) {
  const auto [dx, dy] = direction_delta(actor_dir(viewer));
  return target.x() == viewer.x() + dx && target.y() == viewer.y() + dy;
}

/**
 * @brief 判断两个对象是否面对面
 * @details 双方都在对方的正前方一格
 * @param lhs 对象A
 * @param rhs 对象B
 * @return true 如果双方面对面
 */
bool mutually_facing(const GameObject& lhs, const GameObject& rhs) {
  return is_directly_in_front_of(lhs, rhs) && is_directly_in_front_of(rhs, lhs);
}

/**
 * @brief 判断坐标是否在传统视野范围内（坐标版本）
 * @details 标准视野范围为12格
 * @param lhs_x 观察者X坐标
 * @param lhs_y 观察者Y坐标
 * @param rhs_x 目标X坐标
 * @param rhs_y 目标Y坐标
 * @return true 如果在视野范围内
 */
bool in_legacy_view_range(std::int32_t lhs_x, std::int32_t lhs_y,
                          std::int32_t rhs_x, std::int32_t rhs_y) {
  return std::abs(lhs_x - rhs_x) <= kLegacyViewRange &&
         std::abs(lhs_y - rhs_y) <= kLegacyViewRange;
}

/**
 * @brief 判断两个对象是否在传统视野范围内（对象版本）
 * @param lhs 观察者对象
 * @param rhs 目标对象
 * @return true 如果在视野范围内
 */
bool in_legacy_view_range(const GameObject& lhs, const GameObject& rhs) {
  return in_legacy_view_range(lhs.x(), lhs.y(), rhs.x(), rhs.y());
}

/**
 * @brief 判断目标是否对观察者玩家可见
 * @details 隐藏模式的怪物不可见，自身不可见，需要双方在视野范围内
 * @param watcher 观察者玩家
 * @param target 目标对象
 * @return true 如果目标对该玩家可见
 */
bool is_legacy_visible_to(const Player& watcher, const GameObject& target) {
  if (const auto* monster = as_monster(&target); monster != nullptr && monster->hide_mode()) {
    return false;
  }
  return watcher.id() != target.id() && in_legacy_view_range(watcher, target);
}

/**
 * @brief 向所有能看见 origin 的玩家广播数据包
 * @tparam MakePacket 生成数据包的回调类型
 * @param objects 当前地图的所有对象
 * @param dispatch 运行时调度器
 * @param origin 源对象
 * @param include_origin 是否也发送给源对象自身
 * @param make_packet 生成数据包的回调
 */
template <typename MakePacket>
void queue_actor_origin_packet(
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    RuntimeDispatch& dispatch, const GameObject& origin, bool include_origin,
    MakePacket&& make_packet) {
  static_cast<void>(dispatch);
  for_each_player(objects, [&](std::uint64_t, const Player& watcher) {
    if (watcher.id() == origin.id()) {
      if (!include_origin) {
        return;
      }
    } else if (!is_legacy_visible_to(watcher, origin)) {
      return;
    }
    make_packet(watcher);
  });
}

/**
 * @brief 判断地面物品是否在玩家的传统视野范围内
 * @param watcher 观察者玩家
 * @param item 地面物品
 * @return true 如果在视野范围内
 */
bool in_legacy_view_range(const GameObject& watcher, const MapActor::GroundItem& item) {
  return in_legacy_view_range(watcher.x(), watcher.y(), item.x, item.y);
}

// ============================================================================
// @name 商人NPC对话框构建
// ============================================================================

/**
 * @brief 构建商人的服务菜单条目列表
 * @details 根据NPC支持的服务类型（行会、城堡、购买、出售、修理、仓库），
 *          生成对应的菜单项列表
 * @param merchant 商人NPC对象
 * @return 菜单条目列表
 */
std::vector<MerchantDialogEntry> build_merchant_dialog_entries(const Npc& merchant) {
  std::vector<MerchantDialogEntry> entries;
  if (merchant.supports_guild()) {
    entries.push_back({"Guild", "@guild_menu"});
  }
  if (merchant.supports_castle()) {
    entries.push_back({"Castle", "@castle_menu"});
  }
  if (merchant.supports_buy()) {
    entries.push_back({"Buy", "@buy"});
  }
  if (merchant.supports_sell()) {
    entries.push_back({"Sell", "@sell"});
  }
  if (merchant.supports_repair()) {
    entries.push_back({"Repair", "@repair"});
  }
  if (merchant.supports_storage()) {
    entries.push_back({"Store", "@storage"});
    entries.push_back({"Retrieve", "@getback"});
  }
  return entries;
}

/**
 * @brief 统计商人支持的服务种类数量
 * @param merchant 商人NPC对象
 * @return 服务种类数量
 */
std::size_t merchant_service_group_count(const Npc& merchant) {
  std::size_t count = 0;
  if (merchant.supports_guild()) {
    ++count;
  }
  if (merchant.supports_castle()) {
    ++count;
  }
  if (merchant.supports_buy()) {
    ++count;
  }
  if (merchant.supports_sell()) {
    ++count;
  }
  if (merchant.supports_repair()) {
    ++count;
  }
  if (merchant.supports_storage()) {
    ++count;
  }
  return count;
}

// ============================================================================
// @name NPC对话框文本查找与构建
// ============================================================================

/**
 * @brief 根据动作查找NPC的对话框文本
 * @details 支持标准动作匹配，自动处理 "@home" -> "@main" 的别名映射，
 *          以及 "~" 前缀的静默匹配
 * @param merchant 商人NPC对象
 * @param action 动作名称（如 "@main", "@buy"）
 * @return 对话框文本指针，未找到返回 nullptr
 */
const std::string* find_npc_dialog_text(const Npc& merchant, std::string_view action) {
  auto normalize_action = [](std::string_view value) {
    auto normalized = util::lower_copy(util::trim(std::string(value)));
    if (normalized == "@home") {
      return std::string{"@main"};
    }
    if (normalized == "~@home") {
      return std::string{"~@main"};
    }
    return normalized;
  };
  const auto wanted = normalize_action(action);
  const auto tilde_wanted =
      util::starts_with(wanted, "@") ? "~" + wanted : std::string{};
  const auto plain_wanted =
      util::starts_with(wanted, "~@") ? wanted.substr(1) : std::string{};
  for (const auto& section : merchant.dialog_sections()) {
    const auto current = normalize_action(section.action);
    if (current == wanted || (!tilde_wanted.empty() && current == tilde_wanted) ||
        (!plain_wanted.empty() && current == plain_wanted)) {
      return &section.text;
    }
  }
  return nullptr;
}

/**
 * @brief 字符串全局替换
 * @param text 要修改的字符串
 * @param needle 要查找的子串
 * @param replacement 替换字符串
 */
void replace_all(std::string& text, std::string_view needle, std::string_view replacement) {
  if (needle.empty()) {
    return;
  }
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

/**
 * @brief 向对话框文本追加可点击的条目（<标签/动作>格式）
 * @param text 对话框文本
 * @param label 按钮标签
 * @param action 点击触发的动作
 */
void append_dialog_entry(std::string& text, std::string label, std::string action) {
  if (!text.empty() && text.back() != '\\') {
    text.push_back('\\');
  }
  text += "<" + std::move(label) + "/" + std::move(action) + ">";
}

/**
 * @brief 向对话框文本追加一行文本
 * @param text 对话框文本
 * @param line 追加的行内容
 */
void append_dialog_line(std::string& text, std::string line) {
  if (!text.empty() && text.back() != '\\') {
    text.push_back('\\');
  }
  text += std::move(line);
}

/**
 * @brief 计算分页总页数
 * @param item_count 条目总数
 * @return 总页数（至少1页）
 */
std::size_t dialog_total_pages(std::size_t item_count) {
  return std::max<std::size_t>(1, (item_count + kNpcDialogPageSize - 1) / kNpcDialogPageSize);
}

/**
 * @brief 将请求的页码限制在合法范围内
 * @param requested_page 请求的页码
 * @param item_count 条目总数
 * @return 合法后的页码（1到总页数之间）
 */
std::size_t clamp_dialog_page(std::size_t requested_page, std::size_t item_count) {
  return std::clamp<std::size_t>(requested_page, 1, dialog_total_pages(item_count));
}

/**
 * @brief 从动作负载中解析对话框页码
 * @param payload 动作负载字符串
 * @param prefix 动作前缀（如 "@guild_members"）
 * @return 解析出的页码，默认为1
 */
std::size_t parse_dialog_page(std::string_view payload, std::string_view prefix) {
  const auto lowered = util::lower_copy(payload);
  if (!util::starts_with(lowered, prefix)) {
    return 1;
  }
  const auto suffix = util::trim(lowered.substr(prefix.size()));
  if (suffix.empty()) {
    return 1;
  }
  const auto page = parse_int32(suffix);
  return page.has_value() && *page > 0 ? static_cast<std::size_t>(*page) : 1;
}

// ============================================================================
// @name 行会对话框目标解析
// @brief 从NPC动作命令中解析出行会管理目标信息（页码、成员名等）
// ============================================================================

/**
 * @brief 解析行会成员管理目标
 * @param payload 动作负载字符串（格式："<page> <member_name>"）
 * @return GuildMemberDialogTarget 结构体
 */
GuildMemberDialogTarget parse_guild_member_dialog_target(std::string_view payload) {
  GuildMemberDialogTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 2) {
    return target;
  }

  const auto parsed_page = parse_int32(tokens[1]);
  if (parsed_page.has_value() && *parsed_page > 0) {
    target.page = static_cast<std::size_t>(*parsed_page);
    target.member_name = util::trim(join_tokens(tokens, 2));
    return target;
  }

  target.member_name = util::trim(join_tokens(tokens, 1));
  return target;
}

/**
 * @brief 解析行会成员头衔设置目标
 * @param payload 动作负载（格式："<member_page> <title_page> <member_name>"）
 * @return GuildMemberTitleDialogTarget 结构体
 */
GuildMemberTitleDialogTarget parse_guild_member_title_dialog_target(std::string_view payload) {
  GuildMemberTitleDialogTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 4) {
    return target;
  }

  const auto member_page = parse_int32(tokens[1]);
  if (member_page.has_value() && *member_page > 0) {
    target.member_page = static_cast<std::size_t>(*member_page);
  }

  const auto title_page = parse_int32(tokens[2]);
  if (title_page.has_value() && *title_page > 0) {
    target.title_page = static_cast<std::size_t>(*title_page);
  }

  target.member_name = util::trim(join_tokens(tokens, 3));
  return target;
}

/**
 * @brief 解析行会申请者管理目标
 * @param payload 动作负载（格式："<page> <applicant_name>"）
 * @return GuildApplicantDialogTarget 结构体
 */
GuildApplicantDialogTarget parse_guild_applicant_dialog_target(std::string_view payload) {
  GuildApplicantDialogTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 3) {
    return target;
  }

  const auto page = parse_int32(tokens[1]);
  if (page.has_value() && *page > 0) {
    target.page = static_cast<std::size_t>(*page);
  }
  target.applicant_name = util::trim(join_tokens(tokens, 2));
  return target;
}

/**
 * @brief 解析行会浏览目标
 * @param payload 动作负载（格式："<source> <page> <guild_name>"）
 * @return GuildBrowseTarget 结构体
 */
GuildBrowseTarget parse_guild_browse_target(std::string_view payload) {
  GuildBrowseTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 4) {
    return target;
  }

  target.source = normalize_guild_browse_source(tokens[1]);

  const auto page = parse_int32(tokens[2]);
  if (page.has_value() && *page > 0) {
    target.page = static_cast<std::size_t>(*page);
  }

  target.guild_name = util::trim(join_tokens(tokens, 3));
  return target;
}

/**
 * @brief 解析行会浏览列表目标（含双层分页）
 * @param payload 动作负载（格式："<source> <browse_page> <list_page> <guild_name>"）
 * @return GuildBrowseListTarget 结构体
 */
GuildBrowseListTarget parse_guild_browse_list_target(std::string_view payload) {
  GuildBrowseListTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 5) {
    return target;
  }

  target.source = normalize_guild_browse_source(tokens[1]);

  const auto browse_page = parse_int32(tokens[2]);
  if (browse_page.has_value() && *browse_page > 0) {
    target.browse_page = static_cast<std::size_t>(*browse_page);
  }

  const auto list_page = parse_int32(tokens[3]);
  if (list_page.has_value() && *list_page > 0) {
    target.list_page = static_cast<std::size_t>(*list_page);
  }

  target.guild_name = util::trim(join_tokens(tokens, 4));
  return target;
}

/**
 * @brief 解析行会头衔确认目标
 * @param payload 动作负载（格式："<member_page> <title_page> <member_name> <title_name>"）
 * @return GuildTitleConfirmTarget 结构体
 */
GuildTitleConfirmTarget parse_guild_title_confirm_target(std::string_view payload) {
  GuildTitleConfirmTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 5) {
    return target;
  }

  const auto member_page = parse_int32(tokens[1]);
  if (member_page.has_value() && *member_page > 0) {
    target.member_page = static_cast<std::size_t>(*member_page);
  }

  const auto title_page = parse_int32(tokens[2]);
  if (title_page.has_value() && *title_page > 0) {
    target.title_page = static_cast<std::size_t>(*title_page);
  }

  target.member_name = util::trim(tokens[3]);
  target.title_name = util::trim(join_tokens(tokens, 4));
  return target;
}

/**
 * @brief 解析城堡战争确认目标
 * @param payload 动作负载（格式："<page> <guild_name>"）
 * @return CastleWarConfirmTarget 结构体
 */
CastleWarConfirmTarget parse_castle_war_confirm_target(std::string_view payload) {
  CastleWarConfirmTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 3) {
    return target;
  }

  const auto page = parse_int32(tokens[1]);
  if (page.has_value() && *page > 0) {
    target.page = static_cast<std::size_t>(*page);
  }
  target.guild_name = util::trim(join_tokens(tokens, 2));
  return target;
}

/**
 * @brief 解析城堡行会浏览目标
 * @param payload 动作负载（格式："<source> <page> <guild_name>"）
 * @return CastleGuildBrowseTarget 结构体
 */
CastleGuildBrowseTarget parse_castle_guild_browse_target(std::string_view payload) {
  CastleGuildBrowseTarget target;
  const auto tokens = util::split(std::string(payload), ' ');
  if (tokens.size() < 4) {
    return target;
  }

  target.source = util::lower_copy(tokens[1]);
  if (target.source != "targets") {
    target.source = "wars";
  }

  const auto page = parse_int32(tokens[2]);
  if (page.has_value() && *page > 0) {
    target.page = static_cast<std::size_t>(*page);
  }
  target.guild_name = util::trim(join_tokens(tokens, 3));
  return target;
}

/**
 * @brief 向对话框文本追加页码导航按钮（Prev/Next）
 * @param text 对话框文本
 * @param action_root 翻页动作根命令
 * @param page 当前页码
 * @param total_pages 总页数
 */
void append_page_navigation(std::string& text, std::string action_root, std::size_t page,
                            std::size_t total_pages) {
  if (page > 1) {
    append_dialog_entry(text, "Prev",
                        action_root + " " + std::to_string(static_cast<int>(page - 1)));
  }
  if (page < total_pages) {
    append_dialog_entry(text, "Next",
                        action_root + " " + std::to_string(static_cast<int>(page + 1)));
  }
}

/**
 * @brief 获取玩家当前装备的武器名称
 * @param player 玩家对象
 * @param item_configs 物品配置表
 * @return 武器名称，未装备武器返回 "your weapon"
 */
std::string equipped_weapon_name(const Player& player,
                                 const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto* weapon = player.equipped_item(1);
  if (weapon == nullptr || is_empty(*weapon)) {
    return "your weapon";
  }
  const auto name = item_name(*weapon, item_configs);
  return name.empty() ? std::string("your weapon") : name;
}

std::string default_castle_war_date(const CastleDialogContext& castle_dialog_context);
std::string display_castle_wars(const CastleDialogContext& castle_dialog_context);
std::string display_castle_owner(const CastleDialogContext& castle_dialog_context);
std::string display_castle_lord(const CastleDialogContext& castle_dialog_context);

// ============================================================================
// @name 传统脚本引擎 - 变量替换
// ============================================================================

/**
 * @brief 解析传统脚本变量标记
 * @details 识别 P0-P9（玩家参数）、G0-G9（全局参数）、D0-D9（骰子参数）
 * @param raw 原始变量标记字符串（如 "P0", "G3", "D5"）
 * @return 解析成功的 (组别, 索引) 对
 */
std::optional<std::pair<char, std::int32_t>> parse_legacy_script_variable_token(
    std::string_view raw) {
  auto token = util::trim(std::string(raw));
  if (token.size() != 2 || std::isdigit(static_cast<unsigned char>(token[1])) == 0) {
    return std::nullopt;
  }
  const auto group = static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])));
  if (group != 'P' && group != 'G' && group != 'D') {
    return std::nullopt;
  }
  return std::pair{group, static_cast<std::int32_t>(token[1] - '0')};
}

/**
 * @brief 获取传统脚本变量的字符串值
 * @details 根据变量组别从不同来源取值：P组从玩家脚本参数、G组从全局参数、D组从骰子参数
 * @param player 玩家对象
 * @param script_global_params 脚本全局参数数组
 * @param raw 变量标记字符串
 * @return 变量值的字符串形式，无法识别返回 "0"
 */
std::string legacy_script_str_value(
    const Player& player, const std::array<std::int32_t, 10>& script_global_params,
    std::string_view raw) {
  const auto variable = parse_legacy_script_variable_token(raw);
  if (!variable.has_value()) {
    return "0";
  }
  const auto [group, index] = *variable;
  if (group == 'P') {
    return std::to_string(player.script_param(index));
  }
  if (group == 'G') {
    return std::to_string(script_global_params[static_cast<std::size_t>(index)]);
  }
  return std::to_string(player.script_dice_param(index));
}

/**
 * @brief 渲染传统脚本中的 $STR() 变量标记
 * @details 将文本中所有 $STR(P/G/Dx) 格式的变量标记替换为实际值，
 *          支持处理被 < > 包裹的完整标记
 * @param text 待处理的文本（会被修改）
 * @param player 玩家对象
 * @param script_global_params 脚本全局参数数组
 */
void render_legacy_script_str_values(
    std::string& text, const Player& player,
    const std::array<std::int32_t, 10>& script_global_params) {
  std::size_t pos = 0;
  while ((pos = text.find("$STR(", pos)) != std::string::npos) {
    const auto close = text.find(')', pos + 5);
    if (close == std::string::npos) {
      break;
    }
    auto start = pos;
    auto length = close - pos + 1;
    if (pos > 0 && text[pos - 1] == '<' && close + 1 < text.size() &&
        text[close + 1] == '>') {
      start = pos - 1;
      length += 2;
    }
    const auto replacement = legacy_script_str_value(
        player, script_global_params, std::string_view(text).substr(pos + 5, close - pos - 5));
    text.replace(start, length, replacement);
    pos = start + replacement.size();
  }
}

/**
 * @brief 获取空脚本全局参数数组的静态引用
 * @return 全零的10元素int32数组的常量引用
 */
const std::array<std::int32_t, 10>& empty_legacy_script_global_params() {
  static const std::array<std::int32_t, 10> params{};
  return params;
}

/**
 * @brief 渲染NPC对话框文本，替换所有占位符
 * @details 替换 NPC 对话框文本中的各类占位符，包括用户名、NPC名、
 *          地图名、武器名、城堡信息等，最后渲染脚本变量 $STR()
 * @param merchant 商人NPC对象
 * @param requester 请求对话的玩家
 * @param map_config 当前地图配置
 * @param castle_dialog_context 城堡对话框上下文
 * @param text 原始对话框文本
 * @param item_configs 物品配置表
 * @param script_global_params 脚本全局参数数组（可选）
 * @return 渲染后的完整对话框文本
 */
std::string render_npc_dialog_text(const Npc& merchant, const Player& requester,
                                   const MapConfig& map_config,
                                   const CastleDialogContext& castle_dialog_context,
                                   std::string text,
                                   const std::unordered_map<std::int32_t, ItemConfig>& item_configs,
                                   const std::array<std::int32_t, 10>& script_global_params =
                                       empty_legacy_script_global_params()) {
  replace_all(text, "<$USERNAME>", requester.character().character_name);
  replace_all(text, "<$NPCNAME>", merchant.name());
  replace_all(text, "<$MAPID>", map_config.id);
  replace_all(text, "<$MAPNAME>", map_config.title.empty() ? map_config.id : map_config.title);
  replace_all(text, "<$USERWEAPON>", equipped_weapon_name(requester, item_configs));
  replace_all(text, "<$OWNERGUILD>", display_castle_owner(castle_dialog_context));
  replace_all(text, "<$LORD>", display_castle_lord(castle_dialog_context));
  replace_all(text, "<$GUILDWARFEE>", std::to_string(castle_dialog_context.guild_war_fee));
  replace_all(text, "<$UPGRADEWEAPONFEE>",
              std::to_string(castle_dialog_context.upgrade_weapon_fee));
  replace_all(text, "<$CASTLEWARDATE>", default_castle_war_date(castle_dialog_context));
  replace_all(text, "<$LISTOFWAR>", display_castle_wars(castle_dialog_context));
  render_legacy_script_str_values(text, requester, script_global_params);
  return text;
}

/**
 * @brief 判断是否应该打开商人对话框
 * @details 当NPC有@main脚本、支持行会/城堡功能，或有多个服务种类时打开对话框
 * @param merchant 商人NPC对象
 * @return true 应该打开对话框
 */
bool should_open_merchant_dialog(const Npc& merchant) {
  return find_npc_dialog_text(merchant, "@main") != nullptr || merchant.supports_guild() ||
         merchant.supports_castle() || merchant_service_group_count(merchant) > 1;
}

// ============================================================================
// @name 传统脚本解析引擎
// @brief 解析传统Merchant.txt格式的NPC脚本，支持 #IF/#ACT/#SAY/#ELSESAY/#ELSEACT 流程
// ============================================================================

/**
 * @struct LegacyScriptProc
 * @brief 传统脚本的一个流程块，包含条件、执行动作和对话文本
 */
struct LegacyScriptProc {
  std::vector<std::string> say_lines{};       ///< #SAY 对话文本行
  std::vector<std::string> conditions{};       ///< #IF 条件判断行
  std::vector<std::string> act_lines{};        ///< #ACT 执行动作行
  std::vector<std::string> else_say_lines{};   ///< #ELSESAY 条件不满足时的对话文本
  std::vector<std::string> else_act_lines{};   ///< #ELSEACT 条件不满足时的执行动作
};

/**
 * @struct LegacyScriptBlock
 * @brief 传统脚本块，包含多个流程块
 */
struct LegacyScriptBlock {
  std::vector<LegacyScriptProc> procs{};  ///< 流程块列表
};

/**
 * @enum LegacyScriptParseMode
 * @brief 传统脚本解析模式，跟踪当前解析的脚本段类型
 */
enum class LegacyScriptParseMode {
  say,       ///< 正在解析 #SAY 段
  condition, ///< 正在解析 #IF 条件段
  act,       ///< 正在解析 #ACT 动作段
  else_say,  ///< 正在解析 #ELSESAY 段
  else_act   ///< 正在解析 #ELSEACT 段
};

/**
 * @brief 分割传统脚本文本为行，跳过注释行（;开头或/开头）
 * @param text 原始脚本文本
 * @return 非注释行的列表
 */
std::vector<std::string> split_legacy_script_lines(std::string_view text) {
  std::vector<std::string> lines;
  std::string current;
  for (const auto ch : text) {
    if (ch == '\n' || ch == '\r') {
      auto line = util::trim(current);
      if (!line.empty() && !util::starts_with(line, ";") && !util::starts_with(line, "/")) {
        lines.push_back(std::move(line));
      }
      current.clear();
      continue;
    }
    current.push_back(ch);
  }
  auto line = util::trim(current);
  if (!line.empty() && !util::starts_with(line, ";") && !util::starts_with(line, "/")) {
    lines.push_back(std::move(line));
  }
  return lines;
}

/**
 * @brief 去除脚本行开头的 # 号
 * @param line 脚本行
 * @return 去除 # 号后的行内容
 */
std::string strip_script_hash(std::string line) {
  line = util::trim(std::move(line));
  while (!line.empty() && line.front() == '#') {
    line.erase(line.begin());
    line = util::trim(std::move(line));
  }
  return line;
}

/**
 * @brief 将字符串转换为大写形式
 * @param text 输入字符串
 * @return 大写字符串
 */
std::string script_upper_copy(std::string_view text) {
  std::string upper{text};
  std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return upper;
}

/**
 * @brief 提取脚本行的命令名称（空格前的部分，去#号并大写）
 * @param line 脚本行
 * @return 大写命令名称
 */
std::string script_command_name(std::string_view line) {
  auto command = strip_script_hash(std::string(line));
  const auto space = command.find(' ');
  if (space != std::string::npos) {
    command.resize(space);
  }
  return script_upper_copy(command);
}

/**
 * @brief 提取脚本行的命令参数（空格后的部分）
 * @param line 脚本行
 * @return 命令参数字符串
 */
std::string script_command_payload(std::string_view line) {
  auto command = strip_script_hash(std::string(line));
  const auto space = command.find(' ');
  if (space == std::string::npos) {
    return {};
  }
  return util::trim(command.substr(space + 1));
}

/**
 * @brief 判断命令名是否为传统脚本的条件指令
 * @details 识别 CHECK、RANDOM、GENDER 等30多种条件指令
 * @param command_name 大写命令名
 * @return true 如果是条件指令
 */
bool is_legacy_script_condition(std::string_view command_name) {
  static constexpr std::string_view kConditions[]{
      "CHECK", "CHECKOPEN", "CHECKUNIT", "RANDOM", "GENDER", "DAYTIME", "CHECKLEVEL",
      "CHECKJOB", "CHECKITEM", "CHECKITEMW", "CHECKGOLD", "ISTAKEITEM", "CHECKDURA",
      "CHECKDURAEVA", "DAYOFWEEK", "HOUR", "MIN", "CHECKPKPOINT", "CHECKLUCKYPOINT",
      "CHECKMONMAP", "CHECKMONAREA", "CHECKHUM", "CHECKBAGGAGE", "CHECKNAMELIST",
      "CHECKIDLIST", "CHECK_DELETE_NAMELIST", "CHECK_DELETE_IDLIST", "IFGETDAILYQUEST",
      "CHECKDAILYQUEST", "RANDOMEX", "EQUAL", "LARGE", "SMALL"};
  return std::find(std::begin(kConditions), std::end(kConditions), command_name) !=
         std::end(kConditions);
}

/**
 * @brief 解析传统脚本块，切分为多个 #IF-#ACT-#SAY 流程块
 * @details 支持 #IF/#ACT/#SAY/#ELSESAY/#ELSEACT 完整的脚本流程控制结构，
 *          自动将条件行、动作行和对话行分类到对应的流程块中
 * @param text 原始脚本文本
 * @return 解析后的 LegacyScriptBlock 结构
 */
LegacyScriptBlock parse_legacy_script_block(std::string_view text) {
  LegacyScriptBlock block;
  LegacyScriptProc current;
  auto mode = LegacyScriptParseMode::say;
  auto has_content = [](const LegacyScriptProc& proc) {
    return !proc.say_lines.empty() || !proc.conditions.empty() || !proc.act_lines.empty() ||
           !proc.else_say_lines.empty() || !proc.else_act_lines.empty();
  };
  auto flush = [&]() {
    if (has_content(current)) {
      block.procs.push_back(std::move(current));
      current = LegacyScriptProc{};
      mode = LegacyScriptParseMode::say;
    }
  };
  for (auto line : split_legacy_script_lines(text)) {
    const auto command_name = script_command_name(line);
    const auto payload = script_command_payload(line);
    const auto hashed = !line.empty() && line.front() == '#';

    if (hashed && command_name == "IF") {
      if (has_content(current)) {
        flush();
      }
      mode = LegacyScriptParseMode::condition;
      if (!payload.empty()) {
        current.conditions.push_back(payload);
      }
      continue;
    }
    if (hashed && command_name == "ACT") {
      mode = LegacyScriptParseMode::act;
      if (!payload.empty()) {
        current.act_lines.push_back(payload);
      }
      continue;
    }
    if (hashed && (command_name == "ELSEACT" || command_name == "ELESACT")) {
      mode = LegacyScriptParseMode::else_act;
      if (!payload.empty()) {
        current.else_act_lines.push_back(payload);
      }
      continue;
    }
    if (hashed && command_name == "SAY") {
      mode = LegacyScriptParseMode::say;
      if (!payload.empty()) {
        current.say_lines.push_back(payload);
      }
      continue;
    }
    if (hashed && command_name == "ELSESAY") {
      mode = LegacyScriptParseMode::else_say;
      if (!payload.empty()) {
        current.else_say_lines.push_back(payload);
      }
      continue;
    }

    auto normalized_line = hashed ? strip_script_hash(std::move(line)) : std::move(line);
    const auto normalized_command = script_command_name(normalized_line);
    if (mode == LegacyScriptParseMode::condition || is_legacy_script_condition(normalized_command)) {
      current.conditions.push_back(std::move(normalized_line));
      continue;
    }
    if (mode == LegacyScriptParseMode::act) {
      current.act_lines.push_back(std::move(normalized_line));
    } else if (mode == LegacyScriptParseMode::else_act) {
      current.else_act_lines.push_back(std::move(normalized_line));
    } else if (mode == LegacyScriptParseMode::else_say) {
      current.else_say_lines.push_back(std::move(normalized_line));
    } else {
      current.say_lines.push_back(std::move(normalized_line));
    }
  }
  flush();
  return block;
}

/**
 * @brief 将多行对话文本连接为单字符串
 * @param lines 对话行列表
 * @return 用反斜杠分隔的连接字符串
 */
std::string join_dialog_lines(const std::vector<std::string>& lines) {
  std::string text;
  for (const auto& line : lines) {
    append_dialog_line(text, line);
  }
  return text;
}

/**
 * @brief 判断脚本动作是否使用了NPC已有的内置业务功能
 * @details 检查动作是否为 buy/sell/repair/storage/upgrade/guild/castle 等内置业务
 * @param lowered_payload 小写的动作负载
 * @param npc NPC对象
 * @return true 如果使用了已有业务功能
 */
bool legacy_script_action_uses_existing_business(std::string_view lowered_payload, const Npc& npc) {
  if ((lowered_payload == "@buy" && npc.supports_buy()) ||
      (lowered_payload == "@sell" && npc.supports_sell()) ||
      ((lowered_payload == "@repair" || lowered_payload == "@s_repair") &&
       npc.supports_repair()) ||
      ((lowered_payload == "@storage" || lowered_payload == "@getback") &&
       npc.supports_storage()) ||
      ((lowered_payload == "@upgradenow" || lowered_payload == "@getbackupgnow") &&
       npc.supports_weapon_upgrade())) {
    return true;
  }
  if (util::starts_with(lowered_payload, "@guild_") && npc.supports_guild()) {
    return true;
  }
  if (util::starts_with(lowered_payload, "@castle_") && npc.supports_castle()) {
    return true;
  }
  return false;
}

/**
 * @brief 按名称或ID查找物品配置
 * @details 优先尝试按ID（数字）查找，失败后按名称精确匹配
 * @param item_configs 物品配置表
 * @param value 物品名称或ID字符串
 * @return 物品配置指针，未找到返回 nullptr
 */
const ItemConfig* find_item_config_by_name_or_id(
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs, std::string_view value) {
  const auto maybe_id = parse_int32(util::trim(std::string(value)));
  if (maybe_id.has_value()) {
    return find_item_config(item_configs, *maybe_id);
  }

  const auto wanted = util::lower_copy(util::trim(std::string(value)));
  const ItemConfig* best = nullptr;
  for (const auto& [id, item_config] : item_configs) {
    if (util::lower_copy(item_config.name) != wanted) {
      continue;
    }
    if (best == nullptr || id < best->id) {
      best = &item_config;
    }
  }
  return best;
}

/**
 * @struct LegacyScriptAmountTarget
 * @brief 脚本数量目标，包含目标名称和数量
 */
struct LegacyScriptAmountTarget {
  std::string target{};  ///< 目标名称（物品名或 "gold"）
  std::int32_t amount{1}; ///< 数量
};

/** @brief 传统脚本金币标记（UTF-8简体："金币"） */
constexpr std::string_view kLegacyGoldTokenUtf8 = "\xE9\x87\x91\xE5\xB8\x81";
/** @brief 传统脚本金币标记（UTF-8繁体："金幣"） */
constexpr std::string_view kLegacyGoldTokenUtf8Traditional = "\xE9\x87\x91\xE5\xB9\xA3";
/** @brief 传统脚本金币标记（GBK编码："金币"） */
constexpr std::string_view kLegacyGoldTokenGbk = "\xBD\xF0\xB1\xD2";

/**
 * @brief 分割脚本参数令牌，支持引号括起的参数（引号内的空格不被分割）
 * @param payload 原始参数字符串
 * @return 令牌列表
 */
std::vector<std::string> split_script_tokens(std::string_view payload) {
  std::vector<std::string> tokens;
  std::string current;
  bool quoted = false;
  for (const auto ch : payload) {
    if (ch == '"') {
      quoted = !quoted;
      continue;
    }
    if (!quoted && std::isspace(static_cast<unsigned char>(ch)) != 0) {
      if (!current.empty()) {
        tokens.push_back(std::move(current));
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    tokens.push_back(std::move(current));
  }
  return tokens;
}

/**
 * @brief 判断脚本令牌是否为金币标记
 * @details 支持 "gold"、简体中文"金币"、繁体中文"金幣"和GBK编码的金币标记
 * @param token 脚本令牌
 * @return true 如果是金币标记
 */
bool is_legacy_script_gold_token(std::string_view token) {
  const auto normalized = util::lower_copy(util::trim(std::string(token)));
  return normalized == "gold" || normalized == kLegacyGoldTokenUtf8 ||
         normalized == kLegacyGoldTokenUtf8Traditional || normalized == kLegacyGoldTokenGbk;
}

/**
 * @brief 解析附加在金币标记后的数量（如 "gold500" -> 500）
 * @param payload 参数字符串
 * @return 解析成功返回 LegacyScriptAmountTarget，否则返回 std::nullopt
 */
std::optional<LegacyScriptAmountTarget> parse_attached_gold_amount(std::string_view payload) {
  const auto normalized = util::lower_copy(util::trim(std::string(payload)));
  for (const auto coin : {std::string_view{"gold"}, kLegacyGoldTokenUtf8,
                          kLegacyGoldTokenUtf8Traditional, kLegacyGoldTokenGbk}) {
    if (normalized.size() <= coin.size() || !util::starts_with(normalized, coin)) {
      continue;
    }
    const auto suffix = util::trim(std::string(normalized.substr(coin.size())));
    const auto amount = parse_int32(suffix);
    if (!amount.has_value()) {
      continue;
    }
    LegacyScriptAmountTarget target;
    target.target = std::string(coin);
    target.amount = std::max(*amount, 0);
    return target;
  }
  return std::nullopt;
}

/**
 * @brief 去除脚本标记的方括号（如 "[ITEM]" -> "ITEM"）
 * @param token 原始标记字符串
 * @return 去除方括号后的字符串
 */
std::string strip_legacy_mark_token(std::string token) {
  token = util::trim(std::move(token));
  if (token.size() >= 2 && token.front() == '[' && token.back() == ']') {
    token = token.substr(1, token.size() - 2);
  }
  return util::trim(std::move(token));
}

/**
 * @brief 解析脚本索引值（支持带方括号的格式 [N]）
 * @param token 原始令牌
 * @return 解析成功的索引值
 */
std::optional<std::int32_t> parse_script_index(std::string_view token) {
  return parse_int32(strip_legacy_mark_token(std::string(token)));
}

/**
 * @brief 解析脚本数量目标（物品名+数量的组合）
 * @details 解析格式如 "ItemName 5" 或附加金币格式 "gold500"
 * @param payload 原始负载字符串
 * @return 解析后的 LegacyScriptAmountTarget
 */
LegacyScriptAmountTarget parse_script_amount_target(std::string_view payload) {
  LegacyScriptAmountTarget target;
  const auto tokens = split_script_tokens(payload);
  if (tokens.empty()) {
    return target;
  }
  if (tokens.size() > 1) {
    const auto maybe_amount = parse_int32(tokens.back());
    if (maybe_amount.has_value()) {
      target.amount = std::max(*maybe_amount, 0);
      target.target = util::trim(join_tokens(tokens, 0, " "));
      const auto suffix = std::string(" ") + tokens.back();
      if (target.target.size() >= suffix.size() &&
          target.target.substr(target.target.size() - suffix.size()) == suffix) {
        target.target.resize(target.target.size() - suffix.size());
        target.target = util::trim(std::move(target.target));
      }
      return target;
    }
  }
  if (tokens.size() == 1) {
    if (auto attached = parse_attached_gold_amount(tokens.front()); attached.has_value()) {
      return *attached;
    }
  }
  target.target = util::trim(std::string(payload));
  return target;
}

/**
 * @brief 统计玩家背包中指定名称的物品数量
 * @param player 玩家对象
 * @param item_name_text 物品名称
 * @param item_configs 物品配置表
 * @return 物品数量
 */
std::int32_t count_player_bag_items_by_name(
    const Player& player, std::string_view item_name_text,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto wanted = util::lower_copy(util::trim(std::string(item_name_text)));
  std::int32_t count = 0;
  for (const auto& item : player.character().bag_items) {
    if (!is_empty(item) && util::lower_copy(item_name(item, item_configs)) == wanted) {
      ++count;
    }
  }
  return count;
}

/**
 * @brief 统计玩家已装备的指定名称物品数量
 * @param player 玩家对象
 * @param item_name_text 物品名称
 * @param item_configs 物品配置表
 * @return 已装备的物品数量
 */
std::int32_t count_player_equipped_items_by_name(
    const Player& player, std::string_view item_name_text,
    const std::unordered_map<std::int32_t, ItemConfig>& item_configs) {
  const auto wanted = util::lower_copy(util::trim(std::string(item_name_text)));
  std::int32_t count = 0;
  for (const auto& item : player.character().equipped_items) {
    if (!is_empty(item) && util::lower_copy(item_name(item, item_configs)) == wanted) {
      ++count;
    }
  }
  return count;
}

/**
 * @brief 根据装备别名获取对应的装备槽位列表
 * @details 将传统脚本中的装备别名（如 "DRESS"、"WEAPON"、"RING" 等）映射到对应的装备槽位索引。
 *          "ARMRING"/"BRACELET" 返回左右手镯两个槽位，"RING" 返回左右戒指两个槽位，
 *          其余别名返回单个槽位。不认识的别名返回空列表。
 * @param alias_text 装备别名文本（如 "DRESS"、"WEAPON"、"RING"）
 * @return 对应的装备槽位索引列表
 * @see kEquipDress, kEquipWeapon, kEquipRingLeft
 */
std::vector<std::size_t> legacy_equipment_slots_for_alias(std::string_view alias_text) {
  const auto alias = script_upper_copy(strip_legacy_mark_token(std::string(alias_text)));
  if (alias == "DRESS" || alias == "ARMOUR" || alias == "ARMOR") {
    return {kEquipDress};
  }
  if (alias == "WEAPON") {
    return {kEquipWeapon};
  }
  if (alias == "RIGHTHAND" || alias == "RIGHT" || alias == "TORCH") {
    return {kEquipRightHand};
  }
  if (alias == "NECKLACE" || alias == "NECK") {
    return {kEquipNecklace};
  }
  if (alias == "HELMET" || alias == "HELM") {
    return {kEquipHelmet};
  }
  if (alias == "ARMRING" || alias == "BRACELET") {
    return {kEquipArmRingLeft, kEquipArmRingRight};
  }
  if (alias == "RING") {
    return {kEquipRingLeft, kEquipRingRight};
  }
  if (alias == "BUJUK") {
    return {kEquipBujuk};
  }
  if (alias == "BELT") {
    return {kEquipBelt};
  }
  if (alias == "BOOTS" || alias == "BOOT") {
    return {kEquipBoots};
  }
  if (alias == "CHARM") {
    return {kEquipCharm};
  }
  return {};
}

/**
 * @brief JSON 字符串转义
 * @details 将字符串中的特殊字符（反斜杠、双引号、换行符、回车符、制表符）转义为 JSON 兼容的转义序列
 * @param text 原始文本
 * @return 转义后的 JSON 安全字符串
 */
std::string json_escape(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size());
  for (const auto ch : text) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

/**
 * @brief 连接令牌列表为字符串
 * @details 从指定起始索引开始，用指定分隔符连接令牌列表中的元素
 * @param tokens 令牌列表
 * @param start_index 起始索引
 * @param separator 分隔符
 * @return 连接后的字符串
 * @see split_script_tokens
 */
std::string join_tokens(const std::vector<std::string>& tokens, std::size_t start_index,
                        std::string_view separator) {
  std::string joined;
  for (std::size_t index = start_index; index < tokens.size(); ++index) {
    if (!joined.empty()) {
      joined += separator;
    }
    joined += tokens[index];
  }
  return joined;
}

/**
 * @brief 将字符串解析为 int32 整数
 * @details 使用 std::from_chars 进行高性能整数解析，要求整个字符串完全匹配数字格式
 * @param text 待解析的字符串
 * @return 解析成功返回 int32 值，失败返回 std::nullopt
 */
std::optional<std::int32_t> parse_int32(std::string_view text) {
  std::int32_t value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto [ptr, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  return value;
}

/**
 * @brief 判断账号是否为管理员账号
 * @details 管理员账号包括 "guest"、"admin" 以及所有以 "gm" 开头的账号（不区分大小写）
 * @param account_id 账号ID
 * @return true 如果是管理员账号
 * @note 仅用于 GM 城堡管理命令的权限检查，不涉及游戏内的管理员系统
 */
bool is_admin_account(std::string_view account_id) {
  const auto lowered = util::lower_copy(account_id);
  return lowered == "guest" || lowered == "admin" || util::starts_with(lowered, "gm");
}

/**
 * @brief 获取默认的无人认领城堡拥有者名称
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认拥有者名称
 */
std::string default_unclaimed_castle_owner(const CastleDialogContext& castle_dialog_context);

/**
 * @brief 标准化城堡拥有者名称
 * @details 将空值、"none"、"unclaimed"、"-" 等特殊值统一转换为空字符串
 * @param castle_dialog_context 城堡对话框上下文
 * @param owner 原始拥有者名称
 * @return 标准化后的拥有者名称
 */
std::string normalize_castle_owner(const CastleDialogContext& castle_dialog_context,
                                   std::string owner) {
  const auto lowered = util::lower_copy(owner);
  if (lowered.empty() || lowered == "none" || lowered == "unclaimed" || lowered == "-" ||
      lowered == util::lower_copy(default_unclaimed_castle_owner(castle_dialog_context))) {
    return {};
  }
  return owner;
}

/**
 * @brief 解析城堡战争列表
 * @param castle_dialog_context 城堡对话框上下文
 * @return 战争行会名称列表
 */
std::vector<std::string> parse_castle_war_list(const CastleDialogContext& castle_dialog_context);
/**
 * @brief 汇总城堡战争列表为字符串
 * @param castle_dialog_context 城堡对话框上下文
 * @return 战争汇总字符串
 */
std::string summarize_castle_wars(const CastleDialogContext& castle_dialog_context);
/**
 * @brief 描述城堡拥有者角色名称
 * @param castle_dialog_context 城堡对话框上下文
 * @return 角色名称
 */
std::string describe_castle_owner_role(const CastleDialogContext& castle_dialog_context);
/**
 * @brief 显示城堡拥有者行会名称
 * @param castle_dialog_context 城堡对话框上下文
 * @return 拥有者名称
 */
std::string display_castle_owner(const CastleDialogContext& castle_dialog_context);
/**
 * @brief 显示城堡领主名称
 * @param castle_dialog_context 城堡对话框上下文
 * @return 领主名称
 */
std::string display_castle_lord(const CastleDialogContext& castle_dialog_context);

/**
 * @brief 获取默认城堡名称
 * @details 如果上下文中的城堡名为空，则返回默认名称 "Sabuk"
 * @param castle_dialog_context 城堡对话框上下文
 * @return 城堡名称
 */
std::string default_castle_name(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.castle_name.empty() ? std::string("Sabuk")
                                                   : castle_dialog_context.castle_name;
}

/**
 * @brief 获取默认的无人认领城堡拥有者标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_unclaimed_castle_owner(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.unclaimed_owner_label.empty() ? std::string("Unclaimed")
                                                             : castle_dialog_context.unclaimed_owner_label;
}

/**
 * @brief 获取默认的无人认领城堡领主标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_unclaimed_castle_lord(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.unclaimed_lord_label.empty() ? std::string("Unclaimed")
                                                            : castle_dialog_context.unclaimed_lord_label;
}

/**
 * @brief 获取默认的城堡拥有者角色标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_owner_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.owner_role_label.empty() ? std::string("Castle Owner")
                                                        : castle_dialog_context.owner_role_label;
}

/**
 * @brief 获取默认的城堡拥有者行会角色标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_owner_guild_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.owner_guild_role_label.empty() ? std::string("Owner")
                                                              : castle_dialog_context.owner_guild_role_label;
}

/**
 * @brief 获取默认的城堡挑战者角色标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_challenger_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.challenger_role_label.empty() ? std::string("Challenger")
                                                             : castle_dialog_context.challenger_role_label;
}

/**
 * @brief 获取默认的城堡竞争对手角色标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_rival_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.rival_role_label.empty() ? std::string("Rival")
                                                        : castle_dialog_context.rival_role_label;
}

/**
 * @brief 获取默认的城堡未知角色标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_unknown_role_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.unknown_role_label.empty() ? std::string("Unknown")
                                                          : castle_dialog_context.unknown_role_label;
}

/**
 * @brief 获取默认的城堡战争已列表标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_war_entry_listed_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_entry_listed_label.empty() ? std::string("Listed")
                                                              : castle_dialog_context.war_entry_listed_label;
}

/**
 * @brief 获取默认的城堡战争未列表标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_war_entry_unlisted_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_entry_unlisted_label.empty() ? std::string("Not Listed")
                                                                : castle_dialog_context.war_entry_unlisted_label;
}

/**
 * @brief 获取默认的城堡战争状态活跃标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_war_status_active_label(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_status_active_label.empty() ? std::string("Active")
                                                               : castle_dialog_context.war_status_active_label;
}

/**
 * @brief 获取默认的城堡战争状态可用标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_war_status_available_label(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_status_available_label.empty() ? std::string("Available")
                                                                  : castle_dialog_context.war_status_available_label;
}

/**
 * @brief 获取默认的城堡角色变更为拥有者标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_role_change_owner_label(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.role_change_owner_label.empty() ? std::string("Castle Owner")
                                                               : castle_dialog_context.role_change_owner_label;
}

/**
 * @brief 获取默认的城堡角色变更为挑战者标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 默认标签文字
 */
std::string default_castle_role_change_challenger_label(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.role_change_challenger_label.empty()
             ? std::string("Castle Challenger")
             : castle_dialog_context.role_change_challenger_label;
}

/**
 * @brief 获取默认的城堡认领摘要模板
 * @details 模板中可使用 <$GUILD> 占位符表示行会名称
 * @param castle_dialog_context 城堡对话框上下文
 * @return 摘要模板字符串
 */
std::string default_castle_claim_summary_template(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.claim_summary_template.empty()
             ? std::string("Castle claimed for guild <$GUILD>.")
             : castle_dialog_context.claim_summary_template;
}

/**
 * @brief 获取默认的城堡战争摘要模板
 * @details 模板中可使用 <$TARGETGUILD>、<$GOLD> 等占位符
 * @param castle_dialog_context 城堡对话框上下文
 * @return 摘要模板字符串
 */
std::string default_castle_war_summary_template(
    const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.war_summary_template.empty()
             ? std::string("Castle war declared against <$TARGETGUILD> for <$GOLD> gold.")
             : castle_dialog_context.war_summary_template;
}

/**
 * @brief 获取默认的城堡战争日期
 * @param castle_dialog_context 城堡对话框上下文
 * @return 战争日期字符串
 */
std::string default_castle_war_date(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.castle_war_date.empty() ? std::string("Unknown")
                                                       : castle_dialog_context.castle_war_date;
}

/**
 * @brief 渲染城堡摘要模板
 * @details 替换模板中的占位符（<$CASTLE>、<$GUILD>、<$TARGETGUILD>、<$GOLD>、<$OWNERGUILD>、<$LORD>）
 * @param text 模板文本
 * @param castle_dialog_context 城堡对话框上下文
 * @param guild_name 行会名称
 * @param target_guild_name 目标行会名称
 * @param gold 金币数量
 * @return 渲染后的文本
 */
std::string render_castle_summary_template(std::string text,
                                           const CastleDialogContext& castle_dialog_context,
                                           std::string_view guild_name = {},
                                           std::string_view target_guild_name = {},
                                           std::int32_t gold = 0) {
  replace_all(text, "<$CASTLE>", default_castle_name(castle_dialog_context));
  replace_all(text, "<$GUILD>", std::string(guild_name));
  replace_all(text, "<$TARGETGUILD>", std::string(target_guild_name));
  replace_all(text, "<$GOLD>", std::to_string(gold));
  replace_all(text, "<$OWNERGUILD>", display_castle_owner(castle_dialog_context));
  replace_all(text, "<$LORD>", display_castle_lord(castle_dialog_context));
  return text;
}

/**
 * @brief 选择配置模板或回退模板
 * @details 如果配置的模板不为空则使用配置模板，否则使用回退模板
 * @param configured 配置的模板
 * @param fallback 回退模板
 * @return 选中的模板字符串
 */
std::string configured_summary_template(std::string_view configured, std::string_view fallback) {
  return configured.empty() ? std::string(fallback) : std::string(configured);
}

/**
 * @brief 渲染行会摘要模板
 * @details 替换模板中的占位符（<$GUILD>、<$TARGET>、<$TITLE>、<$NEWLORD>、<$GOLD>）
 * @param text 模板文本
 * @param guild_name 行会名称
 * @param target_name 目标名称
 * @param title_name 头衔名称
 * @param new_lord 新领主名称
 * @param gold 金币数量
 * @return 渲染后的文本
 */
std::string render_guild_summary_template(std::string text, std::string_view guild_name = {},
                                          std::string_view target_name = {},
                                          std::string_view title_name = {},
                                          std::string_view new_lord = {},
                                          std::int32_t gold = 0) {
  replace_all(text, "<$GUILD>", std::string(guild_name));
  replace_all(text, "<$TARGET>", std::string(target_name));
  replace_all(text, "<$TITLE>", std::string(title_name));
  replace_all(text, "<$NEWLORD>", std::string(new_lord));
  replace_all(text, "<$GOLD>", std::to_string(gold));
  return text;
}

/**
 * @brief 渲染行会通知模板
 * @details 是 render_guild_summary_template 的简化版本，仅处理行会名称、目标名称和头衔
 * @param text 模板文本
 * @param guild_name 行会名称
 * @param target_name 目标名称
 * @param title_name 头衔名称
 * @return 渲染后的文本
 * @see render_guild_summary_template
 */
std::string render_guild_notice_template(std::string text, std::string_view guild_name = {},
                                         std::string_view target_name = {},
                                         std::string_view title_name = {}) {
  return render_guild_summary_template(std::move(text), guild_name, target_name, title_name);
}

/**
 * @brief 获取无活跃战争时的显示文字
 * @param castle_dialog_context 城堡对话框上下文
 * @return 无战争文本
 */
std::string no_active_wars_text(const CastleDialogContext& castle_dialog_context) {
  return castle_dialog_context.no_active_wars_text.empty() ? std::string("No active wars.")
                                                           : castle_dialog_context.no_active_wars_text;
}

/**
 * @brief 显示城堡战争信息
 * @param castle_dialog_context 城堡对话框上下文
 * @return 战争信息字符串
 */
std::string display_castle_wars(const CastleDialogContext& castle_dialog_context) {
  return parse_castle_war_list(castle_dialog_context).empty() ? no_active_wars_text(castle_dialog_context)
                                                              : castle_dialog_context.list_of_war;
}

/**
 * @brief 判断城堡是否无人认领
 * @param castle_dialog_context 城堡对话框上下文
 * @return true 如果拥有者行会名称为空
 */
bool is_unclaimed_castle_owner(const CastleDialogContext& castle_dialog_context) {
  return util::trim(castle_dialog_context.owner_guild).empty();
}

/**
 * @brief 显示城堡拥有者
 * @details 如果无人认领则显示"无人认领"标签，否则显示行会名称
 * @param castle_dialog_context 城堡对话框上下文
 * @return 拥有者显示文本
 */
std::string display_castle_owner(const CastleDialogContext& castle_dialog_context) {
  return is_unclaimed_castle_owner(castle_dialog_context)
             ? default_unclaimed_castle_owner(castle_dialog_context)
             : castle_dialog_context.owner_guild;
}

/**
 * @brief 显示城堡领主名称
 * @details 如果城堡无人认领或领主名为空则显示"无人认领"标签
 * @param castle_dialog_context 城堡对话框上下文
 * @return 领主显示文本
 */
std::string display_castle_lord(const CastleDialogContext& castle_dialog_context) {
  return is_unclaimed_castle_owner(castle_dialog_context) ||
                 util::trim(castle_dialog_context.lord).empty()
             ? default_unclaimed_castle_lord(castle_dialog_context)
             : castle_dialog_context.lord;
}

/**
 * @brief 构建城堡 JSON 数据负载
 * @details 将城堡状态序列化为 JSON 格式字符串，用于持久化保存。
 *          支持通过可选的覆写参数替换上下文中的值。
 * @param castle_dialog_context 城堡对话框上下文
 * @param owner_guild_override 拥有者行会覆写值
 * @param war_date_override 战争日期覆写值
 * @param wars_override 战争列表覆写值
 * @param guild_fee_override 行会战争费用覆写值
 * @param upgrade_fee_override 武器升级费用覆写值
 * @param guild_create_fee_override 行会创建费用覆写值
 * @return JSON 格式的城堡状态字符串
 * @see queue_save_castle_state
 */
std::string build_castle_payload(const CastleDialogContext& castle_dialog_context,
                                 std::optional<std::string> owner_guild_override = std::nullopt,
                                 std::optional<std::string> war_date_override = std::nullopt,
                                 std::optional<std::string> wars_override = std::nullopt,
                                 std::optional<std::int32_t> guild_fee_override = std::nullopt,
                                 std::optional<std::int32_t> upgrade_fee_override = std::nullopt,
                                 std::optional<std::int32_t> guild_create_fee_override =
                                     std::nullopt) {
  const auto owner_guild = owner_guild_override.value_or(castle_dialog_context.owner_guild);
  const auto war_date = war_date_override.value_or(castle_dialog_context.castle_war_date);
  const auto wars = wars_override.value_or(castle_dialog_context.list_of_war);
  const auto guild_fee = guild_fee_override.value_or(castle_dialog_context.guild_war_fee);
  const auto upgrade_fee = upgrade_fee_override.value_or(castle_dialog_context.upgrade_weapon_fee);
  const auto guild_create_fee =
      guild_create_fee_override.value_or(castle_dialog_context.guild_create_fee);

  std::ostringstream payload;
  payload << "{\"owner_guild\":\"" << json_escape(owner_guild) << "\""
          << ",\"castle_war_date\":\"" << json_escape(war_date) << "\""
          << ",\"list_of_war\":\"" << json_escape(wars) << "\""
          << ",\"guild_war_fee\":" << guild_fee
          << ",\"upgrade_weapon_fee\":" << upgrade_fee
          << ",\"guild_create_fee\":" << guild_create_fee << "}";
  return payload.str();
}

/**
 * @brief 构建城堡信息显示行
 * @details 生成包含城堡名称、拥有者、领主、角色、战争日期、战争数量和费用的 Key=Value 格式信息行
 * @param castle_dialog_context 城堡对话框上下文
 * @return 信息行字符串
 * @see build_castle_show_dialog_text
 */
std::string build_castle_show_line(const CastleDialogContext& castle_dialog_context) {
  const auto wars = parse_castle_war_list(castle_dialog_context);
  std::ostringstream line;
  line << "Castle=" << default_castle_name(castle_dialog_context)
       << " Owner=" << display_castle_owner(castle_dialog_context)
       << " Lord=" << display_castle_lord(castle_dialog_context)
       << " OwnerRole=" << describe_castle_owner_role(castle_dialog_context)
       << " WarDate=" << default_castle_war_date(castle_dialog_context)
       << " WarCount=" << wars.size()
       << " WarPreview=" << summarize_castle_wars(castle_dialog_context)
       << " Fees=" << castle_dialog_context.guild_war_fee << "/"
       << castle_dialog_context.upgrade_weapon_fee;
  return line.str();
}

/**
 * @brief 忽略大小写比较两个字符串
 * @param lhs 左侧字符串
 * @param rhs 右侧字符串
 * @return true 如果忽略大小写后相等
 */
bool equals_ignore_case(std::string_view lhs, std::string_view rhs) {
  return util::lower_copy(lhs) == util::lower_copy(rhs);
}

/**
 * @brief 投递系统通知消息到玩家
 * @details 创建系统通知包并通过 dispatch 队列发送给指定玩家
 * @param dispatch 运行时调度器
 * @param player 目标玩家
 * @param message 通知消息内容
 * @see queue_packet, make_system_notice_packet
 */
void queue_system_notice(RuntimeDispatch& dispatch, const Player& player, std::string message) {
  queue_packet(dispatch, player.session_id(),
               make_system_notice_packet(player.session_id(), std::move(message)));
}

/**
 * @brief 投递玩家角色保存请求
 * @param dispatch 运行时调度器
 * @param player 需要保存的玩家对象
 */
void queue_save_character(RuntimeDispatch& dispatch, const Player& player) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_character;
  request.account_id = player.character().account_id;
  request.character_name = player.character().character_name;
  request.character = player.persistent_snapshot();
  dispatch.persist_requests.push_back(std::move(request));
}

/**
 * @brief 投递角色记录保存请求（重载版，接受 CharacterRecord）
 * @param dispatch 运行时调度器
 * @param character 需要保存的角色记录
 */
void queue_save_character(RuntimeDispatch& dispatch, const CharacterRecord& character) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_character;
  request.account_id = character.account_id;
  request.character_name = character.character_name;
  request.character = character;
  dispatch.persist_requests.push_back(std::move(request));
}

/**
 * @brief 投递行会状态保存请求
 * @param dispatch 运行时调度器
 * @param guild_state 需要保存的行会状态
 */
void queue_save_guild_state(RuntimeDispatch& dispatch, const GuildState& guild_state) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_guild_state;
  request.reply_to = "world_service";
  request.guild_name = guild_state.guild_name;
  request.guild_state = guild_state;
  dispatch.persist_requests.push_back(std::move(request));
}

/**
 * @brief 投递行会删除请求
 * @param dispatch 运行时调度器
 * @param guild_name 需要删除的行会名称
 */
void queue_delete_guild(RuntimeDispatch& dispatch, std::string guild_name) {
  PersistRequest request;
  request.kind = PersistRequestKind::delete_guild;
  request.reply_to = "world_service";
  request.guild_name = std::move(guild_name);
  dispatch.persist_requests.push_back(std::move(request));
}

/**
 * @brief 投递城堡状态保存请求
 * @details 使用 build_castle_payload 构建 JSON 数据作为持久化负载
 * @param dispatch 运行时调度器
 * @param castle_dialog_context 城堡对话框上下文
 * @see build_castle_payload
 */
void queue_save_castle_state(RuntimeDispatch& dispatch,
                             const CastleDialogContext& castle_dialog_context) {
  PersistRequest request;
  request.kind = PersistRequestKind::save_castle_state;
  request.reply_to = "world_service";
  request.castle_name = default_castle_name(castle_dialog_context);
  request.payload_json = build_castle_payload(castle_dialog_context);
  dispatch.persist_requests.push_back(std::move(request));
}

/**
 * @brief 投递离线行会角色数据加载请求
 * @details 当需要处理离线玩家的行会操作（如审批、踢出、转让头衔）时，
 *          通过此函数异步加载角色数据。操作信息编码在 request_id 中。
 * @param dispatch 运行时调度器
 * @param operation 离线行会角色操作描述
 * @see OfflineGuildCharacterOp, OfflineGuildCharacterOpKind
 */
void queue_load_offline_guild_character(RuntimeDispatch& dispatch,
                                        const OfflineGuildCharacterOp& operation) {
  PersistRequest request;
  request.kind = PersistRequestKind::load_character_by_name;
  request.reply_to = "world_service";
  request.character_name = operation.target_name;
  request.request_id = encode_offline_guild_character_op(operation);
  dispatch.persist_requests.push_back(std::move(request));
}

/**
 * @brief 跨地图投递系统通知消息
 * @details 通过跨地图邮件系统将通知消息发送给指定地图中的目标角色
 * @param dispatch 运行时调度器
 * @param map_id 目标地图ID
 * @param actor_id 目标角色ID
 * @param message 通知消息内容
 * @note 如果地图ID为空、角色ID为0或消息为空则跳过
 */
void queue_cross_map_notice(RuntimeDispatch& dispatch, std::string map_id, std::uint64_t actor_id,
                            std::string message) {
  if (map_id.empty() || actor_id == 0 || message.empty()) {
    return;
  }
  ActorMail mail;
  mail.kind = ActorMailKind::system_notice;
  mail.map_id = std::move(map_id);
  mail.actor_id = actor_id;
  mail.payload = std::move(message);
  dispatch.cross_map_mails.push_back(std::move(mail));
}

/**
 * @brief 跨地图投递行会成员关系同步消息
 * @details 当玩家在不同地图间移动时，通过此函数同步其行会成员关系变更
 * @param dispatch 运行时调度器
 * @param map_id 目标地图ID
 * @param actor_id 目标角色ID
 * @param character 更新的角色记录
 * @param notice 通知消息内容
 * @note 如果地图ID为空或角色ID为0则跳过
 */
void queue_cross_map_guild_membership_sync(RuntimeDispatch& dispatch, std::string map_id,
                                           std::uint64_t actor_id, CharacterRecord character,
                                           std::string notice) {
  if (map_id.empty() || actor_id == 0) {
    return;
  }
  ActorMail mail;
  mail.kind = ActorMailKind::guild_membership_sync;
  mail.map_id = std::move(map_id);
  mail.actor_id = actor_id;
  mail.character = std::move(character);
  mail.payload = std::move(notice);
  dispatch.cross_map_mails.push_back(std::move(mail));
}

/**
 * @brief 设置角色行会成员关系
 * @param character 角色记录
 * @param guild_name 行会名称
 * @param guild_title 行会头衔
 */
void set_character_guild_membership(CharacterRecord& character, std::string guild_name,
                                    std::string guild_title) {
  character.guild_name = std::move(guild_name);
  character.guild_title = std::move(guild_title);
}

/**
 * @brief 清除角色行会成员关系
 * @param character 角色记录
 */
void clear_character_guild_membership(CharacterRecord& character) {
  character.guild_name.clear();
  character.guild_title.clear();
}

/**
 * @brief 根据名称查找行会状态（可变版本）
 * @param guild_castle_snapshot 行会城堡快照
 * @param guild_name 行会名称（忽略大小写）
 * @return 指向 GuildState 的指针，未找到返回 nullptr
 */
GuildState* find_guild_state(GuildCastleSnapshot& guild_castle_snapshot, std::string_view guild_name) {
  for (auto& guild_state : guild_castle_snapshot.guilds) {
    if (equals_ignore_case(guild_state.guild_name, guild_name)) {
      return &guild_state;
    }
  }
  return nullptr;
}

const GuildState* find_guild_state(const GuildCastleSnapshot& guild_castle_snapshot,
                                   std::string_view guild_name) {
  for (const auto& guild_state : guild_castle_snapshot.guilds) {
    if (equals_ignore_case(guild_state.guild_name, guild_name)) {
      return &guild_state;
    }
  }
  return nullptr;
}

/**
 * @brief 根据角色名查找在线玩家（可变版本）
 * @param objects 游戏对象映射表
 * @param character_name 角色名（忽略大小写）
 * @return 指向 Player 的指针，未找到返回 nullptr
 */
Player* find_online_player_by_name(
    std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    std::string_view character_name) {
  for (auto& [_, object] : objects) {
    auto* player = as_player(object.get());
    if (player != nullptr &&
        equals_ignore_case(player->character().character_name, character_name)) {
      return player;
    }
  }
  return nullptr;
}

/**
 * @brief 根据角色名查找在线玩家（常量版本）
 * @param objects 游戏对象映射表
 * @param character_name 角色名（忽略大小写）
 * @return 指向 const Player 的指针，未找到返回 nullptr
 */
const Player* find_online_player_by_name(
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    std::string_view character_name) {
  for (const auto& [_, object] : objects) {
    const auto* player = as_player(object.get());
    if (player != nullptr &&
        equals_ignore_case(player->character().character_name, character_name)) {
      return player;
    }
  }
  return nullptr;
}

/**
 * @brief 检查行会是否拥有指定成员
 * @param guild_state 行会状态
 * @param member_name 成员名称（忽略大小写）
 * @return true 如果该成员在行会中
 */
bool guild_has_member(const GuildState& guild_state, std::string_view member_name) {
  return std::any_of(guild_state.members.begin(), guild_state.members.end(),
                     [&](const std::string& member) {
                       return equals_ignore_case(member, member_name);
                     });
}

/**
 * @brief 检查行会是否有指定申请者
 * @param guild_state 行会状态
 * @param applicant_name 申请者名称（忽略大小写）
 * @return true 如果有该申请者
 */
bool guild_has_applicant(const GuildState& guild_state, std::string_view applicant_name) {
  return std::any_of(guild_state.applicants.begin(), guild_state.applicants.end(),
                     [&](const std::string& applicant) {
                       return equals_ignore_case(applicant, applicant_name);
                     });
}

/**
 * @brief 向行会添加成员
 * @details 自动去除前后空格，跳过空名称和已存在的成员
 * @param guild_state 行会状态
 * @param member_name 成员名称
 * @see guild_has_member
 */
void add_guild_member(GuildState& guild_state, std::string member_name) {
  member_name = util::trim(std::move(member_name));
  if (member_name.empty() || guild_has_member(guild_state, member_name)) {
    return;
  }
  guild_state.members.push_back(std::move(member_name));
}

/**
 * @brief 向行会添加申请者
 * @details 自动去除前后空格，跳过空名称、已是成员或已有申请的申请者
 * @param guild_state 行会状态
 * @param applicant_name 申请者名称
 */
void add_guild_applicant(GuildState& guild_state, std::string applicant_name) {
  applicant_name = util::trim(std::move(applicant_name));
  if (applicant_name.empty() || guild_has_member(guild_state, applicant_name) ||
      guild_has_applicant(guild_state, applicant_name)) {
    return;
  }
  guild_state.applicants.push_back(std::move(applicant_name));
}

/**
 * @brief 从行会移除成员
 * @param guild_state 行会状态
 * @param member_name 要移除的成员名称（忽略大小写）
 */
void remove_guild_member(GuildState& guild_state, std::string_view member_name) {
  guild_state.members.erase(
      std::remove_if(guild_state.members.begin(), guild_state.members.end(),
                     [&](const std::string& member) {
                       return equals_ignore_case(member, member_name);
                     }),
      guild_state.members.end());
}

/**
 * @brief 从行会移除申请者
 * @param guild_state 行会状态
 * @param applicant_name 要移除的申请者名称（忽略大小写）
 */
void remove_guild_applicant(GuildState& guild_state, std::string_view applicant_name) {
  guild_state.applicants.erase(
      std::remove_if(guild_state.applicants.begin(), guild_state.applicants.end(),
                     [&](const std::string& applicant) {
                       return equals_ignore_case(applicant, applicant_name);
                     }),
      guild_state.applicants.end());
}

/**
 * @brief 从拥有者行会同步城堡领主信息
 * @details 标准化拥有者名称后，如果城堡已认领则从拥有者行会中查找领主名称；
 *          如果无人认领则清空领主信息
 * @param guild_castle_snapshot 行会城堡快照
 * @see normalize_castle_owner, find_guild_state
 */
void sync_castle_lord_from_owner(GuildCastleSnapshot& guild_castle_snapshot) {
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  castle_dialog_context.owner_guild =
      normalize_castle_owner(castle_dialog_context, castle_dialog_context.owner_guild);
  if (is_unclaimed_castle_owner(castle_dialog_context)) {
    castle_dialog_context.lord.clear();
    return;
  }
  if (const auto* owner_guild =
          find_guild_state(guild_castle_snapshot, castle_dialog_context.owner_guild);
      owner_guild != nullptr && !owner_guild->lord.empty()) {
    castle_dialog_context.lord = owner_guild->lord;
  } else {
    castle_dialog_context.lord.clear();
  }
}

/**
 * @brief 解析城堡战争行会列表
 * @details 从逗号分隔的战争列表中解析出参与战争的行会名称，
 *          排除空条目和"无活跃战争"文本
 * @param castle_dialog_context 城堡对话框上下文
 * @return 战争行会名称列表
 */
std::vector<std::string> parse_castle_war_list(const CastleDialogContext& castle_dialog_context) {
  const auto trimmed = util::trim(castle_dialog_context.list_of_war);
  if (trimmed.empty() || equals_ignore_case(trimmed, "No active wars.") ||
      equals_ignore_case(trimmed, no_active_wars_text(castle_dialog_context))) {
    return {};
  }
  auto wars = util::split(trimmed, ',');
  wars.erase(std::remove_if(wars.begin(), wars.end(), [](const std::string& name) {
               return name.empty();
             }),
             wars.end());
  return wars;
}

/**
 * @brief 描述行会在城堡战争中的角色
 * @param castle_dialog_context 城堡对话框上下文
 * @param guild_name 行会名称
 * @return 角色描述字符串（"Owner"、"Challenger"、"Rival" 或 "Unaffiliated"）
 */
std::string describe_castle_guild_role(const CastleDialogContext& castle_dialog_context,
                                       std::string_view guild_name) {
  const auto normalized_name = util::trim(std::string(guild_name));
  if (normalized_name.empty()) {
    return "Unaffiliated";
  }
  if (equals_ignore_case(castle_dialog_context.owner_guild, normalized_name)) {
    return default_castle_owner_guild_role_label(castle_dialog_context);
  }

  const auto wars = parse_castle_war_list(castle_dialog_context);
  const auto listed =
      std::any_of(wars.begin(), wars.end(), [&](const std::string& war_name) {
        return equals_ignore_case(war_name, normalized_name);
      });
  return listed ? default_castle_challenger_role_label(castle_dialog_context)
                : default_castle_rival_role_label(castle_dialog_context);
}

/**
 * @brief 汇总城堡战争信息
 * @param castle_dialog_context 城堡对话框上下文
 * @return 战争汇总字符串（无战争时显示"无活跃战争"文本）
 * @see parse_castle_war_list, no_active_wars_text
 */
std::string summarize_castle_wars(const CastleDialogContext& castle_dialog_context) {
  const auto wars = parse_castle_war_list(castle_dialog_context);
  return wars.empty() ? no_active_wars_text(castle_dialog_context) : summarize_name_list(wars);
}

/**
 * @brief 描述城堡拥有者角色
 * @param castle_dialog_context 城堡对话框上下文
 * @return 角色描述字符串
 */
std::string describe_castle_owner_role(const CastleDialogContext& castle_dialog_context) {
  return is_unclaimed_castle_owner(castle_dialog_context)
             ? default_unclaimed_castle_owner(castle_dialog_context)
             : default_castle_owner_role_label(castle_dialog_context);
}

/**
 * @brief 向对话框追加城堡行会列表摘要
 * @details 显示行会的城堡角色、成员/申请者数量和成员预览
 * @param text 对话框文本
 * @param guild_castle_snapshot 行会城堡快照
 * @param guild_name 行会名称
 */
void append_castle_guild_list_summary(std::string& text,
                                      const GuildCastleSnapshot& guild_castle_snapshot,
                                      std::string_view guild_name) {
  append_dialog_line(
      text, "Role: " + describe_castle_guild_role(guild_castle_snapshot.castle_dialog, guild_name));
  if (const auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
      guild_state != nullptr) {
    append_dialog_line(text, "Members/Applicants: " +
                                 std::to_string(guild_state->members.size()) + "/" +
                                 std::to_string(guild_state->applicants.size()));
    append_dialog_line(text, "Preview: " + summarize_name_list(guild_state->members, 2));
  } else {
    append_dialog_line(text, "Guild Data: Unknown");
  }
}

/**
 * @brief 向对话框追加行会目录摘要信息
 * @details 包含城堡角色、成员/申请者数量、成员预览（最多2个）
 * @param text 对话框文本
 * @param guild_castle_snapshot 行会城堡快照
 * @param guild_state 行会状态
 * @see append_dialog_line, describe_castle_guild_role
 */
void append_guild_directory_summary(std::string& text,
                                    const GuildCastleSnapshot& guild_castle_snapshot,
                                    const GuildState& guild_state) {
  append_dialog_line(
      text, "Castle Role: " +
                describe_castle_guild_role(guild_castle_snapshot.castle_dialog,
                                           guild_state.guild_name));
  append_dialog_line(text, "Members/Applicants: " +
                               std::to_string(guild_state.members.size()) + "/" +
                               std::to_string(guild_state.applicants.size()));
  append_dialog_line(text, "Preview: " + summarize_name_list(guild_state.members, 2));
}

/**
 * @brief 向对话框追加行会浏览摘要信息
 * @details 比目录摘要更详细，包含完整的成员预览和申请者预览
 * @param text 对话框文本
 * @param guild_castle_snapshot 行会城堡快照
 * @param guild_state 行会状态
 */
void append_guild_browse_summary(std::string& text,
                                 const GuildCastleSnapshot& guild_castle_snapshot,
                                 const GuildState& guild_state) {
  append_dialog_line(
      text, "Castle Role: " +
                describe_castle_guild_role(guild_castle_snapshot.castle_dialog,
                                           guild_state.guild_name));
  append_dialog_line(text, "Members/Applicants: " +
                               std::to_string(guild_state.members.size()) + "/" +
                               std::to_string(guild_state.applicants.size()));
  append_dialog_line(text, "Preview: " + summarize_name_list(guild_state.members));
  append_dialog_line(text, "Applicant Preview: " + summarize_name_list(guild_state.applicants));
}

/**
 * @brief 构建行会信息单行摘要
 * @details 用于系统通知的 Key=Value 格式行，包含行会名、角色、领主、城堡角色、成员数等信息
 * @param speaker 说话者（请求者）
 * @param guild_castle_snapshot 行会城堡快照
 * @param guild_state 行会状态（可能为 nullptr）
 * @return 信息行字符串
 */
std::string build_guild_info_line(const Player& speaker,
                                  const GuildCastleSnapshot& guild_castle_snapshot,
                                  const GuildState* guild_state) {
  const auto& character = speaker.character();
  if (guild_state == nullptr) {
    std::ostringstream line;
    line << "Guild=" << character.guild_name << " Role=" << character.guild_title
         << " CastleRole=Unknown Members/Applicants=unknown";
    return line.str();
  }

  std::ostringstream line;
  line << "Guild=" << guild_state->guild_name << " Role=" << character.guild_title
       << " Lord=" << guild_state->lord
       << " CastleRole="
       << describe_castle_guild_role(guild_castle_snapshot.castle_dialog, guild_state->guild_name)
       << " Members/Applicants=" << guild_state->members.size() << "/"
       << guild_state->applicants.size()
       << " Preview=" << summarize_name_list(guild_state->members)
       << " ApplicantPreview=" << summarize_name_list(guild_state->applicants);
  return line.str();
}

/**
 * @brief 构建行会信息对话框文本
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @return 对话框文本，包含行会详情、成员入口、申请者入口（仅领主可见）
 */
std::string build_guild_info_dialog_text(const Player& requester,
                                         const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Guild Info\\";
  const auto& character = requester.character();
  if (character.guild_name.empty()) {
    append_dialog_line(text, "You are not in a guild.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto* guild_state = find_guild_state(guild_castle_snapshot, character.guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  append_dialog_line(text, "Guild: " + guild_state->guild_name);
  append_dialog_line(text, "Your Role: " + character.guild_title);
  append_dialog_line(text, "Lord: " + guild_state->lord);
  append_guild_browse_summary(text, guild_castle_snapshot, *guild_state);
  append_dialog_entry(text, "Members", "@guild_members");
  if (equals_ignore_case(guild_state->lord, character.character_name)) {
    append_dialog_entry(text, "Applicants", "@guild_applicants");
  }
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 解析行会成员头衔
 * @details 头衔优先级：领主(Lord) > 请求者自身头衔 > 在线成员存储的头衔 > "Member"
 * @param requester 请求者
 * @param objects 游戏对象映射表
 * @param guild_state 行会状态
 * @param member_name 成员名称
 * @return 成员的头衔字符串
 */
std::string resolve_guild_member_title(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildState& guild_state, std::string_view member_name) {
  if (equals_ignore_case(member_name, guild_state.lord)) {
    return "Lord";
  }
  if (equals_ignore_case(member_name, requester.character().character_name) &&
      !requester.character().guild_title.empty()) {
    return requester.character().guild_title;
  }
  if (const auto* member = find_online_player_by_name(objects, member_name);
      member != nullptr &&
      equals_ignore_case(member->character().guild_name, guild_state.guild_name) &&
      !member->character().guild_title.empty()) {
    return member->character().guild_title;
  }
  return "Member";
}

// ============================================================================
//  行会 NPC 对话框构建器
//  这些函数构建行会相关的 NPC 对话框文本，供传统 NPC 交互系统使用。
//  每个函数对应一个特定的对话框页面，遵循 @action 命令导航模式。
// ============================================================================

/**
 * @brief 构建行会服务主菜单对话框文本
 * @details 根据玩家是否已加入行会显示不同的选项：
 *          未加入时显示创建行会、行会目录；已加入时显示信息、成员、离开等
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @return 对话框文本
 */
std::string build_guild_service_dialog_text(const Player& requester,
                                            const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Guild Office\\";
  const auto& character = requester.character();
  if (character.guild_name.empty()) {
    append_dialog_entry(text, "Create Guild", "@guild_create_menu");
    if (guild_castle_snapshot.guilds.empty()) {
      append_dialog_line(text, "No guilds are currently registered.");
    } else {
      append_dialog_entry(text, "Guild Directory", "@guild_directory");
      if (std::any_of(guild_castle_snapshot.guilds.begin(), guild_castle_snapshot.guilds.end(),
                      [&](const GuildState& guild_state) {
                        return guild_has_applicant(guild_state, character.character_name);
                      })) {
        append_dialog_entry(text, "My Applications", "@guild_my_applications");
      }
    }
  } else {
    append_dialog_entry(text, "Info", "@guild_info");
    append_dialog_entry(text, "Members", "@guild_members");
    append_dialog_entry(text, "Leave", "@guild_leave_confirm");

    const auto* guild_state = find_guild_state(guild_castle_snapshot, character.guild_name);
    if (guild_state != nullptr &&
        equals_ignore_case(guild_state->lord, character.character_name)) {
      append_dialog_entry(text, "Applicants", "@guild_applicants");
    }
  }

  append_dialog_entry(text, "Back", "@main");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 生成行会名称建议列表
 * @details 基于玩家角色名生成默认的行会名称建议（如 "NameGuild"、"NameHall"、"NameLegion"）
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @return 行会名称建议列表（最多3个）
 */
std::vector<std::string> build_guild_create_suggestions(
    const Player& requester, const GuildCastleSnapshot& guild_castle_snapshot) {
  std::vector<std::string> suggestions;
  auto add_suggestion = [&](std::string name) {
    name = util::trim(std::move(name));
    if (name.empty() || find_guild_state(guild_castle_snapshot, name) != nullptr) {
      return;
    }
    if (std::any_of(suggestions.begin(), suggestions.end(), [&](const std::string& existing) {
          return equals_ignore_case(existing, name);
        })) {
      return;
    }
    suggestions.push_back(std::move(name));
  };

  const auto base_name = util::trim(requester.character().character_name);
  add_suggestion(base_name + "Guild");
  add_suggestion(base_name + "Hall");
  add_suggestion(base_name + "Legion");
  for (int suffix = 2; suggestions.size() < 3 && suffix <= 9; ++suffix) {
    add_suggestion(base_name + "Guild" + std::to_string(suffix));
  }
  return suggestions;
}

/**
 * @brief 构建行会创建菜单对话框文本
 * @details 显示创始人信息、创建费用、金币余额，并提供行会名称建议供选择
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @return 对话框文本
 */
std::string build_guild_create_menu_dialog_text(const Player& requester,
                                                const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Guild Charter\\";
  if (!requester.character().guild_name.empty()) {
    append_dialog_line(text, "You already belong to a guild.");
    append_dialog_entry(text, "Guild", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  append_dialog_line(text, "Founder: " + requester.character().character_name);
  append_dialog_line(text, "Founding Fee: " +
                               std::to_string(guild_castle_snapshot.castle_dialog.guild_create_fee));
  append_dialog_line(text, "Gold: " + std::to_string(requester.character().gold));
  append_dialog_line(text, "Choose a charter name below or use @guild_create <name> in chat.");
  const auto suggestions = build_guild_create_suggestions(requester, guild_castle_snapshot);
  if (suggestions.empty()) {
    append_dialog_line(text, "No charter suggestions are currently available.");
  } else {
    for (const auto& suggestion : suggestions) {
      append_dialog_entry(text, "Create " + suggestion, "@guild_create_confirm " + suggestion);
    }
  }
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会创建确认对话框文本
 * @details 显示行会名称、费用信息、状态（准备就绪/已加入/名称缺失/名称不可用/金币不足）
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param guild_name 提议的行会名称
 * @return 对话框文本
 */
std::string build_guild_create_confirm_dialog_text(const Player& requester,
                                                   const GuildCastleSnapshot& guild_castle_snapshot,
                                                   std::string guild_name) {
  std::string text = "Create Guild\\";
  guild_name = util::trim(std::move(guild_name));
  const auto guild_create_fee = guild_castle_snapshot.castle_dialog.guild_create_fee;
  append_dialog_line(text, "Founder: " + requester.character().character_name);
  append_dialog_line(text, "Guild: " + guild_name);
  append_dialog_line(text, "Founding Fee: " + std::to_string(guild_create_fee));
  append_dialog_line(text, "Gold: " + std::to_string(requester.character().gold));

  std::string status = "Ready";
  if (!requester.character().guild_name.empty()) {
    status = "Already In Guild";
  } else if (guild_name.empty()) {
    status = "Missing Name";
  } else if (find_guild_state(guild_castle_snapshot, guild_name) != nullptr) {
    status = "Name Unavailable";
  } else if (guild_create_fee > 0 && !requester.can_spend_gold(guild_create_fee)) {
    status = "Need " + std::to_string(guild_create_fee) + " Gold";
  }
  append_dialog_line(text, "Status: " + status);
  if (status == "Ready") {
    append_dialog_entry(text, "Confirm", "@guild_create_exec " + guild_name);
  } else if (!guild_name.empty()) {
    append_dialog_entry(text, "View Result", "@guild_create_exec " + guild_name);
  }
  append_dialog_entry(text, "Back", "@guild_create_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会成员列表对话框文本
 * @details 分页显示行会成员列表，领主可对每个成员执行管理操作
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param requested_page 请求的页码
 * @return 对话框文本
 */
std::string build_guild_members_dialog_text(const Player& requester,
                                            const GuildCastleSnapshot& guild_castle_snapshot,
                                            std::size_t requested_page) {
  const auto* guild_state = find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  std::string text = "Guild Members\\";
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_state->members.size());
  const auto page = clamp_dialog_page(requested_page, guild_state->members.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(guild_state->members.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, guild_state->guild_name + " (" + std::to_string(static_cast<int>(page)) +
                               "/" + std::to_string(static_cast<int>(total_pages)) + ")");

  const auto is_lord = equals_ignore_case(guild_state->lord, requester.character().character_name);
  for (std::size_t index = start; index < end; ++index) {
    const auto& member = guild_state->members[index];
    append_dialog_line(text, "Member: " + member);
    if (is_lord && !equals_ignore_case(member, requester.character().character_name)) {
      append_dialog_entry(text, "Manage " + member,
                          "@guild_member " + std::to_string(static_cast<int>(page)) + " " +
                              member);
    }
  }

  append_page_navigation(text, "@guild_members", page, total_pages);
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会目录对话框文本
 * @details 分页显示所有已注册的行会，每页显示行会概览和申请/浏览入口
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param requested_page 请求的页码
 * @return 对话框文本
 */
std::string build_guild_directory_dialog_text(const Player& requester,
                                              const GuildCastleSnapshot& guild_castle_snapshot,
                                              std::size_t requested_page) {
  std::string text = "Guild Directory\\";
  if (guild_castle_snapshot.guilds.empty()) {
    append_dialog_line(text, "No guilds are currently registered.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_castle_snapshot.guilds.size());
  const auto page = clamp_dialog_page(requested_page, guild_castle_snapshot.guilds.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end =
      std::min<std::size_t>(guild_castle_snapshot.guilds.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, "Guilds (" + std::to_string(static_cast<int>(page)) + "/" +
                               std::to_string(static_cast<int>(total_pages)) + ")");

  for (std::size_t index = start; index < end; ++index) {
    const auto& guild_state = guild_castle_snapshot.guilds[index];
    append_dialog_line(text, "Guild: " + guild_state.guild_name);
    append_dialog_line(text, "Lord: " + guild_state.lord);
    append_guild_directory_summary(text, guild_castle_snapshot, guild_state);
    append_dialog_entry(text, "View " + guild_state.guild_name,
                        "@guild_browse directory " +
                            std::to_string(static_cast<int>(page)) + " " + guild_state.guild_name);
    if (guild_has_applicant(guild_state, requester.character().character_name)) {
      append_dialog_entry(text, "Pending " + guild_state.guild_name,
                          "@guild_apply_status " + guild_state.guild_name);
    } else {
      append_dialog_entry(text, "Apply " + guild_state.guild_name,
                          "@guild_apply_confirm " + guild_state.guild_name);
    }
  }

  append_page_navigation(text, "@guild_directory", page, total_pages);
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会详情浏览对话框文本
 * @details 显示行会详细信息（领主、成员数、申请者数、城堡关系），并提供成员/申请者列表入口
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 浏览目标（包含来源、页码和行会名）
 * @return 对话框文本
 */
std::string build_guild_browse_dialog_text(const Player& requester,
                                           const GuildCastleSnapshot& guild_castle_snapshot,
                                           const GuildBrowseTarget& target) {
  std::string text = "Guild Detail\\";
  const auto* guild_state = find_guild_state(guild_castle_snapshot, target.guild_name);
  const auto back_action =
      build_guild_browse_back_action(target.source, target.page, target.guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
    append_dialog_entry(text, "Back", back_action);
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  append_dialog_line(text, "Guild: " + guild_state->guild_name);
  append_dialog_line(text, "Lord: " + guild_state->lord);
  append_guild_browse_summary(text, guild_castle_snapshot, *guild_state);
  append_dialog_entry(text, "View Members",
                      "@guild_roster " + target.source + " " +
                          std::to_string(static_cast<int>(target.page)) + " 1 " +
                          guild_state->guild_name);
  if (!guild_state->applicants.empty()) {
    append_dialog_entry(text, "View Applicants",
                        "@guild_applicant_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.page)) + " 1 " +
                            guild_state->guild_name);
  }
  if (equals_ignore_case(guild_castle_snapshot.castle_dialog.owner_guild, guild_state->guild_name)) {
    append_dialog_line(text, "Castle: Owner of " +
                                 default_castle_name(guild_castle_snapshot.castle_dialog));
    append_dialog_line(text, "Castle Lord: " + display_castle_lord(guild_castle_snapshot.castle_dialog));
  } else {
    append_dialog_line(text, "Castle: None");
  }

  if (guild_has_applicant(*guild_state, requester.character().character_name)) {
    append_dialog_line(text, "Status: Pending");
    append_dialog_entry(text, "Pending Application",
                        "@guild_apply_status " + guild_state->guild_name);
  } else if (requester.character().guild_name.empty()) {
    append_dialog_line(text, "Status: Ready");
    append_dialog_entry(text, "Apply to Guild",
                        "@guild_apply_confirm " + guild_state->guild_name);
  }

  append_dialog_entry(text, "Back", back_action);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会成员名册对话框文本（外部浏览用）
 * @details 分页显示指定行会的成员列表，提供上/下页导航
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 浏览列表目标（包含来源、浏览页码、列表页码和行会名）
 * @return 对话框文本
 */
std::string build_guild_roster_dialog_text(const GuildCastleSnapshot& guild_castle_snapshot,
                                           const GuildBrowseListTarget& target) {
  std::string text = "Guild Members\\";
  const auto* guild_state = find_guild_state(guild_castle_snapshot, target.guild_name);
  const auto back_action =
      build_guild_browse_list_back_action(target.source, target.browse_page, target.guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
    append_dialog_entry(text, "Back", back_action);
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_state->members.size());
  const auto page = clamp_dialog_page(target.list_page, guild_state->members.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(guild_state->members.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, guild_state->guild_name + " (" + std::to_string(static_cast<int>(page)) +
                               "/" + std::to_string(static_cast<int>(total_pages)) + ")");

  for (std::size_t index = start; index < end; ++index) {
    append_dialog_line(text, "Member: " + guild_state->members[index]);
  }

  if (page > 1) {
    append_dialog_entry(text, "Prev",
                        "@guild_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.browse_page)) + " " +
                            std::to_string(static_cast<int>(page - 1)) + " " +
                            guild_state->guild_name);
  }
  if (page < total_pages) {
    append_dialog_entry(text, "Next",
                        "@guild_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.browse_page)) + " " +
                            std::to_string(static_cast<int>(page + 1)) + " " +
                            guild_state->guild_name);
  }
  append_dialog_entry(text, "Back", back_action);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会申请者名册对话框文本（外部浏览用）
 * @details 分页显示指定行会的申请者列表，提供上/下页导航
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 浏览列表目标
 * @return 对话框文本
 */
std::string build_guild_applicant_roster_dialog_text(const GuildCastleSnapshot& guild_castle_snapshot,
                                                     const GuildBrowseListTarget& target) {
  std::string text = "Guild Applicants\\";
  const auto* guild_state = find_guild_state(guild_castle_snapshot, target.guild_name);
  const auto back_action =
      build_guild_browse_list_back_action(target.source, target.browse_page, target.guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
    append_dialog_entry(text, "Back", back_action);
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_state->applicants.size());
  const auto page = clamp_dialog_page(target.list_page, guild_state->applicants.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end =
      std::min<std::size_t>(guild_state->applicants.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, guild_state->guild_name + " (" + std::to_string(static_cast<int>(page)) +
                               "/" + std::to_string(static_cast<int>(total_pages)) + ")");

  if (guild_state->applicants.empty()) {
    append_dialog_line(text, "No pending applications.");
  } else {
    for (std::size_t index = start; index < end; ++index) {
      append_dialog_line(text, "Applicant: " + guild_state->applicants[index]);
    }
  }

  if (page > 1) {
    append_dialog_entry(text, "Prev",
                        "@guild_applicant_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.browse_page)) + " " +
                            std::to_string(static_cast<int>(page - 1)) + " " +
                            guild_state->guild_name);
  }
  if (page < total_pages) {
    append_dialog_entry(text, "Next",
                        "@guild_applicant_roster " + target.source + " " +
                            std::to_string(static_cast<int>(target.browse_page)) + " " +
                            std::to_string(static_cast<int>(page + 1)) + " " +
                            guild_state->guild_name);
  }
  append_dialog_entry(text, "Back", back_action);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建"我的申请"对话框文本
 * @details 分页显示玩家当前所有待处理的行会申请
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param requested_page 请求的页码
 * @return 对话框文本
 */
std::string build_guild_my_applications_dialog_text(const Player& requester,
                                                    const GuildCastleSnapshot& guild_castle_snapshot,
                                                    std::size_t requested_page) {
  std::string text = "My Guild Applications\\";
  std::vector<const GuildState*> pending_guilds;
  pending_guilds.reserve(guild_castle_snapshot.guilds.size());
  for (const auto& guild_state : guild_castle_snapshot.guilds) {
    if (guild_has_applicant(guild_state, requester.character().character_name)) {
      pending_guilds.push_back(&guild_state);
    }
  }

  const auto total_pages = dialog_total_pages(pending_guilds.size());
  const auto page = clamp_dialog_page(requested_page, pending_guilds.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(pending_guilds.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, "Applications (" + std::to_string(static_cast<int>(page)) + "/" +
                               std::to_string(static_cast<int>(total_pages)) + ")");

  if (pending_guilds.empty()) {
    append_dialog_line(text, "You have no pending applications.");
  } else {
    for (std::size_t index = start; index < end; ++index) {
      const auto* guild_state = pending_guilds[index];
      append_dialog_line(text, "Guild: " + guild_state->guild_name);
      append_dialog_line(text, "Lord: " + guild_state->lord);
      append_guild_directory_summary(text, guild_castle_snapshot, *guild_state);
      append_dialog_entry(text, "View " + guild_state->guild_name,
                          "@guild_browse applications " +
                              std::to_string(static_cast<int>(page)) + " " +
                              guild_state->guild_name);
    }
  }

  append_page_navigation(text, "@guild_my_applications", page, total_pages);
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会成员管理对话框文本
 * @details 允许领主管理指定成员，包括修改头衔、转让领导权和踢出成员
 * @param requester 请求者（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 成员对话框目标（包含页码和成员名）
 * @return 对话框文本
 */
std::string build_guild_member_manage_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildMemberDialogTarget& target) {
  std::string text = "Guild Member\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back",
                        "@guild_members " + std::to_string(static_cast<int>(target.page)));
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto member_name = util::trim(target.member_name);
  const auto is_lord = equals_ignore_case(guild_state->lord, requester.character().character_name);
  append_dialog_line(text, guild_state->guild_name + " / " + member_name);

  if (!is_lord) {
    append_dialog_line(text, "Only the guild lord can manage members.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That guild member is unavailable.");
  } else if (equals_ignore_case(member_name, requester.character().character_name)) {
    append_dialog_line(text, "Choose another member to manage.");
  } else {
    const auto current_title =
        resolve_guild_member_title(requester, objects, *guild_state, member_name);
    append_dialog_line(text, "Title: " + current_title);
    append_dialog_entry(text, "Titles",
                        "@guild_member_titles " + std::to_string(static_cast<int>(target.page)) +
                            " 1 " + member_name);
    append_dialog_entry(text, "Transfer Leadership",
                        "@guild_transfer_confirm " + std::to_string(static_cast<int>(target.page)) +
                            " " + member_name);
    append_dialog_entry(text, "Kick Member",
                        "@guild_kick_confirm " + std::to_string(static_cast<int>(target.page)) +
                            " " + member_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_members " + std::to_string(static_cast<int>(target.page)));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建踢出成员确认对话框文本
 * @details 显示被踢成员的状态（在线/离线）和头衔，要求领主确认踢出操作
 * @param requester 请求者（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 成员对话框目标
 * @return 对话框文本
 */
std::string build_guild_kick_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildMemberDialogTarget& target) {
  std::string text = "Kick Guild Member\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto member_name = util::trim(target.member_name);
  append_dialog_line(text, requester.character().guild_name + " / " + member_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can kick members.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That character is not a guild member.");
  } else if (equals_ignore_case(member_name, requester.character().character_name)) {
    append_dialog_line(text, "Use guild leave to remove yourself.");
  } else {
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, member_name) != nullptr ? "Online"
                                                                                : "Offline"));
    append_dialog_line(
        text, "Title: " + resolve_guild_member_title(requester, objects, *guild_state, member_name));
    append_dialog_line(text, "Confirm member removal?");
    append_dialog_entry(text, "Confirm",
                        "@guild_kick_exec " + std::to_string(static_cast<int>(target.page)) + " " +
                            member_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_member " + std::to_string(static_cast<int>(target.page)) + " " +
                          member_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建领导权转让确认对话框文本
 * @details 显示当前领主和目标成员信息，要求领主确认转让操作
 * @param requester 请求者（当前领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 成员对话框目标
 * @return 对话框文本
 */
std::string build_guild_transfer_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildMemberDialogTarget& target) {
  std::string text = "Transfer Leadership\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto member_name = util::trim(target.member_name);
  append_dialog_line(text, requester.character().guild_name + " / " + member_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can transfer leadership.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That character is not a guild member.");
  } else if (equals_ignore_case(member_name, requester.character().character_name)) {
    append_dialog_line(text, "You already lead this guild.");
  } else {
    append_dialog_line(text, "Current Lord: " + guild_state->lord);
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, member_name) != nullptr ? "Online"
                                                                                : "Offline"));
    append_dialog_line(text, "New Lord: " + member_name);
    append_dialog_line(text, "Confirm leadership transfer?");
    append_dialog_entry(text, "Confirm",
                        "@guild_transfer_exec " + std::to_string(static_cast<int>(target.page)) +
                            " " + member_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_member " + std::to_string(static_cast<int>(target.page)) + " " +
                          member_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会成员头衔管理对话框文本
 * @details 分页显示可用的头衔列表，领主可为指定成员设置头衔
 * @param requester 请求者（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 头衔对话框目标（包含成员页码、头衔页码和成员名）
 * @return 对话框文本
 */
std::string build_guild_member_titles_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildMemberTitleDialogTarget& target) {
  std::string text = "Guild Titles\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back",
                        "@guild_member " + std::to_string(static_cast<int>(target.member_page)) +
                            " " + target.member_name);
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto member_name = util::trim(target.member_name);
  const auto is_lord = equals_ignore_case(guild_state->lord, requester.character().character_name);
  const auto page =
      std::clamp<std::size_t>(target.title_page, 1, kGuildTitlePages.size());
  const auto& title_page = kGuildTitlePages[page - 1];
  append_dialog_line(text, guild_state->guild_name + " / " + member_name);

  if (!is_lord) {
    append_dialog_line(text, "Only the guild lord can manage titles.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That guild member is unavailable.");
  } else if (equals_ignore_case(member_name, requester.character().character_name)) {
    append_dialog_line(text, "Use leadership transfer to change your own role.");
  } else {
    const auto current_title =
        resolve_guild_member_title(requester, objects, *guild_state, member_name);
    append_dialog_line(text, "Roles (" + std::to_string(static_cast<int>(page)) + "/" +
                                 std::to_string(static_cast<int>(kGuildTitlePages.size())) +
                                 ") " + std::string(title_page.label));
    append_dialog_line(text, "Current: " + current_title);
    for (const auto title_template : title_page.titles) {
      std::string label(title_template);
      if (equals_ignore_case(label, current_title)) {
        label += " (Current)";
      }
      append_dialog_entry(text, label,
                          "@guild_title_confirm " +
                              std::to_string(static_cast<int>(target.member_page)) + " " +
                              std::to_string(static_cast<int>(page)) + " " + member_name + " " +
                              std::string(title_template));
    }
    if (page > 1) {
      append_dialog_entry(text, "Prev",
                          "@guild_member_titles " +
                              std::to_string(static_cast<int>(target.member_page)) + " " +
                              std::to_string(static_cast<int>(page - 1)) + " " + member_name);
    }
    if (page < kGuildTitlePages.size()) {
      append_dialog_entry(text, "Next",
                          "@guild_member_titles " +
                              std::to_string(static_cast<int>(target.member_page)) + " " +
                              std::to_string(static_cast<int>(page + 1)) + " " + member_name);
    }
  }

  append_dialog_entry(text, "Back",
                      "@guild_member " + std::to_string(static_cast<int>(target.member_page)) +
                          " " + member_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会申请者列表对话框文本
 * @details 分页显示申请者列表，领主可逐个审核申请者（批准/拒绝）
 * @param requester 请求者（必须是领主）
 * @param guild_castle_snapshot 行会城堡快照
 * @param requested_page 请求的页码
 * @return 对话框文本
 */
std::string build_guild_applicants_dialog_text(const Player& requester,
                                               const GuildCastleSnapshot& guild_castle_snapshot,
                                               std::size_t requested_page) {
  const auto* guild_state = find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  std::string text = "Guild Applicants\\";
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back", "@guild_menu");
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto total_pages = dialog_total_pages(guild_state->applicants.size());
  const auto page = clamp_dialog_page(requested_page, guild_state->applicants.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end =
      std::min<std::size_t>(guild_state->applicants.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, guild_state->guild_name + " (" + std::to_string(static_cast<int>(page)) +
                               "/" + std::to_string(static_cast<int>(total_pages)) + ")");

  if (guild_state->applicants.empty()) {
    append_dialog_line(text, "No pending applications.");
  } else {
    for (std::size_t index = start; index < end; ++index) {
      const auto& applicant = guild_state->applicants[index];
      append_dialog_line(text, "Applicant: " + applicant);
      append_dialog_entry(text, "Review " + applicant,
                          "@guild_applicant " + std::to_string(static_cast<int>(page)) + " " +
                              applicant);
    }
  }

  append_page_navigation(text, "@guild_applicants", page, total_pages);
  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建申请者审核对话框文本
 * @details 显示申请者信息和状态，提供批准和拒绝按钮
 * @param requester 请求者（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 申请者对话框目标
 * @return 对话框文本
 */
std::string build_guild_applicant_review_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildApplicantDialogTarget& target) {
  std::string text = "Guild Applicant\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
    append_dialog_entry(text, "Back",
                        "@guild_applicants " + std::to_string(static_cast<int>(target.page)));
    append_dialog_entry(text, "Close", "@exit");
    return text;
  }

  const auto applicant_name = util::trim(target.applicant_name);
  append_dialog_line(text, guild_state->guild_name + " / " + applicant_name);

  if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can review applications.");
  } else if (applicant_name.empty() || !guild_has_applicant(*guild_state, applicant_name)) {
    append_dialog_line(text, "That application is no longer pending.");
  } else {
    append_dialog_line(text, "Application: " + applicant_name);
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, applicant_name) != nullptr ? "Online"
                                                                                  : "Offline"));
    append_dialog_entry(text, "Approve",
                        "@guild_approve_confirm " + std::to_string(static_cast<int>(target.page)) +
                            " " + applicant_name);
    append_dialog_entry(text, "Reject",
                        "@guild_reject_confirm " +
                            std::to_string(static_cast<int>(target.page)) + " " + applicant_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_applicants " + std::to_string(static_cast<int>(target.page)));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会申请确认对话框文本
 * @details 显示申请者信息、目标行会详情，并提供确认申请按钮
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param target_guild_name 目标行会名称
 * @return 对话框文本
 */
std::string build_guild_apply_confirm_dialog_text(const Player& requester,
                                                  const GuildCastleSnapshot& guild_castle_snapshot,
                                                  std::string target_guild_name) {
  std::string text = "Guild Application\\";
  target_guild_name = util::trim(std::move(target_guild_name));
  append_dialog_line(text, "Applicant: " + requester.character().character_name);

  if (!requester.character().guild_name.empty()) {
    append_dialog_line(text, "Leave your current guild before joining another.");
  } else if (target_guild_name.empty()) {
    append_dialog_line(text, "Choose a guild first.");
  } else if (const auto* guild_state = find_guild_state(guild_castle_snapshot, target_guild_name);
             guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
  } else {
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Lord: " + guild_state->lord);
    append_dialog_line(text, "Members: " + std::to_string(guild_state->members.size()));
    append_dialog_line(text, "Applicants: " + std::to_string(guild_state->applicants.size()));
    if (guild_has_applicant(*guild_state, requester.character().character_name)) {
      append_dialog_line(text, "Status: Already Pending");
    } else {
      append_dialog_line(text, "Status: Ready");
    }
    append_dialog_line(text, "Confirm guild application?");
    append_dialog_entry(text, "Confirm", "@guild_apply_exec " + guild_state->guild_name);
  }

  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会申请状态查询对话框文本
 * @details 显示玩家对指定行会的申请状态（待处理/无申请），并提供撤回或重新申请入口
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param target_guild_name 目标行会名称
 * @return 对话框文本
 */
std::string build_guild_apply_status_dialog_text(const Player& requester,
                                                 const GuildCastleSnapshot& guild_castle_snapshot,
                                                 std::string target_guild_name) {
  std::string text = "Guild Application Status\\";
  target_guild_name = util::trim(std::move(target_guild_name));
  append_dialog_line(text, "Applicant: " + requester.character().character_name);

  if (target_guild_name.empty()) {
    append_dialog_line(text, "Choose a guild first.");
  } else if (const auto* guild_state = find_guild_state(guild_castle_snapshot, target_guild_name);
             guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
  } else {
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Lord: " + guild_state->lord);
    append_dialog_line(text, "Members: " + std::to_string(guild_state->members.size()));
    if (guild_has_applicant(*guild_state, requester.character().character_name)) {
      append_dialog_line(text, "Status: Pending");
      append_dialog_entry(text, "Withdraw",
                          "@guild_withdraw_confirm " + guild_state->guild_name);
    } else {
      append_dialog_line(text, "Status: No Pending Application");
      append_dialog_entry(text, "Apply",
                          "@guild_apply_confirm " + guild_state->guild_name);
    }
  }

  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建撤回行会申请确认对话框文本
 * @details 显示当前申请状态，要求玩家确认撤回申请
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param target_guild_name 目标行会名称
 * @return 对话框文本
 */
std::string build_guild_withdraw_confirm_dialog_text(const Player& requester,
                                                     const GuildCastleSnapshot& guild_castle_snapshot,
                                                     std::string target_guild_name) {
  std::string text = "Withdraw Guild Application\\";
  target_guild_name = util::trim(std::move(target_guild_name));
  append_dialog_line(text, "Applicant: " + requester.character().character_name);

  if (target_guild_name.empty()) {
    append_dialog_line(text, "Choose a guild first.");
  } else if (const auto* guild_state = find_guild_state(guild_castle_snapshot, target_guild_name);
             guild_state == nullptr) {
    append_dialog_line(text, "Guild not found.");
  } else {
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Lord: " + guild_state->lord);
    if (guild_has_applicant(*guild_state, requester.character().character_name)) {
      append_dialog_line(text, "Status: Pending");
      append_dialog_line(text, "Confirm application withdrawal?");
      append_dialog_entry(text, "Confirm",
                          "@guild_withdraw_exec " + guild_state->guild_name);
    } else {
      append_dialog_line(text, "Status: No Pending Application");
    }
  }

  append_dialog_entry(text, "Back", "@guild_apply_status " + target_guild_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建批准申请者确认对话框文本
 * @details 显示申请者状态和行会信息，要求领主确认批准操作
 * @param requester 请求者（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 申请者对话框目标
 * @return 对话框文本
 */
std::string build_guild_approve_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildApplicantDialogTarget& target) {
  std::string text = "Approve Applicant\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto applicant_name = util::trim(target.applicant_name);
  append_dialog_line(text, requester.character().guild_name + " / " + applicant_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can review applications.");
  } else if (applicant_name.empty() || !guild_has_applicant(*guild_state, applicant_name)) {
    append_dialog_line(text, "That application is no longer pending.");
  } else {
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, applicant_name) != nullptr ? "Online"
                                                                                   : "Offline"));
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Role: Member");
    append_dialog_line(text, "Confirm guild approval?");
    append_dialog_entry(text, "Confirm",
                        "@guild_approve_exec " + std::to_string(static_cast<int>(target.page)) +
                            " " + applicant_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_applicant " + std::to_string(static_cast<int>(target.page)) + " " +
                          applicant_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建拒绝申请者确认对话框文本
 * @details 显示申请者信息，要求领主确认拒绝操作
 * @param requester 请求者（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 申请者对话框目标
 * @return 对话框文本
 */
std::string build_guild_reject_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildApplicantDialogTarget& target) {
  std::string text = "Reject Applicant\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto applicant_name = util::trim(target.applicant_name);
  append_dialog_line(text, requester.character().guild_name + " / " + applicant_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can review applications.");
  } else if (applicant_name.empty() || !guild_has_applicant(*guild_state, applicant_name)) {
    append_dialog_line(text, "That application is no longer pending.");
  } else {
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, applicant_name) != nullptr ? "Online"
                                                                                   : "Offline"));
    append_dialog_line(text, "Guild: " + guild_state->guild_name);
    append_dialog_line(text, "Confirm guild rejection?");
    append_dialog_entry(text, "Confirm",
                        "@guild_reject_exec " + std::to_string(static_cast<int>(target.page)) +
                            " " + applicant_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_applicant " + std::to_string(static_cast<int>(target.page)) + " " +
                          applicant_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建行会头衔变更确认对话框文本
 * @details 显示成员当前头衔和新头衔，要求领主确认头衔变更操作
 * @param requester 请求者（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 头衔确认目标
 * @return 对话框文本
 */
std::string build_guild_title_confirm_dialog_text(
    const Player& requester,
    const std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const GuildCastleSnapshot& guild_castle_snapshot, const GuildTitleConfirmTarget& target) {
  std::string text = "Set Guild Title\\";
  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, requester.character().guild_name);
  const auto member_name = util::trim(target.member_name);
  const auto title_name = util::trim(target.title_name);
  append_dialog_line(text, requester.character().guild_name + " / " + member_name);

  if (guild_state == nullptr) {
    append_dialog_line(text, "Guild data is unavailable.");
  } else if (!equals_ignore_case(guild_state->lord, requester.character().character_name)) {
    append_dialog_line(text, "Only the guild lord can manage titles.");
  } else if (member_name.empty() || !guild_has_member(*guild_state, member_name)) {
    append_dialog_line(text, "That guild member is unavailable.");
  } else if (equals_ignore_case(member_name, guild_state->lord)) {
    append_dialog_line(text, "Use leadership transfer to change the guild lord.");
  } else if (title_name.empty()) {
    append_dialog_line(text, "Choose a guild title first.");
  } else {
    append_dialog_line(
        text, std::string("Status: ") +
                  (find_online_player_by_name(objects, member_name) != nullptr ? "Online"
                                                                                : "Offline"));
    append_dialog_line(
        text, "Current: " + resolve_guild_member_title(requester, objects, *guild_state, member_name));
    append_dialog_line(text, "New Title: " + title_name);
    append_dialog_line(text, "Confirm guild title change?");
    append_dialog_entry(text, "Confirm",
                        "@guild_title_exec " +
                            std::to_string(static_cast<int>(target.member_page)) + " " +
                            std::to_string(static_cast<int>(target.title_page)) + " " +
                            member_name + " " + title_name);
  }

  append_dialog_entry(text, "Back",
                      "@guild_member_titles " +
                          std::to_string(static_cast<int>(target.member_page)) + " " +
                          std::to_string(static_cast<int>(target.title_page)) + " " + member_name);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建退出行会确认对话框文本
 * @details 根据玩家角色显示退出后果：领主退出会转让领导权或解散行会，普通成员直接退出
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @return 对话框文本
 */
std::string build_guild_leave_confirm_dialog_text(const Player& requester,
                                                  const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Leave Guild\\";
  const auto& character = requester.character();
  append_dialog_line(text, "Character: " + character.character_name);

  if (character.guild_name.empty()) {
    append_dialog_line(text, "You are not in a guild.");
  } else {
    append_dialog_line(text, "Guild: " + character.guild_name);
    append_dialog_line(text, "Role: " + character.guild_title);
    if (const auto* guild_state = find_guild_state(guild_castle_snapshot, character.guild_name);
        guild_state != nullptr &&
        equals_ignore_case(guild_state->lord, character.character_name)) {
      if (guild_state->members.size() <= 1) {
        append_dialog_line(text, "Leaving will disband the guild.");
      } else {
        std::string next_lord;
        for (const auto& member_name : guild_state->members) {
          if (!equals_ignore_case(member_name, character.character_name)) {
            next_lord = member_name;
            break;
          }
        }
        append_dialog_line(text, "Leaving will transfer leadership.");
        if (!next_lord.empty()) {
          append_dialog_line(text, "Next Lord: " + next_lord);
        }
      }
    }
    append_dialog_line(text, "Confirm leaving the guild?");
    append_dialog_entry(text, "Confirm", "@guild_leave_exec");
  }

  append_dialog_entry(text, "Back", "@guild_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

// ============================================================================
//  城堡 NPC 对话框构建器
//  这些函数构建城堡相关的 NPC 对话框文本。
// ============================================================================

/**
 * @brief 构建城堡信息展示对话框文本
 * @details 显示城堡详细信息（名称、拥有者、领主、角色、战争日期、费用等），
 *          并为拥有者行会领主提供认领城堡和宣战入口
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @return 对话框文本
 */
std::string build_castle_show_dialog_text(const Player& requester,
                                          const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Castle Info\\";
  const auto& castle = guild_castle_snapshot.castle_dialog;
  const auto wars = parse_castle_war_list(castle);
  append_dialog_line(text, "Castle: " + default_castle_name(castle));
  append_dialog_line(text, "Owner Guild: " + display_castle_owner(castle));
  append_dialog_line(text, "Owner Lord: " + display_castle_lord(castle));
  append_dialog_line(text, "Owner Role: " + describe_castle_owner_role(castle));
  append_dialog_line(text, "War Date: " + default_castle_war_date(castle));
  append_dialog_line(text, "War Count: " + std::to_string(wars.size()));
  append_dialog_line(text, "War Preview: " + summarize_castle_wars(castle));
  append_dialog_line(text, "Fees: " + std::to_string(castle.guild_war_fee) + "/" +
                               std::to_string(castle.upgrade_weapon_fee));
  if (const auto* owner_guild = find_guild_state(guild_castle_snapshot, castle.owner_guild);
      owner_guild != nullptr) {
    append_dialog_entry(text, "View Owner Guild",
                        "@guild_browse castle_show 1 " + owner_guild->guild_name);
    append_dialog_entry(text, "View Owner Members",
                        "@guild_roster castle_show 1 1 " + owner_guild->guild_name);
    if (!owner_guild->applicants.empty()) {
      append_dialog_entry(text, "View Owner Applicants",
                          "@guild_applicant_roster castle_show 1 1 " +
                              owner_guild->guild_name);
    }
  }
  append_dialog_entry(text, "Active Wars", "@castle_wars");

  if (!requester.character().guild_name.empty()) {
    const auto* own_guild =
        find_guild_state(guild_castle_snapshot, requester.character().guild_name);
    if (own_guild != nullptr &&
        equals_ignore_case(own_guild->lord, requester.character().character_name)) {
      append_dialog_entry(text, "Claim Castle", "@castle_claim_confirm");
      append_dialog_entry(text, "Declare War", "@castle_war_targets");
    }
  }

  append_dialog_entry(text, "Back", "@castle_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

// ============================================================================
//  城堡/行会操作执行器
//  这些函数执行城堡和行会的实际业务操作（认领、宣战、创建行会等）。
// ============================================================================

/**
 * @brief 执行城堡认领操作
 * @details 验证玩家条件（需在行会中、需是领主、行会数据可用），
 *          将城堡拥有权转让给玩家所在行会，并保存城堡状态
 * @param speaker 执行操作的玩家
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @return 操作结果（包含是否成功、摘要和详情）
 */
CastleActionResult execute_castle_claim(Player& speaker, GuildCastleSnapshot& guild_castle_snapshot,
                                        RuntimeDispatch& dispatch) {
  CastleActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;

  if (speaker.character().guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.claim_require_guild_template,
                                                 "Join a guild before claiming the castle.");
    result.details.push_back("Requirement: Guild lord with a registered guild.");
    return result;
  }

  const auto* guild_state =
      find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.claim_missing_guild_template,
                                    "Guild data is unavailable. Try again in a moment."),
        castle_dialog_context, speaker.character().guild_name);
    result.details.push_back("Guild snapshot is missing for " + speaker.character().guild_name + ".");
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.claim_only_lord_template,
                                    "Only the guild lord can claim the castle."),
        castle_dialog_context, guild_state->guild_name);
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }

  const auto previous_owner = castle_dialog_context.owner_guild;
  const auto previous_role =
      describe_castle_guild_role(castle_dialog_context, guild_state->guild_name);
  castle_dialog_context.owner_guild = guild_state->guild_name;
  castle_dialog_context.lord = guild_state->lord;
  queue_save_castle_state(dispatch, castle_dialog_context);

  result.success = true;
  result.summary = render_castle_summary_template(
      default_castle_claim_summary_template(castle_dialog_context), castle_dialog_context,
      guild_state->guild_name);
  result.details.push_back("Castle: " + default_castle_name(castle_dialog_context));
  result.details.push_back("Previous Owner: " +
                           (previous_owner.empty()
                                ? default_unclaimed_castle_owner(castle_dialog_context)
                                : previous_owner));
  result.details.push_back("New Owner: " + display_castle_owner(castle_dialog_context));
  result.details.push_back("Lord: " + display_castle_lord(castle_dialog_context));
  result.details.push_back("Owner Guild: " + display_castle_owner(castle_dialog_context));
  result.details.push_back("Owner Lord: " + display_castle_lord(castle_dialog_context));
  result.details.push_back("War Count: " +
                           std::to_string(parse_castle_war_list(castle_dialog_context).size()));
  result.details.push_back("War Preview: " + summarize_castle_wars(castle_dialog_context));
  result.details.push_back("Guild Role Change: " + previous_role + " -> " +
                           default_castle_role_change_owner_label(castle_dialog_context));
  return result;
}

/**
 * @brief 执行城堡宣战操作
 * @details 验证玩家条件（需是行会领主、目标行会存在、资金充足、尚未宣战），
 *          扣除宣战费用并注册战争
 * @param speaker 执行操作的玩家
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @param target_guild_name 目标行会名称
 * @return 操作结果
 */
CastleActionResult execute_castle_war(Player& speaker, GuildCastleSnapshot& guild_castle_snapshot,
                                      RuntimeDispatch& dispatch, std::string target_guild_name) {
  CastleActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;

  if (speaker.character().guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.war_require_guild_template,
                                                 "Join a guild before declaring war.");
    result.details.push_back("Requirement: Guild lord with enough gold.");
    return result;
  }

  const auto* own_guild =
      find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (own_guild == nullptr) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_missing_guild_template,
                                    "Guild data is unavailable. Try again in a moment."),
        castle_dialog_context, speaker.character().guild_name);
    result.details.push_back("Guild snapshot is missing for " + speaker.character().guild_name + ".");
    return result;
  }
  if (!equals_ignore_case(own_guild->lord, speaker.character().character_name)) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_only_lord_template,
                                    "Only the guild lord can declare war."),
        castle_dialog_context, own_guild->guild_name);
    result.details.push_back("Guild lord: " + own_guild->lord);
    return result;
  }

  target_guild_name = util::trim(std::move(target_guild_name));
  if (target_guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.war_usage_template,
                                                 "Usage: @castle war <guild_name>");
    result.details.push_back("Choose a rival guild from the war target list.");
    return result;
  }
  if (equals_ignore_case(target_guild_name, own_guild->guild_name)) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_self_target_template,
                                    "Your guild cannot declare war on itself."),
        castle_dialog_context, own_guild->guild_name, target_guild_name);
    result.details.push_back("Your Guild: " + own_guild->guild_name);
    return result;
  }
  if (find_guild_state(guild_castle_snapshot, target_guild_name) == nullptr) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_target_missing_template,
                                    "Target guild not found."),
        castle_dialog_context, own_guild->guild_name, target_guild_name);
    result.details.push_back("Target Guild: " + target_guild_name);
    return result;
  }

  auto wars = parse_castle_war_list(castle_dialog_context);
  const auto already_listed =
      std::any_of(wars.begin(), wars.end(), [&](const std::string& guild_name) {
        return equals_ignore_case(guild_name, target_guild_name);
      });
  if (already_listed) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_already_registered_template,
                                    "Castle war against <$TARGETGUILD> is already registered."),
        castle_dialog_context, own_guild->guild_name, target_guild_name);
    result.details.push_back("Wars: " + display_castle_wars(castle_dialog_context));
    return result;
  }
  if (!speaker.can_spend_gold(castle_dialog_context.guild_war_fee)) {
    result.summary = render_castle_summary_template(
        configured_summary_template(castle_dialog_context.war_need_gold_template,
                                    "You need <$GOLD> gold to declare war."),
        castle_dialog_context, own_guild->guild_name, target_guild_name,
        castle_dialog_context.guild_war_fee);
    result.details.push_back("Gold: " + std::to_string(speaker.character().gold));
    return result;
  }

  speaker.spend_gold(castle_dialog_context.guild_war_fee);
  queue_save_character(dispatch, speaker);
  wars.push_back(target_guild_name);
  castle_dialog_context.list_of_war = join_tokens(wars, 0, ", ");
  queue_save_castle_state(dispatch, castle_dialog_context);

  result.success = true;
  result.summary = render_castle_summary_template(
      default_castle_war_summary_template(castle_dialog_context), castle_dialog_context,
      own_guild->guild_name, target_guild_name, castle_dialog_context.guild_war_fee);
  result.details.push_back("Castle: " + default_castle_name(castle_dialog_context));
  result.details.push_back("Target Guild: " + target_guild_name);
  result.details.push_back("War Fee: " + std::to_string(castle_dialog_context.guild_war_fee));
  result.details.push_back("Gold: " + std::to_string(speaker.character().gold));
  result.details.push_back("Wars: " + display_castle_wars(castle_dialog_context));
  result.details.push_back("Owner Guild: " + display_castle_owner(castle_dialog_context));
  result.details.push_back("Owner Lord: " + display_castle_lord(castle_dialog_context));
  result.details.push_back("War Count: " + std::to_string(wars.size()));
  result.details.push_back("War Preview: " + summarize_castle_wars(castle_dialog_context));
  result.details.push_back("Guild Role Change: " + own_guild->guild_name + " -> " +
                           default_castle_role_change_challenger_label(castle_dialog_context));
  return result;
}

/**
 * @brief 构建城堡操作结果对话框文本
 * @details 显示操作成功/失败状态、摘要、城堡快照、拥有者快照、目标快照、战争快照和角色变更等信息
 * @param title 对话框标题
 * @param result 城堡操作结果
 * @param back_action 返回按钮的 @action 命令
 * @return 对话框文本
 */
std::string build_castle_action_result_dialog_text(std::string title,
                                                   const CastleActionResult& result,
                                                   std::string back_action) {
  const auto find_detail = [&](std::string_view prefix) -> std::string {
    for (const auto& line : result.details) {
      if (line.rfind(prefix, 0) == 0) {
        return line.substr(prefix.size());
      }
    }
    return {};
  };

  std::string text = std::move(title);
  text.push_back('\\');
  append_dialog_line(text, result.success ? "Result: Success" : "Result: Failed");
  append_dialog_line(text, "Summary: " + result.summary);
  const auto castle_name = find_detail("Castle: ");
  if (!castle_name.empty()) {
    append_dialog_line(text, "Castle Snapshot: " + castle_name);
  }
  const auto owner_guild = find_detail("Owner Guild: ");
  const auto owner_lord = find_detail("Owner Lord: ");
  if (!owner_guild.empty() || !owner_lord.empty()) {
    append_dialog_line(text,
                       "Owner Snapshot: " +
                           (owner_guild.empty() ? std::string("-") : owner_guild) + " / " +
                           (owner_lord.empty() ? std::string("-") : owner_lord));
  }
  const auto target_guild = find_detail("Target Guild: ");
  if (!target_guild.empty()) {
    append_dialog_line(text, "Target Snapshot: " + target_guild);
  }
  const auto war_count = find_detail("War Count: ");
  const auto war_preview = find_detail("War Preview: ");
  if (!war_count.empty() || !war_preview.empty()) {
    append_dialog_line(text, "War Snapshot: " +
                                 (war_count.empty() ? std::string("-") : war_count) + " / " +
                                 (war_preview.empty() ? std::string("None") : war_preview));
  }
  const auto role_change = find_detail("Guild Role Change: ");
  if (!role_change.empty()) {
    append_dialog_line(text, "Role Change: " + role_change);
  }
  for (const auto& line : result.details) {
    append_dialog_line(text, line);
  }
  append_dialog_entry(text, "Castle", "@castle_show");
  append_dialog_entry(text, "Back", std::move(back_action));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 执行行会申请操作
 * @details 验证玩家未加入行会、目标行会存在且尚未申请，添加申请者并通知行会领主
 * @param speaker 执行操作的玩家
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @param guild_name 目标行会名称
 * @return 操作结果
 */
GuildActionResult execute_guild_apply_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string guild_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  guild_name = util::trim(std::move(guild_name));

  if (!speaker.character().guild_name.empty()) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_apply_leave_current_template,
                                    "Leave your current guild before joining another."),
        speaker.character().guild_name);
    result.details.push_back("Current Guild: " + speaker.character().guild_name);
    return result;
  }
  if (guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.guild_apply_choose_guild_template,
                                                 "Choose a guild first.");
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
  if (guild_state == nullptr) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_not_found_template,
                                    "Guild not found."),
        guild_name);
    result.details.push_back("Requested Guild: " + guild_name);
    return result;
  }
  if (guild_has_applicant(*guild_state, speaker.character().character_name)) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_apply_already_pending_template,
                                    "Your application to <$GUILD> is already pending."),
        guild_state->guild_name);
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Lord: " + guild_state->lord);
    return result;
  }

  add_guild_applicant(*guild_state, speaker.character().character_name);
  queue_save_guild_state(dispatch, *guild_state);
  if (auto* guild_lord = find_online_player_by_name(objects, guild_state->lord);
      guild_lord != nullptr) {
    queue_system_notice(dispatch, *guild_lord,
                        render_guild_notice_template(
                            configured_summary_template(
                                castle_dialog_context.guild_apply_alert_template,
                                "<$TARGET> applied to join <$GUILD>."),
                            guild_state->guild_name, speaker.character().character_name));
  }

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_apply_summary_template,
                                  "Application sent to guild <$GUILD>."),
      guild_state->guild_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Lord: " + guild_state->lord);
  result.details.push_back("Applicants Pending: " +
                           std::to_string(guild_state->applicants.size()));
  return result;
}

/**
 * @brief 执行行会创建操作
 * @details 验证玩家未加入行会、名称可用且金币充足，创建行会并设置创始人为领主
 * @param speaker 执行操作的玩家
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @param guild_name 新行会名称
 * @return 操作结果
 */
GuildActionResult execute_guild_create_action(
    Player& speaker, GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch,
    std::string guild_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  guild_name = util::trim(std::move(guild_name));
  const auto guild_create_fee = castle_dialog_context.guild_create_fee;

  if (!speaker.character().guild_name.empty()) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_create_leave_current_template,
                                    "Leave your current guild before creating a new one."),
        speaker.character().guild_name);
    result.details.push_back("Current Guild: " + speaker.character().guild_name);
    return result;
  }
  if (guild_name.empty()) {
    result.summary = configured_summary_template(castle_dialog_context.guild_create_choose_name_template,
                                                 "Choose a guild name first.");
    return result;
  }
  if (find_guild_state(guild_castle_snapshot, guild_name) != nullptr) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_create_name_unavailable_template,
                                    "That guild already exists."),
        guild_name);
    result.details.push_back("Guild: " + guild_name);
    result.details.push_back("Status: Name Unavailable");
    return result;
  }
  if (guild_create_fee > 0 && !speaker.can_spend_gold(guild_create_fee)) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_create_need_gold_template,
                                    "You need <$GOLD> gold to found a guild."),
        guild_name, {}, {}, {}, guild_create_fee);
    result.details.push_back("Guild: " + guild_name);
    result.details.push_back("Status: Need Gold");
    result.details.push_back("Creation Fee: " + std::to_string(guild_create_fee));
    result.details.push_back("Gold: " + std::to_string(speaker.character().gold));
    return result;
  }

  GuildState guild_state;
  guild_state.guild_name = guild_name;
  guild_state.lord = speaker.character().character_name;
  guild_state.members.push_back(speaker.character().character_name);
  guild_castle_snapshot.guilds.push_back(guild_state);
  if (guild_create_fee > 0) {
    speaker.spend_gold(guild_create_fee);
  }
  speaker.set_guild_membership(guild_name, "Lord");
  if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
    castle_dialog_context.lord = guild_state.lord;
  }

  queue_save_guild_state(dispatch, guild_state);
  queue_save_character(dispatch, speaker);

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_create_summary_template,
                                  "Guild <$GUILD> created."),
      guild_name);
  result.details.push_back("Guild: " + guild_name);
  result.details.push_back("Status: Founded");
  result.details.push_back("Lord: " + guild_state.lord);
  result.details.push_back("Role: Lord");
  result.details.push_back("Creation Fee: " + std::to_string(guild_create_fee));
  result.details.push_back("Gold: " + std::to_string(speaker.character().gold));
  result.details.push_back("Members Remaining: 1");
  result.details.push_back("Applicants Remaining: 0");
  return result;
}

/**
 * @brief 执行撤回行会申请操作
 * @details 验证玩家有待处理的申请，移除申请者并通知行会领主
 * @param speaker 执行操作的玩家
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @param guild_name 目标行会名称
 * @return 操作结果
 */
GuildActionResult execute_guild_withdraw_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string guild_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  guild_name = util::trim(std::move(guild_name));

  if (guild_name.empty()) {
    result.summary = "Choose a guild first.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild not found.";
    result.details.push_back("Requested Guild: " + guild_name);
    return result;
  }
  if (!guild_has_applicant(*guild_state, speaker.character().character_name)) {
    result.summary = "No pending application for guild " + guild_state->guild_name + ".";
    result.details.push_back("Applicant: " + speaker.character().character_name);
    return result;
  }

  remove_guild_applicant(*guild_state, speaker.character().character_name);
  queue_save_guild_state(dispatch, *guild_state);
  if (auto* guild_lord = find_online_player_by_name(objects, guild_state->lord);
      guild_lord != nullptr) {
    queue_system_notice(dispatch, *guild_lord,
                        render_guild_notice_template(
                            configured_summary_template(
                                castle_dialog_context.guild_withdraw_alert_template,
                                "<$TARGET> withdrew the application to <$GUILD>."),
                            guild_state->guild_name, speaker.character().character_name));
  }

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_withdraw_summary_template,
                                  "Withdrew application from guild <$GUILD>."),
      guild_state->guild_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Lord: " + guild_state->lord);
  result.details.push_back("Applicants Pending: " +
                           std::to_string(guild_state->applicants.size()));
  return result;
}

/**
 * @brief 执行批准申请者操作
 * @details 验证领主身份和申请有效性，将申请者加入行会并发送通知。
 *          如果申请者离线则投递离线加载请求
 * @param speaker 执行操作的玩家（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @param applicant_name 申请者名称
 * @return 操作结果
 */
GuildActionResult execute_guild_approve_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch,
    std::string applicant_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  applicant_name = util::trim(std::move(applicant_name));

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can manage applications.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (!guild_has_applicant(*guild_state, applicant_name)) {
    result.summary = "That character has no pending application.";
    return result;
  }

  auto* applicant = find_online_player_by_name(objects, applicant_name);
  if (applicant == nullptr) {
    queue_load_offline_guild_character(
        dispatch, OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::approve, speaker.map_id(),
                                          speaker.id(), guild_state->guild_name, applicant_name, {}});
    result.status = "Pending";
    result.summary = "Queued guild approval for offline applicant " + applicant_name + ".";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Status: Offline");
    return result;
  }

  if (!applicant->character().guild_name.empty()) {
    remove_guild_applicant(*guild_state, applicant_name);
    queue_save_guild_state(dispatch, *guild_state);
    result.status = "Success";
    result.summary = applicant_name + " is already in another guild. Application cleared.";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Status: Online");
    return result;
  }

  remove_guild_applicant(*guild_state, applicant_name);
  add_guild_member(*guild_state, applicant->character().character_name);
  applicant->set_guild_membership(guild_state->guild_name, "Member");
  queue_save_guild_state(dispatch, *guild_state);
  queue_save_character(dispatch, *applicant);
  queue_system_notice(dispatch, *applicant,
                      render_guild_notice_template(
                          configured_summary_template(
                              castle_dialog_context.guild_approved_notice_template,
                              "Your application to <$GUILD> was approved."),
                          guild_state->guild_name));

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_approve_summary_template,
                                  "Approved guild application for <$TARGET>."),
      guild_state->guild_name, applicant_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Status: Online");
  result.details.push_back("Role: Member");
  result.details.push_back("Applicants Remaining: " +
                           std::to_string(guild_state->applicants.size()));
  return result;
}

/**
 * @brief 执行拒绝申请者操作
 * @details 验证领主身份，移除申请者并发送拒绝通知
 * @param speaker 执行操作的玩家（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @param applicant_name 申请者名称
 * @return 操作结果
 */
GuildActionResult execute_guild_reject_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch,
    std::string applicant_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  applicant_name = util::trim(std::move(applicant_name));

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can manage applications.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (!guild_has_applicant(*guild_state, applicant_name)) {
    result.summary = "That character has no pending application.";
    return result;
  }

  const auto applicant_online = find_online_player_by_name(objects, applicant_name) != nullptr;
  remove_guild_applicant(*guild_state, applicant_name);
  queue_save_guild_state(dispatch, *guild_state);
  if (auto* applicant = find_online_player_by_name(objects, applicant_name); applicant != nullptr) {
    queue_system_notice(dispatch, *applicant,
                        render_guild_notice_template(
                            configured_summary_template(
                                castle_dialog_context.guild_rejected_notice_template,
                                "Your application to <$GUILD> was rejected."),
                            guild_state->guild_name));
  }

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_reject_summary_template,
                                  "Rejected guild application for <$TARGET>."),
      guild_state->guild_name, applicant_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back(std::string("Status: ") + (applicant_online ? "Online" : "Offline"));
  result.details.push_back("Applicants Remaining: " +
                           std::to_string(guild_state->applicants.size()));
  return result;
}

/**
 * @brief 执行踢出行会成员操作
 * @details 验证领主身份，移除成员并清除其行会成员关系。
 *          如果成员离线则投递离线加载请求
 * @param speaker 执行操作的玩家（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @param member_name 要踢出的成员名称
 * @return 操作结果
 */
GuildActionResult execute_guild_kick_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string member_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  member_name = util::trim(std::move(member_name));

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can kick members.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (member_name.empty()) {
    result.summary = "Usage: @guild kick <member_name>";
    return result;
  }
  if (equals_ignore_case(member_name, speaker.character().character_name)) {
    result.summary = "Use @guild leave to remove yourself from the guild.";
    return result;
  }
  if (!guild_has_member(*guild_state, member_name)) {
    result.summary = "That character is not a guild member.";
    return result;
  }

  auto* member = find_online_player_by_name(objects, member_name);
  if (member == nullptr ||
      !equals_ignore_case(member->character().guild_name, guild_state->guild_name)) {
    queue_load_offline_guild_character(
        dispatch, OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::kick, speaker.map_id(),
                                          speaker.id(), guild_state->guild_name, member_name, {}});
    result.status = "Pending";
    result.summary = "Queued member removal for offline guild member " + member_name + ".";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Status: Offline");
    return result;
  }

  remove_guild_member(*guild_state, member_name);
  member->clear_guild_membership();
  queue_save_guild_state(dispatch, *guild_state);
  queue_save_character(dispatch, *member);
  queue_system_notice(dispatch, *member,
                      render_guild_notice_template(
                          configured_summary_template(
                              castle_dialog_context.guild_removed_notice_template,
                              "You were removed from guild <$GUILD>."),
                          guild_state->guild_name));

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_kick_summary_template,
                                  "Kicked guild member <$TARGET>."),
      guild_state->guild_name, member_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Status: Online");
  result.details.push_back("Members Remaining: " +
                           std::to_string(guild_state->members.size()));
  return result;
}

/**
 * @brief 执行修改行会成员头衔操作
 * @details 验证领主身份和目标成员有效性，修改成员头衔并发送通知。
 *          如果成员离线则投递离线加载请求
 * @param speaker 执行操作的玩家（必须是领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @param target_name 目标成员名称
 * @param title_name 新头衔名称
 * @return 操作结果
 */
GuildActionResult execute_guild_title_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string target_name,
    std::string title_name) {
  GuildActionResult result;
  result.handled = true;
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  target_name = util::trim(std::move(target_name));
  title_name = util::trim(std::move(title_name));

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  const auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can change member titles.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (title_name.empty()) {
    result.summary = "Usage: @guild title <member_name> <title>";
    return result;
  }
  if (!guild_has_member(*guild_state, target_name)) {
    result.summary = "That character is not a guild member.";
    return result;
  }
  if (equals_ignore_case(target_name, guild_state->lord)) {
    result.summary = "Use @guild transfer to change the guild lord.";
    return result;
  }

  auto* target = find_online_player_by_name(objects, target_name);
  if (target == nullptr ||
      !equals_ignore_case(target->character().guild_name, guild_state->guild_name)) {
    queue_load_offline_guild_character(
        dispatch, OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::title, speaker.map_id(),
                                          speaker.id(), guild_state->guild_name, target_name,
                                          title_name});
    result.status = "Pending";
    result.summary = "Queued guild title change for offline member " + target_name + ".";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Status: Offline");
    result.details.push_back("New Title: " + title_name);
    return result;
  }

  target->set_guild_membership(guild_state->guild_name, title_name);
  queue_save_character(dispatch, *target);
  queue_system_notice(dispatch, *target,
                      render_guild_notice_template(
                          configured_summary_template(
                              castle_dialog_context.guild_title_changed_notice_template,
                              "Your guild title is now <$TITLE>."),
                          guild_state->guild_name, {}, title_name));

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_title_summary_template,
                                  "Set guild title for <$TARGET> to <$TITLE>."),
      guild_state->guild_name, target->character().character_name, title_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Status: Online");
  result.details.push_back("New Title: " + title_name);
  return result;
}

/**
 * @brief 执行行会领导权转让操作
 * @details 验证领主身份和目标成员有效性，交换领主和成员的头衔，
 *          更新城堡领主信息（如果该行会拥有城堡）。
 *          如果成员离线则投递离线加载请求
 * @param speaker 执行操作的玩家（当前领主）
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @param target_name 目标成员名称（将成为新领主）
 * @return 操作结果
 */
GuildActionResult execute_guild_transfer_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch, std::string target_name) {
  GuildActionResult result;
  result.handled = true;
  target_name = util::trim(std::move(target_name));
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
  if (guild_state == nullptr) {
    result.summary = "Guild data is unavailable. Try again in a moment.";
    return result;
  }
  if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
    result.summary = "Only the guild lord can transfer leadership.";
    result.details.push_back("Guild lord: " + guild_state->lord);
    return result;
  }
  if (target_name.empty()) {
    result.summary = "Usage: @guild transfer <member_name>";
    return result;
  }
  if (equals_ignore_case(target_name, speaker.character().character_name) ||
      equals_ignore_case(target_name, guild_state->lord)) {
    result.summary = "You already lead this guild.";
    return result;
  }
  if (!guild_has_member(*guild_state, target_name)) {
    result.summary = "That character is not a guild member.";
    return result;
  }

  auto* target = find_online_player_by_name(objects, target_name);
  if (target == nullptr ||
      !equals_ignore_case(target->character().guild_name, guild_state->guild_name)) {
    queue_load_offline_guild_character(
        dispatch, OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::transfer, speaker.map_id(),
                                          speaker.id(), guild_state->guild_name, target_name, {}});
    result.status = "Pending";
    result.summary = "Queued leadership transfer to offline member " + target_name + ".";
    result.details.push_back("Guild: " + guild_state->guild_name);
    result.details.push_back("Current Lord: " + guild_state->lord);
    result.details.push_back("Status: Offline");
    return result;
  }

  const auto previous_lord = guild_state->lord;
  guild_state->lord = target->character().character_name;
  speaker.set_guild_membership(guild_state->guild_name, "Member");
  target->set_guild_membership(guild_state->guild_name, "Lord");
  queue_save_guild_state(dispatch, *guild_state);
  queue_save_character(dispatch, speaker);
  queue_save_character(dispatch, *target);
  if (equals_ignore_case(castle_dialog_context.owner_guild, guild_state->guild_name)) {
    castle_dialog_context.lord = guild_state->lord;
    queue_save_castle_state(dispatch, castle_dialog_context);
  }
  queue_system_notice(dispatch, *target,
                      render_guild_notice_template(
                          configured_summary_template(
                              castle_dialog_context.guild_new_lord_notice_template,
                              "You are now the guild lord of <$GUILD>."),
                          guild_state->guild_name));

  result.status = "Success";
  result.summary = render_guild_summary_template(
      configured_summary_template(castle_dialog_context.guild_transfer_summary_template,
                                  "Transferred guild leadership to <$TARGET>."),
      guild_state->guild_name, target_name);
  result.details.push_back("Guild: " + guild_state->guild_name);
  result.details.push_back("Previous Lord: " + previous_lord);
  result.details.push_back("New Lord: " + guild_state->lord);
  result.details.push_back("Status: Online");
  return result;
}

/**
 * @brief 执行退出行会操作
 * @details 清除玩家行会成员关系。如果玩家是领主则转让领导权；
 *          如果行会仅剩一人则解散行会并清理城堡信息
 * @param speaker 执行操作的玩家
 * @param objects 游戏对象映射表
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @return 操作结果
 */
GuildActionResult execute_guild_leave_action(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    GuildCastleSnapshot& guild_castle_snapshot, RuntimeDispatch& dispatch) {
  GuildActionResult result;
  result.handled = true;

  if (speaker.character().guild_name.empty()) {
    result.summary = "You are not in a guild.";
    return result;
  }

  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  const auto guild_name = speaker.character().guild_name;
  const auto character_name = speaker.character().character_name;
  auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
  const auto was_lord = guild_state != nullptr && equals_ignore_case(guild_state->lord, character_name);

  speaker.clear_guild_membership();
  queue_save_character(dispatch, speaker);

  if (guild_state == nullptr) {
    result.status = "Success";
    result.summary = configured_summary_template(
        castle_dialog_context.guild_membership_cleared_summary_template,
        "Guild membership cleared.");
    result.details.push_back("Former Guild: " + guild_name);
    return result;
  }

  remove_guild_member(*guild_state, character_name);
  if (guild_state->members.empty()) {
    guild_castle_snapshot.guilds.erase(
        std::remove_if(guild_castle_snapshot.guilds.begin(), guild_castle_snapshot.guilds.end(),
                       [&](const GuildState& entry) {
                         return equals_ignore_case(entry.guild_name, guild_name);
                       }),
        guild_castle_snapshot.guilds.end());
    queue_delete_guild(dispatch, guild_name);
    if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
      castle_dialog_context.owner_guild.clear();
      castle_dialog_context.lord.clear();
      castle_dialog_context.list_of_war.clear();
      queue_save_castle_state(dispatch, castle_dialog_context);
    }

    result.status = "Success";
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_disband_summary_template,
                                    "Guild <$GUILD> has been disbanded."),
        guild_name);
    result.details.push_back("Former Guild: " + guild_name);
    result.details.push_back("Members Remaining: 0");
    return result;
  }

  if (was_lord) {
    guild_state->lord = guild_state->members.front();
    if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
      castle_dialog_context.lord = guild_state->lord;
      queue_save_castle_state(dispatch, castle_dialog_context);
    }
      if (auto* next_lord = find_online_player_by_name(objects, guild_state->lord);
          next_lord != nullptr && equals_ignore_case(next_lord->character().guild_name, guild_name)) {
        next_lord->set_guild_membership(guild_name, "Lord");
        queue_save_character(dispatch, *next_lord);
        queue_system_notice(dispatch, *next_lord,
                            render_guild_notice_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_new_lord_notice_template,
                                    "You are now the guild lord of <$GUILD>."),
                                guild_name));
      }
    }

  queue_save_guild_state(dispatch, *guild_state);
  result.status = "Success";
  if (was_lord) {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_leave_transfer_summary_template,
                                    "You left <$GUILD>. New lord: <$NEWLORD>."),
        guild_name, {}, {}, guild_state->lord);
    result.details.push_back("New Lord: " + guild_state->lord);
  } else {
    result.summary = render_guild_summary_template(
        configured_summary_template(castle_dialog_context.guild_leave_summary_template,
                                    "You left <$GUILD>."),
        guild_name);
  }
  result.details.push_back("Former Guild: " + guild_name);
  result.details.push_back("Members Remaining: " +
                           std::to_string(guild_state->members.size()));
  return result;
}

/**
 * @brief 构建行会操作结果对话框文本
 * @details 显示操作状态、摘要、行会快照、成员/申请者数量、领导权变更、头衔更新和资金信息
 * @param title 对话框标题
 * @param result 行会操作结果
 * @param back_action 返回按钮的 @action 命令
 * @param guild_action 行会信息按钮的 @action 命令（默认 @guild_info）
 * @return 对话框文本
 */
std::string build_guild_action_result_dialog_text(std::string title,
                                                  const GuildActionResult& result,
                                                  std::string back_action,
                                                  std::string guild_action = "@guild_info") {
  const auto find_detail = [&](std::string_view prefix) -> std::string {
    for (const auto& line : result.details) {
      if (line.rfind(prefix, 0) == 0) {
        return line.substr(prefix.size());
      }
    }
    return {};
  };

  std::string text = std::move(title);
  text.push_back('\\');
  append_dialog_line(text, "Result: " + result.status);
  append_dialog_line(text, "Summary: " + result.summary);
  const auto guild_name = [&]() -> std::string {
    for (const auto prefix : {std::string_view{"Guild: "}, std::string_view{"Former Guild: "},
                              std::string_view{"Current Guild: "}}) {
      const auto value = find_detail(prefix);
      if (!value.empty()) {
        return value;
      }
    }
    return {};
  }();
  if (!guild_name.empty()) {
    append_dialog_line(text, "Guild Snapshot: " + guild_name);
  }
  const auto status = find_detail("Status: ");
  if (!status.empty()) {
    append_dialog_line(text, "Guild Status: " + status);
  }
  const auto members_remaining = find_detail("Members Remaining: ");
  const auto applicants_remaining = find_detail("Applicants Remaining: ");
  if (!members_remaining.empty() || !applicants_remaining.empty()) {
    append_dialog_line(text,
                       "Counts: " + (members_remaining.empty() ? std::string("-")
                                                               : members_remaining) +
                           "/" +
                           (applicants_remaining.empty() ? std::string("-")
                                                         : applicants_remaining));
  }
  const auto previous_lord = find_detail("Previous Lord: ");
  const auto new_lord = find_detail("New Lord: ");
  if (!previous_lord.empty() || !new_lord.empty()) {
    append_dialog_line(text, "Leadership: " +
                                 (previous_lord.empty() ? std::string("-") : previous_lord) +
                                 " -> " +
                                 (new_lord.empty() ? std::string("-") : new_lord));
  }
  const auto new_title = find_detail("New Title: ");
  if (!new_title.empty()) {
    append_dialog_line(text, "Title Update: " + new_title);
  }
  const auto creation_fee = find_detail("Creation Fee: ");
  const auto gold = find_detail("Gold: ");
  if (!creation_fee.empty() || !gold.empty()) {
    append_dialog_line(text, "Treasury: " +
                                 (creation_fee.empty() ? std::string("-") : creation_fee) +
                                 " / " +
                                 (gold.empty() ? std::string("-") : gold));
  }
  for (const auto& line : result.details) {
    append_dialog_line(text, line);
  }
  append_dialog_entry(text, "Guild", std::move(guild_action));
  append_dialog_entry(text, "Back", std::move(back_action));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建城堡服务菜单对话框文本
 * @details 显示城堡信息和操作入口，行会领主可进行认领和宣战操作
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @return 对话框文本
 */
std::string build_castle_service_dialog_text(const Player& requester,
                                             const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Castle Office\\";
  append_dialog_entry(text, "Show Castle", "@castle_show");
  append_dialog_entry(text, "Active Wars", "@castle_wars");

  const auto& character = requester.character();
  if (!character.guild_name.empty()) {
    const auto* own_guild = find_guild_state(guild_castle_snapshot, character.guild_name);
    if (own_guild != nullptr &&
        equals_ignore_case(own_guild->lord, character.character_name)) {
      append_dialog_entry(text, "Claim Castle", "@castle_claim_confirm");
      append_dialog_entry(text, "Declare War", "@castle_war_targets");
    }
  }

  append_dialog_entry(text, "Back", "@main");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建城堡战争列表对话框文本
 * @details 分页显示当前活跃的战争列表，每项包含行会摘要和浏览入口
 * @param guild_castle_snapshot 行会城堡快照
 * @param requested_page 请求的页码
 * @return 对话框文本
 */
std::string build_castle_wars_dialog_text(const GuildCastleSnapshot& guild_castle_snapshot,
                                          std::size_t requested_page) {
  std::string text = "Castle Wars\\";
  const auto wars = parse_castle_war_list(guild_castle_snapshot.castle_dialog);
  const auto total_pages = dialog_total_pages(wars.size());
  const auto page = clamp_dialog_page(requested_page, wars.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(wars.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, default_castle_name(guild_castle_snapshot.castle_dialog) + " (" +
                               std::to_string(static_cast<int>(page)) + "/" +
                               std::to_string(static_cast<int>(total_pages)) + ")");

  if (wars.empty()) {
    append_dialog_line(text, no_active_wars_text(guild_castle_snapshot.castle_dialog));
  } else {
    for (std::size_t index = start; index < end; ++index) {
      append_dialog_line(text, "War: " + wars[index]);
      append_castle_guild_list_summary(text, guild_castle_snapshot, wars[index]);
      append_dialog_entry(text, "View " + wars[index],
                          "@castle_guild_browse wars " +
                              std::to_string(static_cast<int>(page)) + " " + wars[index]);
    }
  }

  append_page_navigation(text, "@castle_wars", page, total_pages);
  append_dialog_entry(text, "Back", "@castle_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建宣战目标选择对话框文本
 * @details 分页显示可宣战的行会列表（排除自身），提供浏览和宣战入口
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param requested_page 请求的页码
 * @return 对话框文本
 */
std::string build_castle_war_targets_dialog_text(const Player& requester,
                                                 const GuildCastleSnapshot& guild_castle_snapshot,
                                                 std::size_t requested_page) {
  std::string text = "Declare Castle War\\";
  std::vector<std::string> targets;
  for (const auto& guild_state : guild_castle_snapshot.guilds) {
    if (!equals_ignore_case(guild_state.guild_name, requester.character().guild_name)) {
      targets.push_back(guild_state.guild_name);
    }
  }

  const auto total_pages = dialog_total_pages(targets.size());
  const auto page = clamp_dialog_page(requested_page, targets.size());
  const auto start = (page - 1) * kNpcDialogPageSize;
  const auto end = std::min<std::size_t>(targets.size(), start + kNpcDialogPageSize);
  append_dialog_line(text, default_castle_name(guild_castle_snapshot.castle_dialog) + " (" +
                               std::to_string(static_cast<int>(page)) + "/" +
                               std::to_string(static_cast<int>(total_pages)) + ")");

  if (targets.empty()) {
    append_dialog_line(text, "No rival guilds are available.");
  } else {
    for (std::size_t index = start; index < end; ++index) {
      append_dialog_line(text, "Guild: " + targets[index]);
      append_castle_guild_list_summary(text, guild_castle_snapshot, targets[index]);
      append_dialog_entry(text, "View " + targets[index],
                          "@castle_guild_browse targets " +
                              std::to_string(static_cast<int>(page)) + " " + targets[index]);
      append_dialog_entry(text, "War " + targets[index],
                          "@castle_war_confirm " + std::to_string(static_cast<int>(page)) + " " +
                              targets[index]);
    }
  }

  append_page_navigation(text, "@castle_war_targets", page, total_pages);
  append_dialog_entry(text, "Back", "@castle_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建城堡行会浏览对话框文本
 * @details 显示目标行会的详细战争信息、行会数据和城堡角色，
 *          从宣战目标列表进入时可执行宣战操作
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 城堡行会浏览目标
 * @return 对话框文本
 */
std::string build_castle_guild_browse_dialog_text(const Player& requester,
                                                  const GuildCastleSnapshot& guild_castle_snapshot,
                                                  const CastleGuildBrowseTarget& target) {
  std::string text = target.source == "targets" ? "Castle War Target\\" : "Castle War Detail\\";
  const auto back_action =
      target.source == "targets"
          ? "@castle_war_targets " + std::to_string(static_cast<int>(target.page))
          : "@castle_wars " + std::to_string(static_cast<int>(target.page));
  const auto wars = parse_castle_war_list(guild_castle_snapshot.castle_dialog);
  const auto active_war =
      std::any_of(wars.begin(), wars.end(), [&](const std::string& guild_name) {
        return equals_ignore_case(guild_name, target.guild_name);
      });

  append_dialog_line(text, "Castle: " + default_castle_name(guild_castle_snapshot.castle_dialog));
  append_dialog_line(text, "Target Guild: " + target.guild_name);
  append_dialog_line(text, "War Entry: " +
                               (active_war
                                    ? default_castle_war_entry_listed_label(
                                          guild_castle_snapshot.castle_dialog)
                                    : default_castle_war_entry_unlisted_label(
                                          guild_castle_snapshot.castle_dialog)));

  if (const auto* guild_state = find_guild_state(guild_castle_snapshot, target.guild_name);
      guild_state != nullptr) {
    append_dialog_line(text, "Lord: " + guild_state->lord);
    append_dialog_line(text, "Members: " + std::to_string(guild_state->members.size()));
    append_dialog_line(text, "Applicants: " + std::to_string(guild_state->applicants.size()));
    append_dialog_line(text, "Roster Preview: " + summarize_name_list(guild_state->members));
    append_dialog_line(text, "Applicant Preview: " + summarize_name_list(guild_state->applicants));
    append_dialog_entry(text, "View Members",
                        "@guild_roster castle_" + target.source + " " +
                            std::to_string(static_cast<int>(target.page)) + " 1 " +
                            guild_state->guild_name);
    if (!guild_state->applicants.empty()) {
      append_dialog_entry(text, "View Applicants",
                          "@guild_applicant_roster castle_" + target.source + " " +
                              std::to_string(static_cast<int>(target.page)) + " 1 " +
                              guild_state->guild_name);
    }
    append_dialog_entry(text, "Browse Guild",
                        "@guild_browse castle_" + target.source + " " +
                            std::to_string(static_cast<int>(target.page)) + " " +
                            guild_state->guild_name);
    if (equals_ignore_case(guild_castle_snapshot.castle_dialog.owner_guild, guild_state->guild_name)) {
      append_dialog_line(text, "Castle Role: " +
                                   default_castle_owner_guild_role_label(
                                       guild_castle_snapshot.castle_dialog));
      append_dialog_line(text, "Castle: Owner of " +
                                   default_castle_name(guild_castle_snapshot.castle_dialog));
      append_dialog_line(text, "Castle Lord: " + display_castle_lord(guild_castle_snapshot.castle_dialog));
    } else if (active_war) {
      append_dialog_line(text, "Castle Role: " +
                                   default_castle_challenger_role_label(
                                       guild_castle_snapshot.castle_dialog));
      append_dialog_line(text, "Castle: None");
    } else {
      append_dialog_line(text, "Castle Role: " +
                                   default_castle_rival_role_label(
                                       guild_castle_snapshot.castle_dialog));
      append_dialog_line(text, "Castle: None");
    }
  } else {
    append_dialog_line(text, "Guild Data: Unknown");
    append_dialog_line(text, "Castle Role: " +
                                 (active_war
                                      ? default_castle_challenger_role_label(
                                            guild_castle_snapshot.castle_dialog)
                                      : default_castle_unknown_role_label(
                                            guild_castle_snapshot.castle_dialog)));
    append_dialog_line(text, "Castle: None");
  }

  append_dialog_line(text, "War Status: " +
                               (active_war
                                    ? default_castle_war_status_active_label(
                                          guild_castle_snapshot.castle_dialog)
                                    : default_castle_war_status_available_label(
                                          guild_castle_snapshot.castle_dialog)));
  if (target.source == "targets") {
    append_dialog_line(text,
                       "War Fee: " + std::to_string(guild_castle_snapshot.castle_dialog.guild_war_fee));
    append_dialog_line(text, "Gold: " + std::to_string(requester.character().gold));
    append_dialog_entry(text, "Confirm War",
                        "@castle_war_confirm " + std::to_string(static_cast<int>(target.page)) +
                            " " + target.guild_name);
  }

  append_dialog_entry(text, "Back", back_action);
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建城堡认领确认对话框文本
 * @details 显示当前拥有者和新拥有者信息，要求领主确认认领操作
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @return 对话框文本
 */
std::string build_castle_claim_confirm_dialog_text(
    const Player& requester, const GuildCastleSnapshot& guild_castle_snapshot) {
  std::string text = "Claim Castle\\";
  const auto& castle = guild_castle_snapshot.castle_dialog;
  append_dialog_line(text, "Castle: " + default_castle_name(castle));
  append_dialog_line(text, "Current Owner: " + display_castle_owner(castle));
  append_dialog_line(text, "New Owner: " + requester.character().guild_name);
  append_dialog_line(text, "Lord: " + requester.character().character_name);
  append_dialog_line(text, "Confirm castle ownership transfer?");
  append_dialog_entry(text, "Confirm", "@castle_claim");
  append_dialog_entry(text, "Back", "@castle_menu");
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

/**
 * @brief 构建宣战确认对话框文本
 * @details 显示宣战双方信息和战争费用，要求领主确认宣战操作
 * @param requester 请求者（玩家）
 * @param guild_castle_snapshot 行会城堡快照
 * @param target 宣战确认目标
 * @return 对话框文本
 */
std::string build_castle_war_confirm_dialog_text(
    const Player& requester, const GuildCastleSnapshot& guild_castle_snapshot,
    const CastleWarConfirmTarget& target) {
  std::string text = "Declare Castle War\\";
  const auto& castle = guild_castle_snapshot.castle_dialog;
  append_dialog_line(text, "Castle: " + default_castle_name(castle));
  append_dialog_line(text, "Your Guild: " + requester.character().guild_name);
  append_dialog_line(text, "Target Guild: " + target.guild_name);
  append_dialog_line(text, "War Fee: " + std::to_string(castle.guild_war_fee));
  append_dialog_line(text, "Gold: " + std::to_string(requester.character().gold));
  append_dialog_line(text, "Confirm castle war registration?");
  append_dialog_entry(text, "Confirm", "@castle_war " + target.guild_name);
  append_dialog_entry(text, "Back",
                      "@castle_war_targets " + std::to_string(static_cast<int>(target.page)));
  append_dialog_entry(text, "Close", "@exit");
  return text;
}

// ============================================================================
//  行会/城堡 NPC 命令处理
//  这些函数处理行会和城堡 NPC 的 @action 命令路由和执行。
// ============================================================================

/**
 * @brief 处理行会和城堡业务命令
 * @details 解析 @action 命令并路由到对应的执行器函数。
 *          支持命令别名标准化（如 @guild_create -> @guild create），
 *          处理行会创建、申请、批准、拒绝、踢出、转让头衔、转让领导权、退出等操作，
 *          以及城堡认领和宣战操作
 * @param speaker 执行命令的玩家
 * @param objects 游戏对象映射表
 * @param payload 原始命令字符串（以 @ 开头）
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @return true 如果命令已被处理
 */
bool handle_guild_castle_business_command(
    Player& speaker, std::unordered_map<std::uint64_t, std::unique_ptr<GameObject>>& objects,
    const std::string& payload,
                                          GuildCastleSnapshot& guild_castle_snapshot,
                                          RuntimeDispatch& dispatch) {
  if (!util::starts_with(payload, "@")) {
    return false;
  }

  const auto tokens = util::split(payload, ' ');
  if (tokens.empty()) {
    return false;
  }

  std::vector<std::string> normalized_tokens;
  const auto command_root = util::lower_copy(tokens[0]);
  if (command_root == "@guild_create") {
    normalized_tokens = {"@guild", "create"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_join") {
    normalized_tokens = {"@guild", "join"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_apply") {
    normalized_tokens = {"@guild", "apply"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_leave") {
    normalized_tokens = {"@guild", "leave"};
  } else if (command_root == "@guild_kick") {
    normalized_tokens = {"@guild", "kick"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_transfer") {
    normalized_tokens = {"@guild", "transfer"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_title") {
    normalized_tokens = {"@guild", "title"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_title_template") {
    normalized_tokens = {"@guild", "title"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_approve") {
    normalized_tokens = {"@guild", "approve"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_reject") {
    normalized_tokens = {"@guild", "reject"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_applicants") {
    normalized_tokens = {"@guild", "applicants"};
  } else if (command_root == "@guild_info" || command_root == "@guild_show") {
    normalized_tokens = {"@guild", "info"};
  } else if (command_root == "@castle_claim") {
    normalized_tokens = {"@castle", "claim"};
  } else if (command_root == "@castle_war") {
    normalized_tokens = {"@castle", "war"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@castle_show") {
    normalized_tokens = {"@castle", "show"};
  } else {
    normalized_tokens = tokens;
  }

  if (normalized_tokens.empty()) {
    return false;
  }

  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  const auto normalized_root = util::lower_copy(normalized_tokens[0]);
  if (normalized_root == "@guild") {
    auto subcommand =
        normalized_tokens.size() >= 2 ? util::lower_copy(normalized_tokens[1]) : std::string{};
    if (subcommand == "create") {
      const auto result =
          execute_guild_create_action(speaker, guild_castle_snapshot, dispatch,
                                      normalized_tokens.size() > 2 ? join_tokens(normalized_tokens, 2)
                                                                   : std::string{});
      queue_system_notice(dispatch, speaker, result.summary);
      return true;
    }

    if (subcommand == "join") {
      normalized_tokens[1] = "apply";
      subcommand = "apply";
    }

    if (subcommand == "apply") {
      const auto result = execute_guild_apply_action(
          speaker, objects, guild_castle_snapshot, dispatch,
          normalized_tokens.size() > 2 ? join_tokens(normalized_tokens, 2) : std::string{});
      queue_system_notice(dispatch, speaker, result.summary);
      return true;
    }

    if (subcommand == "applicants") {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      const auto* guild_state =
          find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can review applications.");
        return true;
      }
      if (guild_state->applicants.empty()) {
        queue_system_notice(dispatch, speaker, "No pending guild applications.");
        return true;
      }
      queue_system_notice(dispatch, speaker,
                          "GuildApplicants=" + join_tokens(guild_state->applicants, 0, ", "));
      return true;
    }

    if (subcommand == "kick" && normalized_tokens.size() >= 3) {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can kick members.");
        return true;
      }

      const auto member_name = util::trim(join_tokens(normalized_tokens, 2));
      if (member_name.empty()) {
        queue_system_notice(dispatch, speaker, "Usage: @guild kick <member_name>");
        return true;
      }
      if (equals_ignore_case(member_name, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Use @guild leave to remove yourself from the guild.");
        return true;
      }
      if (!guild_has_member(*guild_state, member_name)) {
        queue_system_notice(dispatch, speaker, "That character is not a guild member.");
        return true;
      }

      auto* member = find_online_player_by_name(objects, member_name);
      if (member == nullptr ||
          !equals_ignore_case(member->character().guild_name, guild_state->guild_name)) {
        queue_load_offline_guild_character(
            dispatch,
            OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::kick,
                                    speaker.map_id(),
                                    speaker.id(),
                                    guild_state->guild_name,
                                    member_name,
                                    {}});
        return true;
      }

      remove_guild_member(*guild_state, member_name);
      member->clear_guild_membership();
      queue_save_guild_state(dispatch, *guild_state);
      queue_save_character(dispatch, *member);
      queue_system_notice(dispatch, speaker,
                          render_guild_summary_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_kick_summary_template,
                                  "Kicked guild member <$TARGET>."),
                              guild_state->guild_name, member_name));
      queue_system_notice(dispatch, *member,
                          render_guild_notice_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_removed_notice_template,
                                  "You were removed from guild <$GUILD>."),
                              guild_state->guild_name));
      return true;
    }

    if (subcommand == "transfer" && normalized_tokens.size() >= 3) {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can transfer leadership.");
        return true;
      }

      const auto target_name = util::trim(join_tokens(normalized_tokens, 2));
      if (target_name.empty()) {
        queue_system_notice(dispatch, speaker, "Usage: @guild transfer <member_name>");
        return true;
      }
      if (equals_ignore_case(target_name, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "You already lead this guild.");
        return true;
      }
      if (!guild_has_member(*guild_state, target_name)) {
        queue_system_notice(dispatch, speaker, "That character is not a guild member.");
        return true;
      }
      if (equals_ignore_case(target_name, guild_state->lord)) {
        queue_system_notice(dispatch, speaker, "You already lead this guild.");
        return true;
      }

      auto* target = find_online_player_by_name(objects, target_name);
      if (target == nullptr ||
          !equals_ignore_case(target->character().guild_name, guild_state->guild_name)) {
        queue_load_offline_guild_character(
            dispatch,
            OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::transfer,
                                    speaker.map_id(),
                                    speaker.id(),
                                    guild_state->guild_name,
                                    target_name,
                                    {}});
        return true;
      }

      guild_state->lord = target->character().character_name;
      speaker.set_guild_membership(guild_state->guild_name, "Member");
      target->set_guild_membership(guild_state->guild_name, "Lord");
      queue_save_guild_state(dispatch, *guild_state);
      queue_save_character(dispatch, speaker);
      queue_save_character(dispatch, *target);
      if (equals_ignore_case(castle_dialog_context.owner_guild, guild_state->guild_name)) {
        castle_dialog_context.lord = guild_state->lord;
        queue_save_castle_state(dispatch, castle_dialog_context);
      }
      queue_system_notice(dispatch, speaker,
                          render_guild_summary_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_transfer_summary_template,
                                  "Transferred guild leadership to <$TARGET>."),
                              guild_state->guild_name, target_name));
      queue_system_notice(dispatch, *target,
                          render_guild_notice_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_new_lord_notice_template,
                                  "You are now the guild lord of <$GUILD>."),
                              guild_state->guild_name));
      return true;
    }

    if (subcommand == "title" && normalized_tokens.size() >= 4) {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      const auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can change member titles.");
        return true;
      }

      const auto target_name = normalized_tokens[2];
      const auto title_name = util::trim(join_tokens(normalized_tokens, 3));
      if (title_name.empty()) {
        queue_system_notice(dispatch, speaker, "Usage: @guild title <member_name> <title>");
        return true;
      }
      if (!guild_has_member(*guild_state, target_name)) {
        queue_system_notice(dispatch, speaker, "That character is not a guild member.");
        return true;
      }
      if (equals_ignore_case(target_name, guild_state->lord)) {
        queue_system_notice(dispatch, speaker, "Use @guild transfer to change the guild lord.");
        return true;
      }

      auto* target = find_online_player_by_name(objects, target_name);
      if (target == nullptr ||
          !equals_ignore_case(target->character().guild_name, guild_state->guild_name)) {
        queue_load_offline_guild_character(
            dispatch,
            OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::title,
                                    speaker.map_id(),
                                    speaker.id(),
                                    guild_state->guild_name,
                                    target_name,
                                    title_name});
        return true;
      }
      if (equals_ignore_case(target->character().character_name, guild_state->lord)) {
        queue_system_notice(dispatch, speaker, "Use @guild transfer to change the guild lord.");
        return true;
      }

      target->set_guild_membership(guild_state->guild_name, title_name);
      queue_save_character(dispatch, *target);
      queue_system_notice(dispatch, speaker,
                          render_guild_summary_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_title_summary_template,
                                  "Set guild title for <$TARGET> to <$TITLE>."),
                              guild_state->guild_name, target->character().character_name,
                              title_name));
      queue_system_notice(dispatch, *target,
                          render_guild_notice_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_title_changed_notice_template,
                                  "Your guild title is now <$TITLE>."),
                              guild_state->guild_name, {}, title_name));
      return true;
    }

    if ((subcommand == "approve" || subcommand == "reject") && normalized_tokens.size() >= 3) {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      auto* guild_state = find_guild_state(guild_castle_snapshot, speaker.character().guild_name);
      if (guild_state == nullptr) {
        queue_system_notice(dispatch, speaker, "Guild data is unavailable. Try again in a moment.");
        return true;
      }
      if (!equals_ignore_case(guild_state->lord, speaker.character().character_name)) {
        queue_system_notice(dispatch, speaker, "Only the guild lord can manage applications.");
        return true;
      }

      const auto applicant_name = util::trim(join_tokens(normalized_tokens, 2));
      if (!guild_has_applicant(*guild_state, applicant_name)) {
        queue_system_notice(dispatch, speaker, "That character has no pending application.");
        return true;
      }

      if (subcommand == "reject") {
        remove_guild_applicant(*guild_state, applicant_name);
        queue_save_guild_state(dispatch, *guild_state);
        queue_system_notice(dispatch, speaker,
                            render_guild_summary_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_reject_summary_template,
                                    "Rejected guild application for <$TARGET>."),
                                guild_state->guild_name, applicant_name));
        if (auto* applicant = find_online_player_by_name(objects, applicant_name); applicant != nullptr) {
          queue_system_notice(dispatch, *applicant,
                              render_guild_notice_template(
                                  configured_summary_template(
                                      castle_dialog_context.guild_rejected_notice_template,
                                      "Your application to <$GUILD> was rejected."),
                                  guild_state->guild_name));
        }
        return true;
      }

      auto* applicant = find_online_player_by_name(objects, applicant_name);
      if (applicant == nullptr) {
        queue_load_offline_guild_character(
            dispatch,
            OfflineGuildCharacterOp{OfflineGuildCharacterOpKind::approve,
                                    speaker.map_id(),
                                    speaker.id(),
                                    guild_state->guild_name,
                                    applicant_name,
                                    {}});
        return true;
      }
      if (!applicant->character().guild_name.empty()) {
        remove_guild_applicant(*guild_state, applicant_name);
        queue_save_guild_state(dispatch, *guild_state);
        queue_system_notice(dispatch, speaker,
                            applicant_name + " is already in another guild. Application cleared.");
        return true;
      }

      remove_guild_applicant(*guild_state, applicant_name);
      add_guild_member(*guild_state, applicant->character().character_name);
      applicant->set_guild_membership(guild_state->guild_name, "Member");
      queue_save_guild_state(dispatch, *guild_state);
      queue_save_character(dispatch, *applicant);
      queue_system_notice(dispatch, speaker,
                          render_guild_summary_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_approve_summary_template,
                                  "Approved guild application for <$TARGET>."),
                              guild_state->guild_name, applicant_name));
      queue_system_notice(dispatch, *applicant,
                          render_guild_notice_template(
                              configured_summary_template(
                                  castle_dialog_context.guild_approved_notice_template,
                                  "Your application to <$GUILD> was approved."),
                              guild_state->guild_name));
      return true;
    }

    if (subcommand == "leave") {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }

      const auto guild_name = speaker.character().guild_name;
      const auto character_name = speaker.character().character_name;
      auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
      const auto was_lord = guild_state != nullptr && equals_ignore_case(guild_state->lord, character_name);

      speaker.clear_guild_membership();
      queue_save_character(dispatch, speaker);

      if (guild_state == nullptr) {
        queue_system_notice(
            dispatch, speaker,
            configured_summary_template(castle_dialog_context.guild_membership_cleared_summary_template,
                                        "Guild membership cleared."));
        return true;
      }

      remove_guild_member(*guild_state, character_name);
      if (guild_state->members.empty()) {
        guild_castle_snapshot.guilds.erase(
            std::remove_if(guild_castle_snapshot.guilds.begin(), guild_castle_snapshot.guilds.end(),
                           [&](const GuildState& entry) {
                             return equals_ignore_case(entry.guild_name, guild_name);
                           }),
            guild_castle_snapshot.guilds.end());
        queue_delete_guild(dispatch, guild_name);
        if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
          castle_dialog_context.owner_guild.clear();
          castle_dialog_context.lord.clear();
          castle_dialog_context.list_of_war.clear();
          queue_save_castle_state(dispatch, castle_dialog_context);
        }
        queue_system_notice(dispatch, speaker,
                            render_guild_summary_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_disband_summary_template,
                                    "Guild <$GUILD> has been disbanded."),
                                guild_name));
        return true;
      }

      if (was_lord) {
        guild_state->lord = guild_state->members.front();
        if (equals_ignore_case(castle_dialog_context.owner_guild, guild_name)) {
          castle_dialog_context.lord = guild_state->lord;
        }
      }

      queue_save_guild_state(dispatch, *guild_state);
      if (was_lord) {
        queue_system_notice(dispatch, speaker,
                            render_guild_summary_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_leave_transfer_summary_template,
                                    "You left <$GUILD>. New lord: <$NEWLORD>."),
                                guild_name, {}, {}, guild_state->lord));
      } else {
        queue_system_notice(dispatch, speaker,
                            render_guild_summary_template(
                                configured_summary_template(
                                    castle_dialog_context.guild_leave_summary_template,
                                    "You left <$GUILD>."),
                                guild_name));
      }
      return true;
    }

    if (subcommand == "info" || subcommand == "show") {
      if (speaker.character().guild_name.empty()) {
        queue_system_notice(dispatch, speaker, "You are not in a guild.");
        return true;
      }
      queue_system_notice(
          dispatch, speaker,
          build_guild_info_line(
              speaker, guild_castle_snapshot,
              find_guild_state(guild_castle_snapshot, speaker.character().guild_name)));
      return true;
    }

    return false;
  }

  if (normalized_root == "@castle") {
    const auto subcommand =
        normalized_tokens.size() >= 2 ? util::lower_copy(normalized_tokens[1]) : std::string{};
    if (subcommand == "show") {
      queue_system_notice(dispatch, speaker, build_castle_show_line(castle_dialog_context));
      return true;
    }

    if (subcommand == "claim") {
      const auto result = execute_castle_claim(speaker, guild_castle_snapshot, dispatch);
      queue_system_notice(dispatch, speaker, result.summary);
      return true;
    }

    if (subcommand == "war") {
      const auto result = execute_castle_war(speaker, guild_castle_snapshot, dispatch,
                                             normalized_tokens.size() > 2 ? join_tokens(normalized_tokens, 2)
                                                                          : std::string{});
      queue_system_notice(dispatch, speaker, result.summary);
      return true;
    }

    return false;
  }

  return false;
}

/**
 * @brief 处理城堡 GM 管理命令
 * @details 仅管理员账号可使用，支持设置城堡拥有者、战争日期、战争列表、费用等操作，
 *          以及设置行会领主。需要账号以 "gm"、"guest" 或 "admin" 开头
 * @param speaker 执行命令的玩家（需为管理员账号）
 * @param payload 原始命令字符串
 * @param guild_castle_snapshot 行会城堡快照
 * @param dispatch 运行时调度器
 * @return true 如果命令已被处理
 * @see is_admin_account
 */
bool handle_castle_admin_command(const Player& speaker, const std::string& payload,
                                 GuildCastleSnapshot& guild_castle_snapshot,
                                 RuntimeDispatch& dispatch) {
  auto& castle_dialog_context = guild_castle_snapshot.castle_dialog;
  if (!util::starts_with(payload, "@")) {
    return false;
  }

  const auto tokens = util::split(payload, ' ');
  if (tokens.empty()) {
    return false;
  }

  const auto command_root = util::lower_copy(tokens[0]);
  std::vector<std::string> normalized_tokens;
  if (command_root == "@castle_show") {
    normalized_tokens = {"@castle", "show"};
  } else if (command_root == "@castle_owner") {
    normalized_tokens = {"@castle", "owner"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@castle_wardate") {
    normalized_tokens = {"@castle", "wardate"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@castle_wars") {
    normalized_tokens = {"@castle", "wars"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@castle_fees") {
    normalized_tokens = {"@castle", "fees"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else if (command_root == "@guild_lord" || command_root == "@guild_master") {
    normalized_tokens = {"@guild", "lord"};
    normalized_tokens.insert(normalized_tokens.end(), tokens.begin() + 1, tokens.end());
  } else {
    normalized_tokens = tokens;
  }

  const auto normalized_root = util::lower_copy(normalized_tokens[0]);
  if (normalized_root != "@castle" && normalized_root != "@guild") {
    return false;
  }

  if (!is_admin_account(speaker.character().account_id)) {
    queue_system_notice(dispatch, speaker, "GM castle commands are not allowed for this account.");
    return true;
  }

  if (normalized_root == "@castle") {
    if (normalized_tokens.size() >= 2 && util::lower_copy(normalized_tokens[1]) == "show") {
      queue_system_notice(dispatch, speaker, build_castle_show_line(castle_dialog_context));
      return true;
    }

    const auto subcommand =
        normalized_tokens.size() >= 2 ? util::lower_copy(normalized_tokens[1]) : std::string{};
    if (subcommand == "owner" && normalized_tokens.size() >= 3) {
      castle_dialog_context.owner_guild =
          normalize_castle_owner(castle_dialog_context, normalized_tokens[2]);
      sync_castle_lord_from_owner(guild_castle_snapshot);
      queue_save_castle_state(dispatch, castle_dialog_context);
      queue_system_notice(dispatch, speaker, "Castle owner update queued.");
      return true;
    }
    if (subcommand == "wardate" && normalized_tokens.size() >= 3) {
      castle_dialog_context.castle_war_date = join_tokens(normalized_tokens, 2);
      queue_save_castle_state(dispatch, castle_dialog_context);
      queue_system_notice(dispatch, speaker, "Castle war date update queued.");
      return true;
    }
    if (subcommand == "wars" && normalized_tokens.size() >= 3) {
      castle_dialog_context.list_of_war = join_tokens(normalized_tokens, 2);
      queue_save_castle_state(dispatch, castle_dialog_context);
      queue_system_notice(dispatch, speaker, "Castle rival list update queued.");
      return true;
    }
    if (subcommand == "fees" && normalized_tokens.size() >= 4) {
      const auto guild_fee = parse_int32(normalized_tokens[2]);
      const auto upgrade_fee = parse_int32(normalized_tokens[3]);
      if (!guild_fee.has_value() || !upgrade_fee.has_value()) {
        queue_system_notice(dispatch, speaker, "Usage: @castle fees <guild_fee> <upgrade_fee>");
        return true;
      }
      castle_dialog_context.guild_war_fee = *guild_fee;
      castle_dialog_context.upgrade_weapon_fee = *upgrade_fee;
      queue_save_castle_state(dispatch, castle_dialog_context);
      queue_system_notice(dispatch, speaker, "Castle fee update queued.");
      return true;
    }

    queue_system_notice(dispatch, speaker,
                        "Usage: @castle show|owner <guild>|wardate <text>|wars <text>|fees <a> <b>");
    return true;
  }

  if (normalized_root == "@guild") {
    const auto subcommand =
        normalized_tokens.size() >= 2 ? util::lower_copy(normalized_tokens[1]) : std::string{};
    if ((subcommand == "lord" || subcommand == "master") && normalized_tokens.size() >= 4) {
      const auto guild_name = normalized_tokens[2];
      const auto lord_name = join_tokens(normalized_tokens, 3);
      auto* guild_state = find_guild_state(guild_castle_snapshot, guild_name);
      if (guild_state == nullptr) {
        guild_castle_snapshot.guilds.push_back(GuildState{guild_name, lord_name, {lord_name}});
        guild_state = &guild_castle_snapshot.guilds.back();
      } else {
        guild_state->lord = lord_name;
        add_guild_member(*guild_state, lord_name);
      }
      queue_save_guild_state(dispatch, *guild_state);
      if (equals_ignore_case(guild_name, castle_dialog_context.owner_guild)) {
        castle_dialog_context.lord = lord_name;
      }
      queue_system_notice(dispatch, speaker, "Guild lord update queued.");
      return true;
    }

    queue_system_notice(dispatch, speaker, "Usage: @guild lord <guild> <lord_name>");
    return true;
  }

  return false;
}

// ============================================================================
//  通用工具函数
// ============================================================================

/**
 * @brief 构建商人默认对话框文本
 * @details 如果 NPC 有 @main 脚本则使用脚本内容，否则自动生成服务列表
 * @param merchant 商人 NPC
 * @return 对话框文本
 */
std::string build_merchant_dialog_text(const Npc& merchant) {
  if (const auto* scripted = find_npc_dialog_text(merchant, "@main"); scripted != nullptr) {
    return *scripted;
  }
  const auto entries = build_merchant_dialog_entries(merchant);
  if (entries.empty()) {
    return "Nothing is available right now.\\<Leave/@exit>";
  }

  std::string text = "How can I help you?\\";
  for (const auto& entry : entries) {
    text += "<" + entry.label + "/" + entry.action + ">\\";
  }
  text += "<Leave/@exit>";
  return text;
}

/**
 * @brief 判断坐标点是否在指定的地图区域内
 * @param zone 地图区域配置
 * @param x X 坐标
 * @param y Y 坐标
 * @return true 如果点在区域内
 */
bool point_in_zone(const MapZoneConfig& zone, std::int32_t x, std::int32_t y) {
  return zone.width > 0 && zone.height > 0 && x >= zone.x && y >= zone.y &&
         x < zone.x + zone.width && y < zone.y + zone.height;
}

/**
 * @brief 判断指定坐标是否为安全区
 * @details 如果地图启用完整法律（law_full）或坐标在 badman_zones/safe_zones 区域内则视为安全区
 * @param map_config 地图配置
 * @param x X 坐标
 * @param y Y 坐标
 * @return true 如果是安全区
 */
bool is_safe_zone(const MapConfig& map_config, std::int32_t x, std::int32_t y) {
  if (map_config.law_full) {
    return true;
  }
  if (std::any_of(map_config.badman_zones.begin(), map_config.badman_zones.end(),
                  [&](const MapZoneConfig& zone) { return point_in_zone(zone, x, y); })) {
    return true;
  }
  return std::any_of(map_config.safe_zones.begin(), map_config.safe_zones.end(),
                     [&](const MapZoneConfig& zone) { return point_in_zone(zone, x, y); });
}

/**
 * @brief 计算坐标点的区域状态掩码
 * @details 根据地图配置中的 law_full、fight_zone、fight3_zone 标志计算区域状态
 * @param map_config 地图配置
 * @param x X 坐标
 * @param y Y 坐标
 * @return 区域状态掩码（可能包含 kAreaSafe、kAreaFight、kAreaFreePk）
 * @see kAreaSafe, kAreaFight, kAreaFreePk
 */
std::int32_t area_state_mask(const MapConfig& map_config, std::int32_t x, std::int32_t y) {
  static_cast<void>(x);
  static_cast<void>(y);
  std::int32_t mask = 0;
  if (map_config.fight_zone || map_config.fight3_zone) {
    mask |= kAreaFight;
  }
  if (map_config.fight3_zone) {
    mask |= kAreaFreePk;
  }
  if (map_config.law_full) {
    mask |= kAreaSafe;
  }
  return mask;
}

/**
 * @brief 解析 PK 阻止原因
 * @details 检查攻击者和目标之间的各种 PK 限制条件，
 *          返回阻止 PK 的原因。如果允许 PK 则返回空字符串。
 *          检查项包括：地图 PK 限制、安全区、自伤、新手保护、
 *          死亡状态、地图切换保护、攻击模式等
 * @param map_config 地图配置
 * @param attacker 攻击者
 * @param target 目标
 * @param now_ms 当前时间（毫秒），用于地图切换保护检查，0 表示检查
 * @return 阻止原因字符串，如果允许 PK 则为空字符串
 * @see kMapChangeProtectMs, kHamAll, kHamPeace, kHamPkAttack, kHamGuild
 */
std::string resolve_pk_block_reason(const MapConfig& map_config, const Player& attacker,
                                    const Player& target, std::uint64_t now_ms = 0) {
  const auto fight_map = map_config.fight_zone || map_config.fight3_zone;
  if (!map_config.allow_pk && !fight_map) {
    return "This map forbids PK.";
  }
  if (is_safe_zone(map_config, attacker.x(), attacker.y()) ||
      is_safe_zone(map_config, target.x(), target.y())) {
    return "Safe zone forbids combat.";
  }
  if (attacker.id() == target.id()) {
    return "Cannot attack self.";
  }
  if (!fight_map &&
      (attacker.character().ability.level < 10 || target.character().ability.level < 10)) {
    return "Newbie protection forbids PK.";
  }
  if (target.death_time_ms() != 0 || target.is_dead()) {
    return "Target is already dead.";
  }
  if (!fight_map && now_ms > 0 && target.legacy_run_time_ms() > 0 &&
      now_ms < static_cast<std::uint64_t>(target.legacy_run_time_ms()) + kMapChangeProtectMs) {
    return "Map change protection forbids PK.";
  }
  const auto mode = attacker.attack_mode();
  if (mode == kHamAll) {
    return {};
  }
  if (mode == kHamGroup) {
    if (attacker.legacy_group_id() != 0 &&
        attacker.legacy_group_id() == target.legacy_group_id()) {
      return "Group mode protects group members.";
    }
    return {};
  }
  if (mode == kHamPeace) {
    return "Peace mode forbids PK.";
  }
  if (mode == kHamPkAttack && target.pk_level() < 2 &&
      !target.has_recent_pk_hiter(attacker.id(), now_ms)) {
    return "Red-name mode can only attack PK targets.";
  }
  if (mode == kHamGuild) {
    if (!attacker.character().guild_name.empty() &&
        equals_ignore_case(attacker.character().guild_name, target.character().guild_name)) {
      return "Guild mode protects guild members.";
    }
  }
  return {};
}


}  // namespace
