cmake_minimum_required(VERSION 3.24)

project(ModernMir2Client VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(modern_mir2_client WIN32
  src/app/main.cpp
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
  src/scene/scenes.cpp
)

target_include_directories(modern_mir2_client
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(modern_mir2_client
  PRIVATE
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

add_executable(modern_client_asset_smoke
  src/assets/asset_manager.cpp
  tests/asset_smoke.cpp
)

target_include_directories(modern_client_asset_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME modern_client_asset_smoke COMMAND modern_client_asset_smoke)

add_executable(modern_client_protocol_map_smoke
  tests/protocol_map_smoke.cpp
)

target_include_directories(modern_client_protocol_map_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME modern_client_protocol_map_smoke COMMAND modern_client_protocol_map_smoke)

add_executable(modern_client_flow_smoke
  tests/client_flow_smoke.cpp
)

target_include_directories(modern_client_flow_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME modern_client_flow_smoke COMMAND modern_client_flow_smoke)

add_executable(modern_client_text_encoding_smoke
  tests/text_encoding_smoke.cpp
)

target_include_directories(modern_client_text_encoding_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME modern_client_text_encoding_smoke COMMAND modern_client_text_encoding_smoke)

add_executable(modern_client_item_pending_smoke
  tests/item_pending_smoke.cpp
)

target_include_directories(modern_client_item_pending_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME modern_client_item_pending_smoke COMMAND modern_client_item_pending_smoke)

add_executable(modern_client_character_select_smoke
  tests/character_select_smoke.cpp
)

target_include_directories(modern_client_character_select_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME modern_client_character_select_smoke COMMAND modern_client_character_select_smoke)

add_executable(modern_client_movement_smoke
  src/assets/asset_manager.cpp
  tests/movement_smoke.cpp
)

target_include_directories(modern_client_movement_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME modern_client_movement_smoke COMMAND modern_client_movement_smoke)

add_executable(modern_client_legacy_animation_smoke
  src/animation/legacy_animation.cpp
  src/assets/asset_manager.cpp
  src/render/software_renderer.cpp
  tests/legacy_animation_smoke.cpp
)

target_include_directories(modern_client_legacy_animation_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(modern_client_legacy_animation_smoke
  PRIVATE
    d3d11
    dxgi
    d3dcompiler
    gdi32
    user32
)

add_test(NAME modern_client_legacy_animation_smoke COMMAND modern_client_legacy_animation_smoke)

add_executable(modern_client_map_render_alignment_smoke
  src/animation/legacy_animation.cpp
  src/assets/asset_manager.cpp
  src/render/software_renderer.cpp
  tests/map_render_alignment_smoke.cpp
)

target_include_directories(modern_client_map_render_alignment_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(modern_client_map_render_alignment_smoke
  PRIVATE
    d3d11
    dxgi
    d3dcompiler
    gdi32
    user32
)

add_test(NAME modern_client_map_render_alignment_smoke COMMAND modern_client_map_render_alignment_smoke)

add_executable(modern_client_ui_smoke
  src/ui/ui.cpp
  src/assets/asset_manager.cpp
  src/render/software_renderer.cpp
  tests/ui_smoke.cpp
)

target_include_directories(modern_client_ui_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(modern_client_ui_smoke
  PRIVATE
    d3d11
    dxgi
    d3dcompiler
    gdi32
    user32
)

add_test(NAME modern_client_ui_smoke COMMAND modern_client_ui_smoke)

add_executable(modern_client_audio_mapping_smoke
  src/audio/audio_id_mapping.cpp
  tests/audio_mapping_smoke.cpp
)

target_include_directories(modern_client_audio_mapping_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME modern_client_audio_mapping_smoke COMMAND modern_client_audio_mapping_smoke)

add_executable(modern_client_wav_reader_smoke
  src/audio/wav_reader.cpp
  tests/wav_reader_smoke.cpp
)

target_include_directories(modern_client_wav_reader_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

add_test(NAME modern_client_wav_reader_smoke COMMAND modern_client_wav_reader_smoke)

add_executable(modern_client_direct_sound_backend_smoke
  src/audio/direct_sound_audio_backend.cpp
  src/audio/null_audio_backend.cpp
  src/audio/wav_reader.cpp
  tests/direct_sound_backend_smoke.cpp
)

target_include_directories(modern_client_direct_sound_backend_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(modern_client_direct_sound_backend_smoke
  PRIVATE
    dsound
    dxguid
    winmm
)

add_test(NAME modern_client_direct_sound_backend_smoke
         COMMAND modern_client_direct_sound_backend_smoke)

add_executable(modern_client_scene_audio_smoke
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
  src/scene/scenes.cpp
  tests/scene_audio_smoke.cpp
)

target_include_directories(modern_client_scene_audio_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(modern_client_scene_audio_smoke
  PRIVATE
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

add_test(NAME modern_client_scene_audio_smoke COMMAND modern_client_scene_audio_smoke)

add_executable(modern_client_audio_service_smoke
  src/audio/direct_sound_audio_backend.cpp
  src/audio/audio_id_mapping.cpp
  src/audio/audio_service.cpp
  src/audio/null_audio_backend.cpp
  src/audio/wav_reader.cpp
  tests/audio_service_smoke.cpp
)

target_include_directories(modern_client_audio_service_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(modern_client_audio_service_smoke
  PRIVATE
    dsound
    dxguid
    winmm
)

add_test(NAME modern_client_audio_service_smoke COMMAND modern_client_audio_service_smoke)

add_executable(modern_client_legacy_sound_rules_smoke
  src/audio/legacy_sound_rules.cpp
  src/animation/legacy_animation.cpp
  src/assets/asset_manager.cpp
  src/render/software_renderer.cpp
  tests/legacy_sound_rules_smoke.cpp
)

target_include_directories(modern_client_legacy_sound_rules_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(modern_client_legacy_sound_rules_smoke
  PRIVATE
    d3d11
    dxgi
    d3dcompiler
    gdi32
    user32
)

add_test(NAME modern_client_legacy_sound_rules_smoke
         COMMAND modern_client_legacy_sound_rules_smoke)

add_executable(modern_client_legacy_audio_cue_tracker_smoke
  src/audio/legacy_audio_cue_tracker.cpp
  src/audio/legacy_sound_rules.cpp
  src/audio/direct_sound_audio_backend.cpp
  src/audio/audio_id_mapping.cpp
  src/audio/audio_service.cpp
  src/audio/null_audio_backend.cpp
  src/audio/wav_reader.cpp
  src/animation/legacy_animation.cpp
  src/assets/asset_manager.cpp
  src/render/software_renderer.cpp
  tests/legacy_audio_cue_tracker_smoke.cpp
)

target_include_directories(modern_client_legacy_audio_cue_tracker_smoke
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(modern_client_legacy_audio_cue_tracker_smoke
  PRIVATE
    d3d11
    dxgi
    d3dcompiler
    gdi32
    user32
    dsound
    dxguid
    winmm
)

add_test(NAME modern_client_legacy_audio_cue_tracker_smoke
         COMMAND modern_client_legacy_audio_cue_tracker_smoke)
