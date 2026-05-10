#include <iostream>
#include <string_view>

#include "protocol/canonical_login_state.hpp"

namespace {

int fail(std::string_view stage) {
  std::cerr << "canonical_login_state_smoke failed at " << stage << '\n';
  return 1;
}

}  // namespace

int main() {
  using mir2::CanonicalLoginRequest;
  using mir2::CanonicalLoginStage;
  using mir2::CanonicalLoginTransition;

  auto stage = CanonicalLoginStage::connected;
  if (!mir2::can_accept(stage, CanonicalLoginRequest::authenticate) ||
      mir2::can_accept(stage, CanonicalLoginRequest::select_server) ||
      mir2::can_accept(stage, CanonicalLoginRequest::enter_world)) {
    return fail("connected gates");
  }

  stage = mir2::advance(stage, CanonicalLoginTransition::authenticate);
  if (stage != CanonicalLoginStage::authenticated ||
      !mir2::can_accept(stage, CanonicalLoginRequest::select_server) ||
      mir2::can_accept(stage, CanonicalLoginRequest::query_characters)) {
    return fail("authenticated gates");
  }

  stage = mir2::advance(stage, CanonicalLoginTransition::select_server);
  if (stage != CanonicalLoginStage::server_selected ||
      !mir2::can_accept(stage, CanonicalLoginRequest::query_characters) ||
      !mir2::can_accept(stage, CanonicalLoginRequest::create_character) ||
      !mir2::can_accept(stage, CanonicalLoginRequest::delete_character) ||
      !mir2::can_accept(stage, CanonicalLoginRequest::select_character) ||
      mir2::can_accept(stage, CanonicalLoginRequest::select_server) ||
      mir2::can_accept(stage, CanonicalLoginRequest::enter_world)) {
    return fail("server selected gates");
  }

  stage = mir2::advance(stage, CanonicalLoginTransition::select_character);
  if (stage != CanonicalLoginStage::character_selected ||
      !mir2::can_accept(stage, CanonicalLoginRequest::enter_world) ||
      mir2::can_accept(stage, CanonicalLoginRequest::gameplay)) {
    return fail("character selected gates");
  }

  stage = mir2::advance(stage, CanonicalLoginTransition::enter_game);
  if (stage != CanonicalLoginStage::entering_game ||
      !mir2::can_accept(stage, CanonicalLoginRequest::finish_enter_game) ||
      mir2::can_accept(stage, CanonicalLoginRequest::enter_world) ||
      mir2::can_accept(stage, CanonicalLoginRequest::gameplay)) {
    return fail("entering game gates");
  }

  stage = mir2::advance(stage, CanonicalLoginTransition::enter_game_complete);
  if (stage != CanonicalLoginStage::in_game ||
      !mir2::can_accept(stage, CanonicalLoginRequest::gameplay) ||
      mir2::can_accept(stage, CanonicalLoginRequest::finish_enter_game)) {
    return fail("in game gates");
  }

  stage = mir2::advance(stage, CanonicalLoginTransition::disconnect);
  if (stage != CanonicalLoginStage::disconnected ||
      mir2::can_accept(stage, CanonicalLoginRequest::authenticate) ||
      mir2::advance(stage, CanonicalLoginTransition::authenticate) !=
          CanonicalLoginStage::disconnected ||
      std::string_view{mir2::stage_name(stage)} != "Disconnected") {
    return fail("disconnected gates");
  }

  return 0;
}
