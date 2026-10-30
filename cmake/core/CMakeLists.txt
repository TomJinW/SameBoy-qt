# Keep the upstream submodule untouched; its expression parser also powers cheat search.
set(core_dir "${PROJECT_SOURCE_DIR}/SameBoy/Core")
if(NOT EXISTS "${core_dir}/gb.c")
    message(FATAL_ERROR "Initialize SameBoy first: git submodule update --init --recursive")
endif()
set(core_sources apu camera cheats cheat_search debugger display gb joypad mbc memory printer random
    rewind rumble save_state sgb sm83_cpu sm83_disassembler symbol_hash timing workboy)
list(TRANSFORM core_sources PREPEND "${core_dir}/")
list(TRANSFORM core_sources APPEND ".c")
add_library(sameboy-core STATIC ${core_sources})
set_target_properties(sameboy-core PROPERTIES AUTOMOC OFF)
target_include_directories(sameboy-core PUBLIC "${PROJECT_SOURCE_DIR}/SameBoy")
target_compile_definitions(sameboy-core PRIVATE GB_INTERNAL _GNU_SOURCE _USE_MATH_DEFINES
    GB_VERSION="1.0.4" GB_COPYRIGHT_YEAR="2026")
if(NOT WIN32)
    target_link_libraries(sameboy-core PUBLIC m)
endif()
if(MSVC)
    target_sources(sameboy-core PRIVATE windows_compat.c)
    target_compile_options(sameboy-core PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/windows_compat.h")
    target_compile_definitions(sameboy-core PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
endif()

target_sources(sameboy-core PRIVATE debug_memory.c)

target_sources(sameboy-core PRIVATE debugger_frontend.c)
