# PR-1: Delphi Legacy UI Semantics Baseline

状态: PR-1 审查基线。本文只冻结语义、风险和 trace 标签, 不要求任何运行时 UI 行为改动。

## 1. 结论摘要

当前 C++ 客户端已经具备 `UiTree`/`UiNode` 树、按钮、精灵按钮、编辑框、列表、网格、窗口、Tooltip、拖拽覆盖层、HUD、背包、装备、技能、NPC、商店、仓库、交易、组队、行会、小地图等能力雏形。`ClientApp` 也已经用 `LegacyFrameScheduler` 将 network、key/action、`dwin_process`、scene render、UI paint、present 分阶段调度。

PR-1 的核心结论是: 这些能力不能直接等同于 Delphi-compatible。Delphi 原版的输入分发、modal/capture、窗口坐标、Hint 绘制、原生聊天输入、业务窗口关闭协议、副作用顺序和 WIL/WIX 资源语义仍需逐项对齐。后续 PR 必须先通过本文和 `tests/golden/legacy_ui_expected_trace.txt` 定义的 trace/验收条件, 再宣称兼容。

必须保留的 Delphi 时序为: `Timer1Timer` drain 网络包并 FIFO decode, 然后 `ProcessKeyMessages`, `ProcessActionMessages`, `DWinMan.Process`, `DrawScreen`, `DWinMan.DirectPaint`, `DrawScreenTop`, `DrawHint`, moving item 绘制, 最后 `Flip`。证据见 `Source/Client/ClMain.pas:1128`, `Source/Client/ClMain.pas:1160`, `Source/Client/ClMain.pas:1173`, `Source/Client/ClMain.pas:1174`, `Source/Client/ClMain.pas:1175`, `Source/Client/ClMain.pas:1176`, `Source/Client/ClMain.pas:1178`, `Source/Client/ClMain.pas:1255`, `Source/Client/ClMain.pas:3439`, `Source/Client/ClMain.pas:3463`。

本仓库未找到独立 `FrmDlg.pas`。Delphi 项目文件把 `FrmDlg` 表单绑定到 `FState.pas`, 见 `Source/Client/mir2.dpr:15`, `Source/Client/mir2.dpr:50`, `Source/Client/mir2.dproj:81`。当前对业务窗口和坐标的事实源是 `Source/Client/FState.pas`, 其中类名仍是 `TFrmDlg`。任何来自外部 Delphi 版本的独立 `FrmDlg.pas` 行为都标记为 `待源码核对`。

## 2. 三类边界清单

### A. 绝对不能改

