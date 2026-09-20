"""小白 BLE 无界面探针（开发调试用，不依赖 PySide6）。

作用：扫描/直连 → 枚举服务与特征 → 订阅所有可通知特征 → 逐个可写特征试发
QUERY_STATUS（判定哪个写特征真正到达设备）→ 用可用特征发 ENTER_PROGRAM 并持续心跳
→ 打印收到的全部 D2/D3 帧与解码结果。

用法（Windows Python）：
    python tools\\ble_probe.py                     # 扫描名字含 Spark
    python tools\\ble_probe.py --address AA:BB:..  # 直连地址
    python tools\\ble_probe.py --seconds 12        # 观察时长
"""
from __future__ import annotations

import argparse
import asyncio
import sys
import time

# Windows 控制台默认 GBK：强制 UTF-8，避免中文/符号打印崩溃
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception as exc:  # noqa: BLE001
    sys.stderr.write(f"stdout 设为 UTF-8 失败：{exc}\n")

from bleak import BleakClient, BleakScanner

HEAD, TAIL, DATA_LEN = 0x5A, 0xA5, 10
ADDR_APP, ADDR_DEV = 0x97, 0x98
TYPE_C1, TYPE_C2, TYPE_D2, TYPE_D3 = 0xC1, 0xC2, 0xD2, 0xD3

OP_ENTER_PROGRAM = 0x01
OP_ENTER_REMOTE = 0x02
OP_HEARTBEAT = 0x03
OP_QUERY_STATUS = 0x04
OP_STOP_PROGRAM = 0x05
OP_MOTOR_TIME = 0x10
OP_SHOW_EYE = 0x40
OP_SHOW_NUM = 0x41
OP_SHOW_OFF = 0x42
OP_PLAY_VOICE = 0x50

EVENT_NAMES = {
    0x01: "模式变化", 0x02: "程序中止", 0x03: "红外变化", 0x04: "唤醒",
    0x05: "休眠", 0x06: "低电量", 0x07: "协议错误",
}
MODE_NAMES = {0: "语音", 1: "动力", 2: "感应", 3: "遥控", 4: "编程"}


def build_c2(seq: int, opcode: int, args: bytes = b"") -> bytes:
    data = (bytes([seq, opcode]) + bytes(args)).ljust(DATA_LEN, b"\x00")[:DATA_LEN]
    body = bytes([HEAD, ADDR_APP, ADDR_DEV, DATA_LEN, TYPE_C2]) + data
    return body + bytes([sum(body) & 0xFF, TAIL])


def describe(frame_type: int, data: bytes) -> str:
    if frame_type == TYPE_D2:
        seq, opcode, result = data[0], data[1], data[2]
        if result != 0:
            return f"D2 seq={seq} op=0x{opcode:02X} 错误 result={result}"
        if opcode == OP_QUERY_STATUS:
            mv = data[7] | (data[8] << 8)
            return (f"D2 状态: 模式={MODE_NAMES.get(data[3], data[3])} "
                    f"红外={data[4]}/{data[5]}/{data[6]} 电池={mv}mV "
                    f"任务={(data[9] >> 4) & 0xF} 故障=0x{data[9] & 0xF:02X}")
        return f"D2 DONE seq={seq} op=0x{opcode:02X}"
    if frame_type == TYPE_D3:
        return f"D3 事件: {EVENT_NAMES.get(data[0], data[0])} counter={data[1]}"
    return f"TYPE=0x{frame_type:02X} {data.hex(' ').upper()}"


