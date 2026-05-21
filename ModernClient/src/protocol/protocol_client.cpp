// ============================================================
// Mir2 现代客户端 — ProtocolClient 实现
// 职责：非阻塞 TCP Socket 管理、select 轮询、编解码帧
// 架构：单线程非阻塞模式，所有 I/O 在 poll() 中完成
//
// 与经典传奇客户端的差异：
// 原版使用阻塞 TClientSocket + 独立接收线程/WM_SOCKET 消息，
// 本实现使用非阻塞模式 + 零超时 select，在主循环中同步完成
// 所有 I/O，避免了多线程同步的复杂性。
// ============================================================

#include "protocol/protocol_client.hpp"

#include <array>

namespace mir2::client {

// 构造函数：初始化 WinSock 2.2
// WSAStartup 是所有 Windows Socket 操作的前提
ProtocolClient::ProtocolClient() {
  WSADATA data{};
  winsock_ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

// 析构函数：断开连接并清理 WinSock
ProtocolClient::~ProtocolClient() {
  disconnect();
  if (winsock_ready_) {
    WSACleanup();
  }
}

// 发起非阻塞 TCP 连接
// 成功分为两种情况：
//   1. 立即成功（如同机回环连接）
//   2. 进入 connecting 状态（等待 select 指示完成）
// 两种情况下均返回 true
bool ProtocolClient::connect(const std::string& host, std::uint16_t port) {
#ifdef MIR2_CLIENT_TESTING
  if (test_mode_) {
    disconnect();
    connect_attempts_.push_back(ConnectAttempt{host, port});
    state_ = State::connecting;
    return true;
  }
#endif

  disconnect();  // 断开已有的连接，清理状态

  if (!winsock_ready_) {
    return false;
  }

  // 创建 TCP socket
  socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_ == INVALID_SOCKET) {
    return false;
  }

  // 设置为非阻塞模式：FIONBIO = 1 使 socket 进入非阻塞模式
  // 此后 connect() 会立即返回，连接在后台进行
  u_long nonblocking = 1;
  ioctlsocket(socket_, FIONBIO, &nonblocking);

  // 解析地址并发起连接
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = inet_addr(host.c_str());
  if (address.sin_addr.s_addr == INADDR_NONE) {
    closesocket(socket_);
    socket_ = INVALID_SOCKET;
    return false;
  }

  const auto result = ::connect(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  // 立即成功：如同机回环连接（127.0.0.1），非阻塞模式下也可能立即完成
  if (result == 0) {
    state_ = State::connected;
    events_.push_back(ConnectedEvent{});
    return true;
  }

  // 非阻塞 connect 返回 WSAEWOULDBLOCK 是预期行为
  // 表示连接正在建立中，后续通过 select 检测 write_set 来判断连接是否完成
  const auto error = WSAGetLastError();
  if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINVAL) {
    state_ = State::connecting;
    return true;
  }

  // 其他错误（如网络不可达、目标拒绝连接等）视为连接失败
  closesocket(socket_);
  socket_ = INVALID_SOCKET;
  return false;
}

// 断开连接：关闭 socket，清空所有缓冲区，压入断开事件
void ProtocolClient::disconnect(const std::string& reason) {
  if (socket_ != INVALID_SOCKET) {
    closesocket(socket_);
    socket_ = INVALID_SOCKET;
  }
  if (state_ != State::disconnected) {
    push_disconnect(reason);
  }
  state_ = State::disconnected;
  inbound_buffer_.clear();
  outbound_frames_.clear();
  current_write_offset_ = 0;
}

// 非阻塞轮询：使用 select(零超时) 检测 socket 状态
// 处理顺序：异步连接完成 -> 接收数据 -> 发送数据
// 必须在主循环中每帧调用，以实现低延迟网络通信
void ProtocolClient::poll() {
#ifdef MIR2_CLIENT_TESTING
  if (test_mode_) {
    return;
  }
#endif

  if (socket_ == INVALID_SOCKET) {
    return;
  }

  // 构建 select 的读/写 fd_set
  fd_set read_set{};
  fd_set write_set{};
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  FD_SET(socket_, &read_set);
  FD_SET(socket_, &write_set);

  // 零超时：select 立即返回，不阻塞主循环
  timeval timeout{};
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

  const auto ready = select(0, &read_set, &write_set, nullptr, &timeout);
  if (ready == SOCKET_ERROR) {
    disconnect("socket_select_failed");
    return;
  }

  // 检查异步连接是否完成：
  // 非阻塞 connect 完成后，socket 会在 write_set 中可写
  if (state_ == State::connecting && FD_ISSET(socket_, &write_set)) {
    if (!finish_connect()) {
      disconnect("connect_failed");
      return;
    }
  }

  // 处理可读事件：接收数据并解码帧
  if (state_ == State::connected && FD_ISSET(socket_, &read_set)) {
    handle_readable();
  }

  // 处理可写事件：发送队列中待发送的数据
  if (state_ == State::connected && FD_ISSET(socket_, &write_set) && !outbound_frames_.empty()) {
    handle_writable();
  }
}

// 取出所有待处理事件（使用移动语义操作内部队列）
// 主循环每帧调用一次，获取自上一帧以来所有网络事件
std::vector<ProtocolEvent> ProtocolClient::drain_events() {
  auto events = std::move(events_);
  events_.clear();
  return events;
}

// 完成异步连接：通过 getsockopt SO_ERROR 检查连接是否成功
// 非阻塞 connect 完成后，SO_ERROR 为 0 表示连接成功
bool ProtocolClient::finish_connect() {
  int error = 0;
  int error_size = sizeof(error);
  if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &error_size) == SOCKET_ERROR ||
      error != 0) {
    return false;
  }
  state_ = State::connected;
  events_.push_back(ConnectedEvent{});
  return true;
}

