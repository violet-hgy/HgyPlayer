cmake_minimum_required(VERSION 3.5)

project(HgyPlayer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Qt
# ---------------------------------------------------------------------------
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Multimedia)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Multimedia)

# ---------------------------------------------------------------------------
# FFmpeg（BtbN win64 lgpl-shared，已放在工程目录下）
# 目录结构约定：
#   ffmpeg-master-latest-win64-lgpl-shared/
#     include/   头文件
#     lib/       导入库（MinGW: *.dll.a，MSVC: *.lib）
#     bin/       运行时 DLL
# ---------------------------------------------------------------------------
set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg-master-latest-win64-lgpl-shared")

if(NOT EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h")
    message(FATAL_ERROR "FFmpeg headers not found under: ${FFMPEG_ROOT}")
endif()

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    media/MediaTypes.h
    media/FFmpegVideoParser.h
    media/FFmpegVideoParser.cpp
    media/FFmpegPlayer.h
    media/FFmpegPlayer.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(HgyPlayer
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(HgyPlayer SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(HgyPlayer
            ${PROJECT_SOURCES}
        )
    endif()
endif()

# 让 #include "MediaTypes.h" / "FFmpegVideoParser.h" 可用
target_include_directories(HgyPlayer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/media"
    "${FFMPEG_ROOT}/include"
)

# ---------------------------------------------------------------------------
# 链接 FFmpeg
# MinGW: 优先使用 lib/*.dll.a
# MSVC : 使用 lib/*.lib
# ---------------------------------------------------------------------------
if(MINGW)
    set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")
    target_link_libraries(HgyPlayer PRIVATE
        "${FFMPEG_LIB_DIR}/libavformat.dll.a"
        "${FFMPEG_LIB_DIR}/libavcodec.dll.a"
        "${FFMPEG_LIB_DIR}/libavutil.dll.a"
        "${FFMPEG_LIB_DIR}/libswscale.dll.a"
        "${FFMPEG_LIB_DIR}/libswresample.dll.a"
    )
elseif(MSVC)
    set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")
    target_link_libraries(HgyPlayer PRIVATE
        "${FFMPEG_LIB_DIR}/avformat.lib"
        "${FFMPEG_LIB_DIR}/avcodec.lib"
        "${FFMPEG_LIB_DIR}/avutil.lib"
        "${FFMPEG_LIB_DIR}/swscale.lib"
        "${FFMPEG_LIB_DIR}/swresample.lib"
    )
else()
    message(FATAL_ERROR "Unsupported compiler toolchain for this FFmpeg package layout")
endif()

target_link_libraries(HgyPlayer PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Multimedia
)

# Windows 下把 FFmpeg DLL 复制到可执行文件同目录，避免运行时找不到动态库
if(WIN32)
    add_custom_command(TARGET HgyPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${FFMPEG_ROOT}/bin"
            "$<TARGET_FILE_DIR:HgyPlayer>"
        COMMENT "Copy FFmpeg runtime DLLs next to HgyPlayer"
    )
endif()

if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.HgyPlayer)
endif()
set_target_properties(HgyPlayer PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS HgyPlayer
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(HgyPlayer)
endif()
