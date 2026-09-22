"""BLE 探针延时诊断的离线测试：不依赖蓝牙硬件。

用假 GATT 客户端验证「响应匹配 / 丢帧计数 / 保护窗排队 / 心跳与轮询计数」，
真机采样仍需在 Windows 上用 tools\\ble_probe.py --latency 运行。
"""
from __future__ import annotations

import asyncio
import sys
import time
from pathlib import Path
from typing import Callable

import pytest  # pyright: ignore[reportMissingImports]  -- 运行环境已安装，分析器解析不到 user-site

pytest.importorskip("bleak")

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import ble_probe  # noqa: E402  # pyright: ignore[reportMissingImports]  -- 探针是独立脚本，需先补 sys.path
from xiaobai.protocol import (  # noqa: E402
    ADDR_APP,
    ADDR_DEV,
    build_d2,
    parse_frame,
)


class _EchoClient:
    """假 GATT 客户端：记录写入，可按需回 D2。"""

    def __init__(self) -> None:
        self.writes: list[tuple[float, bytes]] = []
        self.on_write: Callable[[bytes], None] | None = None

    async def write_gatt_char(self, _uuid: str, data: bytes, response: bool) -> None:
        assert response is True
        self.writes.append((time.monotonic(), bytes(data)))
        if self.on_write is not None:
            self.on_write(bytes(data))


def _responder(channel: ble_probe.LatencyChannel):
    def respond(payload: bytes) -> None:
        frame = parse_frame(payload, ADDR_APP, ADDR_DEV)
        assert frame is not None
        parsed = parse_frame(build_d2(frame.seq, frame.opcode, 0), ADDR_DEV, ADDR_APP)
        assert parsed is not None
        asyncio.get_running_loop().call_soon(
            channel.on_frame, ble_probe.TYPE_D2, parsed.data)
    return respond


def test_latency_summarize_percentiles() -> None:
    summary = ble_probe._summarize([1.0, 2.0, 3.0, 4.0, 5.0])
    assert summary["min"] == 1.0
    assert summary["p50"] == 3.0
    assert summary["max"] == 5.0
    assert summary["n"] == 5
    empty = ble_probe._summarize([])
    assert empty["n"] == 0 and empty["p95"] == 0.0


def test_latency_channel_matches_response() -> None:
    async def scenario() -> ble_probe.LatencySample:
        client = _EchoClient()
        channel = ble_probe.LatencyChannel(client, "write")
        client.on_write = _responder(channel)
        return await channel.request(ble_probe.OP_QUERY_STATUS, timeout=1.0)

    sample = asyncio.run(scenario())
    assert sample.ok
    assert sample.rtt_ms >= sample.write_ms >= 0.0


def test_latency_channel_counts_timeouts() -> None:
    async def scenario() -> ble_probe.LatencySample:
        channel = ble_probe.LatencyChannel(_EchoClient(), "write")
        return await channel.request(ble_probe.OP_QUERY_STATUS, timeout=0.05)

    sample = asyncio.run(scenario())
    assert not sample.ok
    assert sample.rtt_ms >= 50.0


def test_latency_channel_waits_after_notification() -> None:
    async def scenario() -> ble_probe.LatencySample:
        channel = ble_probe.LatencyChannel(
            _EchoClient(), "write", guard=0.0, notify_guard=0.05)
        channel.on_frame(ble_probe.TYPE_D2, b"\x00" * 10)   # 模拟刚收到通知
        return await channel.request(ble_probe.OP_QUERY_STATUS, timeout=0.01)

    sample = asyncio.run(scenario())
    assert sample.guard_wait_ms >= 40.0      # 保护窗计入 guard_wait，而非软件排队
    assert sample.queue_wait_ms < 20.0


def test_heartbeat_due_adaptive_policy() -> None:
    channel = ble_probe.LatencyChannel(_EchoClient(), "write")
    now = time.monotonic()
    channel.last_foreground = now - 0.05
    channel.last_hb = now - 0.35
    assert channel.heartbeat_due(True, 0.3, 0.6) is False   # 前台刚发过：跳过冗余心跳
    channel.last_hb = now - 0.65
    assert channel.heartbeat_due(True, 0.3, 0.6) is True    # 600ms 硬上限不能突破
    channel.last_foreground = now - 0.35
    channel.last_hb = now - 0.35
    assert channel.heartbeat_due(True, 0.3, 0.6) is True    # 空闲恢复 300ms 周期
    assert channel.heartbeat_due(False, 0.3, 0.6) is True   # legacy 固定周期


def test_latency_heartbeat_and_poll_loops_count() -> None:
    async def scenario() -> ble_probe.LatencyStats:
        client = _EchoClient()
        channel = ble_probe.LatencyChannel(client, "write")
        client.on_write = _responder(channel)
        stats = ble_probe.LatencyStats("loop")
        stop = asyncio.Event()
        tasks = [
            asyncio.create_task(channel.heartbeat_loop(stats, 0.05, stop)),
            asyncio.create_task(channel.poll_loop(stats, 0.05, stop, 0.5)),
        ]
        await asyncio.sleep(0.35)
        stop.set()
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        return stats

    stats = asyncio.run(scenario())
    assert stats.heartbeats >= 2
    assert stats.polls >= 2
    assert stats.polls_lost == 0
