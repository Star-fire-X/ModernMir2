// ============================================================
// Mir2 现代客户端 — 软件渲染器实现
// 职责：软件表面绘制（填充/混合/精灵）和 D3D11 呈现
// 架构：纯软件光栅化 + D3D11 纹理上传 + 全屏四边形
//
// 渲染流程详解：
// 传奇客户端的渲染以 2D 精灵（sprite）为主，通过 DirectDraw 7
// 的后备缓冲区（backbuffer）和离屏表面（offscreen surface）
// 实现双缓冲渲染。本实现使用等效的两层架构：
//
// 软件层：在内存中的 BGRA 像素缓冲区进行操作，逐像素控制
//         所有混合效果，与传奇客户端调色板/透明色处理一致。
// D3D11 层：将软件缓冲区作为纹理上传到 GPU，通过一个覆盖
//           NDC [-1,1] 范围的全屏四边形绘制到屏幕。使用
//           点采样（point filtering）保证像素完美的缩放。
// ============================================================

#include "render/software_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>

#include <d3dcompiler.h>

namespace mir2::client {

namespace {

// 顶点着色器源代码
// 简单的传递着色器，接收位置（POSITION）和 UV（TEXCOORD0），
// 直接将位置传递到光栅化阶段，UV 传递到像素着色器
constexpr char kVertexShaderSource[] = R"(
struct VSInput {
  float2 pos : POSITION;
  float2 uv : TEXCOORD0;
};

struct PSInput {
  float4 pos : SV_POSITION;
  float2 uv : TEXCOORD0;
};

PSInput main(VSInput input) {
  PSInput output;
  output.pos = float4(input.pos.xy, 0.0f, 1.0f);
  output.uv = input.uv;
  return output;
}
)";

// 像素着色器源代码
// 从纹理（Texture2D）中根据 UV 坐标采样颜色并输出
// 采样器使用点采样（point filtering），保证缩放时不产生模糊
constexpr char kPixelShaderSource[] = R"(
Texture2D surface_tex : register(t0);
SamplerState surface_sampler : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
  return surface_tex.Sample(surface_sampler, uv);
}
)";

// 提取 32 位 BGRA 颜色值中指定通道的 8 位值
// shift: 16=红, 8=绿, 0=蓝
std::uint8_t channel(std::uint32_t color, int shift) {
  return static_cast<std::uint8_t>((color >> shift) & 0xFFU);
}

}  // namespace

// ---- SoftwareSurface 实现 ----

SoftwareSurface::SoftwareSurface(int width, int height) { resize(width, height); }

void SoftwareSurface::resize(int width, int height) {
  width_ = width;
  height_ = height;
  pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0U);
}

void SoftwareSurface::clear(std::uint32_t color) { std::fill(pixels_.begin(), pixels_.end(), color); }

// 填充矩形区域：逐行写入颜色，带边界裁剪
// 防止绘制超出表面边界的矩形导致越界访问
void SoftwareSurface::fill_rect(const RectI& rect, std::uint32_t color) {
  const auto left = std::max(0, rect.x);
  const auto top = std::max(0, rect.y);
  const auto right = std::min(width_, rect.x + rect.w);
  const auto bottom = std::min(height_, rect.y + rect.h);
  for (int y = top; y < bottom; ++y) {
    auto* row = pixels_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
    for (int x = left; x < right; ++x) {
      row[x] = color;
    }
  }
}

// 绘制矩形边框：分别绘制上、下、左、右四条 1 像素宽度的边
void SoftwareSurface::stroke_rect(const RectI& rect, std::uint32_t color) {
  fill_rect(RectI{rect.x, rect.y, rect.w, 1}, color);
  fill_rect(RectI{rect.x, rect.y + rect.h - 1, rect.w, 1}, color);
  fill_rect(RectI{rect.x, rect.y, 1, rect.h}, color);
  fill_rect(RectI{rect.x + rect.w - 1, rect.y, 1, rect.h}, color);
}

