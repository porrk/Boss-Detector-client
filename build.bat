@echo off
setlocal enabledelayedexpansion
REM ============================================================
REM  build.bat  --  构建 face_client (VS2022 + Qt5 + CMake)
REM  CMakeLists 的 POST_BUILD 会自动: windeployqt + 拷 ONNX 模型 + run.bat
REM  本文件为 GBK + CRLF 编码（cmd 原生）
REM ============================================================
set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "DIST=%ROOT%dist"
set "LOG=%ROOT%build.log"
set "TMPLOG=%ROOT%_cmd.tmp"

> "%LOG%" echo build started %date% %time%
call :say "============================================================"
call :say " face_client 构建 (VS2022 + Qt5 + CMake)"
call :say "============================================================"

REM ---- 0. 检测 cmake ----
call :say "[STEP 0] 检测 cmake"
where cmake >nul 2>&1
if errorlevel 1 (
  call :say "[ERROR] 未找到 cmake，请安装 CMake 并加入 PATH"
  goto :fail
)
call :say "  cmake 已就绪"

REM ---- 1. 定位 Qt5 ----
call :say "[STEP 1] 定位 Qt5"
if not defined QT5_DIR (
  for %%P in (
    "D:\Program\Qt\Qt5.14.2\5.14.2\msvc2017_64"
    "D:\Program\Qt\Qt5.14.2\5.14.2\msvc2015_64"
    "C:\Qt\5.15.2\msvc2019_64"
    "C:\Qt\5.15.2\msvc2017_64"
    "C:\Qt\Qt5.15.2\5.15.2\msvc2019_64"
    "D:\Qt\5.15.2\msvc2019_64"
    "D:\Qt\5.14.2\msvc2017_64"
  ) do (
    if not defined QT5_DIR if exist "%%~P\lib\Qt5Widgets.lib" set "QT5_DIR=%%~P"
  )
)
if not defined QT5_DIR (
  call :say "[ERROR] 未找到 Qt5，请编辑 build.bat 路径列表或设 QT5_DIR 环境变量"
  goto :fail
)
call :say "  QT5_DIR=!QT5_DIR!"
set "CMAKE_PREFIX_PATH=!QT5_DIR!"
set "PATH=!QT5_DIR!\bin;%PATH%"

REM ---- 2. CMake 配置 ----
call :say "[STEP 2] CMake 配置 (VS2022 x64)"
if not exist "%BUILD%" mkdir "%BUILD%"
if exist "%BUILD%\CMakeCache.txt" (
  findstr /c:"CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022" "%BUILD%\CMakeCache.txt" >nul 2>&1
  if errorlevel 1 (
    call :say "  检测到不兼容的旧缓存，清理 build/ 后重新配置"
    rmdir /s /q "%BUILD%" 2>nul
    mkdir "%BUILD%" 2>nul
  )
)
cmake -S "%ROOT%." -B "%BUILD%" -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release > "%TMPLOG%" 2>&1
set "RC=!errorlevel!"
type "%TMPLOG%"
type "%TMPLOG%" >> "%LOG%"
if !RC! neq 0 (
  call :say "[ERROR] CMake 配置失败（上方为 cmake 原始输出，详见 build.log）"
  goto :fail
)

REM ---- 3. 构建（POST_BUILD 自动 windeployqt + 拷模型 + 拷 run.bat）----
call :say "[STEP 3] 构建 (Release) —— POST_BUILD 自动打包 Qt/模型/run.bat"
cmake --build "%BUILD%" --config Release > "%TMPLOG%" 2>&1
set "RC=!errorlevel!"
type "%TMPLOG%"
type "%TMPLOG%" >> "%LOG%"
if !RC! neq 0 (
  call :say "[ERROR] 构建失败（上方为编译/链接原始输出，详见 build.log）"
  goto :fail
)

REM ---- 4. 清理无用文件 ----
call :say "[STEP 4] 清理 pdb / 旧 Release 目录"
del /q "%DIST%\*.pdb" >nul 2>&1
if exist "%DIST%\Release\" rmdir /s /q "%DIST%\Release" 2>nul
if exist "%TMPLOG%" del "%TMPLOG%" >nul 2>&1
call :say "  已清理"

call :say "============================================================"
call :say "[OK] 构建完成，产物目录: %DIST%"
call :say "     双击 dist\run.bat 或 dist\face_client.exe 运行"
call :say "============================================================"
echo.
echo 构建成功，按任意键关闭...
pause >nul
goto :eof

:fail
if exist "%TMPLOG%" del "%TMPLOG%" >nul 2>&1
call :say "============================================================"
call :say "[失败] 构建未完成，完整日志见: %LOG%"
call :say "============================================================"
echo.
echo 构建失败，按任意键关闭... 完整日志见 build.log
pause >nul
exit /b 1

:say
echo %~1
>> "%LOG%" echo %~1
goto :eof
