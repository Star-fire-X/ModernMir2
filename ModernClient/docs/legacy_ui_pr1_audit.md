# PR-1 Legacy UI 审核交付件

## 一、目标与交付标准
1. 固化 Delphi 旧端 UI 输入、绘制、协议与场景行为的对齐证据。
2. 列出 `A/B/C` 三类边界（绝对不能改 / 应保留行为 / 可现代化）并给出验证点。
3. 将 C++ 当前实现与 Delphi 语义映射为可执行差距清单。
4. 明确 PR-2~PR-11 的接口边界与验收条件。

## 二、PR-1 已确认结论

### A. Delphi 与 Legacy 行为（待对齐的绝对约束）
1. 主循环阶段与顺序
- Delphi：`DecodeMessagePacket -> ProcessKeyMessages -> ProcessActionMessages -> DWinMan.Process -> DWinMan.DirectPaint -> DrawScreenTop -> DrawHint -> DXDraw1.Flip`
- C++ 当前：`LegacyFrameScheduler::run_frame` 仍有同名阶段 `timer1_network_drain -> process_key_messages -> process_action_messages -> dwin_process -> draw_screen -> dwin_direct_paint -> draw_screen_top -> draw_hint -> draw_moving_item -> flip`
- 证据：
  - `ModernClient/src/app/legacy_frame_scheduler.hpp:26`
  - `ModernClient/src/app/client_app.cpp:323`
- 风险：`draw_hint` 当前阶段未实际挂接 UI Hint/Hint 栈（在 scheduler 是空实现槽），需在后续 PR 明确接入。

2. UI 优先权与场景输入
- Delphi UI 管理器在输入分发上有优先位。
- C++ 当前场景内有 `capture_ui_input -> process_key_messages -> process_action_messages -> dwin_process -> scene_run`
- 证据：
  - `ModernClient/src/scene/scenes.cpp:7737`
  - `ModernClient/src/scene/scenes.cpp:7740`
- 风险：当前世界场景对 `context.ui_input` 的消费逻辑与输入穿透边界需继续补齐（Esc / Enter / 聊天焦点）。

3. 鼠标点击处理队列化
- C++ 现有 `UiTree` 使用 `capture_input` 只命中/收集，`process_queued_events` 执行，满足“点击事件延迟到 dwin 阶段处理”模型。
- 证据：
  - `ModernClient/src/ui/ui.cpp:864`
  - `ModernClient/src/ui/ui.cpp:939`
  - `ModernClient/src/app/client_app.cpp:339`
  - `ModernClient/src/app/client_app.cpp:399`

4. 模态与 ActiveMenu 优先命中
- `UiTree` 有 `active_menu_` 与 `modal_` 优先命中策略，菜单/模态内点击不会透传。
- 证据：
  - `ModernClient/src/ui/ui.cpp:970`
  - `ModernClient/src/ui/ui.cpp:1026`（相关引用在此文件）
  - `ModernClient/src/ui/ui.cpp:1047`（show_modal）/`close_modal`（清引用）

5. 输入+键位可见性栅栏
- 世界场景 `process_key_messages/process_action_messages` 使用 `context.ui_input.consumed || text_focus || dragging` 的输入闸门。
- 证据：
  - `ModernClient/src/scene/scenes.cpp:6181`
  - `ModernClient/src/scene/scenes.cpp:6257`
  - `ModernClient/src/scene/scenes.cpp:6299`

### B. 可直接用于 PR1 认定的“已迁移能力”
1. 框架级输入/UI 阶段链路已存在。
2. 基础控件集合已在 C++ 中有窗口/按钮/输入框/列表/网格/滚动条（占位）/tooltip/cursor overlay。
3. 世界场景在窗口切换时清理状态有基础链路（`change_scene` 与测试覆盖）。
- 证据：
  - `ModernClient/src/ui/ui.hpp`
  - `ModernClient/src/ui/ui.cpp`
  - `ModernClient/src/scene/scenes.cpp:4448`, `5558`, `5781`, `6072`
  - `ModernClient/tests/world_scene_legacy_order_smoke.cpp`（场景退出清理断言）

### C. 明确“待源码核对 / 待补齐”项
1. Delphi 真实窗口坐标与窗口是否允许越界（部分存在硬编码边界 60/520/570，需和 Delphi 源核验）。
2. Delphi `Hint` 显示阶段与 UI draw top 的“最后叠加顺序细节”需要从 `.pas` 进一步抽样对齐。
3. 拖拽窗口与物品的离窗口边界行为、右键菜单焦点行为、快捷键优先级仍需逐项回填。

## 三、边界清单（PR-1 固化）

### A. 绝对不能改
- 输入分发序列：`capture -> key -> action -> dwin -> scene run` 的阶段关系。
- UI 命中优先：active_menu / modal / 捕获 优先于普通树命中。
- 鼠标/文本输入对场景动作的消费屏障。
- `ProcessKeyMessages` 在场景处理前的优先位。
- `modal` 阻断行为与场景透传关闭顺序。
- 鼠标 capture 在按下到弹起之间必须保持。
- 鼠标点击导致 `focused_/captured_`/`dragging` 状态可回收。
- `legacy_mouse_to_map` 与网格坐标约束逻辑不应与 UI 命中混淆。

