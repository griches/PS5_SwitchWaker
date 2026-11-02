# PS5-owned code: explicit membership, compiled with the existing game adapter target.
function(ps5_attach_game_features)
    # build.sh writes the menu version header alongside package metadata.
    # A new build number changes one object, not the CMake configuration.
    set_property(SOURCE "${PS5_PORT_DIR}/ps5/src/presentation/pc_menu_widgets.cpp"
        APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}")
    target_include_directories(cos_game_headers INTERFACE "${PS5_PORT_DIR}/ps5/include")
    if(NOT COS_RUNTIME_REGION OR NOT COS_VERSION EQUAL 3 OR NOT COS_GAME_ID STREQUAL "GZLP01")
        message(FATAL_ERROR "PS5 requires the universal PAL-layout configuration; use ps5/tools/build.sh")
    endif()
    target_compile_definitions(cos_game_headers INTERFACE COS_RUNTIME_REGION=1)
    # Keep upstream's Switch/desktop menu untouched; compile the PS5 presentation instead.
    get_target_property(ps5_pc_sources cos_pc SOURCES)
    list(FILTER ps5_pc_sources EXCLUDE REGEX "/features/pc_(menu|overlay)[.]cpp$")
    # No updater or controller-remapping product/test path is used on PS5.
    list(FILTER ps5_pc_sources EXCLUDE REGEX "/features/pc_(update|update_core|button_remap)[.]cpp$")
    list(FILTER ps5_pc_sources EXCLUDE REGEX "/harness/smokes/pc_button_remap[.]cpp$")
    set_property(TARGET cos_pc PROPERTY SOURCES "${ps5_pc_sources}")
    target_sources(cos_pc PRIVATE
        "${PS5_PORT_DIR}/ps5/src/integration/pc_disc_region.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_menu.cpp"
        "${PS5_PORT_DIR}/ps5/src/integration/pc_menu_smoke.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_menu_language.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_overlay.cpp"
        "${PS5_PORT_DIR}/ps5/src/features/pc_camera_controls.cpp"
        "${PS5_PORT_DIR}/ps5/src/features/pc_gameplay_options.cpp"
        "${PS5_PORT_DIR}/ps5/src/features/pc_player_controls.cpp"
        "${PS5_PORT_DIR}/ps5/src/game_hooks/pc_camera_controls.cpp"
        "${PS5_PORT_DIR}/ps5/src/game_hooks/pc_player_controls.cpp"
        "${PS5_PORT_DIR}/ps5/src/game_hooks/pc_distant_islands.cpp"
        "${PS5_PORT_DIR}/ps5/src/game_hooks/pc_fast_load.cpp"
        "${PS5_PORT_DIR}/ps5/src/game_hooks/pc_haptics.cpp"
        "${PS5_PORT_DIR}/ps5/src/game_hooks/pc_hd_visuals.cpp"
        "${PS5_PORT_DIR}/ps5/src/game_hooks/pc_quick_doors.cpp"
        "${PS5_PORT_DIR}/ps5/src/integration/pc_ps5_features.cpp"
        "${PS5_PORT_DIR}/ps5/src/integration/pc_ui_assets.cpp"
        "${PS5_PORT_DIR}/ps5/src/integration/pc_ui_transform.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_menu_widgets.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_shader_loading.cpp"
    )
    # Header/compile settings only; Aurora already owns the linked ImGui library.
    set_property(SOURCE "${PS5_PORT_DIR}/ps5/src/presentation/pc_menu.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_overlay.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_menu_widgets.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_shader_loading.cpp"
        APPEND PROPERTY INCLUDE_DIRECTORIES "$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>")
    set_property(SOURCE "${PS5_PORT_DIR}/ps5/src/presentation/pc_menu.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_overlay.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_menu_widgets.cpp"
        "${PS5_PORT_DIR}/ps5/src/presentation/pc_shader_loading.cpp"
        APPEND PROPERTY COMPILE_DEFINITIONS "$<TARGET_PROPERTY:imgui,INTERFACE_COMPILE_DEFINITIONS>")
endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL ps5_attach_game_features)
