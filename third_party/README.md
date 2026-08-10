# 第三方依赖

本目录存放项目依赖。`sqlite` 与 `cjson` 为已纳入版本控制的源码（体积小，直接编译）；
`onnxruntime` 体积过大（约 285MB，含 274MB 的 pdb 调试符号），**不纳入版本控制**，需手动下载。

## 目录结构（CMakeLists.txt 期望）

```
third_party/
├── cjson/                 # ✅ 已入库（cJSON.c / cJSON.h）
├── sqlite/                # ✅ 已入库（sqlite3.c / sqlite3.h / sqlite3ext.h）
├── onnxruntime/           # ❌ 需手动下载（见下）
└── paho/                  # 占位，当前未使用
```

## ONNX Runtime（手动下载）

- 版本：**1.15.x**（C API version 18，对应头文件 `ORT_API_VERSION 18`）
- 平台：Windows x64，CPU（CPU Execution Provider）
- 下载：<https://github.com/microsoft/onnxruntime/releases/tag/v1.15.1>
  选 `onnxruntime-win-x64-1.15.1.zip`（非 GPU 版）。

解压后，把 `include/`、`lib/`、`bin/` 三个子目录放到 `third_party/onnxruntime/` 下，
使最终结构为：

```
third_party/onnxruntime/
├── include/               # onnxruntime_c_api.h 等头文件
│   └── ...
├── lib/
│   ├── onnxruntime.lib
│   └── onnxruntime_providers_shared.lib
└── bin/
    ├── onnxruntime.dll
    └── onnxruntime_providers_shared.dll
```

> `*.pdb` 调试符号（约 274MB）非必需，可删除以节省空间；CMake 的 POST_BUILD 只拷贝两个 `.dll`。

构建时 CMake 会链接 `lib/onnxruntime.lib`，并把 `bin/*.dll` 部署到 `dist/`。

## SFace 模型

见 `../models/README.md`（37MB，含下载地址与 SHA-256，同样不纳入版本控制）。
