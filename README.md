# Boss / Advisor Detector · Windows Client (face_client)

> **Language / 语言:** English | [简体中文](README.zh-CN.md)

> When your advisor/boss appears in front of the camera, the client immediately pops up a reminder in the bottom‑right corner — giving you just enough time to switch windows, sit up straight, and close whatever you were slacking off on.
>
> This repository is the **client**: it maintains the template library of "people to watch", subscribes to face embeddings reported by the edge device, runs cosine comparison, and pops up alerts by level.
>
> 🔗 Edge project (Raspberry Pi / camera side — detection + embedding extraction + MQTT reporting):
> [**Boss-Detector-edge**](https://github.com/porrk/Boss-Detector-edge)

## What it does

1. **Enrollment**: Register front‑face photos of people to watch (advisor, boss, …). Locally performs detection → 5‑point alignment → SFace 128‑D embedding → L2 normalization → store.
2. **Listening**: Subscribes to the edge device's MQTT `facedetect/#` topic to receive detected face embeddings (128‑D) in real time.
3. **Comparison**: On receiving an embedding, compares it against the template library by cosine similarity; on a hit, pops an alert at that person's level; on a miss, optionally pops an "unknown person" alert.
4. **Aggregation**: Multiple photos of the same person are weighted‑averaged (by detection score) into a single aggregate, improving robustness. Name duplicate check, look‑alike check, and low/high similarity hints are performed during enrollment/aggregation.
5. **Enroll from detection**: Faces captured by the edge device can be enrolled with one click (embedding pre‑filled, going through the same dedup/aggregation flow).

## Features

### ✅ V1: Template library & local enrollment
- **Template library CRUD**: Built on SQLite; supports add/edit/delete, JSON import/export, sorted by name.
- **Local image enrollment**: Pick image → libfacedetection detection (with 5 keypoints) → similarity transform alignment to 112×112 → ONNX Runtime runs SFace → L2 normalization → store.
- **Close = minimize to tray**: Clicking close hides the app to the system tray (QQ/WeChat‑style); double‑click the tray icon to restore, right‑click for the quit menu.
- **Bottom‑right toast popups**: Self‑drawn toasts, colored by level, stacked, auto‑closing; the toolbar "Test popup" verifies instantly; the settings panel lets you customize per‑level title / color / duration / sound and message templates.
- **Single instance**: Multi‑open is disabled by default.
- **Config file**: All parameters live in `config.ini` (INI format, UTF‑8).

### ✅ V2: MQTT listening & real‑time recognition
- **MQTT subscription**: Hand‑written MQTT 3.1.1 client (built on QTcpSocket, **zero external dependencies — no Paho/OpenSSL**), with auto‑reconnect.
- **Real‑time comparison**: On receiving a 128‑D embedding, compares it against the template library by cosine similarity and pops an alert by `alert_level`; whether to alert on unknown faces is configurable.
- **Cooldown dedup**: The same `track_id` / same person alerts only once within the cooldown window to prevent spam.
- **Status bar**: Shows MQTT connection status and the latest detection result in real time.
- **Enroll from detection**: The toolbar "Enroll from detection" feeds the latest unknown detection's embedding directly into the enrollment dialog (pre‑filled) — no local image picking needed.

### ✅ Template aggregation (multi‑template merge)
- **Weighted‑average aggregation**: Multiple photos of one person are weighted by detection confidence `A = normalize(Σ score_i · emb_i)` and stored as a single aggregate row; recognition performs a max‑cosine match against aggregates.
- **One row per person**: Names are unique in the template table; each person has exactly one aggregate and one alert level.
- **Merge into existing person**: When adding a template you may choose "merge into existing person", pick from a drop‑down, then aggregate.
- **Enrollment safety checks**:
  - Name duplicate: on a duplicate name, a dialog asks "same person (aggregate) / different person (rename)".
  - Look‑alike check: if a newcomer's similarity to an existing person > 0.50, prompts "is this the same person?".
  - Low‑similarity warning: if the new embedding's similarity to the aggregate < 0.20, warns "possible mismatch".
  - High‑similarity hint: > 0.95 hints "possible duplicate enrollment".
  - Soft sample cap: prompts for confirmation when a person exceeds 10 samples.
- **Edit‑mode reset**: Re‑picking an image and re‑detecting in edit mode resets that person to a single sample.

## Architecture

```
┌─────────────┐   MQTT(facedetect/#)   ┌──────────────────────────────────┐
│  Edge        │ ─────────────────────▶ │  Client face_client              │
│ camera+detect│  JSON: device_id,      │                                  │
│ +SFace embed │  track_id,score,       │  MQTT subscribe → parse → renorm │
│ +report      │  embedding[128]        │   → cosine match vs templates    │
└─────────────┘                        │   → alert by level / unknown     │
                                       │  Local image enroll → detect→align→SFace │
                                       │  Template DB SQLite(templates+samples) │
                                       └──────────────────────────────────┘
```

## Tech stack & dependencies
- **C++17 / Qt5** (Widgets, Concurrent, Network) / MSVC (VS2022)
- **libfacedetection** (bundled in repo, BSD, statically linked; **AVX2 disabled** for maximum CPU compatibility)
- **ONNX Runtime 1.15.x** (C API version 18, Microsoft MIT, DLL shipped, runs SFace; **the only external runtime dependency**)
- **SQLite amalgamation 3.46.1** (public domain, source embedded)
- **cJSON 1.7.18** (MIT, source embedded)
- **MQTT**: hand‑written 3.1.1 protocol over QTcpSocket (no Paho, no OpenSSL, no extra DLLs)

> Runtime third‑party dependency: only `onnxruntime.dll` (shipped). Everything else is either source‑embedded or provided by Qt/the OS.

## Build (Windows + VS2022 + Qt5 + CMake)

Prerequisites: Visual Studio 2022 (with C++), CMake, Qt5 (msvc2017_64 / msvc2019_64).
Two large dependencies must be placed manually (not version‑controlled, see `third_party/README.md`):
1. **ONNX Runtime 1.15.1 win‑x64**: extract and place `include/`, `lib/`, `bin/` into `third_party/onnxruntime/`.
2. **SFace model**: per `models/README.md`, download `face_recognition_sface_2021dec.onnx` into `models/`.

Then:
1. Set the Qt5 path (either):
   - Environment variable: `set QT5_DIR=C:\Qt\5.15.2\msvc2019_64`
   - Or let `build.bat` auto‑detect common install locations
2. From the project root:
   ```
   build.bat
   ```
   The script will: CMake configure (VS2022 x64) → build → `windeployqt` to package Qt deps → copy ONNX models to `dist\`.

Artifacts land in `dist\`: `face_client.exe` + Qt DLLs + `onnxruntime.dll` + `models\*.onnx` + `run.bat`.

Run: double‑click `dist\run.bat` or `dist\face_client.exe`.

> On first run, `config.ini` (copied from `assets/default_config.ini`) and `templates.db` are generated next to the exe.

## Migrating to another machine
Copy the entire `dist\` folder to the target machine (Windows x64) — no installation needed.
- If it complains about missing `VCRUNTIME140.dll` etc., install the [VC++ 2015‑2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe) once.
- The target CPU does **not** need AVX2 (it is disabled).

## Config file `config.ini`
Generated from the built‑in default on first launch; edit with any text editor (restart to apply; or change it inside the "Settings" panel). Key entries:

| Section.key | Description |
|---|---|
| general.minimize_on_close | 1 = close minimizes to tray |
| general.single_instance | 1 = disallow multi‑open |
| face.model_path | Relative path to the SFace ONNX model |
| face.detect_min_score | Lower bound for libfacedetection confidence |
| face.input_mean / input_std / swap_rb | SFace preprocessing (opencv_zoo convention: 127.5 / 128 / RGB) |
| recognition.threshold | Cosine similarity threshold (higher = stricter; SFace empirical ≈ 0.363) |
| recognition.min_detection_score | Lower bound for MQTT payload.score |
| recognition.cooldown_seconds | Cooldown dedup for same track_id/person (seconds) |
| recognition.alert_on_unknown | Whether to alert on unknown faces |
| recognition.unknown_alert_level | Default alert level for unknown persons |
| recognition.aggregate_low_threshold | Aggregation: warn when new embedding's similarity to the aggregate is below this (default 0.20) |
| recognition.aggregate_high_threshold | Aggregation: hint at possible duplicate above this (default 0.95) |
| recognition.face_match_threshold | Look‑alike check threshold for new persons (default 0.50) |
| recognition.aggregate_soft_cap | Soft cap of samples per person (default 10) |
| mqtt.* | MQTT connection params (host/port/topic/client_id/username/password/qos/keepalive, etc.) |
| notifications.level_N | `title\|colorRRGGBB\|dwell_ms(0=no auto‑close)\|sound1/0` |
| notifications.known_message / unknown_message | Message templates, support {name} {device_id} {track_id} {score} {time} |

## Enrollment notes

### Local image enrollment
Toolbar "Add template" → pick an image → "Detect & extract embedding" → preview the framed image → choose "New person" or "Merge into existing person" → fill name/level/note → OK.
- For new persons, a name duplicate check and a look‑alike check are performed automatically.
- When merging into an existing person, a similarity check runs against the aggregate (too‑low / too‑high / over cap each prompt for confirmation).

### Enroll from detection
When the edge device detects an unknown person, the toolbar "Enroll from detection" becomes enabled. Clicking it opens the enrollment dialog pre‑filled with that detection's embedding and score; the flow is the same as above.

### JSON import / export
For backup/migration. Format:
```json
[{"name":"Zhang San","embedding":[...128 floats...],
  "embedding_model":"opencv_sface_2021dec","alert_level":0,"note":"emp id 1001"}]
```
On import, **L2 normalization is forced again**, and the first sample is created for each template.

## On thresholds and missed detections

> **Important**: If you "enrolled someone but recognition keeps failing (very low similarity)", it is usually **not a threshold problem** — it's that **the embedding produced by the edge device is inconsistent with the locally enrolled embedding**.

The client pipeline is verified correct: alignment uses the standard ArcFace 5‑point reference + similarity transform; SFace preprocessing is mean=127.5/std=128/RGB/NCHW; recognition uses the dot product of normalized vectors (= cosine similarity). If the cosine similarity between the edge query embedding and the local template of the same person is only ~0.1x (i.e. stranger level), investigate the **edge device's** alignment/preprocessing/model consistency first, rather than lowering the threshold. Aggregating more photos improves robustness but cannot fix embedding drift at the edge.

## Directory layout
```
client_demo/
├─ CMakeLists.txt
├─ build.bat / run.bat
├─ assets/default_config.ini       # config template
├─ src/
│  ├─ main.cpp / app.h/cpp         # wiring & single instance
│  ├─ config.h/cpp                 # config.ini read/write
│  ├─ db.h/cpp                     # SQLite CRUD + aggregation + migration
│  ├─ face/
│  │  ├─ align.{h,cpp}             # 5‑point similarity‑transform alignment
│  │  ├─ face_pipeline.{h,cpp}     # image → detect → align → SFace
│  │  └─ recognition.{h,cpp}       # cosine match / threshold / cooldown
│  ├─ onnx/sface_runner.{h,cpp}    # ONNX Runtime wrapper
│  ├─ net/mqtt_client.{h,cpp}      # hand‑written MQTT 3.1.1 (QTcpSocket)
│  ├─ notify/notifier.{h,cpp}      # recognition result → toast mapping
│  └─ ui/
│     ├─ main_window.{h,cpp}       # main window: template table + toolbar + status bar
│     ├─ template_dialog.{h,cpp}   # enroll / aggregate / dedup / look‑alike
│     ├─ settings_dialog.{h,cpp}   # settings panel
│     └─ toast_window.{h,cpp}      # self‑drawn bottom‑right toast
├─ libfacedetection/               # statically linked
├─ third_party/
│  ├─ onnxruntime/                 # ❌ manual download (see third_party/README.md)
│  ├─ sqlite/  cjson/              # ✅ source embedded
│  └─ paho/                        # placeholder (unused; MQTT goes through Qt)
├─ models/
│  ├─ README.md                    # model download URL + SHA‑256
│  └─ *.onnx                       # ❌ manual download
└─ dist/                           # build artifacts (not in repo)
```

## FAQ
- **Enrollment says "SFace model not loaded"**: check `config.ini`'s `face.model_path` and whether the model exists under `dist\models\`.
- **Can't recognize an enrolled person (very low similarity)**: see "On thresholds and missed detections" above — usually edge‑side embedding inconsistency, not the threshold.
- **Enrollment fails due to low detection confidence**: lower `face.detect_min_score`, or use a front‑facing, sharp, well‑lit photo.
- **MQTT won't connect**: check `config.ini`'s `mqtt.host/port/username/password` and network connectivity.
- **Tray icon not showing**: on Windows, enable the icon in the notification‑area settings.

## License
- This project's code: **MIT License**, see [LICENSE](LICENSE).
- Third party: libfacedetection (BSD), ONNX Runtime (MIT), SQLite (public domain), cJSON (MIT). Licenses live in their respective directories.