// 处理 socket 可读事件：循环 recv 直到 EWOULDBLOCK
// 接收到的数据先存入 inbound_buffer_（累积缓冲区），
// 然后尝试从中解码完整的帧。一个 recv 可能包含多个帧，
// 也可能只是一个帧的一部分。
void ProtocolClient::handle_readable() {
  std::array<std::uint8_t, 8192> buffer{};
  while (true) {
    const auto received = recv(socket_, reinterpret_cast<char*>(buffer.data()),
                               static_cast<int>(buffer.size()), 0);
    if (received > 0) {
      // 将接收数据追加到累积缓冲区尾部
      inbound_buffer_.insert(inbound_buffer_.end(), buffer.begin(),
                             buffer.begin() + static_cast<std::ptrdiff_t>(received));
      // 尝试从缓冲区解码出所有完整的帧
      // drain_frames 会消费缓冲区中完整的帧，不完整的留在缓冲区中
      const auto frames = client_v1::drain_frames(inbound_buffer_);
      for (const auto& frame : frames) {
        events_.push_back(ProtocolFrameEvent{frame});
      }
      continue;  // 继续读取，直到数据读完
    }
    if (received == 0) {
      disconnect("remote_closed");  // 对端关闭连接（优雅关闭）
      return;
    }
    const auto error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK) {
      break;  // 数据已全部读完（非阻塞模式下此错误表示无更多数据）
    }
    disconnect("socket_read_failed");
    return;
  }
}

// 处理 socket 可写事件：循环发送队列中的帧直到全部发完或阻塞
// 发送队列是 std::deque，支持从头部逐帧发送
// 一帧可能分多次发送（部分发送），用 current_write_offset_ 追踪进度
void ProtocolClient::handle_writable() {
  while (!outbound_frames_.empty()) {
    auto& frame = outbound_frames_.front();
    const auto* data = frame.data() + current_write_offset_;
    const auto bytes_remaining = static_cast<int>(frame.size() - current_write_offset_);
    const auto sent = ::send(socket_, reinterpret_cast<const char*>(data), bytes_remaining, 0);
    if (sent > 0) {
      current_write_offset_ += static_cast<std::size_t>(sent);
      if (current_write_offset_ >= frame.size()) {
        outbound_frames_.pop_front();  // 当前帧已全部发送完毕
        current_write_offset_ = 0;
        continue;
      }
      break;  // 当前帧只发送了部分，等待下次可写事件继续发送
    }
    const auto error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK) {
      break;  // 发送缓冲区满，等待下次轮询
    }
    disconnect("socket_write_failed");
    return;
  }
}

// 压入断开事件（断开原因的右值语义）
void ProtocolClient::push_disconnect(std::string reason) {
  events_.push_back(DisconnectedEvent{std::move(reason)});
}

#ifdef MIR2_CLIENT_TESTING
void ProtocolClient::enable_test_mode_for_test() {
  disconnect();
  test_mode_ = true;
  connect_attempts_.clear();
  sent_frames_.clear();
  events_.clear();
}

void ProtocolClient::complete_connect_for_test() {
  if (!test_mode_ || state_ != State::connecting) {
    return;
  }
  state_ = State::connected;
  events_.push_back(ConnectedEvent{});
}

void ProtocolClient::push_disconnected_for_test(std::string reason) {
  if (!test_mode_) {
    return;
  }
  state_ = State::disconnected;
  events_.push_back(DisconnectedEvent{std::move(reason)});
}

std::vector<client_v1::Frame> ProtocolClient::drain_sent_frames_for_test() {
  auto frames = std::move(sent_frames_);
  sent_frames_.clear();
  return frames;
}
#endif

}  // namespace mir2::client
