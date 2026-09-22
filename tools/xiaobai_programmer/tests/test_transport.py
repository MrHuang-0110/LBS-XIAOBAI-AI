"""ECB02 传输调度测试：并发写串行化，并保留 TX/TX、RX/TX 保护间隔。"""
from __future__ import annotations

import asyncio
import time
from typing import Any, cast

from xiaobai.transport import (
    BACKGROUND_STARVATION_GUARD_SECONDS,
    BleTransport,
    NOTIFY_TO_WRITE_GUARD_SECONDS,
    WRITE_GUARD_SECONDS,
)


class _FakeClient:
    def __init__(self, first_delay: float = 0.005) -> None:
        self.starts: list[float] = []
        self.payloads: list[bytes] = []
        self.active = 0
        self.max_active = 0
        self.first_delay = first_delay

    async def write_gatt_char(self, _uuid: str, data: bytes, response: bool) -> None:
        assert response is True
        self.active += 1
        self.max_active = max(self.max_active, self.active)
        self.starts.append(time.monotonic())
        self.payloads.append(bytes(data))
        await asyncio.sleep(self.first_delay if len(self.starts) == 1 else 0.005)
        self.active -= 1


def _transport(client: _FakeClient) -> BleTransport:
    transport = BleTransport.__new__(BleTransport)
    transport.client = cast(Any, client)
    transport._connected = True
    transport.write_uuid = "write"
    transport._write_queue = []
    transport._writer_active = False
    transport._last_write_done = 0.0
    transport._last_notify_at = 0.0
    return transport


def test_concurrent_writes_are_serialized_and_paced() -> None:
    async def scenario() -> _FakeClient:
        client = _FakeClient()
        transport = _transport(client)
        await asyncio.gather(
            transport._send_async(b"first"),
            transport._send_async(b"second"),
            transport._send_async(b"third"),
        )
        return client

    client = asyncio.run(scenario())
    assert client.max_active == 1
    assert len(client.starts) == 3
    for previous, current in zip(client.starts, client.starts[1:]):
        # 每次假写本身占 5ms；下一次还必须在完成后等待完整保护间隔。
        assert current - previous >= WRITE_GUARD_SECONDS


def test_write_waits_after_notification() -> None:
    async def scenario() -> float:
        client = _FakeClient()
        transport = _transport(client)
        transport._last_notify_at = time.monotonic()
        started = time.monotonic()
        await transport._send_async(b"heartbeat")
        return client.starts[0] - started

    delay = asyncio.run(scenario())
    assert delay >= NOTIFY_TO_WRITE_GUARD_SECONDS * 0.8


def test_foreground_write_jumps_ahead_of_background_queue() -> None:
    """按钮命令（前台）不得排在已等待的心跳/轮询（后台）之后。"""
    async def scenario() -> _FakeClient:
        client = _FakeClient(first_delay=0.05)
        transport = _transport(client)
        bg1 = asyncio.ensure_future(transport._send_async(b"bg1", priority=False))
        await asyncio.sleep(0.01)                       # bg1 已在写
        bg2 = asyncio.ensure_future(transport._send_async(b"bg2", priority=False))
        await asyncio.sleep(0.001)
        fg = asyncio.ensure_future(transport._send_async(b"fg", priority=True))
        await asyncio.gather(bg1, bg2, fg)
        return client

    client = asyncio.run(scenario())
    assert client.payloads == [b"bg1", b"fg", b"bg2"]
    assert client.max_active == 1


def test_waiting_background_write_is_not_starved() -> None:
    """普通后台帧可被前台插队；等待过久的后台帧进入饥饿保护，不再被插队。"""
    async def scenario() -> _FakeClient:
        client = _FakeClient(first_delay=0.5)
        transport = _transport(client)
        bg1 = asyncio.ensure_future(transport._send_async(b"bg1", priority=False))
        await asyncio.sleep(0.01)
        bg2 = asyncio.ensure_future(transport._send_async(b"bg2", priority=False))
        await asyncio.sleep(0.13)                       # bg2 等待尚未超过保护窗
        fg1 = asyncio.ensure_future(transport._send_async(b"fg1", priority=True))
        await asyncio.sleep(BACKGROUND_STARVATION_GUARD_SECONDS + 0.05)
        fg2 = asyncio.ensure_future(transport._send_async(b"fg2", priority=True))
        await asyncio.gather(bg1, bg2, fg1, fg2)
        return client

    client = asyncio.run(scenario())
    assert client.payloads == [b"bg1", b"fg1", b"bg2", b"fg2"]


def test_foreground_order_is_preserved() -> None:
    """多条前台命令之间保持发出顺序（停止/动作不可乱序）。"""
    async def scenario() -> _FakeClient:
        client = _FakeClient(first_delay=0.03)
        transport = _transport(client)
        futures = [
            asyncio.ensure_future(transport._send_async(payload, priority=True))
            for payload in (b"act1", b"act2", b"stop")
        ]
        await asyncio.gather(*futures)
        return client

    client = asyncio.run(scenario())
    assert client.payloads == [b"act1", b"act2", b"stop"]
    for previous, current in zip(client.starts, client.starts[1:]):
        assert current - previous >= WRITE_GUARD_SECONDS
