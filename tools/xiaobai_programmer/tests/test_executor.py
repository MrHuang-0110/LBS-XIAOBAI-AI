"""执行器测试：DONE 重报容忍、等待任务、中止、停止、心跳、错误与超时。"""
from __future__ import annotations

import asyncio
import time

import pytest  # pyright: ignore[reportMissingImports]  -- 运行环境已安装，分析器解析不到 user-site

from xiaobai.executor import (
    HEARTBEAT_INTERVAL,
    HEARTBEAT_MAX_INTERVAL,
    DeviceSession,
    ExecutionAborted,
    ExecutorError,
    ProgramRunner,
)
from xiaobai.program_ast import Program
from xiaobai.protocol import (
    ADDR_APP,
    ADDR_DEV,
    Frame,
    OP_ENTER_PROGRAM,
    OP_HEARTBEAT,
    OP_MOTOR_RUN,
    OP_MOTOR_TIME,
    OP_SHOW_NUM,
    OP_SHOW_OFF,
    OP_STOP_PROGRAM,
    OP_WAIT_IR,
    OP_WAIT_VOICE,
    RESULT_BAD_PARAM,
    parse_frame,
)
from xiaobai.simulator import FakeDevice  # pyright: ignore[reportMissingImports]  -- 模块已实现并由 pytest 验证，分析器快照滞后


def _make_session(device: FakeDevice, wait_timeout: float = 5.0) -> DeviceSession:
    session = DeviceSession(device.transport.send, wait_timeout=wait_timeout)
    device.attach(session)
    return session


def test_simple_program_runs():
    async def scenario():
        device = FakeDevice()
        session = _make_session(device)
        runner = ProgramRunner(session)
        await session.enter_program()
        program = Program(statements=[
            {"type": "motor_time", "motor": 0, "dir": 1, "ms": 50},
            {"type": "show_off"},
        ])
        await runner.run(program)
        session.stop_heartbeat()
        return device

    device = asyncio.run(scenario())
    opcodes = [op for _, op, _ in device.commands]
    assert OP_MOTOR_TIME in opcodes and OP_SHOW_OFF in opcodes
    seqs = [seq for seq, op, _ in device.commands if op == OP_MOTOR_TIME]
    assert seqs and seqs[0] == 1


def test_duplicate_done_tolerated():
    async def scenario():
        device = FakeDevice()
        device.duplicate_done = True
        session = _make_session(device)
        runner = ProgramRunner(session)
        await runner.run(Program(statements=[
            {"type": "motor_time", "motor": 1, "dir": 2, "ms": 30},
        ]))
        await asyncio.sleep(0.3)      # 等待 200ms 后的重复 DONE
        return device

    device = asyncio.run(scenario())
    assert any(op == OP_MOTOR_TIME for _, op, _ in device.commands)


def test_wait_voice_and_ir_complete():
    async def scenario():
        device = FakeDevice()
        session = _make_session(device)
        runner = ProgramRunner(session)

        voice_task = asyncio.ensure_future(runner.run(Program(statements=[
            {"type": "wait_voice", "item": 1},
        ])))
        await asyncio.sleep(0.05)
        device.voice_heard(42)         # ASR_01
        await asyncio.wait_for(voice_task, 2.0)

        device.ir[1] = 90
        await runner.run(Program(statements=[
            {"type": "wait_ir", "channel": 1, "cmp": 0, "threshold": 50},
        ]))
        return device

    device = asyncio.run(scenario())
    opcodes = [op for _, op, _ in device.commands]
    assert OP_WAIT_VOICE in opcodes and OP_WAIT_IR in opcodes


def test_stop_program_cancels():
    async def scenario():
        device = FakeDevice()
        session = _make_session(device)
        runner = ProgramRunner(session)
        task = asyncio.ensure_future(runner.run(Program(statements=[
            {"type": "wait_voice", "item": 3},
        ])))
        await asyncio.sleep(0.05)
        await runner.stop()
        with pytest.raises((asyncio.CancelledError, ExecutionAborted)):
            await task
        return device, session

    device, session = asyncio.run(scenario())
    assert any(op == OP_STOP_PROGRAM for _, op, _ in device.commands)
    assert session.aborted


def test_abort_event_stops_program():
    async def scenario():
        device = FakeDevice()
        session = _make_session(device)
        runner = ProgramRunner(session)
        task = asyncio.ensure_future(runner.run(Program(statements=[
            {"type": "wait_voice", "item": 2},
        ])))
        await asyncio.sleep(0.05)
        device.abort(reason=3, target=2)
        with pytest.raises(ExecutionAborted):
            await asyncio.wait_for(task, 2.0)
        return session

    session = asyncio.run(scenario())
    assert session.aborted and "设备中止" in session.abort_reason


