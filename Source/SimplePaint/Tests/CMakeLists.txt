# Tests own their shader outputs and use only the copied module and Windows SDK.
find_program(DXC_EXECUTABLE NAMES dxc.exe dxc REQUIRED)
set(SHADER_HEADERS)
set(SHADER_GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Generated")
foreach(stage IN ITEMS VS PS)
    string(TOLOWER "${stage}" profile)
    if(stage STREQUAL "VS")
        set(variable g_simplePaintVertexShader)
    else()
        set(variable g_simplePaintPixelShader)
    endif()
    set(header "${SHADER_GENERATED_DIRECTORY}/SimplePaint${stage}.h")
    add_custom_command(OUTPUT "${header}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SHADER_GENERATED_DIRECTORY}"
        COMMAND "${DXC_EXECUTABLE}" -E "${stage}Main" -T "${profile}_6_0"
            -O3 -Ges -WX -all_resources_bound -Qstrip_debug -Qstrip_reflect
            -D SIMPLE_PAINT_MATERIAL_COUNT=6 -Fh "${header}" -Vn "${variable}"
            -Fo "${SHADER_GENERATED_DIRECTORY}/SimplePaint${stage}.dxil"
            "${CMAKE_CURRENT_SOURCE_DIR}/../SimplePaint.hlsl"
        DEPENDS ../SimplePaint.hlsl ../SimplePaintCore.hlsli VERBATIM)
    list(APPEND SHADER_HEADERS "${header}")
endforeach()
add_custom_target(SimplePaintTestShaders DEPENDS ${SHADER_HEADERS})
add_test(NAME SimplePaintStandalone
    COMMAND "${CMAKE_COMMAND}"
        "-DMODULE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.."
        "-DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/SimplePaintStandalone"
        "-DSMOKE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/SimplePaintCopyCheck"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "-DCONFIGURATION=$<CONFIG>"
        "-DDXC_EXECUTABLE=${DXC_EXECUTABLE}"
        "-DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/VerifySimplePaintStandalone.cmake")
set_tests_properties(SimplePaintStandalone PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
add_executable(SimplePaintTests SimplePaintTests.cpp)
target_link_libraries(SimplePaintTests PRIVATE SimplePaint)
add_test(NAME SimplePaintContract COMMAND SimplePaintTests)
add_executable(SimplePaintGpuTests SimplePaintGpuTests.cpp ${SHADER_HEADERS})
add_dependencies(SimplePaintGpuTests SimplePaintTestShaders)
target_include_directories(SimplePaintGpuTests PRIVATE "${SHADER_GENERATED_DIRECTORY}")
target_compile_definitions(SimplePaintGpuTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(SimplePaintGpuTests PRIVATE SimplePaint d3d12 dxgi)
if(MSVC)
    target_compile_options(SimplePaintTests PRIVATE /W4 /WX /EHsc /fp:precise)
    target_compile_options(SimplePaintGpuTests PRIVATE /W4 /WX /EHsc /fp:precise)
endif()
add_test(NAME SimplePaintGpuHardware COMMAND SimplePaintGpuTests)
add_test(NAME SimplePaintGpuWarp COMMAND SimplePaintGpuTests --warp)
set_tests_properties(SimplePaintGpuHardware SimplePaintGpuWarp PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

add_executable(OrthographicTransformTests OrthographicTransformTests.cpp)
target_link_libraries(OrthographicTransformTests PRIVATE SimplePaint)
add_test(NAME OrthographicTransforms COMMAND OrthographicTransformTests)
