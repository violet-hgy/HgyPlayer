cmake_minimum_required(VERSION 3.5)

project(HgyPlayer VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
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
    media/render/NativeOpenGLVideoRenderer.h
    media/render/NativeOpenGLVideoRenderer.cpp
    media/render/D3D11VideoRenderer.h
    media/render/D3D11VideoRenderer.cpp
    media/render/D3D11SharedDevice.h
    media/render/D3D11SharedDevice.cpp
    media/render/IBrowserPlayback.h
    media/render/cef/ICefBrowserHost.h
    media/render/cef/CefRuntimeFacade.h
    media/render/cef/CefRuntimeFacade.cpp
    media/render/cef/CefBrowserHostStub.h
    media/render/cef/CefBrowserHostStub.cpp
    media/render/cef/CefVideoRenderer.h
    media/render/cef/CefVideoRenderer.cpp
)

# ---------------------------------------------------------------------------
# CEF：third_party/cef 存在则默认启用（覆盖旧 CMake 缓存里的 OFF）
# 显式关闭：-DHGY_ENABLE_CEF=OFF
# ---------------------------------------------------------------------------
set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef" CACHE PATH "CEF binary distribution root")
if(EXISTS "${CEF_ROOT}/include/cef_app.h")
    set(HGY_ENABLE_CEF ON CACHE BOOL "Enable Chromium Embedded Framework browser renderer" FORCE)
else()
    set(HGY_ENABLE_CEF OFF CACHE BOOL "Enable Chromium Embedded Framework browser renderer" FORCE)
endif()

if(HGY_ENABLE_CEF)
    list(APPEND PROJECT_SOURCES
        media/render/cef/CefRuntimeImpl.h
        media/render/cef/CefRuntimeImpl.cpp
        media/render/cef/CefBrowserHostImpl.h
        media/render/cef/CefBrowserHostImpl.cpp
        media/render/cef/CefAppHandler.h
        media/render/cef/CefAppHandler.cpp
        media/render/cef/CefClientHandler.h
        media/render/cef/CefClientHandler.cpp
    )
    list(REMOVE_ITEM PROJECT_SOURCES
        media/render/cef/CefBrowserHostStub.h
        media/render/cef/CefBrowserHostStub.cpp
    )
endif()

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
    "${CMAKE_CURRENT_SOURCE_DIR}/media/render"
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
    target_link_libraries(HgyPlayer PRIVATE d3d11 dxgi d3dcompiler opengl32)
endif()

if(HGY_ENABLE_CEF)
    if(NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
        message(FATAL_ERROR "HGY_ENABLE_CEF=ON but CEF SDK not found at: ${CEF_ROOT}")
    endif()
    if(NOT EXISTS "${CEF_ROOT}/libcef_dll/CMakeLists.txt")
        message(FATAL_ERROR "CEF libcef_dll wrapper not found under: ${CEF_ROOT}/libcef_dll")
    endif()

    # Use non-bootstrap/non-sandbox mode to match single-exe integration.
    set(USE_SANDBOX OFF CACHE BOOL "Disable CEF sandbox/bootstrap mode" FORCE)

    # 使用标准 MSVC 运行时：Debug=/MDd, Release=/MD，与 Qt Kit 保持一致。
    set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "Match Qt runtime" FORCE)
    # 规避 mspdbsrv 间歇性故障（C1090/0x6BA），优先使用 /Z7 减少独立 PDB 写入竞争。
    set(CEF_DEBUG_INFO_FLAG "/Z7" CACHE STRING "Use /Z7 to avoid PDB server flakiness" FORCE)

    list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
    find_package(CEF REQUIRED)

    add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper EXCLUDE_FROM_ALL)
    ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")

    target_compile_definitions(HgyPlayer PRIVATE HGY_ENABLE_CEF=1 NOMINMAX)
    target_include_directories(HgyPlayer PRIVATE "${CEF_ROOT}")

    add_dependencies(HgyPlayer libcef_dll_wrapper)
    if(MSVC)
        set_property(TARGET HgyPlayer PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        set_property(TARGET libcef_dll_wrapper PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        target_compile_options(HgyPlayer PRIVATE $<$<CONFIG:Debug>:/FS>)
    endif()
    target_link_libraries(HgyPlayer PRIVATE
        libcef_lib
        libcef_dll_wrapper
        ${CEF_STANDARD_LIBS}
    )

    if(CEF_EXE_LINKER_FLAGS_RELEASE)
        target_link_options(HgyPlayer PRIVATE ${CEF_EXE_LINKER_FLAGS_RELEASE})
    endif()
    if(CEF_EXE_LINKER_FLAGS_DEBUG)
        target_link_options(HgyPlayer PRIVATE ${CEF_EXE_LINKER_FLAGS_DEBUG})
    endif()

    if(WIN32)
        set(_cef_copy_cmds "")
        foreach(_cef_dll IN LISTS CEF_BINARY_FILES)
            list(APPEND _cef_copy_cmds
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<IF:$<CONFIG:Debug>,${CEF_BINARY_DIR_DEBUG},${CEF_BINARY_DIR_RELEASE}>/${_cef_dll}"
                    "$<TARGET_FILE_DIR:HgyPlayer>"
            )
        endforeach()
        if(EXISTS "${CEF_RESOURCE_DIR}")
            foreach(_cef_resource IN LISTS CEF_RESOURCE_FILES)
                if(NOT _cef_resource STREQUAL "locales")
                    list(APPEND _cef_copy_cmds
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${CEF_RESOURCE_DIR}/${_cef_resource}"
                            "$<TARGET_FILE_DIR:HgyPlayer>/${_cef_resource}"
                    )
                endif()
            endforeach()
            list(APPEND _cef_copy_cmds
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CEF_RESOURCE_DIR}/locales"
                    "$<TARGET_FILE_DIR:HgyPlayer>/locales"
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CEF_RESOURCE_DIR}"
                    "$<TARGET_FILE_DIR:HgyPlayer>/Resources"
            )
        endif()
        add_custom_command(TARGET HgyPlayer POST_BUILD
            ${_cef_copy_cmds}
            COMMENT "Copy CEF runtime next to HgyPlayer"
        )
    endif()
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