| 分类 | 原因 | 如果改了会破坏什么 | C++ 推荐实现方式 | 验证方法 |
|---|---|---|---|---|
| A: Delphi 默认坐标 | `SCREENWIDTH=800`, `SCREENHEIGHT=600`, 大量窗口直接写死坐标 | 像素错位、误点击、玩家肌肉记忆失效 | 建立只读 legacy 坐标表, 默认 legacy 模式不响应式 | golden image 或布局 trace, 证据 `ClMain.pas:23`, `ClMain.pas:24`, `FState.pas:721`, `FState.pas:1286` |
| A: z-order 规则 | `BringToFront` 删除后追加到父控件列表尾部 | 覆盖关系和命中目标错误 | `UiTree::bring_to_front` 只复刻删除再追加, 不按现代窗口策略排序 | z-order smoke, 证据 `DWinCtl.pas:1547`, `DWinCtl.pas:1559` |
| A: Modal 行为 | `ShowModal` 设置全局 `ModalDWindow` | modal 下层窗口或场景仍响应 | modal 命中和输入必须短路下层 UI/scene | modal fuzz, 证据 `DWinCtl.pas:2343`, `DWinCtl.pas:2347`, `DWinCtl.pas:6383`, `DWinCtl.pas:6439` |
| A: 鼠标命中顺序 | 子控件 hit-test 使用 reverse order | 按钮点击落到父窗口或底层窗口 | child hit-test 从后向前, paint 从前向后 | hit-test smoke, 证据 `DWinCtl.pas:1712`, `DWinCtl.pas:1743`, `DWinCtl.pas:1777`, `DWinCtl.pas:1896` |
| A: 鼠标 capture | Delphi 调 Win32 `SetCapture(MainWinHandle)` | 按下后拖出窗口时丢失拖拽或误触发场景 | `MouseCaptureControl` 等价状态必须优先于普通命中 | capture smoke, 证据 `DWinCtl.pas:1070`, `DWinCtl.pas:1072`, `DWinCtl.pas:1076`, `DWinCtl.pas:1078` |
| A: 按钮 capture/release | `TDButton.MouseDown` capture, `MouseUp` release | 按钮 pressed 状态粘住或点击穿透 | Button down/up 必须围绕同一 captured 控件 | button smoke, 证据 `DWinCtl.pas:1953`, `DWinCtl.pas:1969`, `DWinCtl.pas:1975`, `DWinCtl.pas:1982` |
| A: 窗口拖拽规则 | `Floating` 且 capture/ParentNotify 才移动, 并使用旧 clamp 常量 | 窗口移动手感、可越界范围不同 | 默认照抄 `WINLEFT/WINRIGHT/WINTOP/BOTTOMEDGE`; 不额外 clamp 到可见区 | drag boundary smoke, 证据 `DWinCtl.pas:10`, `DWinCtl.pas:15`, `DWinCtl.pas:2286`, `DWinCtl.pas:2295` |
| A: UI 是否阻止场景点击 | Delphi mouse event 先进入 `DWinMan`, consumed 后直接 `exit` | 点 UI 空白/按钮时角色误移动、攻击、拾取 | `context.ui_input.consumed` 必须阻断 scene action | input trace, 证据 `ClMain.pas:2129`, `ClMain.pas:2238`, `ClMain.pas:2409` |
| A: 聊天焦点与快捷键 | `EdChat.Visible` 时 KeyPress 直接退出快捷键逻辑 | 聊天时误开背包、误放技能 | 聊天 edit visible/text focus 必须禁止快捷键 fallback | chat focus smoke, 证据 `ClMain.pas:1670`, `ClMain.pas:1672`, `ClMain.pas:1674` |
| A: Enter/ESC 聊天行为 | 原生 `TEdit` Enter 发送并隐藏, ESC 清空并隐藏 | 聊天提交/取消手感不一致 | `ResourceTextEdit` native 分支必须保留 max length 70 和 IME close | chat smoke, 证据 `PlayScn.pas:110`, `PlayScn.pas:113`, `PlayScn.pas:150`, `PlayScn.pas:168` |
| A: F1-F12 和字母快捷键 | F1-F8 magic, F9 bag, F10 status, KeyPress I/C/S/V/P/T/G 等 | 常用快捷键肌肉记忆失效 | 先 UI 焦点/聊天, 再 legacy shortcut fallback | shortcut smoke, 证据 `ClMain.pas:1563`, `ClMain.pas:1569`, `ClMain.pas:1577`, `ClMain.pas:1676` |
| A: 业务窗口开关规则 | `OpenItemBag` toggle, shop/trade 会移动背包 | 背包位置和窗口状态与服务端业务不同步 | 窗口 open/close 必须使用 legacy side-effect 函数 | protocol/UI smoke, 证据 `FState.pas:1654`, `FState.pas:1656`, `FState.pas:3970`, `FState.pas:4799` |
| A: 关闭交易触发取消 | 交易关闭按钮调用 `SendCancelDeal` | 服务端仍认为交易打开, 物品/金币锁死 | close handler 必须发送 `TradeCancelRequest` | trade smoke, 证据 `FState.pas:4845`, `FState.pas:4848`, `FState.pas:4849` |
| A: NPC 菜单刷新顺序 | 点击选项后等服务端返回刷新或关闭 | 本地提前刷新导致选项和奖励错位 | UI 只发 request, authoritative refresh 来自协议 apply | NPC smoke, 协议映射证据 `delphi_protocol_map.hpp:241`, `delphi_protocol_map.hpp:696`, `delphi_protocol_map.hpp:699` |
| A: 背包/装备/金币/属性/系统消息 FIFO | Delphi `DecodeMessagePacket` 顺序处理 buffer | 同一 frame 多消息显示顺序不同 | `client_v1` drain 必须 FIFO apply, 不按类型重排 | protocol trace, 证据 `ClMain.pas:3456`, `ClMain.pas:3458`, `ClMain.pas:3461`, `ClMain.pas:3463` |
| A: Tooltip/Hint 层 | `DrawHint` 在 `DrawScreenTop` 后, moving item 前 | Tooltip 被窗口盖住或过早清掉 | Hint 层独立于普通 UI window paint | draw trace, 证据 `ClMain.pas:1174`, `ClMain.pas:1175`, `ClMain.pas:1176`, `DrawScrn.pas:428` |
| A: 鼠标/拖动物品层 | moving item 在 Hint 后绘制 | 背包拖拽物品被 Hint 或窗口遮挡 | moving-item cursor 层必须晚于 Hint | draw trace, 证据 `ClMain.pas:1176`, `ClMain.pas:1178` |
| A: UI 在场景之后 | `DrawScreen` 后才 `DWinMan.DirectPaint` | 场景特效覆盖 UI 或 UI 混入地图层 | render scene 和 UI paint 分阶段, 禁止 batching 重排 | draw call trace, 证据 `ClMain.pas:1173`, `ClMain.pas:1174` |
| A: WIL/WIX 图片索引 | 坐标/资源索引直接决定外观 | 按钮/窗口图错、透明区域错 | 使用 `ArchiveId` + 固定 index, 不按名称猜 | asset smoke, 证据 `FState.pas:718`, `FState.pas:1285`, `FState.pas:1384`, `FState.pas:1504` |
| A: 透明色、字体颜色、中文编码 | 旧端 UI 文本宽度/颜色/编码影响布局 | 中文乱码、文本溢出、hint 宽度不同 | PR-4/PR-10 专项核对字体和 WIL alpha; PR-1 标风险 | golden image, `待源码核对` |

### B. 应保留 legacy 行为但可以现代封装

