# Pinned, unmodified nlohmann/json 3.12.0 single header; see README.md.
add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(nlohmann_json INTERFACE cxx_std_11)
target_compile_definitions(nlohmann_json INTERFACE JSON_USE_IMPLICIT_CONVERSIONS=0)
