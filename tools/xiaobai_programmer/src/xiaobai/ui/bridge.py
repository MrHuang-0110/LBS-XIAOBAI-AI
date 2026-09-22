"""Qt ↔ Blockly(JS) 桥：工作区 JSON 进出，全部走受控 AST，不做 eval。"""
from __future__ import annotations

from PySide6.QtCore import QObject, Signal, Slot  # pyright: ignore[reportMissingImports]  -- Windows 运行依赖，本机未安装


class BlocklyBridge(QObject):
    """QWebChannel 暴露给页面；页面只传 Blockly 序列化 JSON。"""

    runRequested = Signal(str)      # workspace JSON
    stopRequested = Signal()
    logMessage = Signal(str)
    statusMessage = Signal(str)

    @Slot(str)
    def run_workspace(self, workspace_json: str) -> None:
        self.runRequested.emit(workspace_json)

    @Slot()
    def stop_program(self) -> None:
        self.stopRequested.emit()

    @Slot(str)
    def log(self, message: str) -> None:
        self.logMessage.emit(message)

    @Slot(result=str)
    def protocol_version(self) -> str:
        return "BLE v2"
