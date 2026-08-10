# Windows 人脸识别客户端 - 设计与计划 v2（C++ 版 · 待审查）

> 状态：**GRILL-ME 模式**。本文档由两个 subagent 围绕"改用 C++、且需本地图片入库"重新讨论达成共识后生成。
> **在您完成审查并明确批准之前，不会写任何代码、不下载/安装任何依赖。**
> 请逐条"拷问"，标注 保留 / 修改 / 删除，我再定稿执行。

---

## 0. 背景与已勘察事实

- 工作目录 `E:/lzy/interest_demo/client_demo` 现有：
  - `message_parse.md`：MQTT 消息示例。topic=`facedetect/events`，JSON 含 `device_id/track_id/frame_id/captured_at_ms/bbox{x,y,width,height}/score/embedding_model("opencv_sface_2021dec")/embedding[128]/sent_at_ms`。
  - `libfacedetection/`：C++ CNN 人脸检测库（BSD）。**已确认输出 5 点关键点**——`FaceRect{ float score; int x,y,w,h; int lm[10]; }`，`objectdetect_cnn()` 返回 `std::vector<FaceRect>`（定义在 `facedetectcnn-model.cpp:67`）。
  - `models/face_recognition_sface_2021dec.onnx`：OpenCV Zoo **SFace**，输入对齐后 112×112 BGR，输出 128 维，应用层 L2 归一化。
  - `materials/`：`demo_video.mp4`、`template.jpg`、同名 onnx。
- **关键结论**：本地图片入库需要 "检测→5点对齐→SFace推理→归一化"。检测用 libfacedetection（自带关键点），SFace 推理需要一个 ONNX 推理引擎 → 这是 C++ 方案**唯一不可避免**的运行时依赖（ONNX Runtime）。
- 目标平台：**Windows x64**；开发在 WSL 写代码，**构建与运行必须在 Windows + MSVC**。

---

## 1. Subagent 讨论（C++ 方向）

> Agent A = 架构师（稳健、可维护、零依赖优先）
> Agent B = 务实工程师（实现成本、打包迁移、抗坑）

### 议题 1：GUI 框架（Win32 vs Qt）
- **A**：C++ 在 Windows 上候选 (a) **Win32 + comctl32 + GDI+**（零第三方，系统自带）(b) **Qt**（代码量少、UI 漂亮，但大依赖 + LGPL/商业授权）。用户强调"极简依赖、方便打包"，倾向 (a)。
- **B**：承认 (a) 代码更啰嗦（ListView/对话框/弹窗/托盘都要手写），但都是成熟套路；启用视觉样式（manifest + comctl32 v6）后观感可接受。(b) 会拖入几十 MB DLL 与授权顾虑。本工具是内部用，UI 不求华丽。
- **共识：主选 Win32 + comctl32 + GDI+（GUI 零第三方依赖）。Qt 作为备选（如您更看重开发效率/界面，可切换，但需接受大依赖与 LGPL）。请裁决。**

### 议题 2：SFace 推理引擎（不可避免依赖）
- **A**：SFace 是 ONNX 模型，C++ 跑它需要推理引擎。候选 (a) **ONNX Runtime C++**（Microsoft、MIT、官方预编译 Windows x64 包，~15MB DLL，API 成熟）(b) **OpenCV DNN**（能加载 ONNX，但 OpenCV 整包 ~60MB，更重）(c) 手写推理（SFace ~25 层 CNN，不现实）。
- **B**：选 (a)。ONNX Runtime 只需放 `onnxruntime.dll`+`onnxruntime.lib`+头文件到项目里链接，CPU EP 即可，无需 CUDA。是"必须安装/下载"的依赖，需您审查。
- **共识：ONNX Runtime C++（CPU EP）。这是 V1 唯一的外部运行时依赖。**

