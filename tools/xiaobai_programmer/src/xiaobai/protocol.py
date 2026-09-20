"""小白 BLE 协议 v2 编解码（与固件 Protocol/Proto_Ble.c 一一对应）。

权威契约：docs/BLE协议v2-编程模式.md
黄金帧来源：docs/ble_v2_golden_frames.json（测试会交叉校验本文件内嵌副本）
"""
from __future__ import annotations

import itertools
from dataclasses import dataclass, field
from typing import Iterable, Iterator, Mapping, Sequence

HEAD = 0x5A
TAIL = 0xA5
FRAME_LEN = 17
DATA_LEN = 10
ADDR_APP = 0x97
ADDR_DEV = 0x98

TYPE_C1 = 0xC1
TYPE_C2 = 0xC2
TYPE_D2 = 0xD2
TYPE_D3 = 0xD3

# C2 操作码
OP_ENTER_PROGRAM = 0x01
OP_ENTER_REMOTE = 0x02
OP_HEARTBEAT = 0x03
OP_QUERY_STATUS = 0x04
OP_STOP_PROGRAM = 0x05
OP_MOTOR_TIME = 0x10
OP_MOTOR_RUN = 0x11
OP_MOTOR_STOP = 0x12
OP_MOTOR_POWER = 0x13
OP_MOVE_TIME = 0x20
OP_MOVE_RUN = 0x21
OP_MOVE_STOP = 0x22
OP_MOVE_POWER = 0x23
OP_WAIT_IR = 0x30
OP_READ_IR = 0x31
OP_SHOW_EYE = 0x40
OP_SHOW_NUM = 0x41
OP_SHOW_OFF = 0x42
OP_PLAY_VOICE = 0x50
OP_WAIT_VOICE = 0x51

# 参数取值
MOTOR_LEFT = 0
MOTOR_RIGHT = 1
DIR_FORWARD = 1
DIR_BACKWARD = 2
MOVE_FORWARD = 1
MOVE_LEFT = 2
MOVE_RIGHT = 3
MOVE_BACKWARD = 4
IR_LEFT = 0
IR_CENTER = 1
IR_RIGHT = 2
IR_CMP_GT = 0
IR_CMP_LT = 1

# D2 result
RESULT_OK = 0x00
RESULT_BAD_PARAM = 0x01
RESULT_BAD_OPCODE = 0x02
RESULT_BAD_MODE = 0x03

# D3 event
EVT_MODE_CHANGE = 0x01
EVT_PROGRAM_ABORT = 0x02
EVT_IR_CHANGE = 0x03
EVT_WAKE = 0x04
EVT_SLEEP = 0x05
EVT_LOW_BATTERY = 0x06
EVT_PROTOCOL_ERROR = 0x07

ABORT_HEARTBEAT = 1
ABORT_BLE_LOST = 2
ABORT_KEY = 3
ABORT_APP_MODE = 4

TASK_NONE = 0
TASK_MOTOR_TIME = 1
TASK_WAIT_IR = 2
TASK_WAIT_VOICE = 3

FAULT_HEARTBEAT = 0x01
FAULT_BAD_OPCODE = 0x02
FAULT_BAD_PARAM = 0x04

MODE_VOICE = 0
MODE_POWER = 1
MODE_SENSOR = 2
MODE_REMOTE = 3
MODE_PROGRAM = 4   # 与固件 App_Mode_t 一致

MODE_NAMES = {
    MODE_VOICE: "语音模式",
    MODE_POWER: "动力模式",
    MODE_SENSOR: "感应模式",
    MODE_REMOTE: "遥控模式",
    MODE_PROGRAM: "编程模式",
}

RESULT_NAMES = {
    RESULT_OK: "OK",
    RESULT_BAD_PARAM: "参数非法",
    RESULT_BAD_OPCODE: "未知操作码",
    RESULT_BAD_MODE: "模式不允许",
}

EVENT_NAMES = {
    EVT_MODE_CHANGE: "模式变化",
    EVT_PROGRAM_ABORT: "程序中止",
    EVT_IR_CHANGE: "红外变化",
    EVT_WAKE: "唤醒",
    EVT_SLEEP: "休眠",
    EVT_LOW_BATTERY: "低电量",
    EVT_PROTOCOL_ERROR: "协议错误",
}

