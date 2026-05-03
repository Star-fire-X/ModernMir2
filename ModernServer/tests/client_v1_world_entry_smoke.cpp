#include <cassert>
#include <vector>

#include "shared/protocol/client_v1/protocol.hpp"

namespace {

template <typename T>
T round_trip(const T& message, std::uint32_t sequence) {
  using namespace mir2::client_v1;
  auto bytes = encode_frame(make_frame(message, sequence));
  std::vector<std::uint8_t> buffer = bytes;
  auto frames = drain_frames(buffer);
  assert(frames.size() == 1);
  assert(frames.front().sequence == sequence);
  auto decoded = decode_message<T>(frames.front());
  assert(decoded.has_value());
  return *decoded;
}

}  // namespace

int main() {
  using namespace mir2::client_v1;

  const auto enter = round_trip(EnterWorldRequest{"world-token", 1, 1}, 1);
  assert(enter.token == "world-token");

  const auto notice = round_trip(LoginNotice{"Welcome", "Choose OK to enter."}, 2);
  assert(notice.title == "Welcome");

  static_cast<void>(round_trip(LoginNoticeOk{}, 3));

  const auto result = round_trip(EnterWorldResult{true, 1000, "Hero", "0", 330, 270, ""}, 4);
  assert(result.success);
  assert(result.self_actor_id == 1000);

  WorldSnapshot snapshot;
  snapshot.map_id = "0";
  snapshot.width = 700;
  snapshot.height = 700;
  snapshot.self_actor_id = 1000;
  snapshot.actors.push_back(WorldActor{1000, "Hero", 330, 270, 0, 0, 0, ActorType::player});
  const auto decoded_snapshot = round_trip(snapshot, 5);
  assert(decoded_snapshot.actors.size() == 1);
  assert(decoded_snapshot.actors.front().name == "Hero");
  return 0;
}
