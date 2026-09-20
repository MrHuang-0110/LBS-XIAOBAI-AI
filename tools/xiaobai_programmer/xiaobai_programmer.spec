# -*- mode: python ; coding: utf-8 -*-
# PyInstaller 配置：打包成免安装单文件 EXE（内嵌离线 Blockly 资源）
from PyInstaller.utils.hooks import collect_data_files

datas = collect_data_files("xiaobai", includes=["resources/**/*"])

a = Analysis(
    ["src/xiaobai/ui/app.py"],
    pathex=["src"],
    binaries=[],
    datas=datas,
    hiddenimports=["bleak", "PySide6.QtWebEngineWidgets"],
    hookspath=[],
    runtime_hooks=[],
    excludes=["tkinter"],
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz, a.scripts, a.binaries, a.datas, [],
    name="XiaoBaiProgrammer",
    debug=False,
    strip=False,
    upx=False,
    console=False,
    onefile=True,
)