### B. 应保留 legacy 但可现代化
- DWin 等价的 `UiTree` 管理模式（窗口树+z-order+模态）。
- `Window/Control` 的可见/启用/焦点/capture 生命周期。
- Tooltip/hint 节点作为“高层显示对象”。
- `TextEdit` 文本焦点阻断场景输入。
- 场景切换触发的窗口状态清理（清空 focus/capture/悬停引用）。
- 资源命中与像素级测试可通过 sprite frame 实现。

### C. 可现代化但可回退验证
- 使用 D3D11 renderer 重排为批处理，但 draw layer 不能跨层重排。
- 统一资源缓存与 atlas（不改 sprite source 索引含义）。
- InputState/事件队列结构现代化，但不改变阶段边界。
- 断线与切场景清理采用统一系统化策略。

## 四、审计证据映射（当前 C++）

### 1) 主循环与渲染链
- `LegacyFrameScheduler` 与 `ClientApp::run()` 已映射 legacy 阶段。
- 场景渲染 `SceneManager::render_scene()` 后接 `paint_ui()`，再接 `render_modal()`。
- 证据：
  - `ModernClient/src/app/legacy_frame_scheduler.hpp`
  - `ModernClient/src/app/client_app.cpp:323, 350, 352, 353`
  - `ModernClient/src/scene/scenes.cpp:7774, 7780, 7788, 7789`
  - `ModernClient/src/app/client_app.cpp:2233, 2237`

### 2) 输入
- `SceneManager::capture_ui_input()` 存在统一入口。
- `UiTree::capture_input/process_queued_events` 实现“命中 + 队列处理”两拍。
- 证据：
  - `ModernClient/src/scene/scenes.cpp:7750, 7762, 7764`
  - `ModernClient/src/ui/ui.cpp:864, 939, 870`
  - `ModernClient/src/scene/scenes.cpp:6303`

### 3) 现有测试覆盖（已存在）
- `legacy_scene_management` trace（阶段顺序）
- `world_scene_legacy_order`（输入闸门与清理）
- `ui_capture_process_order` / `ui_smoke`（capture 与点击顺序）
- `trade_group_guild_ui_smoke`（窗口交互链）

## 五、PR-1 技术差距（直接可执行项）

### 立即可判定为差距
1. `draw_hint` 在 `ClientApp` 渲染链目前未接入 `Scene` 的系统消息/hover 提示链路（scheduler 里保留了阶段，实际实现为空）。  
   - 文件：`ModernClient/src/app/client_app.cpp:323`（hook 为空）
2. `draw_screen_top`/`draw_hint` 的当前 UI 来源不是统一 trace 对齐来源（缺 trace 节点与现有 `golden` 对应验证）。  
   - 文件：`ModernClient/tests/scene_trace_golden_smoke.cpp`

### 与 Delphi 对齐需继续补录的关键点
1. `UI/Window` 默认坐标表与可见性越界策略核验。
2. `chat` 输入焦点下的 Enter/Esc/F1-11/F2.. 的 exact 优先级。
3. `ModalStack` 与 `ActiveMenu` 在同帧关闭顺序。
4. 物品拖拽 release/abort 在窗口关闭、场景切换时的状态回收。

## 六、PR-2..PR-11 执行建议（接口约束）

1. PR-2：UI 核心模型对齐（Window/Control/Tooltip/Modal 关键语义）
2. PR-3：输入分发与消费规则（捕获、modal、active_menu、dragging、防穿透）
3. PR-4：draw 层与阶段 trace（Top/Hint/cursor 与 frame 顺序）
4. PR-5：登录/选角/角色创建/删除/切场景闭环
5. PR-6：主场景 HUD、聊天输入
6. PR-7：背包/装备/tooltip 与拖拽状态
7. PR-8：技能/NPC/商店/仓库窗口链
8. PR-9：交易/组队/行会与 server ack 绑定
9. PR-10：异常路径与生命周期（跨场景清理、悬空拖拽、重复事件）
10. PR-11：完整测试和 CI（trace/golden/fuzz）

## 七、PR-1 结论
1. 当前仓库已经具备“可开始 PR-2 的输入/渲染基础框架”。
2. PR-1 的关键结论是：**主循环骨架与阶段顺序已就位，但 Hint/Top 渲染与部分 legacy 语义（窗口边界/坐标/键序）仍未闭环**。
3. 下一步必须用 `PR-2` 开始补齐以下硬约束：
   - modal 与 active_menu 边界行为与窗口树清理。
   - `draw_hint` 与 `draw_screen_top` 对应 UI 数据源绑定。
   - 世界场景输入闸门和键序在关键窗口弹出/关闭时的一致性。