| 分类 | 原因 | 如果改了会破坏什么 | C++ 推荐实现方式 | 验证方法 |
|---|---|---|---|---|
| B: `DWinMan` 等价管理器 | Delphi 所有 UI 输入/paint 都经 `DWinMan` | scene 和 UI 输入边界模糊 | `LegacyUiManager` 封装现有 `UiTree`, 对外暴露 legacy 阶段 API | PR-2 manager smoke |
| B: `DWindow`/`DButton`/`DEdit`/`DListBox` 模型 | Delphi 控件类有固定状态和回调 | 控件行为和事件顺序漂移 | 以 C++ 类型安全类复刻外部语义 | unit smoke, 证据 `DWinCtl.pas:1001` |
| B: Window/Control 树 | 父子树决定坐标、命中、绘制 | 子控件 z-order 错 | 继续使用 `UiNode` ownership, 但行为 trace 对齐 | tree smoke |
| B: visible/enabled/focus/capture | 全局引用在 destroy 时清理 | 悬空焦点/capture 崩溃或吞键 | RAII 清理可现代化, 但清理时机必须一致 | lifecycle fuzz, 证据 `DWinCtl.pas:1183`, `DWinCtl.pas:1187` |
| B: ModalStack/ModalDWindow | 旧端全局 modal 指针影响输入和 paint | 下层窗口可点或 modal 不在上层 | PR-2 先实现单 active modal; 是否 stack `待源码核对` | modal trace |
| B: Tooltip/Hint 系统 | `ShowHint` 会 split `\`, clamp, blend 背景 | tooltip 内容、位置、层级不一致 | `LegacyTooltip` 可现代封装, 输入数据用 legacy 行文本 | tooltip golden, 证据 `DrawScrn.pas:200`, `DrawScrn.pas:216`, `DrawScrn.pas:224`, `DrawScrn.pas:445` |
| B: UI 资源缓存 | WIL 资源可缓存但索引不能变 | 加载时序或帧内容改变 | `UiResourceCache` 持有 handles, miss 只记日志 | asset smoke |
| B: 字体渲染封装 | Delphi `TextOut`/`BoldTextOut` 影响宽度 | 换行、裁剪、对齐不一致 | `LegacyFontRenderer` 保留 metrics trace | text golden |
| B: 输入分发器 | Delphi active menu/modal/capture/list 顺序固定 | 快捷键、拖拽、点击穿透错误 | `LegacyInputDispatcher` 按 golden labels emit trace | input trace |
| B: UI draw call trace | 后续 PR 必须验层级 | 无法证明 batching 不重排 | 每个 legacy paint stage 记录标签 | trace diff |
| B: 场景切换 UI 清理 | Delphi 切场景清理 UI/鼠标/动作状态 | 断线后旧窗口残留 | `Scene::exit` 和 disconnect cleanup 明确清理 | scene smoke |
| B: 协议消息适配层 | ModernClient 使用 typed `client_v1` | raw 协议回归或 UI 时序漂移 | 只做 semantic mapping, 不重引 raw framing | protocol map smoke |
| B: `GameState` 状态同步 | UI 应读取 authoritative state | 本地乐观刷新覆盖服务端 | local pending 只用于展示, 最终由 apply 刷新 | protocol smoke |
| B: legacy 窗口坐标表 | FState 中坐标分散 | 后续 PR 难验收 | PR-2 引入坐标注册表前先以本文为事实源 | golden layout |
| B: golden trace 测试 | 兼容性要可验收 | 只靠人工截图不可持续 | 保留 `legacy_ui_expected_trace.txt` 并逐 PR 接 runtime emit | CI smoke |

### C. 可以现代化但必须证明不变

| 分类 | 原因 | 如果改了会破坏什么 | C++ 推荐实现方式 | 验证方法 |
|---|---|---|---|---|
| C: C++ ownership/RAII | 避免手工 delete 和悬空引用 | 如果清理早/晚, focus/capture 时序变 | `unique_ptr`/RAII, 但 emit legacy lifecycle trace | lifecycle fuzz |
| C: 智能指针管理窗口 | C++ 安全性更高 | 对象地址/引用无效导致回调错 | 窗口由 manager owning, 控件 raw observer 只在树内 | ASAN 或 fuzz |
| C: D3D11 后端替代 DirectDraw/DX7 | 当前项目已用 D3D11 present | batching 或采样改变像素 | 软件 surface 上先按 legacy 顺序画完, D3D11 只 present | draw trace + image diff |
| C: 纹理缓存 | 性能优化 | cache miss 时显示顺序或尺寸变化 | 同步可见资源; 异步只预热不改变首帧 | cache trace |
| C: 字体 atlas | 可减少 GDI 成本 | metrics 改变导致布局错 | atlas 必须复用 legacy metrics, 默认关闭直到 golden 通过 | text diff |
| C: draw call batching | 可减少提交 | Tooltip/Hint/mouse 被重排 | batch key 不得跨 legacy layer; layer barrier 强制 flush | layer trace |
| C: 输入事件队列 | C++ 结构化输入 | Delphi 即时回调时序被延迟 | capture/process 两阶段必须有 trace 证明等价 | input trace |
| C: 内部事件总线 | 解耦 UI/业务 | 同 frame 事件顺序重排 | 总线 FIFO, 禁止按 topic regroup | protocol/UI trace |
| C: 自动化测试 | 更可维护 | 若只测现代行为会误判 | smoke 名称和 expected trace 使用 legacy 标签 | CI |
| C: golden image diff | 直观验收 | 资源差异导致误报 | 分 layout diff 和 pixel diff 两级 | PR-11 |
| C: fuzz 测试 | 暴露异常路径 | 随机输入改变真实节奏 | fuzz 只验证不崩溃和不破坏 invariants | fuzz CI |
| C: UI inspector/log overlay | 调试方便 | overlay 影响截图或输入 | legacy 模式默认关闭, trace 模式不可绘制到 surface | manual + trace |

## 3. Delphi 原版 UI 系统审查结果

| 行为 | Delphi 证据 | PR-1 结论 |
|---|---|---|
| 固定 800x600 | `Source/Client/ClMain.pas:23`, `Source/Client/ClMain.pas:24`, `Source/Client/DWinCtl.pas:12`, `Source/Client/DWinCtl.pas:13` | legacy 模式默认 pixel-perfect 800x600 |
| 网络 decode 在 frame 早期 | `Source/Client/ClMain.pas:3439`, `Source/Client/ClMain.pas:3456`, `Source/Client/ClMain.pas:3461`, `Source/Client/ClMain.pas:3463` | UI 相关协议必须先于本帧绘制 apply |
| 主循环绘制顺序 | `Source/Client/ClMain.pas:1128`, `Source/Client/ClMain.pas:1160`, `Source/Client/ClMain.pas:1162`, `Source/Client/ClMain.pas:1173`, `Source/Client/ClMain.pas:1174`, `Source/Client/ClMain.pas:1175`, `Source/Client/ClMain.pas:1176`, `Source/Client/ClMain.pas:1178`, `Source/Client/ClMain.pas:1255` | `DrawHint` 不是普通窗口层, moving item 更晚 |
| 初始化链路 | `Source/Client/ClMain.pas:1028`, `Source/Client/ClMain.pas:1038`, `Source/Client/ClMain.pas:1041`, `Source/Client/ClMain.pas:1042`, `Source/Client/ClMain.pas:1043` | DXDraw 初始化 800x600 surface 后初始化 `DScreen`, `PlayScene`, `FrmDlg` |
| KeyDown 先 UI 后场景 | `Source/Client/ClMain.pas:1519`, `Source/Client/ClMain.pas:1563`, `Source/Client/ClMain.pas:1565` | UI consumed 后场景不处理 |
| KeyPress 聊天阻断快捷键 | `Source/Client/ClMain.pas:1667`, `Source/Client/ClMain.pas:1670`, `Source/Client/ClMain.pas:1672`, `Source/Client/ClMain.pas:1674` | chat visible/text focus 先于 I/C/S/V/P/T/G |
| MouseMove 先 UI | `Source/Client/ClMain.pas:2122`, `Source/Client/ClMain.pas:2129`, `Source/Client/ClMain.pas:2130` | hover/tooltip 与 scene focus 不能反序 |
| MouseDown 特殊右键取消 moving item | `Source/Client/ClMain.pas:2223`, `Source/Client/ClMain.pas:2234`, `Source/Client/ClMain.pas:2235`, `Source/Client/ClMain.pas:2238` | 背包拖动物品时右键优先取消, 然后才考虑 UI/scene |
| MouseUp 先 UI | `Source/Client/ClMain.pas:2406`, `Source/Client/ClMain.pas:2409`, `Source/Client/ClMain.pas:2410` | UI consumed 后不改 scene target |
| 全局 UI 状态 | `Source/Client/DWinCtl.pas:984`, `Source/Client/DWinCtl.pas:985`, `Source/Client/DWinCtl.pas:987`, `Source/Client/DWinCtl.pas:990` | PR-2 必须有等价 active menu/focus/capture/modal 状态 |
| destroy 清理引用 | `Source/Client/DWinCtl.pas:1183`, `Source/Client/DWinCtl.pas:1184`, `Source/Client/DWinCtl.pas:1185`, `Source/Client/DWinCtl.pas:1187` | 窗口关闭/销毁不得留下 dangling tooltip/capture |
| Manager Key 顺序 | `Source/Client/DWinCtl.pas:6157`, `Source/Client/DWinCtl.pas:6172`, `Source/Client/DWinCtl.pas:6182`, `Source/Client/DWinCtl.pas:6193`, `Source/Client/DWinCtl.pas:6201`, `Source/Client/DWinCtl.pas:6224`, `Source/Client/DWinCtl.pas:6232`, `Source/Client/DWinCtl.pas:6239` | ActiveMenu -> Modal -> Focused -> DWinList/待源码核对 |
| Manager Mouse 顺序 | `Source/Client/DWinCtl.pas:6345`, `Source/Client/DWinCtl.pas:6363`, `Source/Client/DWinCtl.pas:6379`, `Source/Client/DWinCtl.pas:6387`, `Source/Client/DWinCtl.pas:6391`, `Source/Client/DWinCtl.pas:6401`, `Source/Client/DWinCtl.pas:6419`, `Source/Client/DWinCtl.pas:6433`, `Source/Client/DWinCtl.pas:6443`, `Source/Client/DWinCtl.pas:6447`, `Source/Client/DWinCtl.pas:6457`, `Source/Client/DWinCtl.pas:6475`, `Source/Client/DWinCtl.pas:6489`, `Source/Client/DWinCtl.pas:6496` | ActiveMenu -> Modal -> Capture -> DWinList |
| Control 子命中顺序 | `Source/Client/DWinCtl.pas:1712`, `Source/Client/DWinCtl.pas:1743`, `Source/Client/DWinCtl.pas:1777` | 子控件从后往前 hit-test |
| Control 绘制顺序 | `Source/Client/DWinCtl.pas:1883`, `Source/Client/DWinCtl.pas:1896`, `Source/Client/DWinCtl.pas:1898` | 子控件从前往后 paint |
| `DWinMan.DirectPaint` 顺序 | `Source/Client/DWinCtl.pas:6639`, `Source/Client/DWinCtl.pas:6643`, `Source/Client/DWinCtl.pas:6648`, `Source/Client/DWinCtl.pas:6653` | DWinList, Modal, ActiveMenu |
| Hint split/layout | `Source/Client/DrawScrn.pas:200`, `Source/Client/DrawScrn.pas:206`, `Source/Client/DrawScrn.pas:216`, `Source/Client/DrawScrn.pas:224`, `Source/Client/DrawScrn.pas:225` | `\` 分行, drawup 会向上偏移 |
| Hint 绘制 | `Source/Client/DrawScrn.pas:428`, `Source/Client/DrawScrn.pas:435`, `Source/Client/DrawScrn.pas:439`, `Source/Client/DrawScrn.pas:441`, `Source/Client/DrawScrn.pas:445`, `Source/Client/DrawScrn.pas:452` | 背景 `WProgUse.Images[394]`, 屏幕 clamp, 文本后绘 |
| 聊天输入 | `Source/Client/PlayScn.pas:37`, `Source/Client/PlayScn.pas:107`, `Source/Client/PlayScn.pas:110`, `Source/Client/PlayScn.pas:111`, `Source/Client/PlayScn.pas:113`, `Source/Client/PlayScn.pas:115`, `Source/Client/PlayScn.pas:118`, `Source/Client/PlayScn.pas:150`, `Source/Client/PlayScn.pas:159`, `Source/Client/PlayScn.pas:161`, `Source/Client/PlayScn.pas:167` | 原生 `TEdit`, 208, SCREENHEIGHT-19, 387x12, maxlength 70 |
| 聊天打开/私聊辅助 | `Source/Client/ClMain.pas:1742`, `Source/Client/ClMain.pas:1755`, `Source/Client/ClMain.pas:2911`, `Source/Client/ClMain.pas:2932`, `Source/Client/FState.pas:1773`, `Source/Client/FState.pas:1780`, `Source/Client/FState.pas:3136` | 空格/回车或 `@`/`!`/`/` 可打开输入; 点击聊天板可抽取用户名填 `/name ` |
| `TFrmDlg` 容器初始化 | `Source/Client/FState.pas:51`, `Source/Client/FState.pas:574`, `Source/Client/FState.pas:581`, `Source/Client/FState.pas:682`, `Source/Client/FState.pas:687`, `Source/Client/FState.pas:695` | `Initialize` 清空并重建 `DWinMan`, 先加入全屏背景控件; DFM 资源未展开 |

`DListBox` 这个 Delphi 类名在当前 `Source/Client/DWinCtl.pas` 未确认到独立实现, 但存在 `TDMemo`, `TDScroll`, `TDPageControl` 等控件。是否另一个 Delphi 分支存在 `DListBox`: `待源码核对`。

## 4. 当前 C++ 项目差距分析

| 领域 | 当前事实 | Legacy 兼容风险 |
|---|---|---|
| UI | `UiNode` 树、`Button`, `SpriteButton`, `TextEdit`, `ListBox`, `Grid`, `ScrollBar`, `Window`, `Tooltip`, `DragSpriteOverlay` 已存在, 见 `ModernClient/src/ui/ui.hpp:92`, `ModernClient/src/ui/ui.hpp:196`, `ModernClient/src/ui/ui.hpp:224`, `ModernClient/src/ui/ui.hpp:245`, `ModernClient/src/ui/ui.hpp:268`, `ModernClient/src/ui/ui.hpp:304`, `ModernClient/src/ui/ui.hpp:310`, `ModernClient/src/ui/ui.hpp:340`, `ModernClient/src/ui/ui.hpp:359` | 这些是现代类, 不能默认等价 Delphi 控件 |
| UI 输入 | `capture_input` 先计算 consumed/hover/focus 并排队, `process_queued_events` 后执行回调, 见 `ModernClient/src/ui/ui.cpp:870`, `ModernClient/src/ui/ui.cpp:881`, `ModernClient/src/ui/ui.cpp:931`, `ModernClient/src/ui/ui.cpp:939`, `ModernClient/src/ui/ui.cpp:965`, `ModernClient/src/ui/ui.cpp:990` | Delphi Win32 event 中先调用 `DWinMan.*`, 当前队列可能改变即时副作用时机 |
| UI 绘制 | `UiNode::paint` 遍历可见 children, `UiTree::paint` 设置 asset manager 后递归, 见 `ModernClient/src/ui/ui.cpp:99`, `ModernClient/src/ui/ui.cpp:100`, `ModernClient/src/ui/ui.cpp:996` | 需要 PR-4 明确 DirectPaint/DrawHint/moving item barrier |
| 场景 | scene 实现在 `ModernClient/src/scene/scenes.cpp`, 接口顺序写在 `ModernClient/src/scene/scenes.hpp:67`, `ModernClient/src/scene/scenes.hpp:71`, `ModernClient/src/scene/scenes.hpp:73` | 现有注释对齐方向正确, 但仍需 trace 验证输入和 UI 副作用 |
| 主循环 | `ClientApp::run` 在 scheduler hook 中 poll protocol、handle events、process key/action、dwin、scene、render、paint UI、modal、present, 见 `ModernClient/src/app/client_app.cpp:323`, `ModernClient/src/app/client_app.cpp:327`, `ModernClient/src/app/client_app.cpp:333`, `ModernClient/src/app/client_app.cpp:335`, `ModernClient/src/app/client_app.cpp:339`, `ModernClient/src/app/client_app.cpp:349`, `ModernClient/src/app/client_app.cpp:352`, `ModernClient/src/app/client_app.cpp:356` | `render_modal` 位置、empty DrawHint hooks、native cursor/moving item 需 PR-4 固化 |
| App modal | app-level modal 会在 `ClientApp::dwin_process` 中截断 scene `dwin_process`, 见 `ModernClient/src/app/client_app.cpp:399`, `ModernClient/src/app/client_app.cpp:400`, `ModernClient/src/app/client_app.cpp:404` | 不完全等于 Delphi `ModalDWindow`, 必须区分 app modal 与 DWindow modal |
| GameState | `GameStateStore` 是服务端 -> apply -> UI/scene 读取, 见 `ModernClient/src/game/game_state.hpp:1`, `ModernClient/src/game/game_state.hpp:11`, `ModernClient/src/game/game_state.hpp:341`, `ModernClient/src/game/game_state.hpp:359`, `ModernClient/src/game/game_state.hpp:370`, `ModernClient/src/game/game_state.hpp:372` | UI 可读 state, 但业务确认顺序必须 FIFO; local pending 不能覆盖 authoritative |
| HUD/窗口 | `LegacyHud` 内联创建背包、聊天、NPC、商店、仓库、组队、交易、行会、小地图, 见 `ModernClient/src/scene/scenes.cpp:1748`, `ModernClient/src/scene/scenes.cpp:1761`, `ModernClient/src/scene/scenes.cpp:2018`, `ModernClient/src/scene/scenes.cpp:2036`, `ModernClient/src/scene/scenes.cpp:2054`, `ModernClient/src/scene/scenes.cpp:2108`, `ModernClient/src/scene/scenes.cpp:2180`, `ModernClient/src/scene/scenes.cpp:2221`, `ModernClient/src/scene/scenes.cpp:2246` | group/trade/guild 等仍有文本按钮/近似布局, 不可宣称 Delphi 外观兼容 |
| 交易关闭 | `close_trade_window` 会发 `TradeCancelRequest`, 清状态并 hide, 见 `ModernClient/src/scene/scenes.cpp:3360`, `ModernClient/src/scene/scenes.cpp:3362`, `ModernClient/src/scene/scenes.cpp:3365`, `ModernClient/src/scene/scenes.cpp:3368` | 方向正确, 仍需与 Delphi dealactiontime、窗口双面板行为对齐 |
| 协议 | `delphi_protocol_map.hpp` 明确 ModernClient 是 structured `client_v1` mapping, 见 `ModernClient/src/protocol/delphi_protocol_map.hpp:1`, `ModernClient/src/protocol/delphi_protocol_map.hpp:7`, `ModernClient/src/protocol/delphi_protocol_map.hpp:9`, `ModernClient/src/protocol/delphi_protocol_map.hpp:13` | 不应重引旧文本 framing; 只做语义映射 |
| UI 资源 | `ArchiveId` 包含 `prguse`, `prguse2`, `items`, `state_item`, `mag_icon`, `mmap`, 见 `ModernClient/src/assets/asset_manager.hpp:37`, `ModernClient/src/assets/asset_manager.hpp:44`, `ModernClient/src/assets/asset_manager.hpp:50`, `ModernClient/src/assets/asset_manager.hpp:53`, `ModernClient/src/assets/asset_manager.hpp:55`, `ModernClient/src/assets/asset_manager.hpp:56` | 图片索引必须固定; 透明/alpha 与 Delphi 仍需核对 |
| 渲染 | 软件 surface + D3D11 present; sampler point+clamp, GDI font 当前为 MS Sans Serif, 见 `ModernClient/src/render/software_renderer.hpp:98`, `ModernClient/src/render/software_renderer.hpp:184`, `ModernClient/src/render/software_renderer.hpp:190`, `ModernClient/src/render/software_renderer.cpp:285`, `ModernClient/src/render/software_renderer.cpp:586`, `ModernClient/src/render/software_renderer.cpp:589` | point sampling 是好方向, 但字体名/字号/中文 charset 和 viewport scaling 仍有风险 |

当前 C++ 风险结论:

| 风险 | PR-1 结论 |
|---|---|
| input queue 可能改变 Delphi 即时输入顺序 | PR-3 前不得视为兼容, 必须有 `ui.input.*` trace |
| D3D11 batching 可能改变 Tooltip/Hint/鼠标层级 | PR-4 必须以 layer barrier 证明不重排 |
| `TextEdit` 与 Delphi 原生 `TEdit` 行为不一致 | 聊天输入必须专项对齐 max length、IME、Enter/ESC、焦点 |
| 部分窗口是现代 sprite 占位或近似实现 | group/trade/guild 等不得视为 legacy-compatible |
| `client_v1` 一次 drain 多条消息可能改变 UI 中间态可见性 | 必须 FIFO apply, 禁止按消息类型重排 |
| app modal 与 Delphi DWindow modal 不同 | PR-2/PR-3 要区分 modal source, 避免吞掉 legacy window 输入 |

## 5. UI golden trace 规范

PR-1 新增 `ModernClient/tests/golden/legacy_ui_expected_trace.txt`, 只定义后续 PR 使用的标签, 不要求 PR-1 runtime emit。该文件必须持续包含:

| Section | 关键顺序 |
|---|---|
| `[ui.frame.legacy_order]` | `network_drain -> decode_packets_fifo -> process_key_messages -> process_action_messages -> dwin_process -> draw_screen -> dwin_direct_paint -> draw_screen_top -> draw_hint -> draw_moving_item -> present` |
| `[ui.input.mouse_down]` | stale cleanup -> active menu -> modal -> capture -> top window hit-test -> scene block |
| `[ui.input.mouse_move]` | stale cleanup -> active menu -> modal -> capture -> hover -> tooltip target -> block rules |
| `[ui.input.mouse_up]` | stale cleanup -> active menu -> modal -> capture -> release/drop/click -> close cleanup |
| `[ui.input.keyboard]` | active menu -> modal -> focused edit/chat -> Enter/ESC -> shortcut fallback |
| `[ui.paint.layers]` | map -> objects -> actors -> effects -> UI windows -> top messages -> hint -> moving item -> mouse -> present |
| `[ui.window.lifecycle]` | show -> bring front -> hide -> close side effect -> destroy reference cleanup -> scene/disconnect cleanup |
| `[ui.business.trade]` | open -> show both windows -> close -> send cancel -> server refresh -> clear |
| `[ui.business.npc]` | click NPC -> request -> open dialog -> select option -> request -> refresh/close |
| `[ui.business.inventory]` | click cell -> local operation -> request -> FIFO server messages -> bag/equip -> gold/attr -> system message |

## 6. 窗口坐标与行为登记表

默认字段说明: “是否可拖动/是否允许越界/是否记忆位置”如果 Delphi 源码未在本 PR 证据中确认, 必须写 `待源码核对`, 不得猜。

| 窗口 | Delphi 类/文件 | 资源/坐标证据 | 打开/关闭与输入规则 | Modal/拖拽/越界/记忆 | C++ 现状 | 待核对点 |
|---|---|---|---|---|---|---|
| 登录 | `TFrmDlg` / `FState.pas` | `WProgUse[60]` 居中, buttons `[61,62,53,64]`, 证据 `FState.pas:718`, `FState.pas:721`, `FState.pas:724`, `FState.pas:727`, `FState.pas:730`, `FState.pas:733` | 登录按钮发送登录; 成功后进入选服/选角链路 | Modal 待源码核对; 拖拽待源码核对; 位置不记忆待源码核对 | 登录 scene 已存在, 外观需对齐 | 输入框具体控件和错误提示顺序 |
| 服务器选择 | `TFrmDlg` / `FState.pas` | `WProgUse[256]` 或 `WProgUse2[4/5]` 居中, close `[64]`, 证据 `FState.pas:812`, `FState.pas:815`, `FState.pas:859`, `FState.pas:862`, `FState.pas:939`, `FState.pas:942` | 选择服务器后连接角色网关 | Modal/拖拽/记忆待源码核对 | server_select scene 已存在 | 不同服务器页资源选择规则 |
| 角色选择 | `TFrmDlg` / `FState.pas` | full screen 0,0, buttons `[66..72]`, 证据 `FState.pas:1088`, `FState.pas:1090`, `FState.pas:1092`, `FState.pas:1112` | start/create/delete/exit | 非普通窗口; 拖拽否; 越界否 | character_select scene 已存在 | 角色预览绘制层 |
| 创建角色 | `TFrmDlg` / `FState.pas` | `WProgUse[73]` 居中, class/sex/hair/ok/close buttons, 证据 `FState.pas:1118`, `FState.pas:1121`, `FState.pas:1124`, `FState.pas:1133` | 创建成功/失败由服务端返回刷新 | Modal/拖拽待源码核对 | create character flow 已有 | 输入框保存/清空规则 |
| 主 HUD | `TFrmDlg` / `FState.pas` | `BOTTOMBOARD`, `DBottom.Top=SCREENHEIGHT-d.Height`, buttons at 643/61, 682/41, 722/21, 764/11, 219/104 等, 证据 `FState.pas:1296`, `FState.pas:1299`, `FState.pas:1307`, `FState.pas:1318`, `FState.pas:1323`, `FState.pas:1344` | 热键和底部按钮开关窗口 | 固定底部, 不拖拽, 不记忆 | `LegacyHud` 已实现 | HP/MP/EXP 条具体资源和闪烁 |
| 聊天 | `TPlayScene.EdChat` | `Left=208`, `Top=SCREENHEIGHT-19`, `Width=387`, `Height=12`, `MaxLength=70`, 证据 `PlayScn.pas:113`, `PlayScn.pas:115`, `PlayScn.pas:118` | Enter send+hide, ESC clear+hide | 原生 edit, 非 modal | `ResourceTextEdit` attaches native edit, 见 `scenes.cpp:2024`, `scenes.cpp:2031` | IME mode/字体/剪贴板行为 |
| 背包 | `DItemBag` / `FState.pas` | `WProgUse[3]`, `Left=0`, `Top=0`, grid 20,13,286,162, close `[371]`, 证据 `FState.pas:1285`, `FState.pas:1286`, `FState.pas:1288`, `FState.pas:1375` | `OpenItemBag` toggle; shop moves to 475,0 | 拖拽/越界待源码核对; 不记忆待源码核对 | `LegacyHud` item bag at 0,0 with grid, 见 `scenes.cpp:1761`, `scenes.cpp:1764` | 拆分数量、丢弃确认、右键规则 |
| 装备/状态 | `DStateWin` / `FState.pas` | `WProgUse[370]`, `Left=SCREENWIDTH-d.Width`, `Top=0`, slot 坐标, 证据 `FState.pas:1160`, `FState.pas:1163`, `FState.pas:1166`, `FState.pas:1229` | F10/C 打开; page 切换 | 拖拽/越界/记忆待源码核对 | equipment hit areas 已部分实现, 见 `scenes.cpp:1875` | 完整槽位、人物预览、属性页 |
| 技能 | 状态窗口 page 3 / `FState.pas` | skill page image `[383]`, magic row text/icon, 证据 `FState.pas:2519`, `FState.pas:2541`, `FState.pas:2559` | F11/S 打开状态技能页; 快捷键绑定 | 待源码核对 | magic page 已部分实现, 见 `scenes.cpp:1355`, `scenes.cpp:1889` | F1-F8 绑定弹窗和 Tooltip |
| NPC 对话 | `DMerchantDlg` / `DMenuDlg` | merchant `[384]` at 0,0, menu `[385]` init 138,163, show shop 0,176, 证据 `FState.pas:1384`, `FState.pas:1386`, `FState.pas:1397`, `FState.pas:1399`, `FState.pas:3959`, `FState.pas:3965` | 点击选项发送协议, 服务端刷新 | Modal 待源码核对; 关闭清点位 | `NpcDialogNode` at 0,0, 见 `scenes.cpp:2036` | 文字分页和 click rect |
| 商店 | `DMenuDlg` / `DSellDlg` | buy menu `[385]`, sell `[392]`, buttons `[388,387,386,393]`, bag 475,0, 证据 `FState.pas:1397`, `FState.pas:1411`, `FState.pas:1419`, `FState.pas:1427`, `FState.pas:3970`, `FState.pas:3985` | 买卖/修理/数量输入由协议确认 | 待源码核对 | merchant/storage/repair 部分实现 | 数量输入和金币刷新顺序 |
| 仓库 | 商人/仓库菜单 | `StorageList` 映射存在, Delphi 具体仓库窗口坐标待源码核对 | 存取需服务端确认 | 待源码核对 | storage window 使用 merchant buy panel 近似, 见 `scenes.cpp:2108` | 完整仓库窗口资源 |
| 交易 | `DDealDlg`/`DDealRemoteDlg` | local `[389]`, remote `[390]`, init right/top; `OpenDealDlg` sets both left `SCREENWIDTH-236-100`, local top remote height-15, 证据 `FState.pas:1504`, `FState.pas:1524`, `FState.pas:4795`, `FState.pas:4798` | close sends cancel; accept handles moving item | 拖拽/越界待源码核对 | C++ close sends `TradeCancelRequest`, 见 `scenes.cpp:3360`, `scenes.cpp:3362` | dealactiontime、双栏坐标、金币输入 |
| 组队 | `DGroupDlg` | `WProgUse[120]` 居中, buttons `[64,122..125]`, 证据 `FState.pas:1480`, `FState.pas:1482`, `FState.pas:1486`, `FState.pas:1500` | allow/create/add/del | 待源码核对 | C++ group window is modern text style, 见 `scenes.cpp:2180` | HP 显示、队长标识 |
| 行会 | `DGuildDlg` | `WProgUse[180]`, 0,0, close 584,6, buttons `[198,200,190,182,192,196,194,184,186,202,188]`, 证据 `FState.pas:1540`, `FState.pas:1542`, `FState.pas:1546`, `FState.pas:1581` | home/list/chat/member/notice/rank/ally/war | edit notice modal hides/restores controls, 证据 `FState.pas:5318`, `FState.pas:5319`, `FState.pas:5336` | C++ guild window modern text style, 见 `scenes.cpp:2221` | 公告编辑、多页列表、权限 |
| 小地图 | bottom button / mini map data | bottom minimap button `[130]` at 219,104, 证据 `FState.pas:1323`, `FState.pas:1325` | V or button opens/request minimap | 待源码核对 | C++ minimap 620,24 panel, 见 `scenes.cpp:2246`, request at `scenes.cpp:3451` | Delphi 小地图窗口资源和坐标 |
| 设置/系统菜单 | bottom buttons/config | option button `[11]` at 764,11, exit/logout `[138/136]`, 证据 `FState.pas:1316`, `FState.pas:1342` | HOME config, Alt shortcuts `待源码核对` | 待源码核对 | app modal/options not fully Delphi | 完整系统菜单 |

## 7. PR-2 到 PR-4 移交约束

| 后续 PR | PR-1 移交物 |
|---|---|
| PR-2 | `LegacyUiManager`、基础控件、z-order、visible/enabled/focus/capture/modal 的行为约束; 必须复用本文 A/B/C 边界和 `ui.window.lifecycle` trace |
| PR-3 | 输入分发顺序、点击穿透规则、ESC/Enter/快捷键规则; 必须实现 `ui.input.mouse_*` 和 `ui.input.keyboard` runtime trace |
| PR-4 | 绘制层级、Hint/Tooltip/mouse 层、D3D11 batching 禁止重排规则; 必须实现 `ui.paint.layers` runtime trace |

## 8. 协议/UI 顺序基线

ModernClient 保持 typed `client_v1` 语义映射, 不恢复旧文本包 framing。协议映射事实源是 `ModernClient/src/protocol/delphi_protocol_map.hpp`。

| 链路 | Delphi/C++ 映射证据 | 必须保持 |
|---|---|---|
| 背包物品使用/装备/丢弃 | `CM_DROPITEM`, `CM_TAKEONITEM`, `CM_TAKEOFFITEM`, `CM_EAT`, 见 `delphi_protocol_map.hpp:211`, `delphi_protocol_map.hpp:220`, `delphi_protocol_map.hpp:223`, `delphi_protocol_map.hpp:229`; bag/equipment refresh 见 `delphi_protocol_map.hpp:1188`, `delphi_protocol_map.hpp:1190`, `delphi_protocol_map.hpp:1198` | UI 命中 -> request -> 服务端 FIFO refresh bag/equip/gold/attr/message, 不本地最终裁决 |
| NPC 对话 | `CM_CLICKNPC`, `CM_MERCHANTDLGSELECT`, `SM_MERCHANTSAY`, `SM_MERCHANTDLGCLOSE`, 见 `delphi_protocol_map.hpp:241`, `delphi_protocol_map.hpp:244`, `delphi_protocol_map.hpp:696`, `delphi_protocol_map.hpp:699` | 场景 NPC 点击只有在 UI 未 consumed 时发送; 菜单点击等服务端返回刷新 |
| 商店/仓库 | `CM_USERBUYITEM`, `CM_USERSELLITEM`, `CM_USERSTORAGEITEM`, `SM_SENDGOODSLIST`, `SM_BUYITEM_SUCCESS`, `SM_SENDUSERSTORAGEITEM`, 见 `delphi_protocol_map.hpp:250`, `delphi_protocol_map.hpp:253`, `delphi_protocol_map.hpp:303`, `delphi_protocol_map.hpp:702`, `delphi_protocol_map.hpp:717`, `delphi_protocol_map.hpp:821` | 价格/买卖/存取结果驱动 UI, 金币和背包刷新不可重排 |
| 交易 | `CM_DEALTRY` 到 `CM_DEALEND`, `SM_DEALMENU` 到 `SM_DEALSUCCESS`, 见 `delphi_protocol_map.hpp:285`, `delphi_protocol_map.hpp:294`, `delphi_protocol_map.hpp:300`, `delphi_protocol_map.hpp:782`, `delphi_protocol_map.hpp:800`, `delphi_protocol_map.hpp:818` | 关闭窗口必须发 cancel; success/cancel 后 authoritative clear |
| 组队/行会 | group/guild request 和 state 映射见 `delphi_protocol_map.hpp:267`, `delphi_protocol_map.hpp:315`, `delphi_protocol_map.hpp:767`, `delphi_protocol_map.hpp:882`, `delphi_protocol_map.hpp:1227`, `delphi_protocol_map.hpp:1229` | UI 操作发 request, 列表/状态由返回刷新 |
| 聊天 | `SendSay` -> `ChatSend`, 见 `delphi_protocol_map.hpp:197`, `delphi_protocol_map.hpp:1073` | text focus 先吞快捷键; 频道语义不能被 UI 本地改写 |

## 9. 明确不做

| 不做 | 原因 |
|---|---|
| 不实现 `LegacyUiManager` | 属于 PR-2 |
| 不改输入分发 | 属于 PR-3 |
| 不改 D3D11 绘制顺序 | 属于 PR-4 |
| 不迁移登录/选角/背包等窗口 | 属于 PR-5 之后 |
| 不引入 ImGui/React/HTML | 与 legacy-compatible 目标冲突 |
| 不做响应式布局或 DPI 自动缩放 | 会改变 Delphi 像素级行为 |
| 不把 raw Delphi socket framing 重新引入 ModernClient | 当前客户端设计以 `client_v1` typed protocol 为准 |

## 10. 待源码核对清单

| 项 | 原因 |
|---|---|
| `FrmDlg.pas` | 当前仓库未找到; `FState.pas` 承载 `TFrmDlg` |
| `FState.dfm` | 以资源方式绑定, PR-1 未展开二进制 DFM; 当前坐标证据以 `FState.pas` 初始化和运行时代码为准 |
| `DListBox` 独立类 | 当前 `DWinCtl.pas` 未确认; 可能在其他分支 |
| `KeyUp` 分发 | 当前重点源码中未确认 `DWinMan.KeyUp` |
| 各窗口 `Floating/EnableFocus/Background` 初始化 | 需要逐个构造代码和 DFM/运行时属性核对 |
| 每个窗口是否允许越界和是否记忆位置 | 仅确认 `TDWindow.MouseMove` clamp 逻辑, 未确认每个窗口 `Floating` |
| 字体名称、字号、charset、中文编码 | 当前 C++ 使用 GDI font, Delphi ini/font 细节需专项核对 |
| WIL 透明色和 blend 与 Delphi `TTexture` 完全一致性 | 当前 C++ alpha 解码需 golden image 证明 |
| `DrawScrn.ShowHint` 局部 `dsurface:TTexture` 的真实 TextWidth/TextHeight 调用对象 | `Source/Client/DrawScrn.pas:204` 和 `Source/Client/DrawScrn.pas:213` 静态阅读未看到赋值 |
| 鼠标指针是否由系统光标或软件最后绘制 | 当前 Delphi 片段只确认 moving item 层; cursor 层 `待源码核对` |
| 商店/仓库/行会编辑窗口的完整 Modal/HideAllControls 行为 | 只确认部分函数, 需逐个入口核对 |
| 死亡/复活提示和系统菜单 | 本 PR 只登记范围, 具体窗口待后续源码核对 |