class Probe:
    def __init__(self) -> None:
        self.buf = bytearray()
        self.responses: asyncio.Queue = asyncio.Queue()

    def feed(self, _char, payload: bytearray) -> None:
        self.buf.extend(payload)
        while len(self.buf) >= 17:
            for i in range(len(self.buf) - 16):
                raw = bytes(self.buf[i:i + 17])
                if (raw[0] == HEAD and raw[1] == ADDR_DEV and raw[2] == ADDR_APP
                        and raw[3] == DATA_LEN and raw[16] == TAIL
                        and (sum(raw[:15]) & 0xFF) == raw[15]):
                    del self.buf[:i + 17]
                    self.responses.put_nowait((raw[4], raw[5:15]))
                    break
            else:
                del self.buf[: max(0, len(self.buf) - 16)]
                return

    async def drain(self, seconds: float, label: str) -> int:
        count = 0
        end = time.time() + seconds
        while time.time() < end:
            try:
                frame_type, data = await asyncio.wait_for(self.responses.get(), 0.2)
            except asyncio.TimeoutError:
                continue
            count += 1
            print(f"  << [{label}] {describe(frame_type, data)}")
        return count


async def run(address: str | None, seconds: float, do_enter: bool) -> int:
    if not address:
        print("扫描 6 秒（请确保设备已上电、且没有被手机/其它 App 占用）…")
        scan_seconds = seconds if seconds > 20 else max(8.0, seconds)
        devices = await BleakScanner.discover(timeout=scan_seconds, return_adv=True)
        found = []
        if isinstance(devices, dict):          # bleak >= 0.22: {address: (device, adv)}
            found = [adv_dev for adv_dev in devices.values()]
        else:
            found = [(d, None) for d in devices]
        for dev, adv in found:
            services = ""
            if adv is not None and getattr(adv, "service_uuids", None):
                short = [u[:8] for u in adv.service_uuids]
                services = " svc=" + ",".join(short)
            rssi = getattr(adv, "rssi", None) if adv is not None else getattr(dev, "rssi", None)
            print(f"  {(dev.name or '(无名)'):22} {dev.address}  rssi={rssi}{services}")
        target = next((dev for dev, _ in found if "spark" in (dev.name or "").lower()), None)
        if target is None:
            print("[X] 未找到名字含 Spark 的设备。请确认：")
            print("    1) 主控已上电且 BLE 模块工作（串口 AT+NAME 配置为 Spark_AI）")
            print("    2) 没有手机/其它 App 正连着它（已连接时不再广播）")
            print("    3) 如名称不同，用 --address XX:XX:.. 直连，或告诉我实际名称")
            return 2
        address = target.address
        print(f"选定 {target.name} {address}")

    probe = Probe()
    print(f"连接 {address} …")
    disconnected = asyncio.Event()

    def on_disconnect(_client) -> None:
        print("!! 连接断开")
        disconnected.set()

    async with BleakClient(address, disconnected_callback=on_disconnect, timeout=20.0) as client:
        print(f"已连接: {client.is_connected}")
        writable, notifiable = [], []
        print("--- 服务/特征 ---")
        for service in client.services:
            for ch in service.characteristics:
                props = ",".join(ch.properties)
                print(f"  {ch.uuid}  [{props}]")
                if "write" in ch.properties or "write-without-response" in ch.properties:
                    writable.append(ch.uuid)
                if "notify" in ch.properties or "indicate" in ch.properties:
                    notifiable.append(ch.uuid)

        for uuid in notifiable:
            try:
                await client.start_notify(uuid, probe.feed)
                print(f"已订阅通知: {uuid}")
            except Exception as exc:  # noqa: BLE001
                print(f"订阅失败 {uuid}: {exc}")

        print("--- 逐个可写特征试发 QUERY_STATUS（判定真正到达设备的特征）---")
        good = None
        for uuid in writable:
            for response in (False, True):
                try:
                    await client.write_gatt_char(uuid, build_c2(0, OP_QUERY_STATUS), response=response)
                    break
                except Exception as exc:  # noqa: BLE001
                    print(f"  {uuid} response={response} 写失败: {exc}")
            got = await probe.drain(1.2, uuid[:8])
            print(f"  {uuid} → 收到 {got} 帧")
            if got:
                good = uuid
                break
        if good is None:
            print("[X] 所有可写特征都没有收到任何回帧：确认设备固件为含 BLE v2 的版本")
        else:
            print(f"[OK] 可用写特征: {good}")

        if good and do_enter:
            print("--- 发 ENTER_PROGRAM（设备应四灯跑马 + 播报 ID 52）---")
            await client.write_gatt_char(good, build_c2(0, OP_ENTER_PROGRAM), response=False)
            await probe.drain(1.0, "enter")

        if good:
            print(f"--- 持续心跳 {seconds:.0f}s（否则 1s 后设备退出编程模式）---")
            end = time.time() + seconds
            while time.time() < end and not disconnected.is_set():
                for response in (False, True):
                    try:
                        await client.write_gatt_char(good, build_c2(0, OP_HEARTBEAT), response=response)
                        break
                    except Exception:  # noqa: BLE001
                        continue
                await probe.drain(0.3, "hb")
        await probe.drain(0.5, "tail")
    print("探针结束")
    return 0


