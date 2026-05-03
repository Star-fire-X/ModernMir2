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
  owner_.notify_connected(session_id_, peer_address_);
  do_read();
}

void GameSession::deliver(const LegacyPacket& packet) {
  auto self = shared_from_this();
  asio::dispatch(socket_.get_executor(), [this, self, packet] {
    if (closed_) {
      return;
    }
    outbound_frames_.push_back(LegacyProtocolCodec::encode(packet));
    if (!write_in_progress_) {
      do_write();
    }
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
    std::error_code ignored;
    pause_timer_.cancel(ignored);
    close_timer_.cancel(ignored);
    socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
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
    paused_ = true;
    pause_timer_.expires_after(delay);
    pause_timer_.async_wait([this, self](const std::error_code& error) {
      if (error || closed_) {
        return;
      }
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

    inbound_buffer_.insert(inbound_buffer_.end(), read_buffer_.begin(),
                           read_buffer_.begin() + static_cast<std::ptrdiff_t>(bytes_read));

    try {
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
