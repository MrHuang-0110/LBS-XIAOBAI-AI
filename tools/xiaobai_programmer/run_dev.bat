@echo off
rem 开发启动：准备 .venv、装依赖、启动小白编程上位机（任何失败都会停住显示原因）
chcp 65001 >nul
setlocal
cd /d "%~dp0"

echo === 小白编程上位机启动器 ===
echo 工作目录: %CD%

rem --- 1. 找 Python 3.11 ---
set "PYEXE=py -3.11"
%PYEXE% -V >nul 2>&1
if errorlevel 1 set "PYEXE=python"
%PYEXE% -V >nul 2>&1
if errorlevel 1 goto :nopython
for /f "delims=" %%v in ('%PYEXE% -V 2^>^&1') do echo 使用解释器: %%v

rem --- 2. 虚拟环境（缺失或损坏则重建）---
if not exist ".venv\Scripts\python.exe" goto :mkvenv
".venv\Scripts\python.exe" -m pip --version >nul 2>&1
if errorlevel 1 goto :mkvenv
goto :deps

:mkvenv
echo [1/3] 创建虚拟环境 .venv ...
if exist ".venv" rmdir /s /q ".venv"
%PYEXE% -m venv ".venv"
if errorlevel 1 goto :err

:deps
echo [2/3] 安装/更新依赖（PySide6 + bleak，首次约几百 MB）...
".venv\Scripts\python.exe" -m pip install --disable-pip-version-check -r requirements.txt
if errorlevel 1 goto :err

rem --- 3. 先做导入自检，把 ImportError 直接暴露出来（src 布局需要 PYTHONPATH）---
set "PYTHONPATH=%~dp0src"
echo [3/3] 自检并启动 ...
".venv\Scripts\python.exe" -c "import xiaobai.ui.app; print('导入自检通过')"
if errorlevel 1 goto :err

".venv\Scripts\python.exe" -m xiaobai.ui.app
if errorlevel 1 goto :err
exit /b 0

:nopython
echo.
echo [错误] 没找到可用的 Python 3.11。
echo        请到 python.org 安装 3.11 并勾选 "Add python.exe to PATH"，
echo        安装后用命令验证: py -3.11 -V
pause
exit /b 1

:err
echo.
echo [错误] 启动失败（errorlevel=%errorlevel%）。把上面整段输出发我即可定位。
pause
exit /b 1
