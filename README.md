# 导师/老板检测器 · Windows 客户端（face_client）

> 当导师/老板出现在摄像头前，客户端右下角立刻弹窗提醒——给你足够的反应时间切屏、端正坐姿、关掉摸鱼窗口。
>
> 本仓库是**客户端**：维护"要盯的人"模板库，订阅边缘端上报的人脸特征，做余弦比对后按级别弹窗。
>
> 🔗 边缘端项目（树莓派/摄像头侧，负责检测+提取特征+MQTT 上报）：
> **<TODO: 替换为导师/老板检测器边缘端仓库地址>**

## 它做什么

1. **入库**：把要盯的人（导师、老板……）的正脸照片录入模板库，本地完成 检测→5点对齐→SFace 提取 128 维特征→L2 归一化→入库。
2. **监听**：通过 MQTT 订阅边缘端 `facedetect/#` 主题，实时接收检测到的人脸特征（128 维嵌入）。
3. **比对**：收到特征后与模板库做余弦相似度比对，命中则按该人员的提醒级别弹窗；未命中可按配置弹出"未知人员"提醒。
4. **聚合**：同一个人可录入多张照片，按检测分数加权平均成一个聚合体，提升识别鲁棒性；新增/聚合时有姓名查重、撞脸检查、相似度过低/过高提示。
5. **从检测入库**：边缘端抓到的人脸可直接一键入库（预填特征，走同样的查重/聚合流程）。

## 功能

### ✅ V1：模板库与本地入库
- **模板库 CRUD**：基于 SQLite，支持新增/编辑/删除/JSON 导入导出，按姓名排序。
- **本地图片入库**：选图 → libfacedetection 检测（含 5 关键点）→ 相似变换对齐到 112×112 → ONNX Runtime 跑 SFace → L2 归一化 → 入库。
- **关闭即最小化到托盘**：点关闭按钮隐藏到系统托盘（类 QQ/微信），托盘双击恢复，右键菜单退出。
- **右下角弹窗**：自绘 Toast，按级别配色、堆叠、定时关闭；工具栏"测试弹窗"可即时验证；设置面板可自定义各级别标题/颜色/时长/声音与消息模板。
- **单实例**：默认禁止多开。
- **配置文件**：所有参数走 `config.ini`（INI 格式，UTF-8）。

### ✅ V2：MQTT 监听与实时识别
- **MQTT 订阅**：手写 MQTT 3.1.1 客户端（基于 QTcpSocket，**零外部依赖，无 Paho/OpenSSL**），支持断线自动重连。
- **实时比对**：收到 128 维特征即与模板库余弦比对，按 `alert_level` 弹窗；未知人脸是否提醒可配置。
- **冷却去重**：同一 `track_id`/同一人在冷却时间内只提醒一次，防刷屏。
- **状态栏**：实时显示 MQTT 连接状态与最近一次检测结果。
- **从检测入库**：工具栏"从检测入库"把最近一次未知检测的特征直接送入入库对话框（预填），无需本地选图。

### ✅ 模板聚合（多模板合并）
- **加权平均聚合**：同一人录入多张照片，按检测置信度加权 `A = normalize(Σ score_i · emb_i)`，存为一行聚合体；识别时对聚合体做最大余弦匹配。
- **一人一行**：模板表姓名唯一，每人只有一个聚合体与一个提醒级别。
- **归入现有人员**：新增模板时可选择"归入现有人员"，下拉选人后聚合。
- **入库安全检查**：
  - 姓名查重：同名时弹窗选择"同一人(聚合) / 另一人(改名)"。
  - 撞脸检查：新人与已登记人员相似度 > 0.50 时提示"是否同一人"。
  - 低相似度警告：新特征与聚合体相似度 < 0.20 提示"可能贴错人"。
  - 高相似度提示：> 0.95 提示"可能重复录入"。
  - 样本软上限：每人超过 10 条样本时提示确认。
- **编辑模式重置**：编辑时重新选图检测 = 把该人员重置为单样本。

## 架构

```
┌─────────────┐   MQTT(facedetect/#)   ┌──────────────────────────────────┐
│  边缘端      │ ─────────────────────▶ │  客户端 face_client               │
│ 摄像头+检测  │  JSON: device_id,      │                                  │
│ +SFace提取   │  track_id,score,       │  MQTT订阅 → 解析 → 重归一化       │
│ +上报        │  embedding[128]        │   → 余弦比对模板库(聚合体)        │
└─────────────┘                        │   → 命中按级别弹窗 / 未知提醒     │
                                       │  本地图片入库 → 检测→对齐→SFace    │
                                       │  模板库 SQLite(templates+samples) │
                                       └──────────────────────────────────┘
```

