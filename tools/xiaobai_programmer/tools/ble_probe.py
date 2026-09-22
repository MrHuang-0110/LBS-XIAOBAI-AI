"""小白 BLE 无界面探针（开发调试用，不依赖 PySide6）。

作用：扫描/直连 → 枚举服务与特征 → 订阅所有可通知特征 → 逐个可写特征试发
QUERY_STATUS（判定哪个写特征真正到达设备）→ 用可用特征发 ENTER_PROGRAM 并持续心跳
→ 打印收到的全部 D2/D3 帧与解码结果。

用法（Windows Python）：
    python tools\\ble_probe.py                     # 扫描名字含 Spark
    python tools\\ble_probe.py --address AA:BB:..  # 直连地址
    python tools\\ble_probe.py --seconds 12        # 观察时长
    python tools\\ble_probe.py --latency --samples 200 --save baseline.json --label baseline
                                                   # 延时诊断：空闲/心跳/轮询/混合四场景
"""
from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Optional

# Windows 控制台默认 GBK：强制 UTF-8，避免中文/符号打印崩溃
try:
    _reconfigure = getattr(sys.stdout, "reconfigure", None)
    if _reconfigure is not None:
        _reconfigure(encoding="utf-8", errors="replace")
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
        event, counter, d = data[0], data[1], data[2:]
        name = EVENT_NAMES.get(event, event)
        if event == 0x01 and len(d) >= 2:            # 模式变化
            return (f"D3 {name}: {MODE_NAMES.get(d[1], d[1])} → {MODE_NAMES.get(d[0], d[0])} "
                    f"(counter={counter})")
        if event == 0x02 and len(d) >= 2:            # 程序中止
            return f"D3 {name}: 原因={d[0]} 目标={MODE_NAMES.get(d[1], d[1])} (counter={counter})"
        if event == 0x03 and len(d) >= 4:            # 红外变化
            return f"D3 {name}: {d[0]}/{d[1]}/{d[2]} 掩码=0x{d[3]:02X} (counter={counter})"
        if event == 0x07 and len(d) >= 1:
            return f"D3 {name}: code={d[0]} (counter={counter})"
        return f"D3 {name}: {d.hex(' ').upper()} (counter={counter})"
    return f"TYPE=0x{frame_type:02X} {data.hex(' ').upper()}"


T0 = time.time()


def stamp() -> str:
    return f"[t={time.time() - T0:5.1f}s]"


class Probe:
    def __init__(self) -> None:
        self.buf = bytearray()
        self.responses: asyncio.Queue = asyncio.Queue()
        # 延时诊断用：每解析出一帧额外回调（帧类型, DATA[10]）
        self.on_frame: Optional[Callable[[int, bytes], None]] = None

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
                    if self.on_frame is not None:
                        self.on_frame(raw[4], raw[5:15])
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
            print(f"  {stamp()} << [{label}] {describe(frame_type, data)}")
        return count


