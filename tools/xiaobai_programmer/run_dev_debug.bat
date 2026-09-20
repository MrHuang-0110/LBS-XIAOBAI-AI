@echo off
rem 诊断模式：全程显示命令输出并始终 pause（双击运行也不会闪退）
chcp 65001 >nul
setlocal
cd /d "%~dp0"
echo === 诊断信息 ===
echo 目录: %CD%
echo.
echo --- 已安装的 Python ---
py -0p 2>nul
py -3.11 -V 2>&1
python -V 2>&1
echo.
echo --- .venv 状态 ---
if exist ".venv\Scripts\python.exe" (echo .venv 存在) else (echo .venv 不存在)
echo.
echo --- 手动执行安装与启动 ---
set "PYEXE=py -3.11"
%PYEXE% -V >nul 2>&1 || set "PYEXE=python"
%PYEXE% -m venv .venv
".venv\Scripts\python.exe" -m pip install -r requirements.txt
set "PYTHONPATH=%CD%\src"
".venv\Scripts\python.exe" -c "import xiaobai.ui.app; print('导入自检通过')"
echo --- 启动界面（关闭窗口后回到这里）---
".venv\Scripts\python.exe" -m xiaobai.ui.app
echo.
echo --- 退出码: %errorlevel% ---
pause