### 议题 3：图像解码 + 5 点对齐
- **A**：解码 JPEG/PNG/BMP 用 **GDI+**（`Gdiplus::Bitmap`，Windows 内置，零依赖）；对齐用 libfacedetection 的 5 关键点做**相似变换**到 112×112，参考点用 ArcFace/SFace 标准 112×112 模板；warp 手写双线性或用 GDI+ 仿射。
- **B**：同意。参考点（112×112）：
  `左眼(38.2946,51.6963) 右眼(73.5318,51.5014) 鼻(56.0252,71.7366) 左嘴(41.5493,92.3655) 右嘴(70.7299,92.2041)`。变换矩阵用最小二乘解 2×3 相似变换，手写 ~40 行。
- **共识：GDI+ 解码 + libfacedetection 关键点 + 手写相似变换对齐，零额外依赖。**

### 议题 4：libfacedetection 集成方式
- **A**：它自带 `CMakeLists.txt` 构建为 SHARED 仅导出 `facedetect_cnn`（返回 int* 缓冲区，需自己解析）。但 `objectdetect_cnn()` 返回干净的 `std::vector<FaceRect>`（含 lm），只是未导出。
- **B**：**直接把 `src/*.cpp` 编进我们的目标（静态），调用 `objectdetect_cnn()`**，省去 DLL 导出与缓冲区解析。AVX2 编译选项沿用（`-DENABLE_AVX2=ON`，MSVC `/arch:AVX2`）。
- **共识：libfacedetection 源码直接静态编译进项目，调用 objectdetect_cnn()。** 注意目标 CPU 需支持 AVX2。

### 议题 5：存储
- **A**：sqlite。C++ 下用 **SQLite amalgamation**（`sqlite3.c`/`sqlite3.h`，公有领域，单文件源码内嵌编译，无需安装任何东西）。
- **B**：同意。≤200 行，单文件 `templates.db`，CRUD 用预编译语句。特征向量存 JSON 文本（128 float）便于导入导出与调试。
- **共识：SQLite amalgamation 内嵌编译，无外部安装。**

### 议题 6：JSON 解析（MQTT payload + 导入导出）
- **A**：C++ 标准库无 JSON。候选 (a) **vendored cJSON**（MIT，单 .c/.h，轻量 C）(b) nlohmann/json（仅头，但编译较重）(c) 手写最小解析（MQTT payload 固定结构，可手写，但易错）。
- **B**：选 (a) cJSON，源码内嵌编译，无安装。它够轻、够稳。
- **共识：vendored cJSON（源码内嵌）。属"随源码携带"而非"系统安装"。**

### 议题 7：托盘 + 右下角弹窗
- **A**：托盘用 `Shell_NotifyIconW`（Win32 原生，零依赖，C++ 比 Python+ctypes 还顺手）；弹窗用**自绘分层窗口**（`WS_EX_LAYERED`+`UpdateLayeredWindow` 或普通置顶 `WS_POPUP`+`topmost`），右下角定位、多条堆叠、定时关闭、配色按级别。完全可定制文本，满足"弹窗消息自定义"。
- **B**：同意。不采用 WinRT 原生 Toast（COM/版本敏感、依赖重）。自绘弹窗与 Python 版方案一致，可控性最好。
- **共识：Shell_NotifyIcon 托盘 + 自绘右下角弹窗，零第三方依赖。**

### 议题 8：配置文件
- **A**：C++/Windows 下最原生的是 **INI + `GetPrivateProfileString`/`WritePrivateProfileString`**（Win32 内置，零依赖），比手写 JSON 配置更省事。
- **B**：同意。首启从 `assets/default_config.ini` 拷贝生成 `config.ini`。
- **共识：INI + Win32 API，零依赖。**