def _percentile(values: list[float], percent: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = round((percent / 100.0) * (len(ordered) - 1))
    return ordered[max(0, min(len(ordered) - 1, index))]


def _summarize(values: list[float]) -> dict:
    return {
        "min": round(_percentile(values, 0), 2),
        "p50": round(_percentile(values, 50), 2),
        "p95": round(_percentile(values, 95), 2),
        "max": round(_percentile(values, 100), 2),
        "n": len(values),
    }


@dataclass
class LatencySample:
    queue_wait_ms: float   # 从提交到拿到写锁（真正的软件排队；验收看这个）
    guard_wait_ms: float   # 拿到锁后等待 30ms 收发保护窗（协议必需，非排队）
    write_ms: float        # write_gatt_char 调用耗时
    rtt_ms: float          # 提交到匹配 D2 响应
    ok: bool               # 是否在超时内收到匹配响应


class LatencyStats:
    def __init__(self, scenario: str) -> None:
        self.scenario = scenario
        self.samples: list[LatencySample] = []
        self.heartbeats = 0
        self.polls = 0
        self.polls_lost = 0

    def add(self, sample: LatencySample) -> None:
        self.samples.append(sample)

    def report(self) -> dict:
        ok = [s for s in self.samples if s.ok]
        return {
            "scenario": self.scenario,
            "samples": len(self.samples),
            "ok": len(ok),
            "lost": len(self.samples) - len(ok),
            "queue_wait_ms": _summarize([s.queue_wait_ms for s in ok]),
            "guard_wait_ms": _summarize([s.guard_wait_ms for s in ok]),
            "write_ms": _summarize([s.write_ms for s in ok]),
            "rtt_ms": _summarize([s.rtt_ms for s in ok]),
            "heartbeats": self.heartbeats,
            "polls": self.polls,
            "polls_lost": self.polls_lost,
        }


class LatencyChannel:
    """模拟上位机 BLE 传输：写串行化 + 30ms 保护窗 + 响应匹配。

    只发送进入编程/心跳/状态查询，不驱动任何电机。"""

    def __init__(self, client, write_uuid: str,
                 guard: float = 0.030, notify_guard: float = 0.030) -> None:
        self.client = client
        self.write_uuid = write_uuid
        self.guard = guard
        self.notify_guard = notify_guard
        self.lock = asyncio.Lock()
        self.last_write = 0.0
        self.last_notify = 0.0
        self.last_foreground = 0.0   # 最近一次前台请求完成时刻（自适应心跳用）
        self.last_hb = 0.0
        self.pending: dict[int, asyncio.Future] = {}
        self.seq = 0

    def next_seq(self) -> int:
        self.seq = self.seq % 255 + 1
        return self.seq

    def on_frame(self, frame_type: int, data: bytes) -> None:
        self.last_notify = time.monotonic()
        if frame_type != TYPE_D2 or len(data) < 2:
            return
        future = self.pending.get(data[0])
        if future is not None and not future.done():
            future.set_result(data)

    async def _locked_write(self, payload: bytes,
                            submitted: float) -> tuple[float, float, float]:
        """返回 (queue_wait_ms, guard_wait_ms, write_ms)；queue_wait 从 submitted 算起。"""
        lock_start = time.monotonic()
        async with self.lock:
            acquired = time.monotonic()
            deadline = max(self.last_write + self.guard,
                           self.last_notify + self.notify_guard)
            delay = deadline - time.monotonic()
            if delay > 0:
                await asyncio.sleep(delay)
            start = time.monotonic()
            await self.client.write_gatt_char(self.write_uuid, payload, response=True)
            done = time.monotonic()
            self.last_write = done
        return ((acquired - lock_start) * 1000.0,
                (start - acquired) * 1000.0,
                (done - start) * 1000.0)

    async def request(self, opcode: int, args: bytes = b"",
                      timeout: float = 3.0, foreground: bool = True) -> LatencySample:
        seq = self.next_seq()
        payload = build_c2(seq, opcode, args)
        future = asyncio.get_running_loop().create_future()
        self.pending[seq] = future
        submitted = time.monotonic()
        queue_wait = 0.0
        guard_wait = 0.0
        write_ms = 0.0
        ok = False
        try:
            queue_wait, guard_wait, write_ms = await self._locked_write(payload, submitted)
            await asyncio.wait_for(future, timeout)
            ok = True
        except asyncio.TimeoutError:
            ok = False
        finally:
            self.pending.pop(seq, None)
        rtt = (time.monotonic() - submitted) * 1000.0
        if foreground:
            self.last_foreground = time.monotonic()
        return LatencySample(queue_wait, guard_wait, write_ms, rtt, ok)

    def heartbeat_due(self, adaptive: bool, interval: float, max_interval: float) -> bool:
        """adaptive = 新上位机策略：前台刚发过则跳过冗余心跳，但最长不超 max_interval。"""
        if not adaptive:
            return True
        now = time.monotonic()
        since_fg = now - self.last_foreground
        since_hb = now - self.last_hb
        if since_hb >= max_interval:
            return True
        return since_fg >= interval and since_hb >= interval

    async def heartbeat_loop(self, stats: LatencyStats, interval: float,
                             stop: asyncio.Event, adaptive: bool = True,
                             max_interval: float = 0.6) -> None:
        self.last_hb = time.monotonic()
        while not stop.is_set():
            if adaptive:
                await asyncio.sleep(0.05)      # 睡眠式循环：卡顿后不补发积压心跳
                if not self.heartbeat_due(True, interval, max_interval):
                    continue
            begin = time.monotonic()
            try:
                _ = await self._locked_write(build_c2(0, OP_HEARTBEAT), begin)
                stats.heartbeats += 1
                self.last_hb = time.monotonic()
            except Exception as exc:  # noqa: BLE001 - 记录后台写失败但不中断采样
                print(f"  心跳写失败：{exc}")
            if not adaptive:
                await asyncio.sleep(max(0.0, interval - (time.monotonic() - begin)))

    async def poll_loop(self, stats: LatencyStats, interval: float,
                        stop: asyncio.Event, timeout: float) -> None:
        while not stop.is_set():
            begin = time.monotonic()
            sample = await self.request(OP_QUERY_STATUS, timeout=timeout, foreground=False)
            stats.polls += 1
            if not sample.ok:
                stats.polls_lost += 1
            await asyncio.sleep(max(0.0, interval - (time.monotonic() - begin)))


@dataclass
class LatencyOptions:
    samples: int = 200
    hb_ms: float = 300.0
    poll_ms: float = 1000.0
    timeout: float = 3.0
    save: str = ""
    label: str = ""
    bg_policy: str = "adaptive"   # adaptive = 新策略；legacy = 优化前基线


async def _sample_scenario(channel: LatencyChannel, probe: Probe, scenario: str,
                           opts: LatencyOptions, heartbeat: bool = False,
                           poll: bool = False) -> LatencyStats:
    stats = LatencyStats(scenario)
    stop = asyncio.Event()
    tasks: list[asyncio.Task] = []
    if heartbeat:
        tasks.append(asyncio.create_task(channel.heartbeat_loop(
            stats, opts.hb_ms / 1000.0, stop, adaptive=opts.bg_policy != "legacy")))
    if poll:
        tasks.append(asyncio.create_task(
            channel.poll_loop(stats, opts.poll_ms / 1000.0, stop, opts.timeout)))
    try:
        for i in range(opts.samples):
            sample = await channel.request(OP_QUERY_STATUS, timeout=opts.timeout)
            stats.add(sample)
            if not sample.ok:
                print(f"  [{scenario}] {i + 1}/{opts.samples} 丢帧（等待响应超时）")
            elif (i + 1) % 25 == 0:
                print(f"  [{scenario}] {i + 1}/{opts.samples} 完成")
    finally:
        stop.set()
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
    return stats


def _print_report(report: dict) -> None:
    queue = report["queue_wait_ms"]
    guard = report["guard_wait_ms"]
    write = report["write_ms"]
    rtt = report["rtt_ms"]
    # 验收只看软件排队（保护窗是协议必需开销，不计入）
    verdict = "PASS" if (
        report["lost"] == 0 and queue["p95"] <= 20.0 and queue["max"] <= 40.0
    ) else "FAIL"
    print(f"  {report['scenario']:<16} samples={report['samples']} ok={report['ok']} "
          f"lost={report['lost']} hb={report['heartbeats']} "
          f"poll={report['polls']}(丢{report['polls_lost']}) [{verdict}]")
    for name, values in (("queue_wait", queue), ("guard_wait", guard),
                         ("write", write), ("rtt", rtt)):
        print(f"      {name:<10} ms: min={values['min']:.2f} p50={values['p50']:.2f} "
              f"p95={values['p95']:.2f} max={values['max']:.2f}")


async def run_latency(client, probe: Probe, write_uuid: str, address: str,
                      opts: LatencyOptions) -> int:
    channel = LatencyChannel(client, write_uuid)
    probe.on_frame = channel.on_frame
    print(f"--- 延时诊断：每场景 {opts.samples} 次 QUERY_STATUS（无电机动作）；"
          f"后台策略={opts.bg_policy} ---")

    # 1) 空闲链路：尚未进入编程模式、无任何后台写
    reports = [(await _sample_scenario(channel, probe, "idle", opts)).report()]
    # 2) 进入编程模式：后续场景都保持 300ms 心跳（与上位机一致）
    _ = await channel._locked_write(build_c2(0, OP_ENTER_PROGRAM), time.monotonic())
    await asyncio.sleep(0.2)
    reports.append((await _sample_scenario(
        channel, probe, "heartbeat_300ms", opts, heartbeat=True)).report())
    # 3) 1s 状态轮询并发（心跳照常，模拟状态页）
    reports.append((await _sample_scenario(
        channel, probe, "poll_1s", opts, heartbeat=True, poll=True)).report())
    # 4) 最坏情况：心跳 + 200ms 通知/查询压力 + 前台请求同时出现
    mixed = LatencyOptions(samples=opts.samples, hb_ms=opts.hb_ms, poll_ms=200.0,
                           timeout=opts.timeout, save=opts.save, label=opts.label,
                           bg_policy=opts.bg_policy)
    reports.append((await _sample_scenario(
        channel, probe, "mixed_200ms", mixed, heartbeat=True, poll=True)).report())

    print("\n--- 延时统计（ms，仅成功样本）---")
    for report in reports:
        _print_report(report)
    if opts.save:
        payload = {
            "label": opts.label,
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "address": address,
            "samples_per_scenario": opts.samples,
            "hb_ms": opts.hb_ms,
            "poll_ms": opts.poll_ms,
            "bg_policy": opts.bg_policy,
            "guard_ms": 30,
            "scenarios": reports,
        }
        Path(opts.save).write_text(
            json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"\n结果已保存：{opts.save}")
    return 0


async def run(address: str | None, seconds: float, do_enter: bool, idle: bool = False,
              latency: LatencyOptions | None = None) -> int:
    if not address:
        print("扫描 6 秒（请确保设备已上电、且没有被手机/其它 App 占用）…")
        scan_seconds = seconds if seconds > 20 else max(8.0, seconds)
        devices: Any = await BleakScanner.discover(timeout=scan_seconds, return_adv=True)
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

    assert address is not None      # 扫描分支已选定 target，此处地址必为 str
    probe = Probe()
    print(f"连接 {address} …")
    disconnected = asyncio.Event()

    def on_disconnect(_client) -> None:
        print(f"  {stamp()} !! 连接断开（link loss）")
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
            for response in (True, False):
                try:
                    await client.write_gatt_char(uuid, build_c2(0, OP_QUERY_STATUS), response=response)
                    print(f"  {uuid[:8]} 试写 OK (response={response})")
                    break
                except Exception as exc:  # noqa: BLE001
                    print(f"  {uuid[:8]} response={response} 写失败: {exc}")
            got = await probe.drain(1.2, uuid[:8])
            print(f"  {uuid} → 收到 {got} 帧")
            if got:
                good = uuid
                break
        if good is None:
            print("[X] 所有可写特征都没有收到任何回帧：确认设备固件为含 BLE v2 的版本")
        else:
            print(f"[OK] 可用写特征: {good}")

        async def write_frame(uuid: str, payload: bytes, label: str = "") -> None:
            """带确认写优先（response=True）：无确认写在本模块上会静默丢包。"""
            errors = []
            for response in (True, False):
                try:
                    await client.write_gatt_char(uuid, payload, response=response)
                    if label:
                        print(f"  {stamp()} >> 写 {label} OK (response={response})")
                    return
                except Exception as exc:  # noqa: BLE001
                    errors.append(f"response={response}: {exc}")
            print(f"  {stamp()} >> 写 {label or payload.hex(' ')} 失败：{errors}")

        async def heartbeat_loop(uuid: str, stop: asyncio.Event) -> None:
            # 心跳必须紧跟 ENTER_PROGRAM 开始：固件只有 1000ms 宽限
            while not stop.is_set():
                await write_frame(uuid, build_c2(0, OP_HEARTBEAT))
                await asyncio.sleep(0.3)

        if good and latency is not None:
            return await run_latency(client, probe, good, address, latency)

        if good and do_enter:
            print("--- 发 ENTER_PROGRAM（设备应四灯跑马 + 播报 ID 52）---")
            await write_frame(good, build_c2(0, OP_ENTER_PROGRAM), "ENTER_PROGRAM")

        if good and idle:
            print(f"--- 空闲观察 {seconds:.0f}s（只订阅通知，不写任何数据）---")
            await probe.drain(seconds, "idle")
            if not disconnected.is_set():
                print(f"  {stamp()} 空闲期间链路保持")
        elif good:
            print(f"--- 持续 {seconds:.0f}s：300ms 心跳 + 通知观察 ---")
            stop = asyncio.Event()
            hb_task = asyncio.create_task(heartbeat_loop(good, stop))
            await probe.drain(seconds / 2, "run")
            # 运行中查一次状态，确认设备仍在编程模式
            await write_frame(good, build_c2(0, OP_QUERY_STATUS), "QUERY_STATUS")
            await probe.drain(seconds / 2, "run")
            stop.set()
            await hb_task
            if not disconnected.is_set():
                print(f"  {stamp()} 全程链路保持（编程模式未被心跳超时踢出）")
        await probe.drain(0.5, "tail")
    print("探针结束")
    return 0


async def hunt(limit: int, scan_seconds: float) -> int:
    """对信号最强的若干个 BLE 设备逐个连接并试发 QUERY_STATUS；
    谁按小白协议回帧，谁就是目标设备（用于设备名不是 Spark_AI / 不广播名字的情况）。"""
    print(f"猎手模式：扫描 {scan_seconds:.0f}s，取信号最强 {limit} 个候选逐个试探…")
    raw: Any = await BleakScanner.discover(timeout=scan_seconds, return_adv=True)
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
    parser.add_argument("--idle", action="store_true", help="连接后不写数据，只观察链路稳定性")
    parser.add_argument("--latency", action="store_true",
                        help="延时诊断：空闲/心跳/轮询/混合四场景统计（无电机动作）")
    parser.add_argument("--samples", type=int, default=200, help="延时诊断每场景采样次数")
    parser.add_argument("--hb-ms", type=float, default=300.0, help="延时诊断心跳周期（ms）")
    parser.add_argument("--poll-ms", type=float, default=1000.0, help="延时诊断状态轮询周期（ms）")
    parser.add_argument("--timeout", type=float, default=3.0, help="延时诊断单次响应等待（s）")
    parser.add_argument("--save", default="", help="把延时统计写入 JSON 文件，便于前后对比")
    parser.add_argument("--label", default="", help="统计标签（如 baseline/optimized）")
    parser.add_argument("--bg-policy", choices=("adaptive", "legacy"), default="adaptive",
                        help="后台心跳策略：adaptive=前台刚发过则跳过冗余心跳（新）；legacy=固定 300ms（基线）")
    args = parser.parse_args()
    try:
        if args.hunt:
            return asyncio.run(hunt(args.hunt, max(8.0, args.seconds)))
        latency = None
        if args.latency:
            latency = LatencyOptions(samples=max(1, args.samples), hb_ms=args.hb_ms,
                                     poll_ms=args.poll_ms, timeout=args.timeout,
                                     save=args.save, label=args.label,
                                     bg_policy=args.bg_policy)
        return asyncio.run(run(args.address, args.seconds, not args.no_enter, args.idle,
                               latency))
    except Exception as exc:  # noqa: BLE001
        print(f"探针异常：{type(exc).__name__}: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
