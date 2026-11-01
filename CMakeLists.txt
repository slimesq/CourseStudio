cmake_minimum_required(VERSION 3.25)

project(CourseStudio LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "CourseStudio uses Windows capture, audio, and Direct3D APIs.")
endif()

if(DEFINED ENV{QTDIR} AND NOT CMAKE_PREFIX_PATH)
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}")
endif()

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets Xml Network Concurrent Multimedia MultimediaWidgets
    OpenGLWidgets WebEngineWidgets WebChannel)

if(DEFINED ENV{OPENCV_HOME} AND NOT OpenCV_DIR)
    foreach(candidate IN ITEMS
        "$ENV{OPENCV_HOME}/x64/vc17/lib"
        "$ENV{OPENCV_HOME}/x64/vc16/lib"
        "$ENV{OPENCV_HOME}/build/x64/vc17/lib"
        "$ENV{OPENCV_HOME}/build/x64/vc16/lib")
        if(EXISTS "${candidate}/OpenCVConfig.cmake")
            set(OpenCV_DIR "${candidate}")
            break()
        endif()
    endforeach()
endif()
find_package(OpenCV REQUIRED)

set(FFMPEG_ROOT "" CACHE PATH "FFmpeg installation root")
set(FFMPEG_INCLUDE_HINTS "$ENV{FFMPEG_INCLUDE}")
set(FFMPEG_LIBRARY_HINTS "$ENV{FFMPEG_LIB}")
foreach(root IN ITEMS "${FFMPEG_ROOT}" "$ENV{FFMPEG_7_HOME}" "$ENV{FFMPEG_HOME}")
    if(root)
        list(APPEND FFMPEG_INCLUDE_HINTS "${root}/include")
        list(APPEND FFMPEG_LIBRARY_HINTS "${root}/lib")
    endif()
endforeach()
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h
    HINTS ${FFMPEG_INCLUDE_HINTS} REQUIRED)
foreach(component IN ITEMS avcodec avdevice avformat avutil swresample swscale)
    string(TOUPPER "${component}" upper_component)
    find_library(FFMPEG_${upper_component}_LIBRARY NAMES ${component}
        HINTS ${FFMPEG_LIBRARY_HINTS} REQUIRED)
    list(APPEND FFMPEG_LIBRARIES "${FFMPEG_${upper_component}_LIBRARY}")
endforeach()

# Match the original .pro/.pri application target. The CUDA and demo files were
# present in the archive but were not compiled by that target.
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

set(PROJECT_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
foreach(source IN LISTS PROJECT_MODULE_SOURCES)
    get_filename_component(source_dir "${source}" DIRECTORY)
    list(APPEND PROJECT_INCLUDE_DIRS "${source_dir}")
endforeach()
list(REMOVE_DUPLICATES PROJECT_INCLUDE_DIRS)
target_include_directories(CourseStudio PRIVATE
    ${PROJECT_INCLUDE_DIRS} "${FFMPEG_INCLUDE_DIR}" ${OpenCV_INCLUDE_DIRS})

target_link_libraries(CourseStudio PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Xml Qt6::Network
    Qt6::Concurrent Qt6::Multimedia Qt6::MultimediaWidgets
    Qt6::OpenGLWidgets Qt6::WebEngineWidgets Qt6::WebChannel
    ${OpenCV_LIBS} ${FFMPEG_LIBRARIES}
    dbghelp psapi d3d11 dxgi d3dcompiler dwmapi user32 windowsapp
    mmdevapi ole32)

if(MSVC)
    target_compile_options(CourseStudio PRIVATE /permissive- /Zc:twoPhase-)
    target_link_options(CourseStudio PRIVATE
        $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
    target_compile_options(CourseStudio PRIVATE $<$<CONFIG:Release>:/Zi>)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/UI/QSS/icon/48.ico")
    set(icon_rc "${CMAKE_CURRENT_BINARY_DIR}/CourseStudioIcon.rc")
    file(WRITE "${icon_rc}"
        "IDI_ICON1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/UI/QSS/icon/48.ico\"\n")
    target_sources(CourseStudio PRIVATE "${icon_rc}")
endif()