### 议题 9：MQTT 客户端（V2）
- **A**：C++ 无内置 MQTT。候选 (a) **Eclipse Paho MQTT C**（MIT，`paho-mqtt3c` 同步客户端，**无 SSL 版可不依赖 OpenSSL**，单 DLL）(b) 手写最小 Winsock2 MQTT 3.1.1 订阅器（CONNECT/SUBSCRIBE/PUBLISH QoS1/PINGREQ/重连，~600 行，零依赖但边界情况风险高）。
- **B**：内部部署求稳，选 (a) Paho C 无 SSL 版（一个 DLL，无 OpenSSL）。若您坚持零外部 DLL，可退而用手写，但我会明确标注风险。
- **共识：V2 用 Paho MQTT C（无 SSL 版，单 DLL，无 OpenSSL）。需您审查。手写为备选。**

### 议题 10：打包与迁移
- **A**：MSVC 编译，`/MT` 静态链接 CRT（目标机无需 vcredist）。libfacedetection/SQLite/cJSON 静态编进 exe；ONNX Runtime（V1）与 Paho（V2）以 DLL 随包。模型 `*.onnx`、`config.ini`、`templates.db`、`app.ico` 随包。
- **B**：最终产物 = 一个文件夹：`face_client.exe + onnxruntime.dll(+paho-mqtt3c.dll) + models/*.onnx + config.ini + templates.db + app.ico`。**复制到任意 Windows x64(支持AVX2) 机器即可运行，无需安装。**
- **共识：MSVC /MT 静态 CRT，拷贝即用。**

### 议题 11：提醒级别与自定义消息
- **A**：模板表加 `alert_level`（0/1/2）；INI 里为每级别定义 标题/颜色/停留/声音 + 消息模板；未知人脸单独配置是否提醒及消息。
- **B**：模板用 `wsprintf`/`std::format`(C++20) 或简单 `%` 占位符替换 `{name}/{score}/{device_id}/{track_id}/{time}/{bbox}`。
- **共识：按上设计。**

---

## 2. 依赖清单（C++ 版 · 需逐条审查）

| 依赖 | 类型 | 阶段 | 必要性 | 说明 |
|---|---|---|---|---|
| Visual Studio 2022 Build Tools + CMake | 构建期 | V1 | 必须(开发机) | 目标机不需要 |
| **ONNX Runtime C++**（onnxruntime.dll/lib/头） | 运行时(随包DLL) | **V1** | **必须** | Microsoft MIT，跑 SFace；V1 唯一外部运行时依赖 |
| libfacedetection 源码（已在仓库） | 源码内嵌编译 | V1 | 必须 | BSD，静态编进，无安装 |
| SQLite amalgamation（sqlite3.c/h） | 源码内嵌编译 | V1 | 必须 | 公有领域，无安装 |
| cJSON（.c/.h） | 源码内嵌编译 | V1 | 必须 | MIT，无安装 |
| GDI+ / comctl32 / Shell_NotifyIcon / Winsock2 | 系统自带 | V1 | 必须 | Windows 内置，零安装 |
| **Paho MQTT C**（paho-mqtt3c.dll，无SSL） | 运行时(随包DLL) | **V2** | **必须** | MIT，无 OpenSSL；V2 外部运行时依赖 |
| Qt（若选 Qt GUI） | 运行时(随包DLL) | - | **备选/默认不用** | 大依赖 + LGPL |
| OpenCV DNN | - | - | **不引入** | 备选推理引擎，默认不用 |
| numpy/Python | - | - | **不引入** | 已改为纯 C++ |

> **V1 外部运行时依赖 = 1（ONNX Runtime）；V2 再 +1（Paho MQTT C）。** 其余均为源码内嵌或系统自带，无需"安装"。

---

## 3. 设计与结构