// Alpha 遮罩混合：使用颜色值和灰度遮罩图对目标区域做 Alpha 混合
// 遮罩中每个像素的亮度值作为该位置的 Alpha 值
// 混合公式：out = src * alpha/255 + dst * (1 - alpha/255)
// 典型用途：GDI 渲染文字后提取遮罩，将文字以指定颜色混合到表面
void SoftwareSurface::blend_mask(int x, int y, int width, int height,
                                 const std::vector<std::uint8_t>& mask, std::uint32_t color) {
  const auto src_r = channel(color, 16);
  const auto src_g = channel(color, 8);
  const auto src_b = channel(color, 0);

  for (int row = 0; row < height; ++row) {
    const auto dst_y = y + row;
    if (dst_y < 0 || dst_y >= height_) {
      continue;
    }
    for (int col = 0; col < width; ++col) {
      const auto dst_x = x + col;
      if (dst_x < 0 || dst_x >= width_) {
        continue;
      }
      // 从遮罩中读取 Alpha 值
      const auto alpha = mask[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(col)];
      if (alpha == 0) {
        continue;  // 完全透明，跳过
      }
      auto& dst =
          pixels_[static_cast<std::size_t>(dst_y) * static_cast<std::size_t>(width_) +
                  static_cast<std::size_t>(dst_x)];
      const auto dst_r = channel(dst, 16);
      const auto dst_g = channel(dst, 8);
      const auto dst_b = channel(dst, 0);
      // 标准 Alpha 混合公式：
      // out = src * alpha + dst * (1 - alpha)
      const auto inv = static_cast<std::uint8_t>(255U - alpha);
      const auto out_r =
          static_cast<std::uint8_t>((src_r * alpha + dst_r * inv) / 255U);
      const auto out_g =
          static_cast<std::uint8_t>((src_g * alpha + dst_g * inv) / 255U);
      const auto out_b =
          static_cast<std::uint8_t>((src_b * alpha + dst_b * inv) / 255U);
      dst = 0xFF000000U | (static_cast<std::uint32_t>(out_r) << 16U) |
            (static_cast<std::uint32_t>(out_g) << 8U) | out_b;
    }
  }
}

// 绘制 RGBA 精灵帧：支持全局 Alpha 缩放和逐像素 Alpha 混合
// 当全局 Alpha = 255 且像素 Alpha = 255 时直接覆盖目标（不透明模式）
// 当像素 Alpha = 0 时跳过该像素（透明模式）
// 其他情况做标准 Alpha 混合
void SoftwareSurface::blit_rgba(int x, int y, int width, int height, const std::uint32_t* pixels,
                                const std::uint8_t global_alpha) {
  if (pixels == nullptr || width <= 0 || height <= 0 || global_alpha == 0) {
    return;
  }

  for (int row = 0; row < height; ++row) {
    const auto dst_y = y + row;
    if (dst_y < 0 || dst_y >= height_) {
      continue;  // Y 方向超出表面边界
    }
    for (int col = 0; col < width; ++col) {
      const auto dst_x = x + col;
      if (dst_x < 0 || dst_x >= width_) {
        continue;  // X 方向超出表面边界
      }

      const auto src =
          pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(col)];
      auto alpha = static_cast<std::uint8_t>((src >> 24U) & 0xFFU);
      if (alpha == 0) {
        continue;  // 源像素完全透明，跳过
      }
      // 应用全局 Alpha 缩放（用于淡入淡出等效果）
      alpha = static_cast<std::uint8_t>(
          (static_cast<std::uint16_t>(alpha) * static_cast<std::uint16_t>(global_alpha)) / 255U);
      auto& dst =
          pixels_[static_cast<std::size_t>(dst_y) * static_cast<std::size_t>(width_) +
                  static_cast<std::size_t>(dst_x)];
      // 完全不透明时直接覆盖（避免不必要的混合计算）
      if (alpha == 255) {
        dst = src;
        continue;
      }

      // 标准 Alpha 混合
      const auto src_r = channel(src, 16);
      const auto src_g = channel(src, 8);
      const auto src_b = channel(src, 0);
      const auto dst_r = channel(dst, 16);
      const auto dst_g = channel(dst, 8);
      const auto dst_b = channel(dst, 0);
      const auto inv = static_cast<std::uint8_t>(255U - alpha);
      const auto out_r =
          static_cast<std::uint8_t>((src_r * alpha + dst_r * inv) / 255U);
      const auto out_g =
          static_cast<std::uint8_t>((src_g * alpha + dst_g * inv) / 255U);
      const auto out_b =
          static_cast<std::uint8_t>((src_b * alpha + dst_b * inv) / 255U);
      dst = 0xFF000000U | (static_cast<std::uint32_t>(out_r) << 16U) |
            (static_cast<std::uint32_t>(out_g) << 8U) | out_b;
    }
  }
}

