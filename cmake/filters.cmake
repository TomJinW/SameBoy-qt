find_program(SAMEBOY_QSB NAMES qsb qsb6 HINTS "${QT_HOST_PATH}/bin" "${QT6_INSTALL_PREFIX}/bin" "/usr/lib/qt6/bin" REQUIRED)
set(sameboy_filter_keys NearestNeighbor Bilinear LCD Pixelated SmoothBilinear MonoLCD CRT FlatCRT
    Scale2x Scale4x AAScale2x AAScale4x HQ2x OmniScale OmniScaleLegacy AAOmniScaleLegacy)
set(sameboy_shader_dir "${PROJECT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${sameboy_shader_dir}")
set(sameboy_shader_outputs)
foreach(key IN LISTS sameboy_filter_keys)
    set(source "${PROJECT_SOURCE_DIR}/SameBoy/Shaders/${key}.fsh")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" FILTER_SOURCE)
    set(fragment "${sameboy_shader_dir}/${key}.frag")
    configure_file("${PROJECT_SOURCE_DIR}/resources/shaders/filter.frag.in" "${fragment}" @ONLY)
    add_custom_command(OUTPUT "${fragment}.qsb"
        COMMAND "${SAMEBOY_QSB}" --glsl 150 --hlsl 50 --msl 12 -o "${fragment}.qsb" "${fragment}"
        DEPENDS "${fragment}" "${source}" VERBATIM)
    list(APPEND sameboy_shader_outputs "${fragment}.qsb")
endforeach()
configure_file("${PROJECT_SOURCE_DIR}/resources/shaders/quad.vert" "${sameboy_shader_dir}/quad.vert" COPYONLY)
add_custom_command(OUTPUT "${sameboy_shader_dir}/quad.vert.qsb"
    COMMAND "${SAMEBOY_QSB}" --glsl 150 --hlsl 50 --msl 12 -o "${sameboy_shader_dir}/quad.vert.qsb" "${sameboy_shader_dir}/quad.vert"
    DEPENDS "${sameboy_shader_dir}/quad.vert" VERBATIM)
list(APPEND sameboy_shader_outputs "${sameboy_shader_dir}/quad.vert.qsb")
add_custom_target(sameboy-shaders DEPENDS ${sameboy_shader_outputs})
function(sameboy_add_filters target)
    target_sources(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src/filterrenderer.cpp")
    target_link_libraries(${target} PRIVATE Qt6::GuiPrivate)
    add_dependencies(${target} sameboy-shaders)
    qt_add_resources(${target} filters PREFIX "/filters" BASE "${sameboy_shader_dir}" FILES ${sameboy_shader_outputs})
endfunction()
