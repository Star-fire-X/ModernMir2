#pragma once

namespace mir2 {

enum class CanonicalLoginStage {
  connected,
  authenticated,
  server_selected,
  character_selected,
  entering_game,
  in_game,
  disconnected
};

enum class CanonicalLoginRequest {
  authenticate,
  select_server,
  query_characters,
  create_character,
  delete_character,
  select_character,
  enter_world,
  finish_enter_game,
  gameplay
};

enum class CanonicalLoginTransition {
  authenticate,
  select_server,
  select_character,
  enter_game,
  enter_game_complete,
  disconnect
};

[[nodiscard]] bool can_accept(CanonicalLoginStage stage, CanonicalLoginRequest request);
[[nodiscard]] CanonicalLoginStage advance(CanonicalLoginStage stage,
                                          CanonicalLoginTransition transition);
[[nodiscard]] const char* stage_name(CanonicalLoginStage stage);

}  // namespace mir2