void SoftwareSurface::blit_rgba_legacy_blend(int x, int y, int width, int height,
                                             const std::uint32_t* pixels) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return;
  }

  const auto blend_channel = [](const std::uint8_t src, const std::uint8_t dst) {
    const auto value =
        static_cast<int>(src) + ((255 - static_cast<int>(src)) * static_cast<int>(dst) + 127) / 255;
    return static_cast<std::uint8_t>(std::min(255, value));
  };

  for (int row = 0; row < height; ++row) {
    const auto dst_y = y + row;
    if (dst_y < 0 || dst_y >= height_) {
      continue;
    }
    for (int col = 0; col < width; ++col) {
      const auto dst_x = x + col;
      if (dst_x < 0 || dst_x >= width_) {
        continue;
      }

      const auto src =
          pixels[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(col)];
      if (((src >> 24U) & 0xFFU) == 0U) {
        continue;
      }

      auto& dst =
          pixels_[static_cast<std::size_t>(dst_y) * static_cast<std::size_t>(width_) +
                  static_cast<std::size_t>(dst_x)];
      const auto out_r = blend_channel(channel(src, 16), channel(dst, 16));
      const auto out_g = blend_channel(channel(src, 8), channel(dst, 8));
      const auto out_b = blend_channel(channel(src, 0), channel(dst, 0));
      dst = 0xFF000000U | (static_cast<std::uint32_t>(out_r) << 16U) |
            (static_cast<std::uint32_t>(out_g) << 8U) | out_b;
    }
  }
}

int SoftwareSurface::width() const { return width_; }

int SoftwareSurface::height() const { return height_; }

std::uint32_t* SoftwareSurface::data() { return pixels_.data(); }

const std::uint32_t* SoftwareSurface::data() const { return pixels_.data(); }

// ---- SoftwareRenderer 实现 ----

SoftwareRenderer::~SoftwareRenderer() { destroy_device(); }

// 初始化渲染器：创建 D3D11 设备、着色器、后台缓冲区资源
// 同时创建经典客户端字体（MS Sans Serif, 12pt）用于文字绘制
bool SoftwareRenderer::initialize(HWND hwnd, int logical_width, int logical_height) {
  hwnd_ = hwnd;
  logical_width_ = logical_width;
  logical_height_ = logical_height;
  surface_.resize(logical_width_, logical_height_);

  // 获取窗口客户区初始尺寸
  RECT rect{};
  GetClientRect(hwnd_, &rect);
  client_width_ = rect.right - rect.left;
  client_height_ = rect.bottom - rect.top;

  // 创建传奇客户端风格字体（原版默认使用 MS Sans Serif）
  // 负高度值表示以字符高度为单位的字体尺寸（与 Delphi 的字体创建兼容）
  constexpr int kLegacyTextHeight = -12;
  font_ = CreateFontW(kLegacyTextHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"MS Sans Serif");

  return create_device(hwnd_) && create_shaders() && create_backbuffer_resources();
}