### 3.1 目录结构
```
client_demo/
├─ CMakeLists.txt                  # 顶层构建（MSVC，/MT，AVX2）
├─ src/
│  ├─ main.cpp                     # WinMain：装配、消息循环
│  ├─ app.h/cpp                    # 应用对象，模块装配，线程队列
│  ├─ config.h/cpp                 # INI 读写（GetPrivateProfileString）
│  ├─ db.h/cpp                     # SQLite 模板库 CRUD + 导入导出
│  ├─ face/
│  │  ├─ face_pipeline.h/cpp       # 图片→检测→对齐→SFace→128维归一化
│  │  ├─ align.h/cpp               # 5点相似变换 + 双线性 warp
│  │  └─ recognition.h/cpp         # 余弦比对/阈值/冷却去重
│  ├─ onnx/
│  │  └─ sface_runner.h/cpp        # ONNX Runtime 封装（加载/预处理/推理）
│  ├─ ui/
│  │  ├─ main_window.h/cpp         # 主窗：ListView模板表 + 按钮 + 状态栏
│  │  ├─ template_dialog.h/cpp     # 新增/编辑：选图/预览/检测框/级别/备注
│  │  ├─ toast_window.h/cpp        # 右下角自绘弹窗（堆叠/定时/配色）
│  │  ├─ tray.h/cpp                # Shell_NotifyIcon 托盘（最小化挂起/恢复/退出）
│  │  └─ settings_dialog.h/cpp     # 设置：MQTT/识别/通知/弹窗测试
│  ├─ net/
│  │  └─ mqtt_client.h/cpp         # Paho C 封装（V2）
│  └─ notify/
│     └─ notifier.h/cpp            # 识别结果→弹窗映射（V2）
├─ third_party/                    # 随源码携带（无需安装）
│  ├─ sqlite/sqlite3.c, sqlite3.h
│  ├─ cjson/cJSON.c, cJSON.h
│  └─ onnxruntime/                 # ONNX Runtime 头+lib（dll 拷到 dist）
│  └─ paho/                        # Paho 头+lib（V2，dll 拷到 dist）
├─ libfacedetection/               # 已存在，静态编入
├─ models/face_recognition_sface_2021dec.onnx   # 已存在
├─ assets/
│  ├─ app.ico
│  └─ default_config.ini
├─ dist/                           # 构建产物（打包发布用）
├─ build.bat                       # CMake + MSVC 构建脚本
├─ run.bat
└─ README.md
```

### 3.2 sqlite 表结构
```sql
CREATE TABLE templates (
  id              INTEGER PRIMARY KEY AUTOINCREMENT,
  name            TEXT    NOT NULL,
  embedding       TEXT    NOT NULL,   -- JSON 数组，128 float，L2 归一化
  embedding_model TEXT,                -- opencv_sface_2021dec
  alert_level     INTEGER NOT NULL DEFAULT 0,  -- 0/1/2
  note            TEXT,
  thumb_path      TEXT,                -- 可选：入库预览缩略图
  created_at      INTEGER NOT NULL,    -- Unix 时间戳
  updated_at      INTEGER NOT NULL
);
CREATE INDEX idx_templates_name ON templates(name);
```
- 入库（图片）：检测→取最大/最高分人脸→5点对齐→SFace→L2 归一化→存。
- 入库（导入）：从 JSON 文件导入（格式同 MQTT `embedding` 字段），**强制再归一化**。
- 导出：整库导出为 JSON，便于备份/迁移。

### 3.3 人脸入库流水线（本地图片）
```
JPEG/PNG/BMP
  └─[GDI+ 解码]→ BGR 三通道图
       └─[libfacedetection objectdetect_cnn]→ vector<FaceRect>(含5点)
            └─[选最大脸]→ 5 关键点
                 └─[相似变换→112×112 标准5点]→ 对齐 BGR
                      └─[ONNX Runtime SFace]→ 128维
                           └─[L2 归一化]→ 存库
```

