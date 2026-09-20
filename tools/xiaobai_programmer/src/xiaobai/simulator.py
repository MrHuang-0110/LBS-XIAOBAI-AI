"""模拟 MCU（无蓝牙硬件）：解析 App 请求帧并按协议回 D2/D3。

既是测试替身，也可作为上位机的“离线演示模式”（UI 里勾选后无需真机）。
""" 
from __future__ import annotations

import asyncio
import struct
from typing import Callable, Optional

from xiaobai.protocol import (
    ADDR_APP,
    ADDR_DEV,
    EVT_MODE_CHANGE,
    EVT_PROGRAM_ABORT,
    FRAME_LEN,
    IR_CMP_GT,
    MODE_PROGRAM,
    OP_ENTER_PROGRAM,
    OP_ENTER_REMOTE,
    OP_HEARTBEAT,
    OP_MOTOR_RUN,
    OP_MOTOR_STOP,
    OP_MOTOR_TIME,
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
    RESULT_OK,
    TASK_MOTOR_TIME,
    TASK_NONE,
    TASK_WAIT_IR,
    TASK_WAIT_VOICE,
    TYPE_C1,
    TYPE_C2,
    build_d2,
    build_d3,
    parse_frame,
)
from xiaobai.transport import FakeTransport

__all__ = ["FakeDevice", "RecordingTransport"]


class RecordingTransport(FakeTransport):
    """记录发送字节，并回调给模拟设备。"""

    def __init__(self) -> None:
        super().__init__()
        self.on_sent: Optional[Callable[[bytes], None]] = None

    def send(self, data: bytes) -> None:
        super().send(data)
        if self.on_sent is not None:
            self.on_sent(bytes(data))


