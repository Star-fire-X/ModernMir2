/**
 * @file login_gateway_service.hpp
 * @brief 遗留登录网关服务头文件
 *
 * @details 定义 LoginGatewayService 类，继承自 GatewayServiceBase。
 *          负责处理传统遗留协议客户端的登录网关连接，监听配置中指定的
 *          登录网关端口，将收到的数据包路由到 AuthService 进行处理。
 *
 * @see GatewayServiceBase
 * @see AuthService
 */

#pragma once

#include "services/gateway_service_base.hpp"

namespace mir2 {

/**
 * @class LoginGatewayService
 * @brief 遗留登录网关服务
 *
 * @details 作为传统客户端连接到服务器的第一个网关，负责接收登录认证
 *          相关的数据包(如密码认证、账号注册、角色管理等)，并将这些
 *          请求转发到 AuthService 进行处理。其 ingress_target 为
 *          "auth_service"。
 */
class LoginGatewayService : public GatewayServiceBase {
 public:
  LoginGatewayService();

 protected:
  /**
   * @brief 获取登录网关端口绑定配置
   * @param context 宿主上下文
   * @return 登录网关的端口绑定配置
   */
  PortBinding binding(const HostContext& context) const override;

  /**
   * @brief 获取消息路由目标
   * @return "auth_service" — 登录网关的数据包路由到认证服务
   */
  std::string ingress_target() const override;
};

}  // namespace mir2
