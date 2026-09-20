@echo off
rem 开发启动：创建/复用 .venv 并运行小白编程上位机
setlocal
cd /d %~dp0
if not exist .venv (
  py -3.11 -m venv .venv || goto :err
)
call .venv\Scripts\activate.bat
python -m pip install -r requirements.txt || goto :err
python -m xiaobai.ui.app
goto :eof
:err
echo 启动失败：请确认已安装 Python 3.11（py -3.11）并可访问 pip。
pause
