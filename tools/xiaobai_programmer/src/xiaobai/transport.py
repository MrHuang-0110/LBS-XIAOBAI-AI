"""BLE 传输层：bleak（Windows BLE GATT）+ 自动重连 + UUID 持久化。

- ECB00CV2 默认走透传 GATT；不同 Windows 蓝牙适配器/固件暴露的 UUID 可能不同，
  因此首次连接后自动发现“可写 + 可通知”特征并持久化，后续直接复用。
- 所有 bleak 调用都跑在专用 asyncio 事件循环线程里，Qt 主线程通过
  `call_soon_threadsafe`/`run_coroutine_threadsafe` 交互。
- 测试用 `FakeTransport`，不依赖蓝牙硬件。
"""
from __future__ import annotations

import asyncio
import json
import threading
from pathlib import Path
from typing import Callable, Iterable, Optional

from xiaobai.protocol import Frame, StreamParser

# 常见 ECB00/透传模块 UUID（仅作为发现优先级，不作为唯一依据）
PREFERRED_WRITE_UUIDS = (
    "0000fff2-0000-1000-8000-00805f9b34fb",   # ECB00 实测写特征（FFF0 服务的 FFF2）
    "0000ffe1-0000-1000-8000-00805f9b34fb",
    "6e400002-b5a3-f393-e0a9-e50e24dcca9e",   # Nordic UART RX
)
PREFERRED_NOTIFY_UUIDS = (
    "0000ffe1-0000-1000-8000-00805f9b34fb",
    "0000fff1-0000-1000-8000-00805f9b34fb",
    "6e400003-b5a3-f393-e0a9-e50e24dcca9e",   # Nordic UART TX
)
NAME_HINTS = ("Spark_AI", "XiaoBai", "小白", "ECB00", "FFE0")


class TransportConfig:
    """UUID/设备地址持久化（JSON 文件，默认放在用户目录）。"""

    def __init__(self, path: Path):
        self.path = path
        self.data: dict = {}
        self.load()

    def load(self) -> None:
        try:
            self.data = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            self.data = {}

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(
            json.dumps(self.data, ensure_ascii=False, indent=2), encoding="utf-8"
        )

    def get(self, key: str, default=None):
        return self.data.get(key, default)

    def set(self, key: str, value) -> None:
        self.data[key] = value
        self.save()


class FakeTransport:
    """测试替身：记录发送、可注入设备帧。"""

    def __init__(self) -> None:
        self.sent: list[bytes] = []
        self.parser = StreamParser()
        self.on_frame: Optional[Callable[[Frame], None]] = None
        self.connected = True

    def send(self, data: bytes) -> None:
        if not self.connected:
            raise ConnectionError("未连接")
        self.sent.append(bytes(data))

    def inject(self, raw: bytes) -> list[Frame]:
        frames = self.parser.feed(raw)
        for frame in frames:
            if self.on_frame:
                self.on_frame(frame)
        return frames


