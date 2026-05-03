// ============================================================
// Mir2 现代客户端 — 程序入口点
// 功能：初始化 Win32 窗口、Direct3D 渲染、资源加载后进入主循环
// 架构：单线程主循环模式，与老式传奇客户端保持一致
//
// 传奇客户端架构说明：
// 经典传奇（Mir2）使用单线程主循环模型，所有子系统（窗口消息、
// 网络 I/O、游戏逻辑更新、渲染）都在同一线程中顺序执行。
// 这种设计避免了多线程同步问题，与 Delphi 原版客户端的
// Application.ProcessMessages + 定时器驱动模型兼容。
// ============================================================

#include "app/client_app.hpp"

#include <windows.h>

int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                   LPSTR /*lpCmdLine*/, int /*nCmdShow*/) {
  // 创建客户端应用实例
  // 构造时所有子系统（窗口、渲染器、网络、资源管理器等）
  // 均处于未初始化的空状态
  mir2::client::ClientApp app;

  // 初始化所有子系统：
  // 1. 加载 client.ini 配置文件
  // 2. 创建 Win32 窗口（无边框 WS_POPUP）
  // 3. 初始化 D3D11 设备和软件渲染器
  // 4. 初始化网络协议客户端
  // 5. 初始化资源管理器（验证 Data/ 和 Map/ 目录）
  // 6. 创建启动场景（BootScene）
  if (!app.initialize()) {
    return 1;  // 初始化失败（如资源目录缺失、D3D11 设备创建失败等）
  }

  // 进入主消息循环：
  // 每帧执行：窗口消息泵送 -> 网络轮询 -> 输入处理 ->
  //           游戏逻辑更新 -> 场景渲染 -> 画面呈现
  // 循环在收到 WM_QUIT 时退出
  return app.run();
}