// 处理窗口尺寸变化：释放旧的后台缓冲区资源并按新尺寸重建
// 在窗口 WM_SIZE 消息处理完成后由主循环调用
void SoftwareRenderer::resize(int client_width, int client_height) {
  client_width_ = std::max(1, client_width);
  client_height_ = std::max(1, client_height);
  if (swap_chain_ == nullptr) {
    return;
  }

  // Resize 前必须释放所有依赖后台缓冲区尺寸的资源
  // 否则 ResizeBuffers 会失败
  safe_release(render_target_view_);
  safe_release(texture_);
  safe_release(shader_resource_view_);
  // 调整交换链缓冲区尺寸（保留现有格式）
  swap_chain_->ResizeBuffers(0, static_cast<UINT>(client_width_), static_cast<UINT>(client_height_),
                             DXGI_FORMAT_UNKNOWN, 0);
  create_backbuffer_resources();
}

void SoftwareRenderer::begin_frame(std::uint32_t clear_color) { surface_.clear(clear_color); }

// 计算逻辑视口：在客户区中居中显示，保持宽高比
// 放大的时候取整数倍缩放（floor），避免亚像素扭曲
// 缩小的时候使用浮点数缩放，让窗口自适应
RectI SoftwareRenderer::logical_viewport() const {
  if (logical_width_ <= 0 || logical_height_ <= 0 || client_width_ <= 0 || client_height_ <= 0) {
    return RectI{};
  }

  const auto scale_x = static_cast<float>(client_width_) / static_cast<float>(logical_width_);
  const auto scale_y = static_cast<float>(client_height_) / static_cast<float>(logical_height_);
  auto scale = std::min(scale_x, scale_y);
  // 放大时取整数倍，保持像素完美缩放（点采样不产生模糊）
  if (scale >= 1.0f) {
    scale = std::max(1.0f, std::floor(scale));
  }

  const auto viewport_width = static_cast<int>(std::round(static_cast<float>(logical_width_) * scale));
  const auto viewport_height =
      static_cast<int>(std::round(static_cast<float>(logical_height_) * scale));
  return RectI{(client_width_ - viewport_width) / 2, (client_height_ - viewport_height) / 2,
               viewport_width, viewport_height};
}

// 窗口坐标到逻辑坐标的映射：将客户区像素坐标映射到逻辑分辨率（800x600）坐标
// 用于鼠标输入坐标转换，确保在不同窗口尺寸下点击位置与逻辑坐标一致
POINT SoftwareRenderer::window_to_logical(int client_x, int client_y) const {
  POINT point{-1, -1};
  const auto viewport = logical_viewport();
  if (viewport.w <= 0 || viewport.h <= 0 || !viewport.contains(client_x, client_y)) {
    return point;  // 在视口外返回 (-1, -1)
  }

  const auto relative_x = static_cast<float>(client_x - viewport.x) / static_cast<float>(viewport.w);
  const auto relative_y = static_cast<float>(client_y - viewport.y) / static_cast<float>(viewport.h);
  point.x =
      std::clamp(static_cast<int>(relative_x * static_cast<float>(logical_width_)), 0, logical_width_ - 1);
  point.y =
      std::clamp(static_cast<int>(relative_y * static_cast<float>(logical_height_)), 0, logical_height_ - 1);
  return point;
}

RectI SoftwareRenderer::logical_to_window_rect(const RectI& logical_rect) const {
  const auto viewport = logical_viewport();
  if (viewport.w <= 0 || viewport.h <= 0 || logical_width_ <= 0 || logical_height_ <= 0 ||
      logical_rect.w <= 0 || logical_rect.h <= 0) {
    return RectI{};
  }

  const auto scale_x = static_cast<float>(viewport.w) / static_cast<float>(logical_width_);
  const auto scale_y = static_cast<float>(viewport.h) / static_cast<float>(logical_height_);
  const auto left =
      viewport.x + static_cast<int>(std::round(static_cast<float>(logical_rect.x) * scale_x));
  const auto top =
      viewport.y + static_cast<int>(std::round(static_cast<float>(logical_rect.y) * scale_y));
  const auto right = viewport.x + static_cast<int>(std::round(
                                      static_cast<float>(logical_rect.x + logical_rect.w) * scale_x));
  const auto bottom = viewport.y + static_cast<int>(std::round(
                                       static_cast<float>(logical_rect.y + logical_rect.h) * scale_y));

  return RectI{left, top, std::max(1, right - left), std::max(1, bottom - top)};
}

