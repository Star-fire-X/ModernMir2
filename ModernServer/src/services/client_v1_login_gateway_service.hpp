/**
 * @file client_v1_login_gateway_service.hpp
 * @brief Client v1 登录网关服务头文件
 *
 * @details 定义 ClientV1LoginGatewayService 类，继承自 ClientV1GatewayServiceBase。
 *          负责处理新协议客户端(Client v1)的登录认证流程，包括：
 *          客户端握手、账号登录、注册、修改密码、更新资料、服务器选择、
 *          角色管理(列表/创建/删除/选择)等全流程。
 *
 * @see ClientV1GatewayServiceBase
 * @see ClientV1AdmissionRegistry
 */

#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "protocol/canonical_login_state.hpp"
#include "services/client_v1_admission_registry.hpp"
#include "services/client_v1_gateway_service_base.hpp"
#include "storage/repository.hpp"

namespace mir2 {

/**
 * @class ClientV1LoginGatewayService
 * @brief Client v1 登录网关服务
 *
 * @details 处理新协议客户端的登录认证全流程：
 *          1. ClientHello — 客户端握手，协议版本检查
 *          2. LoginRequest — 账号密码登录
 *          3. CreateAccountRequest — 注册新账号
 *          4. ChangePasswordRequest — 修改密码
 *          5. UpdateAccountRequest — 更新账号资料
 *          6. SelectServerRequest — 选择游戏服务器
 *          7. CharacterListRequest — 查询角色列表
 *          8. CreateCharacterRequest — 创建新角色
 *          9. DeleteCharacterRequest — 删除角色
 *          10. SelectCharacterRequest — 选择角色(获得进入游戏令牌)
 *
 *          登录成功后发放准入令牌(Admission Token)，供游戏网关验证。
 */
class ClientV1LoginGatewayService : public ClientV1GatewayServiceBase {
 public:
  /**
   * @brief 构造函数
   * @param admissions 准入注册表共享指针
   */
  explicit ClientV1LoginGatewayService(std::shared_ptr<ClientV1AdmissionRegistry> admissions);

  /**
   * @brief 启动服务，初始化数据库和准入注册表
   * @param context 宿主上下文
   */
  void start(HostContext& context) override;

 protected:
  PortBinding binding(const HostContext& context) const override;
  void handle_message(std::uint64_t session_id, const std::string& peer_address,
                      std::uint32_t sequence,
                      const client_v1::Message& message) override;
  void handle_connected(std::uint64_t session_id, const std::string& peer_address) override;
  void handle_disconnected(std::uint64_t session_id, const std::string& peer_address,
                           const std::string& reason) override;

 private:
  /**
   * @struct SessionState
   * @brief 客户端登录会话状态
   */
  struct SessionState {
    bool greeted{false};              ///< 是否已收到客户端握手
    bool authenticated{false};        ///< 是否已通过认证
    std::string account_id{};         ///< 账号ID
    std::string display_name{};       ///< 显示名称
    CanonicalLoginStage stage{CanonicalLoginStage::connected}; ///< 当前登录阶段
  };

  /**
   * @struct LobbyAdmission
   * @brief Lobby 准入记录(服务器选择后创建)
   *
   * @details 当客户端选择服务器后，会创建一个 Lobby 准入记录，
   *          允许后续的角色操作(列表/创建/删除/选择)跳过重新认证。
   */
  struct LobbyAdmission {
    std::string account_id{};        ///< 账号ID
    std::string display_name{};      ///< 显示名称
    std::string server_name{};       ///< 目标服务器名
    CanonicalLoginStage stage{CanonicalLoginStage::server_selected}; ///< 当前阶段
  };

  /// @name 消息处理函数
  /// @{
  void handle_client_hello(std::uint64_t session_id, const client_v1::ClientHello& hello);
  void handle_login_request(std::uint64_t session_id, const client_v1::LoginRequest& request);
  void handle_create_account_request(std::uint64_t session_id,
                                     const client_v1::CreateAccountRequest& request);
  void handle_change_password_request(std::uint64_t session_id,
                                      const client_v1::ChangePasswordRequest& request);
  void handle_update_account_request(std::uint64_t session_id,
                                     const client_v1::UpdateAccountRequest& request);
  void handle_select_server_request(std::uint64_t session_id,
                                    const client_v1::SelectServerRequest& request);
  void handle_character_list_request(std::uint64_t session_id,
                                     const client_v1::CharacterListRequest& request);
  void handle_create_character_request(std::uint64_t session_id,
                                       const client_v1::CreateCharacterRequest& request);
  void handle_delete_character_request(std::uint64_t session_id,
                                       const client_v1::DeleteCharacterRequest& request);
  void handle_select_character_request(std::uint64_t session_id,
                                       const client_v1::SelectCharacterRequest& request);
  /// @}

  /// @name 辅助函数
  /// @{
  [[nodiscard]] bool session_ready(std::uint64_t session_id) const;
  [[nodiscard]] std::optional<SessionState> session(std::uint64_t session_id) const;
  [[nodiscard]] static client_v1::CharacterSummary to_summary(const CharacterRecord& character);
  [[nodiscard]] std::string issue_lobby_token(const SessionState& state,
                                              const std::string& server_name);
  [[nodiscard]] std::optional<SessionState> authenticate_lobby_session(
      std::uint64_t session_id, const std::string& lobby_token);
  void send_server_list(std::uint64_t session_id);
  /// @}

  std::shared_ptr<ClientV1AdmissionRegistry> admissions_{}; ///< 准入注册表
  std::unique_ptr<Repository> repository_{};                ///< 数据仓库
  mutable std::mutex mutex_{};                              ///< 保护会话表的互斥锁
  std::unordered_map<std::uint64_t, SessionState> sessions_{}; ///< 会话状态表
  std::unordered_map<std::string, LobbyAdmission> lobby_admissions_{}; ///< Lobby 准入表
  std::uint64_t next_lobby_token_{1};                       ///< 下一个 Lobby 令牌序号
};

}  // namespace mir2
