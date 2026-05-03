#pragma once

#include "services/gateway_service_base.hpp"

namespace mir2 {

class LoginGatewayService : public GatewayServiceBase {
 public:
  LoginGatewayService();

 protected:
  PortBinding binding(const HostContext& context) const override;
  std::string ingress_target() const override;
};

}  // namespace mir2
