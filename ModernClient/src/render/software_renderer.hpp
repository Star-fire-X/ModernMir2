// ============================================================
// Mir2 现代客户端 — 软件渲染器声明
// 职责：软件表面（SoftwareSurface）的像素级绘制操作，
//       以及通过 D3D11 将软件表面上传到纹理并呈现到屏幕
//
// 渲染架构：
//   两层架构：软件光栅化层 + D3D11 呈现层
//
//   软件层（SoftwareSurface）：
//     32 位 BGRA 像素缓冲区，提供 fill_rect、stroke_rect、
//     blend_mask、blit_rgba 等 2D 绘制操作。所有绘制操作
//     直接操作内存中的像素数组，不涉及硬件加速。
//
//   D3D11 呈现层（SoftwareRenderer）：
//     每帧将 SoftwareSurface 的内容上传到 D3D11 动态纹理，
//     再通过全屏四边形（triangle strip）绘制到后台缓冲区，
//     Present() 提交到屏幕。
//
// 设计原因：
//   传奇客户端的渲染以 2D 精灵（sprite）为主，直接软件渲染
//   可以精确控制每个像素的混合效果，与 DX7 原版客户端的行为
//   更接近。同时避免了对 Direct3D 9/11 2D 辅助库的依赖。
//   软件表面分辨率为 800x600（经典传奇分辨率），
//   通过 D3D11 视口变换居中显示在窗口客户区中。
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <windows.h>
#include <d3d11.h>

namespace mir2::client {

/// 整数矩形，用于坐标计算、裁剪和碰撞检测
struct RectI {
  int x{0};
  int y{0};
  int w{0};
  int h{0};

  /// 检测点 (px, py) 是否在矩形内部（左闭右开区间）
  [[nodiscard]] bool contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};

/// 软件表面：32 位 BGRA 像素缓冲区，提供软件绘制原语
/// 所有绘制操作直接操作内存中的像素数组，不做 GPU 加速
/// 像素格式：0xAABBGGRR（32 位，Alpha + 蓝 + 绿 + 红）
class SoftwareSurface {
 public:
  SoftwareSurface() = default;
  SoftwareSurface(int width, int height);

  /// 调整表面尺寸，重新分配像素缓冲区（全部清零）
  void resize(int width, int height);
  /// 用指定颜色填充整个表面（通常用于帧清除）
  void clear(std::uint32_t color);
  /// 填充矩形区域（无 Alpha 混合，直接覆盖）
  void fill_rect(const RectI& rect, std::uint32_t color);
  /// 绘制矩形边框（四条边各 1 像素，使用 fill_rect 实现）
  void stroke_rect(const RectI& rect, std::uint32_t color);
  /// Alpha 遮罩混合：用颜色和灰度遮罩图对目标区域做 Alpha 混合
  /// 典型用途：文字渲染。遮罩中每个像素的亮度值即为 Alpha
  void blend_mask(int x, int y, int width, int height, const std::vector<std::uint8_t>& mask,
                  std::uint32_t color);
  /// 绘制 RGBA 精灵帧（支持全局 Alpha 缩放和逐像素 Alpha 混合）
  /// @param global_alpha 255 = 完全不透明，0 = 完全透明
  void blit_rgba(int x, int y, int width, int height, const std::uint32_t* pixels,
                 std::uint8_t global_alpha = 255U);
  /// Delphi DrawBlend 风格绘制：透明源像素跳过，非透明像素使用 screen-like 混合
  void blit_rgba_legacy_blend(int x, int y, int width, int height,
                              const std::uint32_t* pixels);

  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] std::uint32_t* data();
  [[nodiscard]] const std::uint32_t* data() const;

 private:
  int width_{0};
  int height_{0};
  std::vector<std::uint32_t> pixels_{};  ///< BGRA 像素数组，行优先存储，每行宽度 = width_
};

/// 软件渲染器：管理 SoftwareSurface 并通过 D3D11 提交到屏幕
///
/// 每帧渲染管线：
///   1. begin_frame() — 用清除色填充软件表面
///   2. 各模块向 surface_ 绘制内容（精灵、UI、文字等）
///   3. present() — 将软件表面上采样到 D3D11 纹理并呈现
///
/// 呈现管线（present）：
///   1. 将 surface_ 内容逐行复制到 D3D11 动态纹理（CPU -> GPU）
///   2. 清除 D3D11 后台缓冲区
///   3. 设置视口（居中，保持宽高比），绘制全屏四边形
///   4. SwapChain::Present(1, 0) 提交并等待垂直同步
class SoftwareRenderer {
 public:
  ~SoftwareRenderer();