// 呈现当前帧到屏幕：
//   1. 将 software surface 内容逐行复制到 D3D11 动态纹理
//   2. 清除 D3D11 后台缓冲区（深灰色）
//   3. 设置居中视口（保持宽高比），绑定着色器和顶点缓冲
//   4. 绘制全屏四边形（triangle strip, 4 顶点）
//   5. SwapChain::Present(1, 0) 等待垂直同步后交换缓冲区
void SoftwareRenderer::present() {
  if (context_ == nullptr || swap_chain_ == nullptr || texture_ == nullptr || render_target_view_ == nullptr) {
    return;
  }

  // Step 1: 将软件表面复制到 D3D11 动态纹理
  // Map(DISCARD) 丢弃旧内容，返回新指针，避免 GPU 等待
  D3D11_MAPPED_SUBRESOURCE mapped{};
  if (SUCCEEDED(context_->Map(texture_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
    for (int y = 0; y < logical_height_; ++y) {
      auto* dst = static_cast<std::uint8_t*>(mapped.pData) +
                  static_cast<std::size_t>(y) * static_cast<std::size_t>(mapped.RowPitch);
      const auto* src =
          reinterpret_cast<const std::uint8_t*>(surface_.data() +
                                                static_cast<std::size_t>(y) *
                                                    static_cast<std::size_t>(logical_width_));
      std::memcpy(dst, src, static_cast<std::size_t>(logical_width_) * sizeof(std::uint32_t));
    }
    context_->Unmap(texture_, 0);
  }

  // Step 2: 清除后台缓冲区（深灰色背景填充黑边区域）
  const auto clear_color = std::array<float, 4>{0.02f, 0.02f, 0.02f, 1.0f};
  context_->OMSetRenderTargets(1, &render_target_view_, nullptr);
  context_->ClearRenderTargetView(render_target_view_, clear_color.data());

  // Step 3: 设置居中视口，保持 4:3 宽高比
  const auto logical_viewport_rect = logical_viewport();
  D3D11_VIEWPORT viewport{};
  viewport.TopLeftX = static_cast<float>(logical_viewport_rect.x);
  viewport.TopLeftY = static_cast<float>(logical_viewport_rect.y);
  viewport.Width = static_cast<float>(logical_viewport_rect.w);
  viewport.Height = static_cast<float>(logical_viewport_rect.h);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;

  // Step 4: 全屏四边形绘制（triangle strip, 4 个顶点 = 2 个三角形）
  const auto stride = static_cast<UINT>(sizeof(Vertex));
  const auto offset = 0U;
  context_->RSSetViewports(1, &viewport);
  context_->IASetInputLayout(input_layout_);
  context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  context_->IASetVertexBuffers(0, 1, &vertex_buffer_, &stride, &offset);
  context_->VSSetShader(vertex_shader_, nullptr, 0);
  context_->PSSetShader(pixel_shader_, nullptr, 0);
  context_->PSSetShaderResources(0, 1, &shader_resource_view_);
  context_->PSSetSamplers(0, 1, &sampler_state_);
  context_->Draw(4, 0);

  // Step 5: Present 并等待垂直同步
  // Present(1, 0) 的参数 1 表示每帧等待垂直消隐（vsync），
  // 限制帧率到显示器刷新率，避免撕裂
  swap_chain_->Present(1, 0);
}

// 绘制文字：使用 GDI 将文字渲染到内存 DC 中提取 Alpha 遮罩，
// 然后通过 blend_mask 将文字混合到软件表面
// GDI 渲染的文字质量好，但每次调用需要 DC 操作，
// 因此使用 cache_text 缓存遮罩避免重复 GDI 调用
void SoftwareRenderer::draw_text(int x, int y, const std::wstring& text, std::uint32_t color) {
  if (text.empty()) {
    return;
  }
  auto& cached = cache_text(text);
  surface_.blend_mask(x, y, cached.width, cached.height, cached.alpha, color);
}

void SoftwareRenderer::draw_text_shadowed(int x, int y, const std::wstring& text,
                                          std::uint32_t color,
                                          std::uint32_t shadow_color) {
  if (text.empty()) {
    return;
  }
  draw_text(x + 1, y + 1, text, shadow_color);
  draw_text(x, y, text, color);
}

int SoftwareRenderer::measure_text_width(const std::wstring& text) {
  if (text.empty()) {
    return 0;
  }
  auto& cached = cache_text(text);
  return std::max(0, cached.width - 4);
}

SoftwareSurface& SoftwareRenderer::surface() { return surface_; }

const SoftwareSurface& SoftwareRenderer::surface() const { return surface_; }

int SoftwareRenderer::logical_width() const { return logical_width_; }

int SoftwareRenderer::logical_height() const { return logical_height_; }

void SoftwareRenderer::safe_release(IUnknown* object) {
  if (object != nullptr) {
    object->Release();
  }
}

// 创建 D3D11 设备和交换链
// 使用 BGRA 格式（与 SoftwareSurface 像素格式匹配）、
// 双缓冲、DISCARD 交换效果（仅支持窗口模式）
bool SoftwareRenderer::create_device(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
  swap_chain_desc.BufferDesc.Width = static_cast<UINT>(client_width_);
  swap_chain_desc.BufferDesc.Height = static_cast<UINT>(client_height_);
  swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.BufferCount = 2;          // 双缓冲
  swap_chain_desc.OutputWindow = hwnd;
  swap_chain_desc.SampleDesc.Count = 1;     // 无多重采样
  swap_chain_desc.Windowed = TRUE;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;  // 旧式交换效果

  // D3D11_CREATE_DEVICE_BGRA_SUPPORT 标志使交换链支持 BGRA 格式
  constexpr auto flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  const auto feature_levels = std::array<D3D_FEATURE_LEVEL, 2>{
      D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL created_level{};
  return SUCCEEDED(D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, feature_levels.data(),
      static_cast<UINT>(feature_levels.size()), D3D11_SDK_VERSION, &swap_chain_desc, &swap_chain_,
      &device_, &created_level, &context_));
}

// 编译着色器并创建渲染管线状态
// 包含：顶点着色器、像素着色器、输入布局、顶点缓冲区、采样器
bool SoftwareRenderer::create_shaders() {
  ID3DBlob* vertex_blob = nullptr;
  ID3DBlob* pixel_blob = nullptr;
  ID3DBlob* error_blob = nullptr;

  // 编译顶点着色器（目标 shader model 4.0）
  const auto vertex_ok = SUCCEEDED(D3DCompile(kVertexShaderSource, std::strlen(kVertexShaderSource),
                                              nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0,
                                              &vertex_blob, &error_blob));
  if (!vertex_ok) {
    safe_release(error_blob);
    return false;
  }
  safe_release(error_blob);
  if (FAILED(device_->CreateVertexShader(vertex_blob->GetBufferPointer(),
                                         vertex_blob->GetBufferSize(), nullptr, &vertex_shader_))) {
    safe_release(vertex_blob);
    return false;
  }

  // 编译像素着色器（目标 shader model 4.0）
  const auto pixel_ok = SUCCEEDED(D3DCompile(kPixelShaderSource, std::strlen(kPixelShaderSource),
                                             nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0,
                                             &pixel_blob, &error_blob));
  if (!pixel_ok) {
    safe_release(vertex_blob);
    safe_release(error_blob);
    return false;
  }
  safe_release(error_blob);
  if (FAILED(device_->CreatePixelShader(pixel_blob->GetBufferPointer(), pixel_blob->GetBufferSize(),
                                        nullptr, &pixel_shader_))) {
    safe_release(vertex_blob);
    safe_release(pixel_blob);
    return false;
  }

  // 创建输入布局：POSITION（R32G32_FLOAT）+ TEXCOORD（R32G32_FLOAT）
  const std::array<D3D11_INPUT_ELEMENT_DESC, 2> layout{{
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
  }};

  if (FAILED(device_->CreateInputLayout(layout.data(), static_cast<UINT>(layout.size()),
                                        vertex_blob->GetBufferPointer(), vertex_blob->GetBufferSize(),
                                        &input_layout_))) {
    safe_release(vertex_blob);
    safe_release(pixel_blob);
    return false;
  }

  // 全屏四边形顶点：覆盖整个 NDC [-1, 1] 范围
  // UV 坐标与纹理坐标匹配 (0,0) = 左上, (1,1) = 右下
  // 顺序：左上, 右上, 左下, 右下（triangle strip 自动连接三角形）
  const std::array<Vertex, 4> vertices{{
      {-1.0f, 1.0f, 0.0f, 0.0f},   // 左上顶点
      {1.0f, 1.0f, 1.0f, 0.0f},    // 右上顶点
      {-1.0f, -1.0f, 0.0f, 1.0f},  // 左下顶点
      {1.0f, -1.0f, 1.0f, 1.0f},   // 右下顶点
  }};

  // 顶点缓冲区不可变（IMMUTABLE），创建时一次性写入
  D3D11_BUFFER_DESC buffer_desc{};
  buffer_desc.ByteWidth = static_cast<UINT>(sizeof(vertices));
  buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
  D3D11_SUBRESOURCE_DATA buffer_data{};
  buffer_data.pSysMem = vertices.data();
  if (FAILED(device_->CreateBuffer(&buffer_desc, &buffer_data, &vertex_buffer_))) {
    safe_release(vertex_blob);
    safe_release(pixel_blob);
    return false;
  }

  // 创建采样器状态：点采样（point filter），寻址模式为 Clamp
  // 点采样保证缩放时不产生模糊，保持像素完美呈现
  D3D11_SAMPLER_DESC sampler_desc{};
  sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
  if (FAILED(device_->CreateSamplerState(&sampler_desc, &sampler_state_))) {
    safe_release(vertex_blob);
    safe_release(pixel_blob);
    return false;
  }

  safe_release(vertex_blob);
  safe_release(pixel_blob);
  return true;
}

// 创建后台缓冲区资源：
//   1. 从交换链获取后台缓冲区纹理
//   2. 创建渲染目标视图（RTV）
//   3. 创建 CPU 可写入的动态纹理（用于接收软件表面数据）
//   4. 创建着色器资源视图（SRV，供像素着色器采样）
bool SoftwareRenderer::create_backbuffer_resources() {
  // 获取交换链的后台缓冲区并创建渲染目标视图
  ID3D11Texture2D* backbuffer = nullptr;
  if (FAILED(swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&backbuffer)))) {
    return false;
  }
  const auto rtv_ok = SUCCEEDED(device_->CreateRenderTargetView(backbuffer, nullptr, &render_target_view_));
  safe_release(backbuffer);
  if (!rtv_ok) {
    return false;
  }

  // 创建 CPU 可写入的动态纹理（尺寸 = 逻辑分辨率 800x600）
  // D3D11_USAGE_DYNAMIC + D3D11_CPU_ACCESS_WRITE 允许 CPU 每帧写入
  // D3D11_BIND_SHADER_RESOURCE 使像素着色器可以采样该纹理
  D3D11_TEXTURE2D_DESC texture_desc{};
  texture_desc.Width = static_cast<UINT>(logical_width_);
  texture_desc.Height = static_cast<UINT>(logical_height_);
  texture_desc.MipLevels = 1;
  texture_desc.ArraySize = 1;
  texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // 与软件表面格式一致
  texture_desc.SampleDesc.Count = 1;
  texture_desc.Usage = D3D11_USAGE_DYNAMIC;
  texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(device_->CreateTexture2D(&texture_desc, nullptr, &texture_))) {
    return false;
  }

  // 创建着色器资源视图，使像素着色器可以采样该纹理
  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
  srv_desc.Format = texture_desc.Format;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  srv_desc.Texture2D.MostDetailedMip = 0;
  srv_desc.Texture2D.MipLevels = 1;
  return SUCCEEDED(device_->CreateShaderResourceView(texture_, &srv_desc, &shader_resource_view_));
}

