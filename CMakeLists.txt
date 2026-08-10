cmake_minimum_required(VERSION 3.16)
project(face_client LANGUAGES C CXX)

# VS 生成器默认 32 位，如果没加 -A x64 就报友好错误
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "32-bit build detected! VS generator defaults to x86 (not x64).\n"
        "Please re-run with -A x64:\n"
        "  mkdir build && cd build && cmake .. -A x64\n"
        "  (add -A x64 to the cmake configure command)"
    )
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 输出目录在 face_client target 上用生成器表达式设置（见 add_executable 后），
# 避免 VS 多配置生成器在 dist/ 下追加 Release/ 子目录

# 全局宏：屏蔽 CRT 安全告警
add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS UNICODE _UNICODE)
# MSVC：源文件为 UTF-8，执行字符集也用 UTF-8（Qt5 期望 UTF-8 字符串）
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------- Qt5 ----------
# 如果没有通过环境变量或 -D 指定 Qt5 路径，自动搜索常见安装位置
if(NOT CMAKE_PREFIX_PATH)
    set(_qt5_candidates
        "D:/Program/Qt/Qt5.14.2/5.14.2/msvc2017_64"
        "D:/Program/Qt/Qt5.14.2/5.14.2/msvc2015_64"
        "C:/Qt/5.15.2/msvc2019_64"
        "C:/Qt/5.15.2/msvc2017_64"
        "C:/Qt/Qt5.15.2/5.15.2/msvc2019_64"
        "D:/Qt/5.15.2/msvc2019_64"
        "D:/Qt/5.14.2/msvc2017_64"
    )
    foreach(_qt5_try ${_qt5_candidates})
        if(EXISTS "${_qt5_try}/lib/cmake/Qt5/Qt5Config.cmake")
            set(CMAKE_PREFIX_PATH "${_qt5_try}" CACHE PATH "Qt5 install prefix" FORCE)
            message(STATUS "Auto-detected Qt5 at: ${_qt5_try}")
            break()
        endif()
    endforeach()
    if(NOT CMAKE_PREFIX_PATH)
        message(FATAL_ERROR
            "Qt5 not found! Set CMAKE_PREFIX_PATH to your Qt5 install directory.\n"
            "  Example: cmake .. -DCMAKE_PREFIX_PATH=D:/Qt/5.14.2/msvc2017_64\n"
            "  Or: set CMAKE_PREFIX_PATH=D:/Qt/5.14.2/msvc2017_64 (environment variable)"
        )
    endif()
endif()

find_package(Qt5 COMPONENTS Widgets Concurrent Network REQUIRED)

# 查找 windeployqt（与 Qt5 同一安装的 bin 目录，Qt5_DIR 形如 .../msvc2017_64/lib/cmake/Qt5）
find_program(WINDEPLOYQT_EXE windeployqt
    HINTS "${Qt5_DIR}/../../../bin"
    DOC "Qt windeployqt utility"
)
if(WINDEPLOYQT_EXE)
    message(STATUS "windeployqt: ${WINDEPLOYQT_EXE}")
else()
    message(WARNING "windeployqt 未找到，构建后需手动运行 windeployqt 打包 Qt 依赖")
endif()

# ---------- libfacedetection（静态，AVX2 已在头文件关闭） ----------
add_library(facedetection STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libfacedetection/src/facedetectcnn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libfacedetection/src/facedetectcnn-model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/libfacedetection/src/facedetectcnn-data.cpp
)
target_include_directories(facedetection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libfacedetection/include)
# 静态编译：让导出宏为空，避免 MSVC 遇到 __attribute__ 报错
target_compile_definitions(facedetection PUBLIC FACEDETECTION_STATIC_DEFINE)
if(MSVC)
    target_compile_options(facedetection PRIVATE /O2 /wd4819)
endif()

# ---------- SQLite amalgamation（静态） ----------
add_library(sqlite3 STATIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_ENABLE_FTS5
)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0 /wd4996)
endif()

# ---------- cJSON（静态） ----------
add_library(cjson STATIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson)
if(MSVC)
    target_compile_options(cjson PRIVATE /W0)
endif()

# ---------- 主程序 ----------
add_executable(face_client WIN32
    src/main.cpp
    src/app.h
    src/app.cpp
    src/config.h
    src/config.cpp
    src/db.h
    src/db.cpp
    src/face/align.h
    src/face/align.cpp
    src/face/face_pipeline.h
    src/face/face_pipeline.cpp
    src/face/recognition.h
    src/face/recognition.cpp
    src/onnx/sface_runner.h
    src/onnx/sface_runner.cpp
    src/ui/main_window.h
    src/ui/main_window.cpp
    src/ui/template_dialog.h
    src/ui/template_dialog.cpp
    src/ui/settings_dialog.h
    src/ui/settings_dialog.cpp
    src/ui/toast_window.h
    src/ui/toast_window.cpp
    src/net/mqtt_client.h
    src/net/mqtt_client.cpp
    src/notify/notifier.h
    src/notify/notifier.cpp
    src/resources.qrc
)

# 用生成器表达式设输出目录，VS 多配置生成器不会追加 <CONFIG>/ 子目录
set_target_properties(face_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_SOURCE_DIR}/dist>
)

target_include_directories(face_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson
    ${CMAKE_CURRENT_SOURCE_DIR}/libfacedetection/include
)

target_link_libraries(face_client PRIVATE
    Qt5::Widgets
    Qt5::Concurrent
    Qt5::Network
    $<$<PLATFORM_ID:Windows>:Qt5::WinMain>
    facedetection
    sqlite3
    cjson
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/lib/onnxruntime.lib
)

# ---- 构建后部署：ONNX Runtime DLL + windeployqt(Qt DLL) + 模型 + run.bat ----
# windeployqt 命令（找不到则跳过，仅打印提示）
if(WINDEPLOYQT_EXE)
    set(DEPLOY_QT_CMD
        COMMAND ${WINDEPLOYQT_EXE} --release --no-translations
                --no-system-d3d-compiler --no-opengl-sw --no-quick-import
                $<TARGET_FILE:face_client>
    )
else()
    set(DEPLOY_QT_CMD
        COMMAND ${CMAKE_COMMAND} -E echo "windeployqt 未找到，跳过 Qt DLL 部署"
    )
endif()

add_custom_command(TARGET face_client POST_BUILD
    # 1. ONNX Runtime 运行时 DLL（仅 dll，不含 pdb）
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime.dll
        $<TARGET_FILE_DIR:face_client>
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/bin/onnxruntime_providers_shared.dll
        $<TARGET_FILE_DIR:face_client>
    # 2. windeployqt 打包 Qt 依赖（Qt5Core/Gui/Widgets/Svg + 平台/图片插件）
    ${DEPLOY_QT_CMD}
    # 3. 拷贝 ONNX 模型到 dist/models/
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/dist/models
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/models/face_recognition_sface_2021dec.onnx
        ${CMAKE_CURRENT_SOURCE_DIR}/dist/models/
    # 4. 拷贝 run.bat 启动脚本到 dist/
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/run.bat
        ${CMAKE_CURRENT_SOURCE_DIR}/dist/
    COMMENT "Deploying: ONNX Runtime + Qt (windeployqt) + models + run.bat"
)