  /// 初始化 D3D11 设备和渲染器
  bool initialize(HWND hwnd, int logical_width, int logical_height);
  /// 处理窗口尺寸变化，重建后台缓冲区资源
  void resize(int client_width, int client_height);
  /// 开始新帧：用清除色填充软件表面
  void begin_frame(std::uint32_t clear_color);
  /// 将软件表面呈现到屏幕（上传纹理 + DrawCall + Present）
  void present();
  /// 计算逻辑分辨率下的居中视口（保持宽高比）
  /// @return 视口在客户区中的位置和尺寸（逻辑坐标对齐到整数缩放）
  [[nodiscard]] RectI logical_viewport() const;
  /// 将窗口客户区坐标映射到逻辑坐标（用于鼠标输入）
  [[nodiscard]] POINT window_to_logical(int client_x, int client_y) const;
  /// 将 800x600 逻辑矩形映射回窗口客户区矩形（用于原生 EDIT 覆盖层）
  [[nodiscard]] RectI logical_to_window_rect(const RectI& logical_rect) const;

  // 便捷代理方法（直接操作 surface_）
  void fill_rect(const RectI& rect, std::uint32_t color) { surface_.fill_rect(rect, color); }
  void stroke_rect(const RectI& rect, std::uint32_t color) { surface_.stroke_rect(rect, color); }
  /// 绘制文字（通过 GDI 缓存文字遮罩后使用 blend_mask 混合到表面）
  void draw_text(int x, int y, const std::wstring& text, std::uint32_t color);
  /// 绘制 Delphi 风格阴影文字：先绘制 1px 黑色阴影，再绘制正文
  void draw_text_shadowed(int x, int y, const std::wstring& text, std::uint32_t color,
                          std::uint32_t shadow_color = 0xFF000000U);
  /// 测量文字像素宽度（复用 GDI 文字缓存，与 draw_text 使用同一字体）
  [[nodiscard]] int measure_text_width(const std::wstring& text);

  [[nodiscard]] SoftwareSurface& surface();
  [[nodiscard]] const SoftwareSurface& surface() const;
  [[nodiscard]] int logical_width() const;
  [[nodiscard]] int logical_height() const;

 private:
  /// GDI 文字缓存：预渲染文字的 Alpha 遮罩
  /// 缓存已渲染的文字遮罩可避免重复的 GDI 调用，提高文字渲染性能
  struct CachedText {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> alpha{};  // 每个像素的 Alpha 值（0-255）
  };

  /// D3D11 全屏四边形的顶点结构（位置 + UV）
  struct Vertex {
    float x;
    float y;
    float u;
    float v;
  };

  static void safe_release(IUnknown* object);
  bool create_device(HWND hwnd);              ///< 创建 D3D11 设备和交换链
  bool create_shaders();                      ///< 编译着色器并创建输入布局/顶点缓冲
  bool create_backbuffer_resources();         ///< 创建渲染目标视图和动态纹理
  void destroy_device();                      ///< 释放所有 D3D11 资源
  CachedText& cache_text(const std::wstring& text);  ///< 获取或创建文字 Alpha 遮罩缓存

  HWND hwnd_{nullptr};
  int logical_width_{0};   ///< 逻辑宽度（800，与经典传奇客户端一致）
  int logical_height_{0};  ///< 逻辑高度（600）
  int client_width_{0};    ///< 窗口客户区实际宽度（由 WM_SIZE 更新）
  int client_height_{0};   ///< 窗口客户区实际高度
  SoftwareSurface surface_{};  ///< 软件绘制表面（800x600 BGRA 像素缓冲区）

  // D3D11 资源
  ID3D11Device* device_{nullptr};
  ID3D11DeviceContext* context_{nullptr};
  IDXGISwapChain* swap_chain_{nullptr};
  ID3D11RenderTargetView* render_target_view_{nullptr};
  ID3D11Texture2D* texture_{nullptr};                    ///< 动态纹理（CPU 写入，GPU 读取）
  ID3D11ShaderResourceView* shader_resource_view_{nullptr};
  ID3D11SamplerState* sampler_state_{nullptr};            ///< 点采样，Clamp 寻址
  ID3D11VertexShader* vertex_shader_{nullptr};
  ID3D11PixelShader* pixel_shader_{nullptr};
  ID3D11InputLayout* input_layout_{nullptr};
  ID3D11Buffer* vertex_buffer_{nullptr};

  HFONT font_{nullptr};                        ///< GDI 字体句柄（MS Sans Serif, 12pt）
  std::unordered_map<std::wstring, CachedText> text_cache_{};  ///< 文字遮罩缓存映射表
};

}  // namespace mir2::client
