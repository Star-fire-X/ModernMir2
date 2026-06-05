/**
 * @file canonical_login_state.cpp
 * @brief 登录状态机逻辑的实现
 *
 * @details 本文件实现了登录状态机的核心逻辑：
 * 1. can_accept()：根据当前阶段判断是否允许处理特定请求，
 *    确保请求按正确的顺序被处理（如必须先认证才能选服务器）。
 * 2. advance()：根据状态转换类型推进登录阶段，
 *    实现 connected -> authenticated -> server_selected ->
 *    character_selected -> entering_game -> in_game 的线性迁移。
 * 3. stage_name()：返回状态的可读字符串名称，用于日志和调试。
 *
 * @warning can_accept() 是守卫函数，应先调用它再执行请求处理逻辑。
 *          advance() 假设转换是合法的，调用者应确保已通过 can_accept() 检查。
 */

#include "protocol/canonical_login_state.hpp"

namespace mir2 {

bool can_accept(CanonicalLoginStage stage, CanonicalLoginRequest request) {
  // 已断开连接后不接受任何请求
  if (stage == CanonicalLoginStage::disconnected) {
    return false;
  }

  switch (request) {
    // 认证请求：只能在 connected 阶段发起，或从 authenticated 阶段重新认证
    case CanonicalLoginRequest::authenticate:
      return stage == CanonicalLoginStage::connected ||
             stage == CanonicalLoginStage::authenticated;
    // 选择服务器：只能在认证通过后
    case CanonicalLoginRequest::select_server:
      return stage == CanonicalLoginStage::authenticated;
    // 角色管理相关请求：只能在选定服务器后
    case CanonicalLoginRequest::query_characters:
    case CanonicalLoginRequest::create_character:
    case CanonicalLoginRequest::delete_character:
    case CanonicalLoginRequest::select_character:
      return stage == CanonicalLoginStage::server_selected;
    // 进入游戏世界：必须在角色选定后
    case CanonicalLoginRequest::enter_world:
      return stage == CanonicalLoginStage::character_selected;
    // 完成进入游戏：必须在正在进入的阶段
    case CanonicalLoginRequest::finish_enter_game:
      return stage == CanonicalLoginStage::entering_game;
    // 游戏操作：必须在游戏中
    case CanonicalLoginRequest::gameplay:
      return stage == CanonicalLoginStage::in_game;
  }
  return false;
}

CanonicalLoginStage advance(CanonicalLoginStage stage, CanonicalLoginTransition transition) {
  // 已断开连接后状态不再变化
  if (stage == CanonicalLoginStage::disconnected) {
    return CanonicalLoginStage::disconnected;
  }

  switch (transition) {
    case CanonicalLoginTransition::authenticate:
      return CanonicalLoginStage::authenticated;
    case CanonicalLoginTransition::select_server:
      return CanonicalLoginStage::server_selected;
    case CanonicalLoginTransition::select_character:
      return CanonicalLoginStage::character_selected;
    case CanonicalLoginTransition::enter_game:
      return CanonicalLoginStage::entering_game;
    case CanonicalLoginTransition::enter_game_complete:
      return CanonicalLoginStage::in_game;
    case CanonicalLoginTransition::disconnect:
      return CanonicalLoginStage::disconnected;
  }
  return stage;
}

const char* stage_name(CanonicalLoginStage stage) {
  switch (stage) {
    case CanonicalLoginStage::connected:
      return "Connected";
    case CanonicalLoginStage::authenticated:
      return "Authenticated";
    case CanonicalLoginStage::server_selected:
      return "ServerSelected";
    case CanonicalLoginStage::character_selected:
      return "CharacterSelected";
    case CanonicalLoginStage::entering_game:
      return "EnteringGame";
    case CanonicalLoginStage::in_game:
      return "InGame";
    case CanonicalLoginStage::disconnected:
      return "Disconnected";
  }
  return "Unknown";
}

}  // namespace mir2
