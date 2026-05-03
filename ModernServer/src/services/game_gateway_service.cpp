#include "services/game_gateway_service.hpp"

namespace mir2 {

GameGatewayService::GameGatewayService() : GatewayServiceBase("game_gateway") {}

PortBinding GameGatewayService::binding(const HostContext& context) const {
  return context.config.ports.game_gateway;
}

std::string GameGatewayService::ingress_target() const { return "world_service"; }

}  // namespace mir2
