"""播报链路判决测试：一次连接依次发 ENTER_PROGRAM / PLAY_VOICE / SHOW_NUM / QUERY_STATUS，
用于区分「ASR 代码未更新」「语音资源未生成」「显示正常但语音不响」。

用法：python voice_path_test.py [--address XX:XX:..]   不带地址则扫描名字含 Spark 的设备
"""
import asyncio
import sys
import time

from bleak import BleakClient
import ble_probe as bp

WRITE = "0000fff2-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000fff1-0000-1000-8000-00805f9b34fb"


async def resolve_address(cli_address: str | None) -> str:
    if cli_address:
        return cli_address
    from bleak import BleakScanner
    print("未给地址，扫描 8s 找 Spark_AI …")
    devices = await BleakScanner.discover(timeout=8.0)
    for dev in devices:
        if "spark" in (dev.name or "").lower():
            print("找到", dev.name, dev.address)
            return dev.address
    raise SystemExit("[X] 没找到 Spark_AI：确认设备已开机且没有被手机/上位机连接")


async def main() -> int:
    cli = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else None
    address = await resolve_address(cli)
    probe = bp.Probe()
    t0 = time.time()
    async with BleakClient(address, timeout=20) as client:
        await client.start_notify(NOTIFY, probe.feed)

        async def send(seq, opcode, args, label):
            await client.write_gatt_char(WRITE, bp.build_c2(seq, opcode, args), response=True)
            print(f"  [t={time.time()-t0:4.1f}s] >> {label}")

        await send(0, bp.OP_ENTER_PROGRAM, b"", "ENTER_PROGRAM  → 期待：四灯跑马 + 语音 ID52「进入编程模式」")
        await asyncio.sleep(0.5)
        for item in (1, 2, 3):
            await send(item, bp.OP_PLAY_VOICE, bytes([item]),
                       f"PLAY_VOICE P0{item} → 期待：第 {item} 条词条")
            await asyncio.sleep(2.0)
        await send(4, bp.OP_SHOW_NUM, bytes([100]), "SHOW_NUM 100  → 期待：屏幕显示 100")
        await asyncio.sleep(0.5)
        await send(5, bp.OP_QUERY_STATUS, b"", "QUERY_STATUS  → 期待：回报模式=编程")

        end = time.time() + 8
        while time.time() < end:
            try:
                await client.write_gatt_char(WRITE, bp.build_c2(0, bp.OP_HEARTBEAT), response=True)
            except Exception as exc:  # noqa: BLE001
                print("  心跳写失败:", exc)
                break
            await probe.drain(0.3, "hb")
    print("测试结束")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(asyncio.run(main()))
    except Exception as exc:  # noqa: BLE001
        print("异常:", type(exc).__name__, exc)
        raise SystemExit(1)
