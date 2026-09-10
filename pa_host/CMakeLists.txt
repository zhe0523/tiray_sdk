cmake_minimum_required(VERSION 3.16)

project(pa_host VERSION 0.1.0 LANGUAGES CXX)

string(TIMESTAMP PA_HOST_BUILD_TIME "%Y-%m-%d %H:%M:%S")

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 优先使用 Qt6；Kylin V10 上如果只安装 Qt5，也能自动回退到 Qt5。
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets SerialPort)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets SerialPort)
find_package(Threads REQUIRED)

add_library(pa_core STATIC
    src/AppLogService.cpp
    src/AppLogService.h
    src/AppSettings.cpp
    src/AppSettings.h
    src/FramePresentationController.cpp
    src/FramePresentationController.h
    src/ImageAcquisitionController.cpp
    src/ImageAcquisitionController.h
    src/ILineTransport.cpp
    src/ILineTransport.h
    src/ImageExportService.cpp
    src/ImageExportService.h
    src/ImageTransferWorkflowController.cpp
    src/ImageTransferWorkflowController.h
    src/ImageAlgorithms.cpp
    src/ImageAlgorithms.h
    src/ImageSession.cpp
    src/ImageSession.h
    src/ImageSource.cpp
    src/ImageSource.h
    src/MtfAnalysis.cpp
    src/MtfAnalysis.h
    src/PaProtocol.cpp
    src/PaProtocol.h
    src/PaBinaryProtocol.cpp
    src/PaBinaryProtocol.h
    src/PcieImageSource.cpp
    src/PcieImageSource.h
    src/PaDeviceController.cpp
    src/PaDeviceController.h
    src/ReplayPresentationScheduler.cpp
    src/ReplayPresentationScheduler.h
    src/TiRawImage.cpp
    src/TiRawImage.h
)

target_include_directories(pa_core PUBLIC src)
target_link_libraries(pa_core PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Threads::Threads
)

add_library(pa_transport STATIC
    src/SerialClient.cpp
    src/SerialClient.h
)

target_include_directories(pa_transport PUBLIC src)
target_link_libraries(pa_transport PUBLIC
    pa_core
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::SerialPort
)

add_executable(pa_host
    src/main.cpp
    src/ImageListPanel.cpp
    src/ImageListPanel.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/ImageView.cpp
    src/ImageView.h
)

target_link_libraries(pa_host PRIVATE
    pa_transport
    pa_core
    Qt${QT_VERSION_MAJOR}::Widgets
)
target_compile_definitions(pa_host PRIVATE
    PA_HOST_VERSION="${PROJECT_VERSION}"
    PA_HOST_BUILD_TIME="${PA_HOST_BUILD_TIME}"
)

# A MinGW executable needs the runtime DLLs from the compiler that produced it.
# Keep them beside the executable so direct launches do not depend on PATH.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(PA_MINGW_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(PA_MINGW_RUNTIME_DLLS)
    foreach(runtime_dll
        libgcc_s_seh-1.dll
        libstdc++-6.dll
        libwinpthread-1.dll
    )
        if(EXISTS "${PA_MINGW_RUNTIME_DIR}/${runtime_dll}")
            list(APPEND PA_MINGW_RUNTIME_DLLS
                "${PA_MINGW_RUNTIME_DIR}/${runtime_dll}"
            )
        endif()
    endforeach()
    if(PA_MINGW_RUNTIME_DLLS)
        add_custom_target(pa_host_runtime
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PA_MINGW_RUNTIME_DLLS}
                "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS ${PA_MINGW_RUNTIME_DLLS}
            VERBATIM
        )
        add_dependencies(pa_host pa_host_runtime)
    endif()
endif()

function(pa_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

pa_enable_warnings(pa_core)
pa_enable_warnings(pa_transport)
pa_enable_warnings(pa_host)

install(TARGETS pa_host RUNTIME DESTINATION bin)

include(CTest)

if(BUILD_TESTING)
    add_executable(pa_host_tests
        tests/pa_host_tests.cpp
    )

    target_link_libraries(pa_host_tests PRIVATE
        pa_core
    )

    pa_enable_warnings(pa_host_tests)

    add_test(NAME pa_host_tests COMMAND pa_host_tests)

    add_executable(pa_image_benchmark
        tools/benchmark_tiraw.cpp
    )

    target_link_libraries(pa_image_benchmark PRIVATE
        pa_core
    )

    pa_enable_warnings(pa_image_benchmark)
endif()
