/**
 * @file game_gateway_service.hpp
 * @brief 遗留游戏网关服务头文件
 *
 * @details 定义 GameGatewayService 类，继承自 GatewayServiceBase。
 *          负责处理传统遗留协议客户端的游戏网关连接，监听配置中指定的
 *          游戏网关端口，将收到的数据包路由到 WorldService 进行处理。
 *
 * @see GatewayServiceBase
 * @see WorldService
 */

#pragma once

#include "services/gateway_service_base.hpp"

namespace mir2 {

/**
 * @class GameGatewayService
 * @brief 遗留游戏网关服务
 *
 * @details 作为传统客户端进入游戏世界后的网关，负责转发游戏内的
 *          移动、攻击、对话、交易等操作数据包到 WorldService 处理。
 *          其 ingress_target 为 "world_service"。
 */
class GameGatewayService : public GatewayServiceBase {
 public:
  GameGatewayService();

 protected:
  /**
   * @brief 获取游戏网关端口绑定配置
   * @param context 宿主上下文
   * @return 游戏网关的端口绑定配置
   */
  PortBinding binding(const HostContext& context) const override;

  /**
   * @brief 获取消息路由目标
   * @return "world_service" — 游戏网关的数据包路由到世界服务
   */
  std::string ingress_target() const override;
};

}  // namespace mir2
