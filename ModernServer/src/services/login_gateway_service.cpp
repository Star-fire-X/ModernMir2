#include "services/login_gateway_service.hpp"

namespace mir2 {

LoginGatewayService::LoginGatewayService() : GatewayServiceBase("login_gateway") {}

PortBinding LoginGatewayService::binding(const HostContext& context) const {
  return context.config.ports.login_gateway;
}

std::string LoginGatewayService::ingress_target() const { return "auth_service"; }

}  // namespace mir2
