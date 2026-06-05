/**
 * @file session_router.hpp
 * @brief 会话路由/分发器
 *
 * @details 定义 SessionRouter 类，负责根据网关名称将入站连接
 * 路由到对应的后端服务。这是网关服务的核心路由组件，
 * 用于解耦网络层与业务服务层。
 *
 * 当前路由规则：
 * - "login_gateway" -> "auth_service"（认证服务）
 * - "game_gateway" -> "world_service"（世界服务）
 * - 其他 -> 空字符串（无法路由）
 */

#pragma once

#include <string>

namespace mir2 {

/**
 * @class SessionRouter
 * @brief 会话路由分发器
 *
 * @details 根据入站连接的网关名称，决定将其转发到哪个后端服务。
 * 路由规则可以通过扩展 route_ingress() 的逻辑来自定义。
 * 目前支持登录网关和游戏网关两种入站连接。
 */
class SessionRouter {
 public:
  /**
   * @brief 根据网关名称路由入站连接到对应的后端服务
   * @param gateway_name 网关名称标识
   * @return std::string 目标服务名称，空字符串表示无法路由
   */
  [[nodiscard]] std::string route_ingress(const std::string& gateway_name) const;
};

}  // namespace mir2
