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
    std::error_code error;
    peer_address_ = socket_.remote_endpoint(error).address().to_string();
    if (error) {
      peer_address_ = "unknown";
    }
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
    outbound_frames_.push_back(client_v1::encode_frame(client_v1::encode_any(message, next_sequence_++)));
    if (!writing_) {
      do_write();
    }
  });
}

void ClientV1Session::send(const client_v1::Message& message, std::chrono::milliseconds delay) {
  if (delay.count() <= 0) {
    send(message);
    return;
  }

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

                            inbound_buffer_.insert(
                                inbound_buffer_.end(), read_buffer_.begin(),
                                read_buffer_.begin() +
                                    static_cast<std::ptrdiff_t>(bytes_transferred));
                            const auto frames = client_v1::drain_frames(inbound_buffer_);
                            if (frames.empty() && !inbound_buffer_.empty() &&
                                inbound_buffer_.size() > (1U << 20U)) {
                              queue_disconnect_and_close(413, "frame_too_large");
                              return;
                            }

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
        outbound_frames_.pop_front();
        do_write();
      }));
}

void ClientV1Session::queue_disconnect_and_close(std::uint16_t code, std::string reason) {
  if (closed_ || close_after_write_) {
    return;
  }
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

  std::error_code ignored;
  close_timer_.cancel(ignored);
  socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
  socket_.close(ignored);
  owner_.on_client_v1_disconnected(session_id_, peer_address_,
                                   reason.empty() ? "closed" : reason);
}

}  // namespace mir2