# 黄金帧（与 docs/ble_v2_golden_frames.json 相同；tests 会交叉校验）
GOLDEN_FRAMES = {
    "C1_UP": "5A 97 98 0A C1 01 00 00 00 00 00 00 00 00 00 55 A5",
    "C2_MOTOR_TIME_LEFT_FWD_1000MS_SEQ1": "5A 97 98 0A C2 01 10 00 01 E8 03 00 00 00 00 52 A5",
    "C2_ENTER_PROGRAM_SEQ0": "5A 97 98 0A C2 00 01 00 00 00 00 00 00 00 00 56 A5",
    "C2_WAIT_IR_CENTER_GT_30_SEQ7": "5A 97 98 0A C2 07 30 01 00 1E 00 00 00 00 00 AB A5",
    "C2_SHOW_NUM_100_SEQ9": "5A 97 98 0A C2 09 41 64 00 00 00 00 00 00 00 03 A5",
    "C2_PLAY_VOICE_P03_SEQ10": "5A 97 98 0A C2 0A 50 03 00 00 00 00 00 00 00 B2 A5",
    "D2_DONE_SEQ1_OP_0X10": "5A 98 97 0A D2 01 10 00 00 00 00 00 00 00 00 76 A5",
    "D2_QUERY_STATUS": "5A 98 97 0A D2 00 04 00 00 0A 14 1E 3C 0F 00 F0 A5",
    "D3_MODE_CHANGE_PROGRAM": "5A 98 97 0A D3 01 01 04 00 00 00 00 00 00 00 6C A5",
    "D3_PROTOCOL_ERROR_CHECKSUM": "5A 98 97 0A D3 07 01 02 00 00 00 00 00 00 00 70 A5",
}


def checksum(buf15: bytes) -> int:
    """帧校验：字节 0..14 累加和低 8 位。"""
    if len(buf15) != FRAME_LEN - 2:
        raise ValueError(f"checksum 需要 {FRAME_LEN - 2} 字节，收到 {len(buf15)}")
    return sum(buf15) & 0xFF


def build_frame(src: int, dst: int, ftype: int, data: bytes) -> bytes:
    """组装 17 字节帧（自动补长/校验/帧尾）。"""
    if len(data) > DATA_LEN:
        raise ValueError(f"DATA 最多 {DATA_LEN} 字节，收到 {len(data)}")
    body = bytes([HEAD, src, dst, DATA_LEN, ftype]) + bytes(data).ljust(DATA_LEN, b"\x00")
    return body + bytes([checksum(body), TAIL])


def build_c1(keys: Sequence[int] | Mapping[int, bool]) -> bytes:
    """C1 遥控帧：10 键位图（1=按下）。"""
    if isinstance(keys, Mapping):
        pressed = [1 if keys.get(i, False) else 0 for i in range(DATA_LEN)]
    else:
        try:
            pressed = [1 if int(k) else 0 for k in keys]
        except (TypeError, ValueError) as exc:
            raise ValueError(f"C1 按键位必须可转 int：{exc}") from exc
        if len(pressed) != DATA_LEN:
            raise ValueError(f"C1 需要 {DATA_LEN} 个按键位")
    return build_frame(ADDR_APP, ADDR_DEV, TYPE_C1, bytes(pressed))


def build_c2(seq: int, opcode: int, args: bytes = b"") -> bytes:
    """C2 编程/会话请求：[seq, opcode, args0..7]。"""
    if not 0 <= seq <= 0xFF:
        raise ValueError("seq 必须在 0..255")
    return build_frame(ADDR_APP, ADDR_DEV, TYPE_C2, bytes([seq, opcode]) + bytes(args))


def build_d2(seq: int, opcode: int, result: int, data: bytes = b"") -> bytes:
    """D2 响应（仅测试/仿真用；设备侧由固件生成）。"""
    return build_frame(
        ADDR_DEV, ADDR_APP, TYPE_D2, bytes([seq, opcode, result]) + bytes(data)
    )


def build_d3(event: int, counter: int, data: bytes = b"") -> bytes:
    """D3 异步事件（仅测试/仿真用）。"""
    return build_frame(ADDR_DEV, ADDR_APP, TYPE_D3, bytes([event, counter]) + bytes(data))


@dataclass(frozen=True)
class Frame:
    """一帧结构合法的 17 字节报文。"""

    type: int
    data: bytes

    @property
    def seq(self) -> int:
        return self.data[0] if len(self.data) > 0 else 0

    @property
    def opcode(self) -> int:
        return self.data[1] if len(self.data) > 1 else 0

    @property
    def result(self) -> int:
        return self.data[2] if len(self.data) > 2 else 0

    @property
    def event(self) -> int:
        return self.data[0] if len(self.data) > 0 else 0

    @property
    def counter(self) -> int:
        return self.data[1] if len(self.data) > 1 else 0

    def args(self) -> bytes:
        return self.data[2:]