### 3.4 config.ini 关键节
```ini
[general]
minimize_on_close=1
single_instance=1

[face]
model_path=models/face_recognition_sface_2021dec.onnx
detect_min_score=0.5          ; libfacedetection 置信度下限
align_size=112

[recognition]
threshold=0.5                 ; 余弦相似度阈值（SFace 经验~0.363，0.5 偏严）
min_detection_score=0.5       ; MQTT payload score 下限
cooldown_seconds=30           ; 同 track_id/同人冷却
alert_on_unknown=1
unknown_alert_level=1

[mqtt]                        ; V2
enabled=0
host=
port=1883
topic=facedetect/events
client_id=face_client
username=
password=
qos=1
keepalive=60
reconnect_delay=5

[storage]
db_path=templates.db

[ui]
popup_duration_ms=6000
popup_max_visible=3

[notifications]
level_0=已知人员|#2E7D32|4000|0
level_1=提醒|#F9A825|6000|1
level_2=重要告警|#C62828|0|1
unknown_title=未知人员
unknown_message=检测到未登记人员\n设备:{device_id}\n置信度:{score:.2f}\n时间:{time}
known_message=识别到:{name}\n设备:{device_id}\n相似度:{score:.2f}\n时间:{time}
```

### 3.5 线程模型
- **主线程（UI）**：Win32 消息循环，所有窗口操作在此。
- **入库工作线程**：图片解码/检测/推理较重（~100ms），放工作线程；完成后 `PostMessage` 到主窗刷新列表。
- **MQTT 线程（V2）**：Paho 同步客户端在独立线程循环；收到消息→比对→把"识别结果"通过 `PostMessage` 投递主窗→主窗调 notifier 弹窗。绝不跨线程直接动 UI。
- **托盘**：回调走主线程消息循环（`WM_USER` 自定义消息），天然同线程，无需跨线程队列。

---

## 4. 实施计划（分阶段）

### V1（GUI + 模板库 CRUD + 本地图片入库 + 托盘 + 弹窗）
1. 工程骨架：`CMakeLists.txt`（MSVC、`/MT`、AVX2、manifest 启用 comctl32 v6 视觉样式）、`build.bat`、`assets/default_config.ini`、`app.ico`。
2. `third_party` 就位：SQLite amalgamation、cJSON；ONNX Runtime 头/lib/dll（**需您审查后我下载**）。
3. `config.cpp`：INI 读写 + 首启生成。
4. `db.cpp`：建表 + CRUD + 导入导出 JSON + 强制 L2 归一化。
5. `onnx/sface_runner.cpp`：加载 SFace、112×112 BGR 输入、输出 128 维。
6. `face/align.cpp` + `face/face_pipeline.cpp`：GDI+ 解码 → objectdetect_cnn → 选脸 → 相似变换对齐 → SFace → 归一化。
7. `ui/tray.cpp`：Shell_NotifyIcon（关闭即隐藏到托盘、右键菜单、双击恢复、退出）。
8. `ui/toast_window.cpp`：右下角自绘弹窗（堆叠/定时/级别配色/占位符替换）。
9. `ui/template_dialog.cpp`：选图→预览→检测框可视化→级别/备注→入库。
10. `ui/main_window.cpp`：ListView 模板表 + 增删改查按钮 + **"测试弹窗"按钮 + 弹窗内容编辑器** + 状态栏。
11. `ui/settings_dialog.cpp`：识别/通知参数 + 弹窗预览。
12. `main.cpp`：装配、单实例互斥量、消息循环。
13. `README.md`：构建/使用/打包说明。

**V1 验收**：
- 模板库增删改查；**支持选择本地图片入库**（自动检测+对齐+提取128维特征）；支持 JSON 导入/导出；≤200 人流畅。
- 点窗口关闭按钮 → 隐藏到托盘挂起（类 QQ/微信），托盘可恢复/退出。
- "测试弹窗"按钮触发右下角弹窗，标题/正文/级别/时长可自定义并即时预览。
- 产物为可复制迁移的文件夹（含 onnxruntime.dll + 模型 + 配置）。

### V2（MQTT 监听 + 识别 + 分级弹窗）
14. `net/mqtt_client.cpp`：Paho C 连接/订阅/断线重连/解析 payload（按 `message_parse.md`）。
15. `face/recognition.cpp`：流式 128 维 vs 模板库余弦比对 + 阈值 + 冷却。
16. `notify/notifier.cpp`：按 `alert_level` 与未知策略映射弹窗 + 消息模板。
17. UI 扩展：MQTT 连接状态指示、最近检测列表、**"把当前检测存为模板"**按钮、MQTT/通知设置。
18. 更新 `build.bat`/README：打包含 `paho-mqtt3c.dll`。

