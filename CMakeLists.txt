cmake_minimum_required(VERSION 3.16)

project(tiray_sdk VERSION 0.1.0 LANGUAGES C)

if(WIN32)
    message(FATAL_ERROR "TiRay 43108 SDK only supports Linux/Kylin")
endif()

include(CTest)
include(GNUInstallDirs)
find_package(Threads REQUIRED)

option(TIRAY_SDK_INTERNAL_BUILD "Build internal SDK with hardware configuration access" OFF)

add_library(tiray_sdk STATIC
    src/tiray_protocol.c
    src/tiray_pcie.c
    src/tiray_async.c
)

target_compile_features(tiray_sdk PUBLIC c_std_99)
target_include_directories(tiray_sdk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(tiray_sdk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tiray_sdk PUBLIC Threads::Threads)
set_target_properties(tiray_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(TIRAY_SDK_INTERNAL_BUILD)
    target_compile_definitions(tiray_sdk PRIVATE TIRAY_SDK_INTERNAL_BUILD=1)
endif()

if(MSVC)
    target_compile_options(tiray_sdk PRIVATE /W4)
else()
    target_compile_options(tiray_sdk PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(tiray_sdk PRIVATE _DEFAULT_SOURCE)
endif()

install(TARGETS tiray_sdk
    EXPORT tiray_sdkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(EXPORT tiray_sdkTargets
    FILE tiray_sdkTargets.cmake
    NAMESPACE tiray::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tiray_sdk"
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY docs/customer/ DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES examples/customer_demo.cpp DESTINATION "${CMAKE_INSTALL_DOCDIR}/examples")
if(TIRAY_SDK_INTERNAL_BUILD)
    install(DIRECTORY docs/internal/ DESTINATION "${CMAKE_INSTALL_DOCDIR}/internal")
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/tiray_sdkConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tiray_sdkConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tiray_sdkConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tiray_sdk"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tiray_sdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tiray_sdkConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tiray_sdk"
)

option(TIRAY_SDK_BUILD_HOST "Build tiray_host maintenance tool (SDK public API only)" OFF)
if(TIRAY_SDK_BUILD_HOST)
    enable_language(CXX)
    add_executable(tiray_host tools/tiray_host/main.cpp)
    target_link_libraries(tiray_host PRIVATE tiray_sdk)
    target_include_directories(tiray_host PRIVATE include)
    target_compile_features(tiray_host PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(tiray_host PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

option(TIRAY_SDK_BUILD_EXAMPLES "Build SDK examples" OFF)
if(TIRAY_SDK_BUILD_EXAMPLES)
    enable_language(CXX)
    add_executable(tiray_sdk_example examples/basic.cpp)
    add_executable(tiray_sdk_async_example examples/async.cpp)
    add_executable(tiray_sdk_hardware_test examples/hardware_test.cpp)
    add_executable(tiray_sdk_customer_demo examples/customer_demo.cpp)
    target_link_libraries(tiray_sdk_example PRIVATE tiray_sdk)
    target_link_libraries(tiray_sdk_async_example PRIVATE tiray_sdk)
    target_link_libraries(tiray_sdk_hardware_test PRIVATE tiray_sdk)
    target_link_libraries(tiray_sdk_customer_demo PRIVATE tiray_sdk)
    target_include_directories(tiray_sdk_example PRIVATE include)
    target_include_directories(tiray_sdk_async_example PRIVATE include)
    target_include_directories(tiray_sdk_hardware_test PRIVATE include)
    target_include_directories(tiray_sdk_customer_demo PRIVATE include)
    target_compile_features(tiray_sdk_example PRIVATE cxx_std_17)
    target_compile_features(tiray_sdk_async_example PRIVATE cxx_std_17)
    target_compile_features(tiray_sdk_hardware_test PRIVATE cxx_std_17)
    target_compile_features(tiray_sdk_customer_demo PRIVATE cxx_std_17)
endif()

if(BUILD_TESTING)
    add_executable(tiray_protocol_tests tests/tiray_protocol_tests.c)
    target_link_libraries(tiray_protocol_tests PRIVATE tiray_sdk)
    target_include_directories(tiray_protocol_tests PRIVATE src include)
    add_test(NAME tiray_protocol_tests COMMAND tiray_protocol_tests)
endif()