def parse_frame(raw: bytes, src: int = ADDR_DEV, dst: int = ADDR_APP) -> Frame | None:
    """解析单帧；结构/校验不通过返回 None。

    默认按“设备→App”方向校验；测试可传 src=ADDR_APP, dst=ADDR_DEV 解 App 请求帧。
    """
    if len(raw) != FRAME_LEN:
        return None
    if raw[0] != HEAD or raw[1] != src or raw[2] != dst:
        return None
    if raw[3] != DATA_LEN or raw[16] != TAIL:
        return None
    if raw[4] not in (TYPE_C1, TYPE_C2, TYPE_D2, TYPE_D3):
        return None
    if checksum(raw[:15]) != raw[15]:
        return None
    return Frame(type=raw[4], data=raw[5:15])


class StreamParser:
    """字节流滑窗分帧（等价固件 Proto_Ble_GetFrame）。

    默认解析“设备→App”方向；测试可用 src=ADDR_APP, dst=ADDR_DEV 解析请求帧。
    """

    def __init__(self, src: int = ADDR_DEV, dst: int = ADDR_APP,
                 max_frames: int = 4, on_error=None):
        self._buf = bytearray()
        self._max = FRAME_LEN * max_frames
        self._src = src
        self._dst = dst
        self.on_error = on_error

    def feed(self, chunk: bytes) -> list[Frame]:
        """喂入字节，返回本次解析出的全部有效帧。"""
        self._buf.extend(chunk)
        if len(self._buf) > self._max:
            drop = len(self._buf) - self._max
            del self._buf[:drop]
            if self.on_error:
                self.on_error("overflow", drop)
        return list(self._drain())

    def reset(self) -> None:
        self._buf.clear()

    def _drain(self) -> Iterator[Frame]:
        while len(self._buf) >= FRAME_LEN:
            for i in range(0, len(self._buf) - FRAME_LEN + 1):
                raw = bytes(self._buf[i : i + FRAME_LEN])
                frame = parse_frame(raw, self._src, self._dst)
                if frame is None:
                    continue
                del self._buf[: i + FRAME_LEN]
                yield frame
                break
            else:
                # 无完整帧：保留最后 FRAME_LEN-1 字节用于跨包拼接
                if len(self._buf) > FRAME_LEN - 1:
                    del self._buf[: len(self._buf) - (FRAME_LEN - 1)]
                return


class SequenceAllocator:
    """动作编号 1..255 循环（会话/心跳/查询使用 seq=0）。"""

    def __init__(self) -> None:
        self._counter = itertools.cycle(range(1, 256))

    def next(self) -> int:
        return next(self._counter)


@dataclass
class DeviceStatus:
    """QUERY_STATUS 解析结果。"""

    mode: int = 0
    ir_left: int = 0
    ir_center: int = 0
    ir_right: int = 0
    battery_mv: int = 0
    task: int = 0
    fault: int = 0

    @property
    def mode_name(self) -> str:
        return MODE_NAMES.get(self.mode, f"未知({self.mode})")

    @classmethod
    def from_frame(cls, frame: Frame) -> "DeviceStatus":
        d = frame.data
        return cls(
            mode=d[3],
            ir_left=d[4],
            ir_center=d[5],
            ir_right=d[6],
            battery_mv=d[7] | (d[8] << 8),
            task=(d[9] >> 4) & 0x0F,
            fault=d[9] & 0x0F,
        )


@dataclass
class EventLog:
    """D3 事件的人类可读描述。"""

    event: int
    counter: int
    data: bytes

    def describe(self) -> str:
        name = EVENT_NAMES.get(self.event, f"事件{self.event}")
        d = self.data   # 已剔除 event/counter，d[0] = data0
        if self.event == EVT_MODE_CHANGE and len(d) >= 2:
            new = MODE_NAMES.get(d[0], d[0])
            old = MODE_NAMES.get(d[1], d[1])
            return f"{name}: {old} → {new}"
        if self.event == EVT_PROGRAM_ABORT and len(d) >= 2:
            return f"{name}: 原因={d[0]} 目标模式={MODE_NAMES.get(d[1], d[1])}"
        if self.event == EVT_IR_CHANGE and len(d) >= 4:
            return f"{name}: 左{d[0]} 中{d[1]} 右{d[2]} 触发={d[3]}"
        if self.event == EVT_LOW_BATTERY and len(d) >= 3:
            mv = d[1] | (d[2] << 8)
            return f"{name}: {'进入' if d[0] else '恢复'} {mv}mV"
        if self.event == EVT_PROTOCOL_ERROR and len(d) >= 1:
            return f"{name}: code={d[0]}"
        return name

    @classmethod
    def from_frame(cls, frame: Frame) -> "EventLog":
        return cls(event=frame.event, counter=frame.counter, data=frame.data[2:])
