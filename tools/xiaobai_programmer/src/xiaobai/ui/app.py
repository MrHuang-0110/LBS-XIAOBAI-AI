"""小白编程上位机主窗口（PySide6 + QtWebEngine + 离线 Blockly）。

页面：设备连接 / 直接控制 / Blockly 编程 / 状态 / 协议日志。
- 打开编程页自动 ENTER_PROGRAM；打开遥控页 ENTER_REMOTE（手机遥控按钮）；
- 执行器在等待 DONE 时持续 300ms 心跳；
- 程序停止按钮始终可用（STOP_PROGRAM + 刹停）。
"""
from __future__ import annotations

import asyncio
import json
import sys
import threading
from pathlib import Path

from PySide6.QtCore import QTimer, QUrl, Qt, Signal  # pyright: ignore[reportMissingImports]
from PySide6.QtWidgets import (  # pyright: ignore[reportMissingImports]
    QApplication,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)
from PySide6.QtWebChannel import QWebChannel  # pyright: ignore[reportMissingImports]
from PySide6.QtWebEngineWidgets import QWebEngineView  # pyright: ignore[reportMissingImports]

from xiaobai import __version__
from xiaobai.executor import DeviceSession, ExecutorError, ProgramRunner
from xiaobai.program_ast import ProgramSchemaError, parse_workspace
from xiaobai.project_io import ProjectFileError, load_project, save_project
from xiaobai.protocol import OP_ENTER_PROGRAM, OP_ENTER_REMOTE
from xiaobai.transport import BleTransport, TransportConfig, FakeTransport
from xiaobai.simulator import FakeDevice  # pyright: ignore[reportMissingImports]  -- 模块已实现并被测试覆盖，分析器快照滞后
from xiaobai.ui.bridge import BlocklyBridge  # pyright: ignore[reportMissingImports]  -- PySide6 仅 Windows 运行依赖
from xiaobai.ui.policy import (
    PROGRAM_TAB_TITLES,
    SESSION_ENTER,
    SESSION_HEARTBEAT,
    SESSION_SKIP,
    ProgramSessionPolicy,
    StatusPollPolicy,
)

RESOURCES = Path(__file__).resolve().parents[1] / "resources"
CONFIG_PATH = Path.home() / ".xiaobai_programmer" / "config.json"