class FakeDevice:
    """行为等价固件侧的状态机（仅测试所需子集）。"""

    def __init__(self, transport: Optional[RecordingTransport] = None) -> None:
        self.transport = transport or RecordingTransport()
        self.transport.on_sent = self._on_sent
        self._buffer = bytearray()
        self._tasks: list[asyncio.Task] = []
        self._timers: list[asyncio.TimerHandle] = []
        self._voice_waiters: list[tuple[int, int]] = []   # (seq, item)

        self.mode = MODE_PROGRAM
        self.ir = {0: 10, 1: 20, 2: 30}
        self.battery_mv = 3900
        self.task = TASK_NONE
        self.heartbeats = 0
        self.commands: list[tuple[int, int, bytes]] = []
        self.abort_on_next_action = False
        self.error_on_next_action: Optional[int] = None   # 注入 D2 错误 result
        self.no_reply = False              # 模拟设备不回报（超时用例）
        self.duplicate_done = True         # 模拟首包丢失：DONE 发两次

    # ---------------- 发送侧 ----------------

    def _on_sent(self, data: bytes) -> None:
        self._buffer.extend(data)
        while len(self._buffer) >= FRAME_LEN:
            for i in range(len(self._buffer) - FRAME_LEN + 1):
                frame = parse_frame(bytes(self._buffer[i : i + FRAME_LEN]), ADDR_APP, ADDR_DEV)
                if frame is None:
                    continue
                del self._buffer[: i + FRAME_LEN]
                self._handle(frame)
                break
            else:
                del self._buffer[: max(0, len(self._buffer) - (FRAME_LEN - 1))]
                break

    def _handle(self, frame) -> None:
        if frame.type == TYPE_C1:
            self.commands.append((0, 0xC1, frame.data))
            return
        if frame.type != TYPE_C2:
            return

        seq, opcode = frame.seq, frame.opcode
        args = frame.args()
        self.commands.append((seq, opcode, args))

        if opcode == OP_HEARTBEAT:
            self.heartbeats += 1
            return
        if opcode == OP_ENTER_PROGRAM:
            self.mode = MODE_PROGRAM
            self._d3(EVT_MODE_CHANGE, bytes([MODE_PROGRAM, 0]))
            return
        if opcode == OP_ENTER_REMOTE:
            self.mode = 3
            self._d3(EVT_MODE_CHANGE, bytes([3, MODE_PROGRAM]))
            return
        if opcode == OP_QUERY_STATUS:
            self._status(seq)
            return
        if opcode == OP_READ_IR:
            self._d2(seq, opcode, bytes([self.ir[0], self.ir[1], self.ir[2], 0, 0, 0, 0]))
            return
        if opcode == OP_STOP_PROGRAM:
            self._cancel_all()
            return

        if self.no_reply:
            return

        if self.error_on_next_action is not None:
            result = self.error_on_next_action
            self.error_on_next_action = None
            self._d2_error(seq, opcode, result)
            return

        if opcode in (OP_MOTOR_TIME, OP_MOVE_TIME):
            ms = struct.unpack("<I", args[2:6] if opcode == OP_MOTOR_TIME else args[1:5])[0]
            self.task = TASK_MOTOR_TIME
            self._schedule_done(seq, opcode, ms / 1000.0)
            return
        if opcode == OP_WAIT_IR:
            self.task = TASK_WAIT_IR
            self._check_ir(seq, opcode, args)
            return
        if opcode == OP_WAIT_VOICE:
            self.task = TASK_WAIT_VOICE
            self._voice_waiters.append((seq, args[0]))
            return
        if self.abort_on_next_action:
            self.abort_on_next_action = False
            self._d3(EVT_PROGRAM_ABORT, bytes([2, 3, 0, 0]))
            return
        # 其余动作无需回报
        self._d2(seq, opcode, b"")

    # ---------------- 设备侧辅助 ----------------

    def attach(self, session) -> None:
        """双向绑定：把设备产生的帧回传给会话。"""
        self.transport.on_frame = session.handle_frame

    def _status(self, seq: int) -> None:
        data = bytes([
            self.mode,
            self.ir[0],
            self.ir[1],
            self.ir[2],
            self.battery_mv & 0xFF,
            (self.battery_mv >> 8) & 0xFF,
            (self.task << 4) & 0xF0,
        ])
        self._d2(seq, OP_QUERY_STATUS, data)

    def _d2(self, seq: int, opcode: int, data: bytes) -> None:
        self.transport.inject(build_d2(seq, opcode, RESULT_OK, data))

    def _d2_error(self, seq: int, opcode: int, result: int) -> None:
        self.transport.inject(build_d2(seq, opcode, result))

    def _d3(self, event: int, data: bytes) -> None:
        self.transport.inject(build_d3(event, 1, data))

    def _schedule_done(self, seq: int, opcode: int, delay: float) -> None:
        loop = asyncio.get_running_loop()
        self._timers.append(loop.call_later(delay, self._send_done, seq, opcode))

    def _send_done(self, seq: int, opcode: int) -> None:
        self.task = TASK_NONE
        self._d2(seq, opcode, b"")
        if self.duplicate_done:
            loop = asyncio.get_running_loop()
            self._timers.append(loop.call_later(0.2, lambda: self._d2(seq, opcode, b"")))

    def _check_ir(self, seq: int, opcode: int, args: bytes) -> None:
        channel, cmp_, threshold = args[0], args[1], args[2]
        value = self.ir[channel]
        hit = (value > threshold) if cmp_ == IR_CMP_GT else (value < threshold)
        if hit:
            self._schedule_done(seq, opcode, 0.02)
        else:
            loop = asyncio.get_running_loop()
            self._timers.append(
                loop.call_later(0.02, self._check_ir, seq, opcode, args)
            )

    def voice_heard(self, cmd: int) -> None:
        """模拟 ASRPRO 上报 cmd=42..51（item = cmd-41）。"""
        item = cmd - 41
        for seq, want in list(self._voice_waiters):
            if want == item:
                self._voice_waiters.remove((seq, want))
                self._send_done(seq, OP_WAIT_VOICE)

    def abort(self, reason: int = 1, target: int = 0) -> None:
        self._d3(EVT_PROGRAM_ABORT, bytes([reason, target, 0, 0]))

    def _cancel_all(self) -> None:
        for task in self._tasks:
            task.cancel()
        for timer in self._timers:
            timer.cancel()
        self._tasks.clear()
        self._timers.clear()
        self._voice_waiters.clear()
        self.task = TASK_NONE

    def shutdown(self) -> None:
        self._cancel_all()
