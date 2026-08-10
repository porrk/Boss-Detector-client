# 人脸识别客户端（face_client）

Windows 桌面端人脸识别客户端，C++ + Qt5 实现。本仓库为 **V1**：模板库增删改查（含本地图片入库）、关闭即最小化到托盘、右下角弹窗（可自定义、可测试）。V2（MQTT 监听 + 识别 + 分级提醒）为后续阶段。

## 功能（V1）
- **模板库 CRUD**：基于 SQLite，支持 ≤200 人；可新增/编辑/删除/导入/导出。
- **本地图片入库**：选图 -> libfacedetection 检测（含 5 关键点）-> 相似变换对齐到 112×112 -> ONNX Runtime 跑 SFace -> L2 归一化 -> 入库。
- **关闭即最小化到托盘**：点窗口关闭按钮不退出，隐藏到系统托盘（类 QQ/微信）；托盘双击恢复，右键菜单可退出。
- **右下角弹窗**：自绘 Toast，按级别配色、堆叠、定时关闭；工具栏“测试弹窗”按钮可即时验证；设置面板可自定义各级别标题/颜色/时长/声音与消息模板。
- **单实例**：默认禁止多开。
- **配置文件**：所有运行参数走 `config.ini`（INI 格式，UTF-8）。

## 技术栈与依赖
- C++17 / Qt5（Widgets、Concurrent）/ MSVC（VS2022）
- **libfacedetection**（仓库自带，BSD，静态编入，AVX2 已关闭以保证最大 CPU 兼容）
- **ONNX Runtime 1.18.1**（Microsoft MIT，随包 DLL，跑 SFace）
- **SQLite amalgamation 3.46.1**（公有领域，源码内嵌）
- **cJSON 1.7.18**（MIT，源码内嵌）
- 系统：comctl32 / GDI+（GUI 由 Qt 接管）/ Shell_NotifyIcon（Qt 接管）

> 运行时第三方依赖：仅 `onnxruntime.dll`（随包）。其余均为源码内嵌或系统自带。

目录约定：
```
third_party/
  onnxruntime/{include,lib,bin}   # 已随仓库
  sqlite/sqlite3.{c,h}            # 已随仓库
  cjson/cJSON.{c,h}               # 已随仓库
libfacedetection/                 # 仓库自带（头文件 AVX2 已注释）
models/face_recognition_sface_2021dec.onnx
```

## 构建（Windows + VS2022 + Qt5 + CMake）

前置：已安装 Visual Studio 2022（含 C++）、CMake、Qt5（msvc2017_64 / msvc2019_64）。

1. 设置 Qt5 路径（任选其一）：
   - 环境变量：`set QT5_DIR=C:\Qt\5.15.2\msvc2019_64`
   - 或让 `build.bat` 自动探测 `C:\Qt\...\msvc*_64`
2. 在项目根目录执行：
   ```
   build.bat
   ```
   脚本会：CMake 配置(VS2022 x64) -> 构建 -> `windeployqt` 打包 Qt 依赖 -> 拷贝 ONNX 模型到 `dist\`。

产物在 `dist\`：`face_client.exe` + Qt DLLs + `onnxruntime.dll` + `models\*.onnx` + `run.bat`。

运行：双击 `dist\run.bat` 或 `dist\face_client.exe`。

> 首次运行会在 exe 同级生成 `config.ini` 与 `templates.db`。

## 迁移到其他电脑
把整个 `dist\` 文件夹复制到目标机（Windows x64）即可运行，无需安装。
- 若提示缺少 `VCRUNTIME140.dll` 等，安装一次 [VC++ 2015-2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe)。
- 目标 CPU 无需支持 AVX2（已关闭）。

## 配置文件 `config.ini`
关键项（首启从内置默认生成，可用任意编辑器修改，改后重启生效；也可在“设置”面板内修改）：

| 节.键 | 说明 |
|---|---|
| general.minimize_on_close | 1=关闭最小化到托盘 |
| general.single_instance | 1=禁止多开 |
| face.model_path | SFace ONNX 模型相对路径 |
| face.detect_min_score | libfacedetection 检测置信度下限 |
| face.input_mean / input_std / swap_rb | SFace 预处理（opencv_zoo 约定 127.5 / 128 / RGB） |
| recognition.threshold | 余弦相似度阈值（越大越严） |
| recognition.cooldown_seconds | 同 track_id 冷却去重（V2） |
| recognition.alert_on_unknown | 未知人脸是否提醒（V2） |
| notifications.level_N | `标题\|颜色RRGGBB\|停留毫秒(0=不自动关)\|声音1/0` |
| notifications.known_message / unknown_message | 消息模板，支持 {name} {device_id} {track_id} {score} {time} |
| mqtt.* | V2：MQTT 连接参数 |

## 入库说明
- **本地图片入库**：工具栏“新增模板” -> 选择图片 -> “检测并提取特征” -> 预览带框图 -> 填姓名/级别/备注 -> 确定。
- **JSON 导入/导出**：用于备份/迁移。格式：
  ```json
  [{"name":"张三","embedding":[...128个float...],
    "embedding_model":"opencv_sface_2021dec","alert_level":0,"note":"工号1001"}]
  ```
  导入时会**强制再次 L2 归一化**，与 MQTT 流式特征可比。

## 常见问题
- **入库提示“SFace 模型未加载”**：检查 `config.ini` 的 `face.model_path` 与 `dist\models\` 下模型是否存在。
- **入库成功但与 MQTT 流式特征相似度很低**：SFace 预处理可能与边缘端不一致。核对边缘端使用的均值/标准差/通道顺序，调整 `face.input_mean / input_std / swap_rb`。
- **检测置信度偏低导致入库失败**：调低 `face.detect_min_score`，或使用正脸、清晰、光照良好的图片。
- **托盘图标不显示**：Windows 需在通知区域设置中显示该图标。

## V2 计划（后续）
- 引入 Paho MQTT C（无 SSL 版）持续监听 `facedetect/events`。
- 收到 128 维特征后与模板库余弦比对，按 `alert_level` 与未知策略弹窗，支持冷却去重。
- 支持“从实时流捕获入库”。

## 许可
- 本项目代码：见仓库声明。
- 第三方：libfacedetection(BSD)、ONNX Runtime(MIT)、SQLite(公有领域)、cJSON(MIT)。许可证随各目录。
