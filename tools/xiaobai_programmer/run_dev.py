"""开发启动（跨平台/命令行）：准备依赖并启动界面。

Windows 推荐直接双击 run_dev.bat；本脚本等价于它，便于在 WSL/macOS 调试核心逻辑。
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def main() -> int:
    env = dict(os.environ)
    env["PYTHONPATH"] = str(ROOT / "src") + os.pathsep + env.get("PYTHONPATH", "")
    subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", "-r", str(ROOT / "requirements.txt")])
    return subprocess.call([sys.executable, "-m", "xiaobai.ui.app"], env=env)


if __name__ == "__main__":
    raise SystemExit(main())
