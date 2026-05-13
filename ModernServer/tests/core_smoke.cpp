#include <iostream>
#include <string>
#include <vector>

#include "core/bounded_mpsc_queue.hpp"
#include "core/wheel_timer.hpp"
#include "protocol/legacy_edcode.hpp"
#include "protocol/legacy_game_codec.hpp"
#include "protocol/legacy_protocol.hpp"

int main() {
  mir2::BoundedMpscQueue<int> queue(2);
  if (!queue.try_push(1) || !queue.try_push(2) || queue.try_push(3)) {
    std::cerr << "queue\n";
    return 1;
  }
  const auto queue_value = queue.try_pop();
  if (!queue_value.has_value() || queue_value.value() != 1) {
    std::cerr << "queue_pop\n";
    return 1;
  }

  mir2::WheelTimer<int> timer(16);
  timer.schedule(10, 2, 42);
  if (!timer.pop_ready(11).empty()) {
    std::cerr << "timer_early\n";
    return 1;
  }
  const auto ready = timer.pop_ready(12);
  if (ready.size() != 1 || ready.front() != 42) {
    std::cerr << "timer_ready\n";
    return 1;
  }

  mir2::LegacyPacket packet;
  packet.header.ident = 1234;
  packet.body = {'O', 'K'};
  auto encoded = mir2::LegacyProtocolCodec::encode(packet);
  if (std::string(encoded.begin(), encoded.end()) != "#OK!") {
    std::cerr << "wire_encode\n";
    return 1;
  }
  auto decoded = mir2::LegacyProtocolCodec::drain_packets(encoded);
  if (decoded.size() != 1 ||
      std::string(decoded.front().body.begin(), decoded.front().body.end()) != "OK") {
    std::cerr << "wire_decode\n";
    return 1;
  }

  // Uplink from old client carries a check-code digit that LegacyProtocolCodec must strip.
  std::vector<std::uint8_t> client_frame{'#', '1', 'P', 'I', 'N', 'G', '!'};
  auto from_client = mir2::LegacyProtocolCodec::drain_packets(client_frame);
  if (from_client.size() != 1 ||
      std::string(from_client.front().body.begin(), from_client.front().body.end()) != "PING") {
    std::cerr << "client_frame\n";
    return 1;
  }

  // Downstream frames never carry a check-code; digits that are real data must be preserved.
  std::vector<std::uint8_t> downstream_frame{'#', 'D', 'A', 'T', 'A', '!'};
  auto from_server = mir2::LegacyProtocolCodec::drain_packets(downstream_frame);
  if (from_server.size() != 1 ||
      std::string(from_server.front().body.begin(), from_server.front().body.end()) != "DATA") {
    std::cerr << "downstream_frame\n";
    return 1;
  }

  std::vector<std::uint8_t> overlong_frame(70 * 1024, static_cast<std::uint8_t>('A'));
  overlong_frame.front() = static_cast<std::uint8_t>('#');
  const auto overlong_packets = mir2::LegacyProtocolCodec::drain_packets(overlong_frame);
  if (!overlong_packets.empty() || !overlong_frame.empty()) {
    std::cerr << "overlong_frame\n";
    return 1;
  }

  const auto message = mir2::make_default_message(mir2::kSmLogon, 1, 2, 3, 4);
  const auto encoded_message = mir2::legacy_encode_message(message);
  const auto decoded_message = mir2::legacy_decode_message(encoded_message);
  if (!decoded_message.has_value() || decoded_message->ident != mir2::kSmLogon ||
      decoded_message->recog != 1 || decoded_message->param != 2 || decoded_message->tag != 3 ||
      decoded_message->series != 4) {
    std::cerr << "message_roundtrip\n";
    return 1;
  }

  const auto game_packet =
      mir2::make_legacy_game_packet(7, 0, 0, mir2::make_default_message(mir2::kSmLogon, 1, 2, 3, 4),
                                    "BODY");
  const auto decoded_game = mir2::decode_legacy_game_packet(game_packet);
  if (!decoded_game.has_value() || decoded_game->message.ident != mir2::kSmLogon ||
      decoded_game->message.recog != 1 || decoded_game->body != "BODY") {
    std::cerr << "game_packet\n";
    return 1;
  }

  const auto raw_packet = mir2::make_legacy_raw_packet(7, "+GOOD/100");
  auto raw_encoded = mir2::LegacyProtocolCodec::encode(raw_packet);
  auto raw_decoded = mir2::LegacyProtocolCodec::drain_packets(raw_encoded);
  if (raw_decoded.size() != 1 ||
      std::string(raw_decoded.front().body.begin(), raw_decoded.front().body.end()) != "+GOOD/100") {
    std::cerr << "raw_packet\n";
    return 1;
  }

  // Full round-trip: game message → encode → frame → deframe → decode
  {
    const auto msg = mir2::make_default_message(mir2::kSmHear, 42, 0, 0, 1);
    const auto body = mir2::legacy_encode_string("Hello World");
    const auto pkt = mir2::make_legacy_game_packet(99, 1, 2, msg, body);
    if (pkt.header.socket_number != 99 || pkt.header.user_gate_index != 1 ||
        pkt.header.user_list_index != 2 || pkt.header.ident != mir2::kSmHear) {
      std::cerr << "roundtrip_packet_header\n";
      return 1;
    }
    const auto framed = mir2::LegacyProtocolCodec::encode(pkt);
    if (framed.size() < 2 || framed.front() != static_cast<std::uint8_t>('#') ||
        framed.back() != static_cast<std::uint8_t>('!')) {
      std::cerr << "roundtrip_framing\n";
      return 1;
    }
    std::vector<std::uint8_t> buffer = framed;
    const auto drained = mir2::LegacyProtocolCodec::drain_packets(buffer);
    if (!buffer.empty() || drained.size() != 1) {
      std::cerr << "roundtrip_drain\n";
      return 1;
    }
    const auto decoded = mir2::decode_legacy_game_packet(drained.front());
    if (!decoded.has_value() || decoded->message.ident != mir2::kSmHear ||
        decoded->message.recog != 42 || decoded->message.series != 1) {
      std::cerr << "roundtrip_message\n";
      return 1;
    }
    if (decoded->body != body) {
      std::cerr << "roundtrip_body\n";
      return 1;
    }
    const auto decoded_text = mir2::legacy_decode_string(decoded->body);
    if (decoded_text != "Hello World") {
      std::cerr << "roundtrip_text\n";
      return 1;
    }
  }

  return 0;
}