## 技术栈与依赖
- **C++17 / Qt5**（Widgets、Concurrent、Network）/ MSVC（VS2022）
- **libfacedetection**（仓库自带，BSD，静态编入；**AVX2 已关闭**以保证最大 CPU 兼容）
- **ONNX Runtime 1.15.x**（C API version 18，Microsoft MIT，随包 DLL，跑 SFace；**唯一外部运行时依赖**）
- **SQLite amalgamation 3.46.1**（公有领域，源码内嵌）
- **cJSON 1.7.18**（MIT，源码内嵌）
- **MQTT**：手写 3.1.1 协议 over QTcpSocket（无 Paho、无 OpenSSL、无额外 DLL）

> 运行时第三方依赖：仅 `onnxruntime.dll`（随包）。其余均为源码内嵌或 Qt/系统自带。

## 构建（Windows + VS2022 + Qt5 + CMake）

前置：Visual Studio 2022（含 C++）、CMake、Qt5（msvc2017_64 / msvc2019_64）。
另需手动放置两个大体积依赖（不入版本控制，见 `third_party/README.md`）：
1. **ONNX Runtime 1.15.1 win-x64**：解压后把 `include/`、`lib/`、`bin/` 放到 `third_party/onnxruntime/`。
2. **SFace 模型**：按 `models/README.md` 下载 `face_recognition_sface_2021dec.onnx` 到 `models/`。

然后：
1. 设置 Qt5 路径（任选其一）：
   - 环境变量：`set QT5_DIR=C:\Qt\5.15.2\msvc2019_64`
   - 或让 `build.bat` 自动探测常见安装位置
2. 在项目根目录执行：
   ```
   build.bat
   ```
   脚本会：CMake 配置(VS2022 x64) → 构建 → `windeployqt` 打包 Qt 依赖 → 拷贝 ONNX 模型到 `dist\`。

产物在 `dist\`：`face_client.exe` + Qt DLLs + `onnxruntime.dll` + `models\*.onnx` + `run.bat`。

运行：双击 `dist\run.bat` 或 `dist\face_client.exe`。

> 首次运行会在 exe 同级生成 `config.ini`（从 `assets/default_config.ini` 拷贝）与 `templates.db`。

## 迁移到其他电脑
把整个 `dist\` 文件夹复制到目标机（Windows x64）即可运行，无需安装。
- 若提示缺少 `VCRUNTIME140.dll` 等，安装一次 [VC++ 2015-2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)。
- 目标 CPU 无需支持 AVX2（已关闭）。

## 配置文件 `config.ini`
首启从内置默认生成，可用任意编辑器修改（改后重启生效；也可在"设置"面板内修改）。关键项：

| 节.键 | 说明 |
|---|---|
| general.minimize_on_close | 1=关闭最小化到托盘 |
| general.single_instance | 1=禁止多开 |
| face.model_path | SFace ONNX 模型相对路径 |
| face.detect_min_score | libfacedetection 检测置信度下限 |
| face.input_mean / input_std / swap_rb | SFace 预处理（opencv_zoo 约定 127.5 / 128 / RGB） |
| recognition.threshold | 余弦相似度阈值（越大越严；SFace 经验约 0.363） |
| recognition.min_detection_score | MQTT payload.score 下限 |
| recognition.cooldown_seconds | 同 track_id/同人冷却去重（秒） |
| recognition.alert_on_unknown | 未知人脸是否提醒 |
| recognition.unknown_alert_level | 未知人员默认提醒级别 |
| recognition.aggregate_low_threshold | 聚合：新特征与聚合体相似度低于此值警告（默认 0.20） |
| recognition.aggregate_high_threshold | 聚合：高于此值提示可能重复（默认 0.95） |
| recognition.face_match_threshold | 新增人员撞脸检查阈值（默认 0.50） |
| recognition.aggregate_soft_cap | 每人样本数软上限（默认 10） |
| mqtt.* | MQTT 连接参数（host/port/topic/client_id/username/password/qos/keepalive 等） |
| notifications.level_N | `标题\|颜色RRGGBB\|停留毫秒(0=不自动关)\|声音1/0` |
| notifications.known_message / unknown_message | 消息模板，支持 {name} {device_id} {track_id} {score} {time} |

## 入库说明

### 本地图片入库
工具栏"新增模板" → 选择图片 → "检测并提取特征" → 预览带框图 → 选择"新增人员"或"归入现有人员" → 填姓名/级别/备注 → 确定。
- 新增人员时会自动做姓名查重与撞脸检查。
- 归入现有人员时会对新特征与聚合体做相似度检查（过低/过高/超上限均弹窗确认）。

### 从检测入库
当边缘端检测到未知人员时，工具栏"从检测入库"启用。点击后打开入库对话框并预填该次检测的特征与置信度，流程同上。

### JSON 导入/导出
用于备份/迁移。格式：
```json
[{"name":"张三","embedding":[...128个float...],
  "embedding_model":"opencv_sface_2021dec","alert_level":0,"note":"工号1001"}]
