function(add_condemnedvr_test target source test_name)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE fearvr-common fearvr-warnings)
  # Several small tests use assert; keep assertions active in
  # RelWithDebInfo, where MSVC would otherwise define NDEBUG.
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  endif()
  add_test(NAME ${test_name} COMMAND ${target})
endfunction()

add_condemnedvr_test(test_protocol
  test_protocol.cpp protocol)
add_condemnedvr_test(test_xr_session_state
  test_xr_session_state.cpp xr_session_state)
add_condemnedvr_test(test_stereo_math
  test_stereo_math.cpp stereo_math)
add_condemnedvr_test(test_head_tracking_math
  test_head_tracking_math.cpp head_tracking_math)
add_condemnedvr_test(test_arm_ik
  test_arm_ik.cpp arm_ik)
add_condemnedvr_test(test_condemned_arm_ik_lifecycle
  test_condemned_arm_ik_lifecycle.cpp condemned_arm_ik_lifecycle)
add_condemnedvr_test(test_stereo_hud_math
  test_stereo_hud_math.cpp stereo_hud_math)
add_condemnedvr_test(test_input_state
  test_input_state.cpp input_state)
add_condemnedvr_test(test_render_scale
  test_render_scale.cpp render_scale)
add_condemnedvr_test(test_weapon_weight
  test_weapon_weight.cpp weapon_weight)
add_condemnedvr_test(test_condemned_combat_diagnostics
  test_condemned_combat_diagnostics.cpp condemned_combat_diagnostics)
add_condemnedvr_test(test_condemned_physical_melee
  test_condemned_physical_melee.cpp condemned_physical_melee)
add_condemnedvr_test(test_condemned_block_pose
  test_condemned_block_pose.cpp condemned_block_pose)
add_condemnedvr_test(test_condemned_tool_menu
  test_condemned_tool_menu.cpp condemned_tool_menu)
add_condemnedvr_test(test_condemned_interaction_authoring
  test_condemned_interaction_authoring.cpp condemned_interaction_authoring)
add_condemnedvr_test(test_condemned_player_collision
  test_condemned_player_collision.cpp condemned_player_collision)
add_condemnedvr_test(test_condemned_menu_input
  test_condemned_menu_input.cpp condemned_menu_input)
add_condemnedvr_test(test_condemned_retail_menu
  test_condemned_retail_menu.cpp condemned_retail_menu)
add_condemnedvr_test(test_mono_panel_anchor
  test_mono_panel_anchor.cpp mono_panel_anchor)
add_condemnedvr_test(test_loader_event_format
  test_loader_event_format.cpp loader_event_format)
add_condemnedvr_test(test_condemned_background_render
  test_condemned_background_render.cpp condemned_background_render)
