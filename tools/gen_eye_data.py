#!/usr/bin/env python3
"""构建期把 V0.2 眼睛动画 JSON 转换为固件静态列数据（User/Eye_Data.c/.h）。

映射规则（docs/BLE协议v2-编程模式.md、动画交付 README）：
- 单眼 7×7；JSON 行字符串 7 位，最左 LED = bit6，最右 = bit0；
- TM1640 每列 1 字节，bit0..bit6 = 第 0..6 行，bit7 恒 0；
- 左眼占 TM1640 列 0..6，右眼占列 7..13；列内左侧 LED 对应列 0。

用法：
    python3 tools/gen_eye_data.py            # 生成
    python3 tools/gen_eye_data.py --check    # 只检查已生成文件是否最新
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
JSON_PATH = ROOT / "docs" / "眼睛点阵动画V02" / "眼睛点阵动画V0.2-开发交付" / "V0.2动画帧.json"
DST_C = ROOT / "User" / "Eye_Data.c"
DST_H = ROOT / "User" / "Eye_Data.h"

TM_COLS = 14
EYE_COLS = 7

# 3×5 数字字库（行主序，'1'=亮）。0–100 无前导零，居中排布。
DIGITS = {
    "0": ["111", "101", "101", "101", "111"],
    "1": ["010", "110", "010", "010", "111"],
    "2": ["111", "001", "111", "100", "111"],
    "3": ["111", "001", "111", "001", "111"],
    "4": ["101", "101", "111", "001", "001"],
    "5": ["111", "100", "111", "001", "111"],
    "6": ["111", "100", "111", "101", "111"],
    "7": ["111", "001", "010", "010", "010"],
    "8": ["111", "101", "111", "101", "111"],
    "9": ["111", "101", "111", "001", "111"],
}


def load_animations() -> list[dict]:
    try:
        raw = JSON_PATH.read_text(encoding="utf-8")
    except OSError as exc:
        raise SystemExit(f"无法读取动画 JSON {JSON_PATH}: {exc}") from exc
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"动画 JSON 解析失败 {JSON_PATH}: {exc}") from exc

    anims = data.get("animations")
    if not isinstance(anims, list) or len(anims) != 10:
        raise SystemExit("动画 JSON 必须包含 10 个 animations 条目")
    return anims


def eye_columns(rows: list[str]) -> list[int]:
    """7×7（行字符串，最左=bit6）→ 7 个 TM1640 列字节（bit0..6=行0..6）。"""
    if len(rows) != 7:
        raise SystemExit(f"眼睛行数不是 7：{rows!r}")
    cols = []
    for c in range(EYE_COLS):          # c=0 为最左列
        byte = 0
        for r in range(7):             # r=0 为最上行
            row = rows[r]
            if len(row) != 7 or any(ch not in "01" for ch in row):
                raise SystemExit(f"行数据非法：{row!r}")
            if row[6 - c] == "1":      # 最左 LED = bit6
                byte |= 1 << r
        cols.append(byte & 0x7F)       # bit7 必须保持 0
    return cols


def as_int(value, what: str) -> int:
    """把 JSON 字段转 int；非法值时给出明确错误退出。"""
    try:
        return int(value)
    except (TypeError, ValueError) as exc:
        raise SystemExit(f"字段 {what} 不是整数：{value!r}") from exc


def frame_bytes(frame: dict) -> tuple[int, list[int]]:
    left = eye_columns(frame["left"])
    right = eye_columns(frame["right"])
    return as_int(frame["duration_ms"], "duration_ms"), left + right


def digit_columns() -> list[list[int]]:
    """3×5 点阵字库。点阵可用 7 行（bit0..bit6），5 行字模整体下移 1 行，
    在 bit1..bit5 垂直居中，与表情区的行分布对齐。"""
    out = []
    for d in "0123456789":
        rows = DIGITS[d]
        cols = []
        for c in range(3):
            byte = 0
            for r in range(5):
                if rows[r][c] == "1":
                    byte |= 1 << (r + 1)
            cols.append(byte)
        out.append(cols)
    return out


def render_header() -> str:
    return """/* 本文件由 tools/gen_eye_data.py 自动生成，请勿手改。
 * 来源：docs/眼睛点阵动画V02/.../V0.2动画帧.json（48 帧，10 组表情）。
 * 每列 1 字节：bit0..bit6 = 行 0..6，bit7 恒 0；列 0..6 左眼，列 7..13 右眼。 */
