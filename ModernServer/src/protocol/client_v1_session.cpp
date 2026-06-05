/**
 * @file client_v1_session.cpp
 * @brief Client v1 网络会话管理实现
 *
 * @details 本文件实现了 ClientV1Session 的所有方法。
 * 会话管理使用 asio::strand 确保线程安全，所有操作（包括读写、关闭）
 * 都在 strand 内部串行化执行。
 *
 * 核心流程：
 * 1. start() 初始化会话，通知拥有者连接建立，开始读循环
 * 2. do_read() 异步读取 TCP 数据，累积到 inbound_buffer_，
 *    通过 client_v1::drain_frames() 提取完整帧，
 *    每帧解码后通知拥有者
 * 3. do_write() 从 outbound_frames_ 队列取帧异步发送，发送完递归处理下一帧
 * 4. 支持延迟发送和优雅断线（发送断线帧后等待排空再关闭）
 *
 * @warning 所有对外公开方法都通过 asio::dispatch 将操作调度到 strand 中执行，
 *          确保线程安全。
 */

#include "protocol/client_v1_session.hpp"

namespace mir2 {

ClientV1Session::ClientV1Session(asio::ip::tcp::socket socket, ClientV1SessionOwner& owner)
    : socket_(std::move(socket)),
      strand_(asio::make_strand(socket_.get_executor())),
      owner_(owner),
      close_timer_(strand_) {}

void ClientV1Session::start(std::uint64_t session_id) {
  auto self = shared_from_this();
  asio::dispatch(strand_, [this, self, session_id] {
    if (closed_) {
      return;
    }
    session_id_ = session_id;
    // 获取客户端地址
    std::error_code error;
    peer_address_ = socket_.remote_endpoint(error).address().to_string();
    if (error) {
      peer_address_ = "unknown";
    }
    // 通知拥有者连接已建立
    owner_.on_client_v1_connected(session_id_, peer_address_);
    do_read();
  });
}

void ClientV1Session::send(const client_v1::Message& message) {
  auto self = shared_from_this();
  asio::dispatch(strand_, [this, self, message] {
    if (closed_) {
      return;
    }
    // 编码消息为帧，分配序列号，加入发送队列
    outbound_frames_.push_back(client_v1::encode_frame(client_v1::encode_any(message, next_sequence_++)));
    if (!writing_) {
      do_write();
    }
  });
}

void ClientV1Session::send_frame(const client_v1::Frame& frame) {
  send_frames(std::vector<client_v1::Frame>{frame});
}

void ClientV1Session::send_frames(const std::vector<client_v1::Frame>& frames) {
  auto self = shared_from_this();
  asio::dispatch(strand_, [this, self, frames] {
    if (closed_) {
      return;
    }
    for (auto frame : frames) {
      frame.sequence = next_sequence_++;
      outbound_frames_.push_back(client_v1::encode_frame(frame));
    }
    if (!writing_) {
      do_write();
    }
  });
}

void ClientV1Session::send_frames(const std::vector<client_v1::Frame>& frames,
                                  std::chrono::milliseconds delay) {
  // 延迟 <= 0 时立即发送
  if (delay.count() <= 0) {
    send_frames(frames);
    return;
  }

  // 使用定时器实现延迟发送
  auto self = shared_from_this();
  asio::dispatch(strand_, [this, self, frames, delay] {
    if (closed_) {
      return;
    }
    auto timer = std::make_shared<asio::steady_timer>(strand_);
    timer->expires_after(delay);
    timer->async_wait(asio::bind_executor(
        strand_, [this, self, frames, timer](const std::error_code& error) {
          if (error || closed_) {
            return;
          }
          for (auto frame : frames) {
            frame.sequence = next_sequence_++;
            outbound_frames_.push_back(client_v1::encode_frame(frame));
          }
          if (!writing_) {
            do_write();
          }
        }));
  });
}

void ClientV1Session::send(const client_v1::Message& message, std::chrono::milliseconds delay) {
  // 延迟 <= 0 时立即发送
  if (delay.count() <= 0) {
    send(message);
    return;
  }

  // 使用定时器实现延迟发送
  auto self = shared_from_this();
  asio::dispatch(strand_, [this, self, message, delay] {
    if (closed_) {
      return;
    }
    auto timer = std::make_shared<asio::steady_timer>(strand_);
    timer->expires_after(delay);
    timer->async_wait(asio::bind_executor(
        strand_, [this, self, message, timer](const std::error_code& error) {
          if (error || closed_) {
            return;
          }
          outbound_frames_.push_back(
              client_v1::encode_frame(client_v1::encode_any(message, next_sequence_++)));
          if (!writing_) {
            do_write();
          }
        }));
  });
}

void ClientV1Session::send_disconnect_and_close(std::uint16_t code, std::string reason) {
  auto self = shared_from_this();
  asio::dispatch(strand_, [this, self, code, reason = std::move(reason)]() mutable {
    queue_disconnect_and_close(code, std::move(reason));
  });
}

void ClientV1Session::close(std::string reason) {
  auto self = shared_from_this();
  asio::dispatch(strand_, [this, self, reason = std::move(reason)] {
    notify_closed(reason);
  });
}

void ClientV1Session::do_read() {
  auto self = shared_from_this();
  socket_.async_read_some(
      asio::buffer(read_buffer_),
      asio::bind_executor(strand_,
                          [this, self](const std::error_code& error,
                                       std::size_t bytes_transferred) {
                            if (error) {
                              notify_closed(error.message());
                              return;
                            }

                            // 将读取的数据追加到入站缓冲区
                            inbound_buffer_.insert(
                                inbound_buffer_.end(), read_buffer_.begin(),
                                read_buffer_.begin() +
                                    static_cast<std::ptrdiff_t>(bytes_transferred));

                            // 从累积缓冲区中提取完整帧
                            const auto frames = client_v1::drain_frames(inbound_buffer_);

                            // 安全检查：如果缓冲区已超过 1MB 但仍未提取出完整帧，
                            // 可能是恶意的超大帧攻击，断开连接
                            if (frames.empty() && !inbound_buffer_.empty() &&
                                inbound_buffer_.size() > (1U << 20U)) {
                              queue_disconnect_and_close(413, "frame_too_large");
                              return;
                            }

                            // 解码每帧并通知拥有者
                            for (const auto& frame : frames) {
                              const auto decoded = client_v1::decode_any(frame);
                              if (!decoded.has_value()) {
                                queue_disconnect_and_close(400, "protocol_decode_error");
                                return;
                              }
                              owner_.on_client_v1_message(session_id_, peer_address_,
                                                          frame.sequence, *decoded);
                            }
                            do_read();
                          }));
}

void ClientV1Session::do_write() {
  // 没有待发送数据时，检查是否需要执行排空后关闭
  if (outbound_frames_.empty()) {
    writing_ = false;
    if (close_after_write_) {
      notify_closed(close_reason_.empty() ? "closed" : close_reason_);
    }
    return;
  }

  writing_ = true;
  auto self = shared_from_this();
  asio::async_write(
      socket_, asio::buffer(outbound_frames_.front()),
      asio::bind_executor(strand_, [this, self](const std::error_code& error,
                                                std::size_t /*bytes_transferred*/) {
        if (error) {
          notify_closed(error.message());
          return;
        }
        // 发送完成后移除已发送的帧，继续处理下一帧
        outbound_frames_.pop_front();
        do_write();
      }));
}

void ClientV1Session::queue_disconnect_and_close(std::uint16_t code, std::string reason) {
  // 防止重复关闭
  if (closed_ || close_after_write_) {
    return;
  }
  // 构造断线原因帧并加入发送队列
  outbound_frames_.push_back(client_v1::encode_frame(
      client_v1::make_frame(client_v1::DisconnectReason{code, reason}, next_sequence_++)));
  close_after_write_ = true;
  close_reason_ = reason;
  if (!writing_) {
    do_write();
  }
}

void ClientV1Session::notify_closed(const std::string& reason) {
  if (closed_) {
    return;
  }
  closed_ = true;

  // 取消定时器，关闭套接字
  std::error_code ignored;
  close_timer_.cancel(ignored);
  socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
  socket_.close(ignored);
  // 通知拥有者连接已断开
  owner_.on_client_v1_disconnected(session_id_, peer_address_,
                                   reason.empty() ? "closed" : reason);
}

}  // namespace mir2
