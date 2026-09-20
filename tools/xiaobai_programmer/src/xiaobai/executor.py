"""受控程序执行器：把 Program AST 翻译成 C2 指令，处理心跳/DONE/事件。

语义（docs/BLE协议v2-编程模式.md）：
- 每条动作分配 1..255 循环编号；
- 需要回报的动作（定时电机/组合、等待红外、等待词条）等待设备 D2 DONE；
  设备首包立即、之后每 200ms 重发，因此丢失首包也能继续；
- 心跳 300ms 持续发送（等待 DONE 期间不中断），1000ms 设备侧超时会中止程序；
- 收到 D3 PROGRAM_ABORT → 立即中止执行并提示；
- 本地循环/条件/等待由本执行器完成，MCU 只执行当前动作。
"""
from __future__ import annotations

import asyncio
import struct
import time
from dataclasses import dataclass
from typing import Awaitable, Callable, Optional

from xiaobai.program_ast import Program, ProgramSchemaError
from xiaobai.protocol import (
    DIR_BACKWARD,
    DIR_FORWARD,
    EVT_MODE_CHANGE,
    EVT_PROGRAM_ABORT,
    Frame,
    IR_CMP_GT,
    IR_CMP_LT,
    MODE_NAMES,
    OP_ENTER_PROGRAM,
    OP_ENTER_REMOTE,
    OP_HEARTBEAT,
    OP_MOTOR_POWER,
    OP_MOTOR_RUN,
    OP_MOTOR_STOP,
    OP_MOTOR_TIME,
    OP_MOVE_POWER,
    OP_MOVE_RUN,
    OP_MOVE_STOP,
    OP_MOVE_TIME,
    OP_PLAY_VOICE,
    OP_QUERY_STATUS,
    OP_READ_IR,
    OP_SHOW_EYE,
    OP_SHOW_NUM,
    OP_SHOW_OFF,
    OP_STOP_PROGRAM,
    OP_WAIT_IR,
    OP_WAIT_VOICE,
    RESULT_NAMES,
    SequenceAllocator,
    DeviceStatus,
    EventLog,
    TYPE_C1,
    TYPE_C2,
    TYPE_D2,
    TYPE_D3,
    build_c1,
    build_c2,
    TASK_NONE,
)

HEARTBEAT_INTERVAL = 0.3
DEFAULT_WAIT_TIMEOUT = 60.0
TIMED_EXTRA_TIMEOUT = 3.0


class ExecutorError(RuntimeError):
    """执行失败（协议错误/参数非法/中止）。"""


class ExecutionAborted(ExecutorError):
    """设备侧中止程序（心跳超时/断连/按键/切模式）。"""


@dataclass
class ExecutorCallbacks:
    on_log: Optional[Callable[[str], None]] = None
    on_event: Optional[Callable[[EventLog], None]] = None
    on_status: Optional[Callable[[DeviceStatus], None]] = None
    on_progress: Optional[Callable[[str], None]] = None

    def log(self, message: str) -> None:
        if self.on_log:
            self.on_log(message)

    def event(self, ev: EventLog) -> None:
        if self.on_event:
            self.on_event(ev)

    def status(self, st: DeviceStatus) -> None:
        if self.on_status:
            self.on_status(st)

    def progress(self, message: str) -> None:
        if self.on_progress:
            self.on_progress(message)


