/**
 * @file auth_service.hpp
 * @brief 用户认证服务头文件
 *
 * @details 定义 AuthService 类，负责处理用户登录、注册、修改密码、角色管理等
 * 认证相关的业务逻辑。作为模块运行在独立的线程中，通过 LocalBus 消息总线
 * 与登录网关、持久化服务等进行异步通信。
 *
 * @note 认证流程采用状态机驱动，每个会话的登录阶段由 CanonicalLoginStage 跟踪。
 *       认证结果通过回调结果处理，支持请求标识(request_id)进行异步响应匹配。
 */

#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "core/module.hpp"
#include "protocol/canonical_login_state.hpp"

namespace mir2 {

/**
 * @class AuthService
 * @brief 用户认证服务模块
 *
 * @details 处理客户端认证生命周期中的各个阶段，包括：
 *          - 账号密码认证(authenticate_account)
 *          - 账号注册(create_account)
 *          - 账号信息更新(update_account)
 *          - 密码修改(change_password)
 *          - 角色查询(query_characters)
 *          - 角色创建预检查(create_precheck)与提交(create_commit)
 *          - 角色删除(delete_character)
 *          - 角色选择(select_character)
 *
 *          维护会话状态表、待处理请求表和准入表，确保认证流程的完整性和安全性。
 *          支持重复登录检测和强制踢出机制。
 */
class AuthService : public Module {
 public:
  AuthService() = default;

  /**
   * @brief 析构函数，自动停止服务并等待工作线程结束
   */
  ~AuthService() override {
    stop();
    join();
  }

  /**
   * @brief 获取模块名称
   * @return 返回 "auth_service"
   */
  [[nodiscard]] std::string name() const override { return "auth_service"; }

  /**
   * @brief 启动认证服务
   * @param context 宿主上下文，包含配置、消息总线等系统资源引用
   */
  void start(HostContext& context) override;

  /**
   * @brief 停止认证服务
   */
  void stop() override;

  /**
   * @brief 等待工作线程结束
   */
  void join() override;

  /**
   * @brief 获取服务快照信息
   * @return 包含运行状态、待请求数等信息的键值对映射
   */
  [[nodiscard]] std::unordered_map<std::string, std::string> snapshot() const override;

 private:
  /**
   * @struct SessionState
   * @brief 客户端会话状态
   *
   * @details 跟踪每个客户端连接的认证进度，包括当前登录阶段、
   *          认证凭据、最后指令时间等关键信息。
   */
  struct SessionState {
    std::string account_id{};      ///< 账号ID
    std::int32_t certification{0}; ///< 认证凭据值
    std::int32_t client_version{0}; ///< 客户端版本号
    std::int64_t last_command_at_ms{0}; ///< 上一条指令的时间戳(毫秒),用于指令频率控制
    CanonicalLoginStage stage{CanonicalLoginStage::connected}; ///< 当前登录阶段状态
  };

  /**
   * @enum PendingAuthRequestKind
   * @brief 待处理认证请求的类型枚举
   */
  enum class PendingAuthRequestKind {
    authenticate_account,  ///< 账号认证
    create_account,        ///< 创建账号
    update_account,        ///< 更新账号信息
    change_password,       ///< 修改密码
    query_characters,      ///< 查询角色列表
    create_precheck,       ///< 创建角色前的预检查
    create_commit,         ///< 提交创建角色
    delete_character,      ///< 删除角色
    select_character       ///< 选择角色
  };

  /**
   * @struct PendingAuthRequest
   * @brief 待处理认证请求
   *
   * @details 保存已发送到持久化服务的请求上下文，当收到响应时用于匹配和恢复请求信息。
   */
  struct PendingAuthRequest {
    PendingAuthRequestKind kind{PendingAuthRequestKind::query_characters}; ///< 请求类型
    std::uint64_t session_id{0};   ///< 会话ID
    std::string account_id{};      ///< 账号ID
    std::string character_name{};  ///< 角色名
    std::int32_t certification{0}; ///< 认证凭据
    std::int32_t client_version{0}; ///< 客户端版本
    AccountRecord account{};       ///< 账号记录
    CharacterRecord character{};   ///< 角色记录
  };

  /**
   * @struct LoginAdmission
   * @brief 登录准入记录
   *
   * @details 认证通过后创建的准入凭证，用于后续的服务器选择、角色选择等操作。
   *          包含认证凭据值作为唯一标识。
   */
  struct LoginAdmission {
    std::string account_id{};        ///< 账号ID
    std::string selected_character{}; ///< 已选择的角色名
    std::int32_t certification{0};   ///< 认证凭据值
    CanonicalLoginStage stage{CanonicalLoginStage::authenticated}; ///< 当前阶段
  };

  /**
   * @brief 工作线程主循环
   */
  void run();

  /**
   * @brief 处理会话事件(连接、断开、数据包等)
   * @param event 会话事件
   */
  void handle_session_event(const SessionEvent& event);

  /**
   * @brief 处理逻辑指令(如撤销认证)
   * @param command 逻辑指令
   */
  void handle_logic_command(const LogicCommand& command);

  /**
   * @brief 处理持久化结果回调
   * @param result 持久化操作结果
   */
  void handle_persist_result(const PersistResult& result);

  /**
   * @brief 生成唯一的请求ID
   * @return 格式为 "auth:<递增序号>" 的请求ID字符串
   */
  [[nodiscard]] std::string make_request_id();

  HostContext* context_{nullptr};
  std::shared_ptr<LocalBus::Endpoint> endpoint_{};
  std::thread worker_{};
  std::atomic_bool running_{false};
  std::unordered_map<std::string, PendingAuthRequest> pending_requests_{};
  std::unordered_map<std::uint64_t, SessionState> session_states_{};
  std::unordered_map<std::int32_t, LoginAdmission> admissions_{};
  std::unordered_map<std::string, std::string> last_selected_character_{};
  std::atomic_int32_t next_certification_{1000};
  std::uint64_t next_request_id_{1};
};

}  // namespace mir2
