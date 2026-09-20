#!/usr/bin/env python3
"""从 docs/ble_v2_golden_frames.json 生成 tests/host/golden_frames.h。

C 宿主测试与 Python 测试共用同一份黄金帧来源，避免两侧硬编码漂移。
用法：
    python3 tools/gen_golden.py            # 生成
    python3 tools/gen_golden.py --check    # 只校验已生成文件是否最新（CI/门禁用）
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "ble_v2_golden_frames.json"
DST = ROOT / "tests" / "host" / "golden_frames.h"

HEADER = """/* 本文件由 tools/gen_golden.py 自动生成，请勿手改。
 * 来源：docs/ble_v2_golden_frames.json（协议契约 docs/BLE协议v2-编程模式.md §9）
 */
#ifndef GOLDEN_FRAMES_H
#define GOLDEN_FRAMES_H

#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *bytes;
    uint16_t len;
} GoldenFrame_t;

"""


def load_source() -> dict:
    """读取黄金帧来源；文件缺失/JSON 非法时给出明确错误退出。"""
    try:
        raw = SRC.read_text(encoding="utf-8")
    except OSError as exc:
        raise SystemExit(f"无法读取黄金帧来源 {SRC}: {exc}") from exc
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise SystemExit(f"黄金帧 JSON 解析失败 {SRC}: {exc}") from exc


def render() -> str:
    data = load_source()
    out = [HEADER]
    names = []
    for idx, item in enumerate(data["frames"]):
        raw = bytes.fromhex(item["hex"].replace(" ", ""))
        if len(raw) != 17:
            raise SystemExit(f"{item['name']} 长度不是 17 字节")
        var = f"k_golden_{idx}"
        body = ", ".join(f"0x{b:02X}" for b in raw)
        out.append(f"/* {item['name']}: {item['desc']} */\n")
        out.append(f"static const uint8_t {var}[17] = {{ {body} }};\n\n")
        names.append(f"    {{ \"{item['name']}\", {var}, 17 }},")
    out.append("#define GOLDEN_FRAME_COUNT %d\n\n" % len(names))
    out.append("static const GoldenFrame_t k_golden_frames[] = {\n")
    out.append("\n".join(names))
    out.append("\n};\n\n#endif /* GOLDEN_FRAMES_H */\n")
    return "".join(out)


def main() -> int:
    text = render()
    if "--check" in sys.argv:
        current = DST.read_text(encoding="utf-8") if DST.exists() else ""
        if current != text:
            print("golden_frames.h 不是最新：请运行 python3 tools/gen_golden.py", file=sys.stderr)
            return 1
        print("golden_frames.h OK")
        return 0
    DST.parent.mkdir(parents=True, exist_ok=True)
    DST.write_text(text, encoding="utf-8")
    print(f"已生成 {DST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