class DeviceSession:
    """一条 BLE 会话：心跳 + 请求/响应路由 + 事件分发。"""

    def __init__(self, send: Callable[[bytes], Awaitable[None] | None],
                 callbacks: ExecutorCallbacks | None = None,
                 wait_timeout: float = DEFAULT_WAIT_TIMEOUT):
        self._send = send
        self.callbacks = callbacks or ExecutorCallbacks()
        self.wait_timeout = wait_timeout
        self.seq = SequenceAllocator()
        self._pending: dict[tuple[int, int], asyncio.Future] = {}
        self._hb_task: Optional[asyncio.Task] = None
        self._abort_event = asyncio.Event()
        self._abort_reason = ""
        self.session_id = 0
        self.status = DeviceStatus()

    # ---------------- 发送 ----------------

    async def _emit(self, data: bytes) -> None:
        result = self._send(data)
        if asyncio.iscoroutine(result):
            await result

    async def send_c2(self, seq: int, opcode: int, args: bytes = b"") -> None:
        await self._emit(build_c2(seq, opcode, args))

    async def send_c1(self, keys) -> None:
        await self._emit(build_c1(keys))

    # ---------------- 会话控制 ----------------

    async def enter_program(self) -> None:
        self.session_id += 1
        await self.send_c2(0, OP_ENTER_PROGRAM)
        self.callbacks.log("已请求进入编程模式")
        self.start_heartbeat()

    async def enter_remote(self) -> None:
        self.stop_heartbeat()
        await self.send_c2(0, OP_ENTER_REMOTE)
        self.callbacks.log("已请求进入遥控模式")

    async def heartbeat_once(self) -> None:
        await self.send_c2(0, OP_HEARTBEAT)

    def start_heartbeat(self) -> None:
        if self._hb_task and not self._hb_task.done():
            return
        self._hb_task = asyncio.get_running_loop().create_task(self._heartbeat_loop())

    def stop_heartbeat(self) -> None:
        if self._hb_task and not self._hb_task.done():
            self._hb_task.cancel()
        self._hb_task = None

    async def _heartbeat_loop(self) -> None:
        try:
            while True:
                await self.heartbeat_once()
                await asyncio.sleep(HEARTBEAT_INTERVAL)
        except asyncio.CancelledError:
            pass
        except Exception as exc:  # noqa: BLE001 - 心跳失败上报但不崩
            self.callbacks.log(f"心跳发送失败：{exc}")

    async def query_status(self) -> DeviceStatus:
        future = self._expect(0, OP_QUERY_STATUS)
        await self.send_c2(0, OP_QUERY_STATUS)
        frame = await self._wait(future, timeout=3.0)
        status = DeviceStatus.from_frame(frame)
        self.status = status
        self.callbacks.status(status)
        return status

    async def read_ir(self, channel: int) -> int:
        future = self._expect(0, OP_READ_IR)
        await self.send_c2(0, OP_READ_IR)
        frame = await self._wait(future, timeout=3.0)
        if channel == 0:
            return frame.data[3]
        if channel == 1:
            return frame.data[4]
        return frame.data[5]

    # ---------------- 请求/响应 ----------------

    def _expect(self, seq: int, opcode: int) -> asyncio.Future:
        loop = asyncio.get_running_loop()
        future = loop.create_future()
        self._pending[(seq, opcode)] = future
        return future

    async def _wait(self, future: asyncio.Future, timeout: float) -> Frame:
        try:
            return await asyncio.wait_for(future, timeout=timeout)
        except asyncio.TimeoutError as exc:
            raise ExecutorError(f"等待设备响应超时（{timeout:.1f}s）") from exc

    async def request(self, opcode: int, args: bytes = b"", timeout: float = 5.0) -> Frame:
        """发送一条动作指令并等待 DONE（支持设备 200ms 重发，丢首包不影响）。"""
        seq = self.seq.next()
        future = self._expect(seq, opcode)
        await self.send_c2(seq, opcode, args)
        self.callbacks.progress(f"发送指令 opcode=0x{opcode:02X} seq={seq}")
        return await self._wait(future, timeout=timeout)

    async def request_no_reply(self, opcode: int, args: bytes = b"") -> None:
        """不需要回报的动作（显示/功率/持续/停止/播放）。"""
        seq = self.seq.next()
        await self.send_c2(seq, opcode, args)
        self.callbacks.progress(f"发送指令 opcode=0x{opcode:02X} seq={seq}（无需回报）")

    async def stop_program(self) -> None:
        await self.send_c2(0, OP_STOP_PROGRAM)
        self._abort("上位机停止程序")

    # ---------------- 帧路由 ----------------

    def handle_frame(self, frame: Frame) -> None:
        if frame.type == TYPE_D2:
            self._handle_d2(frame)
        elif frame.type == TYPE_D3:
            ev = EventLog.from_frame(frame)
            self.callbacks.event(ev)
            if ev.event == EVT_PROGRAM_ABORT:
                reason = ev.data[0] if len(ev.data) > 0 else 0
                target = ev.data[1] if len(ev.data) > 1 else 0
                self._abort(f"设备中止程序（原因={reason} 目标模式={MODE_NAMES.get(target, target)}）")
            elif ev.event == EVT_MODE_CHANGE and len(ev.data) >= 2:
                self.callbacks.log(f"模式变化：{MODE_NAMES.get(ev.data[0], ev.data[0])}")
        elif frame.type == TYPE_C2:
            self.callbacks.log("收到非预期的 C2 帧（设备→App 方向），已忽略")
        elif frame.type == TYPE_C1:
            self.callbacks.log("收到 C1 帧（设备→App 方向），已忽略")

    def _handle_d2(self, frame: Frame) -> None:
        key = (frame.seq, frame.opcode)
        future = self._pending.pop(key, None)
        if future is None or future.done():
            # 200ms 重发/迟到响应：忽略即可
            self.callbacks.log(
                f"D2 响应（已完成或无等待）：seq={frame.seq} op=0x{frame.opcode:02X}"
            )
            return
        if frame.result != 0:
            message = RESULT_NAMES.get(frame.result, f"result={frame.result}")
            if frame.opcode == OP_QUERY_STATUS:
                future.set_result(frame)
            else:
                future.set_exception(ExecutorError(f"设备返回错误：{message}"))
            return
        future.set_result(frame)

    def _abort(self, reason: str) -> None:
        self._abort_reason = reason
        self._abort_event.set()
        for future in self._pending.values():
            if not future.done():
                future.set_exception(ExecutionAborted(reason))
        self._pending.clear()
        self.stop_heartbeat()

    def reset_abort(self) -> None:
        self._abort_event.clear()
        self._abort_reason = ""

    @property
    def aborted(self) -> bool:
        return self._abort_event.is_set()

    @property
    def abort_reason(self) -> str:
        return self._abort_reason