def test_heartbeat_keeps_session_alive():
    async def scenario():
        device = FakeDevice()
        session = _make_session(device)
        runner = ProgramRunner(session)
        await session.enter_program()
        await runner.run(Program(statements=[{"type": "wait", "seconds": 0.8}]))
        session.stop_heartbeat()
        return device

    device = asyncio.run(scenario())
    assert device.heartbeats >= 2


def test_error_result_raises():
    async def scenario():
        device = FakeDevice()
        device.error_on_next_action = RESULT_BAD_PARAM
        session = _make_session(device)
        runner = ProgramRunner(session)
        with pytest.raises(ExecutorError):
            await runner.run(Program(statements=[
                {"type": "motor_time", "motor": 0, "dir": 1, "ms": 30},
            ]))
        return device

    asyncio.run(scenario())


def test_timeout_raises():
    async def scenario():
        device = FakeDevice()
        device.no_reply = True
        session = _make_session(device, wait_timeout=0.2)
        runner = ProgramRunner(session)
        with pytest.raises(ExecutorError):
            await runner.run(Program(statements=[
                {"type": "motor_time", "motor": 0, "dir": 1, "ms": 30},
            ]))
        return device

    asyncio.run(scenario())


class _Recorder:
    """记录每次发送的时间/优先级/帧内容（模拟无 BLE 的发送通道）。"""

    def __init__(self) -> None:
        self.frames: list[tuple[float, bool, Frame]] = []

    async def send(self, data: bytes, priority: bool = False) -> None:
        frame = parse_frame(data, ADDR_APP, ADDR_DEV)
        assert frame is not None
        self.frames.append((time.monotonic(), priority, frame))

    def times(self, opcode: int) -> list[float]:
        return [t for t, _, f in self.frames if f.opcode == opcode]

    def flags(self) -> list[tuple[int, bool]]:
        return [(f.opcode, priority) for _, priority, f in self.frames]


def test_foreground_frames_are_priority_and_heartbeat_is_not():
    async def scenario() -> _Recorder:
        rec = _Recorder()
        session = DeviceSession(rec.send)
        await session.enter_program()
        await session.request_no_reply(OP_MOTOR_RUN, bytes([0, 1]))
        await session.heartbeat_once()
        session.stop_heartbeat()
        return rec

    rec = asyncio.run(scenario())
    flags = rec.flags()
    assert (OP_ENTER_PROGRAM, True) in flags      # 建会话必须先于按钮动作
    assert (OP_MOTOR_RUN, True) in flags
    assert (OP_HEARTBEAT, False) in flags         # 心跳不得抢前台队列


def test_heartbeat_idle_cadence():
    async def scenario() -> _Recorder:
        rec = _Recorder()
        session = DeviceSession(rec.send)
        await session.enter_program()
        await asyncio.sleep(1.05)
        session.stop_heartbeat()
        return rec

    rec = asyncio.run(scenario())
    beats = rec.times(OP_HEARTBEAT)
    assert len(beats) >= 2                        # 空闲时按 300ms 周期维持
    for previous, current in zip(beats, beats[1:]):
        assert HEARTBEAT_INTERVAL - 0.05 <= current - previous <= HEARTBEAT_MAX_INTERVAL + 0.05


def test_heartbeat_skips_after_foreground_but_caps_interval():
    async def scenario() -> _Recorder:
        rec = _Recorder()
        session = DeviceSession(rec.send)
        await session.enter_program()
        deadline = time.monotonic() + 1.25
        while time.monotonic() < deadline:        # 每 100ms 一条前台动作
            await session.request_no_reply(OP_MOTOR_RUN, bytes([0, 1]))
            await asyncio.sleep(0.1)
        session.stop_heartbeat()
        return rec

    rec = asyncio.run(scenario())
    beats = rec.times(OP_HEARTBEAT)
    foreground = rec.times(OP_MOTOR_RUN)
    assert beats, "前台流量不能代替显式心跳（需兼容旧固件的 1000ms 超时）"
    assert len(beats) <= 3, "前台流量期间不应按 300ms 周期补冗余心跳"
    previous_hb: float | None = None
    for beat in beats:
        gap_hb = None if previous_hb is None else beat - previous_hb
        previous_hb = beat
        forced = gap_hb is None or gap_hb >= HEARTBEAT_MAX_INTERVAL - 0.1
        prior_fg = [t for t in foreground if t < beat]
        if forced or not prior_fg:
            continue
        assert beat - prior_fg[-1] >= HEARTBEAT_INTERVAL - 0.05