**V2 验收**：
- 持续监听 MQTT，收到特征即比对识别。
- 按识别结果弹窗，未知人脸是否提醒可配置。
- 模板库中不同人脸可设不同提醒级别，对应不同弹窗消息。
- 支持"从实时流捕获入库"。

---

## 5. 待您裁决的关键问题（Grill-Me 清单）

> 请逐条 **保留/修改/删除** 或补充。

1. **GUI 框架**：选 **Win32+comctl32+GDI+（零第三方，代码多）** [推荐] 还是 **Qt（代码少、UI 好，但大依赖+LGPL）**？
2. **SFace 推理引擎**：确认用 **ONNX Runtime C++（CPU EP，随包 DLL）**？这是 V1 唯一外部运行时依赖。
3. **图像解码/对齐**：GDI+ 解码 + libfacedetection 5 点 + 手写相似变换到 112×112（ArcFace 标准参考点）。同意？
4. **libfacedetection 集成**：源码静态编入、调用 `objectdetect_cnn()`（已确认返回含 lm 的 FaceRect）。同意？
5. **存储**：SQLite amalgamation（公有领域，源码内嵌）。同意？
6. **JSON**：vendored cJSON（MIT，源码内嵌）。同意？（备选 nlohmann/json）
7. **配置**：INI + Win32 API。同意？
8. **MQTT（V2）**：选 **Paho MQTT C 无 SSL 版（单 DLL，无 OpenSSL）** [推荐] 还是 **手写最小 Winsock MQTT（零外部 DLL，但风险高）**？
9. **打包**：MSVC `/MT` 静态 CRT，拷贝文件夹即用；**目标 CPU 需支持 AVX2**（libfacedetection）。同意？若需兼容老 CPU，我可加非 AVX2 回退编译分支。
10. **ONNX Runtime 获取方式**：我下载官方 Windows x64 预编译包（github.com/microsoft/onnxruntime）放入 `third_party/onnxruntime/`。同意此来源？
11. **阈值默认值**：余弦阈值 `0.5`、检测 score 下限 `0.5`、冷却 `30s`。需调整吗？（SFace 经验 ~0.363，0.5 偏严。）
12. **MQTT 服务端信息**：host/port/账号/topic 您提供，还是配置留空自填？
13. **构建工具链**：您机器是否已装 Visual Studio 2022（或 Build Tools）+ CMake？需要我给安装指引吗？
14. **界面语言**：默认中文。需要英文吗？
15. **单实例 / 图标**：禁止多开（互斥量）？图标您提供 `.ico` 还是我用占位？

---

## 6. 风险与缓解
- **R1 ONNX Runtime 体积/版本**：用 CPU EP、固定版本（如 1.17.x）；DLL ~15MB，可接受。
- **R2 AVX2 兼容**：默认 AVX2；如目标机老旧，加非 AVX2 回退（编译分支）。
- **R3 特征未归一化**：入库强制 L2 归一化；流式按文档已归一化，运行时校验模长≈1。
- **R4 阈值不准**：配置项 + UI 可调 + 日志输出相似度。
- **R5 跨线程 UI**：严格 `PostMessage` 到主线程。
- **R6 Win32 代码量**：模块拆分清晰、注释充分；如开发成本超预期可回退 Qt（需您同意）。
- **R7 SFace 输入约定**：需核对 opencv_zoo SFace 的预处理（BGR、是否减均值/归一化到 [0,1] 或 [-1,1]）。我会在实现前先做一次"已知图→特征"比对验证，确保与边缘端一致。

---
*本文档为 C++ 版计划草案，等待您的审查与批准。批准前不会创建任何业务代码文件或下载/安装任何依赖。*
