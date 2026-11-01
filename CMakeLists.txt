cmake_minimum_required(VERSION 3.25)

project(CourseStudio LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "CourseStudio uses Windows capture, audio, and Direct3D APIs.")
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Network Concurrent OpenGLWidgets WebEngineWidgets)
find_package(OpenCV REQUIRED)

set(FFMPEG_ROOT "" CACHE PATH "FFmpeg installation root")
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    HINTS "${FFMPEG_ROOT}/include" REQUIRED)
foreach(component IN ITEMS avcodec avformat avutil swresample swscale)
    string(TOUPPER "${component}" upper_component)
    find_library(FFMPEG_${upper_component}_LIBRARY NAMES ${component}
        HINTS "${FFMPEG_ROOT}/lib" REQUIRED)
    list(APPEND FFMPEG_LIBRARIES "${FFMPEG_${upper_component}_LIBRARY}")
endforeach()

# CUDA experiments and standalone demo windows are separate from the application.
file(GLOB PROJECT_ROOT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
set(PROJECT_MODULE_SOURCES)
foreach(module IN ITEMS Core Model Net Record UI VFX)
    file(GLOB_RECURSE module_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${module}/*.h")
    list(APPEND PROJECT_MODULE_SOURCES ${module_sources})
endforeach()
foreach(unused IN ITEMS
    Core/TestDemoWindow.cpp Core/TestDemoWindow.h Core/YUVTestWidget.h
    Net/Http/TestResourceWindow.cpp Net/Http/TestResourceWindow.h)
    list(REMOVE_ITEM PROJECT_MODULE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${unused}")
endforeach()

set(PROJECT_SOURCES ${PROJECT_ROOT_SOURCES} ${PROJECT_MODULE_SOURCES}
    res.qrc img.qrc)
add_executable(CourseStudio WIN32 ${PROJECT_SOURCES})
set_target_properties(CourseStudio PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    AUTOMOC ON
    AUTORCC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_include_directories(CourseStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    Core Model Net/Http Record VFX
    UI/Live UI/Login UI/Pattern UI/Screen UI/Utils
    ${FFMPEG_INCLUDE_DIR} ${OpenCV_INCLUDE_DIRS})

target_link_libraries(CourseStudio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent
    Qt6::OpenGLWidgets Qt6::WebEngineWidgets
    ${OpenCV_LIBS} ${FFMPEG_LIBRARIES}
    dbghelp d3d11 dxgi d3dcompiler user32 windowsapp
    mmdevapi ole32)

if(MSVC)
    target_compile_options(CourseStudio PRIVATE /permissive-)
    target_link_options(CourseStudio PRIVATE
        $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
    target_compile_options(CourseStudio PRIVATE $<$<CONFIG:Release>:/Zi>)
endif()

configure_file(cmake/CourseStudioIcon.rc.in CourseStudioIcon.rc @ONLY)
target_sources(CourseStudio PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/CourseStudioIcon.rc")

# Keep the CUDA/D3D11 experiment outside the app target while indexing its
# C++ source and generating local clangd flags for the kernel source.
set(CUDA_CUDART_ROOT "" CACHE PATH "CUDA runtime development package or toolkit root")
set(CUDA_NVCC_ROOT "" CACHE PATH "CUDA compiler package or toolkit root")
if(CUDA_CUDART_ROOT)
    find_path(CUDA_CUDART_INCLUDE_DIR cuda_runtime.h
        HINTS "${CUDA_CUDART_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
    find_path(CUDA_NVCC_INCLUDE_DIR crt/host_config.h
        HINTS "${CUDA_NVCC_ROOT}/include" "${CUDA_CUDART_ROOT}/include"
        NO_DEFAULT_PATH REQUIRED)
    add_library(CourseStudioCudaIndex OBJECT EXCLUDE_FROM_ALL
        Cuda/CudaAccelerator.cpp Cuda/CudaAccelerator.h)
    target_compile_features(CourseStudioCudaIndex PRIVATE cxx_std_20)
    target_include_directories(CourseStudioCudaIndex PRIVATE
        Core "${CUDA_CUDART_INCLUDE_DIR}" "${CUDA_NVCC_INCLUDE_DIR}")
    if(MSVC)
        target_compile_options(CourseStudioCudaIndex PRIVATE /utf-8)
    endif()
    if(CUDA_NVCC_ROOT)
        set(CUDA_CLANGD_ROOT "${CUDA_NVCC_ROOT}")
    else()
        set(CUDA_CLANGD_ROOT "${CUDA_CUDART_ROOT}")
    endif()
    configure_file(cmake/CudaClangd.yaml.in "${CMAKE_CURRENT_SOURCE_DIR}/Cuda/.clangd" @ONLY)
endif()
