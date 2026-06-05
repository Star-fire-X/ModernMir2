/**
 * @file game_session.cpp
 * @brief 游戏世界会话实现
 *
 * @details 本文件实现了 GameSession 类的所有方法。
 * 负责处理旧版 Legacy 协议客户端的 TCP 连接、数据包收发和生命周期管理。
 *
 * 核心流程：
 * 1. start() 初始化会话，记录客户端地址，通知 GatewayServiceBase，开始 do_read()
 * 2. do_read() 异步读取 TCP 字节流，通过 LegacyProtocolCodec::drain_packets()
 *    提取完整的 Legacy 帧，每帧调用 owner_.forward_packet() 转发给业务逻辑层
 * 3. do_write() 从 outbound_frames_ 队列取编码后的帧数据异步发送
 * 4. deliver() 系列方法将 LegacyPacket 编码后加入发送队列
 * 5. close() 清理资源并通知 owner
 *
 * @note 支持 pause_for() 功能（在场景切换等场景暂停读取），
 *       以及 deliver_and_close() 功能（发送最后一条消息后断开）。
 * @see GameSession
 * @see LegacyProtocolCodec
 * @see GatewayServiceBase
 */

#include "protocol/game_session.hpp"

#include "protocol/legacy_protocol.hpp"
#include "services/gateway_service_base.hpp"

namespace mir2 {

GameSession::GameSession(asio::ip::tcp::socket socket, GatewayServiceBase& owner)
    : socket_(std::move(socket)),
      owner_(owner),
      pause_timer_(socket_.get_executor()),
      close_timer_(socket_.get_executor()) {}

void GameSession::start(std::uint64_t session_id) {
  session_id_ = session_id;
  std::error_code ignored;
  const auto remote = socket_.remote_endpoint(ignored);
  if (!ignored) {
    peer_address_ = remote.address().to_string() + ":" + std::to_string(remote.port());
  }
  // 通知网关服务新连接建立
  owner_.notify_connected(session_id_, peer_address_);
  do_read();
}

void GameSession::deliver(const LegacyPacket& packet) {
  auto self = shared_from_this();
  asio::dispatch(socket_.get_executor(), [this, self, packet] {
    if (closed_) {
      return;
    }
    // 使用 LegacyProtocolCodec 编码数据包为 #...#! 帧格式
    outbound_frames_.push_back(LegacyProtocolCodec::encode(packet));
    if (!write_in_progress_) {
      do_write();
    }
  });
}

void GameSession::deliver(const LegacyPacket& packet, std::chrono::milliseconds delay) {
  if (delay.count() <= 0) {
    deliver(packet);
    return;
  }

  // 使用定时器实现延迟发送
  auto self = shared_from_this();
  asio::dispatch(socket_.get_executor(), [this, self, packet, delay] {
    if (closed_) {
      return;
    }
    auto timer = std::make_shared<asio::steady_timer>(socket_.get_executor());
    timer->expires_after(delay);
    timer->async_wait([this, self, packet, timer](const std::error_code& error) {
      if (error || closed_) {
        return;
      }
      outbound_frames_.push_back(LegacyProtocolCodec::encode(packet));
      if (!write_in_progress_) {
        do_write();
      }
    });
  });
}

void GameSession::deliver_and_close(const LegacyPacket& packet, std::chrono::milliseconds delay,
                                    std::string reason) {
  auto self = shared_from_this();
  asio::dispatch(socket_.get_executor(),
                 [this, self, packet, delay, reason = std::move(reason)]() mutable {
                   if (closed_) {
                     return;
                   }
                   // 标记发送完成后关闭，保存延迟时间和原因
                   close_after_flush_ = true;
                   close_delay_ = delay;
                   close_reason_ =
                       reason.empty() ? std::string("forced_disconnect") : std::move(reason);
                   outbound_frames_.push_back(LegacyProtocolCodec::encode(packet));
                   if (!write_in_progress_) {
                     do_write();
                   }
                 });
}

void GameSession::close(const std::string& reason) {
  auto self = shared_from_this();
  asio::dispatch(socket_.get_executor(), [this, self, reason] {
    if (closed_) {
      return;
    }
    closed_ = true;
    paused_ = false;
    close_after_flush_ = false;
    // 取消所有定时器
    std::error_code ignored;
    pause_timer_.cancel(ignored);
    close_timer_.cancel(ignored);
    // 关闭套接字（双向关闭）
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
    // 通知网关服务
    owner_.remove_session(session_id_);
    owner_.notify_disconnected(session_id_, peer_address_, reason);
  });
}

void GameSession::pause_for(std::chrono::milliseconds delay) {
  auto self = shared_from_this();
  asio::dispatch(socket_.get_executor(), [this, self, delay] {
    if (closed_) {
      return;
    }
    // 设置暂停标志，暂停 do_read 循环
    paused_ = true;
    pause_timer_.expires_after(delay);
    pause_timer_.async_wait([this, self](const std::error_code& error) {
      if (error || closed_) {
        return;
      }
      // 暂停结束，恢复读取
      paused_ = false;
      do_read();
    });
  });
}

bool GameSession::note_backpressure(std::size_t disconnect_threshold) {
  ++overload_strikes_;
  return overload_strikes_ > disconnect_threshold;
}

void GameSession::do_read() {
  if (closed_ || paused_) {
    return;
  }

  auto self = shared_from_this();
  socket_.async_read_some(asio::buffer(read_buffer_), [this, self](const std::error_code& error,
                                                                   std::size_t bytes_read) {
    if (error) {
      close(error.message());
      return;
    }

    // 将读取的数据追加到入站缓冲区
    inbound_buffer_.insert(inbound_buffer_.end(), read_buffer_.begin(),
                           read_buffer_.begin() + static_cast<std::ptrdiff_t>(bytes_read));

    try {
      // 从缓冲区中提取完整的 Legacy 帧并逐个转发
      for (auto& packet : LegacyProtocolCodec::drain_packets(inbound_buffer_)) {
        owner_.forward_packet(session_id_, peer_address_, packet, self);
      }
    } catch (const std::exception& ex) {
      close(ex.what());
      return;
    }

    if (!paused_) {
      do_read();
    }
  });
}

void GameSession::do_write() {
  if (closed_ || outbound_frames_.empty()) {
    write_in_progress_ = false;
    if (!closed_ && close_after_flush_) {
      schedule_close();
    }
    return;
  }

  write_in_progress_ = true;
  auto self = shared_from_this();
  asio::async_write(socket_, asio::buffer(outbound_frames_.front()),
                    [this, self](const std::error_code& error, std::size_t /*bytes_written*/) {
                      if (error) {
                        close(error.message());
                        return;
                      }
                      // 发送完当前帧，继续发送下一帧
                      outbound_frames_.pop_front();
                      if (!outbound_frames_.empty()) {
                        do_write();
                      } else {
                        write_in_progress_ = false;
                        if (close_after_flush_) {
                          schedule_close();
                        }
                      }
                    });
}

void GameSession::schedule_close() {
  if (closed_) {
    return;
  }
  close_after_flush_ = false;
  auto self = shared_from_this();
  close_timer_.expires_after(close_delay_);
  close_timer_.async_wait([this, self](const std::error_code& error) {
    if (error || closed_) {
      return;
    }
    close(close_reason_);
  });
}

}  // namespace mir2
