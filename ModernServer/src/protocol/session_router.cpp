#include "protocol/session_router.hpp"

namespace mir2 {

std::string SessionRouter::route_ingress(const std::string& gateway_name) const {
  if (gateway_name == "login_gateway") {
    return "auth_service";
  }
  if (gateway_name == "game_gateway") {
    return "world_service";
  }
  return {};
}

}  // namespace mir2
