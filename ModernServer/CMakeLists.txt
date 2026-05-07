cmake_minimum_required(VERSION 3.24)

project(ModernMir2Host VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /bigobj)
endif()

if(WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

include(FetchContent)
include(CTest)

find_package(Threads REQUIRED)
find_package(SQLite3 QUIET)
find_package(ODBC QUIET)

if(NOT SQLite3_FOUND AND MINGW)
  find_path(SQLITE3_INCLUDE_DIR sqlite3.h
    PATHS
      C:/msys64/ucrt64/include
      C:/msys64/mingw64/include
  )
  find_library(SQLITE3_LIBRARY
    NAMES sqlite3 libsqlite3.a sqlite3.dll.a
    PATHS
      C:/msys64/ucrt64/lib
      C:/msys64/mingw64/lib
  )
  if(SQLITE3_INCLUDE_DIR AND SQLITE3_LIBRARY)
    add_library(SQLite::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(SQLite::SQLite3 PROPERTIES
      IMPORTED_LOCATION "${SQLITE3_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${SQLITE3_INCLUDE_DIR}"
    )
  else()
    message(FATAL_ERROR "SQLite3 was not found in the default search path or the MSYS fallback path.")
  endif()
endif()

if(NOT TARGET SQLite::SQLite3)
  message(FATAL_ERROR
    "SQLite3 was not found. For MSVC builds, install sqlite3 with vcpkg and configure with "
    "-DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake.")
endif()

FetchContent_Declare(
  asio_src
  URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
)

FetchContent_Declare(
  spdlog_src
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.14.1.tar.gz
)

FetchContent_Declare(
  tomlplusplus_src
  URL https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
)

FetchContent_MakeAvailable(asio_src spdlog_src tomlplusplus_src)

add_library(asio INTERFACE)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
target_include_directories(asio INTERFACE ${asio_src_SOURCE_DIR}/asio/include)

set(MIR2_HOST_SOURCES
  src/config/config_loader.cpp
  src/core/default_modules.cpp
  src/core/host_runtime.cpp
  src/importer/legacy_character_importer.cpp
  src/importer/legacy_importer.cpp
  src/protocol/client_v1_session.cpp
  src/protocol/game_session.cpp
  src/protocol/legacy_edcode.cpp
  src/protocol/legacy_game_codec.cpp
  src/protocol/legacy_protocol.cpp
  src/protocol/session_router.cpp
  src/services/auth_service.cpp
  src/services/client_v1_gateway_service_base.cpp
  src/services/client_v1_game_gateway_service.cpp
  src/services/client_v1_login_gateway_service.cpp
  src/services/game_gateway_service.cpp
  src/services/gateway_service_base.cpp
  src/services/log_service.cpp
  src/services/login_gateway_service.cpp
  src/services/persistence_service.cpp
  src/services/world_service.cpp
  src/storage/repository.cpp
  src/world/game_object.cpp
  src/world/legacy_event_manager.cpp
  src/world/legacy_frame_driver.cpp
  src/world/legacy_item_rules.cpp
  src/world/legacy_map_environment.cpp
  src/world/legacy_magic_runtime.cpp
  src/world/legacy_skill_formula.cpp
  src/world/logic_runtime.cpp
  src/world/make_index_allocator.cpp
  src/world/map_actor.cpp
)

add_library(mir2_hostlib ${MIR2_HOST_SOURCES})

if(BUILD_TESTING)
  target_compile_definitions(mir2_hostlib PUBLIC MIR2_ENABLE_TEST_HOOKS=1)
endif()

target_include_directories(mir2_hostlib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(mir2_hostlib
  PUBLIC
    Threads::Threads
    SQLite::SQLite3
    asio
    spdlog::spdlog
    tomlplusplus::tomlplusplus
)

if(ODBC_FOUND AND MSVC)
  target_link_libraries(mir2_hostlib PUBLIC ODBC::ODBC)
  target_compile_definitions(mir2_hostlib PUBLIC MIR2_ENABLE_ODBC=1)
endif()

if(WIN32)
  target_link_libraries(mir2_hostlib PUBLIC ws2_32 mswsock)
endif()

add_executable(mir2_host src/app/main.cpp)
target_link_libraries(mir2_host PRIVATE mir2_hostlib)

if(BUILD_TESTING)
  add_executable(mir2_core_smoke tests/core_smoke.cpp)
  target_link_libraries(mir2_core_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_core_smoke COMMAND mir2_core_smoke)

  add_executable(mir2_logic_smoke tests/logic_smoke.cpp)
  target_link_libraries(mir2_logic_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_logic_smoke COMMAND mir2_logic_smoke)

  add_executable(mir2_legacy_frame_smoke tests/legacy_frame_smoke.cpp)
  target_link_libraries(mir2_legacy_frame_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_frame_smoke COMMAND mir2_legacy_frame_smoke)

  add_executable(mir2_legacy_environment_smoke tests/legacy_environment_smoke.cpp)
  target_link_libraries(mir2_legacy_environment_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_environment_smoke COMMAND mir2_legacy_environment_smoke)

  add_executable(mir2_legacy_mapinfo_import_routes_smoke
    tests/legacy_mapinfo_import_routes_smoke.cpp)
  target_link_libraries(mir2_legacy_mapinfo_import_routes_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_mapinfo_import_routes_smoke
    COMMAND mir2_legacy_mapinfo_import_routes_smoke)

  add_executable(mir2_movement_blocking_legacy_smoke
    tests/movement_blocking_legacy_smoke.cpp)
  target_link_libraries(mir2_movement_blocking_legacy_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_movement_blocking_legacy_smoke
    COMMAND mir2_movement_blocking_legacy_smoke)

  add_executable(mir2_visibility_delta_smoke tests/visibility_delta_smoke.cpp)
  target_link_libraries(mir2_visibility_delta_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_visibility_delta_smoke COMMAND mir2_visibility_delta_smoke)

  add_executable(mir2_visibility_phase3_smoke tests/visibility_phase3_smoke.cpp)
  target_link_libraries(mir2_visibility_phase3_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_visibility_phase3_smoke COMMAND mir2_visibility_phase3_smoke)

  add_executable(mir2_safe_zone_legacy_smoke tests/safe_zone_legacy_smoke.cpp)
  target_link_libraries(mir2_safe_zone_legacy_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_safe_zone_legacy_smoke COMMAND mir2_safe_zone_legacy_smoke)

  add_executable(mir2_door_open_close_smoke tests/door_open_close_smoke.cpp)
  target_link_libraries(mir2_door_open_close_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_door_open_close_smoke COMMAND mir2_door_open_close_smoke)

  add_executable(mir2_cross_map_gate_transfer_smoke
    tests/cross_map_gate_transfer_smoke.cpp)
  target_link_libraries(mir2_cross_map_gate_transfer_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_cross_map_gate_transfer_smoke
    COMMAND mir2_cross_map_gate_transfer_smoke)

  add_executable(mir2_legacy_player_lifecycle_smoke tests/legacy_player_lifecycle_smoke.cpp)
  target_link_libraries(mir2_legacy_player_lifecycle_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_player_lifecycle_smoke COMMAND mir2_legacy_player_lifecycle_smoke)

  add_executable(mir2_legacy_player_inbox_smoke tests/legacy_player_inbox_smoke.cpp)
  target_link_libraries(mir2_legacy_player_inbox_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_player_inbox_smoke COMMAND mir2_legacy_player_inbox_smoke)

  add_executable(mir2_legacy_player_operate_order_smoke
    tests/legacy_player_operate_order_smoke.cpp)
  target_link_libraries(mir2_legacy_player_operate_order_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_player_operate_order_smoke
    COMMAND mir2_legacy_player_operate_order_smoke)

  add_executable(mir2_legacy_player_rotation_smoke
    tests/legacy_player_rotation_smoke.cpp)
  target_link_libraries(mir2_legacy_player_rotation_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_player_rotation_smoke
    COMMAND mir2_legacy_player_rotation_smoke)

  add_executable(mir2_legacy_player_disconnect_smoke tests/legacy_player_disconnect_smoke.cpp)
  target_link_libraries(mir2_legacy_player_disconnect_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_player_disconnect_smoke COMMAND mir2_legacy_player_disconnect_smoke)

  add_executable(mir2_legacy_creature_order_smoke tests/legacy_creature_order_smoke.cpp)
  target_link_libraries(mir2_legacy_creature_order_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_creature_order_smoke COMMAND mir2_legacy_creature_order_smoke)

  add_executable(mir2_legacy_monster_cursor_smoke tests/legacy_monster_cursor_smoke.cpp)
  target_link_libraries(mir2_legacy_monster_cursor_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_monster_cursor_smoke COMMAND mir2_legacy_monster_cursor_smoke)

  add_executable(mir2_legacy_monster_import_defs_spawns_drops_smoke
    tests/legacy_monster_import_defs_spawns_drops_smoke.cpp)
  target_link_libraries(mir2_legacy_monster_import_defs_spawns_drops_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_monster_import_defs_spawns_drops_smoke
    COMMAND mir2_legacy_monster_import_defs_spawns_drops_smoke)

  add_executable(mir2_monster_spawn_count_range_smoke
    tests/monster_spawn_count_range_smoke.cpp)
  target_link_libraries(mir2_monster_spawn_count_range_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_spawn_count_range_smoke
    COMMAND mir2_monster_spawn_count_range_smoke)

  add_executable(mir2_monster_base_object_smoke tests/monster_base_object_smoke.cpp)
  target_link_libraries(mir2_monster_base_object_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_base_object_smoke COMMAND mir2_monster_base_object_smoke)

  add_executable(mir2_monster_legacy_tick_ai_smoke
    tests/monster_legacy_tick_ai_smoke.cpp)
  target_link_libraries(mir2_monster_legacy_tick_ai_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_legacy_tick_ai_smoke
    COMMAND mir2_monster_legacy_tick_ai_smoke)

  add_executable(mir2_monster_legacy_combat_damage_smoke
    tests/monster_legacy_combat_damage_smoke.cpp)
  target_link_libraries(mir2_monster_legacy_combat_damage_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_legacy_combat_damage_smoke
    COMMAND mir2_monster_legacy_combat_damage_smoke)

  add_executable(mir2_monster_struck_retaliation_smoke
    tests/monster_struck_retaliation_smoke.cpp)
  target_link_libraries(mir2_monster_struck_retaliation_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_struck_retaliation_smoke
    COMMAND mir2_monster_struck_retaliation_smoke)

  add_executable(mir2_monster_death_corpse_ghost_smoke
    tests/monster_death_corpse_ghost_smoke.cpp)
  target_link_libraries(mir2_monster_death_corpse_ghost_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_death_corpse_ghost_smoke
    COMMAND mir2_monster_death_corpse_ghost_smoke)

  add_executable(mir2_monster_spawn_alive_count_smoke
    tests/monster_spawn_alive_count_smoke.cpp)
  target_link_libraries(mir2_monster_spawn_alive_count_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_spawn_alive_count_smoke
    COMMAND mir2_monster_spawn_alive_count_smoke)

  add_executable(mir2_monster_spawn_zentime_smoke
    tests/monster_spawn_zentime_smoke.cpp)
  target_link_libraries(mir2_monster_spawn_zentime_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_spawn_zentime_smoke
    COMMAND mir2_monster_spawn_zentime_smoke)

  add_executable(mir2_monster_legacy_drop_scatter_smoke
    tests/monster_legacy_drop_scatter_smoke.cpp)
  target_link_libraries(mir2_monster_legacy_drop_scatter_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_legacy_drop_scatter_smoke
    COMMAND mir2_monster_legacy_drop_scatter_smoke)

  add_executable(mir2_monster_drop_gold_split_smoke
    tests/monster_drop_gold_split_smoke.cpp)
  target_link_libraries(mir2_monster_drop_gold_split_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_drop_gold_split_smoke
    COMMAND mir2_monster_drop_gold_split_smoke)

  add_executable(mir2_monster_drop_owner_protection_smoke
    tests/monster_drop_owner_protection_smoke.cpp)
  target_link_libraries(mir2_monster_drop_owner_protection_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_drop_owner_protection_smoke
    COMMAND mir2_monster_drop_owner_protection_smoke)

  add_executable(mir2_monster_slave_exp_smoke tests/monster_slave_exp_smoke.cpp)
  target_link_libraries(mir2_monster_slave_exp_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_slave_exp_smoke COMMAND mir2_monster_slave_exp_smoke)

  add_executable(mir2_monster_slave_ownership_smoke tests/monster_slave_ownership_smoke.cpp)
  target_link_libraries(mir2_monster_slave_ownership_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_slave_ownership_smoke COMMAND mir2_monster_slave_ownership_smoke)

  add_executable(mir2_monster_slave_lifecycle_smoke tests/monster_slave_lifecycle_smoke.cpp)
  target_link_libraries(mir2_monster_slave_lifecycle_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_slave_lifecycle_smoke COMMAND mir2_monster_slave_lifecycle_smoke)

  add_executable(mir2_monster_drop_random_upgrade_smoke
    tests/monster_drop_random_upgrade_smoke.cpp)
  target_link_libraries(mir2_monster_drop_random_upgrade_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_drop_random_upgrade_smoke
    COMMAND mir2_monster_drop_random_upgrade_smoke)

  add_executable(mir2_legacy_npc_merchant_cursor_smoke tests/legacy_npc_merchant_cursor_smoke.cpp)
  target_link_libraries(mir2_legacy_npc_merchant_cursor_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_npc_merchant_cursor_smoke COMMAND mir2_legacy_npc_merchant_cursor_smoke)

  add_executable(mir2_legacy_random_smoke tests/legacy_random_smoke.cpp)
  target_link_libraries(mir2_legacy_random_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_random_smoke COMMAND mir2_legacy_random_smoke)

  add_executable(mir2_skill_formula_golden_smoke tests/skill_formula_golden_smoke.cpp)
  target_link_libraries(mir2_skill_formula_golden_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_formula_golden_smoke COMMAND mir2_skill_formula_golden_smoke)

  add_executable(mir2_skill_protocol_golden_smoke tests/skill_protocol_golden_smoke.cpp)
  target_link_libraries(mir2_skill_protocol_golden_smoke PRIVATE mir2_hostlib)
  target_compile_definitions(mir2_skill_protocol_golden_smoke
    PRIVATE "MIR2_CONFIG_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/config\"")
  add_test(NAME mir2_skill_protocol_golden_smoke COMMAND mir2_skill_protocol_golden_smoke)

  add_executable(mir2_magic_config_golden_smoke tests/magic_config_golden_smoke.cpp)
  target_link_libraries(mir2_magic_config_golden_smoke PRIVATE mir2_hostlib)
  target_compile_definitions(mir2_magic_config_golden_smoke
    PRIVATE "MIR2_CONFIG_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/config\"")
  add_test(NAME mir2_magic_config_golden_smoke COMMAND mir2_magic_config_golden_smoke)

  add_executable(mir2_skill_legacy_spell_sequence_smoke
    tests/skill_legacy_spell_sequence_smoke.cpp)
  target_link_libraries(mir2_skill_legacy_spell_sequence_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_legacy_spell_sequence_smoke
    COMMAND mir2_skill_legacy_spell_sequence_smoke)

  add_executable(mir2_skill_book_lifecycle_smoke
    tests/skill_book_lifecycle_smoke.cpp)
  target_link_libraries(mir2_skill_book_lifecycle_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_book_lifecycle_smoke
    COMMAND mir2_skill_book_lifecycle_smoke)

  add_executable(mir2_skill_training_lvexp_smoke
    tests/skill_training_lvexp_smoke.cpp)
  target_link_libraries(mir2_skill_training_lvexp_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_training_lvexp_smoke
    COMMAND mir2_skill_training_lvexp_smoke)

  add_executable(mir2_skill_active_spell_phase3_smoke
    tests/skill_active_spell_phase3_smoke.cpp)
  target_link_libraries(mir2_skill_active_spell_phase3_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_active_spell_phase3_smoke
    COMMAND mir2_skill_active_spell_phase3_smoke)

  add_executable(mir2_skill_bujuk_status_phase4_smoke
    tests/skill_bujuk_status_phase4_smoke.cpp)
  target_link_libraries(mir2_skill_bujuk_status_phase4_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_bujuk_status_phase4_smoke
    COMMAND mir2_skill_bujuk_status_phase4_smoke)

  add_executable(mir2_skill_summon_slave_smoke
    tests/skill_summon_slave_smoke.cpp)
  target_link_libraries(mir2_skill_summon_slave_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_summon_slave_smoke
    COMMAND mir2_skill_summon_slave_smoke)

  add_executable(mir2_skill_common_magic_matrix_smoke
    tests/skill_common_magic_matrix_smoke.cpp)
  target_link_libraries(mir2_skill_common_magic_matrix_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_common_magic_matrix_smoke
    COMMAND mir2_skill_common_magic_matrix_smoke)

  add_executable(mir2_skill_weapon_sword_common_smoke
    tests/skill_weapon_sword_common_smoke.cpp)
  target_link_libraries(mir2_skill_weapon_sword_common_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_weapon_sword_common_smoke
    COMMAND mir2_skill_weapon_sword_common_smoke)

  add_executable(mir2_skill_status_poison_buff_hide_shield_smoke
    tests/skill_status_poison_buff_hide_shield_smoke.cpp)
  target_link_libraries(mir2_skill_status_poison_buff_hide_shield_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_status_poison_buff_hide_shield_smoke
    COMMAND mir2_skill_status_poison_buff_hide_shield_smoke)

  add_executable(mir2_skill_training_persistence_smoke
    tests/skill_training_persistence_smoke.cpp)
  target_link_libraries(mir2_skill_training_persistence_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_skill_training_persistence_smoke
    COMMAND mir2_skill_training_persistence_smoke)

  add_executable(mir2_legacy_combat_resolution_smoke tests/legacy_combat_resolution_smoke.cpp)
  target_link_libraries(mir2_legacy_combat_resolution_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_combat_resolution_smoke COMMAND mir2_legacy_combat_resolution_smoke)

  add_executable(mir2_legacy_combat_formula_smoke tests/legacy_combat_formula_smoke.cpp)
  target_link_libraries(mir2_legacy_combat_formula_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_combat_formula_smoke COMMAND mir2_legacy_combat_formula_smoke)

  add_executable(mir2_player_derived_ability_equipment_smoke
      tests/player_derived_ability_equipment_smoke.cpp)
    target_link_libraries(mir2_player_derived_ability_equipment_smoke PRIVATE mir2_hostlib)
    add_test(NAME mir2_player_derived_ability_equipment_smoke
      COMMAND mir2_player_derived_ability_equipment_smoke)
  add_executable(mir2_equipment_cantakeon_legacy_smoke
      tests/equipment_cantakeon_legacy_smoke.cpp)
    target_link_libraries(mir2_equipment_cantakeon_legacy_smoke PRIVATE mir2_hostlib)
    add_test(NAME mir2_equipment_cantakeon_legacy_smoke
      COMMAND mir2_equipment_cantakeon_legacy_smoke)
  add_executable(mir2_equipment_derived_upgrade_stats_smoke
      tests/equipment_derived_upgrade_stats_smoke.cpp)
    target_link_libraries(mir2_equipment_derived_upgrade_stats_smoke PRIVATE mir2_hostlib)
    add_test(NAME mir2_equipment_derived_upgrade_stats_smoke
      COMMAND mir2_equipment_derived_upgrade_stats_smoke)

  add_executable(mir2_pk_attack_mode_rules_smoke tests/pk_attack_mode_rules_smoke.cpp)
  target_link_libraries(mir2_pk_attack_mode_rules_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_pk_attack_mode_rules_smoke COMMAND mir2_pk_attack_mode_rules_smoke)

  add_executable(mir2_player_death_revive_smoke tests/player_death_revive_smoke.cpp)
  target_link_libraries(mir2_player_death_revive_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_player_death_revive_smoke COMMAND mir2_player_death_revive_smoke)

  add_executable(mir2_experience_levelup_legacy_smoke
    tests/experience_levelup_legacy_smoke.cpp)
  target_link_libraries(mir2_experience_levelup_legacy_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_experience_levelup_legacy_smoke
    COMMAND mir2_experience_levelup_legacy_smoke)

  add_executable(mir2_legacy_spell_resolution_smoke tests/legacy_spell_resolution_smoke.cpp)
  target_link_libraries(mir2_legacy_spell_resolution_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_spell_resolution_smoke COMMAND mir2_legacy_spell_resolution_smoke)

  add_executable(mir2_legacy_item_resolution_smoke tests/legacy_item_resolution_smoke.cpp)
  target_link_libraries(mir2_legacy_item_resolution_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_item_resolution_smoke COMMAND mir2_legacy_item_resolution_smoke)

  add_executable(mir2_legacy_script_runtime_smoke tests/legacy_script_runtime_smoke.cpp)
  target_link_libraries(mir2_legacy_script_runtime_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_script_runtime_smoke COMMAND mir2_legacy_script_runtime_smoke)

  add_executable(mir2_legacy_script_condition_action_smoke tests/legacy_script_condition_action_smoke.cpp)
  target_link_libraries(mir2_legacy_script_condition_action_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_script_condition_action_smoke COMMAND mir2_legacy_script_condition_action_smoke)

  add_executable(mir2_legacy_script_command_decode_full_smoke
    tests/legacy_script_command_decode_full_smoke.cpp)
  target_link_libraries(mir2_legacy_script_command_decode_full_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_script_command_decode_full_smoke
    COMMAND mir2_legacy_script_command_decode_full_smoke)

  add_executable(mir2_legacy_mapquest_import_trigger_smoke
    tests/legacy_mapquest_import_trigger_smoke.cpp)
  target_link_libraries(mir2_legacy_mapquest_import_trigger_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_mapquest_import_trigger_smoke
    COMMAND mir2_legacy_mapquest_import_trigger_smoke)

  add_executable(mir2_legacy_user_engine_timer_smoke tests/legacy_user_engine_timer_smoke.cpp)
  target_link_libraries(mir2_legacy_user_engine_timer_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_user_engine_timer_smoke COMMAND mir2_legacy_user_engine_timer_smoke)

  add_executable(mir2_legacy_event_manager_smoke tests/legacy_event_manager_smoke.cpp)
  target_link_libraries(mir2_legacy_event_manager_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_event_manager_smoke COMMAND mir2_legacy_event_manager_smoke)

  add_executable(mir2_repository_smoke tests/repository_smoke.cpp)
  target_link_libraries(mir2_repository_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_repository_smoke COMMAND mir2_repository_smoke)

  add_executable(mir2_legacy_character_import_smoke
    tests/legacy_character_import_smoke.cpp)
  target_link_libraries(mir2_legacy_character_import_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_character_import_smoke
    COMMAND mir2_legacy_character_import_smoke)

  add_executable(mir2_legacy_character_import_unknown_data_smoke
    tests/legacy_character_import_unknown_data_smoke.cpp)
  target_link_libraries(mir2_legacy_character_import_unknown_data_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_legacy_character_import_unknown_data_smoke
    COMMAND mir2_legacy_character_import_unknown_data_smoke)

  add_executable(mir2_auth_smoke tests/auth_smoke.cpp)
  target_link_libraries(mir2_auth_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_auth_smoke COMMAND mir2_auth_smoke)

  add_executable(mir2_account_smoke tests/account_smoke.cpp)
  target_link_libraries(mir2_account_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_account_smoke COMMAND mir2_account_smoke)

  add_executable(mir2_world_kick_smoke tests/world_kick_smoke.cpp)
  target_link_libraries(mir2_world_kick_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_world_kick_smoke COMMAND mir2_world_kick_smoke)

  add_executable(mir2_world_invalid_command_smoke tests/world_invalid_command_smoke.cpp)
  target_link_libraries(mir2_world_invalid_command_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_world_invalid_command_smoke COMMAND mir2_world_invalid_command_smoke)

  add_executable(mir2_gateway_kick_smoke tests/gateway_kick_smoke.cpp)
  target_link_libraries(mir2_gateway_kick_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_gateway_kick_smoke COMMAND mir2_gateway_kick_smoke)

  add_executable(mir2_items_smoke tests/items_smoke.cpp)
  target_link_libraries(mir2_items_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_items_smoke COMMAND mir2_items_smoke)

  add_executable(mir2_item_phase1_compat_smoke tests/item_phase1_compat_smoke.cpp)
  target_link_libraries(mir2_item_phase1_compat_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_item_phase1_compat_smoke COMMAND mir2_item_phase1_compat_smoke)

  add_executable(mir2_item_phase2_ground_smoke tests/item_phase2_ground_smoke.cpp)
  target_link_libraries(mir2_item_phase2_ground_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_item_phase2_ground_smoke COMMAND mir2_item_phase2_ground_smoke)

  add_executable(mir2_item_phase2_use_smoke tests/item_phase2_use_smoke.cpp)
  target_link_libraries(mir2_item_phase2_use_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_item_phase2_use_smoke COMMAND mir2_item_phase2_use_smoke)

  add_executable(mir2_item_phase3_trade_smoke tests/item_phase3_trade_smoke.cpp)
  target_link_libraries(mir2_item_phase3_trade_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_item_phase3_trade_smoke COMMAND mir2_item_phase3_trade_smoke)

  add_executable(mir2_special_consumables_legacy_smoke
      tests/special_consumables_legacy_smoke.cpp)
    target_link_libraries(mir2_special_consumables_legacy_smoke PRIVATE mir2_hostlib)
    add_test(NAME mir2_special_consumables_legacy_smoke
      COMMAND mir2_special_consumables_legacy_smoke)

  add_executable(mir2_repair_smoke tests/repair_smoke.cpp)
  target_link_libraries(mir2_repair_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_repair_smoke COMMAND mir2_repair_smoke)

  add_executable(mir2_sell_smoke tests/sell_smoke.cpp)
  target_link_libraries(mir2_sell_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_sell_smoke COMMAND mir2_sell_smoke)

  add_executable(mir2_buy_smoke tests/buy_smoke.cpp)
  target_link_libraries(mir2_buy_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_buy_smoke COMMAND mir2_buy_smoke)

  add_executable(mir2_merchant_dialog_smoke tests/merchant_dialog_smoke.cpp)
  target_link_libraries(mir2_merchant_dialog_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_merchant_dialog_smoke COMMAND mir2_merchant_dialog_smoke)

  add_executable(mir2_scripted_merchant_smoke tests/scripted_merchant_smoke.cpp)
  target_link_libraries(mir2_scripted_merchant_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_scripted_merchant_smoke COMMAND mir2_scripted_merchant_smoke)

  add_executable(mir2_scripted_npc_dialog_smoke tests/scripted_npc_dialog_smoke.cpp)
  target_link_libraries(mir2_scripted_npc_dialog_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_scripted_npc_dialog_smoke COMMAND mir2_scripted_npc_dialog_smoke)

  add_executable(mir2_scripted_castle_dialog_smoke tests/scripted_castle_dialog_smoke.cpp)
  target_link_libraries(mir2_scripted_castle_dialog_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_scripted_castle_dialog_smoke COMMAND mir2_scripted_castle_dialog_smoke)

  add_executable(mir2_merchant_castle_admin_smoke tests/merchant_castle_admin_smoke.cpp)
  target_link_libraries(mir2_merchant_castle_admin_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_merchant_castle_admin_smoke COMMAND mir2_merchant_castle_admin_smoke)

  add_executable(mir2_guild_castle_menu_smoke tests/guild_castle_menu_smoke.cpp)
  target_link_libraries(mir2_guild_castle_menu_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_guild_castle_menu_smoke COMMAND mir2_guild_castle_menu_smoke)

  add_executable(mir2_guild_castle_business_smoke tests/guild_castle_business_smoke.cpp)
  target_link_libraries(mir2_guild_castle_business_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_guild_castle_business_smoke COMMAND mir2_guild_castle_business_smoke)

  add_executable(mir2_world_castle_refresh_smoke tests/world_castle_refresh_smoke.cpp)
  target_link_libraries(mir2_world_castle_refresh_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_world_castle_refresh_smoke COMMAND mir2_world_castle_refresh_smoke)

  add_executable(mir2_world_castle_push_refresh_smoke tests/world_castle_push_refresh_smoke.cpp)
  target_link_libraries(mir2_world_castle_push_refresh_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_world_castle_push_refresh_smoke COMMAND mir2_world_castle_push_refresh_smoke)

  add_executable(mir2_world_guild_offline_admin_smoke tests/world_guild_offline_admin_smoke.cpp)
  target_link_libraries(mir2_world_guild_offline_admin_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_world_guild_offline_admin_smoke COMMAND mir2_world_guild_offline_admin_smoke)

  add_executable(mir2_world_guild_cross_map_sync_smoke tests/world_guild_cross_map_sync_smoke.cpp)
  target_link_libraries(mir2_world_guild_cross_map_sync_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_world_guild_cross_map_sync_smoke COMMAND mir2_world_guild_cross_map_sync_smoke)

  add_executable(mir2_storage_smoke tests/storage_smoke.cpp)
  target_link_libraries(mir2_storage_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_storage_smoke COMMAND mir2_storage_smoke)

  add_executable(mir2_combat_smoke tests/combat_smoke.cpp)
  target_link_libraries(mir2_combat_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_combat_smoke COMMAND mir2_combat_smoke)

  add_executable(mir2_monster_ai_smoke tests/monster_ai_smoke.cpp)
  target_link_libraries(mir2_monster_ai_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_ai_smoke COMMAND mir2_monster_ai_smoke)

  add_executable(mir2_monster_race_ai_smoke tests/monster_race_ai_smoke.cpp)
  target_link_libraries(mir2_monster_race_ai_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_race_ai_smoke COMMAND mir2_monster_race_ai_smoke)

  add_executable(mir2_monster_special_race_smoke tests/monster_special_race_smoke.cpp)
  target_link_libraries(mir2_monster_special_race_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_special_race_smoke COMMAND mir2_monster_special_race_smoke)

  add_executable(mir2_monster_home_leash_smoke tests/monster_home_leash_smoke.cpp)
  target_link_libraries(mir2_monster_home_leash_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_home_leash_smoke COMMAND mir2_monster_home_leash_smoke)

  add_executable(mir2_monster_death_drop_smoke tests/monster_death_drop_smoke.cpp)
  target_link_libraries(mir2_monster_death_drop_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_monster_death_drop_smoke COMMAND mir2_monster_death_drop_smoke)

  add_executable(mir2_pk_rules_smoke tests/pk_rules_smoke.cpp)
  target_link_libraries(mir2_pk_rules_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_pk_rules_smoke COMMAND mir2_pk_rules_smoke)

  add_executable(mir2_aoe_spell_smoke tests/aoe_spell_smoke.cpp)
  target_link_libraries(mir2_aoe_spell_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_aoe_spell_smoke COMMAND mir2_aoe_spell_smoke)

  add_executable(mir2_debuff_spell_smoke tests/debuff_spell_smoke.cpp)
  target_link_libraries(mir2_debuff_spell_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_debuff_spell_smoke COMMAND mir2_debuff_spell_smoke)

  add_executable(mir2_player_buff_smoke tests/player_buff_smoke.cpp)
  target_link_libraries(mir2_player_buff_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_player_buff_smoke COMMAND mir2_player_buff_smoke)

  add_executable(mir2_player_debuff_smoke tests/player_debuff_smoke.cpp)
  target_link_libraries(mir2_player_debuff_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_player_debuff_smoke COMMAND mir2_player_debuff_smoke)

  add_executable(mir2_player_shield_smoke tests/player_shield_smoke.cpp)
  target_link_libraries(mir2_player_shield_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_player_shield_smoke COMMAND mir2_player_shield_smoke)

  add_executable(mir2_player_movement_smoke tests/player_movement_smoke.cpp)
  target_link_libraries(mir2_player_movement_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_player_movement_smoke COMMAND mir2_player_movement_smoke)

  add_executable(mir2_host_startup_order_smoke tests/host_startup_order_smoke.cpp)
  target_link_libraries(mir2_host_startup_order_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_host_startup_order_smoke COMMAND mir2_host_startup_order_smoke)

  add_executable(mir2_world_tick_cadence_smoke tests/world_tick_cadence_smoke.cpp)
  target_link_libraries(mir2_world_tick_cadence_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_world_tick_cadence_smoke COMMAND mir2_world_tick_cadence_smoke)

  add_executable(mir2_host_status_snapshot_smoke tests/host_status_snapshot_smoke.cpp)
  target_link_libraries(mir2_host_status_snapshot_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_host_status_snapshot_smoke COMMAND mir2_host_status_snapshot_smoke)

  add_executable(mir2_client_v1_protocol_smoke tests/client_v1_protocol_smoke.cpp)
  target_link_libraries(mir2_client_v1_protocol_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_protocol_smoke COMMAND mir2_client_v1_protocol_smoke)

  add_executable(mir2_client_v1_auth_boundary_smoke tests/client_v1_auth_boundary_smoke.cpp)
  target_link_libraries(mir2_client_v1_auth_boundary_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_auth_boundary_smoke COMMAND mir2_client_v1_auth_boundary_smoke)

  add_executable(mir2_client_v1_account_state_smoke
    tests/client_v1_account_state_smoke.cpp)
  target_link_libraries(mir2_client_v1_account_state_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_account_state_smoke
    COMMAND mir2_client_v1_account_state_smoke)

  add_executable(mir2_client_v1_lobby_character_state_smoke
    tests/client_v1_lobby_character_state_smoke.cpp)
  target_link_libraries(mir2_client_v1_lobby_character_state_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_lobby_character_state_smoke
    COMMAND mir2_client_v1_lobby_character_state_smoke)

  add_executable(mir2_client_v1_enter_map_persistence_smoke
    tests/client_v1_enter_map_persistence_smoke.cpp)
  target_link_libraries(mir2_client_v1_enter_map_persistence_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_enter_map_persistence_smoke
    COMMAND mir2_client_v1_enter_map_persistence_smoke)

  add_executable(mir2_client_v1_enter_world_boundary_smoke
    tests/client_v1_enter_world_boundary_smoke.cpp)
  target_link_libraries(mir2_client_v1_enter_world_boundary_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_enter_world_boundary_smoke
    COMMAND mir2_client_v1_enter_world_boundary_smoke)

  add_executable(mir2_client_v1_session_boundary_smoke
    tests/client_v1_session_boundary_smoke.cpp)
  target_link_libraries(mir2_client_v1_session_boundary_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_session_boundary_smoke
    COMMAND mir2_client_v1_session_boundary_smoke)

  add_executable(mir2_client_v1_gateway_lifecycle_smoke
    tests/client_v1_gateway_lifecycle_smoke.cpp)
  target_link_libraries(mir2_client_v1_gateway_lifecycle_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_gateway_lifecycle_smoke
    COMMAND mir2_client_v1_gateway_lifecycle_smoke)

  add_executable(mir2_client_v1_game_command_bridge_smoke
    tests/client_v1_game_command_bridge_smoke.cpp)
  target_link_libraries(mir2_client_v1_game_command_bridge_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_game_command_bridge_smoke
    COMMAND mir2_client_v1_game_command_bridge_smoke)

  add_executable(mir2_client_v1_magic_gateway_smoke tests/client_v1_magic_gateway_smoke.cpp)
  target_link_libraries(mir2_client_v1_magic_gateway_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_magic_gateway_smoke COMMAND mir2_client_v1_magic_gateway_smoke)

  add_executable(mir2_client_v1_world_entry_smoke tests/client_v1_world_entry_smoke.cpp)
  target_link_libraries(mir2_client_v1_world_entry_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_world_entry_smoke COMMAND mir2_client_v1_world_entry_smoke)

  add_executable(mir2_client_v1_game_gateway_notice_smoke tests/client_v1_game_gateway_notice_smoke.cpp)
  target_link_libraries(mir2_client_v1_game_gateway_notice_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_game_gateway_notice_smoke COMMAND mir2_client_v1_game_gateway_notice_smoke)

  add_executable(mir2_client_v1_login_to_world_flow_smoke tests/client_v1_login_to_world_flow_smoke.cpp)
  target_link_libraries(mir2_client_v1_login_to_world_flow_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_login_to_world_flow_smoke COMMAND mir2_client_v1_login_to_world_flow_smoke)

  add_executable(mir2_client_v1_imported_character_flow_smoke
    tests/client_v1_imported_character_flow_smoke.cpp)
  target_link_libraries(mir2_client_v1_imported_character_flow_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_imported_character_flow_smoke
    COMMAND mir2_client_v1_imported_character_flow_smoke)

  add_executable(mir2_client_v1_world_min_loop_smoke
    tests/client_v1_world_min_loop_smoke.cpp)
  target_link_libraries(mir2_client_v1_world_min_loop_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_world_min_loop_smoke
    COMMAND mir2_client_v1_world_min_loop_smoke)

  add_executable(mir2_client_v1_inventory_npc_smoke tests/client_v1_inventory_npc_smoke.cpp)
  target_link_libraries(mir2_client_v1_inventory_npc_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_inventory_npc_smoke COMMAND mir2_client_v1_inventory_npc_smoke)

  add_executable(mir2_client_v1_host_runtime_flow_smoke tests/client_v1_host_runtime_flow_smoke.cpp)
  target_link_libraries(mir2_client_v1_host_runtime_flow_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_client_v1_host_runtime_flow_smoke COMMAND mir2_client_v1_host_runtime_flow_smoke)

  add_executable(mir2_config_gateway_flags_smoke tests/config_gateway_flags_smoke.cpp)
  target_link_libraries(mir2_config_gateway_flags_smoke PRIVATE mir2_hostlib)
  add_test(NAME mir2_config_gateway_flags_smoke COMMAND mir2_config_gateway_flags_smoke)
endif()