class MainWindow(QWidget):
    logSignal = Signal(str)
    statusSignal = Signal(object)        # DeviceStatus
    bleStateSignal = Signal(bool, str)   # 连接状态（BLE 线程 → GUI 主线程）
    errorSignal = Signal(str)            # 错误提示（可能来自 BLE 线程）

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle(f"小白编程 v{__version__}")
        self.resize(1180, 760)

        self.config = TransportConfig(CONFIG_PATH)
        self.transport = BleTransport(self.config, self._on_frame_from_ble)
        self.transport.set_state_callback(self._on_ble_state)
        self.session = DeviceSession(self._send_ble, callbacks=self._callbacks())
        self.runner = ProgramRunner(self.session)
        self.fake: FakeDevice | None = None
        self.loop = self.transport._loop
        self.status_policy = StatusPollPolicy()
        self.session_policy = ProgramSessionPolicy()

        self._build_ui()
        self.status_policy.on_tab_changed(self.tabs.tabText(self.tabs.currentIndex()))
        self.logSignal.connect(self.log_view.appendPlainText)
        self.statusSignal.connect(self._render_status)
        self.errorSignal.connect(self._error)
        self.bleStateSignal.connect(self._apply_ble_state)
        # 状态查询只在“状态”页可见时周期发送（进页立刻查一次），
        # 避免直接控制/程序执行期间的后台轮询抢占前台指令。
        self.status_timer = QTimer(self)
        self.status_timer.timeout.connect(self._poll_status)

    # ---------------- UI ----------------

    def _build_ui(self) -> None:
        tabs = QTabWidget()
        tabs.addTab(self._build_connect_tab(), "设备连接")
        tabs.addTab(self._build_control_tab(), "直接控制")
        tabs.addTab(self._build_blockly_tab(), "Blockly 编程")
        tabs.addTab(self._build_status_tab(), "状态")
        tabs.addTab(self._build_log_tab(), "协议日志")
        tabs.currentChanged.connect(self._on_tab_changed)

        layout = QVBoxLayout(self)
        layout.addWidget(tabs)
        self.tabs = tabs

    def _build_connect_tab(self) -> QWidget:
        page = QWidget()
        box = QVBoxLayout(page)
        self.scan_list = QListWidget()
        row = QHBoxLayout()
        scan_btn = QPushButton("扫描 BLE")
        scan_btn.clicked.connect(self._scan)
        conn_btn = QPushButton("连接")
        conn_btn.clicked.connect(self._connect)
        demo_btn = QPushButton("离线演示（模拟设备）")
        demo_btn.clicked.connect(self._connect_demo)
        row.addWidget(scan_btn)
        row.addWidget(conn_btn)
        row.addWidget(demo_btn)
        self.connect_label = QLabel("未连接")
        box.addWidget(self.connect_label)
        box.addWidget(self.scan_list)
        box.addLayout(row)
        self.remote_btn = QPushButton("进入手机遥控模式（ENTER_REMOTE）")
        self.remote_btn.clicked.connect(lambda: self._send_session(OP_ENTER_REMOTE))
        box.addWidget(self.remote_btn)
        return page

    def _build_control_tab(self) -> QWidget:
        page = QWidget()
        box = QVBoxLayout(page)
        rows = [
            [("左正转", "motor", (0, 1)), ("左反转", "motor", (0, 2)), ("左停", "motor", (0, 0))],
            [("右正转", "motor", (1, 1)), ("右反转", "motor", (1, 2)), ("右停", "motor", (1, 0))],
            [("前进", "move", 1), ("左转", "move", 2), ("右转", "move", 3), ("后退", "move", 4)],
        ]
        for row_def in rows:
            row = QHBoxLayout()
            for text, kind, value in row_def:
                btn = QPushButton(text)
                btn.clicked.connect(lambda _=False, k=kind, v=value: self._direct(k, v))
                row.addWidget(btn)
            box.addLayout(row)
        special = QHBoxLayout()
        for text, opcode, args in [
            ("组合停止", 0x22, b""),
            ("1档", 0x13, bytes([1])),
            ("2档", 0x13, bytes([2])),
            ("3档", 0x13, bytes([3])),
            ("关闭显示", 0x42, b""),
        ]:
            btn = QPushButton(text)
            btn.clicked.connect(lambda _=False, o=opcode, a=args: self._fire(o, a))
            special.addWidget(btn)
        box.addLayout(special)
        self.stop_btn = QPushButton("停止程序（始终可用）")
        self.stop_btn.clicked.connect(self._stop_program)
        box.addWidget(self.stop_btn)
        return page

    def _build_blockly_tab(self) -> QWidget:
        page = QWidget()
        box = QVBoxLayout(page)
        self.web = QWebEngineView()
        # 必须保存引用：局部变量会被 Python GC，C++ 侧通道随之失效 → JS 报“桥未连接”
        self.channel = QWebChannel(self)
        self.bridge = BlocklyBridge(self)
        self.bridge.runRequested.connect(self._run_workspace)
        self.bridge.stopRequested.connect(self._stop_program)
        self.channel.registerObject("bridge", self.bridge)
        self.web.page().setWebChannel(self.channel)
        self.web.load(QUrl.fromLocalFile(str(RESOURCES / "index.html")))
        box.addWidget(self.web)

        row = QHBoxLayout()
        run_btn = QPushButton("运行")
        run_btn.clicked.connect(lambda: self.web.page().runJavaScript("window.xiaobaiRun && window.xiaobaiRun()"))
        save_btn = QPushButton("保存工程")
        save_btn.clicked.connect(self._save_project)
        load_btn = QPushButton("加载工程")
        load_btn.clicked.connect(self._load_project)
        row.addWidget(run_btn)
        row.addWidget(save_btn)
        row.addWidget(load_btn)
        row.addWidget(self._mk_stop_button())
        box.addLayout(row)
        return page

    def _mk_stop_button(self) -> QPushButton:
        btn = QPushButton("停止")
        btn.clicked.connect(self._stop_program)
        return btn

    def _build_status_tab(self) -> QWidget:
        page = QWidget()
        box = QVBoxLayout(page)
        self.status_label = QLabel("等待连接…")
        box.addWidget(self.status_label)
        return page

    def _build_log_tab(self) -> QWidget:
        page = QWidget()
        box = QVBoxLayout(page)
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        box.addWidget(self.log_view)
        return page

    # ---------------- 连接 ----------------

    def _scan(self) -> None:
        self.log("扫描 BLE…")
        future = self.transport.scan(5.0)

        def done(fut) -> None:
            try:
                devices = fut.result()
            except Exception as exc:  # noqa: BLE001 - 扫描失败提示用户
                self.log(f"扫描失败：{exc}")
                return
            self.scan_list.clear()
            for item in devices:
                label = f"{item['name'] or '未知'}  {item['address']}"
                self.scan_list.addItem(label)
            self.scan_list.setProperty("addresses", [d["address"] for d in devices])

        future.add_done_callback(done)

    def _connect(self) -> None:
        row = self.scan_list.currentRow()
        addresses = self.scan_list.property("addresses") or []
        if row < 0 or row >= len(addresses):
            self.log("请先扫描并选择一个设备")
            return
        self._connect_address(addresses[row])

    def _connect_address(self, address: str) -> None:
        self.log(f"连接 {address} …")

        def done(fut) -> None:
            if fut.exception() is not None:
                self.log(f"连接失败：{fut.exception()}")
                return
            self.log("BLE 已连接，正在探活（查询设备状态）…")
            self._submit(self._probe_device())

        self.transport.connect(address).add_done_callback(done)

    async def _probe_device(self) -> None:
        """连上后发一次 QUERY_STATUS：有响应说明写入通路正常。"""
        try:
            status = await self.session.query_status()
            self.log(
                f"设备在线：{status.mode_name} 红外 {status.ir_left}/{status.ir_center}/"
                f"{status.ir_right} 电池 {status.battery_mv}mV"
            )
        except Exception as exc:  # noqa: BLE001 - 探活失败要给用户可读提示
            self.log(f"默认写特征 {self.transport.write_uuid} 无响应：{exc}")
        # 逐个尝试其它可写特征（不同固件/蓝牙栈写入特征不同），成功即持久化
        for uuid in list(self.transport.write_candidates):
            if uuid == self.transport.write_uuid:
                continue
            self.transport.write_uuid = uuid
            self.transport.config.set("write_uuid", uuid)
            try:
                status = await self.session.query_status()
            except Exception:  # noqa: BLE001 - 继续试下一个候选
                continue
            self.log(
                f"改用写特征 {uuid} 成功：{status.mode_name} 电池 {status.battery_mv}mV"
            )
            return
        self.log("所有可写特征都无响应：请确认设备已烧录含 BLE v2 的固件，或在上位机日志里核对 UUID")

    def _on_ble_state(self, connected: bool, info: str) -> None:
        # 注意：本回调运行在 BLE 事件循环线程，只能发信号，不能直接碰控件
        self.bleStateSignal.emit(connected, info)

    def _apply_ble_state(self, connected: bool, info: str) -> None:
        self.connect_label.setText(("已连接：" + info) if connected else "未连接")
        self.log(("已连接 " if connected else "已断开 ") + info)
        if not connected:
            self.session_policy.reset()
            # 在 BLE 事件循环线程里取消心跳任务，避免跨线程 cancel
            if self.loop.is_running():
                self.loop.call_soon_threadsafe(self.session.stop_heartbeat)
            return
        title = self.tabs.tabText(self.tabs.currentIndex())
        if title in PROGRAM_TAB_TITLES:
            self._ensure_program_session()
        elif title == "状态":
            self._poll_status()

    def _send_ble(self, data: bytes, priority: bool = False):
        """把线程池 Future 适配成执行器可 await 的对象（UI 线程不阻塞）。"""
        return asyncio.wrap_future(self.transport.send(data, priority))

    def _connect_demo(self) -> None:
        """离线演示：本地模拟 MCU，便于无硬件验证 Blockly 流程。"""
        device = FakeDevice()
        self.fake = device
        self.session = DeviceSession(device.transport.send, callbacks=self._callbacks())
        device.attach(self.session)
        self.runner = ProgramRunner(self.session)
        self.log("已启动离线模拟设备（无需蓝牙）")

    def _on_frame_from_ble(self, frame) -> None:
        self.session.handle_frame(frame)

    def _callbacks(self):
        from xiaobai.executor import ExecutorCallbacks

        return ExecutorCallbacks(
            on_log=self.log,
            on_event=lambda ev: self.log(f"事件：{ev.describe()}"),
            on_status=lambda st: self.statusSignal.emit(st),
            on_progress=self.log,
        )

    # ---------------- 操作 ----------------

    def _direct(self, kind: str, value) -> None:
        if kind == "motor":
            motor, direction = value
            if direction == 0:
                self._fire(0x12, bytes([motor]))
            else:
                self._fire(0x11, bytes([motor, direction]))
        else:
            self._fire(0x21, bytes([value]))

    def _fire(self, opcode: int, args: bytes) -> None:
        # 每个前台动作前先确保编程会话（停止/中止后心跳已停，需重建）
        self._ensure_program_session()

        async def coro() -> None:
            await self.session.request_no_reply(opcode, args)

        self._submit(coro())

    def _send_session(self, opcode: int) -> None:
        if opcode == OP_ENTER_PROGRAM:
            self._ensure_program_session()
        elif opcode == OP_ENTER_REMOTE:
            self.session_policy.reset()
            self._submit(self.session.enter_remote())

    async def _ensure_program_session_async(self) -> None:
        """确保编程会话（ENTER_PROGRAM + 心跳）；同一连接内只发一次。"""
        connected = self.transport.connected or self.fake is not None
        action = self.session_policy.decide(connected=connected, aborted=self.session.aborted)
        if action == SESSION_HEARTBEAT:
            self.session.start_heartbeat()   # 幂等：兜底恢复停止/中止后的心跳
            return
        if action == SESSION_SKIP:
            raise ConnectionError("BLE 未连接")
        try:
            await self.session.enter_program()
        except Exception:
            self.session_policy.on_enter_failed()
            raise

    def _ensure_program_session(self) -> None:
        if not (self.transport.connected or self.fake is not None):
            return
        self._submit(self._ensure_program_session_async())

    def _run_workspace(self, workspace_json: str) -> None:
        try:
            workspace = json.loads(workspace_json)
            program = parse_workspace(workspace)
        except (json.JSONDecodeError, ProgramSchemaError) as exc:
            self._error(f"程序不合法：{exc}")
            return

        async def coro():
            try:
                await self._ensure_program_session_async()
                await self.runner.run(program)
            except ExecutorError as exc:
                self.errorSignal.emit(str(exc))

        self._submit(coro())

    def _stop_program(self) -> None:
        self._submit(self.runner.stop())

    def _save_project(self) -> None:
        path, _ = QFileDialog.getSaveFileName(self, "保存工程", "program.xbprog", "*.xbprog")
        if not path:
            return

        def done(result) -> None:
            try:
                workspace = json.loads(result)
                program = parse_workspace(workspace)
                save_project(Path(path), Path(path).stem, workspace, program)
                self.log(f"工程已保存：{path}")
            except (json.JSONDecodeError, ProgramSchemaError, ProjectFileError) as exc:
                self._error(f"保存失败：{exc}")

        self.web.page().runJavaScript(
            "Blockly.serialization.workspaces.save(Blockly.getMainWorkspace())",
            done,
        )

    def _load_project(self) -> None:
        path, _ = QFileDialog.getOpenFileName(self, "加载工程", "", "*.xbprog")
        if not path:
            return
        try:
            project = load_project(Path(path))
        except ProjectFileError as exc:
            self._error(str(exc))
            return
        payload = json.dumps(project.workspace, ensure_ascii=False)
        self.web.page().runJavaScript(
            f"window.xiaobaiLoad && window.xiaobaiLoad({json.dumps(payload)})"
        )
        self.log(f"工程已加载：{path}")

    def _poll_status(self) -> None:
        connected = self.transport.connected or self.fake is not None
        # 只在“状态”页可见时轮询：直接控制/Blockly 页面的后台查询会与按钮指令抢写锁；
        # 执行程序（尤其 WAIT_VOICE）时不发周期查询，上一条查询未回时不叠加。
        if not self.status_policy.should_query(
            connected=connected,
            runner_running=self.runner.running,
            query_pending=self.session.status_query_pending,
        ):
            return
        self._submit(self.session.query_status())

    # ---------------- 工具 ----------------

    def _submit(self, coro) -> None:
        future = asyncio.run_coroutine_threadsafe(coro, self.loop)
        future.add_done_callback(self._on_future_done)

    def _on_future_done(self, future) -> None:
        if future.cancelled():
            return
        exc = future.exception()
        if exc is not None:
            self.log(f"操作失败：{exc}")

    def _on_tab_changed(self, index: int) -> None:
        title = self.tabs.tabText(index)
        if self.status_policy.on_tab_changed(title):
            self._poll_status()          # 进页立即查一次
        interval = self.status_policy.timer_interval_ms()
        if interval:
            self.status_timer.start(interval)
        else:
            self.status_timer.stop()
        if title not in PROGRAM_TAB_TITLES:
            return
        if self.transport.connected or self.fake is not None:
            self._ensure_program_session()
        else:
            self.log("尚未连接设备：请先在「设备连接」页连接 BLE，或点「离线演示（模拟设备）」")

    def log(self, message: str) -> None:
        self.logSignal.emit(message)

    def _render_status(self, status) -> None:
        self.status_label.setText(
            f"模式：{status.mode_name}   红外：左{status.ir_left} 中{status.ir_center} "
            f"右{status.ir_right}   电池：{status.battery_mv} mV   任务：{status.task}  "
            f"故障：0x{status.fault:02X}"
        )

    def _error(self, message: str) -> None:
        self.log(f"错误：{message}")
        QMessageBox.warning(self, "小白编程", message)

    def closeEvent(self, event) -> None:  # noqa: N802 - Qt 命名
        self.status_timer.stop()
        if self.loop.is_running():
            self.loop.call_soon_threadsafe(self.session.stop_heartbeat)
        self.transport.shutdown()
        super().closeEvent(event)


def main() -> int:
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