def test_heartbeat_priority_only_at_hard_deadline():
    async def idle() -> _Recorder:
        rec = _Recorder()
        session = DeviceSession(rec.send)
        await session.enter_program()
        await asyncio.sleep(0.5)
        session.stop_heartbeat()
        return rec

    rec = asyncio.run(idle())
    idle_flags = [p for _, p, f in rec.frames if f.opcode == OP_HEARTBEAT]
    assert idle_flags and idle_flags[0] is False      # 空闲心跳让位给前台

    async def busy() -> _Recorder:
        rec = _Recorder()
        session = DeviceSession(rec.send)
        await session.enter_program()
        deadline = time.monotonic() + 0.75
        while time.monotonic() < deadline:            # 前台每 100ms 一条
            await session.request_no_reply(OP_MOTOR_RUN, bytes([0, 1]))
            await asyncio.sleep(0.1)
        session.stop_heartbeat()
        return rec

    rec = asyncio.run(busy())
    busy_flags = [p for _, p, f in rec.frames if f.opcode == OP_HEARTBEAT]
    assert busy_flags and all(busy_flags)             # 600ms 硬上限心跳按前台优先级入队


def test_heartbeat_stall_does_not_burst():
    """事件循环被写操作卡住恢复后，不得补发多条过期心跳。"""
    async def scenario() -> _Recorder:
        rec = _Recorder()
        stall_once = True

        async def send(data: bytes, priority: bool = False) -> None:
            nonlocal stall_once
            frame = parse_frame(data, ADDR_APP, ADDR_DEV)
            assert frame is not None
            rec.frames.append((time.monotonic(), priority, frame))
            if stall_once and frame.opcode == OP_HEARTBEAT:
                stall_once = False
                await asyncio.sleep(0.8)

        session = DeviceSession(send)
        await session.enter_program()
        await asyncio.sleep(1.6)
        session.stop_heartbeat()
        return rec

    rec = asyncio.run(scenario())
    beats = rec.times(OP_HEARTBEAT)
    for previous, current in zip(beats, beats[1:]):
        assert current - previous >= HEARTBEAT_INTERVAL - 0.05


def test_stop_heartbeat_is_idempotent_and_cleans_task():
    async def scenario() -> tuple[_Recorder, int]:
        rec = _Recorder()
        session = DeviceSession(rec.send)
        await session.enter_program()
        session.stop_heartbeat()
        session.stop_heartbeat()                  # 重复停止不报错
        count = len(rec.times(OP_HEARTBEAT))
        await asyncio.sleep(0.7)
        return rec, count

    rec, count = asyncio.run(scenario())
    assert len(rec.times(OP_HEARTBEAT)) == count   # 任务已取消，不再发送


def test_enter_program_clears_previous_abort():
    async def scenario() -> DeviceSession:
        device = FakeDevice()
        session = _make_session(device)
        device.abort(reason=1, target=0)              # 设备侧中止
        assert session.aborted
        await session.enter_program()                 # 重建会话
        session.stop_heartbeat()
        return session

    session = asyncio.run(scenario())
    assert not session.aborted
    assert not session.abort_reason


def test_show_num_evaluates_expression_and_clamps():
    async def scenario() -> FakeDevice:
        device = FakeDevice()
        device.ir[1] = 42
        device.battery_mv = 3900
        session = _make_session(device)
        runner = ProgramRunner(session)
        await runner.run(Program(statements=[
            {"type": "show_num", "value": {"type": "ir", "channel": 1}},   # 红外值
            {"type": "show_num", "value": {"type": "battery"}},           # 3900mV → 限幅 100
            {"type": "show_num", "number": 7},                            # 旧工程字面量
        ]))
        return device

    device = asyncio.run(scenario())
    values = [args[0] for _, op, args in device.commands if op == OP_SHOW_NUM]
    assert values == [42, 100, 7]


def test_status_query_pending_clears_after_timeout():
    async def scenario() -> None:
        sent: list[bytes] = []

        def send(data: bytes, priority: bool = False) -> None:
            sent.append(data)

        session = DeviceSession(send)
        assert not session.status_query_pending
        with pytest.raises(ExecutorError):
            await session.query_status(timeout=0.05)     # 设备不回响应
        assert not session.status_query_pending          # 超时后 pending 必须恢复
        with pytest.raises(ExecutorError):
            await session.query_status(timeout=0.05)
        assert len(sent) == 2                            # 未被 pending 卡死，可继续查

    asyncio.run(scenario())


def test_repeat_and_if_ir():
    async def scenario():
        device = FakeDevice()
        device.ir[0] = 80
        session = _make_session(device)
        runner = ProgramRunner(session)
        await runner.run(Program(statements=[
            {"type": "repeat", "times": 2, "body": [
                {"type": "motor_time", "motor": 0, "dir": 1, "ms": 20},
            ]},
            {"type": "if",
             "cond": {"type": "cmp", "op": ">",
                      "a": {"type": "ir", "channel": 0}, "b": {"type": "num", "value": 50}},
             "then": [{"type": "show_off"}],
             "else": [{"type": "stop_program"}]},
        ]))
        return device

    device = asyncio.run(scenario())
    timed = [1 for _, op, _ in device.commands if op == OP_MOTOR_TIME]
    assert len(timed) == 2
    assert any(op == OP_SHOW_OFF for _, op, _ in device.commands)
    assert not any(op == OP_STOP_PROGRAM for _, op, _ in device.commands)
