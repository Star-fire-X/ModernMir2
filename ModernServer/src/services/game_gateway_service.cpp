/**
 * @file game_gateway_service.cpp
 * @brief 遗留游戏网关服务实现
 *
 * @details 实现 GameGatewayService 类，配置游戏网关的端口绑定和
 *          消息路由目标。所有游戏逻辑的协议解析和处理委托给 WorldService。
 */

#include "services/game_gateway_service.hpp"

namespace mir2 {

GameGatewayService::GameGatewayService() : GatewayServiceBase("game_gateway") {}

PortBinding GameGatewayService::binding(const HostContext& context) const {
  return context.config.ports.game_gateway;
}

std::string GameGatewayService::ingress_target() const { return "world_service"; }

}  // namespace mir2
