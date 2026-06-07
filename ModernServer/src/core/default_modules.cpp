/**
 * @file default_modules.cpp
 * @brief 默认模块注册函数实现
 *
 * @details 该文件实现了 register_default_modules() 函数，负责创建并注册
 *          mir2 游戏服务器运行时的所有默认服务模块。根据运行时配置，
 *          可以启用或禁用特定类型的网关服务（旧版/新版客户端协议）。
 *
 * 设计要点：
 * - 日志服务最先注册，确保后续模块初始化时日志系统已就绪
 * - 持久化服务紧随其后，为其他模块提供数据存储能力
 * - 网关服务根据配置条件性注册，支持不同版本客户端的接入
 * - ClientV1 网关共享同一个 AdmissionRegistry 实例，用于协调客户端准入控制
 */

#include "core/default_modules.hpp"

#include <memory>

#include "services/auth_service.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_game_gateway_service.hpp"
#include "services/client_v1_login_gateway_service.hpp"
#include "services/game_gateway_service.hpp"
#include "services/log_service.hpp"
#include "services/login_gateway_service.hpp"
#include "services/persistence_service.hpp"
#include "services/world_service.hpp"

namespace mir2 {

/**
 * @brief 向运行时注册所有默认模块
 * @param runtime 目标 HostRuntime 实例的引用
 *
 * @details 注册顺序说明：
 *
 * 第一阶段 —— 核心服务（始终注册）：
 * 1. LogService          - 日志记录服务，提供统一的日志接口
 * 2. PersistenceService  - 数据持久化服务，负责数据库操作
 * 3. AuthService         - 用户认证服务，处理登录验证
 * 4. WorldService        - 世界逻辑服务，管理游戏世界状态
 *
 * 第二阶段 —— 旧版网关服务（根据 enable_legacy_gateways 配置）：
 * 5. LoginGatewayService  - 旧版登录网关，处理传统客户端登录连接
 * 6. GameGatewayService   - 旧版游戏网关，处理传统客户端游戏连接
 *
 * 第三阶段 —— 新版客户端网关服务（根据 enable_client_v1_gateways 配置）：
 * 7. ClientV1LoginGatewayService  - 新版登录网关
 * 8. ClientV1GameGatewayService   - 新版游戏网关
 *
 * ClientV1 网关共享同一个 ClientV1AdmissionRegistry 实例，用于：
 * - 记录已认证的客户端连接
 * - 防止未授权连接进入游戏世界
 * - 协调登录网关到游戏网关的会话转移
 *
 * @see HostRuntime::register_module()
 * @see HostConfig::runtime::enable_legacy_gateways
 * @see HostConfig::runtime::enable_client_v1_gateways
 */
void register_default_modules(HostRuntime& runtime) {
  // ---- 第一阶段：核心服务 ----
  runtime.register_module(std::make_unique<LogService>());
  runtime.register_module(std::make_unique<PersistenceService>());
  runtime.register_module(std::make_unique<AuthService>());
  runtime.register_module(std::make_unique<WorldService>());

  // ---- 第二阶段：旧版网关服务（按需启用） ----
  if (runtime.context().config.runtime.enable_legacy_gateways) {
    runtime.register_module(std::make_unique<LoginGatewayService>());
    runtime.register_module(std::make_unique<GameGatewayService>());
  }

  // ---- 第三阶段：新版客户端网关服务（按需启用） ----
  if (runtime.context().config.runtime.enable_client_v1_gateways) {
    // 创建共享的准入注册表，用于协调登录网关到游戏网关的会话转移
    auto client_v1_admissions = std::make_shared<ClientV1AdmissionRegistry>();
    runtime.register_module(
        std::make_unique<ClientV1LoginGatewayService>(client_v1_admissions));
    runtime.register_module(
        std::make_unique<ClientV1GameGatewayService>(client_v1_admissions));
  }
}

}  // namespace mir2
