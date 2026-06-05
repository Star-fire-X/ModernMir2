/**
 * @file session_router.cpp
 * @brief 会话路由分发器实现
 *
 * @details 实现 SessionRouter::route_ingress() 方法。
 * 根据当前的网关名称到后端服务的映射规则进行路由。
 * 这是一个简单的静态路由表，未来可扩展为动态配置。
 *
 * 当前路由映射：
 * - login_gateway -> auth_service：登录网关的流量路由到认证服务
 * - game_gateway -> world_service：游戏网关的流量路由到世界服务
 *
 * @return 空字符串表示无法找到对应的后端服务，调用者应处理此情况。
 */

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
