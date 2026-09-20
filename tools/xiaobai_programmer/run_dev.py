"""开发启动：等价 run_dev.bat，便于跨平台调试（Windows 上用 .bat 即可）。"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if __name__ == "__main__":
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-r", str(ROOT / "requirements.txt")])
    subprocess.check_call([sys.executable, "-m", "xiaobai.ui.app"])
