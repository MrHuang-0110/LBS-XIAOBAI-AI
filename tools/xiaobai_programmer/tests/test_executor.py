"""执行器测试：DONE 重报容忍、等待任务、中止、停止、心跳、错误与超时。"""
from __future__ import annotations

import asyncio

import pytest  # pyright: ignore[reportMissingImports]  -- 运行环境已安装，分析器解析不到 user-site

from xiaobai.executor import DeviceSession, ExecutionAborted, ExecutorError, ProgramRunner
from xiaobai.program_ast import Program
from xiaobai.protocol import (
    OP_MOTOR_TIME,
    OP_SHOW_OFF,
    OP_STOP_PROGRAM,
    OP_WAIT_IR,
    OP_WAIT_VOICE,
    RESULT_BAD_PARAM,
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
