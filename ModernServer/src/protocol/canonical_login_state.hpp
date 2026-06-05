/**
 * @file canonical_login_state.hpp
 * @brief 登录状态机的规范化定义
 *
 * @details 本文件定义了登录流程的状态机模型，包括：
 * - CanonicalLoginStage：登录所处的阶段（连接/认证/选服/选角/进入游戏/游戏中/断线）
 * - CanonicalLoginRequest：外部请求类型（定义在当前状态下允许的操作）
 * - CanonicalLoginTransition：状态转换类型（定义合法的阶段间迁移）
 *
 * 该状态机确保登录流程按正确的顺序执行，防止跳过关键步骤（如未认证直接选服）
 * 或在不正确的状态下执行非法操作。核心函数 can_accept() 和 advance() 分别
 * 用于检查请求的合法性以及执行状态迁移。
 */

#pragma once

namespace mir2 {

/**
 * @enum CanonicalLoginStage
 * @brief 登录阶段枚举
 *
 * @details 定义客户端登录到进入游戏完整流程中的所有阶段。
 * 状态转换顺序为：connected -> authenticated -> server_selected ->
 * character_selected -> entering_game -> in_game。
 * 任意阶段均可转换为 disconnected 表示断开连接。
 */
enum class CanonicalLoginStage {
  connected,          ///< 刚刚建立 TCP 连接
  authenticated,      ///< 已通过账号密码认证
  server_selected,    ///< 已选择游戏服务器
  character_selected, ///< 已选择游戏角色
  entering_game,      ///< 正在进入游戏世界
  in_game,            ///< 已在游戏中
  disconnected        ///< 连接已断开（终态）
};

/**
 * @enum CanonicalLoginRequest
 * @brief 登录请求类型枚举
 *
 * @details 客户端在登录过程中可能发出的各种请求。
 * 这些请求需要在适当的登录阶段才能被接受，
 * 由 can_accept() 函数进行合法性校验。
 */
enum class CanonicalLoginRequest {
  authenticate,        ///< 身份认证请求
  select_server,       ///< 选择服务器请求
  query_characters,    ///< 查询角色列表请求
  create_character,    ///< 创建角色请求
  delete_character,    ///< 删除角色请求
  select_character,    ///< 选择角色请求
  enter_world,         ///< 进入游戏世界请求
  finish_enter_game,   ///< 进入游戏完成通知
  gameplay             ///< 正常游戏操作
};

/**
 * @enum CanonicalLoginTransition
 * @brief 登录状态转换枚举
 *
 * @details 定义合法的状态迁移操作。每种转换对应一个特定的阶段推进，
 * 由 advance() 函数执行实际的状态变更。
 * disconnect 转换可从任意非终态退出到 disconnected 终态。
 */
enum class CanonicalLoginTransition {
  authenticate,        ///< 认证成功，从 connected -> authenticated
  select_server,       ///< 选服成功，从 authenticated -> server_selected
  select_character,    ///< 选角成功，从 server_selected -> character_selected
  enter_game,          ///< 开始进入游戏，从 character_selected -> entering_game
  enter_game_complete, ///< 进入游戏完成，从 entering_game -> in_game
  disconnect           ///< 断开连接，从任意状态 -> disconnected
};

/**
 * @brief 检查当前阶段是否允许处理特定请求
 *
 * @details 根据登录状态机的规则，判断给定的登录阶段是否接受某个请求类型。
 * 例如，选择服务器请求只能在 authenticated 阶段处理，
 * 选择角色请求只能在 server_selected 阶段处理。
 * disconnected 阶段不接受任何请求。
 *
 * @param stage 当前登录阶段
 * @param request 要检查的请求类型
 * @return true 如果该请求在当前阶段是合法的
 * @return false 如果该请求在当前阶段不被允许
 */
[[nodiscard]] bool can_accept(CanonicalLoginStage stage, CanonicalLoginRequest request);

/**
 * @brief 执行登录状态迁移
 *
 * @details 根据给定的状态转换类型，将当前登录阶段迁移到下一个阶段。
 * 例如，authenticate 转换将状态从 connected 迁移到 authenticated。
 * 如果当前状态为 disconnected（终态），则始终停留在该状态。
 *
 * @param stage 当前登录阶段
 * @param transition 要执行的状态转换
 * @return CanonicalLoginStage 迁移后的新阶段
 */
[[nodiscard]] CanonicalLoginStage advance(CanonicalLoginStage stage,
                                          CanonicalLoginTransition transition);

/**
 * @brief 获取登录阶段的字符串名称
 *
 * @param stage 登录阶段
 * @return const char* 阶段名称的 C 字符串，如"Connected"、"Authenticated"等
 */
[[nodiscard]] const char* stage_name(CanonicalLoginStage stage);

}  // namespace mir2
