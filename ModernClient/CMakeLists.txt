cmake_minimum_required(VERSION 3.24)

project(ModernMir2Client VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /bigobj)
endif()

if(WIN32)
  add_compile_definitions(_WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

enable_testing()

add_library(modern_client_headers INTERFACE)
target_include_directories(modern_client_headers
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_library(modern_client_core STATIC
  src/app/client_app.cpp
  src/audio/direct_sound_audio_backend.cpp
  src/audio/audio_id_mapping.cpp
  src/audio/legacy_audio_cue_tracker.cpp
  src/audio/legacy_sound_rules.cpp
  src/audio/audio_service.cpp
  src/audio/null_audio_backend.cpp
  src/audio/wav_reader.cpp
  src/animation/legacy_animation.cpp
  src/assets/asset_manager.cpp
  src/platform/win32_window.cpp
  src/render/software_renderer.cpp
  src/protocol/protocol_client.cpp
  src/ui/ui.cpp
  src/ui/legacy_ui.cpp
  src/scene/legacy_auth_ui.cpp
  src/scene/legacy_play_ui.cpp
  src/scene/scenes.cpp
)

target_link_libraries(modern_client_core
  PUBLIC
    modern_client_headers
    d3d11
    dxgi
    d3dcompiler
    gdi32
    user32
    ws2_32
    dsound
    dxguid
    winmm
)

add_executable(modern_mir2_client WIN32 src/app/main.cpp)
target_link_libraries(modern_mir2_client PRIVATE modern_client_core)

function(add_modern_client_header_smoke target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE modern_client_headers)
  add_test(NAME ${target} COMMAND ${target})
endfunction()

function(add_modern_client_core_smoke target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE modern_client_core)
  add_test(NAME ${target} COMMAND ${target})
endfunction()

add_modern_client_core_smoke(modern_client_asset_smoke tests/asset_smoke.cpp)
add_modern_client_header_smoke(modern_client_protocol_map_smoke tests/protocol_map_smoke.cpp)
add_modern_client_header_smoke(modern_client_flow_smoke tests/client_flow_smoke.cpp)
add_modern_client_header_smoke(modern_client_text_encoding_smoke tests/text_encoding_smoke.cpp)
add_modern_client_header_smoke(modern_client_item_pending_smoke tests/item_pending_smoke.cpp)
add_modern_client_header_smoke(modern_client_character_select_smoke tests/character_select_smoke.cpp)
add_modern_client_core_smoke(modern_client_movement_smoke tests/movement_smoke.cpp)
add_modern_client_header_smoke(modern_client_movement_action_gate_smoke tests/movement_action_gate_smoke.cpp)
add_modern_client_header_smoke(modern_client_legacy_move_decision_smoke tests/legacy_move_decision_smoke.cpp)
add_modern_client_header_smoke(modern_client_legacy_attack_timing_smoke tests/legacy_attack_timing_smoke.cpp)
add_modern_client_core_smoke(modern_client_legacy_animation_smoke tests/legacy_animation_smoke.cpp)
add_modern_client_core_smoke(modern_client_legacy_animation_replay_golden_smoke tests/legacy_animation_replay_golden_smoke.cpp)
add_modern_client_core_smoke(modern_client_map_render_alignment_smoke tests/map_render_alignment_smoke.cpp)
add_modern_client_core_smoke(modern_client_world_render_smoke tests/world_render_smoke.cpp)
add_modern_client_core_smoke(modern_client_combat_presentation_smoke tests/combat_presentation_smoke.cpp)
add_modern_client_header_smoke(modern_client_legacy_actor_event_semantics_smoke tests/legacy_actor_event_semantics_smoke.cpp)
add_modern_client_core_smoke(modern_client_ui_smoke tests/ui_smoke.cpp)
add_modern_client_header_smoke(modern_client_frame_order_smoke tests/client_app_frame_order_smoke.cpp)
add_modern_client_header_smoke(modern_client_scene_trace_golden_smoke tests/scene_trace_golden_smoke.cpp)
target_compile_definitions(modern_client_scene_trace_golden_smoke
  PRIVATE MIR2_CLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_modern_client_header_smoke(modern_client_legacy_ui_expected_trace_smoke tests/legacy_ui_expected_trace_smoke.cpp)
target_compile_definitions(modern_client_legacy_ui_expected_trace_smoke
  PRIVATE MIR2_CLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_modern_client_core_smoke(modern_client_legacy_ui_manager_smoke tests/legacy_ui_manager_smoke.cpp)
target_compile_definitions(modern_client_legacy_ui_manager_smoke
  PRIVATE MIR2_CLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_modern_client_core_smoke(modern_client_legacy_ui_paint_layers_smoke tests/legacy_ui_paint_layers_smoke.cpp)
target_compile_definitions(modern_client_legacy_ui_paint_layers_smoke
  PRIVATE MIR2_CLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_modern_client_core_smoke(modern_client_legacy_ui_input_trace_smoke tests/legacy_ui_input_trace_smoke.cpp)
target_compile_definitions(modern_client_legacy_ui_input_trace_smoke
  PRIVATE MIR2_CLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_modern_client_core_smoke(modern_client_legacy_auth_ui_trace_smoke tests/legacy_auth_ui_trace_smoke.cpp)
target_compile_definitions(modern_client_legacy_auth_ui_trace_smoke
  PRIVATE MIR2_CLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_modern_client_core_smoke(modern_client_legacy_auth_ui_layout_smoke tests/legacy_auth_ui_layout_smoke.cpp)
add_modern_client_core_smoke(modern_client_legacy_play_ui_trace_smoke tests/legacy_play_ui_trace_smoke.cpp)
target_compile_definitions(modern_client_legacy_play_ui_trace_smoke
  PRIVATE MIR2_CLIENT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_modern_client_core_smoke(modern_client_legacy_play_ui_layout_smoke tests/legacy_play_ui_layout_smoke.cpp)
add_modern_client_header_smoke(modern_client_scene_state_fuzz_smoke tests/scene_state_fuzz_smoke.cpp)
add_modern_client_core_smoke(modern_client_ui_capture_process_order_smoke tests/ui_capture_process_order_smoke.cpp)
add_modern_client_core_smoke(modern_client_world_scene_legacy_order_smoke tests/world_scene_legacy_order_smoke.cpp)
add_modern_client_core_smoke(modern_client_inventory_ui_smoke tests/inventory_ui_smoke.cpp)
add_modern_client_core_smoke(modern_client_chat_magic_ui_smoke tests/chat_magic_ui_smoke.cpp)
add_modern_client_core_smoke(modern_client_trade_group_guild_ui_smoke tests/trade_group_guild_ui_smoke.cpp)
add_modern_client_core_smoke(modern_client_audio_mapping_smoke tests/audio_mapping_smoke.cpp)
add_modern_client_core_smoke(modern_client_wav_reader_smoke tests/wav_reader_smoke.cpp)
add_modern_client_core_smoke(modern_client_direct_sound_backend_smoke tests/direct_sound_backend_smoke.cpp)
add_modern_client_core_smoke(modern_client_scene_audio_smoke tests/scene_audio_smoke.cpp)
add_modern_client_core_smoke(modern_client_audio_service_smoke tests/audio_service_smoke.cpp)
add_modern_client_core_smoke(modern_client_legacy_sound_rules_smoke tests/legacy_sound_rules_smoke.cpp)
add_modern_client_core_smoke(modern_client_legacy_audio_cue_tracker_smoke tests/legacy_audio_cue_tracker_smoke.cpp)


add_custom_target(modern_client_ci_phase2_fast
  DEPENDS
    modern_mir2_client
    modern_client_asset_smoke
    modern_client_protocol_map_smoke
    modern_client_flow_smoke
    modern_client_text_encoding_smoke
    modern_client_movement_smoke
    modern_client_movement_action_gate_smoke
    modern_client_legacy_move_decision_smoke
    modern_client_legacy_attack_timing_smoke
    modern_client_item_pending_smoke
    modern_client_frame_order_smoke
    modern_client_scene_trace_golden_smoke
    modern_client_legacy_ui_expected_trace_smoke
    modern_client_legacy_ui_manager_smoke
    modern_client_legacy_ui_paint_layers_smoke
    modern_client_legacy_ui_input_trace_smoke
    modern_client_legacy_auth_ui_trace_smoke
    modern_client_legacy_auth_ui_layout_smoke
    modern_client_legacy_play_ui_trace_smoke
    modern_client_legacy_play_ui_layout_smoke
    modern_client_scene_state_fuzz_smoke
    modern_client_ui_capture_process_order_smoke
    modern_client_world_scene_legacy_order_smoke
    modern_client_inventory_ui_smoke
    modern_client_chat_magic_ui_smoke
    modern_client_trade_group_guild_ui_smoke
    modern_client_character_select_smoke
    modern_client_legacy_animation_smoke
    modern_client_legacy_animation_replay_golden_smoke
    modern_client_legacy_actor_event_semantics_smoke
    modern_client_world_render_smoke
    modern_client_combat_presentation_smoke
)
