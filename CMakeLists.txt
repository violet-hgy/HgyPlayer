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

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets OpenGL)
else()
    find_package(Qt5 REQUIRED COMPONENTS OpenGL)
endif()

# ---------------------------------------------------------------------------
# FFmpeg（BtbN win64 lgpl-shared，已放在工程目录下）
# 仓库保留完整共享包，便于他人克隆后直接编译、版本一致。
# 目录结构约定：
#   ffmpeg-master-latest-win64-lgpl-shared/
#     include/   头文件
#     lib/       导入库（MinGW: *.dll.a，MSVC: *.lib）
#     bin/       完整运行时（含 DLL / exe / 未使用的 avfilter、avdevice 等）
# 本工程仅以库方式调用（libav* / libsw* API），不启动命令行工具。
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
    media/GpuVideoFrame.h
    media/render/IVideoRenderer.h
    media/render/VideoRendererFactory.h
    media/render/VideoRendererFactory.cpp
    media/render/QImageVideoRenderer.h
    media/render/QImageVideoRenderer.cpp
    media/render/OpenGLVideoRenderer.h
    media/render/OpenGLVideoRenderer.cpp
    media/render/D3D11VideoRenderer.h
    media/render/D3D11VideoRenderer.cpp
    media/render/D3D11SharedDevice.h
    media/render/D3D11SharedDevice.cpp
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

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    target_link_libraries(HgyPlayer PRIVATE
        Qt6::OpenGLWidgets
        Qt6::OpenGL
    )
else()
    target_link_libraries(HgyPlayer PRIVATE Qt5::OpenGL)
endif()

if(WIN32)
    target_link_libraries(HgyPlayer PRIVATE d3d11 dxgi d3dcompiler)
endif()

# ---------------------------------------------------------------------------
# Windows 运行目录：最小可用 DLL 拷贝（仓库 bin/ 仍保留完整包，不在此删文件）
#
# 与上方 target_link_libraries 保持一致，只拷贝实际链接到的库对应 DLL：
#   avutil / avcodec / avformat / swscale / swresample
# 依赖关系自洽：avformat→avcodec→avutil/swresample，swscale→avutil
#
# 明确不拷贝（运行本播放器不需要）：
#   - ffmpeg.exe / ffplay.exe / ffprobe.exe（命令行工具）
#   - avfilter-*.dll / avdevice-*.dll（未链接，且不被上述 DLL 反向依赖）
#
# 通配版本号（如 avcodec-63.dll），升级 FFmpeg 共享包后一般无需改此处。
# ---------------------------------------------------------------------------
if(WIN32)
    set(_ffmpeg_runtime_dll_globs
        avutil-*.dll
        avcodec-*.dll
        avformat-*.dll
        swscale-*.dll
        swresample-*.dll
    )
    set(FFMPEG_RUNTIME_DLLS "")
    foreach(_pat IN LISTS _ffmpeg_runtime_dll_globs)
        file(GLOB _matched CONFIGURE_DEPENDS "${FFMPEG_ROOT}/bin/${_pat}")
        list(APPEND FFMPEG_RUNTIME_DLLS ${_matched})
    endforeach()
    if(NOT FFMPEG_RUNTIME_DLLS)
        message(FATAL_ERROR "No FFmpeg runtime DLLs found under: ${FFMPEG_ROOT}/bin")
    endif()

    set(_ffmpeg_copy_cmds "")
    foreach(_dll IN LISTS FFMPEG_RUNTIME_DLLS)
        list(APPEND _ffmpeg_copy_cmds
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}"
                "$<TARGET_FILE_DIR:HgyPlayer>"
        )
    endforeach()
    add_custom_command(TARGET HgyPlayer POST_BUILD
        ${_ffmpeg_copy_cmds}
        COMMENT "Copy minimal FFmpeg runtime DLLs next to HgyPlayer"
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