class BleTransport:
    """真实 BLE 传输。需要安装 bleak。"""

    def __init__(self, config: TransportConfig, on_frame: Callable[[Frame], None]):
        self.config = config
        self.on_frame = on_frame
        self.parser = StreamParser(on_error=self._on_parse_error)
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._run_loop, name="ble-loop", daemon=True)
        self._thread.start()

        self.client = None
        self.device_name = ""
        self.address = ""
        self.write_uuid = config.get("write_uuid", "")
        self.notify_uuid = config.get("notify_uuid", "")
        self.write_candidates: list[str] = []   # 发现到的全部可写特征（探活回退用）
        self._connected = False
        self._closing = False
        self._on_state: Optional[Callable[[bool, str], None]] = None

    # ---------------- 事件循环桥接 ----------------

    def _run_loop(self) -> None:
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    def call(self, coro):
        """从 Qt 线程提交协程，返回 concurrent.futures.Future。"""
        return asyncio.run_coroutine_threadsafe(coro, self._loop)

    def _on_parse_error(self, kind: str, detail) -> None:
        # 协议错误由执行器/日志层记录，这里不抛异常
        pass

    def set_state_callback(self, cb: Callable[[bool, str], None]) -> None:
        self._on_state = cb

    def _notify_state(self, connected: bool, info: str = "") -> None:
        self._connected = connected
        if self._on_state:
            self._on_state(connected, info)

    @property
    def connected(self) -> bool:
        return self._connected

    # ---------------- 扫描/连接 ----------------

    async def _scan_async(self, timeout: float = 5.0) -> list[dict]:
        from bleak import BleakScanner

        devices = await BleakScanner.discover(timeout=timeout)
        found = []
        for d in devices:
            name = d.name or ""
            found.append({"address": d.address, "name": name, "raw": d})
        found.sort(key=lambda x: (not any(h in x["name"] for h in NAME_HINTS), x["name"]))
        return found

    def scan(self, timeout: float = 5.0):
        return self.call(self._scan_async(timeout))

    async def _connect_async(self, address: str) -> None:
        from bleak import BleakClient

        if self.client is not None:
            try:
                await self.client.disconnect()
            except Exception:  # noqa: BLE001 - 断开失败不阻塞重连
                pass

        self.client = BleakClient(address, disconnected_callback=self._on_disconnected)
        await self.client.connect()
        self.address = address
        self.device_name = getattr(self.client, "name", "") or ""

        await self._discover_characteristics()

        if not self.notify_uuid:
            raise RuntimeError("未找到可通知特征（设备→App）")
        await self.client.start_notify(self.notify_uuid, self._on_notify)
        self._notify_state(True, self.device_name or address)

    async def _discover_characteristics(self) -> None:
        assert self.client is not None
        services = self.client.services
        writable, notifiable = [], []
        for service in services:
            for char in service.characteristics:
                props = set(char.properties)
                if "write" in props or "write-without-response" in props:
                    writable.append(char.uuid)
                if "notify" in props or "indicate" in props:
                    notifiable.append(char.uuid)

        def _pick(candidates: Iterable[str], preferred: tuple[str, ...], fallback: str) -> str:
            lowered = {c.lower(): c for c in candidates}
            for want in preferred:
                if want in lowered:
                    return lowered[want]
            for key, value in lowered.items():
                if key.startswith("0000ffe") or key.startswith("6e4000"):
                    return value
            return fallback

        # 候选排序：优先已知 UUID，其余按发现顺序，探活失败时可逐个回退
        self.write_candidates = sorted(
            writable,
            key=lambda u: (u.lower() not in PREFERRED_WRITE_UUIDS, writable.index(u)),
        )
        self.write_uuid = self.write_uuid or _pick(writable, PREFERRED_WRITE_UUIDS, "")
        self.notify_uuid = self.notify_uuid or _pick(notifiable, PREFERRED_NOTIFY_UUIDS, "")
        # 持久化发现结果，下次免扫描直连
        self.config.set("write_uuid", self.write_uuid)
        self.config.set("notify_uuid", self.notify_uuid)
        self.config.set("last_address", self.address)

    def connect(self, address: str):
        return self.call(self._connect_async(address))

    async def _disconnect_async(self) -> None:
        self._closing = True
        if self.client is not None:
            try:
                if self.notify_uuid:
                    await self.client.stop_notify(self.notify_uuid)
            except Exception:  # noqa: BLE001
                pass
            try:
                await self.client.disconnect()
            except Exception:  # noqa: BLE001
                pass
        self.client = None
        self._notify_state(False, "已断开")

    def disconnect(self):
        return self.call(self._disconnect_async())

    def shutdown(self) -> None:
        try:
            self.disconnect().result(timeout=3)
        except Exception:  # noqa: BLE001 - 退出时尽力断开
            pass
        self._loop.call_soon_threadsafe(self._loop.stop)

    def _on_disconnected(self, _client) -> None:
        self._notify_state(False, "连接意外断开")

    async def _auto_reconnect(self) -> None:
        address = self.address or self.config.get("last_address", "")
        if not address:
            return
        for delay in (1.0, 2.0, 4.0, 8.0, 8.0):
            await asyncio.sleep(delay)
            try:
                await self._connect_async(address)
                return
            except Exception:  # noqa: BLE001 - 重连失败继续退避
                continue

    # ---------------- 数据 ----------------

    def _on_notify(self, _char, data: bytearray) -> None:
        frames = self.parser.feed(bytes(data))
        for frame in frames:
            if self.on_frame:
                self.on_frame(frame)

    async def _send_async(self, data: bytes) -> None:
        if self.client is None or not self._connected:
            raise ConnectionError("BLE 未连接")
        if not self.write_uuid:
            raise RuntimeError("未发现可写特征")
        try:
            # 实测（ECB00 + Windows）：无确认写会静默丢包且不报错，必须优先带确认写
            await self.client.write_gatt_char(self.write_uuid, data, response=True)
        except Exception as first:
            try:
                await self.client.write_gatt_char(self.write_uuid, data, response=False)
            except Exception as second:
                raise RuntimeError(
                    f"BLE 写入失败（写特征 {self.write_uuid}）：{second} / 首次：{first}"
                ) from second

    def send(self, data: bytes):
        return self.call(self._send_async(data))
