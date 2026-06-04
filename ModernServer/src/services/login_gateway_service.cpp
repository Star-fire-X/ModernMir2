/**
 * @file login_gateway_service.cpp
 * @brief 遗留登录网关服务实现
 *
 * @details 实现 LoginGatewayService 类，配置登录网关的端口绑定和
 *          消息路由目标。这是一个轻量的实现，所有协议解析和业务逻辑
 *          委托给 AuthService 处理。
 */

#include "services/login_gateway_service.hpp"

namespace mir2 {

LoginGatewayService::LoginGatewayService() : GatewayServiceBase("login_gateway") {}

PortBinding LoginGatewayService::binding(const HostContext& context) const {
  return context.config.ports.login_gateway;
}

std::string LoginGatewayService::ingress_target() const { return "auth_service"; }

}  // namespace mir2
