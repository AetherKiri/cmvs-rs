cmake_minimum_required(VERSION 3.19)
project(aether_cmvs_runtime LANGUAGES CXX)
if(NOT APPLE)
    message(FATAL_ERROR "The CMVS Aether provider currently supports macOS and iOS")
endif()
get_filename_component(CMVS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NOT AETHERKIRI_CMVS_ABI_INCLUDE)
    set(AETHERKIRI_CMVS_ABI_INCLUDE "${CMAKE_SOURCE_DIR}/abi/include")
endif()
find_program(CMVS_CARGO cargo REQUIRED)
find_program(CMVS_RUSTC rustc REQUIRED)
set(cmvs_arch "${CMAKE_OSX_ARCHITECTURES}")
if(NOT cmvs_arch)
    set(cmvs_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
if(cmvs_arch STREQUAL "arm64" OR cmvs_arch STREQUAL "aarch64")
    set(cmvs_cpu aarch64)
elseif(cmvs_arch STREQUAL "x86_64")
    set(cmvs_cpu x86_64)
else()
    message(FATAL_ERROR "Build CMVS once per Apple architecture: ${cmvs_arch}")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    string(TOLOWER "${CMAKE_OSX_SYSROOT}" cmvs_sdk)
    if(cmvs_sdk MATCHES "iphonesimulator" AND cmvs_cpu STREQUAL "aarch64")
        set(cmvs_target aarch64-apple-ios-sim)
    else()
        set(cmvs_target "${cmvs_cpu}-apple-ios")
    endif()
else()
    set(cmvs_target "${cmvs_cpu}-apple-darwin")
endif()
set(AETHERKIRI_CMVS_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo-target" CACHE PATH "CMVS Cargo build cache")
set(cmvs_library "${AETHERKIRI_CMVS_CARGO_TARGET_DIR}/${cmvs_target}/release/libcmvs_host.a")
file(GLOB_RECURSE cmvs_inputs CONFIGURE_DEPENDS
    "${CMVS_ROOT}/crates/*/src/*.rs" "${CMVS_ROOT}/crates/*/Cargo.toml"
    "${CMVS_ROOT}/crates/cmvs-core/profiles/*.txt"
    "${CMVS_ROOT}/crates/cmvs-core/profiles/*.profile"
    "${CMVS_ROOT}/tools/*.py"
    "${CMVS_ROOT}/docs/games/chronoclock/contract-*.json")
list(APPEND cmvs_inputs "${CMVS_ROOT}/Cargo.toml" "${CMVS_ROOT}/Cargo.lock")
# The software compositor needs optimization even in a Debug app. Keep symbols.
add_custom_command(OUTPUT "${cmvs_library}"
    COMMAND "${CMAKE_COMMAND}" -E env "RUSTC=${CMVS_RUSTC}"
        "CARGO_TARGET_DIR=${AETHERKIRI_CMVS_CARGO_TARGET_DIR}"
        "CARGO_PROFILE_RELEASE_DEBUG=1"
        "${CMVS_CARGO}" build --locked --manifest-path "${CMVS_ROOT}/Cargo.toml"
        -p cmvs-host --lib --release --target "${cmvs_target}"
    DEPENDS ${cmvs_inputs} WORKING_DIRECTORY "${CMVS_ROOT}" VERBATIM USES_TERMINAL)
add_custom_target(aether_cmvs_rust_build DEPENDS "${cmvs_library}")
add_library(aether_cmvs_rust STATIC IMPORTED GLOBAL)
set_target_properties(aether_cmvs_rust PROPERTIES IMPORTED_LOCATION "${cmvs_library}")
add_dependencies(aether_cmvs_rust aether_cmvs_rust_build)
add_library(aether_cmvs_runtime STATIC cmvs_runtime_provider.cpp)
target_compile_features(aether_cmvs_runtime PUBLIC cxx_std_17)
set_target_properties(aether_cmvs_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(aether_cmvs_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    "${AETHERKIRI_CMVS_ABI_INCLUDE}" PRIVATE "${CMVS_ROOT}/crates/cmvs-host")
target_link_libraries(aether_cmvs_runtime PUBLIC aether_cmvs_rust)
foreach(framework AVFAudio AudioToolbox CoreAudio CoreFoundation Foundation Security SystemConfiguration)
    find_library(CMVS_${framework}_FRAMEWORK ${framework} REQUIRED)
    target_link_libraries(aether_cmvs_runtime PUBLIC "${CMVS_${framework}_FRAMEWORK}")
endforeach()
if(PROJECT_IS_TOP_LEVEL OR ENABLE_TESTS)
    enable_testing()
    add_executable(cmvs-provider-test tests/provider.cpp)
    target_link_libraries(cmvs-provider-test PRIVATE aether_cmvs_runtime)
    add_test(NAME cmvs-provider COMMAND cmvs-provider-test)
endif()