```
导入时会**强制再次 L2 归一化**，并为每个模板建立首条样本。

## 关于阈值与漏检

> **重要**：若发现"明明录了人却识别不出来（相似度很低）"，通常**不是阈值问题**，而是**边缘端产生的嵌入与本地入库的嵌入不一致**。

客户端流水线已验证正确：对齐使用标准 ArcFace 5 点参考 + 相似变换，SFace 预处理为 mean=127.5/std=128/RGB/NCHW，识别用归一化后的点积（=余弦相似度）。若同一人的边缘查询特征与本地模板余弦相似度仅 0.1x（处于陌生人水平），请优先排查**边缘端**的对齐/预处理/模型一致性，而非调低阈值。聚合多张照片可提升鲁棒性，但无法修复边缘端本身的嵌入漂移。

## 目录结构
```
client_demo/
├─ CMakeLists.txt
├─ build.bat / run.bat
├─ assets/default_config.ini       # 配置模板
├─ src/
│  ├─ main.cpp / app.h/cpp         # 装配与单实例
│  ├─ config.h/cpp                 # config.ini 读写
│  ├─ db.h/cpp                     # SQLite CRUD + 聚合 + 迁移
│  ├─ face/
│  │  ├─ align.{h,cpp}             # 5 点相似变换对齐
│  │  ├─ face_pipeline.{h,cpp}     # 图片→检测→对齐→SFace
│  │  └─ recognition.{h,cpp}       # 余弦比对/阈值/冷却
│  ├─ onnx/sface_runner.{h,cpp}    # ONNX Runtime 封装
│  ├─ net/mqtt_client.{h,cpp}      # 手写 MQTT 3.1.1（QTcpSocket）
│  ├─ notify/notifier.{h,cpp}      # 识别结果→弹窗映射
│  └─ ui/
│     ├─ main_window.{h,cpp}       # 主窗：模板表 + 工具栏 + 状态栏
│     ├─ template_dialog.{h,cpp}   # 入库/聚合/查重/撞脸
│     ├─ settings_dialog.{h,cpp}   # 设置面板
│     └─ toast_window.{h,cpp}      # 右下角自绘弹窗
├─ libfacedetection/               # 静态编入
├─ third_party/
│  ├─ onnxruntime/                 # ❌ 手动下载（见 third_party/README.md）
│  ├─ sqlite/  cjson/              # ✅ 源码内嵌
│  └─ paho/                        # 占位（未使用，MQTT 走 Qt）
├─ models/
│  ├─ README.md                    # 模型下载地址 + SHA-256
│  └─ *.onnx                       # ❌ 手动下载
└─ dist/                           # 构建产物（不入库）
```

## 常见问题
- **入库提示"SFace 模型未加载"**：检查 `config.ini` 的 `face.model_path` 与 `dist\models\` 下模型是否存在。
- **识别不出已入库的人（相似度很低）**：见上文"关于阈值与漏检"，多为边缘端嵌入不一致，而非阈值。
- **检测置信度偏低导致入库失败**：调低 `face.detect_min_score`，或使用正脸、清晰、光照良好的图片。
- **MQTT 连不上**：检查 `config.ini` 的 `mqtt.host/port/username/password`，以及网络连通性。
- **托盘图标不显示**：Windows 需在通知区域设置中显示该图标。

## 许可
- 本项目代码：见仓库声明。
- 第三方：libfacedetection(BSD)、ONNX Runtime(MIT)、SQLite(公有领域)、cJSON(MIT)。许可证随各目录。