class ProgramRunner:
    """AST 解释执行（PC 本地循环/条件/等待）。"""

    def __init__(self, session: DeviceSession):
        self.session = session
        self._task: Optional[asyncio.Task] = None
        self._running = False
        self.callbacks = session.callbacks

    @property
    def running(self) -> bool:
        return self._running

    async def run(self, program: Program) -> None:
        if self._running:
            raise ExecutorError("已有程序在运行")
        self._running = True
        self.session.reset_abort()
        self.callbacks.progress("程序开始")
        try:
            await self._exec_statements(program.statements)
            self.callbacks.progress("程序结束")
        finally:
            self._running = False

    def start(self, program: Program) -> asyncio.Task:
        self._task = asyncio.get_running_loop().create_task(self.run(program))
        return self._task

    async def stop(self) -> None:
        await self.session.stop_program()
        if self._task and not self._task.done():
            self._task.cancel()
        self._running = False

    # ---------------- 语句执行 ----------------

    async def _exec_statements(self, statements: list[dict]) -> None:
        for stmt in statements:
            self._check_abort()
            await self._exec_statement(stmt)

    async def _exec_statement(self, stmt: dict) -> None:
        stype = stmt["type"]

        if stype == "motor_time":
            await self._timed(OP_MOTOR_TIME, bytes([stmt["motor"], stmt["dir"]]), stmt["ms"])
        elif stype == "motor_run":
            await self.session.request_no_reply(OP_MOTOR_RUN, bytes([stmt["motor"], stmt["dir"]]))
        elif stype == "motor_stop":
            await self.session.request_no_reply(OP_MOTOR_STOP, bytes([stmt["motor"]]))
        elif stype == "motor_power":
            await self.session.request_no_reply(OP_MOTOR_POWER, bytes([stmt["level"]]))
        elif stype == "move_time":
            await self._timed(OP_MOVE_TIME, bytes([stmt["move"]]), stmt["ms"])
        elif stype == "move_run":
            await self.session.request_no_reply(OP_MOVE_RUN, bytes([stmt["move"]]))
        elif stype == "move_stop":
            await self.session.request_no_reply(OP_MOVE_STOP)
        elif stype == "move_power":
            await self.session.request_no_reply(OP_MOVE_POWER, bytes([stmt["level"]]))
        elif stype == "wait_ir":
            await self.session.request(
                OP_WAIT_IR,
                bytes([stmt["channel"], stmt["cmp"], stmt["threshold"]]),
                timeout=self.session.wait_timeout,
            )
        elif stype == "show_eye":
            await self.session.request_no_reply(OP_SHOW_EYE, bytes([stmt["eye"]]))
        elif stype == "show_num":
            await self.session.request_no_reply(OP_SHOW_NUM, bytes([stmt["number"]]))
        elif stype == "show_off":
            await self.session.request_no_reply(OP_SHOW_OFF)
        elif stype == "play_voice":
            await self.session.request_no_reply(OP_PLAY_VOICE, bytes([stmt["item"]]))
        elif stype == "wait_voice":
            await self.session.request(
                OP_WAIT_VOICE, bytes([stmt["item"]]), timeout=self.session.wait_timeout
            )
        elif stype == "wait":
            await asyncio.sleep(stmt["seconds"])
        elif stype == "stop_program":
            await self.session.stop_program()
        elif stype == "repeat":
            for _ in range(stmt["times"]):
                self._check_abort()
                await self._exec_statements(stmt["body"])
        elif stype == "while":
            iterations = 0
            while True:
                self._check_abort()
                value = bool(await self._eval(stmt["cond"]))
                if stmt["until"]:
                    value = not value
                if not value:
                    break
                await self._exec_statements(stmt["body"])
                iterations += 1
                if iterations >= 10000:
                    raise ExecutorError("循环次数超过 10000，已强制停止")
        elif stype == "if":
            if bool(await self._eval(stmt["cond"])):
                await self._exec_statements(stmt["then"])
            else:
                await self._exec_statements(stmt["else"])
        else:
            raise ExecutorError(f"AST 含未知语句：{stype!r}")

    async def _timed(self, opcode: int, prefix: bytes, ms: int) -> None:
        duration = self._to_int(ms, "动作时长")
        if duration < 0:
            raise ExecutorError(f"动作时长不能为负：{duration}")
        args = prefix + struct.pack("<I", duration)
        await self.session.request(
            opcode, args, timeout=duration / 1000.0 + TIMED_EXTRA_TIMEOUT
        )

    def _check_abort(self) -> None:
        if self.session.aborted:
            raise ExecutionAborted(self.session.abort_reason or "程序已中止")

    # ---------------- 表达式求值 ----------------

    @staticmethod
    def _to_int(value, what: str) -> int:
        try:
            return int(value)
        except (TypeError, ValueError) as exc:
            raise ExecutorError(f"{what} 不是整数：{value!r}") from exc

    @staticmethod
    def _to_float(value, what: str) -> float:
        try:
            return float(value)
        except (TypeError, ValueError) as exc:
            raise ExecutorError(f"{what} 不是数值：{value!r}") from exc

    async def _eval(self, expr: dict):
        etype = expr["type"]
        if etype == "num":
            return expr["value"]
        if etype == "bool":
            return expr["value"]
        if etype == "ir":
            return await self.session.read_ir(self._to_int(expr["channel"], "红外通道"))
        if etype == "battery":
            status = await self.session.query_status()
            return status.battery_mv
        if etype == "mode":
            status = await self.session.query_status()
            return status.mode
        if etype == "not":
            return not bool(await self._eval(expr["operand"]))
        if etype in ("and", "or"):
            a = bool(await self._eval(expr["a"]))
            if etype == "and":
                return a and bool(await self._eval(expr["b"]))
            return a or bool(await self._eval(expr["b"]))
        if etype == "cmp":
            a = await self._eval(expr["a"])
            b = await self._eval(expr["b"])
            return _compare(expr["op"], a, b)
        if etype == "math":
            a = self._to_float(await self._eval(expr["a"]), "算术左值")
            b = self._to_float(await self._eval(expr["b"]), "算术右值")
            if expr["op"] == "+":
                return a + b
            if expr["op"] == "-":
                return a - b
            if expr["op"] == "*":
                return a * b
            if b == 0:
                raise ExecutorError("除零")
            return a / b
        raise ExecutorError(f"AST 含未知表达式：{etype!r}")


def _compare(op: str, a, b) -> bool:
    if op == "==":
        return a == b
    if op == "!=":
        return a != b
    if op == "<":
        return a < b
    if op == "<=":
        return a <= b
    if op == ">":
        return a > b
    if op == ">=":
        return a >= b
    raise ExecutorError(f"未知比较符：{op}")


def build_ast_quick_check(program: Program) -> None:
    """执行前的静态检查（重复校验，防御手工改过的 .xbprog）。"""
    try:
        Program.from_dict(program.to_dict())
    except ProgramSchemaError as exc:
        raise ExecutorError(f"程序 AST 非法：{exc}") from exc
