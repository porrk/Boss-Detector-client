# ============================================================================
# libfacedetection —— 独立动态库 (SHARED)
# 对外仅导出 facedetect_cnn (由 FACEDETECTION_EXPORT 标记)。
# AVX/NEON 内联函数代码在此目标内编译，对应编译标志也只加到这里。
# ============================================================================

add_library(facedetection SHARED
    src/facedetectcnn.cpp
    src/facedetectcnn-model.cpp
    src/facedetectcnn-data.cpp
)

# 对外头文件目录（PUBLIC：使用本库的目标会自动获得该 include 路径）
target_include_directories(facedetection PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 动态库符号可见性：默认隐藏，仅 FACEDETECTION_EXPORT 标记的符号导出。
# CMake 会为 SHARED 目标自动定义 facedetection_EXPORTS，配合 facedetection_export.h 使用。
set_target_properties(facedetection PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ----------------------------------------------------------------------------
# SIMD 加速：AVX/NEON 内联函数需要对应编译标志 + 宏定义
# ----------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(facedetection PRIVATE -O3)

    if(ENABLE_AVX512)
        target_compile_options(facedetection PRIVATE
            -mavx512f -mavx512cd -mavx512dq -mavx512bw -mavx512vl -mfma)
        target_compile_definitions(facedetection PRIVATE _ENABLE_AVX512)
        message(STATUS "libfacedetection: SIMD = AVX512")
    elseif(ENABLE_AVX2)
        target_compile_options(facedetection PRIVATE -mavx2 -mfma)
        target_compile_definitions(facedetection PRIVATE _ENABLE_AVX2)
        message(STATUS "libfacedetection: SIMD = AVX2")
    elseif(ENABLE_NEON)
        target_compile_definitions(facedetection PRIVATE _ENABLE_NEON)
        # AArch64 下 NEON 默认开启无需标志；32 位 ARM 需要 -mfpu=neon。
        if(CXX_HAS_MFPU_NEON)
            target_compile_options(facedetection PRIVATE -mfpu=neon)
        endif()
        message(STATUS "libfacedetection: SIMD = NEON")
    else()
        message(STATUS "libfacedetection: SIMD disabled")
    endif()
elseif(MSVC)
    target_compile_options(facedetection PRIVATE /O2)

    if(ENABLE_AVX512)
        target_compile_options(facedetection PRIVATE /arch:AVX512)
        target_compile_definitions(facedetection PRIVATE _ENABLE_AVX512)
    elseif(ENABLE_AVX2)
        target_compile_options(facedetection PRIVATE /arch:AVX2)
        target_compile_definitions(facedetection PRIVATE _ENABLE_AVX2)
    elseif(ENABLE_NEON)
        # MSVC for ARM: NEON 默认开启，无需额外标志。
        target_compile_definitions(facedetection PRIVATE _ENABLE_NEON)
    endif()
endif()

# OpenMP：加速卷积运算（可选）
if(OpenMP_FOUND)
    target_link_libraries(facedetection PRIVATE OpenMP::OpenMP_CXX)
endif()