async def hunt(limit: int, scan_seconds: float) -> int:
    """对信号最强的若干个 BLE 设备逐个连接并试发 QUERY_STATUS；
    谁按小白协议回帧，谁就是目标设备（用于设备名不是 Spark_AI / 不广播名字的情况）。"""
    print(f"猎手模式：扫描 {scan_seconds:.0f}s，取信号最强 {limit} 个候选逐个试探…")
    raw = await BleakScanner.discover(timeout=scan_seconds, return_adv=True)
    entries = []
    if isinstance(raw, dict):
        for dev, adv in raw.values():
            entries.append((dev, adv))
    else:
        entries = [(d, None) for d in raw]
    entries.sort(key=lambda e: -(getattr(e[1], "rssi", None) or -127))

    for dev, adv in entries[:limit]:
        rssi = getattr(adv, "rssi", None)
        print(f"--- 试探 {dev.name or '(无名)'} {dev.address} rssi={rssi} ---")
        probe = Probe()
        try:
            async with BleakClient(dev.address, timeout=10.0) as client:
                writable, notifiable = [], []
                for service in client.services:
                    for ch in service.characteristics:
                        if "write" in ch.properties or "write-without-response" in ch.properties:
                            writable.append(ch.uuid)
                        if "notify" in ch.properties or "indicate" in ch.properties:
                            notifiable.append(ch.uuid)
                for uuid in notifiable:
                    try:
                        await client.start_notify(uuid, probe.feed)
                    except Exception:  # noqa: BLE001
                        continue
                for uuid in writable:
                    for response in (False, True):
                        try:
                            await client.write_gatt_char(uuid, build_c2(0, OP_QUERY_STATUS), response=response)
                            break
                        except Exception:  # noqa: BLE001
                            continue
                    if await probe.drain(1.0, f"hunt:{uuid[:8]}"):
                        print(f"[FOUND] 目标设备 = {dev.address}（写特征 {uuid}）")
                        print(f"        中文名/名称: {dev.name!r}  服务: "
                              f"{','.join(u[:8] for s in client.services for u in [s.uuid])}")
                        return 0
        except Exception as exc:  # noqa: BLE001
            print(f"    连接失败：{type(exc).__name__}: {exc}")
        if probe.buf:
            print("    （有通知但组不成合法帧）")
    print("[X] 没有候选设备按协议回帧。可能原因：设备已断电 / 已被手机连着 / 不在范围内")
    return 3


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", default=None)
    parser.add_argument("--seconds", type=float, default=8.0)
    parser.add_argument("--no-enter", action="store_true")
    parser.add_argument("--hunt", type=int, default=0, help="猎手模式：试探信号最强的 N 个设备")
    args = parser.parse_args()
    try:
        if args.hunt:
            return asyncio.run(hunt(args.hunt, max(8.0, args.seconds)))
        return asyncio.run(run(args.address, args.seconds, not args.no_enter))
    except Exception as exc:  # noqa: BLE001
        print(f"探针异常：{type(exc).__name__}: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
