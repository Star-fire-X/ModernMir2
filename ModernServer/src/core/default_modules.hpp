/**
 * @file default_modules.hpp
 * @brief 默认模块注册函数声明
 *
 * @details 该文件声明了 register_default_modules() 函数，该函数用于向 HostRuntime
 *          注册服务器运行时所需的所有默认服务模块。这些模块构成了游戏服务器的核心功能，
 *          包括日志记录、持久化存储、认证授权、世界管理以及网关服务等。
 *
 * 设计说明：
 * - 通过统一的注册入口简化 HostRuntime 的初始化流程
 * - 根据运行配置有条件地注册不同的网关实现
 * - 支持新旧两种网关协议的共存与切换
 */

#pragma once

#include "core/host_runtime.hpp"

namespace mir2 {

/**
 * @brief 注册所有默认服务模块到运行时环境
 * @param runtime HostRuntime 实例的引用，模块将被注册到此运行时中
 *
 * @details 该函数按依赖顺序依次注册以下服务模块：
 *          1. LogService（日志服务）- 最先注册，供其他模块使用
 *          2. PersistenceService（持久化服务）- 数据库访问基础服务
 *          3. AuthService（认证服务）- 用户身份验证
 *          4. WorldService（世界服务）- 游戏世界逻辑管理
 *
 *          根据运行时配置，还会条件性地注册：
 *          5. LoginGatewayService / GameGatewayService - 旧版网关协议服务
 *          6. ClientV1LoginGatewayService / ClientV1GameGatewayService - 新版客户端网关服务
 *
 * @note 注册顺序对模块的生命周期管理有影响。停止时按注册顺序的逆序执行，
 *       但 PersistenceService 具有特殊处理（最后停止），确保其他模块停止时
 *       持久化服务仍然可用。
 *
 * @see HostRuntime::register_module()
 * @see HostRuntime::stop_all()
 */
void register_default_modules(HostRuntime& runtime);

}  // namespace mir2
