cmake_minimum_required(VERSION 3.21)
project(SimplePaintCopyConsumer LANGUAGES CXX)

# This fixture is copied alongside ONLY the SimplePaint folder before configure.
# It must not include, link, or compile any other source from the application.
add_subdirectory(SimplePaint)
add_executable(SimplePaintCopyConsumer Main.cpp)
target_link_libraries(SimplePaintCopyConsumer PRIVATE SimplePaint)

find_program(DXC_EXECUTABLE NAMES dxc.exe dxc REQUIRED)
foreach(count IN ITEMS default 3)
    set(definitions)
    if(NOT count STREQUAL "default")
        set(definitions -D "SIMPLE_PAINT_MATERIAL_COUNT=${count}")
    endif()
    foreach(stage IN ITEMS VS PS)
        string(TOLOWER "${stage}" profile)
        set(output "${CMAKE_CURRENT_BINARY_DIR}/Paint${stage}-${count}.dxil")
        add_custom_command(OUTPUT "${output}"
            COMMAND "${DXC_EXECUTABLE}" -E "${stage}Main" -T "${profile}_6_0"
                -O3 -Ges -WX ${definitions} -Fo "${output}"
                "${CMAKE_CURRENT_SOURCE_DIR}/SimplePaint/SimplePaint.hlsl"
            DEPENDS SimplePaint/SimplePaint.hlsl SimplePaint/SimplePaintCore.hlsli
            VERBATIM)
        list(APPEND shaders "${output}")
    endforeach()
endforeach()
add_custom_target(CopiedShaderChecks ALL DEPENDS ${shaders})
enable_testing()
add_test(NAME CopiedSimplePaint COMMAND SimplePaintCopyConsumer)