// 销毁所有 D3D11 资源（必须逆序释放）
// 因资源之间有依赖关系（如 RTV 依赖后台缓冲区），
// 释放顺序必须与创建顺序相反
void SoftwareRenderer::destroy_device() {
  if (font_ != nullptr) {
    DeleteObject(font_);
    font_ = nullptr;
  }
  text_cache_.clear();
  safe_release(vertex_buffer_);
  safe_release(input_layout_);
  safe_release(vertex_shader_);
  safe_release(pixel_shader_);
  safe_release(sampler_state_);
  safe_release(shader_resource_view_);
  safe_release(texture_);
  safe_release(render_target_view_);
  safe_release(swap_chain_);
  safe_release(context_);
  safe_release(device_);
  // 将所有指针置为空，防止 double-release
  vertex_buffer_ = nullptr;
  input_layout_ = nullptr;
  vertex_shader_ = nullptr;
  pixel_shader_ = nullptr;
  sampler_state_ = nullptr;
  shader_resource_view_ = nullptr;
  texture_ = nullptr;
  render_target_view_ = nullptr;
  swap_chain_ = nullptr;
  context_ = nullptr;
  device_ = nullptr;
}

// 文字缓存：使用 GDI 将文字渲染到内存 DC 中，提取 Alpha 遮罩
// 缓存以文字字符串为键，避免相同文字的重复 GDI 渲染开销
// 提取白色文字的 R 通道作为 Alpha 值（因为 SetTextColor 设为白色）
SoftwareRenderer::CachedText& SoftwareRenderer::cache_text(const std::wstring& text) {
  const auto cached = text_cache_.find(text);
  if (cached != text_cache_.end()) {
    return cached->second;
  }

  HDC screen = GetDC(hwnd_);
  HDC dc = CreateCompatibleDC(screen);
  SelectObject(dc, font_);
  SIZE size{};
  GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
  const auto width = std::max<int>(1, size.cx + 4);    // +4 边距避免裁切
  const auto height = std::max<int>(1, size.cy + 4);

  // 创建 32 位 DIB 段来渲染文字
  // biHeight = -height 表示自顶向下位图（与 GDI 默认方向匹配）
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;  // 上到下位图（top-down）
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
  SelectObject(dc, bitmap);
  SetBkColor(dc, RGB(0, 0, 0));          // 黑色背景
  SetTextColor(dc, RGB(255, 255, 255));  // 白色文字
  SetBkMode(dc, OPAQUE);
  RECT rect{0, 0, width, height};
  FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  TextOutW(dc, 0, 0, text.c_str(), static_cast<int>(text.size()));

  // 从 DIB 像素中提取红色通道作为 Alpha 遮罩
  // 白色文字的 R=255（不透明），黑色背景的 R=0（透明）
  CachedText result;
  result.width = width;
  result.height = height;
  result.alpha.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0U);

  auto* src = static_cast<const std::uint32_t*>(pixels);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto pixel = src[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)];
      // 红色通道值 = alpha（因为文字是白色的，R 通道 = 亮度）
      result.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] =
          static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
    }
  }

  DeleteObject(bitmap);
  DeleteDC(dc);
  ReleaseDC(hwnd_, screen);

  return text_cache_.emplace(text, std::move(result)).first->second;
}

}  // namespace mir2::client
