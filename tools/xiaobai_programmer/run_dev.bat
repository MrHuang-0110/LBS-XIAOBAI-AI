@echo off
rem 开发启动：准备 .venv、装依赖、启动小白编程上位机（失败会停住显示错误）
setlocal
cd /d %~dp0

rem --- 找 Python 3.11 ---
set "PYEXE=py -3.11"
%PYEXE% -V >nul 2>&1 || set "PYEXE=python"
%PYEXE% -V >nul 2>&1 || goto :nopython

rem --- 虚拟环境（缺失/损坏则重建）---
if not exist ".venv\Scripts\python.exe" goto :mkvenv
".venv\Scripts\python.exe" -m pip --version >nul 2>&1 || goto :mkvenv
goto :deps

:mkvenv
echo [1/3] 创建虚拟环境 .venv ...
if exist ".venv" rmdir /s /q ".venv"
%PYEXE% -m venv ".venv" || goto :err

:deps
echo [2/3] 安装/更新依赖（PySide6、bleak）...
".venv\Scripts\python.exe" -m pip install --disable-pip-version-check -q -r requirements.txt || goto :err

rem --- 启动（src 布局：把 src 加进 PYTHONPATH）---
echo [3/3] 启动小白编程 ...
set "PYTHONPATH=%~dp0src"
".venv\Scripts\python.exe" -m xiaobai.ui.app
if errorlevel 1 goto :err
exit /b 0

:nopython
echo.
echo [错误] 未找到 Python 3.11。请从 python.org 安装 3.11，并勾选 "Add python.exe to PATH"，
echo        或用命令 py -3.11 -V 验证；国内网络可加 -i https://pypi.tuna.tsinghua.edu.cn/simple
pause
exit /b 1

:err
echo.
echo [错误] 启动失败（errorlevel=%errorlevel%）。把上面的报错整段发我即可定位。
pause
exit /b 1