#ifndef EYE_DATA_H
#define EYE_DATA_H

#include <stdint.h>

#define EYE_ANIM_COUNT  10U
#define EYE_FRAME_COLS  14U

typedef struct {
    uint16_t ms;                   /* 该帧持续毫秒（来自 JSON duration_ms） */
    uint8_t  col[EYE_FRAME_COLS];  /* TM1640 列数据 */
} Eye_Frame_t;

typedef struct {
    const Eye_Frame_t *frames;
    uint8_t            count;
} Eye_Anim_t;

/* 索引 0..9 = EYE_01..EYE_10 */
extern const Eye_Anim_t g_eye_anims[EYE_ANIM_COUNT];

/* 3×5 数字字库：每数字 3 列，bit1..bit5 = 行 1..5（7 行点阵垂直居中） */
extern const uint8_t g_digit_cols[10][3];

#endif
"""


def render_source(anims: list[dict]) -> str:
    out = [
        "/* 本文件由 tools/gen_eye_data.py 自动生成，请勿手改。 */\n",
        '#include "Eye_Data.h"\n\n',
    ]
    frame_names = []
    entries = []
    for anim in anims:
        aid = as_int(anim["id"], "animations[].id")
        key = anim["key"]
        frames = anim["frames"]
        var = f"k_frames_eye_{aid:02d}_{key}"
        frame_names.append(var)
        entries.append((anim, var, aid))
        out.append(f"/* EYE_{aid:02d} {anim['name']}：{len(frames)} 帧 */\n")
        out.append(f"static const Eye_Frame_t {var}[] = {{\n")
        for f in frames:
            ms, cols = frame_bytes(f)
            body = ", ".join(f"0x{b:02X}" for b in cols)
            out.append(f"    {{ {ms:5d}, {{ {body} }} }},\n")
        out.append("};\n\n")

    out.append("const Eye_Anim_t g_eye_anims[EYE_ANIM_COUNT] = {\n")
    for anim, var, aid in entries:
        out.append(f"    {{ {var}, {len(anim['frames'])} }},   /* EYE_{aid:02d} */\n")
    out.append("};\n\n")

    out.append("/* 3×5 数字字库：每数字 3 列，bit1..bit5 = 行 1..5（7 行点阵垂直居中） */\n")
    out.append("const uint8_t g_digit_cols[10][3] = {\n")
    for idx, cols in enumerate(digit_columns()):
        body = ", ".join(f"0x{b:02X}" for b in cols)
        out.append(f"    {{ {body} }},   /* {idx} */\n")
    out.append("};\n")
    return "".join(out)


def main() -> int:
    anims = load_animations()
    header = render_header()
    source = render_source(anims)

    if "--check" in sys.argv:
        problems = []
        for path, text in ((DST_H, header), (DST_C, source)):
            try:
                current = path.read_text(encoding="utf-8")
            except OSError as exc:
                problems.append(f"{path.name} 读取失败: {exc}")
                continue
            if current != text:
                problems.append(f"{path.name} 不是最新")
        if problems:
            print("；".join(problems), file=sys.stderr)
            print("请运行 python3 tools/gen_eye_data.py", file=sys.stderr)
            return 1
        print("Eye_Data.c/.h OK")
        return 0

    DST_H.write_text(header, encoding="utf-8")
    DST_C.write_text(source, encoding="utf-8")
    total = sum(len(a["frames"]) for a in anims)
    print(f"已生成 User/Eye_Data.c/.h：{total} 帧 / {len(anims)} 组表情")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
