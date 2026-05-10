#include "protocol/canonical_login_state.hpp"

namespace mir2 {

bool can_accept(CanonicalLoginStage stage, CanonicalLoginRequest request) {
  if (stage == CanonicalLoginStage::disconnected) {
    return false;
  }

  switch (request) {
    case CanonicalLoginRequest::authenticate:
      return stage == CanonicalLoginStage::connected ||
             stage == CanonicalLoginStage::authenticated;
    case CanonicalLoginRequest::select_server:
      return stage == CanonicalLoginStage::authenticated;
    case CanonicalLoginRequest::query_characters:
    case CanonicalLoginRequest::create_character:
    case CanonicalLoginRequest::delete_character:
    case CanonicalLoginRequest::select_character:
      return stage == CanonicalLoginStage::server_selected;
    case CanonicalLoginRequest::enter_world:
      return stage == CanonicalLoginStage::character_selected;
    case CanonicalLoginRequest::finish_enter_game:
      return stage == CanonicalLoginStage::entering_game;
    case CanonicalLoginRequest::gameplay:
      return stage == CanonicalLoginStage::in_game;
  }
  return false;
}

CanonicalLoginStage advance(CanonicalLoginStage stage, CanonicalLoginTransition transition) {
  if (stage == CanonicalLoginStage::disconnected) {
    return CanonicalLoginStage::disconnected;
  }

  switch (transition) {
    case CanonicalLoginTransition::authenticate:
      return CanonicalLoginStage::authenticated;
    case CanonicalLoginTransition::select_server:
      return CanonicalLoginStage::server_selected;
    case CanonicalLoginTransition::select_character:
      return CanonicalLoginStage::character_selected;
    case CanonicalLoginTransition::enter_game:
      return CanonicalLoginStage::entering_game;
    case CanonicalLoginTransition::enter_game_complete:
      return CanonicalLoginStage::in_game;
    case CanonicalLoginTransition::disconnect:
      return CanonicalLoginStage::disconnected;
  }
  return stage;
}

const char* stage_name(CanonicalLoginStage stage) {
  switch (stage) {
    case CanonicalLoginStage::connected:
      return "Connected";
    case CanonicalLoginStage::authenticated:
      return "Authenticated";
    case CanonicalLoginStage::server_selected:
      return "ServerSelected";
    case CanonicalLoginStage::character_selected:
      return "CharacterSelected";
    case CanonicalLoginStage::entering_game:
      return "EnteringGame";
    case CanonicalLoginStage::in_game:
      return "InGame";
    case CanonicalLoginStage::disconnected:
      return "Disconnected";
  }
  return "Unknown";
}

}  // namespace mir2
