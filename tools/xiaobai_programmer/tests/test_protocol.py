"""协议黄金帧/编解码/流解析测试（与 C 端 tests/host 共用契约）。"""
from __future__ import annotations

import json
from pathlib import Path

from xiaobai import protocol as P

# 模式编号（协议契约 docs/BLE协议v2-编程模式.md §6：0 语音 / 1 动力 / 2 感应 / 3 遥控 / 4 编程）
MODE_VOICE = 0
MODE_PROGRAM = 4

GOLDEN_JSON = (
    Path(__file__).resolve().parents[3] / "docs" / "ble_v2_golden_frames.json"
)


def _golden(name: str) -> bytes:
    return bytes.fromhex(P.GOLDEN_FRAMES[name].replace(" ", ""))


def test_golden_frames_match_contract():
    data = json.loads(GOLDEN_JSON.read_text(encoding="utf-8"))
    doc = {item["name"]: item["hex"] for item in data["frames"]}
    assert set(doc) == set(P.GOLDEN_FRAMES), "内嵌黄金帧与 docs 契约不一致"
    for name, hexstr in doc.items():
        assert hexstr.replace(" ", "").upper() == P.GOLDEN_FRAMES[name].replace(" ", "").upper()


def test_builders_match_golden_frames():
    assert P.build_c1([1, 0, 0, 0, 0, 0, 0, 0, 0, 0]) == _golden("C1_UP")
    assert P.build_c2(1, P.OP_MOTOR_TIME, bytes([0, 1, 0xE8, 0x03, 0, 0, 0, 0])) == _golden(
        "C2_MOTOR_TIME_LEFT_FWD_1000MS_SEQ1"
    )
    assert P.build_c2(0, P.OP_ENTER_PROGRAM) == _golden("C2_ENTER_PROGRAM_SEQ0")
    assert P.build_c2(7, P.OP_WAIT_IR, bytes([1, 0, 30, 0, 0, 0, 0, 0])) == _golden(
        "C2_WAIT_IR_CENTER_GT_30_SEQ7"
    )
    assert P.build_c2(9, P.OP_SHOW_NUM, bytes([100, 0, 0, 0, 0, 0, 0, 0])) == _golden(
        "C2_SHOW_NUM_100_SEQ9"
    )
    assert P.build_c2(10, P.OP_PLAY_VOICE, bytes([3, 0, 0, 0, 0, 0, 0, 0])) == _golden(
        "C2_PLAY_VOICE_P03_SEQ10"
    )
    assert P.build_d2(1, P.OP_MOTOR_TIME, P.RESULT_OK) == _golden("D2_DONE_SEQ1_OP_0X10")
    assert P.build_d3(P.EVT_MODE_CHANGE, 1, bytes([MODE_PROGRAM, 0])) == _golden(
        "D3_MODE_CHANGE_PROGRAM"
    )


def test_parse_frame_roundtrip_and_reject():
    raw = _golden("D2_DONE_SEQ1_OP_0X10")
    frame = P.parse_frame(raw)
    assert frame is not None
    assert frame.type == P.TYPE_D2
    assert frame.seq == 1 and frame.opcode == P.OP_MOTOR_TIME and frame.result == 0

    bad = bytearray(raw)
    bad[15] ^= 0xFF
    assert P.parse_frame(bytes(bad)) is None
    assert P.parse_frame(raw[:-1]) is None
    # App→设备方向：默认解析器拒绝，请求方向可解
    assert P.parse_frame(_golden("C2_ENTER_PROGRAM_SEQ0")) is None
    assert P.parse_frame(_golden("C2_ENTER_PROGRAM_SEQ0"), P.ADDR_APP, P.ADDR_DEV) is not None


def test_stream_parser_split_glue_noise():
    parser = P.StreamParser(P.ADDR_APP, P.ADDR_DEV)   # 解析 App→设备请求帧
    f1 = _golden("C1_UP")
    f2 = _golden("C2_ENTER_PROGRAM_SEQ0")

    assert parser.feed(f1[:8]) == []          # 分包
    frames = parser.feed(f1[8:])              # 补齐
    assert len(frames) == 1 and frames[0].type == P.TYPE_C1

    frames = parser.feed(b"\x00\x5a\x97" + f1 + f2)   # 噪声 + 粘包
    assert [f.type for f in frames] == [P.TYPE_C1, P.TYPE_C2]


def test_stream_parser_bad_checksum_resync():
    parser = P.StreamParser(P.ADDR_APP, P.ADDR_DEV)
    bad = bytearray(_golden("C2_MOTOR_TIME_LEFT_FWD_1000MS_SEQ1"))
    bad[15] ^= 0x01
    assert parser.feed(bytes(bad)) == []
    frames = parser.feed(_golden("C2_WAIT_IR_CENTER_GT_30_SEQ7"))
    assert len(frames) == 1 and frames[0].opcode == P.OP_WAIT_IR


def test_stream_parser_overflow_recovers():
    parser = P.StreamParser(P.ADDR_APP, P.ADDR_DEV)
    parser.feed(b"\x11" * 200)
    frames = parser.feed(_golden("C2_PLAY_VOICE_P03_SEQ10"))
    assert len(frames) == 1 and frames[0].opcode == P.OP_PLAY_VOICE


def test_device_status_from_golden():
    frame = P.parse_frame(_golden("D2_QUERY_STATUS"))
    assert frame is not None
    status = P.DeviceStatus.from_frame(frame)
    assert status.mode == MODE_VOICE
    assert (status.ir_left, status.ir_center, status.ir_right) == (10, 20, 30)
    assert status.battery_mv == 3900
    assert status.task == P.TASK_NONE and status.fault == 0


def test_sequence_allocator_cycles():
    alloc = P.SequenceAllocator()
    values = [alloc.next() for _ in range(256)]
    assert values[0] == 1 and values[254] == 255
    assert values[255] == 1          # 255 后回绕到 1
    assert alloc.next() == 2


def test_event_describe():
    frame = P.parse_frame(_golden("D3_MODE_CHANGE_PROGRAM"))
    assert frame is not None
    text = P.EventLog.from_frame(frame).describe()
    assert "编程模式" in text
